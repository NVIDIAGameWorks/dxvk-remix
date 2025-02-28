/*
* Copyright (c) 2024, AMD Corporation. All rights reserved.
*/
#include "rtx_fsr.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_imgui.h"
#include "rtx_matrix_helpers.h"

namespace dxvk {

const char* fsrProfileToString(FSRProfile fsrProfile) {
  switch (fsrProfile) {
    case FSRProfile::UltraPerf: return "Ultra Performance";
    case FSRProfile::MaxPerf: return "Max Performance";
    case FSRProfile::Balanced: return "Balanced";
    case FSRProfile::MaxQuality: return "Max Quality";
    case FSRProfile::Auto: return "Auto";
    case FSRProfile::FullResolution: return "Full Resolution";
    default:
    case FSRProfile::Invalid: return "Invalid";
  }
}

DxvkFSR::DxvkFSR(DxvkDevice* device)
  : CommonDeviceObject(device)
  , RtxPass(device) {
  // Initialize FSR context
  if (!m_contextInitialized) {
    initializeFSR(nullptr);
  }
}

DxvkFSR::~DxvkFSR() {
  onDestroy();
}

void DxvkFSR::onDestroy() {
  if (m_contextInitialized) {
    ffxFsr3ContextDestroy(&m_fsrContext);
    m_contextInitialized = false;
  }
}

void DxvkFSR::release() {
  m_recreate = true;
  if (m_contextInitialized) {
    ffxFsr3ContextDestroy(&m_fsrContext);
    m_contextInitialized = false;
  }
}

bool DxvkFSR::supportsFSR() const {
  // Check for FSR3 support - this should check for actual hardware/driver requirements
  return true;
}

bool DxvkFSR::isEnabled() const {
  return RtxOptions::Get()->isFSREnabled();
}

FSRProfile DxvkFSR::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
  FSRProfile desiredProfile = FSRProfile::UltraPerf;

  // Standard display resolution based FSR config
  if (displayHeight <= 1080) {
    desiredProfile = FSRProfile::MaxQuality;
  } else if (displayHeight < 2160) {
    desiredProfile = FSRProfile::Balanced;
  } else if (displayHeight < 4320) {
    desiredProfile = FSRProfile::MaxPerf;
  } else {
    desiredProfile = FSRProfile::UltraPerf;
  }

  if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Medium) {
    // When using medium preset, bias FSR more towards performance
    desiredProfile = (FSRProfile)std::max(0, (int)desiredProfile - 1);
  } else if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Low) {
    desiredProfile = FSRProfile::UltraPerf;
  }

  return desiredProfile;
}

void DxvkFSR::setSetting(const uint32_t displaySize[2], const FSRProfile profile, uint32_t outRenderSize[2]) {
  ScopedCpuProfileZone();
  
  FSRProfile actualProfile = profile;
  if (actualProfile == FSRProfile::Auto) {
    actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
  }

  if (m_actualProfile == actualProfile && 
      displaySize[0] == m_outputSize[0] && 
      displaySize[1] == m_outputSize[1]) {
    outRenderSize[0] = m_inputSize[0];
    outRenderSize[1] = m_inputSize[1];
    return;
  }

  m_actualProfile = actualProfile;
  m_recreate = true;
  m_profile = profile;

  if (m_profile == FSRProfile::FullResolution) {
    m_inputSize[0] = outRenderSize[0] = displaySize[0];
    m_inputSize[1] = outRenderSize[1] = displaySize[1];
  } else {
    // Calculate optimal render resolution based on FSR quality mode
    float scaleFactors[] = { 3.0f, 2.0f, 1.7f, 1.5f }; // UltraPerf to MaxQuality
    float scaleFactor = scaleFactors[std::min((int)m_actualProfile, 3)];
    
    m_inputSize[0] = outRenderSize[0] = uint32_t(displaySize[0] / scaleFactor);
    m_inputSize[1] = outRenderSize[1] = uint32_t(displaySize[1] / scaleFactor);

    // Ensure dimensions are multiples of 8
    m_inputSize[0] = (m_inputSize[0] + 7) & ~7;
    m_inputSize[1] = (m_inputSize[1] + 7) & ~7;
  }

  m_outputSize[0] = displaySize[0];
  m_outputSize[1] = displaySize[1];
}

