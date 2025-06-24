/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_nrd_context.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx.h"
#include "rtx_options.h"
#include "rtx/pass/nrd_args.h"
#include "../../util/util_string.h"
#include "../../util/util_globaltime.h"
#include <Shlwapi.h>
#include <filesystem>

namespace nrd {
  using pfnCreateInstance = Result(*)(const InstanceCreationDesc& instanceCreationDesc, Instance*& instance);
  using pfnDestroyInstance = void (*)(Instance& instance);
  using pfnGetLibraryDesc = const LibraryDesc& (*)();
  using pfnGetInstanceDesc = const InstanceDesc& (*)(const Instance& instance);
  using pfnSetCommonSettings = Result (*)(Instance& instance, const CommonSettings& commonSettings);
  using pfnSetDenoiserSettings = Result (*)(Instance& instance, Identifier identifier, const void* denoiserSettings);
  using pfnGetComputeDispatches = Result (*)(Instance& instance, const Identifier* identifiers, uint32_t identifiersNum, const DispatchDesc*& dispatchDescs, uint32_t& dispatchDescsNum);
  using pfnGetResourceTypeString = const char* (*)(ResourceType resourceType);
  using pfnGetDenoiserString = const char* (*)(Denoiser denoiser);

  struct DispatchNRD {
    pfnCreateInstance CreateInstance;
    pfnDestroyInstance DestroyInstance;
    pfnGetLibraryDesc GetLibraryDesc;
    pfnGetInstanceDesc GetInstanceDesc;
    pfnSetCommonSettings SetCommonSettings;
    pfnSetDenoiserSettings SetDenoiserSettings;
    pfnGetComputeDispatches GetComputeDispatches;
    pfnGetResourceTypeString GetResourceTypeString;
    pfnGetDenoiserString GetDenoiserString;
  } dispatch;

  HMODULE initialize() {
    HMODULE hModule;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) &initialize, &hModule);

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(hModule, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);

    std::filesystem::path path(modulePath);
    path.append("NRD.dll");
    HMODULE hNRD = LoadLibraryW(path.c_str());
    if (hNRD == NULL) {
      dxvk::Logger::err("Unable to load NRD, this feature will be disabled");
      return NULL;
    }

#define GET_PROC_MACRO(proc)  dispatch.##proc = (pfn##proc)GetProcAddress(hNRD, #proc)
    GET_PROC_MACRO(CreateInstance);
    GET_PROC_MACRO(DestroyInstance);
    GET_PROC_MACRO(GetLibraryDesc);
    GET_PROC_MACRO(GetInstanceDesc);
    GET_PROC_MACRO(SetCommonSettings);
    GET_PROC_MACRO(SetDenoiserSettings);
    GET_PROC_MACRO(GetComputeDispatches);
    GET_PROC_MACRO(GetResourceTypeString);
    GET_PROC_MACRO(GetDenoiserString);
#undef GET_PROC_MACRO

    const LibraryDesc& desc = dispatch.GetLibraryDesc();
    if (desc.versionMajor != NRD_VERSION_MAJOR || desc.versionMinor != NRD_VERSION_MINOR || desc.versionBuild != NRD_VERSION_BUILD) {
      dxvk::Logger::err("Incorrect version of NRD has been loaded.  Ensure the correct DLLs have been copied to the Remix binary directory.  NRD disabled.");
      FreeLibrary(hNRD);
      return NULL;
    }

    return hNRD;
  }
}

namespace dxvk {
  static void* NrdAllocate(void* userArg, size_t size, size_t alignment) {
    return malloc(size);
  }

  static void* NrdReallocate(void* userArg, void* memory, size_t size, size_t alignment) {
    return realloc(memory, size);
  }

  static void NrdFree(void* userArg, void* memory) {
    free(memory);
  }

  static VkFormat TranslateFormat(nrd::Format format) {
    switch (format) {
    case nrd::Format::R16_UINT:
      return VK_FORMAT_R16_UINT;
    case nrd::Format::R16_UNORM:
      return VK_FORMAT_R16_UNORM;
    case nrd::Format::R32_SFLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case nrd::Format::R16_SFLOAT:
      return VK_FORMAT_R16_SFLOAT;
    case nrd::Format::RG16_SFLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case nrd::Format::RG32_SFLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    case nrd::Format::R8_UNORM:
      return VK_FORMAT_R8_UNORM;
    case nrd::Format::RG8_UNORM:
      return VK_FORMAT_R8G8_UNORM;
    case nrd::Format::RGBA8_UNORM:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case nrd::Format::RGBA16_SFLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case nrd::Format::RGBA32_SFLOAT:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case nrd::Format::RGBA32_UINT:
      return VK_FORMAT_R32G32B32A32_UINT;
    case nrd::Format::R32_UINT:
      return VK_FORMAT_R32_UINT;
    case nrd::Format::RG32_UINT:
      return VK_FORMAT_R32G32_UINT;
    case nrd::Format::R11_G11_B10_UFLOAT:
      return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case nrd::Format::R10_G10_B10_A2_UNORM:
      return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    case nrd::Format::R10_G10_B10_A2_UINT:
      return VK_FORMAT_A2R10G10B10_UINT_PACK32;
    default:
      assert(!"Unknown/Unsupported format.");
      return VK_FORMAT_UNDEFINED;
    }
  }

