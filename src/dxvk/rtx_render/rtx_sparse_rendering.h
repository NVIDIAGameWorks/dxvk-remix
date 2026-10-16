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

      RTX_OPTION_ARGS("rtx.sparseRendering", float, samplingRate, 0.357f,
        "Per-pixel sampling rate.\n"
        "When enableAdaptiveSamplingRate is set, it applies at the reference internal resolution and other internal resolutions scale it, see samplingRateResolutionExponent.\n"
        "Graphics presets set it together with samplingRateResolutionExponent. The default is the Ultra preset's rate at 4K with DLSS Auto.\n"
        "Lower values boost performance at the cost of image detail and temporal stability.\n"
        "Range [0.0625, 1.0]. Rates below ~0.2 tend to suffer from undersampling and yield diminishing performance returns.\n"
        "Sparse rendering turns off when the scaled rate exceeds maxSamplingRate.",
        args.environment = "RTX_SPARSE_RENDERING_SAMPLING_RATE",
        // 1/255 is the absolute minimum, but 1/16 is a more reasonable lower bound for image quality. Even 1/16 will show temporal instabilities but tends to not be catastrophic.
        // Second, there are diminishing returns on performance with lower rates so aggressive rates don't provide much more performance but can cause very bad image quality.
        args.minValue = 1.0f / 16.0f,
        args.maxValue = 1.0f,
        args.flags = RtxOptionFlags::UserSetting);

      RTX_OPTION("rtx.sparseRendering", bool, enableAdaptiveSamplingRate, true,
        "Picks the sampling rate automatically for the current resolution. Use the quality option to further tune the balance of performance and quality.\n"
        "When disabled, samplingRate is used as is at every resolution.");

      RTX_OPTION_ARGS("rtx.sparseRendering", float, samplingRateResolutionExponent, 0.69f,
        "Scales samplingRate by the reference internal resolution's pixel count divided by the current internal resolution's pixel count, raised to this exponent.\n"
        "Graphics presets set it together with samplingRate.\n"
        "The scaled rate stays within samplingRate's range. 0 applies samplingRate at every resolution, and 1 keeps the number of active pixels constant.",
        args.minValue = 0.0f,
        args.maxValue = 1.0f,
        args.flags = RtxOptionFlags::UserSetting);

      RTX_OPTION_ARGS("rtx.sparseRendering", float, quality, 0.5f,
        "Trades performance for image quality when auto scaling is enabled.\n"
        "0.5 is the default and recommended value, 1 gives the highest quality and 0 the highest performance.\n"
        "Values toward 1 can raise the rate above maxSamplingRate, which turns sparse rendering off.\n"
        "The auto scaled rate is already adjusted for each graphics preset and resolution, so this is meant for slight fine tuning.\n"
        "Setting it too low is not recommended, since image quality degrades quickly at low rates.\n"
        "Adjusts the rate that auto scaling calculates from samplingRate and samplingRateResolutionExponent.",
        args.minValue = 0.0f,
        args.maxValue = 1.0f);

      RTX_OPTION_ARGS("rtx.sparseRendering", float, maxSamplingRate, 0.75f,
        "Sparse rendering turns off when the scaled samplingRate exceeds this, since above it the performance gain is no longer worth the loss in image quality.",
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

      RTX_OPTION_ARGS("rtx.sparseRendering", bool, enableSparseAwareAlphaBlendSearch, true,
        "Improves the lighting of semi-transparent surfaces, such as particles and smoke, when sparse rendering is enabled.\n"
        "Costs a little performance, more so at lower sampling rates.",
        args.flags = RtxOptionFlags::UserSetting);

      RTX_OPTION("rtx.sparseRendering", bool, enableSparsePrimaryRayMissComposition, false,
        "When enabled, primary miss pixels (sky) use sparse rendering.\n"
        "This improves performance at the cost of sky reconstruction artifacts.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryHit, false,
        "When enabled, volumetric NEE integration at primary-hit pixels uses sparse rendering.");

      RTX_OPTION("rtx.sparseRendering", bool, enableSparseVolumetricsPrimaryMiss, false,
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

    // Returns the sampling rate at the current render resolution.
    // Returns 1 when sparse rendering is off, including when the rate exceeds maxSamplingRate.
    float getSamplingRate() const;

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

    // Returns samplingRate scaled for a raytracing resolution with this many pixels.
    static float calculateSamplingRate(uint32_t pixelCount);
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
