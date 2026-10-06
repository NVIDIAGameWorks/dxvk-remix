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

      RTX_OPTION_ARGS("rtx.sparseRendering", float, samplingRate, 0.2f,
        "Per-pixel sampling rate applied across the whole pipeline.\n"
        "Lower values boost performance at the cost of image detail and temporal stability.\n"
        "Range [0.0625, 1.0]. Rates below ~0.2 tend to look bad and yield diminishing perf returns.\n"
        "Rates near 1.0 leave sparse rendering off, since the compacted GBuffer storage cannot hold that many active pixels.",
        args.environment = "RTX_SPARSE_RENDERING_SAMPLING_RATE",
        // 1/255 is the absolute minimum, but 1/16 is a more reasonable lower bound for image quality. Even 1/16 will show temporal instabilities but tends to not be catastrophic.
        // Second, there are diminishing returns on performance with lower rates so aggressive rates don't provide much more performance but can cause very bad image quality.
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f);

      // Deprecated per-signal rates - these are migrated to samplingRate via an onChange callback.
      static void deprecatedSamplingRateOnChange(DxvkDevice* device);
      RTX_OPTION_ARGS("rtx.sparseRendering", float, directLightingSamplingRate, 1.0f,
        "Warning: This option is deprecated, please use rtx.sparseRendering.samplingRate instead.\n"
        "Direct and indirect lighting use a single shared rate. A value set here migrates to that option unless it\n"
        "already holds a value from the same config, which is left as it is.",
        args.environment = "RTX_SPARSE_RENDERING_DIRECT_LIGHTING_SAMPLING_RATE",
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f,
        args.onChangeCallback = &deprecatedSamplingRateOnChange);

      RTX_OPTION_ARGS("rtx.sparseRendering", float, indirectLightingSamplingRate, 1.0f,
        "Warning: This option is deprecated, please use rtx.sparseRendering.samplingRate instead.\n"
        "Direct and indirect lighting use a single shared rate. A value set here migrates to that option unless it\n"
        "already holds a value from the same config, which is left as it is - including one migrated from\n"
        "rtx.sparseRendering.directLightingSamplingRate, which takes precedence when both are set.",
        args.environment = "RTX_SPARSE_RENDERING_INDIRECT_LIGHTING_SAMPLING_RATE",
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f,
        args.onChangeCallback = &deprecatedSamplingRateOnChange);

      RTX_OPTION("rtx.sparseRendering", bool, enableSparsePrimaryRayMissComposition, true,
        "When enabled, primary miss pixels (sky) use sparse rendering.\n"
        "This improves performance at the cost of sky reconstruction artifacts.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryHit, true,
        "When enabled, volumetric NEE integration at primary-hit pixels uses sparse rendering.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryMiss, true,
        "When enabled, volumetric NEE integration at primary-miss (sky) pixels uses sparse rendering.");
    };

    // Describes the placement of the compacted storage. Holds the extent that the compacted resources are allocated at
    // and the two parameters that map a global compacted index into that extent.
    // Falls back to the render extent with no capacity when sparse rendering is disabled or the storage cannot be packed,
    // which keeps sparse rendering off.
    struct CompactedStorageLayout {
      VkExtent3D extent;
      uint32_t squaresPerRow;
      uint32_t capacity;
    };

    // Derives the layout from the render extent, the options and isEnabledAndSupported,
    // so the resources and the shader args cannot disagree.
    CompactedStorageLayout calculateCompactedStorageLayout(const VkExtent3D& downscaledExtent) const;

    SparseRendering(dxvk::DxvkDevice* device);

    // Config-only mirror of isEnabled(): true iff the RTX options required for sparse rendering
    // (SR option, DLSS-RR, NRC indirect mode) are configured to be enabled. Safe to call at
    // startup before NRC/RR have finished initialising. Use this for shader prewarming;
    // use isActive() for per-frame dispatch decisions.
    static bool isEnabledByOptions();

    bool resamplesNrcTrainingPaths(bool nrcIsActive) const;

    // Launches a compute pass over the active-pixel list under sparse rendering.
    // Launches it over the given workgroups when sparse rendering is off.
    static void dispatchCompactedConsumer(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput, const VkExtent3D& workgroups);

    void showImguiSettings();
    void dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void setSparseRenderingArgs(RtxContext& ctx, SparseRenderingArgs& args) const;
    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

  protected:
    bool isEnabled() const override;
    void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;
    void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) override;
    void releaseDownscaledResource() override;

  private:
    // Returns whether sparse rendering can run on this device and configuration.
    // Leaves out only the storage capacity check, because the capacity is sized from this result.
    // Resources are compacted exactly when this holds, so the dense GBuffer path never sees compacted resources.
    bool isEnabledAndSupported() const;

    static float calculateCapacityRate(uint32_t pixelCount);

    void dispatchActivePixelSamplingRate(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchActivePixelMask(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchCompactActivePixels(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    bool checkCompactActivePixelsRequirements() const;
    bool checkCompactedLaunchLimits(uint32_t maxLaunchPixels) const;
    void allocateActivePixelListBuffers(Rc<DxvkContext>& ctx, Resources::RaytracingOutput& rtOutput) const;

    VkExtent3D m_activePixelMaskExtent = { 0u, 0u, 0u };
  };

} // namespace dxvk
