#include "rtx_fsr.h"
#include "dxvk_device.h"
#include "rtx_context.h"
#include "rtx_options.h"

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
  initializeFSR(nullptr);
}

DxvkFSR::~DxvkFSR() {
  onDestroy();
}

void DxvkFSR::onDestroy() {
  if (mFsrContext.contextDescription.callbacks.fpDestroy) {
    mFsrContext.contextDescription.callbacks.fpDestroy(&mFsrContext);
  }
  if (mFrameGenContext.contextDescription.callbacks.fpDestroy) {
    mFrameGenContext.contextDescription.callbacks.fpDestroy(&mFrameGenContext);
  }
}

void DxvkFSR::release() {
  mRecreate = true;
  onDestroy();
}

bool DxvkFSR::supportsFSR() const {
  return true; // FSR is supported on all hardware
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
    // For > 4k (e.g. 8k)
    desiredProfile = FSRProfile::UltraPerf;
  }

  if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Medium) {
    // When using medium preset, bias FSR more towards performance
    desiredProfile = (FSRProfile)std::max(0, (int)desiredProfile - 1);
  } else if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Low) {
    // When using low preset, give me all the perf I can get!!!
    desiredProfile = FSRProfile::UltraPerf;
  }

  return desiredProfile;
}

void DxvkFSR::setSetting(const uint32_t displaySize[2], const FSRProfile profile, uint32_t outRenderSize[2]) {
  ScopedCpuProfileZone();
  
  // Handle the "auto" case
  FSRProfile actualProfile = profile;
  if (actualProfile == FSRProfile::Auto) {
    actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
  }

  if (mActualProfile == actualProfile && displaySize[0] == mFSROutputSize[0] && displaySize[1] == mFSROutputSize[1]) {
    // Nothing changed that would alter FSR resolution(s), so return the last cached optimal render size
    outRenderSize[0] = mInputSize[0];
    outRenderSize[1] = mInputSize[1];
    return;
  }

  mActualProfile = actualProfile;
  mRecreate = true;
  mProfile = profile;

  if (mProfile == FSRProfile::FullResolution) {
    mInputSize[0] = outRenderSize[0] = displaySize[0];
    mInputSize[1] = outRenderSize[1] = displaySize[1];
  } else {
    float scale;
    switch (actualProfile) {
      case FSRProfile::UltraPerf: scale = 0.33f; break;
      case FSRProfile::MaxPerf: scale = 0.5f; break;
      case FSRProfile::Balanced: scale = 0.66f; break;
      case FSRProfile::MaxQuality: scale = 0.75f; break;
      default: scale = 1.0f; break;
    }

    // Ensure dimensions are multiples of 32 for optimal performance
    const int step = 32;
    mInputSize[0] = outRenderSize[0] = ((uint32_t)(displaySize[0] * scale + step - 1) / step) * step;
    mInputSize[1] = outRenderSize[1] = ((uint32_t)(displaySize[1] * scale + step - 1) / step) * step;
  }

  mFSROutputSize[0] = displaySize[0];
  mFSROutputSize[1] = displaySize[1];
}