  NRDContext::NRDContext(DxvkDevice* device, DenoiserType type)
    : CommonDeviceObject(device), m_vkd(device->vkd()), m_type(type) {
    m_hNRD = nrd::initialize();

    if (m_hNRD == NULL) {
      return;
    }

    m_settings.initialize(nrd::dispatch.GetLibraryDesc(), device->instance()->config(), type);
    
    // Disable the replace direct specular HitT with indirect specular HitT if we are using combined denoiser.
    // Because in combined denoiser the direct and indirect signals are denoised together,
    // in such case we will break the denoiser if replace the direct with indirect specular HitT.
    RtxOptions::replaceDirectSpecularHitTWithIndirectSpecularHitT.setDeferred(RtxOptions::denoiseDirectAndIndirectLightingSeparately());
  }

  NRDContext::~NRDContext() {
    destroyResources();
    destroyPipelines();

    if (m_hNRD != NULL) {
      FreeLibrary(m_hNRD);
    }
  }

  void NRDContext::onDestroy() {
    m_cbData = nullptr;
  }

  const char* NRDContext::getDenoiserName() const {
    switch (m_type) {
    case DenoiserType::DirectAndIndirectLight: return "Direct And Indirect Light";
    case DenoiserType::DirectLight: return "Direct Light";
    case DenoiserType::IndirectLight: return "Indirect Light";
    case DenoiserType::Secondaries: return "Secondaries";
    case DenoiserType::Reference: return "Reference";
    default: assert(0); return "<invalid argument>";  break;
    }
  }

