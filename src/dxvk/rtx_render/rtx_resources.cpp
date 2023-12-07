/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_resources.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "../util/util_blueNoise_128x128x64.h"
#include <rtxdi/RtxdiParameters.h>
#include "rtx/pass/raytrace_args.h"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/algorithm/nee_cache_data.h"
#include <assert.h>
#include "rtx_options.h"
#include "rtx/utility/gpu_printing.h"
#include "rtx_terrain_baker.h"
#include "rtx_scene_manager.h"
#include "rtx_texture_manager.h"

namespace dxvk {

  Rc<DxvkImageView> Resources::createImageView(Rc<DxvkContext>& ctx,
                                               const Rc<DxvkImage>& image,
                                               const VkFormat format,
                                               const uint32_t numLayers,
                                               const VkImageViewType imageViewType,
                                               bool isColorAttachment) {
    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = imageViewType;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = numLayers;

    viewInfo.format = format;

    if (isColorAttachment) {
      viewInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    } else {
      viewInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    return ctx->getDevice()->createImageView(image, viewInfo);
  }

  Resources::Resource Resources::createImageResource(Rc<DxvkContext>& ctx,
                                                     const char* name,
                                                     const VkExtent3D& extent,
                                                     const VkFormat format,
                                                     const uint32_t numLayers,
                                                     const VkImageType imageType,
                                                     const VkImageViewType imageViewType,
                                                     const VkImageCreateFlags imageCreateFlags,
                                                     bool isColorAttachment) {
    DxvkImageCreateInfo desc;
    desc.type = imageType;
    desc.flags = imageCreateFlags;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = extent;
    desc.numLayers = numLayers;
    desc.mipLevels = 1;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    desc.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    desc.format = format;

    if (isColorAttachment) {
      desc.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    } else {
      desc.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (imageViewType == VK_IMAGE_VIEW_TYPE_CUBE) {
      desc.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    Resource resource;
    resource.image = ctx->getDevice()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, name);
    resource.view = createImageView(ctx, resource.image, format, numLayers, imageViewType, isColorAttachment);
    ctx->changeImageLayout(resource.image, VK_IMAGE_LAYOUT_GENERAL);

    VkImageSubresourceRange subRange = {};
    subRange.layerCount = 1;
    subRange.levelCount = 1;
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkClearColorValue clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Note: Initialize to zero, or we get corruption on resolution change
    ctx->clearColorImage(resource.image, clearValue, subRange);

    return resource;
  }



  Resources::AliasedResource::AliasedResource(Rc<DxvkContext>& ctx,
                                              const VkExtent3D& extent,
                                              const VkFormat format,
                                              const char* name,
                                              const bool allowCompatibleFormatAliasing,
                                              const uint32_t numLayers,
                                              const VkImageType imageType,
                                              const VkImageViewType imageViewType)
#ifdef REMIX_DEVELOPMENT
    : m_thisObjectAddress(std::make_shared<const Resources::AliasedResource*>(this))
    , m_name(name)
#endif
  {
    VkImageCreateFlags imageCreateFlags = 0;

    if (allowCompatibleFormatAliasing)
      imageCreateFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    m_device = ctx->getDevice().ptr();
    m_sharedResource = new SharedResource(createImageResource(ctx, name, extent, format, numLayers, imageType, imageViewType, imageCreateFlags));
    m_view = m_sharedResource->resource.view;
  }

  Resources::AliasedResource::AliasedResource(const Resources::AliasedResource& other,
                                              Rc<DxvkContext>& ctx,
                                              const VkExtent3D& extent,
                                              const VkFormat format,
                                              const char* name,
                                              const uint32_t numLayers,
                                              const VkImageType imageType,
                                              const VkImageViewType imageViewType)
    : m_device(other.m_device)
    , m_sharedResource(other.m_sharedResource)
#ifdef REMIX_DEVELOPMENT
    , m_thisObjectAddress(std::make_shared<const Resources::AliasedResource*>(this))
    , m_name(name)
#endif
  {
    const DxvkImageCreateInfo& otherImageInfo = other.imageInfo();

#ifdef REMIX_DEVELOPMENT
    const bool areFormatsCompatibleResult =
      format == otherImageInfo.format ||
      ((otherImageInfo.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       areFormatsCompatible(format, otherImageInfo.format));

    assert(
      extent.width == otherImageInfo.extent.width &&
      extent.height == otherImageInfo.extent.height &&
      extent.depth == otherImageInfo.extent.depth &&
      numLayers == otherImageInfo.numLayers &&
      imageType == otherImageInfo.type &&
      imageViewType == other.imageViewType() &&
      areFormatsCompatibleResult &&
      "Input aliased resource was created with incompatible create resource parameters");
#endif

    if (format == otherImageInfo.format)
      m_view = other.m_view;
    else
      m_view = createImageView(ctx, m_sharedResource->resource.image, format, numLayers, imageViewType);
  }

  Resources::AliasedResource& Resources::AliasedResource::operator=(Resources::AliasedResource&& other) {
    if (this != &other) {
      m_device = other.m_device;
      m_sharedResource = other.m_sharedResource;
      m_view = other.m_view;
      m_writeFrameIdx = other.m_writeFrameIdx;
#ifdef REMIX_DEVELOPMENT
      m_thisObjectAddress = std::make_shared<const AliasedResource*>(this);
      m_name = other.m_name;
#endif
    }

    return *this;
  }

  Rc<DxvkImageView> Resources::AliasedResource::view(Resources::AccessType accessType, bool isAccessedByGPU) const {
    registerAccess(accessType, isAccessedByGPU);

    return m_view;
  }

  Rc<DxvkImage> Resources::AliasedResource::image(Resources::AccessType accessType, bool isAccessedByGPU) const {
    registerAccess(accessType, isAccessedByGPU);

    return m_sharedResource->resource.image;
  }

  const Resources::Resource& Resources::AliasedResource::resource(Resources::AccessType accessType, bool isAccessedByGPU) const {
    registerAccess(accessType, isAccessedByGPU);

    m_sharedResource->resource.view = m_view;

    return m_sharedResource->resource;
  }

  bool Resources::AliasedResource::ownsResource() const {
#ifdef REMIX_DEVELOPMENT
    return !m_sharedResource->owner.expired() && m_sharedResource->owner.lock() == m_thisObjectAddress;
#else
    return true;
#endif
  }

  const char* Resources::AliasedResource::name() const {
#ifdef REMIX_DEVELOPMENT     
    return m_name;
#else
    return nullptr;
#endif
  }

  void Resources::AliasedResource::registerAccess(AccessType accessType, bool isAccessedByGPU) const {

    if (isAccessedByGPU) {
      switch (accessType) {
      case AccessType::Write:
      case AccessType::ReadWrite:
        m_writeFrameIdx = m_device->getCurrentFrameId();
        break;
      case AccessType::Read:
        // Do nothing
        break;
      default:
        assert(0 && "Unsupported access type");
        break;
      }
    }

#ifdef REMIX_DEVELOPMENT   
    if (isAccessedByGPU) {
      switch (accessType) {
      case AccessType::Write:
        takeOwnership();
        break;
      case AccessType::ReadWrite:
      case AccessType::Read:
        if (!ownsResource()) {
          std::string errorMessage = str::format("AliasedResource WAR hazard detected:",
                 "\nNew access type: ", accessType == AccessType::Read ? "Read" : "ReadWrite",
                 "\nNew owner: \"", name() ? name() : "name unknown", "\""
                 "\nPrevious owner: \"", m_sharedResource->owner.expired()
                                       ? "not set"
                                       : (*m_sharedResource->owner.lock().get())->name()
                                          ? (*m_sharedResource->owner.lock().get())->name()
                                          : "name unknown", "\"");
          ONCE(Logger::err(errorMessage));
          assert(0 && "[AliasedResource] WAR hazard detected");
        }
        break;
      default:
        assert(0 && "Unsupported access type");
        break;
      }
    }
#endif
  }

  void Resources::AliasedResource::takeOwnership() const {
#ifdef REMIX_DEVELOPMENT     
    m_sharedResource->owner = m_thisObjectAddress;
#endif
  }

  bool Resources::AliasedResource::sharesTheSameView(const Resources::AliasedResource& other) const {
    return m_view.ptr() == other.m_view.ptr();
  }

  Resources::Resources(DxvkDevice* device)
    : CommonDeviceObject(device) { }

  void Resources::createRaytracingOutput(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    ScopedCpuProfileZone();

    assert(targetExtent.width > 0 && targetExtent.height > 0 && targetExtent.depth > 0);

    if (m_downscaledExtent != downscaledExtent) {
      m_downscaledExtent = downscaledExtent;

      createDownscaledResources(ctx);
    }

    if (targetExtent != m_targetExtent) {
      m_targetExtent = targetExtent;

      createTargetResources(ctx);
    }
  }

  bool Resources::validateRaytracingOutput(const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) const {
    return m_raytracingOutput.isReady() && m_targetExtent == targetExtent && m_downscaledExtent == downscaledExtent;
  }

  void Resources::onFrameBegin(Rc<DxvkContext> ctx, RtxTextureManager& textureManager, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    executeFrameBeginEventList(m_onFrameBegin, ctx, downscaledExtent, targetExtent);

    if (ctx->isDLFGEnabled()) {
      const uint32_t currentFrameId = ctx->getDevice()->getCurrentFrameId();
      const Rc<RtxSemaphore>& frameEndSemaphore = ctx->getCommonObjects()->metaDLFG().getFrameEndSemaphore();

      // if we've launched N frames, wait on the current - Nth frame
      if (currentFrameId >= kDLFGMaxGPUFramesInFlight) {
        // xxxnsubtil: lots of problems when toggling the enable here and during init, leaving disabled for now
        // at worst this may cause transient corruption
        //ctx->getCommandList()->addWaitSemaphore(frameEndSemaphore->handle(), currentFrameId - kDLFGMaxGPUFramesInFlight);
      } else {
        // CPU sync when semaphore wraps around
        if (currentFrameId == 0) {
          ctx->getDevice()->waitForIdle();
          // xxxnsubtil: spec does not allow signaling back to zero, but not clear how this should be handled --- recreate the semaphore?
        }
      }
    }
    
    // Alias resources that alias to different resources frame to frame
    m_raytracingOutput.m_secondaryConeRadius = AliasedResource(m_raytracingOutput.getCurrentRtxdiConfidence(), ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "Secondary Cone Radius");
    m_raytracingOutput.m_sharedIntegrationSurfacePdf = AliasedResource(m_raytracingOutput.getCurrentRtxdiIlluminance(), ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "Shared Integration Surface PDF");
    assert(
      m_raytracingOutput.m_secondaryConeRadius.sharesTheSameView(m_raytracingOutput.getCurrentRtxdiConfidence()) &&
      m_raytracingOutput.m_sharedIntegrationSurfacePdf.sharesTheSameView(m_raytracingOutput.getCurrentRtxdiIlluminance()) &&
      "New view for an aliased resource was created on the fly. Avoid doing that or ensure it has no negative side effects.");
  }

  void Resources::onResize(Rc<DxvkContext> ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    if (!validateRaytracingOutput(downscaledExtent, targetExtent)) {
      ctx->getDevice()->waitForIdle();

      createRaytracingOutput(ctx, downscaledExtent, targetExtent);
    }
  }

  void Resources::createConstantsBuffer() {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(RaytraceArgs);
    m_constants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
  }

  Rc<DxvkBuffer> Resources::getConstantsBuffer() {
    if (m_constants == nullptr) {
      createConstantsBuffer();
    }
    assert(m_constants != nullptr);
    return m_constants;
  }

  void Resources::createBlueNoiseTexture(dxvk::Rc<DxvkContext> ctx) {
    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.format = VK_FORMAT_R8_UNORM;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = VkExtent3D { 128, 128, 1 };
    desc.numLayers = 64;
    desc.mipLevels = 1;
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    desc.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_GENERAL;

    m_blueNoiseTex = m_device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "blue noise");

    uint32_t rowPitch = desc.extent.width;
    uint32_t layerPitch = rowPitch * desc.extent.height;

    ctx->updateImage(m_blueNoiseTex,
                     VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, desc.numLayers },
                     VkOffset3D { 0, 0, 0 },
                     desc.extent,
                     (void*) &blueNoise_128x128x64[0][0], rowPitch, layerPitch);

    ctx->emitMemoryBarrier(0,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                           VK_ACCESS_SHADER_READ_BIT);

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = m_blueNoiseTex->info().format;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 64;
    m_blueNoiseTexView = m_device->createImageView(m_blueNoiseTex, viewInfo);
  }

  Rc<DxvkSampler> dxvk::Resources::getSampler(
    const VkFilter filter,
    const VkSamplerMipmapMode mipFilter,
    const VkSamplerAddressMode addressModeU,
    const VkSamplerAddressMode addressModeV,
    const VkSamplerAddressMode addressModeW,
    const VkClearColorValue borderColor/* = VkClearColorValue()*/,
    const float mipBias/* = 0*/,
    const bool useAnisotropy/* = false*/) {
    const VkPhysicalDeviceLimits& limits = m_device->properties().core.properties.limits;
    const float maxAniso = std::min(limits.maxSamplerAnisotropy, RtxOptions::Get()->getMaxAnisotropySamples());

    // Fill out the rest of the sample create info
    DxvkSamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.mipmapMode = mipFilter;
    samplerInfo.mipmapLodBias = mipBias;
    samplerInfo.mipmapLodMin = 0.0f;
    samplerInfo.mipmapLodMax = VK_LOD_CLAMP_NONE;
    samplerInfo.useAnisotropy = useAnisotropy;
    samplerInfo.maxAnisotropy = maxAniso;
    samplerInfo.addressModeU = addressModeU;
    samplerInfo.addressModeV = addressModeV;
    samplerInfo.addressModeW = addressModeW;
    samplerInfo.compareToDepth = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.borderColor = borderColor;
    samplerInfo.usePixelCoord = VK_FALSE;
    
    // Build a hash key to lookup the sampler in the cache.
    XXH64_hash_t key = samplerInfo.calculateHash();

    Rc<DxvkSampler> sampler;

    auto samplerIt = m_samplerCache.find(key);
    if (samplerIt == m_samplerCache.end()) {

      // Create the sampler
      sampler = m_device->createSampler(samplerInfo);

      // Add it to the cache
      m_samplerCache.insert({ key, sampler });
    } else {
      sampler = samplerIt->second;
    }

    return sampler;
  }

  Rc<DxvkSampler> dxvk::Resources::getSampler(
    const VkFilter filter,
    const VkSamplerMipmapMode mipFilter,
    const VkSamplerAddressMode addressMode,
    const float mipBias/* = 0*/,
    const bool useAnisotropy/* = false*/) {
    return getSampler(filter, mipFilter, addressMode, addressMode, addressMode, VkClearColorValue(), mipBias, useAnisotropy);
  }

  Rc<DxvkImageView> Resources::getWhiteTexture(Rc<DxvkContext> ctx) {
    if (m_whiteTex == nullptr || m_whiteTexView == nullptr) {
      DxvkImageCreateInfo desc;
      desc.type = VK_IMAGE_TYPE_2D;
      desc.format = VK_FORMAT_R8G8B8A8_UNORM;
      desc.flags = 0;
      desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
      desc.extent = VkExtent3D { 1, 1, 1 };
      desc.numLayers = 1;
      desc.mipLevels = 1;
      desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT;
      desc.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
        | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      desc.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_SHADER_READ_BIT;
      desc.tiling = VK_IMAGE_TILING_OPTIMAL;
      desc.layout = VK_IMAGE_LAYOUT_GENERAL;

      m_whiteTex = m_device->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXMaterialTexture, "white");

      uint32_t rowPitch = desc.extent.width;
      uint32_t layerPitch = rowPitch * desc.extent.height;
      uint32_t data = 0xFFFFFFFF; // All white

      ctx->updateImage(m_whiteTex,
        VkImageSubresourceLayers { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, desc.numLayers },
        VkOffset3D { 0, 0, 0 },
        desc.extent,
        (void*) &data, rowPitch, layerPitch);

      ctx->emitMemoryBarrier(0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_ACCESS_SHADER_READ_BIT);

      DxvkImageViewCreateInfo viewInfo;
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      viewInfo.format = m_whiteTex->info().format;
      viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLevel = 0;
      viewInfo.numLevels = 1;
      viewInfo.minLayer = 0;
      viewInfo.numLayers = 1;
      m_whiteTexView = m_device->createImageView(m_whiteTex, viewInfo);

      const XXH64_hash_t kAllWhiteHash = 0x7768697465746578; // 'whitetex' in ASCII
      m_whiteTex->setHash(kAllWhiteHash);
    }
    return m_whiteTexView;
  }

