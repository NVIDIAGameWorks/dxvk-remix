/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include <locale>
#include <codecvt>
#include <cassert>

#include "rtx.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "dxvk_device.h"
#include "rtx_xess.h"
#include "rtx_camera.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "rtx_auto_exposure.h"
#include "../../util/util_math.h"
#include "../util/util_string.h"
#include "../util/log/log.h"

#include "xess/inc/xess/xess.h"
#include "xess/inc/xess/xess_vk.h"
#include "xess/inc/xess/xess_debug.h"

namespace dxvk {
  const char* xessPresetToString(XeSSPreset xessPreset) {
    switch (xessPreset) {
    case XeSSPreset::UltraPerf: return "Ultra Performance";
    case XeSSPreset::Performance: return "Performance";
    case XeSSPreset::Balanced: return "Balanced";
    case XeSSPreset::Quality: return "Quality";
    case XeSSPreset::UltraQuality: return "Ultra Quality";
    case XeSSPreset::UltraQualityPlus: return "Ultra Quality Plus";
    case XeSSPreset::NativeAA: return "Native Anti-Aliasing";
    case XeSSPreset::Custom: return "Custom";
    default:
      assert(false);
    case XeSSPreset::Invalid: return "Invalid";
    }
  }

  // Helper function to convert XeSS result to string
  static const char* xessResultToString(xess_result_t result) {
    switch (result) {
      case XESS_RESULT_SUCCESS: return "Success";
      case XESS_RESULT_WARNING_NONEXISTING_FOLDER: return "Warning: Nonexisting folder";
      case XESS_RESULT_WARNING_OLD_DRIVER: return "Warning: Old driver";
      case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE: return "Error: Unsupported device";
      case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER: return "Error: Unsupported driver";
      case XESS_RESULT_ERROR_UNINITIALIZED: return "Error: Uninitialized";
      case XESS_RESULT_ERROR_INVALID_ARGUMENT: return "Error: Invalid argument";
      case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY: return "Error: Device out of memory";
      case XESS_RESULT_ERROR_DEVICE: return "Error: Device error";
      case XESS_RESULT_ERROR_NOT_IMPLEMENTED: return "Error: Not implemented";
      case XESS_RESULT_ERROR_INVALID_CONTEXT: return "Error: Invalid context";
      case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "Error: Operation in progress";
      case XESS_RESULT_ERROR_UNSUPPORTED: return "Error: Unsupported";
      case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY: return "Error: Can't load library";
      case XESS_RESULT_ERROR_WRONG_CALL_ORDER: return "Error: Wrong call order";
      case XESS_RESULT_ERROR_UNKNOWN: return "Error: Unknown";
      default: return "Unknown result code";
    }
  }



  // Constructor with device
  DxvkXeSS::DxvkXeSS(DxvkDevice* device) 
    : RtxPass(device),
      CommonDeviceObject(device),
      m_initialized(false),
      m_xessContext(nullptr),
      m_targetExtent{0, 0, 0},
      m_currentPreset(XeSSPreset::Balanced),
      m_preset(XeSSPreset::Balanced),
      m_actualPreset(XeSSPreset::Balanced),
      m_inputSize{0, 0},
      m_xessOutputSize{0, 0},
      m_recreate(false),
      m_lastResolutionScale(-1.0f) {
  }

  DxvkXeSS::~DxvkXeSS() {
    destroyXeSSContext();
  }

  // RtxPass interface implementations
  bool DxvkXeSS::isEnabled() const {
    return RtxOptions::isXeSSEnabled();
  }

  bool DxvkXeSS::onActivation(Rc<DxvkContext>& ctx) {
    // Check if XeSS is supported on this system (use stored device pointer)
    if (!validateXeSSSupport(device())) {
      Logger::warn("XeSS: System does not support XeSS - activation failed");
      return false;
    }
    
    m_recreate = true; // Force recreation of context
    Logger::info("XeSS: Activated successfully");
    return true;
  }

  void DxvkXeSS::onDeactivation() {
    if (m_xessContext) {
      destroyXeSSContext();
    }
    m_initialized = false;
  }

