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

#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "../spirv/spirv_code_buffer.h"
#include "rtx_resources.h"
#include "rtx_options.h"

namespace dxvk {

  enum class ReSTIRGIBiasCorrection : int {
    None,
    BRDF,
    Raytrace,
    Pairwise,
    PairwiseRaytrace
  };

  enum class ReSTIRGISampleStealing : int {
    None,
    StealSample,
    StealPixel
  };

  enum class ReSTIRGIMIS : int {
    None,
    Roughness,
    Parallax
  };
  
  class RtxContext;

  class DxvkReSTIRGIRayQuery: public RtxPass {
    
  public:
    
    DxvkReSTIRGIRayQuery(DxvkDevice* device);
    ~DxvkReSTIRGIRayQuery() = default;

    void dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);

    void showImguiSettings();

    int getTemporalHistoryLength(float frameTimeMs) {
      if(useAdaptiveTemporalHistory())
        return static_cast<int>(std::max(temporalAdaptiveHistoryLengthMs() / frameTimeMs, 20.0f));
      else
        return temporalFixedHistoryLength();
    }

  private:
    bool isActive() { return RtxOptions::Get()->useReSTIRGI(); }

    RTX_OPTION("rtx.restirGI", bool, useTemporalReuse, true, "");
    RTX_OPTION("rtx.restirGI", bool, useSpatialReuse, true, "");
    RTX_OPTION("rtx.restirGI", ReSTIRGIMIS, misMode, ReSTIRGIMIS::Parallax, "");
    RTX_OPTION("rtx.restirGI", bool, useFinalVisibility, true, "");
    RTX_OPTION("rtx.restirGI", bool, useVirtualSample, true,
      R"(ReSTIR virtual sample can improve results on highly specular surfaces by storing virtual samples behind the mirror, instead of actual samples "on the mirror". When an indirect ray hits a highly specular surface, the hit T will get accumulated until a path vertex with significant contribution is hit. Then the hit T will be used to extend the 1st indirect ray, whose extended end point will be the virtual sample's position. If the significant path vertex has high specular contribution, its distance to light source will also get accumulated. The last path vertex with radiance greater than 2 times of the previous accumulated radiance will get virtualized. Value is based on experiment. Higher values tend to keep the first path vertex with non-zero contribution, unless another one with much higher contribution is discovered.)");
    RTX_OPTION("rtx.restirGI", float, virtualSampleLuminanceThreshold, 2.0, "The last path vertex with radiance greater than 2 times of the previous accumulated radiance will get virtualized. Value is based on experiment. Higher values tend to keep the first path vertex with non-zero contribution, unless another one with much higher contribution is discovered.");
    RTX_OPTION("rtx.restirGI", float, virtualSampleRoughnessThreshold, 0.2, R"(Surface with roughness under this threshold is consider to be highly specular, i.e. a "mirror".)");
    RTX_OPTION("rtx.restirGI", float, virtualSampleSpecularThreshold, 0.5, "If a highly specular path vertex's specular light portion is higher than this. Its distance to the light source will get accumulated.");

    RTX_OPTION("rtx.restirGI", float, pairwiseMISCentralWeight, 0.1, "");
    RTX_OPTION("rtx.restirGI", bool, useDemodulatedTargetFunction, false, "");
    RTX_OPTION("rtx.restirGI", bool, usePermutationSampling, true, "");
    RTX_OPTION("rtx.restirGI", ReSTIRGISampleStealing,  useSampleStealing, ReSTIRGISampleStealing::StealPixel, "");
    RTX_OPTION("rtx.restirGI", bool, stealBoundaryPixelSamplesWhenOutsideOfScreen , true, "");
    RTX_OPTION("rtx.restirGI", bool, useTemporalBiasCorrection, true, "");
    RTX_OPTION("rtx.restirGI", bool, useDiscardEnlargedPixels, true, "");
    RTX_OPTION("rtx.restirGI", bool, useTemporalJacobian, true, "");
    RW_RTX_OPTION("rtx.restirGI", bool, useReflectionReprojection, true, "Use reflection reprojection for reflective objects to achieve stable result when the camera is moving.");
    RTX_OPTION("rtx.restirGI", float, reflectionMinParallax, 3.0, "When the parallax between normal and reflection reprojection is greater than this threshold, randomly choose one reprojected position and reuse the sample on it. Otherwise, get a sample between the two positions.");
    RTX_OPTION("rtx.restirGI", bool, useBoilingFilter, true, "");
    RTX_OPTION("rtx.restirGI", float, boilingFilterMinThreshold, 10.0, "");
    RTX_OPTION("rtx.restirGI", float, boilingFilterMaxThreshold, 20.0, "");
    RTX_OPTION("rtx.restirGI", float, boilingFilterRemoveReservoirThreshold, 62.0, "");
    RTX_OPTION_ENV("rtx.restirGI", bool, useAdaptiveTemporalHistory, true, "DXVK_USE_ADAPTIVE_RESTIR_GI_ACCUMULATION", "");
    RTX_OPTION("rtx.restirGI", int, temporalAdaptiveHistoryLengthMs, 500, "");
    RTX_OPTION("rtx.restirGI", int, temporalFixedHistoryLength, 30, "");
    RTX_OPTION("rtx.restirGI", int, permutationSamplingSize, 2, "");
    RW_RTX_OPTION("rtx.restirGI", ReSTIRGIBiasCorrection, biasCorrectionMode, ReSTIRGIBiasCorrection::PairwiseRaytrace, "");
    RTX_OPTION("rtx.restirGI", float, fireflyThreshold, 50.f, "");
    RTX_OPTION("rtx.restirGI", float, roughnessClamp, 0.01f, "");
    RTX_OPTION("rtx.restirGI", float, misRoughness, 0.3, "");
    RTX_OPTION("rtx.restirGI", float, parallaxAmount, 0.02, "");
  };
}