  Rc<DxvkImageView> Resources::getBlueNoiseTexture(dxvk::Rc<DxvkContext> ctx) {
    if (m_blueNoiseTex == nullptr) {
      createBlueNoiseTexture(ctx);
    }
    assert(m_blueNoiseTex != nullptr);
    assert(m_blueNoiseTexView != nullptr);

    return m_blueNoiseTexView;
  }

  Resources::Resource Resources::getSkyMatte(Rc<DxvkContext> ctx, VkFormat format) {
    if (format == VK_FORMAT_UNDEFINED) {
      return m_skyMatte;
    }

    if (!m_skyMatte.isValid() || m_skyMatte.image->info().extent != m_targetExtent ||
        m_skyMatte.image->info().format != format) {
      m_skyMatte = createImageResource(ctx, "sky matte", m_targetExtent, format,
                                       1, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D,
                                       VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, true);
    }

    assert(m_skyMatte.isValid());

    return m_skyMatte;
  }

  Resources::Resource Resources::getSkyProbe(Rc<DxvkContext> ctx, VkFormat format) {
    if (format == VK_FORMAT_UNDEFINED) {
      return m_skyProbe;
    }

    const uint32_t skyProbeSide = RtxOptions::Get()->skyProbeSide();

    if (!m_skyProbe.isValid() || m_skyProbe.image->info().extent.width != skyProbeSide ||
        m_skyProbe.image->info().format != format) {
      const VkExtent3D skyProbeExt = { skyProbeSide, skyProbeSide, 1 };

      m_skyProbe = createImageResource(ctx, "sky probe", skyProbeExt, format,
                                       6, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE,
                                       VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, true);
    }

    assert(m_skyProbe.isValid());

    return m_skyProbe;
  }

