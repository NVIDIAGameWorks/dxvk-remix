/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_ngx_wrapper.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_vk.h>

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <nvsdk_ngx_helpers_vk.h>

#include "rtx_resources.h"

#include <dxvk_device.h>

#include <cstdio>
#include <cstdarg>

namespace dxvk
{
  NGXWrapper* NGXWrapper::s_instance = nullptr;

  namespace
  {
    std::string resultToString(NVSDK_NGX_Result result)
    {
      char buf[1024];
      snprintf(buf, sizeof(buf), "(code: 0x%08x, info: %ls)", result, GetNGXResultAsString(result));
      buf[sizeof(buf) - 1] = '\0';
      return std::string(buf);
    }
  }

  NGXWrapper::NGXWrapper(DxvkDevice* device, const wchar_t* logFolder)
    : m_device(device)
  {
    initializeNGX(logFolder);
  }

  NGXWrapper::~NGXWrapper()
  {
    if (m_initialized) {
      releaseDLSS();
    }
    shutdownNGX();
  }

  void NGXWrapper::initializeNGX(const wchar_t* logFolder)
  {
    ScopedCpuProfileZone();
    NVSDK_NGX_Result result = NVSDK_NGX_Result_Fail;
    m_initialized = false;
    m_supportsDLSS = false;
    
    VkDevice vkDevice = m_device->handle();
    auto adapter = m_device->adapter();
    VkPhysicalDevice vkPhysicalDevice = adapter->handle();
    auto instance = m_device->instance();
    VkInstance vkInstance = instance->handle();
    result = NVSDK_NGX_VULKAN_Init(RtxOptions::Get()->applicationId(), logFolder, vkInstance, vkPhysicalDevice, vkDevice);

    if (NVSDK_NGX_FAILED(result)) {
      if (result == NVSDK_NGX_Result_FAIL_FeatureNotSupported || result == NVSDK_NGX_Result_FAIL_PlatformError) {
        Logger::err(str::format("NVIDIA NGX is not available on this hardware/platform: ", resultToString(result)));
      } else {
        Logger::err(str::format("Failed to initialize NGX: ", resultToString(result)));
      }
      return;
    }

    result = NVSDK_NGX_VULKAN_AllocateParameters(&m_parameters);

    if (NVSDK_NGX_FAILED(result)) {
      Logger::err(str::format("NVSDK_NGX_VULKAN_AllocateParameters failed: ", resultToString(result)));
      return;
    }

    result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&m_parameters);

    if (NVSDK_NGX_FAILED(result)) {
      Logger::err(str::format("NVSDK_NGX_VULKAN_GetCapabilityParameters failed: ", resultToString(result)));
      return;
    }
    
    // Currently, the SDK and this sample are not in sync.  The sample is a bit forward looking,
    // in this case.  This will likely be resolved very shortly, and therefore, the code below
    // should be thought of as needed for a smooth user experience.
#if defined(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver)        \
    && defined (NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor) \
    && defined (NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor)

    // If NGX Successfully initialized then it should set those flags in return
    int needsUpdatedDriver = 0;
    if (!NVSDK_NGX_FAILED(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver)) && needsUpdatedDriver) {
      std::string message = "NVIDIA DLSS cannot be loaded due to outdated driver.";
      unsigned int majorVersion = 0;
      unsigned int minorVersion = 0;
      if (!NVSDK_NGX_FAILED(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &majorVersion)) &&
        !NVSDK_NGX_FAILED(m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minorVersion))) {
        message += "Minimum driver version required: " + std::to_string(majorVersion) + "." + std::to_string(minorVersion);
      }
      Logger::err(message);
      return;
    }
