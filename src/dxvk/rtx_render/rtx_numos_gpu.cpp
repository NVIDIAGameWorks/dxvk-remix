/*
* Copyright (c) 2026, Kim2091
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <algorithm>
#include <cstring>

#include "rtx_numos_gpu.h"
#include "../dxvk_device.h"
#include "../dxvk_shader.h"
#include "../../util/log/log.h"

namespace dxvk {

  namespace {
    using numos::BindingDim;
    using numos::BindingKind;
    using numos::GpuHandle;

    // Host resources live at fixed handles far above anything m_nextHandle reaches.
    constexpr GpuHandle kExternalAccel = 1ull << 62;
    constexpr GpuHandle kExternalViewBase = kExternalAccel + 1;
    constexpr size_t kBufferTailPad = 256;
    constexpr size_t kConstantsRingBytes = 1 << 20;
    constexpr size_t kPlaceholderBufferBytes = 65536;

    VkFormat toVkFormat(numos::Format format) {
      switch (format) {
      case numos::Format::R8Unorm:     return VK_FORMAT_R8_UNORM;
      case numos::Format::RGBA8Unorm:  return VK_FORMAT_R8G8B8A8_UNORM;
      case numos::Format::R16Float:    return VK_FORMAT_R16_SFLOAT;
      case numos::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
      case numos::Format::R32Uint:     return VK_FORMAT_R32_UINT;
      case numos::Format::R32Float:    return VK_FORMAT_R32_SFLOAT;
      case numos::Format::RG32Float:   return VK_FORMAT_R32G32_SFLOAT;
      case numos::Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case numos::Format::RG16Float:   return VK_FORMAT_R16G16_SFLOAT;
      }
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    // Maps the SPIR-V format of a storage image to a format for its placeholder.
    VkFormat spirvToVkFormat(uint32_t spirvFormat) {
      switch (spirvFormat) {
      case 1:  return VK_FORMAT_R32G32B32A32_SFLOAT; // Rgba32f
      case 2:  return VK_FORMAT_R16G16B16A16_SFLOAT; // Rgba16f
      case 3:  return VK_FORMAT_R32_SFLOAT;          // R32f
      case 4:  return VK_FORMAT_R8G8B8A8_UNORM;      // Rgba8
      case 6:  return VK_FORMAT_R32G32_SFLOAT;       // Rg32f
      case 7:  return VK_FORMAT_R16G16_SFLOAT;       // Rg16f
      case 9:  return VK_FORMAT_R16_SFLOAT;          // R16f
      case 15: return VK_FORMAT_R8_UNORM;            // R8
      case 33: return VK_FORMAT_R32_UINT;            // R32ui
      default: return VK_FORMAT_R16G16B16A16_SFLOAT; // Any float format reads as zero.
      }
    }

    VkImageViewType toViewType(BindingDim dim) {
      switch (dim) {
      case BindingDim::Tex3D:      return VK_IMAGE_VIEW_TYPE_3D;
      case BindingDim::Tex2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      case BindingDim::Tex2D:      return VK_IMAGE_VIEW_TYPE_2D;
      case BindingDim::None:       break;
      }
      return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
    }

    BindingDim dimOf(const numos::TextureDesc& desc) {
      switch (desc.type) {
      case numos::TextureType::Tex3D:      return BindingDim::Tex3D;
      case numos::TextureType::Tex2DArray: return BindingDim::Tex2DArray;
      case numos::TextureType::Tex2D:      break;
      }
      return BindingDim::Tex2D;
    }

    DxvkResourceSlot toSlot(const numos::PassBindingInfo& b) {
      const VkImageViewType view = toViewType(b.dim);
      switch (b.kind) {
      case BindingKind::ConstantBuffer:         return { b.slot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER };
      case BindingKind::SampledTexture:         return { b.slot, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, view };
      case BindingKind::StorageTexture:         return { b.slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, view, VK_ACCESS_SHADER_WRITE_BIT };
      case BindingKind::Sampler:                return { b.slot, VK_DESCRIPTOR_TYPE_SAMPLER };
      case BindingKind::CombinedTextureSampler: return { b.slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, view };
      case BindingKind::StructuredBuffer:       return { b.slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
      case BindingKind::RWStructuredBuffer:     return { b.slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_IMAGE_VIEW_TYPE_MAX_ENUM, VK_ACCESS_SHADER_WRITE_BIT };
      case BindingKind::AccelerationStructure:  return { b.slot, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };
      }
      return { b.slot };
    }

    VkExtent3D mipExtent(const numos::TextureDesc& desc, uint32_t mip) {
      return { std::max(desc.width >> mip, 1u), std::max(desc.height >> mip, 1u),
               desc.type == numos::TextureType::Tex3D ? std::max(desc.depth >> mip, 1u) : 1u };
    }

    uint32_t layerCount(const numos::TextureDesc& desc) {
      return desc.type == numos::TextureType::Tex2DArray ? desc.depth : 1u;
    }

    Rc<DxvkImage> makeImage(DxvkDevice* pDevice, VkImageType type, VkFormat format, VkExtent3D extent,
                            uint32_t layers, uint32_t mips, const char* name) {
      DxvkImageCreateInfo info;
      info.type = type;
      info.flags = 0;
      info.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      info.extent = extent;
      info.numLayers = layers;
      info.mipLevels = mips;
      info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      // The transfer stage lets DXVK's barrier after a clear or upload order the next transfer too.
      info.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      info.tiling = VK_IMAGE_TILING_OPTIMAL;
      info.layout = VK_IMAGE_LAYOUT_UNDEFINED;
      info.format = format;
      return pDevice->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, name);
    }

    Rc<DxvkImageView> makeView(DxvkDevice* pDevice, const Rc<DxvkImage>& image, VkImageViewType type, uint32_t minLevel,
                               uint32_t numLevels, bool storage) {
      DxvkImageViewCreateInfo info;
      info.type = type;
      info.format = image->info().format;
      info.usage = storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
      info.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      info.minLevel = minLevel;
      info.numLevels = numLevels;
      info.minLayer = 0;
      info.numLayers = image->info().numLayers;
      return pDevice->createImageView(image, info);
    }

    VkImageSubresourceRange wholeRange(const Rc<DxvkImage>& image) {
      VkImageSubresourceRange range = {};
      range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      range.levelCount = image->info().mipLevels;
      range.layerCount = image->info().numLayers;
      return range;
    }
  }

  NumosGpu::NumosGpu(DxvkDevice* pDevice, std::string spirvDirectory)
    : m_pDevice(pDevice)
    , m_spirvDirectory(std::move(spirvDirectory))
    , m_passes(numos::passCount()) {
    const VkDeviceSize align = pDevice->properties().core.properties.limits.minUniformBufferOffsetAlignment;
    m_constantsAlign = std::max<size_t>(m_constantsAlign, size_t(align));
  }

  NumosGpu::~NumosGpu() = default;

  void NumosGpu::beginFrame(const Rc<DxvkContext>& ctx) {
    m_ctx = ctx;
    m_constantsOffset = 0;
    for (GpuHandle h : m_retiredConstants) {
      m_resources.erase(h);
    }
    m_retiredConstants.clear();
  }

  GpuHandle NumosGpu::setExternalAccel(const Rc<DxvkAccelStructure>& accel) {
    if (accel == nullptr) {
      m_resources.erase(kExternalAccel);
      return 0;
    }
    Resource& r = m_resources[kExternalAccel];
    r.kind = Resource::Kind::Accel;
    r.accel = accel;
    return kExternalAccel;
  }

  GpuHandle NumosGpu::setExternalView(uint32_t index, const Rc<DxvkImageView>& view) {
    const GpuHandle handle = kExternalViewBase + index;
    if (view == nullptr) {
      m_resources.erase(handle);
      return 0;
    }
    Resource& r = m_resources[handle];
    r.kind = Resource::Kind::ExternalView;
    r.externalView = view;
    return handle;
  }

  NumosGpu::Resource* NumosGpu::find(GpuHandle handle) {
    auto it = m_resources.find(handle);
    return it == m_resources.end() ? nullptr : &it->second;
  }

  const NumosGpu::Resource* NumosGpu::find(GpuHandle handle) const {
    auto it = m_resources.find(handle);
    return it == m_resources.end() ? nullptr : &it->second;
  }

  GpuHandle NumosGpu::add(Resource&& resource) {
    const GpuHandle handle = m_nextHandle++;
    m_resources.emplace(handle, std::move(resource));
    return handle;
  }

  Rc<DxvkImageView> NumosGpu::getView(GpuHandle texture) const {
    const Resource* pResource = find(texture);
    if (pResource == nullptr) {
      return nullptr;
    }
    if (pResource->kind == Resource::Kind::ExternalView) {
      return pResource->externalView;
    }
    return pResource->kind == Resource::Kind::Texture ? pResource->texture.view : nullptr;
  }

  Rc<DxvkImage> NumosGpu::getImage(GpuHandle texture) const {
    const Resource* pResource = find(texture);
    return pResource && pResource->kind == Resource::Kind::Texture ? pResource->texture.image : nullptr;
  }

  Rc<DxvkBuffer> NumosGpu::getBuffer(GpuHandle buffer) const {
    const Resource* pResource = find(buffer);
    return pResource && pResource->kind == Resource::Kind::Buffer ? pResource->buffer.buffer : nullptr;
  }

  Rc<DxvkSampler> NumosGpu::getSampler(GpuHandle sampler) const {
    const Resource* pResource = find(sampler);
    return pResource && pResource->kind == Resource::Kind::Sampler ? pResource->sampler : nullptr;
  }

  GpuHandle NumosGpu::createTexture(const numos::TextureDesc& desc) {
    Resource r;
    r.kind = Resource::Kind::Texture;
    r.texture.desc = desc;
    r.texture.name = std::string("NUMOS ") + (desc.debugName ? desc.debugName : "texture");
    const bool is3D = desc.type == numos::TextureType::Tex3D;
    r.texture.image = makeImage(m_pDevice, is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D, toVkFormat(desc.format),
                                { desc.width, desc.height, is3D ? desc.depth : 1u }, layerCount(desc),
                                std::max(desc.mipLevels, 1u), r.texture.name.c_str());
    r.texture.view = makeView(m_pDevice, r.texture.image, toViewType(dimOf(desc)), 0, std::max(desc.mipLevels, 1u), false);
    m_ctx->changeImageLayout(r.texture.image, VK_IMAGE_LAYOUT_GENERAL);
    return add(std::move(r));
  }

  GpuHandle NumosGpu::createBuffer(const numos::BufferDesc& desc) {
    DxvkBufferCreateInfo info;
    info.size = align(desc.size + kBufferTailPad, 256);
    info.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkMemoryPropertyFlags memory = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    switch (desc.usage) {
    case numos::BufferUsage::Constant:
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      info.access = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case numos::BufferUsage::Structured:
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      info.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    case numos::BufferUsage::Readback:
      info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT;
      memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
    }
    const std::string name = std::string("NUMOS ") + (desc.debugName ? desc.debugName : "buffer");
    Resource r;
    r.kind = Resource::Kind::Buffer;
    r.buffer.desc = desc;
    r.buffer.buffer = m_pDevice->createBuffer(info, memory, DxvkMemoryStats::Category::RTXBuffer, name.c_str());
    if (desc.usage != numos::BufferUsage::Readback) {
      m_ctx->clearBuffer(r.buffer.buffer, 0, info.size, 0);
    }
    return add(std::move(r));
  }

  GpuHandle NumosGpu::createSampler(const numos::SamplerDesc& desc) {
    auto filter = [](numos::Filter f) { return f == numos::Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; };
    auto address = [](numos::AddressMode a) {
      return a == numos::AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    };
    DxvkSamplerCreateInfo info = {};
    info.magFilter = filter(desc.filter);
    info.minFilter = filter(desc.filter);
    info.mipmapMode = desc.mipFilter == numos::Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.mipmapLodBias = 0.0f;
    info.mipmapLodMin = 0.0f;
    info.mipmapLodMax = desc.maxLod >= numos::SamplerDesc::kLodClampNone ? VK_LOD_CLAMP_NONE : desc.maxLod;
    info.useAnisotropy = VK_FALSE;
    info.maxAnisotropy = 1.0f;
    info.addressModeU = address(desc.addressU);
    info.addressModeV = address(desc.addressV);
    info.addressModeW = address(desc.addressW);
    info.compareToDepth = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.borderColor = VkClearColorValue {};
    info.usePixelCoord = VK_FALSE;
    Resource r;
    r.kind = Resource::Kind::Sampler;
    r.sampler = m_pDevice->createSampler(info);
    return add(std::move(r));
  }

  void NumosGpu::destroy(GpuHandle resource) {
    // Dropping the reference is enough, because DXVK keeps the object alive while recorded work uses it.
    if (resource != 0 && resource != m_constantsHandle) {
      m_resources.erase(resource);
    }
  }

  numos::IGpu::ConstantsRegion NumosGpu::uploadConstants(const void* pData, size_t size) {
    const size_t offset = align(m_constantsOffset, m_constantsAlign);
    if (m_constants == nullptr || offset + size + kBufferTailPad > m_constants->info().size) {
      // Regions handed out earlier this frame keep the old handle, so a new ring gets a new one.
      const size_t bytes = std::max(kConstantsRingBytes, align(size + kBufferTailPad, 256) * 2);
      numos::BufferDesc desc;
      desc.usage = numos::BufferUsage::Constant;
      desc.size = m_constants == nullptr ? bytes : std::max(bytes, size_t(m_constants->info().size) * 2);
      desc.debugName = "constants ring";
      const GpuHandle previous = m_constantsHandle;
      m_constantsHandle = createBuffer(desc);
      m_constants = getBuffer(m_constantsHandle);
      if (previous != 0) {
        m_retiredConstants.push_back(previous);
      }
      m_constantsOffset = 0;
      return uploadConstants(pData, size);
    }
    m_ctx->writeToBuffer(m_constants, offset, size, pData);
    m_constantsOffset = offset + size;
    return { m_constantsHandle, offset, size };
  }

  void NumosGpu::updateBuffer(GpuHandle handle, size_t offset, size_t size, const void* pData) {
    const Rc<DxvkBuffer> dst = getBuffer(handle);
    if (dst == nullptr || size == 0) {
      return;
    }
    if (offset % 4 != 0) {
      // vkCmdUpdateBuffer needs a 4 byte aligned offset, so other offsets are staged through the constants ring.
      const ConstantsRegion staged = uploadConstants(pData, size);
      m_ctx->copyBuffer(dst, offset, getBuffer(staged.buffer), staged.offset, size);
      return;
    }
    // The tail padding keeps this write partial, so DXVK updates in place instead of renaming.
    m_ctx->writeToBuffer(dst, offset, size, pData);
  }

  void NumosGpu::copyBuffer(GpuHandle dst, size_t dstOffset, GpuHandle src, size_t srcOffset, size_t size) {
    Resource* pDst = find(dst);
    const Rc<DxvkBuffer> s = getBuffer(src);
    if (pDst == nullptr || pDst->kind != Resource::Kind::Buffer || s == nullptr || size == 0) {
      return;
    }
    m_ctx->copyBuffer(pDst->buffer.buffer, dstOffset, s, srcOffset, size);
    pDst->buffer.copied = true;
  }

  void NumosGpu::copyTexture(GpuHandle dst, GpuHandle src) {
    const Resource* pSrc = find(src);
    const Rc<DxvkImage> d = getImage(dst);
    if (pSrc == nullptr || pSrc->kind != Resource::Kind::Texture || d == nullptr) {
      return;
    }
    const numos::TextureDesc& desc = pSrc->texture.desc;
    for (uint32_t mip = 0; mip < std::max(desc.mipLevels, 1u); ++mip) {
      const VkImageSubresourceLayers layers = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layerCount(desc) };
      m_ctx->copyImage(d, layers, VkOffset3D {}, pSrc->texture.image, layers, VkOffset3D {}, mipExtent(desc, mip));
    }
  }

  void NumosGpu::clearTexture(GpuHandle texture, const float rgba[4]) {
    const Resource* pResource = find(texture);
    if (pResource == nullptr || pResource->kind != Resource::Kind::Texture) {
      return;
    }
    VkClearColorValue value = {};
    if (pResource->texture.desc.format == numos::Format::R32Uint) {
      for (int i = 0; i < 4; ++i) {
        value.uint32[i] = uint32_t(std::max(rgba[i], 0.0f));
      }
    } else {
      std::memcpy(value.float32, rgba, sizeof(value.float32));
    }
    m_ctx->clearColorImage(pResource->texture.image, value, wholeRange(pResource->texture.image));
  }

  void NumosGpu::uploadTexture(GpuHandle texture, uint32_t mip, const void* pData, size_t size) {
    const Resource* pResource = find(texture);
    if (pResource == nullptr || pResource->kind != Resource::Kind::Texture) {
      return;
    }
    const numos::TextureDesc& desc = pResource->texture.desc;
    const VkExtent3D extent = mipExtent(desc, mip);
    const uint32_t layers = layerCount(desc);
    const size_t texelBytes = pResource->texture.image->formatInfo()->elementSize;
    const size_t rowPitch = size_t(extent.width) * texelBytes;
    const size_t slicePitch = rowPitch * extent.height;
    if (size < slicePitch * extent.depth * layers) {
      Logger::err(str::format("[NUMOS] texture upload to ", pResource->texture.name, " is ", size, " bytes, too small for mip ", mip));
      return;
    }
    const VkImageSubresourceLayers subresources = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layers };
    m_ctx->updateImage(pResource->texture.image, subresources, VkOffset3D {}, extent, pData, rowPitch, slicePitch);
  }

  Rc<DxvkImageView> NumosGpu::viewFor(Texture& texture, uint32_t mip, uint32_t mipCount, BindingDim dim, bool storage) {
    const uint32_t mips = std::max(texture.desc.mipLevels, 1u);
    mip = std::min(mip, mips - 1);
    const uint32_t count = storage ? 1u : std::min(mipCount, mips - mip);
    if (!storage && mip == 0 && count == mips && dim == dimOf(texture.desc)) {
      return texture.view;
    }
    const uint64_t key = uint64_t(mip) | uint64_t(count) << 8 | uint64_t(storage) << 16 | uint64_t(dim) << 17;
    Rc<DxvkImageView>& v = texture.views[key];
    if (v == nullptr) {
      v = makeView(m_pDevice, texture.image, toViewType(dim), mip, count, storage);
    }
    return v;
  }

  Rc<DxvkImageView> NumosGpu::placeholder(BindingDim dim, bool storage, uint32_t spirvFormat) {
    const VkFormat format = storage ? spirvToVkFormat(spirvFormat) : VK_FORMAT_R16G16B16A16_SFLOAT;
    const uint64_t key = uint64_t(dim) | uint64_t(storage) << 8 | uint64_t(format) << 16;
    Rc<DxvkImageView>& v = m_placeholders[key];
    if (v == nullptr) {
      const Rc<DxvkImage> image = makeImage(m_pDevice, dim == BindingDim::Tex3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                                            format, { 1, 1, 1 }, 1, 1, "NUMOS placeholder");
      m_ctx->changeImageLayout(image, VK_IMAGE_LAYOUT_GENERAL);
      m_ctx->clearColorImage(image, VkClearColorValue {}, wholeRange(image));
      v = makeView(m_pDevice, image, toViewType(dim), 0, 1, storage);
    }
    return v;
  }

  Rc<DxvkShader> NumosGpu::getShader(uint32_t pass) {
    Pass& p = m_passes[pass];
    if (p.loaded) {
      return p.shader;
    }
    p.loaded = true;
    const numos::PassInfo& info = numos::passInfo(pass);
    const std::vector<uint32_t> words = numos::loadPassSpirv(numos::PassId(pass), m_spirvDirectory);
    if (words.empty()) {
      // loadPassSpirv logs the reason.
      Logger::err(str::format("[NUMOS] pass ", info.name, " has no valid SPIR-V in ", m_spirvDirectory));
      m_shaderLoadFailed = true;
      return nullptr;
    }
    std::vector<DxvkResourceSlot> slots;
    slots.reserve(info.bindingCount);
    for (uint32_t i = 0; i < info.bindingCount; ++i) {
      slots.push_back(toSlot(info.bindings[i]));
    }
    p.shader = new DxvkShader(VK_SHADER_STAGE_COMPUTE_BIT, uint32_t(slots.size()), slots.data(),
                              { 0u, 0u, 0u, info.pushConstantSize },
                              SpirvCodeBuffer(uint32_t(words.size()), words.data()),
                              DxvkShaderOptions(), DxvkShaderConstData());
    p.shader->setDebugName(info.name);
    p.shader->generateShaderKey();
    m_pDevice->registerShader(p.shader, true);
    return p.shader;
  }

  void NumosGpu::prewarmShaders(numos::PassId cloudRender) {
    for (uint32_t pass = 0; pass < numos::passCount(); ++pass) {
      const numos::PassId id = numos::PassId(pass);
      const bool cloudRenderVariant = id == numos::PassId::cloud_render || id == numos::PassId::cloud_render_fast ||
                                      id == numos::PassId::cloud_render_reference;
      if (!cloudRenderVariant || id == cloudRender) {
        getShader(pass);
      }
    }
  }

  void NumosGpu::dispatch(const numos::DispatchDesc& desc) {
    if (desc.pass >= numos::passCount()) {
      return;
    }
    const Rc<DxvkShader> shader = getShader(desc.pass);
    if (shader == nullptr) {
      return;
    }
    const numos::PassInfo& info = numos::passInfo(desc.pass);
    const uint32_t count = std::min(desc.bindingCount, info.bindingCount);

    for (uint32_t i = 0; i < count; ++i) {
      const numos::Binding& b = desc.bindings[i];
      const numos::PassBindingInfo& decl = info.bindings[i];
      switch (b.kind) {
      case BindingKind::ConstantBuffer:
      case BindingKind::StructuredBuffer:
      case BindingKind::RWStructuredBuffer: {
        const Resource* pResource = find(b.resource);
        if (pResource == nullptr || pResource->kind != Resource::Kind::Buffer) {
          if (m_placeholderBuffer == nullptr) {
            DxvkBufferCreateInfo bufferInfo;
            bufferInfo.size = kPlaceholderBufferBytes;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            bufferInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            m_placeholderBuffer = m_pDevice->createBuffer(bufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                         DxvkMemoryStats::Category::RTXBuffer, "NUMOS placeholder buffer");
            m_ctx->clearBuffer(m_placeholderBuffer, 0, kPlaceholderBufferBytes, 0);
          }
          m_ctx->bindResourceBuffer(b.slot, DxvkBufferSlice(m_placeholderBuffer, 0, kPlaceholderBufferBytes));
          break;
        }
        const size_t total = pResource->buffer.desc.size;
        const size_t offset = std::min(b.offset, total);
        const size_t size = b.size == ~size_t(0) ? total - offset : std::min(b.size, total - offset);
        m_ctx->bindResourceBuffer(b.slot, DxvkBufferSlice(pResource->buffer.buffer, offset, size));
        break;
      }
      case BindingKind::Sampler: {
        Rc<DxvkSampler> s = getSampler(b.resource);
        if (s == nullptr) {
          if (m_defaultSampler == nullptr) {
            m_defaultSampler = getSampler(createSampler(numos::SamplerDesc {}));
          }
          s = m_defaultSampler;
        }
        m_ctx->bindResourceSampler(b.slot, s);
        break;
      }
      case BindingKind::SampledTexture:
      case BindingKind::StorageTexture:
      case BindingKind::CombinedTextureSampler: {
        const bool storage = b.kind == BindingKind::StorageTexture;
        Resource* pResource = find(b.resource);
        Rc<DxvkImageView> v;
        if (pResource != nullptr && pResource->kind == Resource::Kind::Texture) {
          v = viewFor(pResource->texture, b.mipLevel, b.mipCount, decl.dim, storage);
        } else if (pResource != nullptr && pResource->kind == Resource::Kind::ExternalView) {
          v = pResource->externalView;
        } else {
          v = placeholder(decl.dim, storage, decl.spirvImageFormat);
        }
        m_ctx->bindResourceView(b.slot, v, nullptr);
        if (b.kind == BindingKind::CombinedTextureSampler) {
          Rc<DxvkSampler> s = getSampler(b.sampler);
          if (s == nullptr) {
            if (m_defaultSampler == nullptr) {
              m_defaultSampler = getSampler(createSampler(numos::SamplerDesc {}));
            }
            s = m_defaultSampler;
          }
          m_ctx->bindResourceSampler(b.slot, s);
        }
        break;
      }
      case BindingKind::AccelerationStructure: {
        const Resource* pResource = find(b.resource);
        if (pResource == nullptr || pResource->kind != Resource::Kind::Accel) {
          Logger::err(str::format("[NUMOS] pass ", info.name, " dispatched without an acceleration structure"));
          return;
        }
        m_ctx->bindAccelerationStructure(b.slot, pResource->accel);
        break;
      }
      }
    }

    m_ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, shader);
    if (info.pushConstantSize > 0) {
      // The other push constant bank holds the render state of D3D9.
      m_ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
      uint8_t push[128] = {};
      const uint32_t size = std::min<uint32_t>(info.pushConstantSize, sizeof(push));
      if (desc.pushConstants != nullptr) {
        std::memcpy(push, desc.pushConstants, std::min(desc.pushConstantSize, size));
      }
      m_ctx->pushConstants(0, size, push);
    }
    m_ctx->dispatch(desc.groupsX, desc.groupsY, desc.groupsZ);
  }

  void NumosGpu::barrier() {
    // DXVK orders the hazards it tracks, and this covers the rest.
    m_ctx->emitMemoryBarrier(0,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  }

  bool NumosGpu::readBuffer(GpuHandle readback, void* pDst, size_t size) {
    const Resource* pResource = find(readback);
    if (pResource == nullptr || pResource->kind != Resource::Kind::Buffer || !pResource->buffer.copied ||
        pResource->buffer.buffer->isInUse(DxvkAccess::Read)) {
      return false;
    }
    std::memcpy(pDst, pResource->buffer.buffer->mapPtr(0), std::min(size, pResource->buffer.desc.size));
    return true;
  }

  void NumosGpu::beginZone(const char* name) {
    m_zones.emplace_back(name ? name : "NUMOS");
    VkDebugUtilsLabelEXT label = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    label.pLabelName = m_zones.back().c_str();
    m_ctx->beginDebugLabel(&label);
  }

  void NumosGpu::endZone() {
    if (!m_zones.empty()) {
      m_zones.pop_back();
      m_ctx->endDebugLabel();
    }
  }

}
