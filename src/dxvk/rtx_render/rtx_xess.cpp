/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "../../util/util_math.h"
#include "../util/util_string.h"
#include "../util/log/log.h"

#include "../../../submodules/xess/inc/xess/xess.h"
#include "../../../submodules/xess/inc/xess/xess_vk.h"

namespace dxvk {
  const char* xessProfileToString(XeSSProfile xessProfile) {
    switch (xessProfile) {
    case XeSSProfile::UltraPerf: return "Ultra Performance";
    case XeSSProfile::Performance: return "Performance";
    case XeSSProfile::Balanced: return "Balanced";
    case XeSSProfile::Quality: return "Quality";
    case XeSSProfile::UltraQuality: return "Ultra Quality";
    case XeSSProfile::UltraQualityPlus: return "Ultra Quality Plus";
    case XeSSProfile::NativeAA: return "Native Anti-Aliasing";
    case XeSSProfile::Custom: return "Custom";
    default:
      assert(false);
    case XeSSProfile::Invalid: return "Invalid";
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

  // Default constructor
  DxvkXeSS::DxvkXeSS() 
    : m_device(nullptr), m_context(nullptr) {
    Logger::info("[RTX-XeSS] XeSS object created without device (default constructor)");
  }

  // Constructor with device
  DxvkXeSS::DxvkXeSS(DxvkDevice* device) 
    : m_device(device), m_context(nullptr) {
    Logger::info("XeSS: Initializing XeSS upscaler...");
    
    // First check if XeSS is enabled in options
    if (!RtxOptions::isXeSSEnabled()) {
      Logger::info("XeSS: Disabled in options (upscaler type is not XeSS)");
      m_enabled = false;
      return;
    }
    
    Logger::info("XeSS: Enabled in options, checking system support...");
    
    // Check if XeSS is supported on this system
    if (!validateXeSSSupport(device)) {
      Logger::warn("XeSS: System does not support XeSS - falling back to other upscaler");
      m_enabled = false;
      return;
    }
    
    m_enabled = true;
    Logger::info("XeSS: Successfully initialized and ready for use");
  }

  DxvkXeSS::~DxvkXeSS() {
    destroyXeSSContext();
  }

  void DxvkXeSS::onDestroy() {
    destroyXeSSContext();
  }

  void DxvkXeSS::release() {
    if (m_xessContext) {
      destroyXeSSContext();
    }
    m_initialized = false;
  }

  bool DxvkXeSS::isXeSSLibraryAvailable() {
    Logger::info("[RTX-XeSS] Checking XeSS library availability...");
    
    // Try to get XeSS version to test if library is available
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    
    if (result == XESS_RESULT_SUCCESS) {
      Logger::info(str::format("[RTX-XeSS] XeSS SDK version: ", version.major, ".", version.minor, ".", version.patch));
      return true;
    } else {
      Logger::warn(str::format("[RTX-XeSS] XeSS library not available: ", xessResultToString(result)));
      return false;
    }
  }

  bool DxvkXeSS::validateXeSSSupport(DxvkDevice* device) {
    Logger::info("[RTX-XeSS] Validating XeSS support...");
    
    if (!isXeSSLibraryAvailable()) {
      return false;
    }

    // Get XeSS version
    xess_version_t version;
    xess_result_t result = xessGetVersion(&version);
    if (result != XESS_RESULT_SUCCESS) {
      Logger::warn(str::format("[RTX-XeSS] Failed to get XeSS version: ", xessResultToString(result)));
      return false;
    }
    
    Logger::info(str::format("[RTX-XeSS] XeSS SDK version: ", version.major, ".", version.minor, ".", version.patch));

    // Check required instance extensions
    uint32_t instanceExtCount = 0;
    const char* const* instanceExtensions = nullptr;
    uint32_t minVkApiVersion = 0;
    
    result = xessVKGetRequiredInstanceExtensions(&instanceExtCount, &instanceExtensions, &minVkApiVersion);
    if (result == XESS_RESULT_SUCCESS) {
      Logger::info(str::format("[RTX-XeSS] XeSS requires ", instanceExtCount, " instance extensions, min Vulkan API version: ", minVkApiVersion));
      
      // Log the required extensions
      for (uint32_t i = 0; i < instanceExtCount; i++) {
        Logger::info(str::format("[RTX-XeSS] Required instance extension: ", instanceExtensions[i]));
      }
    } else {
      Logger::warn(str::format("[RTX-XeSS] Failed to get required instance extensions: ", xessResultToString(result)));
    }

    // Check required device extensions
    uint32_t deviceExtCount = 0;
    const char* const* deviceExtensions = nullptr;
    
    result = xessVKGetRequiredDeviceExtensions(
      device->instance()->handle(),
      device->adapter()->handle(),
      &deviceExtCount,
      &deviceExtensions
    );
    
    if (result == XESS_RESULT_SUCCESS) {
      Logger::info(str::format("[RTX-XeSS] XeSS requires ", deviceExtCount, " device extensions"));
      
      // Log the required extensions
      for (uint32_t i = 0; i < deviceExtCount; i++) {
        Logger::info(str::format("[RTX-XeSS] Required device extension: ", deviceExtensions[i]));
      }
    } else {
      Logger::warn(str::format("[RTX-XeSS] Failed to get required device extensions: ", xessResultToString(result)));
    }

    // GPU compatibility check
    auto adapter = device->adapter();
    auto deviceProps = adapter->deviceProperties();
    
    if (deviceProps.vendorID == 0x8086) { // Intel
      Logger::info("[RTX-XeSS] Intel GPU detected - using optimized XeSS path");
    } else {
      Logger::info("[RTX-XeSS] Non-Intel GPU detected - using generic XeSS path");
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
      Logger::info("[RTX-XeSS] XeSS context creation test successful");
      // Clean up test context
      xessDestroyContext(testContext);
      return true;
    } else {
      Logger::warn(str::format("[RTX-XeSS] XeSS context creation test failed: ", xessResultToString(result)));
      return false;
    }
  }

  xess_quality_settings_t DxvkXeSS::profileToQuality(XeSSProfile profile) const {
    switch (profile) {
      case XeSSProfile::UltraPerf: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
      case XeSSProfile::Performance: return XESS_QUALITY_SETTING_PERFORMANCE;
      case XeSSProfile::Balanced: return XESS_QUALITY_SETTING_BALANCED;
      case XeSSProfile::Quality: return XESS_QUALITY_SETTING_QUALITY;
      case XeSSProfile::UltraQuality: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
      case XeSSProfile::UltraQualityPlus: return XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS;
      case XeSSProfile::NativeAA: return XESS_QUALITY_SETTING_AA;
      case XeSSProfile::Custom: return XESS_QUALITY_SETTING_BALANCED; // Use balanced as base for custom
      default: return XESS_QUALITY_SETTING_BALANCED;
    }
  }

  VkExtent3D DxvkXeSS::getInputSize(const VkExtent3D& targetExtent) const {
    if (!m_enabled) {
      return targetExtent;
    }

    XeSSProfile currentProfile = getProfile();
    float scaleFactor = 1.0f;
    
    if (currentProfile == XeSSProfile::Custom) {
      // For Custom profile, use resolution scale directly
      scaleFactor = RtxOptions::resolutionScale();
    } else {
      // For other profiles, use predefined scaling factors based on quality setting
      xess_quality_settings_t quality = profileToQuality(currentProfile);
      
      switch (quality) {
        case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scaleFactor = 1.0f / 3.0f; break;   // 3.0x upscaling
        case XESS_QUALITY_SETTING_PERFORMANCE:       scaleFactor = 1.0f / 2.3f; break;   // 2.3x upscaling
        case XESS_QUALITY_SETTING_BALANCED:          scaleFactor = 1.0f / 2.0f; break;   // 2.0x upscaling
        case XESS_QUALITY_SETTING_QUALITY:           scaleFactor = 1.0f / 1.7f; break;   // 1.7x upscaling
        case XESS_QUALITY_SETTING_ULTRA_QUALITY:     scaleFactor = 1.0f / 1.5f; break;   // 1.5x upscaling
        case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scaleFactor = 1.0f / 1.3f; break;  // 1.3x upscaling
        case XESS_QUALITY_SETTING_AA:                scaleFactor = 1.0f; break;          // 1.0x (native)
        default:                                      scaleFactor = 1.0f / 2.0f; break;   // Default to balanced
      }
    }

    VkExtent3D inputExtent;
    inputExtent.width = std::max(1u, static_cast<uint32_t>(targetExtent.width * scaleFactor));
    inputExtent.height = std::max(1u, static_cast<uint32_t>(targetExtent.height * scaleFactor));
    inputExtent.depth = targetExtent.depth;

    return inputExtent;
  }

  void DxvkXeSS::initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent) {
    if (!m_enabled) {
      return;
    }

    // Check if we need to recreate the context
    XeSSProfile currentProfile = getProfile();
    if (m_initialized && 
        m_targetExtent.width == targetExtent.width &&
        m_targetExtent.height == targetExtent.height &&
        m_currentProfile == currentProfile) {
      return; // Already initialized with correct settings
    }

    // Release existing context if any
    if (m_xessContext) {
      destroyXeSSContext();
    }

    m_targetExtent = targetExtent;
    m_inputExtent = getInputSize(targetExtent);
    m_currentProfile = currentProfile;

    createXeSSContext(targetExtent);
    m_initialized = true;

    Logger::info(str::format("XeSS: Initialized with input resolution ", m_inputExtent.width, "x", m_inputExtent.height, 
                            " -> output resolution ", m_targetExtent.width, "x", m_targetExtent.height));
  }

  void DxvkXeSS::createXeSSContext(const VkExtent3D& targetExtent) {
    Logger::info("XeSS: Creating XeSS context...");
    
    if (!m_device) {
      Logger::err("XeSS: Cannot create context - no device available");
      return;
    }
    
    // Create XeSS context using real SDK
    xess_result_t result = xessVKCreateContext(
      m_device->instance()->handle(),
      m_device->adapter()->handle(),
      m_device->handle(),
      &m_xessContext
    );
    
    if (result != XESS_RESULT_SUCCESS) {
      Logger::err(str::format("XeSS: Failed to create context: ", xessResultToString(result)));
      m_xessContext = nullptr;
      return;
    }
    
    Logger::info("XeSS: Context created successfully");
    
    // Initialize XeSS with current settings
    xess_vk_init_params_t initParams = {};
    initParams.outputResolution.x = targetExtent.width;
    initParams.outputResolution.y = targetExtent.height;
    initParams.qualitySetting = profileToQuality(m_currentProfile);
    initParams.initFlags = XESS_INIT_FLAG_NONE; // Can be extended with more flags as needed
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
    
    Logger::info("XeSS: Context initialized successfully");
  }

  void DxvkXeSS::destroyXeSSContext() {
    if (m_xessContext) {
      Logger::info("XeSS: Destroying XeSS context");
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
    
    if (!m_enabled) {
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
        Logger::warn("XeSS: setSetting() not called yet, using fallback resolution for initialization");
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

    Logger::debug("XeSS: Dispatching upscaling");
    
    // Set up image barriers for XeSS inputs and outputs
    std::vector<Rc<DxvkImageView>> inputs = {
      rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
      rtOutput.m_primaryScreenSpaceMotionVector.view,
      rtOutput.m_primaryDepth.view
    };

    std::vector<Rc<DxvkImageView>> outputs = {
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
    SceneManager& sceneManager = m_device->getCommon()->getSceneManager();
    RtCamera& camera = sceneManager.getCamera();
    float jitterOffset[2];
    camera.getJittering(jitterOffset);

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

    // Execution parameters
    execParams.jitterOffsetX = jitterOffset[0];
    execParams.jitterOffsetY = jitterOffset[1];
    execParams.exposureScale = 1.0f; // Default exposure scale
    execParams.resetHistory = resetHistory ? 1 : 0;
    // Use the input size from setSetting for Custom profile, or m_inputExtent for other profiles
    if (getProfile() == XeSSProfile::Custom) {
      execParams.inputWidth = m_inputSize.width;
      execParams.inputHeight = m_inputSize.height;
    } else {
      execParams.inputWidth = m_inputExtent.width;
      execParams.inputHeight = m_inputExtent.height;
    }
    
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
      Logger::debug("XeSS: Execute successful");
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

  XeSSProfile DxvkXeSS::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
    XeSSProfile desiredProfile = XeSSProfile::UltraPerf;

    // Standard display resolution based XeSS config
    if (displayHeight <= 1080) {
      desiredProfile = XeSSProfile::Quality;
    } else if (displayHeight < 2160) {
      desiredProfile = XeSSProfile::Balanced;
    } else if (displayHeight < 4320) {
      desiredProfile = XeSSProfile::Performance;
    } else {
      // For > 4k (e.g. 8k)
      desiredProfile = XeSSProfile::UltraPerf;
    }

    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias XeSS more towards performance
      desiredProfile = (XeSSProfile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = (XeSSProfile) std::max(0, (int) desiredProfile - 2);
    }

    return desiredProfile;
  }

  void DxvkXeSS::setSetting(const uint32_t displaySize[2], const XeSSProfile profile, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    
    // Use the profile directly (Auto preset removed)
    XeSSProfile actualProfile = profile;

    // For Custom profile, also check if resolution scale has changed
    bool resolutionScaleChanged = false;
    if (actualProfile == XeSSProfile::Custom) {
      float currentScale = RtxOptions::resolutionScale();
      resolutionScaleChanged = (currentScale != m_lastResolutionScale);
      m_lastResolutionScale = currentScale;
    }
    
    if (m_actualProfile == actualProfile && 
        displaySize[0] == m_xessOutputSize.width && 
        displaySize[1] == m_xessOutputSize.height &&
        !resolutionScaleChanged) {
      // Nothing changed that would alter XeSS resolution(s), so return the last cached optimal render size
      outRenderSize[0] = m_inputSize.width;
      outRenderSize[1] = m_inputSize.height;
      return;
    }
    
    m_actualProfile = actualProfile;
    m_recreate = true;
    m_profile = profile;

    if (m_profile == XeSSProfile::NativeAA) {
      m_inputSize.width = outRenderSize[0] = displaySize[0];
      m_inputSize.height = outRenderSize[1] = displaySize[1];
    } else if (m_profile == XeSSProfile::Custom) {
      // Use resolution scale for custom profile
      float scale = RtxOptions::resolutionScale();
      m_inputSize.width = outRenderSize[0] = std::max(1u, (uint32_t)(displaySize[0] * scale));
      m_inputSize.height = outRenderSize[1] = std::max(1u, (uint32_t)(displaySize[1] * scale));
      Logger::debug(str::format("XeSS Custom: Using resolution scale ", scale, ", input: ", m_inputSize.width, "x", m_inputSize.height, ", output: ", displaySize[0], "x", displaySize[1]));
    } else {
      // Calculate optimal input resolution based on quality setting
      xess_2d_t outputRes = { displaySize[0], displaySize[1] };
      xess_2d_t inputRes;
      
      xess_quality_settings_t quality = profileToQuality(m_actualProfile);
      
      // Use XeSS SDK to get optimal input resolution
      if (m_xessContext) {
        xess_result_t result = xessGetOptimalInputResolution(m_xessContext, &outputRes, quality, &inputRes, nullptr, nullptr);
        if (result == XESS_RESULT_SUCCESS) {
          m_inputSize.width = outRenderSize[0] = inputRes.x;
          m_inputSize.height = outRenderSize[1] = inputRes.y;
        } else {
          // Fallback to manual calculation using correct XeSS 1.3+ scaling factors
          float scale = 1.0f;
          switch (quality) {
          case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scale = 1.0f / 3.0f; break;   // 3.0x upscaling
          case XESS_QUALITY_SETTING_PERFORMANCE: scale = 1.0f / 2.3f; break;        // 2.3x upscaling
          case XESS_QUALITY_SETTING_BALANCED: scale = 1.0f / 2.0f; break;           // 2.0x upscaling
          case XESS_QUALITY_SETTING_QUALITY: scale = 1.0f / 1.7f; break;            // 1.7x upscaling
          case XESS_QUALITY_SETTING_ULTRA_QUALITY: scale = 1.0f / 1.5f; break;      // 1.5x upscaling
          case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scale = 1.0f / 1.3f; break; // 1.3x upscaling
          case XESS_QUALITY_SETTING_AA: scale = 1.0f; break;                        // 1.0x (native)
          default: scale = 1.0f / 2.0f; break;                                      // Default to balanced
          }
          m_inputSize.width = outRenderSize[0] = std::max(1u, (uint32_t)(displaySize[0] * scale));
          m_inputSize.height = outRenderSize[1] = std::max(1u, (uint32_t)(displaySize[1] * scale));
        }
      } else {
        // Fallback calculation when no context available yet using correct XeSS 1.3+ scaling factors
        float scale = 1.0f;
        switch (quality) {
        case XESS_QUALITY_SETTING_ULTRA_PERFORMANCE: scale = 1.0f / 3.0f; break;   // 3.0x upscaling
        case XESS_QUALITY_SETTING_PERFORMANCE: scale = 1.0f / 2.3f; break;        // 2.3x upscaling
        case XESS_QUALITY_SETTING_BALANCED: scale = 1.0f / 2.0f; break;           // 2.0x upscaling
        case XESS_QUALITY_SETTING_QUALITY: scale = 1.0f / 1.7f; break;            // 1.7x upscaling
        case XESS_QUALITY_SETTING_ULTRA_QUALITY: scale = 1.0f / 1.5f; break;      // 1.5x upscaling
        case XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS: scale = 1.0f / 1.3f; break; // 1.3x upscaling
        case XESS_QUALITY_SETTING_AA: scale = 1.0f; break;                        // 1.0x (native)
        default: scale = 1.0f / 2.0f; break;                                      // Default to balanced
        }
        m_inputSize.width = outRenderSize[0] = std::max(1u, (uint32_t)(displaySize[0] * scale));
        m_inputSize.height = outRenderSize[1] = std::max(1u, (uint32_t)(displaySize[1] * scale));
      }
    }

    m_xessOutputSize.width = displaySize[0];
    m_xessOutputSize.height = displaySize[1];
  }

  XeSSProfile DxvkXeSS::getCurrentProfile() const {
    return m_actualProfile;
  }

  void DxvkXeSS::getInputSize(uint32_t& width, uint32_t& height) const {
    width = m_inputSize.width;
    height = m_inputSize.height;
  }

  void DxvkXeSS::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = m_xessOutputSize.width;
    height = m_xessOutputSize.height;
  }

  XeSSProfile DxvkXeSS::getAutoProfile() const {
    // Use the resolution-based auto profile selection with current output size
    uint32_t displayWidth = m_xessOutputSize.width > 0 ? m_xessOutputSize.width : 1920;
    uint32_t displayHeight = m_xessOutputSize.height > 0 ? m_xessOutputSize.height : 1080;
    
    XeSSProfile desiredProfile = XeSSProfile::UltraPerf;

    // Standard display resolution based XeSS config
    if (displayHeight <= 1080) {
      desiredProfile = XeSSProfile::Quality;
    } else if (displayHeight < 2160) {
      desiredProfile = XeSSProfile::Balanced;
    } else if (displayHeight < 4320) {
      desiredProfile = XeSSProfile::Performance;
    } else {
      // For > 4k (e.g. 8k)
      desiredProfile = XeSSProfile::UltraPerf;
    }

    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias XeSS more towards performance
      desiredProfile = (XeSSProfile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = (XeSSProfile) std::max(0, (int) desiredProfile - 2);
    }

    return desiredProfile;
  }

  void DxvkXeSS::setSetting(const char* name, const char* value) {
    // Placeholder for XeSS settings
    Logger::debug(str::format("XeSS: Setting ", name, " = ", value));
  }
} 