#endif

    int dlssAvailable = 0;
    result = m_parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
    if (NVSDK_NGX_FAILED(result) || !dlssAvailable) {
      Logger::err(str::format("NVIDIA DLSS not available on this hardward/platform: ", resultToString(result)));
      return;
    }

    m_supportsDLSS = true;

    m_initialized = true;
  }

  void NGXWrapper::shutdownNGX()
  {
    if (m_initialized) {
      releaseDLSS();
      NVSDK_NGX_VULKAN_Shutdown1(m_device->handle());

      m_initialized = false;
    }
  }

  void NGXWrapper::initializeDLSS(
    Rc<DxvkContext> renderContext,
    Rc<DxvkCommandList> cmdList,
    uint32_t maxRenderSize[2],
    uint32_t displayOutSize[2],
    bool isContentHDR,
    bool depthInverted,
    bool autoExposure,
    bool sharpening,
    NVSDK_NGX_PerfQuality_Value perfQuality)
  {
    ScopedCpuProfileZone();
    if (!m_supportsDLSS || !m_initialized)
      return;

    const unsigned int CreationNodeMask = 1;
    const unsigned int VisibilityNodeMask = 1;

    const bool lowResolutionMotionVectors = true; // we let the Snippet do the upsampling of the motion vector
    const bool jitteredMV = false;
    // Next create features
    int createFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
    createFlags |= lowResolutionMotionVectors ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0;
    createFlags |= isContentHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0;
    createFlags |= depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0;
    createFlags |= jitteredMV ? NVSDK_NGX_DLSS_Feature_Flags_MVJittered : 0;
    createFlags |= autoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0;
    createFlags |= sharpening ? NVSDK_NGX_DLSS_Feature_Flags_DoSharpening : 0;

    NVSDK_NGX_DLSS_Create_Params createParams = {};

    createParams.Feature.InWidth = maxRenderSize[0];
    createParams.Feature.InHeight = maxRenderSize[1];
    createParams.Feature.InTargetWidth = displayOutSize[0];
    createParams.Feature.InTargetHeight = displayOutSize[1];
    createParams.Feature.InPerfQualityValue = perfQuality;
    createParams.InFeatureCreateFlags = createFlags;

  
    VkCommandBuffer vkCommandBuffer = cmdList->getCmdBuffer(dxvk::DxvkCmdBuffer::ExecBuffer);

    NVSDK_NGX_Result result = NGX_VULKAN_CREATE_DLSS_EXT(vkCommandBuffer, CreationNodeMask, VisibilityNodeMask, &m_featureDLSS, m_parameters, &createParams);

    if (NVSDK_NGX_FAILED(result)) {
      Logger::err(str::format("Failed to create DLSS feature: ", resultToString(result)));
      return;
    }
  }

  void NGXWrapper::releaseDLSS()
  {
    if (m_featureDLSS) {
      NVSDK_NGX_VULKAN_ReleaseFeature(m_featureDLSS);
      m_featureDLSS = nullptr;
    }
  }

  void NGXWrapper::releaseInstance() {
    if (s_instance != nullptr) {
      delete s_instance;
      s_instance = nullptr;
    }
  }

  NGXWrapper* NGXWrapper::getInstance(DxvkDevice* device) {
    // Only one NGX instance is allowed, release resource if a different device appears
    if (s_instance && s_instance->m_device != device) {
      releaseInstance();
    }

    if (!s_instance) {
      s_instance = new NGXWrapper(device, dxvk::str::tows(dxvk::env::getExePath().c_str()).c_str());
    }
    return s_instance;
  }

  NGXWrapper::OptimalSettings NGXWrapper::queryOptimalSettings(const uint32_t displaySize[2], NVSDK_NGX_PerfQuality_Value perfQuality) const
  {
    ScopedCpuProfileZone();
    OptimalSettings settings;

    NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(m_parameters,
      displaySize[0], displaySize[1], perfQuality,
      &settings.optimalRenderSize[0], &settings.optimalRenderSize[1],
      &settings.maxRenderSize[0], &settings.maxRenderSize[1],
      &settings.minRenderSize[0], &settings.minRenderSize[1],
      &settings.sharpness);

    if (NVSDK_NGX_FAILED(result)) {
      Logger::err(str::format("Querying optimal settings failed: ", resultToString(result)));
      return settings;
    }

    // Depending on what version of DLSS DLL is being used, a sharpness of > 1.f was possible.
    settings.sharpness = clamp(settings.sharpness, 0.01f, 1.f);

    return settings;
  }

  NVSDK_NGX_Resource_VK TextureToResourceVK(const Resources::Resource* tex, bool isUAV/*, nvrhi::TextureSubresourceSet subresources*/)
  {
    if (tex->view == nullptr || tex->image == nullptr)
      return {};

    VkImageView imageView = tex->view->handle();
    auto info = tex->image->info();
    VkFormat format = info.format;
    VkImage image = tex->image->handle();
    VkImageSubresourceRange subresourceRange = tex->view->subresources();
    return NVSDK_NGX_Create_ImageView_Resource_VK(imageView, image, subresourceRange, format, info.extent.width, info.extent.height, isUAV);
  }

  bool NGXWrapper::evaluateDLSS(
    Rc<DxvkCommandList> commandList,
    Rc<DxvkContext> renderContext,
    const Resources::Resource* pUnresolvedColor,
    const Resources::Resource* pResolvedColor,
    const Resources::Resource* pMotionVectors,
    const Resources::Resource* pDepth,
    const Resources::Resource* pDiffuseAlbedo,
    const Resources::Resource* pSpecularAlbedo,
    const Resources::Resource* pExposure,
    const Resources::Resource* pPosition,
    const Resources::Resource* pNormals,
    const Resources::Resource* pRoughness,
    const Resources::Resource* pBiasCurrentColorMask,
    bool resetAccumulation,
    bool antiGhost,
    float sharpness,
    float preExposure,
    float jitterOffset[2],
    float motionVectorScale[2],
    bool autoExposure) const
  {
    ScopedCpuProfileZone();
    if (!m_featureDLSS)
      return false;

    // In DLSS v2, the target is already upsampled (while in v1, the upsampling is handled in a later pass)
    uint32_t inWidth = pUnresolvedColor->image->info().extent.width;
    uint32_t inHeight = pUnresolvedColor->image->info().extent.height;
    uint32_t outWidth = pResolvedColor->image->info().extent.width;
    uint32_t outHeight = pResolvedColor->image->info().extent.height;
    assert(outWidth >= inWidth && outHeight >= inHeight);

    bool success = true;

    VkCommandBuffer vkCommandbuffer = commandList->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);

    NVSDK_NGX_Resource_VK unresolvedColorResource = TextureToResourceVK(pUnresolvedColor, false);
    NVSDK_NGX_Resource_VK resolvedColorResource = TextureToResourceVK(pResolvedColor, true);
    NVSDK_NGX_Resource_VK motionVectorsResource = TextureToResourceVK(pMotionVectors, false);
    NVSDK_NGX_Resource_VK depthResource = TextureToResourceVK(pDepth, false);
    NVSDK_NGX_Resource_VK exposureResource = TextureToResourceVK(pExposure, false);
    NVSDK_NGX_Resource_VK biasCurrentColorMaskResource = TextureToResourceVK(pBiasCurrentColorMask, false);

    NVSDK_NGX_VK_DLSS_Eval_Params evalParams = {};
    evalParams.Feature.pInColor = &unresolvedColorResource;
    evalParams.Feature.pInOutput = &resolvedColorResource;
    evalParams.pInDepth = &depthResource;
    // xxxnsubtil: the DLSS indicator reads the exposure texture even when DLSS autoexposure is on
    evalParams.pInExposureTexture = &exposureResource;
    evalParams.pInMotionVectors = &motionVectorsResource;
    evalParams.pInBiasCurrentColorMask = antiGhost ? &biasCurrentColorMaskResource : nullptr;
    evalParams.InJitterOffsetX = jitterOffset[0];
    evalParams.InJitterOffsetY = jitterOffset[1];
    evalParams.Feature.InSharpness = sharpness;
    evalParams.InPreExposure = preExposure;
    evalParams.InReset = resetAccumulation ? 1 : 0;
    evalParams.InMVScaleX = motionVectorScale[0];
    evalParams.InMVScaleY = motionVectorScale[1];
    evalParams.InRenderSubrectDimensions = { inWidth, inHeight };

    NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(vkCommandbuffer, m_featureDLSS, m_parameters, &evalParams);
    if (NVSDK_NGX_FAILED(result)) {
      success = false;
    }

    return success;
  }

} // namespace dxvk