  void NRDContext::prepareResources(
    Rc<DxvkContext> ctx,
    const Resources::RaytracingOutput& rtOutput) {

    if (!m_cbData) {
      m_cbData = std::make_unique<RtxStagingDataAlloc>(
        device(),
        "RtxStagingDataAlloc: NRD CB",
        (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    const uint16_t width = static_cast<uint16_t>(rtOutput.m_compositeOutputExtent.width);
    const uint16_t height = static_cast<uint16_t>(rtOutput.m_compositeOutputExtent.height);

    bool bCreateDenoiser = m_denoiser != m_settings.m_denoiserDesc.denoiser ||
      m_settings.m_commonSettings.resourceSize[0] != width ||
      m_settings.m_commonSettings.resourceSize[1] != height ||
      (m_transientTex.size() == 0 && m_permanentTex.size() == 0);

    if (bCreateDenoiser) {
      Logger::debug(str::format("[RTX] NRD: initializing denoiser ", getDenoiserName()));

      m_denoiser = m_settings.m_denoiserDesc.denoiser;

      if (m_settings.m_denoiserDesc.identifier == UINT32_MAX) {
        static uint32_t uniqueId = 0;
        m_settings.m_denoiserDesc.identifier = uniqueId++;
      }

      // Destroy previous graphics state
      {
        ctx->getDevice()->waitForIdle();
        destroyResources();
        destroyPipelines();
      }

      // Initialize new graphics state
      {
        m_settings.m_commonSettings.resourceSizePrev[0] = width;
        m_settings.m_commonSettings.resourceSizePrev[1] = height;
        m_settings.m_commonSettings.resourceSize[0] = width;
        m_settings.m_commonSettings.resourceSize[1] = height;
        m_settings.m_commonSettings.rectSizePrev[0] = width;
        m_settings.m_commonSettings.rectSizePrev[1] = height;
        m_settings.m_commonSettings.rectSize[0] = width;
        m_settings.m_commonSettings.rectSize[1] = height;

        if (!m_denoiserInstance) {
          nrd::InstanceCreationDesc instanceCreationDesc;
          instanceCreationDesc.allocationCallbacks.Allocate = NrdAllocate;
          instanceCreationDesc.allocationCallbacks.Reallocate = NrdReallocate;
          instanceCreationDesc.allocationCallbacks.Free = NrdFree;
          instanceCreationDesc.allocationCallbacks.userArg = nullptr; // ? ToDo
          instanceCreationDesc.denoisersNum = 1;
          instanceCreationDesc.denoisers = &m_settings.m_denoiserDesc;

          THROW_IF_FALSE(nrd::dispatch.CreateInstance(instanceCreationDesc, m_denoiserInstance) == nrd::Result::SUCCESS);
        }

        createPipelines();

        createResources(ctx, rtOutput);

        m_settings.m_resetHistory = true;
      }
    }

    if (m_settings.m_commonSettings.enableValidation && !m_validationTex.isValid()) {
      m_validationTex = Resources::createImageResource(ctx, "nrd validation texture", rtOutput.m_compositeOutputExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    }
  }

  DxvkSamplerCreateInfo getSamplerInfo(const nrd::Sampler& nrdSampler) {

    DxvkSamplerCreateInfo samplerInfo;

    if (nrdSampler == nrd::Sampler::NEAREST_CLAMP) {
      samplerInfo.magFilter = VK_FILTER_NEAREST;
      samplerInfo.minFilter = VK_FILTER_NEAREST;
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    } else {
      samplerInfo.magFilter = VK_FILTER_LINEAR;
      samplerInfo.minFilter = VK_FILTER_LINEAR;
      samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }

    samplerInfo.mipmapLodBias = 0.0f;
    samplerInfo.mipmapLodMin = 0.0f;
    samplerInfo.mipmapLodMax = FLT_MAX;
    samplerInfo.useAnisotropy = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    if (nrdSampler == nrd::Sampler::NEAREST_CLAMP || nrdSampler == nrd::Sampler::LINEAR_CLAMP) {
      samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    } else {
      samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    }

    samplerInfo.compareToDepth = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.borderColor = { 0.f, 0.f, 0.f, 1.f }; // Opaque black
    samplerInfo.usePixelCoord = VK_FALSE;

    return samplerInfo;
  }

  void NRDContext::createResources(
    Rc<DxvkContext> ctx,
    const Resources::RaytracingOutput& rtOutput) {
    const nrd::InstanceDesc& instanceDesc = nrd::dispatch.GetInstanceDesc(*m_denoiserInstance);

    DxvkImageCreateInfo desc;
    desc.type = VK_IMAGE_TYPE_2D;
    desc.flags = 0;
    desc.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    desc.numLayers = 1;
    desc.mipLevels = 1;
    // VK_IMAGE_USAGE_TRANSFER_DST_BIT needed for clears in NRD
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    desc.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    desc.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    desc.tiling = VK_IMAGE_TILING_OPTIMAL;
    desc.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;
    viewInfo.numLevels = 1;

    const uint32_t textureCount = instanceDesc.permanentPoolSize + instanceDesc.transientPoolSize;

    // Take a copy so we can pull from the bag without aliasing.
    SharedTransientPool sharedPoolCopy = m_sharedTransientTex;

    for (uint32_t i = 0; i < textureCount; i++) {

      const bool isPermanent = (i < instanceDesc.permanentPoolSize);

      const nrd::TextureDesc& nrdTextureDesc = isPermanent
        ? instanceDesc.permanentPool[i]
        : instanceDesc.transientPool[i - instanceDesc.permanentPoolSize];

      viewInfo.format = desc.format = TranslateFormat(nrdTextureDesc.format);
      
      desc.extent = { 
        util::ceilDivide(m_settings.m_commonSettings.resourceSize[0], nrdTextureDesc.downsampleFactor),
        util::ceilDivide(m_settings.m_commonSettings.resourceSize[1], nrdTextureDesc.downsampleFactor),
        1 };

      if (isPermanent) {
        // Always allocate these
        Resources::Resource resource;
        resource.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "nrd permament tex");
        resource.view = device()->createImageView(resource.image, viewInfo);

        ctx->changeImageLayout(resource.image, VK_IMAGE_LAYOUT_GENERAL);
        m_permanentTex.emplace_back(std::move(resource));
      }
      else {
        DxvkHashState result;
        result.add(std::hash<size_t>()(desc.hash()));
        result.add(std::hash<size_t>()(viewInfo.hash()));
        const size_t imageHash = result;

        // See if we can find an existing transient from the pool
        auto transientResource = sharedPoolCopy.find(imageHash);

        if (transientResource != sharedPoolCopy.end() && transientResource->second.lock()) {
          // Cache in this instance
          m_transientTex.emplace_back(transientResource->second.lock());

          // Take one for this pass and remove it so it cannot be shared
          sharedPoolCopy.erase(imageHash);
        } else {
          // If the weak_ptr is now dead, then remove it
          if (transientResource != sharedPoolCopy.end() && !transientResource->second.lock()) {
            m_sharedTransientTex.erase(imageHash);
          }
          Resources::Resource resource;
          resource.image = device()->createImage(desc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXRenderTarget, "nrd transient tex");
          resource.view = device()->createImageView(resource.image, viewInfo);

          ctx->changeImageLayout(resource.image, VK_IMAGE_LAYOUT_GENERAL);

          // Create in this instance
          m_transientTex.emplace_back(std::make_shared<Resource>(resource));

          // NOTE: Insert into the main pool (not copy)
          m_sharedTransientTex[imageHash] = m_transientTex.back();
        }
      }
    }
  }

  Resources::Resource NRDContext::getValidationTexture() const {
    return m_validationTex;
  }

  void NRDContext::createPipelines() {

    const nrd::InstanceDesc& instanceDesc = nrd::dispatch.GetInstanceDesc(*m_denoiserInstance);

    const nrd::DescriptorPoolDesc& descriptorDesc = instanceDesc.descriptorPoolDesc;
    const nrd::SPIRVBindingOffsets spirvOffsets = nrd::dispatch.GetLibraryDesc().spirvBindingOffsets;

    // Create constant buffer
    // With NRD, using width + height + method, you receive a description of the pipelines
    // to create. You receive a max constant buffer size across all pipelines.
    // Only with a specific set of NRD settings, you get the dispatch descriptions
    // which include per pipeline constant buffer size and texture pool assignments
    DxvkBufferCreateInfo cbufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    cbufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    cbufferInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    cbufferInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    cbufferInfo.size = instanceDesc.constantBufferMaxDataSize;


    // Create static sampler binding infos
    std::vector<VkDescriptorSetLayoutBinding> samplersBindInfo;
    {
      samplersBindInfo.resize(instanceDesc.samplersNum);
      m_staticSamplers.resize(instanceDesc.samplersNum);

      for (uint32_t i = 0; i < instanceDesc.samplersNum; i++) {

        DxvkSamplerCreateInfo samplerInfo;
        samplerInfo = getSamplerInfo(instanceDesc.samplers[i]);

        // Create sampler 
        m_staticSamplers[i] = device()->createSampler(samplerInfo);

        // Bind info
        const uint32_t reg = static_cast<uint32_t>(instanceDesc.samplers[i]);
        samplersBindInfo[i].binding = spirvOffsets.samplerOffset + reg;
        samplersBindInfo[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        samplersBindInfo[i].descriptorCount = 1;
        samplersBindInfo[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        samplersBindInfo[i].pImmutableSamplers = nullptr;
      }
    }

    // Create binding infos for all the pipelines
    for (uint32_t i = 0; i < instanceDesc.pipelinesNum; i++) {

      const nrd::PipelineDesc& nrdPipelineDesc = instanceDesc.pipelines[i];
      const nrd::ComputeShaderDesc& nrdComputeShader = nrdPipelineDesc.computeShaderSPIRV;

      // Start with static samplers bind infos
      std::vector<VkDescriptorSetLayoutBinding> bindInfo(samplersBindInfo.begin(), samplersBindInfo.end());
      uint32_t cbBindInfoIndex = ComputePipeline::kInvalidIndex;
      uint32_t resourcesStartIndex = ComputePipeline::kInvalidIndex;
      {
        // Constant Buffer
        if (nrdPipelineDesc.hasConstantData)
        {
          VkDescriptorSetLayoutBinding binding{};
          binding.binding = spirvOffsets.constantBufferOffset + instanceDesc.constantBufferRegisterIndex;
          binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          binding.descriptorCount = 1;
          binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
          binding.pImmutableSamplers = nullptr;

          cbBindInfoIndex = (uint32_t)bindInfo.size();
          bindInfo.emplace_back(std::move(binding));
        }

        // Textures
        resourcesStartIndex = (uint32_t)bindInfo.size();
        for (uint32_t j = 0; j < nrdPipelineDesc.resourceRangesNum; j++) {

          const nrd::ResourceRangeDesc& nrdDescriptorRange = nrdPipelineDesc.resourceRanges[j];

          const bool isSRV = nrdDescriptorRange.descriptorType == nrd::DescriptorType::TEXTURE;
          VkDescriptorType descType = isSRV ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
          uint32_t vkBaseOffset = isSRV ? spirvOffsets.textureOffset : spirvOffsets.storageTextureAndBufferOffset;

          assert(nrdDescriptorRange.baseRegisterIndex == 0);
          for (uint32_t k = 0; k < nrdDescriptorRange.descriptorsNum; k++) {

            VkDescriptorSetLayoutBinding binding{};
            binding.binding = vkBaseOffset + k;
            binding.descriptorType = descType;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binding.pImmutableSamplers = nullptr;

            bindInfo.emplace_back(std::move(binding));
          }
        }
      }

      // Create descriptor set layout   
      VkDescriptorSetLayout descriptorSetLayout;
      {
        VkDescriptorSetLayoutCreateInfo dsetInfo;
        dsetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsetInfo.pNext = nullptr;
        dsetInfo.flags = 0;
        dsetInfo.bindingCount = (uint32_t)bindInfo.size();
        dsetInfo.pBindings = &bindInfo[0];

        VK_THROW_IF_FAILED(m_vkd->vkCreateDescriptorSetLayout(m_vkd->device(), &dsetInfo, nullptr, &descriptorSetLayout));
      }

      // Create pipeline
      VkPipelineLayout pipelineLayout = createPipelineLayout(descriptorSetLayout);
      VkPipeline pipeline = createPipeline(nrdComputeShader, nrdPipelineDesc, pipelineLayout);

      ComputePipeline computePipeline;
      computePipeline.descriptorSetLayout = descriptorSetLayout;
      computePipeline.pipelineLayout = pipelineLayout;
      computePipeline.pipeline = pipeline;
      computePipeline.constantBufferIndex = cbBindInfoIndex;
      computePipeline.resourcesStartIndex = resourcesStartIndex;
      computePipeline.bindings = std::move(bindInfo);
      m_computePipelines.emplace_back(std::move(computePipeline));
    }
  }

  VkPipelineLayout NRDContext::createPipelineLayout(VkDescriptorSetLayout dsetLayout) {

    VkPipelineLayoutCreateInfo pipeInfo;
    pipeInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeInfo.pNext = nullptr;
    pipeInfo.flags = 0;
    pipeInfo.setLayoutCount = 1;
    pipeInfo.pSetLayouts = &dsetLayout;
    pipeInfo.pushConstantRangeCount = 0;
    pipeInfo.pPushConstantRanges = nullptr;

    VkPipelineLayout result = VK_NULL_HANDLE;
    VK_THROW_IF_FAILED(m_vkd->vkCreatePipelineLayout(m_vkd->device(), &pipeInfo, nullptr, &result));

    return result;
  }

  VkPipeline NRDContext::createPipeline(const nrd::ComputeShaderDesc& nrdCS,
    const nrd::PipelineDesc& nrdPipelineDesc,
    const VkPipelineLayout& pipelineLayout) {

    VkShaderModuleCreateInfo shaderInfo;
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.pNext = nullptr;
    shaderInfo.flags = 0;
    shaderInfo.codeSize = nrdCS.size;
    shaderInfo.pCode = (const uint32_t*)nrdCS.bytecode;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VK_THROW_IF_FAILED(m_vkd->vkCreateShaderModule(m_vkd->device(), &shaderInfo, nullptr, &shaderModule));

    VkPipelineShaderStageCreateInfo stageInfo;
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext = nullptr;
    stageInfo.flags = 0;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";
    stageInfo.pSpecializationInfo = nullptr;

    VkComputePipelineCreateInfo pipeInfo;
    pipeInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.pNext = nullptr;
    pipeInfo.flags = 0;
    pipeInfo.stage = stageInfo;
    pipeInfo.layout = pipelineLayout;
    pipeInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipeInfo.basePipelineIndex = -1;

    VkPipeline result = VK_NULL_HANDLE;
    const VkResult status = m_vkd->vkCreateComputePipelines(m_vkd->device(), VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &result);

    m_vkd->vkDestroyShaderModule(m_vkd->device(), shaderModule, nullptr);

    if (status != VK_SUCCESS) {
      throw DxvkError("Dxvk: Failed to create meta clear compute pipeline");
    }

    return result;
  }

  const Resources::Resource* NRDContext::getTexture(const nrd::ResourceDesc& resource, const DxvkDenoise::Input& inputs, const DxvkDenoise::Output& outputs) {

    switch (resource.type)
    {
    case nrd::ResourceType::IN_MV:
      return inputs.motionVector;
    case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
      return inputs.normal_roughness;
    case nrd::ResourceType::IN_VIEWZ:
      return inputs.linearViewZ;
    case nrd::ResourceType::IN_DIFF_CONFIDENCE:
      return inputs.confidence;
    case nrd::ResourceType::IN_SPEC_CONFIDENCE:
      return inputs.confidence;
    case nrd::ResourceType::IN_DISOCCLUSION_THRESHOLD_MIX:
      return inputs.disocclusionThresholdMix;
    case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
      return inputs.diffuse_hitT;
    case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:
      return inputs.specular_hitT;
    case nrd::ResourceType::IN_SIGNAL:
      return inputs.reference;
    case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
      return outputs.diffuse_hitT;
    case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:
      return outputs.specular_hitT;
    case nrd::ResourceType::OUT_SIGNAL:
      return outputs.reference;
    case nrd::ResourceType::TRANSIENT_POOL:
      assert(resource.indexInPool < m_transientTex.size());
      return m_transientTex[resource.indexInPool].get();
    case nrd::ResourceType::PERMANENT_POOL:
      assert(resource.indexInPool < m_permanentTex.size());
      return &m_permanentTex[resource.indexInPool];
    case nrd::ResourceType::OUT_VALIDATION:
      return &m_validationTex;
    default:
      throw DxvkError("Unavailable resource type");
    }
    return nullptr;
  }

  void NRDContext::dispatch(
    Rc<DxvkContext> ctx,
    DxvkBarrierSet& barriers,
    const SceneManager& sceneManager,
    const Resources::RaytracingOutput& rtOutput,
    const DxvkDenoise::Input& inputs,
    const DxvkDenoise::Output& outputs) {
    if (m_hNRD == NULL) {
      return;
    }

    m_settings.m_resetHistory |= inputs.reset;

    ScopedGpuProfileZone(ctx, "NRD");
    static_cast<RtxContext*>(ctx.ptr())->setFramePassStage(RtxFramePassStage::NRD);

    prepareResources(ctx, rtOutput);

    updateNRDSettings(sceneManager, inputs, rtOutput);

    std::vector<Rc<DxvkImageView>> pInputs, pOutputs;
    if (m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::REFERENCE) {
      pInputs = { inputs.reference->view, inputs.normal_roughness->view, inputs.linearViewZ->view, inputs.motionVector->view };
      pOutputs = { outputs.reference->view };
    } else {
      pInputs = { inputs.diffuse_hitT->view, inputs.specular_hitT->view, inputs.normal_roughness->view, inputs.linearViewZ->view, inputs.motionVector->view };
      pOutputs = { outputs.diffuse_hitT->view, outputs.specular_hitT->view };
    }

    for (auto input : pInputs) {
      barriers.accessImage(
        input->image(), input->imageSubresources(),
        input->imageInfo().layout, input->imageInfo().stages, input->imageInfo().access,
        input->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    for (auto output : pOutputs) {
      barriers.accessImage(
        output->image(), output->imageSubresources(),
        output->imageInfo().layout, output->imageInfo().stages, output->imageInfo().access,
        output->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    }
    barriers.recordCommands(ctx->getCommandList());

    auto needsCustomView = [&](const Rc<DxvkImageView>& view, bool bStorage) {

      VkImageUsageFlags usage = view->info().usage;
      bool usageMatches = 
        bStorage 
        ? (usage & VK_IMAGE_USAGE_STORAGE_BIT && usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        : (usage & VK_IMAGE_USAGE_SAMPLED_BIT);
      return !usageMatches;
    };

    auto createImageViewCreateInfo = [&](DxvkImage& image, bool bStorage) {

      DxvkImageViewCreateInfo viewInfo = {};
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = image.info().format;
      viewInfo.usage = bStorage ? VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLevel = 0;
      viewInfo.numLevels = 1;
      viewInfo.minLayer = 0;
      viewInfo.numLayers = 1;

      return viewInfo;
    };

    // Prepare and run dispatches
    const nrd::InstanceDesc& instanceDesc = nrd::dispatch.GetInstanceDesc(*m_denoiserInstance);
    {
      uint32_t dispatchDescNum = 0;
      const nrd::DispatchDesc* dispatchDescs = nullptr;

      nrd::dispatch.GetComputeDispatches(*m_denoiserInstance, &m_settings.m_denoiserDesc.identifier, 1, dispatchDescs, dispatchDescNum);

      for (uint32_t i = 0; i < dispatchDescNum; i++) {

        const nrd::DispatchDesc& dispatchDesc = dispatchDescs[i];
        const nrd::PipelineDesc& pipelineDesc = instanceDesc.pipelines[dispatchDesc.pipelineIndex];
        const ComputePipeline& computePipeline = m_computePipelines[dispatchDesc.pipelineIndex];

        ScopedGpuProfileZoneDynamicZ(ctx, dispatchDesc.name);

        VkDescriptorSet descriptorSet = ctx->allocateDescriptorSet(computePipeline.descriptorSetLayout, "NRD descriptor set");

        std::vector<VkWriteDescriptorSet> descriptorWriteSets;

        // Variables referenced inside descriptorWriteSets must have the same lifetime, so preallocate
        std::vector<VkDescriptorImageInfo> samplerDescs{ instanceDesc.samplersNum };
        VkDescriptorBufferInfo             cbDesc{};
        std::vector<VkDescriptorImageInfo> imageDesc{ dispatchDesc.resourcesNum };

        // Static sampler descriptors
        for (size_t i = 0; i < instanceDesc.samplersNum; i++) {

          const VkDescriptorSetLayoutBinding& binding = computePipeline.bindings[i];
          samplerDescs[i].sampler = m_staticSamplers[i]->handle();
          samplerDescs[i].imageView = VK_NULL_HANDLE;
          samplerDescs[i].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
          descriptorWriteSets.emplace_back(DxvkDescriptor::texture(descriptorSet, &samplerDescs[i], binding.descriptorType, binding.binding));
          
          ctx->getCommandList()->trackResource<DxvkAccess::None>(m_staticSamplers[i]);
        }

        // Update constants
        // The ReLAX A-trous passes use the same shader pipeline with different constant values.
        // In this case, the default constant buffer cannot guarantee values got updated in each pass.
        // Use RtxStagingDataAlloc to fix this issue.
        if (dispatchDesc.constantBufferDataSize > 0) {
          // Setting alignment to device limit minUniformBufferOffsetAlignment because the offset value should be its multiple.
          // See https://vulkan.lunarg.com/doc/view/1.2.189.2/windows/1.2-extensions/vkspec.html#VUID-VkWriteDescriptorSet-descriptorType-00327
          const auto& devInfo = device()->properties().core.properties;
          VkDeviceSize alignment = devInfo.limits.minUniformBufferOffsetAlignment;
          DxvkBufferSlice cbSlice = m_cbData->alloc(alignment, dispatchDesc.constantBufferDataSize);
          ctx->getCommandList()->trackResource<DxvkAccess::Write>(cbSlice.buffer());
          memcpy(cbSlice.mapPtr(0), dispatchDesc.constantBufferData, dispatchDesc.constantBufferDataSize);

          const VkDescriptorSetLayoutBinding& cb = computePipeline.bindings[computePipeline.constantBufferIndex];
          assert(cb.descriptorCount == 1);

          cbDesc = cbSlice.getDescriptor().buffer;
          descriptorWriteSets.emplace_back(DxvkDescriptor::buffer(descriptorSet, &cbDesc, cb.descriptorType, cb.binding));

          barriers.accessBuffer(cbSlice.getSliceHandle(),
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            cbSlice.buffer()->info().stages,
            cbSlice.buffer()->info().access);
        }

        // Gather needed resource infos for the pipeline
        for (size_t i = 0; i < dispatchDesc.resourcesNum; i++) {

          const nrd::ResourceRangeDesc& descRange = pipelineDesc.resourceRanges[i];
          const VkDescriptorSetLayoutBinding& binding = computePipeline.bindings[computePipeline.resourcesStartIndex + i];
          assert(binding.descriptorCount == 1);

          const nrd::ResourceDesc& resource = dispatchDesc.resources[i];

          const Resources::Resource* texture = getTexture(resource, inputs, outputs);

          const bool bStorage = resource.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;

          Rc<DxvkImageView> imageView;

          if (needsCustomView(texture->view, bStorage)) {
            DxvkImageViewCreateInfo viewCreateInfo = createImageViewCreateInfo(*texture->image.ptr(), bStorage);
            imageView = device()->createImageView(texture->image, viewCreateInfo);
          } else {
            imageView = texture->view;
          }

          // Ensure resources are kept alive
          ctx->getCommandList()->trackResource<DxvkAccess::None>(imageView);
          if (bStorage) {
            ctx->getCommandList()->trackResource<DxvkAccess::Write>(texture->image);
          } else {
            ctx->getCommandList()->trackResource<DxvkAccess::Read>(texture->image);
          }

          descriptorWriteSets.emplace_back(DxvkDescriptor::texture(descriptorSet, &imageDesc[i], *imageView, binding.descriptorType, binding.binding));

          // Create a barrier
          barriers.accessImage(
            texture->image, imageView->imageSubresources(),
            imageView->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, imageView->imageInfo().access,
            imageView->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            bStorage ? VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_READ_BIT);

#ifdef REMIX_DEVELOPMENT
          // Cache NRD image views
          static_cast<RtxContext*>(ctx.ptr())->cacheResourceAliasingImageView(imageView);
#endif
        }

        barriers.recordCommands(ctx->getCommandList());

        ctx->getCommandList()->updateDescriptorSets(descriptorWriteSets.size(), descriptorWriteSets.data());

        ctx->getCommandList()->cmdBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipeline);
        ctx->getCommandList()->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.pipelineLayout, descriptorSet, 0, nullptr);

        ctx->getCommandList()->cmdDispatch(dispatchDesc.gridWidth, dispatchDesc.gridHeight, 1);

        for (auto output : pOutputs) {
          ctx->getCommandList()->trackResource<DxvkAccess::None>(output);
          ctx->getCommandList()->trackResource<DxvkAccess::Write>(output->image());
        }
      }
    }

    // Transition external resources back
    {
      for (auto input : pInputs) {
        barriers.accessImage(
          input->image(), input->imageSubresources(),
          input->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
          input->imageInfo().layout, input->imageInfo().stages, input->imageInfo().access);
      }

      for (auto output : pOutputs) {
        barriers.accessImage(
          output->image(), output->imageSubresources(),
          output->imageInfo().layout, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
          output->imageInfo().layout, output->imageInfo().stages, output->imageInfo().access);
      }
    }

    m_settings.m_resetHistory = false;
  }

  void NRDContext::updateNRDSettings(
    const SceneManager& sceneManager,
    const DxvkDenoise::Input& inputs,
    const Resources::RaytracingOutput& rtOutput) {

    if (m_settings.m_denoiserDesc.denoiser != nrd::Denoiser::REFERENCE) {
      // Don't allow adaptive scaling for direct light in ReBlur
      if (m_settings.m_denoiserDesc.denoiser != nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR || m_settings.m_type != dxvk::DenoiserType::DirectLight) {
        updateAdaptiveScaling(inputs.diffuse_hitT->image->info().extent);
      }

      if (RtxOptions::adaptiveAccumulation()) {
        m_settings.updateAdaptiveAccumulation(GlobalTime::get().deltaTimeMs());
      }
    }

    // nrd::SetCommonSettings
    nrd::CommonSettings& commonSettings = m_settings.m_commonSettings;
    {
      const auto& camera = sceneManager.getCamera();

      const uint16_t width = static_cast<uint16_t>(rtOutput.m_compositeOutputExtent.width);
      const uint16_t height = static_cast<uint16_t>(rtOutput.m_compositeOutputExtent.height);

      m_settings.m_commonSettings.resourceSizePrev[0] = width;
      m_settings.m_commonSettings.resourceSizePrev[1] = height;
      m_settings.m_commonSettings.resourceSize[0] = width;
      m_settings.m_commonSettings.resourceSize[1] = height;
      m_settings.m_commonSettings.rectSizePrev[0] = width;
      m_settings.m_commonSettings.rectSizePrev[1] = height;
      m_settings.m_commonSettings.rectSize[0] = width;
      m_settings.m_commonSettings.rectSize[1] = height;

      // Note: Convert camera matrices to Matrix4 for the sake of NRD (only accepts float matrices).
      const Matrix4 viewMatrix = camera.getWorldToView();
      const Matrix4 prevViewMatrix = camera.getPreviousWorldToView();
      const Matrix4 viewToProjectionMatrix = camera.getViewToProjection();
      const Matrix4 prevViewToProjectionMatrix = camera.getPreviousViewToProjection();

      // Check whether camera has changed
      if (m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::REFERENCE &&
          (memcmp(commonSettings.worldToViewMatrix, viewMatrix.data, sizeof(Matrix4)) != 0 ||
           memcmp(commonSettings.viewToClipMatrix, viewToProjectionMatrix.data, sizeof(Matrix4)) != 0)) {
        m_settings.m_resetHistory = true;
      }

      // Pass non-jittered camera matrices
      memcpy(commonSettings.worldToViewMatrix, viewMatrix.data, sizeof(Matrix4));
      memcpy(commonSettings.worldToViewMatrixPrev, prevViewMatrix.data, sizeof(Matrix4));
      memcpy(commonSettings.viewToClipMatrix, viewToProjectionMatrix.data, sizeof(Matrix4));
      memcpy(commonSettings.viewToClipMatrixPrev, prevViewToProjectionMatrix.data, sizeof(Matrix4));

      // Note: Ensure matrix sizes are compatible (this could be done better with C++20's std::bit_cast rather than using
      // std::memcpy).
      static_assert(sizeof(commonSettings.worldToViewMatrix) == sizeof(viewMatrix));
      static_assert(sizeof(commonSettings.worldToViewMatrixPrev) == sizeof(prevViewMatrix));
      static_assert(sizeof(commonSettings.viewToClipMatrix) == sizeof(viewToProjectionMatrix));
      static_assert(sizeof(commonSettings.viewToClipMatrixPrev) == sizeof(prevViewToProjectionMatrix));

      float jitterVec[2];
      camera.getJittering(jitterVec);
      commonSettings.isMotionVectorInWorldSpace = true;
      commonSettings.motionVectorScale[0] = commonSettings.isMotionVectorInWorldSpace ? 1.0f : 1.0f / width;
      commonSettings.motionVectorScale[1] = commonSettings.isMotionVectorInWorldSpace ? 1.0f : 1.0f / height;
      commonSettings.motionVectorScale[2] = commonSettings.motionVectorScale[1]; // Enable 2.5D Motion Vector in NRD, we use the scale that matches previous default NRD scale on Z (mv = mv.xyz * mvScale.xyy)
      commonSettings.cameraJitterPrev[0] = commonSettings.cameraJitter[0];
      commonSettings.cameraJitterPrev[1] = commonSettings.cameraJitter[1];
      commonSettings.cameraJitter[0] = jitterVec[0] / static_cast<float>(width);
      commonSettings.cameraJitter[1] = jitterVec[1] / static_cast<float>(height);
      commonSettings.timeDeltaBetweenFrames = GlobalTime::get().deltaTimeMs();
      commonSettings.frameIndex = device()->getCurrentFrameId();
      commonSettings.accumulationMode = m_settings.m_resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

      auto* cameraTeleportDirectionInfo = sceneManager.getRayPortalManager().getCameraTeleportationRayPortalDirectionInfo();

      if (cameraTeleportDirectionInfo && RtxOptions::useVirtualShadingNormalsForDenoising()) {
        memcpy(commonSettings.worldPrevToWorldMatrix, &cameraTeleportDirectionInfo->portalToOpposingPortalDirection, sizeof(Matrix4));
      } else {
        static const auto identity = Matrix4{};
        memcpy(commonSettings.worldPrevToWorldMatrix, &identity, sizeof(Matrix4));
      }

      commonSettings.isHistoryConfidenceAvailable = inputs.confidence != nullptr;
      commonSettings.isDisocclusionThresholdMixAvailable = inputs.disocclusionThresholdMix != nullptr;

      THROW_IF_FALSE(nrd::dispatch.SetCommonSettings(*m_denoiserInstance, commonSettings) == nrd::Result::SUCCESS);
    }

    // nrd::SetDenoiserSettings
    {
      const void* denoiserSettings = nullptr;

      switch (m_settings.m_denoiserDesc.denoiser) {
      case nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR:
        denoiserSettings = static_cast<void*>(&m_settings.m_reblurSettings);
        break;
      case nrd::Denoiser::RELAX_DIFFUSE_SPECULAR:
        denoiserSettings = static_cast<void*>(&m_settings.m_relaxSettings);
        break;
      case nrd::Denoiser::REFERENCE:
        denoiserSettings = static_cast<void*>(&m_settings.m_referenceSettings);
        break;
      default:
        assert("Invalid option");
      };

      THROW_IF_FALSE(nrd::dispatch.SetDenoiserSettings(*m_denoiserInstance, m_settings.m_denoiserDesc.identifier, denoiserSettings) == nrd::Result::SUCCESS);
    }
  }

  void NRDContext::updateAdaptiveScaling(const VkExtent3D& renderSize) {
    // This default height is hard-code to align with NRD default settings (1440p),
    // we probably need to move this to settings later
    constexpr float defaultScreenHeight = 1440.0f;
    float radiusResolutionScale = RtxOptions::adaptiveResolutionDenoising() ? static_cast<float>(std::min(renderSize.width, renderSize.height)) / defaultScreenHeight : 1.0f;
    if (m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) {
      m_settings.m_reblurSettings.maxBlurRadius = m_settings.m_reblurInternalBlurRadius.maxBlurRadius > 0.0f ?
        std::max(1.0f, round(m_settings.m_reblurInternalBlurRadius.maxBlurRadius * radiusResolutionScale)) : 0.0f;
      m_settings.m_reblurSettings.diffusePrepassBlurRadius = m_settings.m_reblurInternalBlurRadius.diffusePrepassBlurRadius > 0.0f ?
        std::max(1.0f, round(m_settings.m_reblurInternalBlurRadius.diffusePrepassBlurRadius * radiusResolutionScale)) : 0.0f;
      m_settings.m_reblurSettings.specularPrepassBlurRadius = m_settings.m_reblurInternalBlurRadius.specularPrepassBlurRadius > 0.0f ?
        std::max(1.0f, round(m_settings.m_reblurInternalBlurRadius.specularPrepassBlurRadius * radiusResolutionScale)) : 0.0f;
    }
    else if (m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR) {
      m_settings.m_relaxSettings.diffusePrepassBlurRadius = m_settings.m_relaxInternalBlurRadius.diffusePrepassBlurRadius > 0.0f ?
        std::max(1.0f, round(m_settings.m_relaxInternalBlurRadius.diffusePrepassBlurRadius * radiusResolutionScale)) : 0.0f;
      m_settings.m_relaxSettings.specularPrepassBlurRadius = m_settings.m_relaxInternalBlurRadius.specularPrepassBlurRadius > 0.0f ?
        std::max(1.0f, round(m_settings.m_relaxInternalBlurRadius.specularPrepassBlurRadius * radiusResolutionScale)) : 0.0f;
    }
  }

  void NRDContext::destroyResources() {
    m_transientTex.clear();
    m_permanentTex.clear();
    m_sharedTransientTex.clear();
    m_validationTex.reset();
  }

  void NRDContext::destroyPipelines() {
    for (auto& pipeline : m_computePipelines) {
      m_vkd->vkDestroyPipeline(m_vkd->device(), pipeline.pipeline, nullptr);
      m_vkd->vkDestroyPipelineLayout(m_vkd->device(), pipeline.pipelineLayout, nullptr);
      m_vkd->vkDestroyDescriptorSetLayout(m_vkd->device(), pipeline.descriptorSetLayout, nullptr);
    }

    m_computePipelines.clear();
    m_staticSamplers.clear();
  }

  void NRDContext::showImguiSettings() {
    m_settings.showImguiSettings();
  }

  NrdArgs NRDContext::getNrdArgs() const {
    static_assert(nrd::CommonSettings{}.denoisingRange == 500000.0f, "NRD's default settings has changed, denoisingRange must be re-evaluated");
    constexpr float denoisingRangeLimit = nrd::CommonSettings{}.denoisingRange;

    const float missLinearViewZ = denoisingRangeLimit + 1.0f;
    static_assert(missLinearViewZ > denoisingRangeLimit && missLinearViewZ - denoisingRangeLimit > 0.01f);

    // Note: Ensure the denoising range is at least 1 ulp less than the miss linear view Z value, otherwise it will not
    // function properly.
    assert(m_settings.m_commonSettings.denoisingRange <= denoisingRangeLimit);

    NrdArgs args;

    args.isReblurEnabled = m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
    args.missLinearViewZ = missLinearViewZ;
    args.maxDirectHitTContribution = m_settings.m_groupedSettings.maxDirectHitTContribution;

    auto getHitDistanceParameters = [](const nrd::HitDistanceParameters& params) {
      return Vector4(params.A, params.B, params.C, params.D);
    };

    args.hitDistanceParams = getHitDistanceParameters(m_settings.m_reblurSettings.hitDistanceParameters);

    return args;
  }

  bool NRDContext::isReferenceDenoiserEnabled() const {
    return m_settings.m_denoiserDesc.denoiser == nrd::Denoiser::REFERENCE;
  }

  const NrdSettings& NRDContext::getNrdSettings() const {
    return m_settings;
  }

  void NRDContext::setNrdSettings(const NrdSettings& refSettings) {
    m_settings = refSettings;
  }

  void NRDContext::release() {
    destroyResources();
    destroyPipelines();
    m_cbData = nullptr;
    
    if (m_denoiserInstance) {
      nrd::dispatch.DestroyInstance(*m_denoiserInstance);
      m_denoiserInstance = nullptr;
    }
  }
} // namespace dxvk