void DxvkFSR::initializeFSR(Rc<DxvkContext> pRenderContext) {
  ScopedCpuProfileZone();

  // Initialize FSR context
  FfxUpscaleContextDescription fsrDesc = {};
  fsrDesc.flags = FFX_UPSCALE_ENABLE_NATIVE_AA | FFX_UPSCALE_ENABLE_SHARPENING;
  fsrDesc.maxRenderSize[0] = mInputSize[0];
  fsrDesc.maxRenderSize[1] = mInputSize[1];
  fsrDesc.displaySize[0] = mFSROutputSize[0];
  fsrDesc.displaySize[1] = mFSROutputSize[1];
  fsrDesc.fpMessage = nullptr;  // Optional message callback

  // Create FSR context
  FfxErrorCode fsrError = ffxUpscaleContextCreate(&mFsrContext, &fsrDesc);
  if (fsrError != FFX_OK) {
    Logger::err("Failed to create FSR context");
    return;
  }

  // Initialize Frame Generation if enabled
  if (RtxOptions::Get()->enableFrameGeneration()) {
    FfxFrameGenerationContextDescription frameGenDesc = {};
    frameGenDesc.flags = FFX_FRAMEGENERATION_ENABLE_INTERPOLATION;
    frameGenDesc.maxRenderSize[0] = mFSROutputSize[0];
    frameGenDesc.maxRenderSize[1] = mFSROutputSize[1];
    frameGenDesc.displaySize[0] = mFSROutputSize[0];
    frameGenDesc.displaySize[1] = mFSROutputSize[1];
    frameGenDesc.fpMessage = nullptr;

    FfxErrorCode frameGenError = ffxFrameGenerationContextCreate(&mFrameGenContext, &frameGenDesc);
    if (frameGenError == FFX_OK) {
      mFrameGenEnabled = true;
    } else {
      Logger::err("Failed to create Frame Generation context");
      mFrameGenEnabled = false;
    }
  }
}

void DxvkFSR::dispatch(
  Rc<RtxContext> ctx,
  DxvkBarrierSet& barriers,
  const Resources::RaytracingOutput& rtOutput,
  bool resetHistory) {
  ScopedGpuProfileZone(ctx, "FSR");

  if (mRecreate) {
    initializeFSR(ctx);
    mRecreate = false;
  }

  // Set up FSR dispatch parameters
  FfxUpscaleDispatchDescription dispatchParams = {};
  dispatchParams.commandList = ctx->getCommandList()->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
  dispatchParams.color = rtOutput.m_compositeOutput.image(Resources::AccessType::Read);
  dispatchParams.depth = rtOutput.m_primaryDepth.image;
  dispatchParams.motionVectors = rtOutput.m_primaryScreenSpaceMotionVector.image;
  dispatchParams.output = rtOutput.m_finalOutput.image;
  dispatchParams.jitterOffset[0] = 0.0f;  // Get from camera if jittering is used
  dispatchParams.jitterOffset[1] = 0.0f;
  dispatchParams.reset = resetHistory;

  // Dispatch FSR
  FfxErrorCode fsrError = ffxUpscaleContextDispatch(&mFsrContext, &dispatchParams);
  if (fsrError != FFX_OK) {
    Logger::err("Failed to dispatch FSR");
    return;
  }

  // Handle Frame Generation if enabled
  if (mFrameGenEnabled) {
    FfxFrameGenerationDispatchDescription frameGenParams = {};
    frameGenParams.commandList = ctx->getCommandList()->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    frameGenParams.color = rtOutput.m_finalOutput.image;
    frameGenParams.depth = rtOutput.m_primaryDepth.image;
    frameGenParams.motionVectors = rtOutput.m_primaryScreenSpaceMotionVector.image;
    frameGenParams.exposure = rtOutput.m_exposure.image;
    frameGenParams.output = rtOutput.m_finalOutput.image;
    frameGenParams.reset = resetHistory;

    FfxErrorCode frameGenError = ffxFrameGenerationContextDispatch(&mFrameGenContext, &frameGenParams);
    if (frameGenError != FFX_OK) {
      Logger::err("Failed to dispatch Frame Generation");
    }
  }
}

void DxvkFSR::showImguiSettings() {
  if (ImGui::CollapsingHeader("FSR Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();

    bool frameGen = RtxOptions::Get()->enableFrameGeneration();
    if (ImGui::Checkbox("Enable Frame Generation", &frameGen)) {
      RtxOptions::Get()->enableFrameGenerationRef() = frameGen;
      mRecreate = true;
    }

    float sharpness = RtxOptions::Get()->fsrSharpness();
    if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f)) {
      RtxOptions::Get()->fsrSharpnessRef() = sharpness;
    }

    ImGui::Unindent();
  }
}

} // namespace dxvk 