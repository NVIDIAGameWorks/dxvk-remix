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
#include <locale>
#include <codecvt>
#include <cassert>

#include "rtx.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx/pass/tonemap/tonemapping.h"
#include "rtx/pass/dlss/dlss.h"
#include "dxvk_device.h"
#include "rtx_dlss.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_shaders/prepare_dlss.h"
#include "../util/util_env.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"

#include "rtx_matrix_helpers.h"

#include "rtx_ngx_wrapper.h"

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

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class PrepareDLSSShader : public ManagedShader
    {
      SHADER_SOURCE(PrepareDLSSShader, VK_SHADER_STAGE_COMPUTE_BIT, prepare_dlss)

      BEGIN_PARAMETER()
        TEXTURE2D(DLSS_NORMALS_INPUT)
        TEXTURE2D(DLSS_VIRTUAL_NORMALS_INPUT)
        RW_TEXTURE2D(DLSS_NORMALS_OUTPUT)
        CONSTANT_BUFFER(DLSS_CONSTANTS)
      END_PARAMETER()
    };
  }

  DxvkDLSS::DxvkDLSS(DxvkDevice* device) : CommonDeviceObject(device) {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(DLSSArgs);
    m_constants = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer);
  }

  DxvkDLSS::~DxvkDLSS() {
    NGXWrapper::releaseInstance();
  }

  NVSDK_NGX_PerfQuality_Value profileToQuality(DLSSProfile profile) {
    NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_Balanced;
    switch (profile)
    {
    case DLSSProfile::UltraPerf: perfQuality = NVSDK_NGX_PerfQuality_Value_UltraPerformance; break;
    case DLSSProfile::MaxPerf: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf; break;
    case DLSSProfile::Balanced: perfQuality = NVSDK_NGX_PerfQuality_Value_Balanced; break;
    case DLSSProfile::MaxQuality: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break;
    case DLSSProfile::FullResolution: perfQuality = NVSDK_NGX_PerfQuality_Value_MaxQuality; break; // Need to set MaxQ as some modes dont support full res
    }
    return perfQuality;
  }

  bool DxvkDLSS::supportsDLSS() const {
    return NGXWrapper::getInstance(m_device)->supportsDLSS();
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

    if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Medium) {
      // When using medium preset, bias DLSS more towards performance
      desiredProfile = (DLSSProfile)std::max(0, (int) desiredProfile - 1);
    } else if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Low) {
      // When using low preset, give me all the perf I can get!!!
      desiredProfile = DLSSProfile::UltraPerf;
    }

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

    if (mProfile == DLSSProfile::FullResolution) {
      mInputSize[0] = outRenderSize[0] = displaySize[0];
      mInputSize[1] = outRenderSize[1] = displaySize[1];
    } else {
      NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mActualProfile);

      auto optimalSettings = NGXWrapper::getInstance(m_device)->queryOptimalSettings(displaySize, perfQuality);
      mInputSize[0] = outRenderSize[0] = optimalSettings.optimalRenderSize[0];
      mInputSize[1] = outRenderSize[1] = optimalSettings.optimalRenderSize[1];
    }

    mDLSSOutputSize[0] = displaySize[0];
    mDLSSOutputSize[1] = displaySize[1];
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
    Rc<DxvkCommandList> cmdList,
    Rc<RtxContext> ctx,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    bool resetHistory)
  {
    ScopedGpuProfileZone(ctx, "DLSS");

    if (!mEnabled)
      return;

    bool dlssAutoExposure = useDlssAutoExposure();
    mRecreate |= (mAutoExposure != dlssAutoExposure);
    mAutoExposure = dlssAutoExposure;

    if (mRecreate) {
      initializeDLSS(ctx, cmdList);
      mRecreate = false;
    }

    SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    {
      // The DLSS y coordinate is pointing down
      float jitterOffset[2];
      sceneManager.getCamera().getJittering(jitterOffset);
      mMotionVectorScale = MotionVectorScale::Absolute;

      float motionVectorScale[2] = { 1.f,1.f };

      std::vector<Rc<DxvkImageView>> pInputs = {
        rtOutput.m_compositeOutput.view(Resources::AccessType::Read),
        rtOutput.m_primaryScreenSpaceMotionVector.view,
        rtOutput.m_primaryDepth.view,
        rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view,
        rtOutput.m_primaryAlbedo.view,
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Read)
      };

      const DxvkAutoExposure& autoExposure = device()->getCommon()->metaAutoExposure();
      if (!mAutoExposure)
        pInputs.push_back(autoExposure.getExposureTexture().view);

      std::vector<Rc<DxvkImageView>> pOutputs = {
        rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Read),
        rtOutput.m_finalOutput.view
      };

      for (auto input : pInputs) {
        if (input == nullptr)
          continue;
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

      barriers.recordCommands(cmdList);

      auto motionVectorInput = &rtOutput.m_primaryScreenSpaceMotionVector;
      auto depthInput = &rtOutput.m_primaryDepth;
      auto normalsInput = nullptr;
      // Note: Texture contains specular albedo in this case as DLSS happens after demodulation
      auto specularAlbedoInput = &rtOutput.m_primarySpecularAlbedo.resource(Resources::AccessType::Read);

      // Note: Add texture inputs added here to the pInputs array above to properly access the images.
      NGXWrapper::getInstance(m_device)->evaluateDLSS(cmdList,
                                  ctx,
                                  &rtOutput.m_compositeOutput.resource(Resources::AccessType::Read),  // pUnresolvedColor
                                  &rtOutput.m_finalOutput,                                            // pResolvedColor
                                  motionVectorInput,                                                  // pMotionVectors
                                  depthInput,                                                         // pDepth
                                  &rtOutput.m_primaryAlbedo,                                          // pDiffuseAlbedo
                                  specularAlbedoInput,                                                // pSpecularAlbedo
                                  &autoExposure.getExposureTexture(),                                 // pExposure
                                  &rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal(),      // pPosition
                                  normalsInput,                                                       // pNormals
                                  &rtOutput.m_primaryPerceptualRoughness,                             // pRoughness
                                  &rtOutput.m_sharedBiasCurrentColorMask.resource(Resources::AccessType::Read),// pBiasCurrentColorMask
                                  resetHistory,
                                  0.f,
                                  mBiasCurrentColorEnabled,
                                  mPreExposure,
                                  jitterOffset,
                                  motionVectorScale,
                                  mAutoExposure);

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

        cmdList->trackResource<DxvkAccess::None>(output);
        cmdList->trackResource<DxvkAccess::Write>(output->image());
      }
      barriers.recordCommands(cmdList);
    }
  }

  void DxvkDLSS::showImguiSettings() {
    ImGui::Checkbox("Anti-Ghost", &mBiasCurrentColorEnabled);
  }

  void DxvkDLSS::initializeDLSS(Rc<DxvkContext> renderContext, Rc<DxvkCommandList> cmdList) {
    NGXWrapper* dlssWrapper = NGXWrapper::getInstance(m_device);
    dlssWrapper->releaseDLSS();

    NVSDK_NGX_PerfQuality_Value perfQuality = profileToQuality(mProfile);

    auto optimalSettings = dlssWrapper->queryOptimalSettings(mInputSize, perfQuality);

    dlssWrapper->initializeDLSS(renderContext, cmdList, mInputSize, mDLSSOutputSize, mIsHDR, mInverseDepth, mAutoExposure, false, perfQuality);
  }
}
