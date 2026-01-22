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
#include "rtx/pass/tonemap/tonemapping.h"
#include "dxvk_device.h"
#include "rtx_dlss.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_ngx_wrapper.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"

#include "rtx_matrix_helpers.h"


namespace dxvk {
  const char* dlssProfileToString(DLSSProfile dlssProfile) {
    switch (dlssProfile) {
    case DLSSProfile::UltraPerf: return "Ultra Performance";
    case DLSSProfile::MaxPerf: return "Max Performance";
    case DLSSProfile::Balanced: return "Balanced";
    case DLSSProfile::MaxQuality: return "Max Quality";
    case DLSSProfile::Auto: return "Auto";
    case DLSSProfile::FullResolution: return "Full Resolution";
    default:
      assert(false);
    case DLSSProfile::Invalid: return "Invalid";
    }
  }

  DxvkDLSS::DxvkDLSS(DxvkDevice* device) : CommonDeviceObject(device), RtxPass(device) {
    // Trigger DLSS context creation
    if (!m_dlssContext) {
      m_dlssContext = device->getCommon()->metaNGXContext().createDLSSContext();
    };
  }

  DxvkDLSS::~DxvkDLSS() { }

  void DxvkDLSS::onDestroy() {
    if (m_dlssContext) {
      m_dlssContext->releaseNGXFeature();
    }
    m_dlssContext = nullptr;
  }

  void DxvkDLSS::release() {
    mRecreate = true;

    if (m_dlssContext) {
      m_dlssContext->releaseNGXFeature();
    }
  }

  NVSDK_NGX_PerfQuality_Value DxvkDLSS::profileToQuality(DLSSProfile profile) {
    NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_Balanced;
    switch (profile)
    {
    case DLSSProfile::UltraPerf: perfQuality = NVSDK_NGX_PerfQuality_Value_UltraPerformance; break;
    case DLSSProfile::MaxPerf: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf; break;
    case DLSSProfile::Balanced: perfQuality = NVSDK_NGX_PerfQuality_Value_Balanced; break;
    case DLSSProfile::MaxQuality: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break;
    case DLSSProfile::FullResolution: perfQuality = NVSDK_NGX_PerfQuality_Value_DLAA; break;
    case DLSSProfile::Auto: assert(false && "DLSSProfile::Auto passed to DxvkDLSS::profileToQuality without being resolved first"); break;
    case DLSSProfile::Invalid: assert(false && "DLSSProfile::Invalid passed to DxvkDLSS::profileToQuality"); break;
    }
    return perfQuality;
  }

  bool DxvkDLSS::supportsDLSS() const {
    return m_device->getCommon()->metaNGXContext().supportsDLSS();
  }

  bool DxvkDLSS::isEnabled() const {
      return RtxOptions::isDLSSOrRayReconstructionEnabled();
  }