FSRProfile DxvkFSR::getCurrentProfile() const {
  return m_actualProfile;
}

void DxvkFSR::getInputSize(uint32_t& width, uint32_t& height) const {
  width = m_inputSize[0];
  height = m_inputSize[1];
}

void DxvkFSR::getOutputSize(uint32_t& width, uint32_t& height) const {
  width = m_outputSize[0];
  height = m_outputSize[1];
}

void DxvkFSR::dispatch(
  Rc<RtxContext> ctx,
  DxvkBarrierSet& barriers,
  const Resources::RaytracingOutput& rtOutput,
  bool resetHistory) {
  ScopedGpuProfileZone(ctx, "FSR");

  if (m_recreate) {
    initializeFSR(ctx);
    m_recreate = false;
  }

  // Set up FSR3 dispatch parameters
  FfxFsr3DispatchDescription dispatchParams = {};
  dispatchParams.commandList = ctx->getCommandList();
  dispatchParams.color = rtOutput.m_compositeOutput.view(Resources::AccessType::Read)->handle();
  dispatchParams.depth = rtOutput.m_primaryDepth.view->handle();
  dispatchParams.motionVectors = rtOutput.m_primaryScreenSpaceMotionVector.view->handle();
  dispatchParams.exposure = nullptr; // Optional
  dispatchParams.reactive = nullptr; // Optional
  dispatchParams.transparencyAndComposition = nullptr; // Optional
  dispatchParams.output = rtOutput.m_finalOutput.view->handle();
  dispatchParams.jitterOffset[0] = 0.0f; // Get from camera
  dispatchParams.jitterOffset[1] = 0.0f;
  dispatchParams.motionVectorScale.x = 1.0f;
  dispatchParams.motionVectorScale.y = 1.0f;
  dispatchParams.reset = resetHistory;
  dispatchParams.enableSharpening = true;
  dispatchParams.sharpness = RtxOptions::Get()->fsrSharpness();
  dispatchParams.frameTimeDelta = 16.7f; // Get actual frame time
  dispatchParams.preExposure = 1.0f;
  dispatchParams.renderSize.width = m_inputSize[0];
  dispatchParams.renderSize.height = m_inputSize[1];
  dispatchParams.displaySize.width = m_outputSize[0];
  dispatchParams.displaySize.height = m_outputSize[1];

  // Execute FSR3 upscaling
  ffxFsr3ContextDispatch(&m_fsrContext, &dispatchParams);

  // Handle barriers
  for (auto& barrier : barriers) {
    ctx->getCommandList()->trackResource<DxvkAccess::Write>(barrier.image);
  }
}

void DxvkFSR::showImguiSettings() {
  ImGui::Checkbox("Frame Generation", &m_enableFrameGeneration);
  ImGui::SliderFloat("Sharpness", &m_sharpness, 0.0f, 1.0f);
}

void DxvkFSR::initializeFSR(Rc<DxvkContext> ctx) {
  if (m_contextInitialized) {
    ffxFsr3ContextDestroy(&m_fsrContext);
  }

  // Initialize FSR3 context
  FfxFsr3CreateParams createParams = {};
  createParams.flags = FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
  createParams.maxRenderSize.width = m_inputSize[0];
  createParams.maxRenderSize.height = m_inputSize[1];
  createParams.displaySize.width = m_outputSize[0];
  createParams.displaySize.height = m_outputSize[1];
  
  // Initialize the FSR3 context
  FfxErrorCode errorCode = ffxFsr3ContextCreate(&m_fsrContext, &createParams);
  if (errorCode == FFX_OK) {
    m_contextInitialized = true;
  }
}

} // namespace dxvk 