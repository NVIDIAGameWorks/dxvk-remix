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
#pragma once

#include "rtx_resources.h"
#include "rtx_options.h"

struct RaytraceArgs;

namespace dxvk {
  class Config;
  class RtxContext;

  class DxvkRtxdiRayQuery {
    
  public:
    
    DxvkRtxdiRayQuery(DxvkDevice* device);
    ~DxvkRtxdiRayQuery() = default;

    void dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchGradient(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);
    void dispatchConfidence(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);

    void showImguiSettings();
    void setRaytraceArgs(Resources::RaytracingOutput& rtOutput) const;
    bool getEnableDenoiserConfidence() const { return enableTemporalReuse() && enableDenoiserConfidence(); }
    
    RTX_OPTION("rtx.di", bool, enableCrossPortalLight, true, "");
    RTX_OPTION("rtx.di", bool, enableInitialVisibility, true, "Whether to trace a visibility ray for the light sample selected in the initial sampling pass.");
    RTX_OPTION("rtx.di", bool, enableBestLightSampling, true, "Whether to include a single best light from the previous frame's pixel neighborhood into initial sampling.");
    RW_RTX_OPTION("rtx.di", bool, enableRayTracedBiasCorrection, true, "Whether to use ray traced bias correction in the spatial reuse pass.");
    RTX_OPTION("rtx.di", bool, enableSampleStealing, true, "No visibile IQ gains, but exhibits considerable perf drop (8% in integrate pass).");
    RTX_OPTION("rtx.di", bool, stealBoundaryPixelSamplesWhenOutsideOfScreen, true, "Steal screen boundary samples when a hit point is outside the screen.");
    RTX_OPTION("rtx.di", bool, enableSpatialReuse, true, "Whether to apply spatial reuse.");
    RTX_OPTION("rtx.di", bool, enableTemporalBiasCorrection, true, "");
    RTX_OPTION("rtx.di", bool, enableTemporalReuse, true, "Whether to apply temporal reuse.");
    RTX_OPTION("rtx.di", bool, enableDiscardInvisibleSamples, true, "Whether to discard reservoirs that are determined to be invisible in final shading.");
    RTX_OPTION("rtx.di", bool, enableDiscardEnlargedPixels, true, "");
    RTX_OPTION("rtx.di", bool, enableDenoiserConfidence, true, "");
    RTX_OPTION("rtx.di", uint32_t, initialSampleCount, 4, "The number of lights randomly selected from the global pool to consider when selecting a light with RTXDI.");
    RTX_OPTION("rtx.di", uint32_t, spatialSamples, 2, "The number of spatial reuse samples in converged areas.");
    RTX_OPTION("rtx.di", uint32_t, disocclusionSamples, 4, "The number of spatial reuse samples in disocclusion areas.");
    RTX_OPTION("rtx.di", uint32_t, disocclusionFrames, 8, "");
    RTX_OPTION("rtx.di", uint32_t, gradientFilterPasses, 4, "");
    RTX_OPTION("rtx.di", uint32_t, permutationSamplingNthFrame, 0, "Apply permutation sampling when (frameIdx % this == 0), 0 means off.");
    RTX_OPTION("rtx.di", uint32_t, maxHistoryLength, 4, "Maximum age of reservoirs for temporal reuse.");
    RTX_OPTION("rtx.di", float, gradientHitDistanceSensitivity, 10.f, "");
    RTX_OPTION("rtx.di", float, confidenceHistoryLength, 8.f, "");
    RTX_OPTION("rtx.di", float, confidenceGradientPower, 8.f, "");
    RTX_OPTION("rtx.di", float, confidenceGradientScale, 6.f, "");
    RTX_OPTION("rtx.di", float, minimumConfidence, 0.1f, "");
    RTX_OPTION("rtx.di", float, confidenceHitDistanceSensitivity, 300.0f, "");
  };
}