  DLSSProfile DxvkDLSS::getAutoProfile(uint32_t displayWidth, uint32_t displayHeight) {
    DLSSProfile desiredProfile = DLSSProfile::UltraPerf;

    // Standard display resolution based DLSS config
    if (displayHeight <= 1080) {
      desiredProfile = DLSSProfile::MaxQuality;
    } else if (displayHeight < 2160) {
      desiredProfile = DLSSProfile::Balanced;
    } else if (displayHeight < 4320) {
      desiredProfile = DLSSProfile::MaxPerf;
    } else {
      // For > 4k (e.g. 8k)
      desiredProfile = DLSSProfile::UltraPerf;
    }

    if (RtxOptions::graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias DLSS more towards performance
      desiredProfile = (DLSSProfile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = (DLSSProfile) std::max(0, (int) desiredProfile - 2);
    }

    // Note: Ensure the resulting desired profile has been resolved to something non-auto.
    assert(desiredProfile != DLSSProfile::Auto);

    return desiredProfile;
  }

  void DxvkDLSS::setSetting(const uint32_t displaySize[2], const DLSSProfile profile, uint32_t outRenderSize[2]) {
    ScopedCpuProfileZone();
    // Handle the "auto" case, this is the param we'll pass to determine optimal resolution setting
    DLSSProfile actualProfile = profile;
    if (actualProfile == DLSSProfile::Auto) {
      actualProfile = getAutoProfile(displaySize[0], displaySize[1]);
    }

    if (mActualProfile == actualProfile && displaySize[0] == mDLSSOutputSize[0] && displaySize[1] == mDLSSOutputSize[1]) {
      // Nothing changed that would alter DLSS resolution(s), so return the last cached optimal render size
      outRenderSize[0] = mInputSize[0];
      outRenderSize[1] = mInputSize[1];
      return;
    }
    
    mActualProfile = actualProfile;

    // We need to force a recreation of resources before running DLSS.
    mRecreate = true;

    // Update our requested profile
    mProfile = profile;

    const NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);
    if (!m_dlssContext) {
      m_dlssContext = m_device->getCommon()->metaNGXContext().createDLSSContext();
    }
    const auto optimalSettings = m_dlssContext->queryOptimalSettings(displaySize, perfQuality);

    mInputSize[0] = outRenderSize[0] = optimalSettings.optimalRenderSize[0];
    mInputSize[1] = outRenderSize[1] = optimalSettings.optimalRenderSize[1];

    mDLSSOutputSize[0] = displaySize[0];
    mDLSSOutputSize[1] = displaySize[1];

    // Note: Input size used for DLSS must be less than or equal to the desired output size. This is a requirement of the DLSS API currently.
    assert(mInputSize[0] <= mDLSSOutputSize[0] && mInputSize[1] <= mDLSSOutputSize[1]);
  }

  DLSSProfile DxvkDLSS::getCurrentProfile() const {
    return mActualProfile;
  }

  void DxvkDLSS::getInputSize(uint32_t& width, uint32_t& height) const {
    width = mInputSize[0];
    height = mInputSize[1];
  }

  void DxvkDLSS::getOutputSize(uint32_t& width, uint32_t& height) const {
    width = mDLSSOutputSize[0];
    height = mDLSSOutputSize[1];
  }

  bool DxvkDLSS::useDlssAutoExposure() const {
    // Force internal auto-exposure when external exposure is not available
    const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
    if (autoExposure.enabled() && autoExposure.getExposureTexture().image != nullptr) {
      return false;
    }

    return true;
  }

  void DxvkDLSS::dispatch(
    Rc<RtxContext> ctx,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    bool resetHistory)
  {
    ScopedGpuProfileZone(ctx, "DLSS");
    ctx->setFramePassStage(RtxFramePassStage::DLSS);

    bool dlssAutoExposure = useDlssAutoExposure();
    mRecreate |= (mAutoExposure != dlssAutoExposure);
    mAutoExposure = dlssAutoExposure;

    if (mRecreate) {
      initializeDLSS(ctx);
      mRecreate = false;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    {
      // Hack to bypass ownership check for aliased resources
      rtOutput.m_rayReconstructionHitDistance.view(Resources::AccessType::Write);
    }

    {
      // The DLSS y coordinate is pointing down
      float jitterOffset[2];
      RtCamera& camera = sceneManager.getCamera();
      camera.getJittering(jitterOffset);
      mMotionVectorScale = MotionVectorScale::Absolute;

      float motionVectorScale[2] = { 1.f,1.f };

      std::vector<Rc<DxvkImageView>> pInputs = {
        rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
        rtOutput.m_primaryScreenSpaceMotionVector.view,
        rtOutput.m_primaryDepth.view,
        rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read),
        rtOutput.m_primaryAlbedo.view,
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Read)
      };

      const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
      if (!mAutoExposure) {
        pInputs.push_back(autoExposure.getExposureTexture().view);
      }

      std::vector<Rc<DxvkImageView>> pOutputs = {
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Write),
        rtOutput.m_finalOutput.view(Resources::AccessType::Write)
      };

