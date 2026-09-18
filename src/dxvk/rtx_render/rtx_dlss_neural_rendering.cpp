/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include <vector>

#include "../../util/log/log.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_dlss_neural_rendering.h"
#include "rtx_imgui.h"
#include "rtx_ngx_wrapper.h"

namespace dxvk {

  DlssNeuralRendering::DlssNeuralRendering(DxvkDevice* device)
    : DxvkDLSS(device) {
  }

  bool DlssNeuralRendering::supportsDlssNeuralRendering() const {
    return m_device->getCommon()->metaNGXContext().supportsDlssNeuralRendering();
  }

  void DlssNeuralRendering::release() {
    m_dlssNeuralRenderingContext = {};
    mRecreate = true;
  }

  void DlssNeuralRendering::onDestroy() {
    release();
  }

  bool DlssNeuralRendering::useDlssNeuralRendering() const {
    return isActive();
  }

  bool DlssNeuralRendering::isEnabled() const {
    const NGXContext& ngxContext = m_device->getCommon()->metaNGXContext();
    return DlssNeuralRendering::enable() &&
      (!ngxContext.isDlssNeuralRenderingSupportChecked() || ngxContext.supportsDlssNeuralRendering());
  }

  bool DlssNeuralRendering::onActivation(Rc<DxvkContext>& ctx) {
    const bool initialized = initializeDlssNeuralRendering(ctx);
    mRecreate = !initialized;
    if (!initialized) {
      disableAfterFailure("DLSS-NR activation failed; disabling DLSS-NR.");
    }
    return initialized;
  }

  void DlssNeuralRendering::onDeactivation() {
    if (m_dlssNeuralRenderingContext &&
        m_dlssNeuralRenderingContext->isNeuralRenderingInitialized()) {
      m_device->waitForIdle();
    }
    release();
  }