  Rc<DxvkImageView> Resources::getCompatibleViewForView(const Rc<DxvkImageView>& view, VkFormat format) {
    // Lazy GC
    static uint32_t lastGCFrame = 0;

    const uint32_t currentFrame = m_device->getCurrentFrameId();
    const uint32_t numFramesToKeepViews = RtxOptions::Get()->numFramesToKeepMaterialTextures();
    if (currentFrame >= lastGCFrame + numFramesToKeepViews) {
      m_viewCache.erase_if(
        [currentFrame, numFramesToKeepViews](const auto& item) {
          return currentFrame >= item->second.second + numFramesToKeepViews;
        });

      lastGCFrame = currentFrame;
    }

    if (format == view->info().format) {
      return view;
    }

    if (!areFormatsCompatible(format, view->info().format)) {
      return {};
    }

    const XXH64_hash_t hash = XXH64_std_hash<VkImage>{}(view->image()->handle()) ^
      XXH64_std_hash<VkFormat>{}(view->info().format) ^
      XXH64_std_hash<VkFormat>{}(format);

    auto& compatibleView = m_viewCache[hash];

    if (likely(compatibleView.first != nullptr)) {
      compatibleView.second = currentFrame;
      return compatibleView.first;
    }

    auto viewCreateInfo = view->info();
    viewCreateInfo.format = format;

    compatibleView.first = m_device->createImageView(view->image(), viewCreateInfo);
    compatibleView.second = currentFrame;

    return compatibleView.first;
  }

