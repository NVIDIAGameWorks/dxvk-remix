/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_option.h"
#include "rtx_resources.h"
#include "rtx_common_object.h"
#include "rtx/pass/sparse_rendering/sparse_rendering.h"

#include <memory>

namespace dxvk {

  class RtxContext;
  class DxvkDevice;
  class DxvkPipelineManager;

  class SparseRendering : public CommonDeviceObject, public RtxPass {
  public:

    struct Options {
      RTX_OPTION_ARGS("rtx.sparseRendering", bool, enableSparseRendering, false,
        "Enables sparse rendering. When enabled, applies a constant per-pixel sampling rate across the screen. DLSS Ray Reconstruction is required.",
            args.environment = "RTX_SPARSE_RENDERING_ENABLE");

      RTX_OPTION_ENV("rtx.sparseRendering", PerPixelRateNoiseSource, perPixelRateNoiseSource, PerPixelRateNoiseSource::BlueNoise128x128x64x8, "RTX_SPARSE_RENDERING_PER_PIXEL_RATE_NOISE_SOURCE",
        "Selects the noise source used to threshold per-pixel sampling rates.\n"
        "WhiteNoise (0): wangHash ~ white-noise.\n"
        "BlueNoise128x128x64x8 (1): 8 bit 128x128 blue-noise 64 frame length.");

      RTX_OPTION_ARGS("rtx.sparseRendering", float, directLightingSamplingRate, 1.0f,
        "Per-pixel sampling rate for primary direct lighting (RTXDI + integrate_direct).\n"
        "Lower values boost performance at the cost of image detail and temporal stability.\n"
        "Range [0.0625, 1.0]. Rates below ~0.2 tend to look bad and yield diminishing perf returns.",
        args.environment = "RTX_SPARSE_RENDERING_DIRECT_LIGHTING_SAMPLING_RATE",
        // 1/255 is the absolute minimum, but 1/16 is a more reasonable lower bound for image quality. Even 1/16 will show temporal instabilities but tends to not be catastrophic.
        // Second, there are diminishing returns on performance with lower rates so aggressive rates don't provide much more performance but can cause very bad image quality.
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f);

      RTX_OPTION_ARGS("rtx.sparseRendering", float, indirectLightingSamplingRate, 1.0f,
        "Per-pixel sampling rate for primary indirect lighting (integrate_indirect, integrate_nee, NRC resolve).\n"
        "Lower values boost performance at the cost of image detail and temporal stability.\n"
        "Range [0.0625, 1.0]. Rates below ~0.2 tend to look bad and yield diminishing perf returns.",
        args.environment = "RTX_SPARSE_RENDERING_INDIRECT_LIGHTING_SAMPLING_RATE",
        // 1/255 is the absolute minimum, but 1/16 is a more reasonable lower bound for image quality. Even 1/16 will show temporal instabilities but has been tested to not be not catastrophic.
        // Second, there are diminishing returns on performance with lower rates so aggressive rates don't provide much more performance but can cause very bad image quality.
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f);

      RTX_OPTION("rtx.sparseRendering", bool, enableSparsePrimaryRayMissComposition, false,
        "When enabled, primary miss pixels (sky) use sparse rendering.\n"
        "This improves performance at the cost of sky reconstruction artifacts.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseSecondaryLighting, false,
        "When enabled, secondary surfaces (PSR glass reflections) use sparse rendering.\n"
        "This improves performance at the cost of potential sparse NRD denoising artifacts on glass.\n"
        "This option is in development and may not produce good denoised results.");

      RTX_OPTION("rtx.sparseRendering", bool, forceNrcTrainingPixelsActive, true,
        "When enabled, pixels that correspond to NRC training paths are forced active in sparse rendering.");

      RTX_OPTION("rtx.sparseRendering", bool, enableRtxdiReuseForInactivePixels, false,
        "Enables RTXDI reservoir reuse (temporal reprojection and spatial reuse) on inactive pixels.\n"
        "Disabling improves performance and, counterintuitively, has been observed to reduce ghosting."
        "In theory it can increase direct lighting noise under animated lighting.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryHit, true,
        "When enabled, volumetric NEE integration at primary-hit pixels uses sparse rendering.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryMiss, false,
        "When enabled, volumetric NEE integration at primary-miss (sky) pixels uses sparse rendering.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparsePrimarySpecularAlbedo, false,
        "When enabled, the primary specular albedo guide for DLSS Ray Reconstruction is written sparsely: active pixels get\n"
        "the computed specular albedo, inactive primary-hit pixels are zeroed. When disabled (default), the guide is computed\n"
        "and written densely for every primary-hit pixel so RR sees a clean dense signal.");
    };

    SparseRendering(dxvk::DxvkDevice* device);

    // Config-only mirror of isEnabled(): true iff the RTX options required for sparse rendering
    // (SR option, DLSS-RR, NRC indirect mode) are configured to be enabled. Safe to call at
    // startup before NRC/RR have finished initialising. Use this for shader prewarming;
    // use isActive() for per-frame dispatch decisions.
    static bool isEnabledByOptions();

    void showImguiSettings();
    void dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void setSparseRenderingArgs(RtxContext& ctx, SparseRenderingArgs& args) const;
    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

  protected:
    bool isEnabled() const override;
    bool onActivation(Rc<DxvkContext>& ctx) override;
    void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;
    void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) override;
    void releaseDownscaledResource() override;

  private:
    void dispatchActivePixelSamplingRate(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchActivePixelMask(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchCompactActivePixels(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    bool checkCompactActivePixelsRequirements() const;

    VkExtent3D m_activePixelMaskExtent = { 0u, 0u, 0u };
  };

} // namespace dxvk
