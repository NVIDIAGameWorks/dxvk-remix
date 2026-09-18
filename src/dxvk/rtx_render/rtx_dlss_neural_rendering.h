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
#pragma once

#include "rtx_dlss.h"

namespace dxvk {

  class NGXNeuralRenderingContext;

  class DlssNeuralRendering : public DxvkDLSS {
  public:
    explicit DlssNeuralRendering(DxvkDevice* device);

    bool supportsDlssNeuralRendering() const;

    void showDlssNeuralRenderingImguiSettings();

    // Returns true when NGX produced a valid output image.
    bool dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory,
      bool useRayReconstructionGuides);

    void release();

    bool useDlssNeuralRendering() const;

    void setDlssNeuralRenderingSettings(const uint32_t displaySize[2]);

    virtual void onDestroy();

    enum class Model : int {
      Model0 = 0,
      Model1 = 1,
      Model2 = 2,
    };

    RTX_OPTION("rtx.dlssNeuralRendering", bool, enable, false, "Enable DLSS 3D-Guided Neural Generation.\n");

    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, intensity, 1.0f,
                    "Controls the overall DLSS 3D-Guided Neural Generation effect. Lower values reduce the effect; higher values increase it.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, structuralStrength, 0.7f,
                    "Controls DLSS 3D-Guided Neural Generation adjustments to details, shadows, and materials. Lower values preserve the original structure; higher values apply stronger adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, toneStrength, 0.3f,
                    "Controls DLSS 3D-Guided Neural Generation adjustments to lightness, darkness, and color. Lower values preserve the original tone and color; higher values apply stronger adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);
    RTX_OPTION("rtx.dlssNeuralRendering", Model, model, Model::Model0,
               "Selects among three DLSS 3D-Guided Neural Generation model variants. Their appearance is content-dependent; compare them in the target scene and choose the preferred result. 0: Model A, 1: Model B, 2: Model C.");
    RTX_OPTION("rtx.dlssNeuralRendering", bool,  useAutoMask, false, "Use the automatic character mask instead of the material control mask.");
    RTX_OPTION_ARGS("rtx.dlssNeuralRendering", float, skinStructureStrength, 0.5f,
                    "Controls DLSS 3D-Guided Neural Generation adjustments to characters when Auto Mask is enabled. Lower values preserve the original structure; higher values apply stronger structural adjustments.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    // Highlight recovery substitutes original pre-NR HDR for pixels that were very bright
    // and would land in the active tone mapper's shoulder.
    //   .x/.y = exposed HDR max-channel preserve range (default 1.0..8.0)
    //   .z/.w = exposed LDR max-channel shoulder range (default 0.75..0.99)
    RTX_OPTION("rtx.dlssNeuralRendering", bool,    enableHighlightRecovery, true, "Recover highlights compressed by DLSS 3D-Guided Neural Generation's LDR-domain processing.");
    RTX_OPTION("rtx.dlssNeuralRendering", Vector4, highlightRecoveryThresholds, Vector4(1.0f, 8.0f, 0.75f, 0.99f), "Smoothstep edges for highlight recovery: (exposed HDR max-channel min, exposed HDR max-channel max, LDR max-channel min, LDR max-channel max).");

    RTX_OPTION("rtx.dlssNeuralRendering", bool, enableVolumetricControlMask, true, "Modulate the DLSS 3D-Guided Neural Generation control mask with fog, volumetric, and alpha-blended transmittance. Unavailable while Auto Mask is enabled.");

  protected:
    bool isEnabled() const override;
    bool onActivation(Rc<DxvkContext>& ctx) override;
    void onDeactivation() override;

  private:
    bool initializeDlssNeuralRendering(Rc<DxvkContext> pRenderContext);
    void disableAfterFailure(const char* pReason);

    uint32_t m_displaySize[2] = {};

    std::unique_ptr<NGXNeuralRenderingContext> m_dlssNeuralRenderingContext;
  };
} // namespace dxvk