  uint32_t Resources::getFormatCompatibilityCategoryIndex(const VkFormat format) {
    //https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap46.html#formats-compatibility-classes
    switch (format) {
    default: return kInvalidFormatCompatibilityCategoryIndex;
      break;
    case VK_FORMAT_R4G4_UNORM_PACK8:
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_USCALED:
    case VK_FORMAT_R8_SSCALED:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
      return 0;

    case VK_FORMAT_R10X6G10X6_UNORM_2PACK16:
    case VK_FORMAT_R12X4G12X4_UNORM_2PACK16:
    case VK_FORMAT_R16G16_S10_5_NV:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_USCALED:
    case VK_FORMAT_R8G8B8A8_SSCALED:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SNORM:
    case VK_FORMAT_B8G8R8A8_USCALED:
    case VK_FORMAT_B8G8R8A8_SSCALED:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_USCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      return 3;

    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_USCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64_SFLOAT:
      return 5;

    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64_SFLOAT:
      return 7;
    }
  }

  bool Resources::areFormatsCompatible(const VkFormat format1, const VkFormat format2) { 
  
    uint32_t categoryIndex1 = getFormatCompatibilityCategoryIndex(format1);
    uint32_t categoryIndex2 = getFormatCompatibilityCategoryIndex(format2);

    return categoryIndex1 != kInvalidFormatCompatibilityCategoryIndex && 
           categoryIndex1 == categoryIndex2;
  }

