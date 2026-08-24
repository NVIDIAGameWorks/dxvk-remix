/*
* Copyright (c) 2024-2026, NVIDIA CORPORATION. All rights reserved.
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
  class NGXRayReconstructionContext;
  class DxvkPipelineManager;
  class DxvkRayReconstruction : public DxvkDLSS {
  public:
    enum class RayReconstructionParticleBufferMode : uint32_t {
      None,
      RayReconstructionUpscaling,
    };

    // Note: Values must match NVSDK_NGX_RayReconstruction_Hint_Render_Preset in nvsdk_ngx_defs_dlssd.h.
    // Presets A/B/C were removed in the SDK and G+ are unused, so only the valid presets are exposed here.
    enum class RayReconstructionPreset : uint32_t {
      Default = 0,
      D = 4,
      E = 5,
      F = 6,
    };

    explicit DxvkRayReconstruction(DxvkDevice* device);

    bool supportsRayReconstruction() const;

    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void showRayReconstructionImguiSettings(bool showAdvancedSettings);

    RayReconstructionParticleBufferMode getParticleBufferMode();

    bool useParticleBuffer() {
      return getParticleBufferMode() != RayReconstructionParticleBufferMode::None;
    }

    void dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory = false,
      float frameTimeMilliseconds = 16.0f);

    void release();

    bool useRayReconstruction() const;
    bool denoiseSecondarySignalWithExternalDenoiser() const;

    void setSettings(const uint32_t displaySize[2], const DLSSProfile profile, uint32_t outRenderSize[2]);

    virtual void onDestroy();

    RTX_OPTION("rtx.rayreconstruction", RayReconstructionParticleBufferMode, particleBufferMode,
               RayReconstructionParticleBufferMode::RayReconstructionUpscaling,
               "Use a separate particle buffer to handle particles.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableNRDForTraining, false, "Enable NRD. This option is only for training or debug purpose.\n");
    RTX_OPTION("rtx.rayreconstruction", PathTracerPreset, pathTracerPreset, PathTracerPreset::RayReconstruction, 
               "Path tracer preset to use when Ray Reconstruction is enabled.");
    RTX_OPTION("rtx.rayreconstruction", bool, useSpecularHitDistance, true, "Use specular hit distance to reduce ghosting.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, preserveSettingsInNativeMode, false, "Preserve settings when switched to native mode, otherwise the default preset will be applied.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, combineSpecularAlbedo, true, "Combine primary and secondary specular albedo to improve DLSS-RR reflection quality.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableDetailEnhancement, true, "Enable detail enhancement filter to enhance normal map details.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, demodulateRoughness, true, "Demodulate roughness to enhance roughness details.\n");
    RTX_OPTION("rtx.rayreconstruction", float, upscalerRoughnessDemodulationOffset, 1.5f, "Strength of upscaler roughness demodulation. Only used by DLSS-RR.");
    RTX_OPTION("rtx.rayreconstruction", float, upscalerRoughnessDemodulationMultiplier, 0.15f, "Multiplier of upscaler roughness demodulation to suppress noise. Only used by DLSS-RR.");
    RTX_OPTION("rtx.rayreconstruction", bool, demodulateAttenuation, true, "Demodulate attenuation to reduce ghosting when an object is behind textured translucent objects.\n");
    RTX_OPTION_ARGS("rtx.rayreconstruction", bool, preprocessSecondarySignal, true, "Denoise secondary signal before passing to DLSS-RR. This option improves reflection on translucent objects.\n",
                    args.environment = "RTX_RAY_RECONSTRUCTION_PREPROCESS_SECONDARY_SIGNAL");
    RTX_OPTION("rtx.rayreconstruction", bool, compositeVolumetricLight, true, "Composite volumetric light and then input the result to DLSS-RR, otherwise volumetric light is in a separate layer. Disabling it may introduce flickering artifacts.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableDisocclusionMaskBlur, false, "Enables blurring of disocclusion mask to suppress instabilities due to abrupt mask value changes.");
    RTX_OPTION("rtx.rayreconstruction", uint, disocclusionMaskBlurRadius, 32, "Pixel radius to use for blurring disocclusion mask.");
    RTX_OPTION("rtx.rayreconstruction", float, disocclusionMaskBlurNormalizedGaussianWeightSigma, 0.5f,
               "Normalized Gaussian weight sigma to use for blurring disocclusion mask.\n"
               "The sigma is applied to the normalized blur kernel radius extents (i.e. <0, 1>).");
      RTX_OPTION("rtx.rayreconstruction", bool, invalidateHistoryForAnimatedWater, false,
                 "Adds animated water surfaces to the disocclusion mask, making DLSS Ray Reconstruction discard their temporal history every frame.\n"
                 "This suppresses ghosting from the water's per-frame normal variation, but can introduce shimmering on objects seen through or near the water, so it is disabled by default.\n"
                 "Enable it for content whose animated water ghosts noticeably.");
    RTX_OPTION_ARGS("rtx.rayreconstruction", RayReconstructionPreset, preset, RayReconstructionPreset::Default,
                    "Render preset for Ray Reconstruction. Default = 0 (DLSS picks the best preset), D = 4, E = 5, F = 6.",
                    args.environment = "RTX_RAY_RECONSTRUCTION_PRESET",
                    args.flags = RtxOptionFlags::UserSetting);

  private:
    void initializeRayReconstruction(Rc<DxvkContext> pRenderContext);

    bool                        m_biasCurrentColorEnabled = true;
    RayReconstructionPreset     m_prevPreset;

    Rc<DxvkBuffer> m_constants;
    std::unique_ptr<NGXRayReconstructionContext> m_rayReconstructionContext;
  };
} // namespace dxvk