      for (auto input : pInputs) {
        if (input == nullptr) {
          continue;
        }
        
        barriers.accessImage(
          input->image(),
          input->imageSubresources(),
          input->imageInfo().layout,
          input->imageInfo().stages,
          input->imageInfo().access,
          input->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_READ_BIT);
      }

      for (auto output : pOutputs) {
        barriers.accessImage(
          output->image(),
          output->imageSubresources(),
          output->imageInfo().layout,
          output->imageInfo().stages,
          output->imageInfo().access,
          output->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT);
      }

      barriers.recordCommands(ctx->getCommandList());

      auto motionVectorInput = &rtOutput.m_primaryScreenSpaceMotionVector;
      auto depthInput = &rtOutput.m_primaryDepth;
      // Note: Texture contains specular albedo in this case as DLSS happens after demodulation
      auto specularAlbedoInput = &rtOutput.m_primarySpecularAlbedo.resource(Resources::AccessType::Read);
      m_dlssContext->setWorldToViewMatrix(camera.getWorldToView());
      m_dlssContext->setViewToProjectionMatrix(camera.getViewToProjection());

      // Note: Add texture inputs added here to the pInputs array above to properly access the images.
      NGXDLSSContext::NGXBuffers buffers;
      buffers.pUnresolvedColor = &rtOutput.m_compositeOutput.resource(Resources::AccessType::Read);
      buffers.pResolvedColor = &rtOutput.m_finalOutput.resource(Resources::AccessType::Read);
      buffers.pMotionVectors = motionVectorInput;
      buffers.pDepth = depthInput;
      buffers.pExposure = &autoExposure.getExposureTexture();
      buffers.pBiasCurrentColorMask = &rtOutput.m_sharedBiasCurrentColorMask.resource(Resources::AccessType::Read);

      NGXDLSSContext::NGXSettings settings;
      settings.resetAccumulation = resetHistory;
      settings.antiGhost = mBiasCurrentColorEnabled;
      settings.preExposure = mPreExposure;
      settings.jitterOffset[0] = jitterOffset[0];
      settings.jitterOffset[1] = jitterOffset[1];
      settings.motionVectorScale[0] = motionVectorScale[0];
      settings.motionVectorScale[1] = motionVectorScale[1];

      m_dlssContext->evaluateDLSS(ctx, buffers, settings);

      for (auto output : pOutputs) {
        barriers.accessImage(
          output->image(),
          output->imageSubresources(),
          output->imageInfo().layout,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_SHADER_WRITE_BIT,
          output->imageInfo().layout,
          output->imageInfo().stages,
          output->imageInfo().access);

        ctx->getCommandList()->trackResource<DxvkAccess::None>(output);
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(output->image());
      }
      barriers.recordCommands(ctx->getCommandList());
    }
  }

  void DxvkDLSS::showImguiSettings() {
    RemixGui::Checkbox("Anti-Ghost", &mBiasCurrentColorEnabled);
  }

  void DxvkDLSS::initializeDLSS(Rc<DxvkContext> renderContext) {
    // Toggling eye adaptation may cause DLSS get reinitialized while last frame is still executing.
    // Use waitForIdle() to prevent racing conditions.
    m_device->waitForIdle();

    if (!m_dlssContext) {
      m_dlssContext = m_device->getCommon()->metaNGXContext().createDLSSContext();
    }
    m_dlssContext->releaseNGXFeature();

    // Note: Use "actual profile" here not the set profile as this value should have any auto profiles resolved to an actual DLSS profile which is
    // required for initializing DLSS.
    const NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);

    m_dlssContext->initialize(renderContext, mInputSize, mDLSSOutputSize, mIsHDR, mInverseDepth, mAutoExposure, false, perfQuality);
  }
}