  bool DlssNeuralRendering::dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory,
      bool useRayReconstructionGuides) {
    ScopedGpuProfileZone(ctx, "DlssNeuralRendering");
    ctx->setFramePassStage(RtxFramePassStage::DLSSNR);

    if (!useDlssNeuralRendering()) {
      return false;
    }

    if (mRecreate) {
      const bool initialized = initializeDlssNeuralRendering(ctx);
      mRecreate = !initialized;
      if (!initialized) {
        disableAfterFailure("DLSS-NR feature recreation failed; disabling DLSS-NR.");
        return false;
      }
    }

    if (!m_dlssNeuralRenderingContext ||
        !m_dlssNeuralRenderingContext->isNeuralRenderingInitialized()) {
      disableAfterFailure("DLSS-NR context is not initialized; disabling DLSS-NR.");
      return false;
    }

    const Resources::Resource* pMotionVectorInput = useRayReconstructionGuides
      ? &rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR
      : &rtOutput.m_primaryScreenSpaceMotionVector;
    const Resources::Resource* pDepthInput = useRayReconstructionGuides
      ? &rtOutput.m_primaryDepthDLSSRR.resource(Resources::AccessType::Read)
      : &rtOutput.m_primaryDepth;
    {
      float jitterOffset[2];
      device()->getCommon()->getSceneManager().getCamera().getJittering(jitterOffset);

      // Screen-space motion vectors are expressed in render-pixel units.
      float motionVectorScale[2] = { 1.f, 1.f };

      std::vector<Rc<DxvkImageView>> pInputs = {
        rtOutput.m_neuralRenderingInput.view(Resources::AccessType::Read),
        pMotionVectorInput->view,
        pDepthInput->view,
        rtOutput.m_controlMask.view,
      };

      std::vector<Rc<DxvkImageView>> pOutputs = {
        rtOutput.m_neuralRenderingOutput.view(Resources::AccessType::Write)
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

#ifdef REMIX_DEVELOPMENT
        ctx->cacheResourceAliasingImageView(input);
#endif
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

#ifdef REMIX_DEVELOPMENT
        ctx->cacheResourceAliasingImageView(output);
#endif
      }

      barriers.recordCommands(ctx->getCommandList());

      // Note: Add texture inputs added here to the pInputs array above to properly access the images.
      NGXNeuralRenderingContext::NGXNeuralRenderingBuffers buffers;
      buffers.pInColor = &rtOutput.m_neuralRenderingInput.resource(Resources::AccessType::Read);
      buffers.pOutColor = &rtOutput.m_neuralRenderingOutput.resource(Resources::AccessType::Write);
      buffers.pMotionVectors = pMotionVectorInput;
      buffers.pDepth = pDepthInput;
      buffers.pControlMask = &rtOutput.m_controlMask;

      NGXNeuralRenderingContext::NGXNeuralRenderingSettings settings = {};
      settings.jitterOffset[0] = jitterOffset[0];
      settings.jitterOffset[1] = jitterOffset[1];
      settings.motionVectorScale[0] = motionVectorScale[0];
      settings.motionVectorScale[1] = motionVectorScale[1];
      settings.resetAccumulation = resetHistory;
      settings.intensity             = DlssNeuralRendering::intensity();
      settings.toneStrength          = DlssNeuralRendering::toneStrength();
      settings.structuralStrength    = DlssNeuralRendering::structuralStrength();
      settings.model                 = static_cast<uint32_t>(static_cast<int>(DlssNeuralRendering::model()));
      settings.useAutoMask           = DlssNeuralRendering::useAutoMask();
      settings.skinStructureStrength = settings.useAutoMask
        ? DlssNeuralRendering::skinStructureStrength()
        : 0.0f;

      const bool evaluateSucceeded = m_dlssNeuralRenderingContext->evaluateNeuralRendering(ctx, buffers, settings);

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

      if (!evaluateSucceeded) {
        disableAfterFailure("DLSS-NR evaluation failed; disabling DLSS-NR.");
        return false;
      }
    }

    return true;
  }

  void DlssNeuralRendering::showDlssNeuralRenderingImguiSettings() {
    static auto modelCombo = RemixGui::ComboWithKey<Model>(
      "Model",
      { {
        { Model::Model0, "Model A" },
        { Model::Model1, "Model B" },
        { Model::Model2, "Model C" },
      } });

    RemixGui::Checkbox("DLSS 3D-Guided Neural Generation", &DlssNeuralRendering::enableObject());

    constexpr ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
    modelCombo.getKey(&DlssNeuralRendering::modelObject());
    RemixGui::DragFloat("Structure Intensity", &DlssNeuralRendering::structuralStrengthObject(), 0.01f, 0.0f, 1.0f, "%.3f", flags);
    RemixGui::DragFloat("Tone Intensity", &DlssNeuralRendering::toneStrengthObject(), 0.01f, 0.0f, 1.0f, "%.3f", flags);
    RemixGui::Checkbox("Enable Auto Mask", &DlssNeuralRendering::useAutoMaskObject());
    const bool autoMaskEnabled = DlssNeuralRendering::useAutoMask();
    ImGui::BeginDisabled(!autoMaskEnabled);
    RemixGui::DragFloat("Character Intensity", &DlssNeuralRendering::skinStructureStrengthObject(), 0.01f, 0.0f, 1.0f, "%.3f", flags);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (RemixGui::CollapsingHeader("Advanced Settings")) {
      ImGui::Indent();
      RemixGui::Checkbox("Highlight Recovery", &DlssNeuralRendering::enableHighlightRecoveryObject());
      // Thresholds: (exposed HDR max-channel min/max, LDR max-channel min/max)
      RemixGui::DragFloat4("Highlight Recovery Thresholds", &DlssNeuralRendering::highlightRecoveryThresholdsObject(), 0.01f, 0.0f, 32.0f, "%.3f", flags);
      ImGui::BeginDisabled(autoMaskEnabled);
      RemixGui::Checkbox("Volumetric Improvement", &DlssNeuralRendering::enableVolumetricControlMaskObject());
      ImGui::EndDisabled();
      ImGui::Unindent();
    }
  }

  void DlssNeuralRendering::setDlssNeuralRenderingSettings(const uint32_t displaySize[2]) {
    ScopedCpuProfileZone();

    m_displaySize[0] = displaySize[0];
    m_displaySize[1] = displaySize[1];

    mRecreate = true;
  }

  bool DlssNeuralRendering::initializeDlssNeuralRendering(Rc<DxvkContext> renderContext) {
    if (!m_dlssNeuralRenderingContext) {
      m_dlssNeuralRenderingContext = m_device->getCommon()->metaNGXContext().createDlssNeuralRenderingContext();
    }

    if (!m_dlssNeuralRenderingContext) {
      return false;
    }

    m_dlssNeuralRenderingContext->initialize(renderContext, m_displaySize);
    return m_dlssNeuralRenderingContext->isNeuralRenderingInitialized();
  }

  void DlssNeuralRendering::disableAfterFailure(const char* pReason) {
    Logger::err(pReason);
    mRecreate = false;
    DlssNeuralRendering::enable.setDeferred(false);
  }
} // namespace dxvk
