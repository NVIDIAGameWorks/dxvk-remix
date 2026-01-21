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
#include <cmath>
#include <filesystem>
#include <Shlwapi.h>

#include "rtx.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "dxvk_device.h"
#include "rtx_fsr.h"
#include "rtx_camera.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "rtx_auto_exposure.h"
#include "../../util/util_math.h"
#include "../util/util_string.h"
#include "../util/log/log.h"

// FFX loader is already included via rtx_fsr.h

namespace dxvk {

  // Convert VkFormat to FFX API surface format
  static FfxApiSurfaceFormat vkFormatToFfxFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R32G32B32A32_FLOAT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R32G32_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R8_UNORM:
      return FFX_API_SURFACE_FORMAT_R8_UNORM;
    case VK_FORMAT_R32_UINT:
      return FFX_API_SURFACE_FORMAT_R32_UINT;
    case VK_FORMAT_R8G8B8A8_UNORM:
      return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_UNORM:
      return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM; // Treat as same for FFX
    case VK_FORMAT_R8G8B8A8_SRGB:
      return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
      return FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB; // Treat as same for FFX
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
    case VK_FORMAT_R16G16_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R16G16_UINT:
      return FFX_API_SURFACE_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16_UINT:
      return FFX_API_SURFACE_FORMAT_R16_UINT;
    case VK_FORMAT_R16_UNORM:
      return FFX_API_SURFACE_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM:
      return FFX_API_SURFACE_FORMAT_R16_SNORM;
    case VK_FORMAT_R8_UINT:
      return FFX_API_SURFACE_FORMAT_R8_UINT;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R32_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    case VK_FORMAT_D32_SFLOAT:
      return FFX_API_SURFACE_FORMAT_R32_FLOAT; // Depth as R32_FLOAT for FFX
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return FFX_API_SURFACE_FORMAT_R32_FLOAT; // Depth portion
    case VK_FORMAT_D24_UNORM_S8_UINT:
      return FFX_API_SURFACE_FORMAT_R32_FLOAT; // Approximate as R32
    default:
      Logger::warn(str::format("FSR3: Unknown VkFormat for FFX conversion: ", static_cast<uint32_t>(format), ", defaulting to R8G8B8A8_UNORM"));
      return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    }
  }

  const char* fsrPresetToString(FSRPreset preset) {
    switch (preset) {
    case FSRPreset::UltraPerformance: return "Ultra Performance";
    case FSRPreset::Performance: return "Performance";
    case FSRPreset::Balanced: return "Balanced";
    case FSRPreset::Quality: return "Quality";
    case FSRPreset::NativeAA: return "Native Anti-Aliasing";
    case FSRPreset::Custom: return "Custom";
    default:
      assert(false);
    case FSRPreset::Invalid: return "Invalid";
    }
  }

  DxvkFSR::DxvkFSR(DxvkDevice* device)
    : RtxPass(device),
      CommonDeviceObject(device),
      m_initialized(false),
      m_upscalingContext(nullptr),
      m_targetExtent{0, 0, 0},
      m_inputSize{0, 0},
      m_fsrOutputSize{0, 0},
      m_jitterIndex(0),
      m_hFFX(nullptr) {
  }

  DxvkFSR::~DxvkFSR() {
    destroyFSRContext();
  }

  bool DxvkFSR::isEnabled() const {
    return RtxOptions::isFSREnabled();
  }

  bool DxvkFSR::onActivation(Rc<DxvkContext>& ctx) {
    m_recreate = true;
    Logger::info("FSR3: Activated successfully");
    return true;
  }

  void DxvkFSR::onDeactivation() {
    if (m_upscalingContext) {
      destroyFSRContext();
    }
    m_initialized = false;
  }

  float DxvkFSR::getUpscaleFactor(FSRPreset preset) const {
    switch (preset) {
    case FSRPreset::UltraPerformance: return 3.0f;
    case FSRPreset::Performance:      return 2.0f;
    case FSRPreset::Balanced:         return 1.7f;
    case FSRPreset::Quality:          return 1.5f;
    case FSRPreset::NativeAA:         return 1.0f;
    case FSRPreset::Custom:           return 1.0f / RtxOptions::resolutionScale();
    default:                          return 1.7f;
    }
  }

  float DxvkFSR::calcUpscaleFactor() const {
    if (FSROptions::preset() == FSRPreset::Custom) {
      return 1.0f / RtxOptions::resolutionScale();
    } else {
      return getUpscaleFactor(FSROptions::preset());
    }
  }

  float DxvkFSR::calcRecommendedMipBias() const {
    // FSR formula: -log2(upscale_factor)
    float upscaleFactor = calcUpscaleFactor();
    return -std::log2(upscaleFactor);
  }

  uint32_t DxvkFSR::calcRecommendedJitterSequenceLength() const {
    // FSR3 recommendation: use standard Halton sequence length based on upscale ratio
    const float upscaleFactor = calcUpscaleFactor();
    uint32_t recommendedLength = static_cast<uint32_t>(std::ceil(upscaleFactor * upscaleFactor * 8.0f));
    return std::clamp(recommendedLength, 8u, 64u);
  }

  VkExtent3D DxvkFSR::getInputSize(const VkExtent3D& targetExtent) const {
    float upscaleFactor = calcUpscaleFactor();
    
    VkExtent3D inputSize;
    inputSize.width = static_cast<uint32_t>(std::roundf(targetExtent.width / upscaleFactor));
    inputSize.height = static_cast<uint32_t>(std::roundf(targetExtent.height / upscaleFactor));
    inputSize.depth = 1;
    
    // Ensure minimum size
    inputSize.width = std::max(inputSize.width, 1u);
    inputSize.height = std::max(inputSize.height, 1u);
    
    return inputSize;
  }

  void DxvkFSR::getInputSize(uint32_t& width, uint32_t& height) const {
    width = m_inputSize.width;
    height = m_inputSize.height;
  }

  void DxvkFSR::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = m_fsrOutputSize.width;
    height = m_fsrOutputSize.height;
  }

  void DxvkFSR::setSetting(const uint32_t displaySize[2], const FSRPreset preset, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    
    FSRPreset actualPreset = preset;
    if (actualPreset == FSRPreset::Invalid) {
      actualPreset = FSRPreset::Balanced;
    }

    // Check for resolution scale changes in Custom preset
    float currentScale = RtxOptions::resolutionScale();
    bool scaleChanged = (actualPreset == FSRPreset::Custom && m_lastResolutionScale != currentScale);
    
    if (m_actualPreset == actualPreset && 
        displaySize[0] == m_fsrOutputSize.width && 
        displaySize[1] == m_fsrOutputSize.height &&
        !scaleChanged) {
      // Nothing changed, return cached values
      outRenderSize[0] = m_inputSize.width;
      outRenderSize[1] = m_inputSize.height;
      return;
    }
    
    m_actualPreset = actualPreset;
    m_lastResolutionScale = currentScale;
    m_recreate = true;

    // Calculate render resolution based on preset
    float upscaleFactor = getUpscaleFactor(actualPreset);
    
    m_fsrOutputSize.width = displaySize[0];
    m_fsrOutputSize.height = displaySize[1];
    
    m_inputSize.width = static_cast<uint32_t>(std::roundf(displaySize[0] / upscaleFactor));
    m_inputSize.height = static_cast<uint32_t>(std::roundf(displaySize[1] / upscaleFactor));
    
    // Ensure minimum size
    m_inputSize.width = std::max(m_inputSize.width, 1u);
    m_inputSize.height = std::max(m_inputSize.height, 1u);
    
    outRenderSize[0] = m_inputSize.width;
    outRenderSize[1] = m_inputSize.height;
    
    Logger::debug(str::format("FSR3: setSetting display=", displaySize[0], "x", displaySize[1], 
                              " render=", m_inputSize.width, "x", m_inputSize.height,
                              " preset=", fsrPresetToString(actualPreset)));
  }

  void DxvkFSR::createFSRContext(const VkExtent3D& targetExtent) {
    ScopedCpuProfileZone();
    
    if (m_upscalingContext) {
      destroyFSRContext();
    }

    m_targetExtent = targetExtent;

    // Create Vulkan backend
    ffx::CreateBackendVKDesc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backendDesc.vkDevice = device()->handle();
    backendDesc.vkPhysicalDevice = device()->adapter()->handle();
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    // Create upscale context
    ffx::CreateContextDescUpscale createDesc{};
    createDesc.maxUpscaleSize = { targetExtent.width, targetExtent.height };
    createDesc.maxRenderSize = { targetExtent.width, targetExtent.height };
    createDesc.flags = 0;
    
    if (FSROptions::useAutoExposure()) {
      createDesc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    }
    
    if (FSROptions::enableHDR()) {
      createDesc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
    }
    
    // Assume inverted infinite depth (common in modern renderers)
    createDesc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE;

    ffx::ReturnCode retCode = ffx::CreateContext(m_upscalingContext, nullptr, createDesc, backendDesc);
    
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR3: Failed to create upscaling context: ", static_cast<uint32_t>(retCode)));
      m_upscalingContext = nullptr;
      m_initialized = false;
      return;
    }

    m_initialized = true;
    m_jitterIndex = 0;
    
    Logger::info(str::format("FSR3: Created context ", targetExtent.width, "x", targetExtent.height));
  }

  void DxvkFSR::destroyFSRContext() {
    if (m_upscalingContext) {
      ffx::DestroyContext(m_upscalingContext);
      m_upscalingContext = nullptr;
    }
    m_initialized = false;
  }

  void DxvkFSR::initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent) {
    createFSRContext(targetExtent);
  }

  void DxvkFSR::dispatch(
    Rc<DxvkContext> renderContext,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    RtCamera& camera,
    bool resetHistory,
    float deltaTimeMs) {
    
    ScopedGpuProfileZone(renderContext, "FSR3 Upscale");

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

    // Initialize FSR if needed
    if (m_recreate || !m_initialized) {
      VkExtent3D targetExtent;
      if (m_fsrOutputSize.width > 0 && m_fsrOutputSize.height > 0) {
        targetExtent = { 
          m_fsrOutputSize.width,
          m_fsrOutputSize.height,
          1
        };
      } else {
        targetExtent = { 
          rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image->info().extent.width,
          rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image->info().extent.height,
          1
        };
      }
      initialize(renderContext, targetExtent);
      m_recreate = false;
    }

    if (!m_initialized || !m_upscalingContext) {
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

    // Set up image barriers for FSR inputs and outputs
    auto colorView = rtOutput.m_compositeOutput.view(Resources::AccessType::Read);
    auto motionView = rtOutput.m_primaryScreenSpaceMotionVector.view;
    auto depthView = rtOutput.m_primaryDepth.view;
    auto outputView = rtOutput.m_finalOutput.view(Resources::AccessType::Write);

    // Input barriers
    barriers.accessImage(
      colorView->image(),
      colorView->imageSubresources(),
      colorView->imageInfo().layout,
      colorView->imageInfo().stages,
      colorView->imageInfo().access,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT);

    barriers.accessImage(
      motionView->image(),
      motionView->imageSubresources(),
      motionView->imageInfo().layout,
      motionView->imageInfo().stages,
      motionView->imageInfo().access,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT);

    barriers.accessImage(
      depthView->image(),
      depthView->imageSubresources(),
      depthView->imageInfo().layout,
      depthView->imageInfo().stages,
      depthView->imageInfo().access,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_READ_BIT);

    // Output barrier
    barriers.accessImage(
      outputView->image(),
      outputView->imageSubresources(),
      outputView->imageInfo().layout,
      outputView->imageInfo().stages,
      outputView->imageInfo().access,
      VK_IMAGE_LAYOUT_GENERAL,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_ACCESS_SHADER_WRITE_BIT);

    barriers.recordCommands(renderContext->getCommandList());

    // Get jitter from camera
    float jitterOffset[2];
    camera.getJittering(jitterOffset);
    m_jitterX = jitterOffset[0];
    m_jitterY = jitterOffset[1];

    // Get resolution info
    uint32_t renderWidth = m_inputSize.width > 0 ? m_inputSize.width : rtOutput.m_compositeOutputExtent.width;
    uint32_t renderHeight = m_inputSize.height > 0 ? m_inputSize.height : rtOutput.m_compositeOutputExtent.height;
    uint32_t displayWidth = m_fsrOutputSize.width > 0 ? m_fsrOutputSize.width : outputView->imageInfo().extent.width;
    uint32_t displayHeight = m_fsrOutputSize.height > 0 ? m_fsrOutputSize.height : outputView->imageInfo().extent.height;

    // Helper function to create FFX resource from Vulkan image using SDK helper
    auto createFfxResource = [](Rc<DxvkImageView> view, uint32_t state, uint32_t additionalUsages = FFX_API_RESOURCE_USAGE_READ_ONLY) -> FfxApiResource {
      // Build a VkImageCreateInfo from the image info for the SDK helper
      VkImageCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      createInfo.imageType = VK_IMAGE_TYPE_2D;
      createInfo.format = view->info().format;
      createInfo.extent.width = view->imageInfo().extent.width;
      createInfo.extent.height = view->imageInfo().extent.height;
      createInfo.extent.depth = 1;
      createInfo.mipLevels = 1;
      createInfo.arrayLayers = 1;
      createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      // Set usage based on the image's actual usage
      createInfo.usage = view->image()->info().usage;
      createInfo.flags = view->image()->info().flags;
      
      // Use SDK helper functions
      FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(view->image()->handle(), createInfo, additionalUsages);
      return ffxApiGetResourceVK(reinterpret_cast<void*>(view->image()->handle()), desc, state);
    };

    // Set up FSR dispatch parameters
    ffx::DispatchDescUpscale dispatchDesc{};
    dispatchDesc.commandList = renderContext->getCommandList()->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    dispatchDesc.color = createFfxResource(colorView, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    dispatchDesc.depth = createFfxResource(depthView, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_DEPTHTARGET);
    dispatchDesc.motionVectors = createFfxResource(motionView, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    dispatchDesc.exposure = {}; // Auto-exposure
    dispatchDesc.reactive = {}; // No reactive mask
    dispatchDesc.transparencyAndComposition = {}; // No T&C mask
    // Output is written as UAV - use UNORDERED_ACCESS state so FFX leaves it ready for subsequent reads
    dispatchDesc.output = createFfxResource(outputView, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);

    // Pass jitter values directly to FSR - no negation needed
    // FSR expects the same jitter values that were applied to the projection matrix
    dispatchDesc.jitterOffset.x = m_jitterX;
    dispatchDesc.jitterOffset.y = m_jitterY;
    
    // Motion vectors are already in absolute pixel units (like DLSS), so scale is 1.0
    dispatchDesc.motionVectorScale.x = 1.f;
    dispatchDesc.motionVectorScale.y = 1.f;
    
    dispatchDesc.renderSize.width = renderWidth;
    dispatchDesc.renderSize.height = renderHeight;
    dispatchDesc.upscaleSize.width = displayWidth;
    dispatchDesc.upscaleSize.height = displayHeight;
    dispatchDesc.enableSharpening = FSROptions::sharpness() > 0.0f;
    dispatchDesc.sharpness = FSROptions::sharpness();
    dispatchDesc.frameTimeDelta = deltaTimeMs;
    dispatchDesc.preExposure = 1.0f;
    dispatchDesc.reset = resetHistory;
    
    // Pass actual near/far values - FFX_UPSCALE_ENABLE_DEPTH_INVERTED flag handles the inversion
    // SDK internally handles the near/far based on the inverted flag
    dispatchDesc.cameraNear = camera.getNearPlane();
    dispatchDesc.cameraFar = camera.getFarPlane();
    dispatchDesc.cameraFovAngleVertical = camera.getFov();
    dispatchDesc.flags = 0;
    
    // Increment jitter index
    m_jitterIndex++;

    // Execute FSR
    ffx::ReturnCode retCode = ffx::Dispatch(m_upscalingContext, dispatchDesc);
    
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::warn(str::format("FSR3: Dispatch failed: ", static_cast<uint32_t>(retCode)));
      
      // Fallback to simple copy on failure
      renderContext->copyImage(
        rtOutput.m_finalOutput.resource(Resources::AccessType::Write).image,
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { 0, 0, 0 },
        rtOutput.m_compositeOutputExtent);
    }
  }

  void DxvkFSR::showImguiSettings() {
    ImGui::Checkbox("Auto Exposure", &FSROptions::useAutoExposureObject());
    ImGui::SetTooltipToLastWidgetOnHover("Use FSR3's automatic exposure handling.");
    
    ImGui::Checkbox("HDR Mode", &FSROptions::enableHDRObject());
    ImGui::SetTooltipToLastWidgetOnHover("Enable HDR input/output for FSR3.");
    
    ImGui::SliderFloat("Sharpening", &FSROptions::sharpnessObject(), 0.0f, 1.0f, "%.2f");
    ImGui::SetTooltipToLastWidgetOnHover("FSR3 RCAS sharpening strength. 0.0 = off, 1.0 = maximum.");
  }

}