  void Resources::createDownscaledResources(Rc<DxvkContext>& ctx) {
    Logger::debug("Render resolution changed, recreating rendering resources");

    // Explicit constant to make it clear where cross format aliasing occurs. 
    // Changing it to false requires further changes below.
    const bool allowCompatibleFormatAliasing = true;

    // Volumetrics
    m_raytracingOutput.m_froxelVolumeExtent = util::computeBlockCount(m_downscaledExtent, VkExtent3D {
      RtxOptions::Get()->getFroxelGridResolutionScale(),
      RtxOptions::Get()->getFroxelGridResolutionScale(),
      1
    });
    m_raytracingOutput.m_froxelVolumeExtent.depth = RtxOptions::Get()->getFroxelDepthSlices();
    m_raytracingOutput.m_numFroxelVolumes = RtxOptions::Get()->enableVolumetricsInPortals() ? maxRayPortalCount + 1 : 1;

    VkExtent3D froxelGridFullDimensions = m_raytracingOutput.m_froxelVolumeExtent;
    // Note: preintegrated radiance is only computed for one (main) volume, not all of them
    m_raytracingOutput.m_volumePreintegratedRadiance = createImageResource(ctx, "volume preintegrated radiance", froxelGridFullDimensions, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);

    froxelGridFullDimensions.width *= m_raytracingOutput.m_numFroxelVolumes;

    m_raytracingOutput.m_volumeReservoirs[0] = createImageResource(ctx, "volume reservoir 0", froxelGridFullDimensions, VK_FORMAT_R32G32B32A32_UINT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_raytracingOutput.m_volumeReservoirs[1] = createImageResource(ctx, "volume reservoir 1", froxelGridFullDimensions, VK_FORMAT_R32G32B32A32_UINT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    // Note: RGBA16 used here as R11G11B10 develops precision issues when accumulated over many frames. Luckily can make use of 16 bit alpha
    // channel to store additional information however (such as the history age, previously we'd want this in its own texture so it could be
    // sampled from exactly whereas the radiance would be interpolated, but interpolating the history age is likely a better estimation of the
    // actual age anyways, though do note this is fairly wasteful as the history age only needs to be 8-ish bits).
    m_raytracingOutput.m_volumeAccumulatedRadiance[0] = createImageResource(ctx, "volume accumulated radiance 0", froxelGridFullDimensions, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_raytracingOutput.m_volumeAccumulatedRadiance[1] = createImageResource(ctx, "volume accumulated radiance 1", froxelGridFullDimensions, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_raytracingOutput.m_volumeFilteredRadiance = createImageResource(ctx, "volume filtered radiance", froxelGridFullDimensions, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);

    // GBuffer (Primary/Secondary Surfaces)
    m_raytracingOutput.m_sharedFlags = createImageResource(ctx, "shared flags", m_downscaledExtent, VK_FORMAT_R16_UINT);
    // Note: Could be B10G11R11_UFLOAT_PACK32 potentially if the precision of that is acceptable for the shared radiance.
    // Otherwise we split the channels like this to reduce memory usage (as no 3 component 16 bit float formats are very well supported),
    // this is fine because we only read/write to this texture in a coherent way so bringing in 2x many cachelines is not a problem (versus
    // random access reads where they would be a problem).
    m_raytracingOutput.m_sharedRadianceRG = createImageResource(ctx, "shared radiance RG", m_downscaledExtent, VK_FORMAT_R16G16_SFLOAT);
    m_raytracingOutput.m_sharedRadianceB = createImageResource(ctx, "shared radiance B", m_downscaledExtent, VK_FORMAT_R16_SFLOAT);
    m_raytracingOutput.m_sharedMaterialData0 = createImageResource(ctx, "shared material data 0", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_sharedMaterialData1 = createImageResource(ctx, "shared material data 1", m_downscaledExtent, VK_FORMAT_R32_UINT);
    // Note: This value is isolated rather than being packed with other data (such as the alpha channel combined with the Shared Radiance RGB) so that
    // reads/writes to it do not bring in extra unneeded data into the cachelines (as we don't need that shared radiance information except in compositing).
    m_raytracingOutput.m_sharedMediumMaterialIndex = createImageResource(ctx, "shared medium material index", m_downscaledExtent, VK_FORMAT_R16_UINT);
    m_raytracingOutput.m_sharedBiasCurrentColorMask = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R8_UNORM, "Shared Attenuation", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_sharedSurfaceIndex = createImageResource(ctx, "shared surface index", m_downscaledExtent, VK_FORMAT_R16_UINT);

    m_raytracingOutput.m_primaryAttenuation = createImageResource(ctx, "primary attenuation", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_primaryWorldShadingNormal = createImageResource(ctx, "primary world shading normal", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_primaryWorldInterpolatedNormal = createImageResource(ctx, "primary world interpolated normal", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_primaryPerceptualRoughness = createImageResource(ctx, "primary perceptual roughness", m_downscaledExtent, VK_FORMAT_R8_UNORM);
    m_raytracingOutput.m_primaryLinearViewZ = createImageResource(ctx, "primary linear view Z", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
    for (auto& i : m_raytracingOutput.m_primaryDepthQueue) {
      i = createImageResource(ctx, "primary depth", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
      if (!ctx->getCommonObjects()->metaNGXContext().supportsDLFG()) {
        break;
      }
    }
    m_raytracingOutput.m_primaryAlbedo = createImageResource(ctx, "primary albedo", m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    m_raytracingOutput.m_primaryBaseReflectivity = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Primary Base Reflectivity");
    m_raytracingOutput.m_primarySpecularAlbedo = AliasedResource(m_raytracingOutput.m_primaryBaseReflectivity, ctx, m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Primary Specular Albedo");
    m_raytracingOutput.m_primaryVirtualMotionVector = createImageResource(ctx, "primary virtual motion vector", m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT);
    for (auto& i : m_raytracingOutput.m_primaryScreenSpaceMotionVectorQueue) {
      i = createImageResource(ctx, "primary screen space motion vector", m_downscaledExtent, VK_FORMAT_R16G16_SFLOAT);
      if (!ctx->getCommonObjects()->metaNGXContext().supportsDLFG()) {
        break;
      }
    }
    m_raytracingOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness = createImageResource(ctx, "primary virtual world shading normal perceptual roughness", m_downscaledExtent, VK_FORMAT_R16G16B16A16_UNORM);
    m_raytracingOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising = createImageResource(ctx, "primary virtual world shading normal perceptual roughness denoising", m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    m_raytracingOutput.m_primaryHitDistance = createImageResource(ctx, "primary hit distance", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
    m_raytracingOutput.m_primaryViewDirection = createImageResource(ctx, "primary view direction", m_downscaledExtent, VK_FORMAT_R16G16_SNORM);
    m_raytracingOutput.m_primaryConeRadius = createImageResource(ctx, "primary cone radius", m_downscaledExtent, VK_FORMAT_R16_SFLOAT);
    m_raytracingOutput.m_primaryWorldPositionWorldTriangleNormal[0] = createImageResource(ctx, "primary world position world triangle normal 0", m_downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_raytracingOutput.m_primaryWorldPositionWorldTriangleNormal[1] = createImageResource(ctx, "primary world position world triangle normal 1", m_downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_raytracingOutput.m_primaryPositionError = createImageResource(ctx, "primary position error", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
    
    m_raytracingOutput.m_primaryRtxdiIlluminance[0] = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "Primary RTXDI Illuminance [0]");
    m_raytracingOutput.m_primaryRtxdiIlluminance[1] = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "Primary RTXDI Illuminance[1]");

    m_raytracingOutput.m_primaryRtxdiTemporalPosition = createImageResource(ctx, "primary rtxdi temporal position", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_primarySurfaceFlags = createImageResource(ctx, "primary surface flags", m_downscaledExtent, VK_FORMAT_R8_UINT);
    m_raytracingOutput.m_primaryDisocclusionThresholdMix = createImageResource(ctx, "primary disocclusion threshold mix", m_downscaledExtent, VK_FORMAT_R8_UNORM);
    m_raytracingOutput.m_sharedSubsurfaceData = createImageResource(ctx, "primary subsurface material buffer", m_downscaledExtent, VK_FORMAT_R16G16B16A16_UINT);
    if (m_objectPickingImagesRequired) {
      m_raytracingOutput.m_primaryObjectPicking = createImageResource(ctx, "primary object picking", m_downscaledExtent, VK_FORMAT_R32_UINT);
    }

    m_raytracingOutput.m_secondaryAttenuation = createImageResource(ctx, "secondary attenuation", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_secondaryWorldShadingNormal = createImageResource(ctx, "secondary world shading normal", m_downscaledExtent, VK_FORMAT_R32_UINT);
    m_raytracingOutput.m_secondaryPerceptualRoughness = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R8_UNORM, "Secondary Perceptual Roughness", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_secondaryLinearViewZ = createImageResource(ctx, "secondary linear view z", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
    m_raytracingOutput.m_secondaryAlbedo = createImageResource(ctx, "secondary albedo", m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    m_raytracingOutput.m_secondaryBaseReflectivity = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Secondary Base Reflectivity");
    m_raytracingOutput.m_secondarySpecularAlbedo = AliasedResource(
    m_raytracingOutput.m_secondaryBaseReflectivity, ctx, m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32, "Secondary Specular Albedo");
    m_raytracingOutput.m_secondaryVirtualMotionVector = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Secondary Virtual Motion Vector");
    m_raytracingOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness = createImageResource(ctx, "secondary virtual world shading normal perceptual roughness", m_downscaledExtent, VK_FORMAT_R16G16B16A16_UNORM);
    m_raytracingOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughnessDenoising = createImageResource(ctx, "secondary virtual world shading normal perceptual roughness denoising", m_downscaledExtent, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    m_raytracingOutput.m_secondaryHitDistance = createImageResource(ctx, "secondary hit distance", m_downscaledExtent, VK_FORMAT_R32_SFLOAT);
    m_raytracingOutput.m_secondaryViewDirection = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16_SNORM, "Secondary View Direction", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_secondaryWorldPositionWorldTriangleNormal = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT, "Secondary World Position World Triangle Normal", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_secondaryPositionError = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R32_SFLOAT, "Secondary Position Error", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_decalMaterial = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R32G32B32A32_UINT, "Decal Material");
    m_raytracingOutput.m_decalEmissiveRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Decal Emissive Radiance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_alphaBlendGBuffer = createImageResource(ctx, "alpha blend gbuffer", m_downscaledExtent, VK_FORMAT_R32G32B32A32_UINT);
    m_raytracingOutput.m_alphaBlendRadiance = AliasedResource(m_raytracingOutput.m_secondaryVirtualMotionVector, ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Alpha Blend Radiance");
    m_raytracingOutput.m_indirectRadianceHitDistance = AliasedResource(m_raytracingOutput.m_decalEmissiveRadiance, ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Indirect Radiance Hit Distance");

    // Denoiser input and output (Primary/Secondary Surfaces with Direct/Indirect or Combined Radiance)
    // Note: A single texture is aliased for both the noisy output from the integration pass and the denoised result from NRD.
    m_raytracingOutput.m_primaryDirectDiffuseRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Primary Direct Diffuse Radiance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_primaryDirectSpecularRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Primary Direct Specular Radiance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_primaryIndirectDiffuseRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Primary Indirect Diffuse Radiance Hit Distance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_primaryIndirectSpecularRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Primary Indirect Specular Radiance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_secondaryCombinedDiffuseRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Secondary Combined Diffuse Radiance", allowCompatibleFormatAliasing);
    m_raytracingOutput.m_secondaryCombinedSpecularRadiance = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Secondary Combined Specular Radiance", allowCompatibleFormatAliasing);

    m_raytracingOutput.m_gbufferPSRData[0] = AliasedResource(m_raytracingOutput.m_decalMaterial, ctx, m_downscaledExtent, VK_FORMAT_R32G32B32A32_UINT, "GBuffer PSR Data 0");
    m_raytracingOutput.m_gbufferPSRData[1] = AliasedResource(m_raytracingOutput.m_decalEmissiveRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 1");
    m_raytracingOutput.m_gbufferPSRData[2] = AliasedResource(m_raytracingOutput.m_primaryDirectDiffuseRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 2");
    m_raytracingOutput.m_gbufferPSRData[3] = AliasedResource(m_raytracingOutput.m_primaryDirectSpecularRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 3");
    m_raytracingOutput.m_gbufferPSRData[4] = AliasedResource(m_raytracingOutput.m_primaryIndirectSpecularRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 4");
    m_raytracingOutput.m_gbufferPSRData[5] = AliasedResource(m_raytracingOutput.m_secondaryCombinedDiffuseRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 5");
    m_raytracingOutput.m_gbufferPSRData[6] = AliasedResource(m_raytracingOutput.m_secondaryCombinedSpecularRadiance, ctx, m_downscaledExtent, VK_FORMAT_R32G32_UINT, "GBuffer PSR Data 6");

    m_raytracingOutput.m_indirectRayOriginDirection = AliasedResource(
      m_raytracingOutput.m_secondaryWorldPositionWorldTriangleNormal, ctx, m_downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT, "Indirect Ray Origin Direction");
    m_raytracingOutput.m_indirectThroughputConeRadius = AliasedResource(
      m_raytracingOutput.m_decalEmissiveRadiance, ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Indirect Throughput Cone Radius");
    m_raytracingOutput.m_indirectFirstSampledLobeData = AliasedResource(m_raytracingOutput.m_secondaryPositionError, ctx, m_downscaledExtent, VK_FORMAT_R32_UINT, "Indirect First Sampled Lobe Data");
    m_raytracingOutput.m_indirectFirstHitPerceptualRoughness = AliasedResource(
      m_raytracingOutput.m_secondaryPerceptualRoughness, ctx, m_downscaledExtent, VK_FORMAT_R8_UNORM, "Indirect First Hit Perceptual Roughness");
    m_raytracingOutput.m_bsdfFactor = createImageResource(ctx, "bsdf factor", m_downscaledExtent, VK_FORMAT_R16G16_SFLOAT);
    m_raytracingOutput.m_bsdfFactor2 = createImageResource(ctx, "bsdf factor 2", m_downscaledExtent, VK_FORMAT_R16G16_SFLOAT);

    // Final Output
    m_raytracingOutput.m_compositeOutput = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Composite Output");
    m_raytracingOutput.m_compositeOutputExtent = m_downscaledExtent;
    m_raytracingOutput.m_lastCompositeOutput = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "Last Composite Output");

    // RTXDI Data
    m_raytracingOutput.m_gbufferLast = createImageResource(ctx, "rtxdi gbuffer last", m_downscaledExtent, VK_FORMAT_R32G32_SFLOAT);
    m_raytracingOutput.m_reprojectionConfidence = createImageResource(ctx, "rtxdi reprojection confidence", m_downscaledExtent, VK_FORMAT_R16_SFLOAT);
    m_raytracingOutput.m_rtxdiConfidence[0] = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "RTXDI Confidence 0");
    m_raytracingOutput.m_rtxdiConfidence[1] = AliasedResource(ctx, m_downscaledExtent, VK_FORMAT_R16_SFLOAT, "RTXDI Confidence 1");

    // RTXDI Gradients
    const VkExtent3D rtxDiGradientExtents = { (m_downscaledExtent.width + RTXDI_GRAD_FACTOR - 1) / RTXDI_GRAD_FACTOR, (m_downscaledExtent.height + RTXDI_GRAD_FACTOR - 1) / RTXDI_GRAD_FACTOR, 1 };
    m_raytracingOutput.m_rtxdiGradients = createImageResource(ctx, "rtxdi gradients", rtxDiGradientExtents, VK_FORMAT_R16G16_SFLOAT, 2);

    // RTXDI Best Lights - using the same downscaling factor as Gradients
    m_raytracingOutput.m_rtxdiBestLights = AliasedResource(ctx, rtxDiGradientExtents, VK_FORMAT_R16G16_UINT, "RTXDI Best Lights");

    int numReservoirBuffer = 3;
    int reservoirSize = sizeof(RTXDI_PackedReservoir);
    int renderWidthBlocks = (m_downscaledExtent.width + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    int renderHeightBlocks = (m_downscaledExtent.height + RTXDI_RESERVOIR_BLOCK_SIZE - 1) / RTXDI_RESERVOIR_BLOCK_SIZE;
    int reservoirBufferPixels = renderWidthBlocks * renderHeightBlocks * RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE;
    DxvkBufferCreateInfo rtxdiBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    rtxdiBufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    rtxdiBufferInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    rtxdiBufferInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    rtxdiBufferInfo.size = reservoirBufferPixels * numReservoirBuffer * reservoirSize;
    m_raytracingOutput.m_rtxdiReservoirBuffer = m_device->createBuffer(rtxdiBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
    
    // ReSTIR GI
    numReservoirBuffer = 3;
    reservoirSize = sizeof(ReSTIRGI_PackedReservoir);
    rtxdiBufferInfo.size = reservoirBufferPixels * numReservoirBuffer * reservoirSize;
    m_raytracingOutput.m_restirGIReservoirBuffer = m_device->createBuffer(rtxdiBufferInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
    m_raytracingOutput.m_restirGIRadiance = AliasedResource(m_raytracingOutput.m_compositeOutput, ctx, m_downscaledExtent, VK_FORMAT_R16G16B16A16_SFLOAT, "ReSTIR GI Radiance");
    m_raytracingOutput.m_restirGIHitGeometry = createImageResource(ctx, "restir gi hit geometry", m_downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);

    DxvkBufferCreateInfo neeCacheInfo = rtxdiBufferInfo;
    int cellCount = NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION;
    neeCacheInfo.size = cellCount * NEE_CACHE_CELL_CANDIDATE_TOTAL_SIZE;
    m_raytracingOutput.m_neeCache = m_device->createBuffer(neeCacheInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
    neeCacheInfo.size = cellCount * NEE_CACHE_CELL_TASK_TOTAL_SIZE;
    m_raytracingOutput.m_neeCacheTask = m_device->createBuffer(neeCacheInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
    neeCacheInfo.size = cellCount * NEE_CACHE_SAMPLES * sizeof(NeeCache_PackedSample);
    m_raytracingOutput.m_neeCacheSample = m_device->createBuffer(neeCacheInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
    m_raytracingOutput.m_neeCacheThreadTask = createImageResource(ctx, "radiance cache thread task", m_downscaledExtent, VK_FORMAT_R32G32_UINT);

    // Displacement
    m_raytracingOutput.m_sharedTextureCoord = createImageResource(ctx, "displacement texture coordinate", m_downscaledExtent, VK_FORMAT_R32G32_SFLOAT);

    // Post Effect motion blur prefilter intermediate textures
    m_raytracingOutput.m_primarySurfaceFlagsIntermediateTexture1 = AliasedResource(m_raytracingOutput.m_secondaryPerceptualRoughness, ctx, m_downscaledExtent, VK_FORMAT_R8_UINT, "Primary Surface Flags Intermediate Texture 1");
    m_raytracingOutput.m_primarySurfaceFlagsIntermediateTexture2 = AliasedResource(m_raytracingOutput.m_sharedBiasCurrentColorMask, ctx, m_downscaledExtent, VK_FORMAT_R8_UINT, "Primary Surface Flags Intermediate Texture 2");

    // GPU print buffer
    {
      const uint32_t bufferLength = kMaxFramesInFlight;

      DxvkBufferCreateInfo gpuPrintBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      gpuPrintBufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
      gpuPrintBufferInfo.stages = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      gpuPrintBufferInfo.access = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
      gpuPrintBufferInfo.size = bufferLength * sizeof(GpuPrintBufferElement);
      
      m_raytracingOutput.m_gpuPrintBuffer = m_device->createBuffer(gpuPrintBufferInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXBuffer);
      GpuPrintBufferElement* gpuPrintElements = reinterpret_cast<GpuPrintBufferElement*>(m_raytracingOutput.m_gpuPrintBuffer->mapPtr(0));
     
      if (gpuPrintElements) {
        for (uint32_t i = 0; i < bufferLength; i++) {
          gpuPrintElements[i].invalidate();
        }
      }
    }

    // Let other systems know of the resize
    executeResizeEventList(m_onDownscaleResize, ctx, m_downscaledExtent);
  }

  void Resources::createTargetResources(Rc<DxvkContext>& ctx) {
    Logger::debug("Target resolution changed, recreating target resources");

    m_raytracingOutput.m_finalOutput = createImageResource(ctx, "final output", m_targetExtent, VK_FORMAT_R16G16B16A16_SFLOAT);

    // Post Effect intermediate textures
    m_raytracingOutput.m_postFxIntermediateTexture = createImageResource(ctx, "postfx intermediate texture", m_targetExtent, VK_FORMAT_R16G16B16A16_SFLOAT);

    // Let other systems know of the resize
    executeResizeEventList(m_onTargetResize, ctx, m_targetExtent);
  }

  Resources::MipMapResource Resources::createMipmapResource(Rc<DxvkContext> ctx, const VkExtent3D& extent, VkFormat format, int mipLevel, const char *name) {
    Resources::MipMapResource resource;
    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.extent = extent;
    desc.numLayers = 1;
    desc.mipLevels = mipLevel;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    desc.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    resource.image = ctx->getDevice()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, name);

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    viewInfo.format = format;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    resource.view.clear();
    for (int w = extent.width, h = extent.height; w > 1 && h > 1; w /= 2, h /= 2) {
      resource.view.push_back(ctx->getDevice()->createImageView(resource.image, viewInfo));
      viewInfo.minLevel++;
    }

    viewInfo.minLevel = 0;
    viewInfo.numLevels = mipLevel;
    resource.mipMapView = ctx->getDevice()->createImageView(resource.image, viewInfo);
    ctx->changeImageLayout(resource.image, VK_IMAGE_LAYOUT_GENERAL);
    return resource;
  }

  void Resources::executeResizeEventList(ResizeEventList& eventList, Rc<DxvkContext>& ctx, const VkExtent3D& extent) {
    for (auto iter = eventList.begin(); iter != eventList.end(); ) {
      auto callback = iter->lock();
      if (callback && (*callback)) {
        // Execute living events
        (*callback)(ctx, extent); // assumes these callbacks don't add more events...
        ++iter;
      } else {
        // Remove old events that are no longer in scope
        iter = eventList.erase(iter);
      }
    }
  }

  void Resources::executeFrameBeginEventList(FrameBeginEventList& eventList, Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    for (auto iter = eventList.begin(); iter != eventList.end(); ) {
      auto callback = iter->lock();
      if (callback && (*callback)) {
        // Dispatch living events
        (*callback)(ctx, downscaledExtent, targetExtent); // assumes these callbacks don't add more events...
        ++iter;
      } else {
        // Remove old events that are no longer in scope
        iter = eventList.erase(iter);
      }
    }
  }

  RtxPass::RtxPass(DxvkDevice* device) : m_events(
    [this](Rc<DxvkContext>& ctx, const VkExtent3D& extent) { onTargetResize(ctx, extent); },
    [this](Rc<DxvkContext>& ctx, const VkExtent3D& extent) { onDownscaledResize(ctx, extent); },
    [this](Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent, const VkExtent3D& downscaledExtent) { onFrameBegin(ctx, targetExtent, downscaledExtent); }) {

    device->getCommon()->getResources().addEventHandler(m_events);
  }

  void RtxPass::onFrameBegin(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    bool lastStatus = m_shouldDispatch;
    m_shouldDispatch = isActive();
    if (m_shouldDispatch != lastStatus) {
      if (m_shouldDispatch) {
        createTargetResource(ctx, targetExtent);
        createDownscaledResource(ctx, downscaledExtent);
      } else {
        releaseTargetResource();
        releaseDownscaledResource();
      }
    }
  }

  void RtxPass::onTargetResize(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) {
    if (m_shouldDispatch) {
      releaseTargetResource();
      createTargetResource(ctx, targetExtent);
    }
  }

  void RtxPass::onDownscaledResize(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    if (m_shouldDispatch) {
      releaseDownscaledResource();
      createDownscaledResource(ctx, downscaledExtent);
    }
  }
}  // namespace dxvk