  bool DxvkXeSS::isXeSSLibraryAvailable() {
    // Try to get XeSS version to test if library is available
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    
    if (result == XESS_RESULT_SUCCESS) {
      Logger::debug(str::format("XeSS: SDK version: ", version.major, ".", version.minor, ".", version.patch));
      return true;
    } else {
      Logger::warn(str::format("XeSS: library not available: ", xessResultToString(result)));
      return false;
    }
  }

  bool DxvkXeSS::validateXeSSSupport(DxvkDevice* device) {
    if (!isXeSSLibraryAvailable()) {
      return false;
    }

    // Get XeSS version
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("XeSS: Failed to get XeSS version: ", xessResultToString(result)));
      return false;
    }

    // GPU compatibility check
    auto adapter = device->adapter();
    auto deviceProps = adapter->deviceProperties();
    
    if (deviceProps.vendorID == 0x8086) { // Intel
      Logger::debug("XeSS: Intel GPU detected - using optimized XeSS path");
    } else {
      Logger::debug("XeSS: Non-Intel GPU detected - using generic XeSS path");
    }

    // Test context creation
    xess_context_handle_t testContext = nullptr;
    result = xessVKCreateContext(
      device->instance()->handle(),
      device->adapter()->handle(),
      device->handle(),
      &testContext
    );
    
    if (result == XESS_RESULT_SUCCESS) {
      // Clean up test context
      xessDestroyContext(testContext);
      return true;
    } else {
      Logger::warn(str::format("XeSS: context creation test failed: ", xessResultToString(result)));
      return false;
    }
  }

  xess_quality_settings_t DxvkXeSS::presetToQuality(XeSSPreset preset) const {
    switch (preset) {
      case XeSSPreset::UltraPerf: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
      case XeSSPreset::Performance: return XESS_QUALITY_SETTING_PERFORMANCE;
      case XeSSPreset::Balanced: return XESS_QUALITY_SETTING_BALANCED;
      case XeSSPreset::Quality: return XESS_QUALITY_SETTING_QUALITY;
      case XeSSPreset::UltraQuality: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
      case XeSSPreset::UltraQualityPlus: return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
      case XeSSPreset::NativeAA: return XESS_QUALITY_SETTING_AA;
      case XeSSPreset::Custom: return XESS_QUALITY_SETTING_BALANCED; // Use balanced as base for custom
      default: return XESS_QUALITY_SETTING_BALANCED;
    }
  }

  VkExtent3D DxvkXeSS::getInputSize(const VkExtent3D& targetExtent) const {
    if (!isActive() || !m_xessContext) {
      return targetExtent;
    }

    XeSSPreset currentPreset = XessOptions::preset();
    
    if (currentPreset == XeSSPreset::Custom) {
      // For Custom preset, use resolution scale directly
      const float downscaleFactor = RtxOptions::resolutionScale();
      VkExtent3D inputExtent;
      inputExtent.width = std::max(1u, static_cast<uint32_t>(targetExtent.width * downscaleFactor));
      inputExtent.height = std::max(1u, static_cast<uint32_t>(targetExtent.height * downscaleFactor));
      inputExtent.depth = targetExtent.depth;
      return inputExtent;
    } else {
      // Use XeSS SDK to get optimal input resolution for XeSS 2.1+ compliance
      xess_2d_t targetRes = { static_cast<uint32_t>(targetExtent.width), static_cast<uint32_t>(targetExtent.height) };
      xess_2d_t optimalInputRes = { 0, 0 };
      xess_2d_t minInputRes = { 0, 0 };
      xess_2d_t maxInputRes = { 0, 0 };
      
      xess_quality_settings_t quality = presetToQuality(currentPreset);
      xess_result_t result = xessGetOptimalInputResolution(
        m_xessContext, 
        &targetRes, 
        quality, 
        &optimalInputRes, 
        &minInputRes, 
        &maxInputRes
      );
      
      if (result == XESS_RESULT_SUCCESS) {
        VkExtent3D inputExtent;
        inputExtent.width = optimalInputRes.x;
        inputExtent.height = optimalInputRes.y;
        inputExtent.depth = targetExtent.depth;
        
        return inputExtent;
      } else {
        Logger::warn(str::format("XeSS 2.1: Failed to get optimal input resolution, using fallback: ", xessResultToString(result)));
        // Fallback to hardcoded values
        const float downscaleFactor = 1.f / calcUpscaleFactor();
        
        VkExtent3D inputExtent;
        inputExtent.width = std::max(1u, static_cast<uint32_t>(targetExtent.width * downscaleFactor));
        inputExtent.height = std::max(1u, static_cast<uint32_t>(targetExtent.height * downscaleFactor));
        inputExtent.depth = targetExtent.depth;
        return inputExtent;
      }
    }
  }

  void DxvkXeSS::initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent) {
    if (!isActive()) {
      return;
    }

    // Check if we need to recreate the context
    XeSSPreset currentPreset = XessOptions::preset();
    if (m_initialized && 
        m_targetExtent.width == targetExtent.width &&
        m_targetExtent.height == targetExtent.height &&
        m_currentPreset == currentPreset) {
      return; // Already initialized with correct settings
    }

    // Release existing context if any
    if (m_xessContext) {
      destroyXeSSContext();
    }

    m_targetExtent = targetExtent;
    m_currentPreset = currentPreset;

    createXeSSContext(targetExtent);
    m_initialized = true;
  }

  void DxvkXeSS::createXeSSContext(const VkExtent3D& targetExtent) {
    if (!device()) {
      Logger::err("XeSS: Cannot create context - no device available");
      return;
    }
    
    // Create XeSS context using real SDK
    xess_result_t     result = xessVKCreateContext(
      device()->instance()->handle(),
      device()->adapter()->handle(),
      device()->handle(),
      &m_xessContext
    );
    
    if (result != XESS_RESULT_SUCCESS) {
      Logger::err(str::format("XeSS: Failed to create context: ", xessResultToString(result)));
      m_xessContext = nullptr;
      return;
    }
    
    xess_quality_settings_t quality = presetToQuality(m_currentPreset);
    xess_init_flags_t preFlags = XESS_INIT_FLAG_NONE;
    
    // Trigger pre-build in background to reduce later initialization stalls
    result = xessVKBuildPipelines(m_xessContext, VK_NULL_HANDLE, false, preFlags);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::debug(str::format("XeSS: Pipeline pre-build failed, will compile during init: ", xessResultToString(result)));
    }
    
    // XeSS 2.1: Verify driver compatibility and warn if suboptimal
    xess_result_t driverResult = xessIsOptimalDriver(m_xessContext);
    if (driverResult == XESS_RESULT_WARNING_OLD_DRIVER) {
      Logger::warn("XeSS: Using older driver - update recommended for optimal performance and quality");
    } else if (driverResult != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("XeSS: Driver verification returned: ", xessResultToString(driverResult)));
    }
    
    // Always use KPSS network model (best quality)
    result = xessSelectNetworkModel(m_xessContext, XESS_NETWORK_MODEL_KPSS);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("XeSS: Failed to select KPSS network model: ", xessResultToString(result)));
    }
    
    // XeSS 2.1: Set responsive pixel mask clamp value if different from default
    float responsiveMaskClamp = DxvkXeSS::XessOptions::responsivePixelMaskClampValue();
    if (responsiveMaskClamp != 0.8f) { // Only set if different from XeSS default
      result = xessSetMaxResponsiveMaskValue(m_xessContext, responsiveMaskClamp);
      if (result != XESS_RESULT_SUCCESS) {
        Logger::warn(str::format("XeSS: Failed to set responsive pixel mask clamp value: ", xessResultToString(result)));
      }
    }
    
    // Initialize XeSS with current settings
    xess_vk_init_params_t initParams = {};
    initParams.outputResolution.x = targetExtent.width;
    initParams.outputResolution.y = targetExtent.height;
    initParams.qualitySetting = presetToQuality(m_currentPreset);

    // Set initialization flags based on renderer state and user options
    initParams.initFlags = XESS_INIT_FLAG_NONE;
    
    initParams.creationNodeMask = 1;
    initParams.visibleNodeMask = 1;
    initParams.tempBufferHeap = VK_NULL_HANDLE;
    initParams.bufferHeapOffset = 0;
    initParams.tempTextureHeap = VK_NULL_HANDLE;
    initParams.textureHeapOffset = 0;
    initParams.pipelineCache = VK_NULL_HANDLE;
    
    result = xessVKInit(m_xessContext, &initParams);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::err(str::format("XeSS: Failed to initialize context: ", xessResultToString(result)));
      xessDestroyContext(m_xessContext);
      m_xessContext = nullptr;
      return;
    }
  }

  uint32_t DxvkXeSS::calcRecommendedJitterSequenceLength() const {
    if (!DxvkXeSS::XessOptions::useRecommendedJitterSequenceLength()) {
      return RtxOptions::cameraJitterSequenceLength(); // Use global setting
    }
    
    // XeSS 2.1 formula: ceil(upscale_factor^2 * 8)
    // For extreme scaling (e.g. 0.10x = 10x upscaling), this ensures sufficient temporal samples
    const float upscaleFactor = calcUpscaleFactor();
    uint32_t recommendedLength = static_cast<uint32_t>(std::ceil(upscaleFactor * upscaleFactor * 8.0f));
    
    // Expanded range: minimum of 8, maximum of 1024 for extreme scaling scenarios
    recommendedLength = std::clamp(recommendedLength, 8u, 1024u);
    
    return recommendedLength;
  }

  float DxvkXeSS::getUpscaleFactor(xess_quality_settings_t quality) const {
    switch (quality) {
    case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: return 3.0f;
    case XESS_QUALITY_SETTING_PERFORMANCE:       return 2.3f;
    case XESS_QUALITY_SETTING_BALANCED:          return 2.0f;
    case XESS_QUALITY_SETTING_QUALITY:           return 1.7f;
    case XESS_QUALITY_SETTING_ULTRA_QUALITY:     return 1.5f;
    case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: return 1.3f;
    case XESS_QUALITY_SETTING_AA:                 return 1.0f;
    default:                                      return 2.0f;
    }
  }

  float DxvkXeSS::calcUpscaleFactor() const {
    if (XessOptions::preset() == XeSSPreset::Custom) {
      return 1.0f / RtxOptions::resolutionScale();
    } else {
      xess_quality_settings_t quality = presetToQuality(XessOptions::preset());
      return getUpscaleFactor(quality);
    }
  }

  float DxvkXeSS::calcRecommendedMipBias() const {
    // XeSS 2.1 formula: -log2(upscale_factor)
    float upscaleFactor = calcUpscaleFactor();
    float mipBias = -std::log2(upscaleFactor);

    return mipBias;
  }

  void DxvkXeSS::destroyXeSSContext() {
    if (m_xessContext) {
      xess_result_t result = xessDestroyContext(m_xessContext);
      if (result != XESS_RESULT_SUCCESS) {
        Logger::warn(str::format("XeSS: Warning during context destruction: ", xessResultToString(result)));
      }
      m_xessContext = nullptr;
    }
  }

  void DxvkXeSS::dispatch(
    Rc<DxvkContext> renderContext,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    bool resetHistory) {
    
    if (!isActive()) {
      // Fallback: just copy input to output
      renderContext->copyImage(
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutputExtent);
      return;
    }

    // Initialize XeSS if needed (similar to DLSS pattern)
    if (m_recreate || !m_initialized) {
      // Use the target extent that was already calculated in setSetting()
      // If setSetting() hasn't been called yet (e.g., Auto preset on first load), 
      // fall back to the actual output texture resolution
      VkExtent3D targetExtent;
      if (m_xessOutputSize.width > 0 && m_xessOutputSize.height > 0) {
        targetExtent = { 
          m_xessOutputSize.width,
          m_xessOutputSize.height,
          1
        };
      } else {
        // Fallback to actual output texture resolution
        targetExtent = { 
          rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image->info().extent.width,
          rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image->info().extent.height,
          1
        };
      }
      initialize(renderContext, targetExtent);
      m_recreate = false;
    }

    if (!m_initialized || !m_xessContext) {
      // Fallback: just copy input to output
      renderContext->copyImage(
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutputExtent);
      return;
    }
    
    // Set up image barriers for XeSS inputs and outputs
    std::array<Rc<DxvkImageView>, 4> inputs = {
      rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
      rtOutput.m_primaryScreenSpaceMotionVector.view,
      rtOutput.m_primaryDepth.view,
      nullptr // Placeholder for auto-exposure texture
    };

    auto& autoExposure = device()->getCommon()->metaAutoExposure();
    if (autoExposure.enabled() && autoExposure.getExposureTexture().image != nullptr) {
      inputs[3] = autoExposure.getExposureTexture().view;
    }

    std::array<Rc<DxvkImageView>, 1> outputs = {
      rtOutput.m_finalOutput.view(Resources::AccessType::Write)
    };

    // Set up barriers for input textures
    for (auto input : inputs) {
      if (input == nullptr) {
        continue;
      }
      
      barriers.accessImage(
        input->image(),
        input->imageSubresources(),
        input->imageInfo().layout,
        input->imageInfo().stages,
        input->imageInfo().access,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT);
    }

    // Set up barriers for output texture
    for (auto output : outputs) {
      barriers.accessImage(
        output->image(),
        output->imageSubresources(),
        output->imageInfo().layout,
        output->imageInfo().stages,
        output->imageInfo().access,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT);
    }

    barriers.recordCommands(renderContext->getCommandList());

    // Get jitter offset from camera
    auto& sceneManager = device()->getCommon()->getSceneManager();
    RtCamera& camera = sceneManager.getCamera();
    float jitterOffset[2];
    camera.getJittering(jitterOffset);

    // XeSS expects jitter in the range [-0.5, 0.5] in pixel units
    // The camera jitter is already in pixel units, so we can use it directly
    // However, we need to ensure it's properly scaled for XeSS
    float xessJitterX = jitterOffset[0];
    float xessJitterY = jitterOffset[1];
    
    if (DxvkXeSS::XessOptions::useOptimizedJitter()) {
      // Get XeSS jitter scale to understand expected range
      float jitterScaleX, jitterScaleY;
      xess_result_t scaleResult = xessGetJitterScale(m_xessContext, &jitterScaleX, &jitterScaleY);
      if (scaleResult == XESS_RESULT_SUCCESS) {
        // Apply XeSS jitter scale if available
        xessJitterX *= jitterScaleX;
        xessJitterY *= jitterScaleY;
        
        // Applied jitter scale - debug logging removed to avoid spam
      }
    }
    
    // Apply user jitter scale multiplier
    float userJitterScale = DxvkXeSS::XessOptions::jitterScale();
    
    // Apply adaptive jitter scaling for extreme upscaling scenarios
    if (DxvkXeSS::XessOptions::useOptimizedJitter()) {
      const float upscaleFactor = calcUpscaleFactor();
      
      // Adaptive jitter scaling to reduce swimming artifacts at extreme scaling
      if (upscaleFactor > 6.0f) {
        // Extreme scaling (e.g., 0.10x resolution = 10x upscaling): configurable jitter reduction
        float extremeDamping = DxvkXeSS::XessOptions::scalingJitterDamping();
        userJitterScale *= extremeDamping;
        // Extreme scaling detected - debug logging removed to avoid spam
      } else if (upscaleFactor > 4.0f) {
        // Very high scaling: moderate jitter reduction
        userJitterScale *= 0.75f;
      } else if (upscaleFactor > 2.5f) {
        // High scaling: light jitter reduction
        userJitterScale *= 0.85f;
      }
    }
    
    xessJitterX *= userJitterScale;
    xessJitterY *= userJitterScale;
    
    // Clamp jitter to XeSS expected range [-0.5, 0.5]
    xessJitterX = std::clamp(xessJitterX, -0.5f, 0.5f);
    xessJitterY = std::clamp(xessJitterY, -0.5f, 0.5f);

    // Set up XeSS execution parameters
    xess_vk_execute_params_t execParams = {};
    
    // Input color texture
    auto colorView = rtOutput.m_compositeOutput.view(Resources::AccessType::Read);
    execParams.colorTexture.imageView = colorView->handle();
    execParams.colorTexture.image = colorView->image()->handle();
    execParams.colorTexture.subresourceRange = colorView->subresources();
    execParams.colorTexture.format = colorView->info().format;
    execParams.colorTexture.width = colorView->imageInfo().extent.width;
    execParams.colorTexture.height = colorView->imageInfo().extent.height;

    // Optional Exposure texture
    if (autoExposure.enabled() && autoExposure.getExposureTexture().image != nullptr) {
      auto exposureView = autoExposure.getExposureTexture().view;
      execParams.exposureScaleTexture.imageView = exposureView->handle();
      execParams.exposureScaleTexture.image = exposureView->image()->handle();
      execParams.exposureScaleTexture.subresourceRange = exposureView->subresources();
      execParams.exposureScaleTexture.format = exposureView->info().format;
      execParams.exposureScaleTexture.width = exposureView->imageInfo().extent.width;
      execParams.exposureScaleTexture.height = exposureView->imageInfo().extent.height;
    } else {
      // Zero out the struct if not used
      memset(&execParams.exposureScaleTexture, 0, sizeof(execParams.exposureScaleTexture));
    }

    // Motion vector texture
    auto motionView = rtOutput.m_primaryScreenSpaceMotionVector.view;
    execParams.velocityTexture.imageView = motionView->handle();
    execParams.velocityTexture.image = motionView->image()->handle();
    execParams.velocityTexture.subresourceRange = motionView->subresources();
    execParams.velocityTexture.format = motionView->info().format;
    execParams.velocityTexture.width = motionView->imageInfo().extent.width;
    execParams.velocityTexture.height = motionView->imageInfo().extent.height;

    // Depth texture
    auto depthView = rtOutput.m_primaryDepth.view;
    execParams.depthTexture.imageView = depthView->handle();
    execParams.depthTexture.image = depthView->image()->handle();
    execParams.depthTexture.subresourceRange = depthView->subresources();
    execParams.depthTexture.format = depthView->info().format;
    execParams.depthTexture.width = depthView->imageInfo().extent.width;
    execParams.depthTexture.height = depthView->imageInfo().extent.height;

    // Output texture
    auto outputView = rtOutput.m_finalOutput.view(Resources::AccessType::Write);
    execParams.outputTexture.imageView = outputView->handle();
    execParams.outputTexture.image = outputView->image()->handle();
    execParams.outputTexture.subresourceRange = outputView->subresources();
    execParams.outputTexture.format = outputView->info().format;
    execParams.outputTexture.width = outputView->imageInfo().extent.width;
    execParams.outputTexture.height = outputView->imageInfo().extent.height;

    // Always provide jitter separately as calculated above
    execParams.jitterOffsetX = xessJitterX;
    execParams.jitterOffsetY = xessJitterY;

    execParams.exposureScale = 1.0f; // Default exposure scale
    execParams.resetHistory = resetHistory ? 1 : 0;
    // Use the input size from setSetting for all profiles
    execParams.inputWidth = m_inputSize.width;
    execParams.inputHeight = m_inputSize.height;
    
    // Base coordinates (default to 0,0)
    execParams.inputColorBase = { 0, 0 };
    execParams.inputMotionVectorBase = { 0, 0 };
    execParams.inputDepthBase = { 0, 0 };
    execParams.inputResponsiveMaskBase = { 0, 0 };
    execParams.outputColorBase = { 0, 0 };

    // Execute XeSS
    VkCommandBuffer cmdBuffer = renderContext->getCommandList()->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    xess_result_t result = xessVKExecute(m_xessContext, cmdBuffer, &execParams);
    
    if (result != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("XeSS: Execute failed: ", xessResultToString(result)));
      
      // Fallback to simple copy on failure
      renderContext->copyImage(
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutputExtent);
    } else {
      // XeSS execution successful - removed debug logging to avoid spam
    }

    // Restore barriers for output texture
    for (auto output : outputs) {
      barriers.accessImage(
        output->image(),
        output->imageSubresources(),
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        output->imageInfo().layout,
        output->imageInfo().stages,
        output->imageInfo().access);

      renderContext->getCommandList()->trackResource<DxvkAccess::None>(output);
      renderContext->getCommandList()->trackResource<DxvkAccess::Write>(output->image());
    }
    
    barriers.recordCommands(renderContext->getCommandList());
  }

  void DxvkXeSS::setSetting(const uint32_t displaySize[2], const XeSSPreset preset, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    
    // Use the preset directly
    XeSSPreset actualPreset = preset;

    // For Custom preset, also check if resolution scale has changed
    bool resolutionScaleChanged = false;
    if (actualPreset == XeSSPreset::Custom) {
      float currentScale = RtxOptions::resolutionScale();
      resolutionScaleChanged = (currentScale != m_lastResolutionScale);
      m_lastResolutionScale = currentScale;
    }
    
    if (m_actualPreset == actualPreset && 
        displaySize[0] == m_xessOutputSize.width && 
        displaySize[1] == m_xessOutputSize.height &&
        !resolutionScaleChanged) {
      // Nothing changed that would alter XeSS resolution(s), so return the last cached optimal render size
      outRenderSize[0] = m_inputSize.width;
      outRenderSize[1] = m_inputSize.height;
      return;
    }
    
    m_actualPreset = actualPreset;
    m_recreate = true;
    m_preset = preset;
    
    if (m_preset == XeSSPreset::NativeAA) {
      m_inputSize.width = outRenderSize[0] = displaySize[0];
      m_inputSize.height = outRenderSize[1] = displaySize[1];
    } else if (m_preset == XeSSPreset::Custom) {
      // Use resolution scale for custom preset
      float scale = RtxOptions::resolutionScale();
      m_inputSize.width = outRenderSize[0] = std::max(1u, (uint32_t)(displaySize[0] * scale));
      m_inputSize.height = outRenderSize[1] = std::max(1u, (uint32_t)(displaySize[1] * scale));
    } else {
      // Calculate optimal input resolution based on quality setting
      xess_2d_t outputRes = { displaySize[0], displaySize[1] };
      xess_2d_t inputRes;
      
      xess_quality_settings_t quality = presetToQuality(m_actualPreset);
      
      bool useFallbackScaling = false;

      // Use XeSS SDK to get optimal input resolution
      if (m_xessContext) {
        xess_result_t result = xessGetOptimalInputResolution(m_xessContext, &outputRes, quality, &inputRes, nullptr, nullptr);
        if (result == XESS_RESULT_SUCCESS) {
          m_inputSize.width = outRenderSize[0] = inputRes.x;
          m_inputSize.height = outRenderSize[1] = inputRes.y;
        } else {
          useFallbackScaling = true;
        }
      } else {
        useFallbackScaling = true;
      }

      // Fallback to manual calculation using correct XeSS 1.3+ scaling factors
      if (useFallbackScaling) {
        const float downscaleFactor = 1.f / calcUpscaleFactor();
        m_inputSize.width = outRenderSize[0] = std::max(1u, (uint32_t) (displaySize[0] * downscaleFactor));
        m_inputSize.height = outRenderSize[1] = std::max(1u, (uint32_t) (displaySize[1] * downscaleFactor));
      }
    }

    m_xessOutputSize.width = displaySize[0];
    m_xessOutputSize.height = displaySize[1];
    
    // Update the camera system with the current upscaling ratio for dynamic jitter calculation
    float currentUpscalingRatio = (float)displaySize[0] / (float)m_inputSize.width;
  }

  void DxvkXeSS::getInputSize(uint32_t& width, uint32_t& height) const {
    width = m_inputSize.width;
    height = m_inputSize.height;
  }

  void DxvkXeSS::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = m_xessOutputSize.width;
    height = m_xessOutputSize.height;
  }
} 