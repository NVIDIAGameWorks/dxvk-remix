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

    RTX_OPTION("rtx.restirGI", bool, useTemporalReuse, true, "Enables temporal reuse.");
    RTX_OPTION("rtx.restirGI", bool, useSpatialReuse, true, "Enables spatial reuse.");
    RTX_OPTION("rtx.restirGI", bool, useFinalVisibility, true, "Tests visiblity in output.");

    // ReSTIR GI cannot work very well on specular surfaces. We need to mix the specular output with its input to improve quality.
    RTX_OPTION("rtx.restirGI", ReSTIRGIMIS, misMode, ReSTIRGIMIS::Parallax, "MIS mode to mix specular output with input.");
    RTX_OPTION("rtx.restirGI", float, misRoughness, 0.3, "Reference roughness when roughness MIS is used. Higher values give ReSTIR inputs higher weight.");
    RTX_OPTION("rtx.restirGI", float, parallaxAmount, 0.02, "Parallax strength when parallax MIS is used. Higher values give ReSTIR inputs higher weight.");

    // ReSTIR virtual sample can improve results on highly specular surfaces by storing virtual samples "behind the mirror",
    // instead of actual samples "on the mirror".
    // When an indirect ray hits a highly specular surface, the hit T will get accumulated until a path vertex with significant
    // contribution is hit. Then the hit T will be used to extend the 1st indirect ray, whose extended end point will be the
    // virtual sample's position. If the significant path vertex has high specular contribution, its distance to light source
    // will also get accumulated.
    RTX_OPTION("rtx.restirGI", bool, useVirtualSample, true, "Uses virtual position for samples from highly specular surfaces.");
    RTX_OPTION("rtx.restirGI", float, virtualSampleLuminanceThreshold, 2.0, "The last path vertex with luminance greater than 2 times of the previous accumulated radiance will get virtualized. Higher values tend to keep the first path vertex with non-zero contribution.");
    RTX_OPTION("rtx.restirGI", float, virtualSampleRoughnessThreshold, 0.2, R"(Surface with roughness under this threshold is considered to be highly specular, i.e. a "mirror".)");
    RTX_OPTION("rtx.restirGI", float, virtualSampleSpecularThreshold, 0.5, "If a highly specular path vertex's direct specular light portion is higher than this. Its distance to the light source will get accumulated.");

    RTX_OPTION("rtx.restirGI", bool, useTemporalBiasCorrection, true, "Corrects bias caused by temporal reprojection.");
    RW_RTX_OPTION("rtx.restirGI", ReSTIRGIBiasCorrection, biasCorrectionMode, ReSTIRGIBiasCorrection::PairwiseRaytrace, "Bias correction mode to combine central with its neighbors in spatial reuse.");
    RTX_OPTION("rtx.restirGI", float, pairwiseMISCentralWeight, 0.1, "The importance of central sample in pairwise bias correction modes.");
    
    RTX_OPTION("rtx.restirGI", bool, useDemodulatedTargetFunction, false, "Demodulates target function. This will improve the result in non-pairwise modes.");
    RTX_OPTION("rtx.restirGI", bool, usePermutationSampling, true, "Uses permutation sample to perturb samples. This will improve results in DLSS.");
    RTX_OPTION("rtx.restirGI", ReSTIRGISampleStealing,  useSampleStealing, ReSTIRGISampleStealing::StealPixel, "Steals ReSTIR GI samples in path tracer. This will improve highly specular results.");
    RTX_OPTION("rtx.restirGI", bool, stealBoundaryPixelSamplesWhenOutsideOfScreen , true, "Steals ReSTIR GI samples even a hit point is outside the screen. This will further improve highly specular samples at the cost of some bias.");
    RTX_OPTION("rtx.restirGI", bool, useDiscardEnlargedPixels, true, "Discards enlarged samples when the camera is moving towards an object.");
    RTX_OPTION("rtx.restirGI", bool, useTemporalJacobian, true, "Calculates Jacobian determinant in temporal reprojection.");
    RW_RTX_OPTION("rtx.restirGI", bool, useReflectionReprojection, true, "Uses reflection reprojection for reflective objects to achieve stable result when the camera is moving.");
    RTX_OPTION("rtx.restirGI", float, reflectionMinParallax, 3.0, "When the parallax between normal and reflection reprojection is greater than this threshold, randomly choose one reprojected position and reuse the sample on it. Otherwise, get a sample between the two positions.");
    RTX_OPTION("rtx.restirGI", bool, useBoilingFilter, true, "Enables boiling filter to suppress boiling artifacts.");
    RTX_OPTION("rtx.restirGI", float, boilingFilterMinThreshold, 10.0, "Boiling filter threshold when surface normal is perpendicular to view direction.");
    RTX_OPTION("rtx.restirGI", float, boilingFilterMaxThreshold, 20.0, "Boiling filter threshold when surface normal is parallel to view direction.");
    RTX_OPTION("rtx.restirGI", float, boilingFilterRemoveReservoirThreshold, 62.0, "Removes a sample when a sample's weight exceeds this threshold.");
    RTX_OPTION_ENV("rtx.restirGI", bool, useAdaptiveTemporalHistory, true, "DXVK_USE_ADAPTIVE_RESTIR_GI_ACCUMULATION", "Adjust temporal history length based on frame rate.");
    RTX_OPTION("rtx.restirGI", int, temporalAdaptiveHistoryLengthMs, 500, "Temporal history time length, when adaptive temporal history is enabled.");
    RTX_OPTION("rtx.restirGI", int, temporalFixedHistoryLength, 30, "Fixed temporal history length, when adaptive temporal history is disabled.");
    RTX_OPTION("rtx.restirGI", int, permutationSamplingSize, 2, "Permutation sampling strength.");
    RTX_OPTION("rtx.restirGI", float, fireflyThreshold, 50.f, "Clamps specular input to suppress boiling.");
    RTX_OPTION("rtx.restirGI", float, roughnessClamp, 0.01f, "Clamps minimum roughness a sample's importance is evaluated.");
    RTX_OPTION("rtx.restirGI", bool, useSampleValidation, true, "Validate samples when direct light has changed.");
    RTX_OPTION_ENV("rtx.restirGI", float, sampleValidationThreshold, 0.5, "DXVK_RESTIR_GI_SAMPLE_VALIDATION_THRESHOLD", "Validate samples when normalized pixel change is above this value.");
  };
}
