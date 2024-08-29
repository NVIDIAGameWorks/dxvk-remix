/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#define USE_32BIT_RAY_DIRECTION 1
#define RAB_HAS_PREVIOUS_GBUFFER 1
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RTXDI_RESERVOIRS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/rtxdi/rtxdi_reuse_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/texture.slangh"
#include "rtx/utility/sampling.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/concept/light/light.slangh"
#include "rtx/algorithm/resolve.slangh"
#include "rtx/algorithm/rtxdi/rtxdi.slangh"

[shader("compute")]
[numthreads(16, 8, 1)]
void main(int2 thread_id : SV_DispatchThreadID)
{
  Camera camera = cb.camera;
  if (thread_id.x >= camera.resolution.x || thread_id.y >= camera.resolution.y)
  {
    return;
  }

  RAB_Surface surface = RAB_GetGBufferSurface(thread_id, false);
  if (!RAB_IsSurfaceValid(surface))
  {
    return;
  }
  
  RAB_RandomSamplerState rng = RAB_InitRandomSampler(thread_id, cb.frameIdx, 2);

  vec2 screenSpaceMotion = PrimaryScreenSpaceMotionVector[thread_id];
  ivec2 prevPixelCenter = thread_id + ivec2(screenSpaceMotion.xy);

  uint16_t previousBestLightIndex = RTXDI_INVALID_LIGHT_INDEX;
  uint8_t previousBestLightPortalIdx = RTXDI_INVALID_PORTAL_INDEX;
  if (all(prevPixelCenter >= 0) && all(prevPixelCenter < cb.camera.resolution) && cb.enableRtxdiBestLightSampling)
  {
    uint2 previousBestLight = RtxdiBestLights[prevPixelCenter / RTXDI_GRAD_FACTOR];

    // Map the previous light index to the current frame, make sure the portal still exists if that light was a cross-portal one
    if (previousBestLight.x != RTXDI_INVALID_LIGHT_INDEX && (previousBestLight.y == RTXDI_INVALID_PORTAL_INDEX || previousBestLight.y < cb.numActiveRayPortals))
    {
      previousBestLightIndex = RAB_TranslateLightIndex(previousBestLight.x, /* currentToPrevious = */ false);
      previousBestLightPortalIdx = previousBestLight.y;
    }
  }


  RTXDI_Reservoir state = RTXDI_EmptyReservoir();
  if (cb.lightCount != 0)
  {
    RTXDI_SampleRandomLights(state, rng, surface, previousBestLightIndex, previousBestLightPortalIdx, cb.enableRtxdiInitialVisibility);

    RTXDI_Reservoir bestLightReservoir = RTXDI_SampleSingleLight(rng, surface, previousBestLightIndex, previousBestLightPortalIdx, cb.enableRtxdiInitialVisibility);
    float random = RAB_GetNextRandom(rng);
    RTXDI_CombineReservoirs(state, bestLightReservoir, random, bestLightReservoir.targetPdf);
    
    // Note: I have NO IDEA why this works, but it's necessary to get unbiased results with best light sampling.
    state.M = 1;
  }

  const float3 virtualMotionVector = PrimaryVirtualMotionVector[thread_id].xyz;
  const float3 prevVirtualWorldPosition = surface.virtualWorldPosition + virtualMotionVector;

  float viewDirectionDotTriangleNormal = abs(dot(surface.minimalRayInteraction.viewDirection, surface.minimalSurfaceInteraction.triangleNormal));

  RTXDI_TemporalResamplingParameters tparams;
  tparams.prevPixelCenter = prevPixelCenter;
  tparams.expectedPrevHitDistance = length(prevVirtualWorldPosition - cameraGetPreviousWorldPosition(camera).xyz);
  tparams.virtualMotionVector = virtualMotionVector;
  tparams.sourceBufferIndex = RTXDI_GetTemporalInputPage();
  tparams.maxHistoryLength = cb.enableRtxdiTemporalReuse ? cb.rtxdiMaxHistoryLength : 0;
  tparams.biasCorrectionMode = cb.enableRtxdiTemporalBiasCorrection ? 
    cb.enableRtxdiRayTracedBiasCorrection 
      ? RTXDI_BIAS_CORRECTION_RAY_TRACED
      : RTXDI_BIAS_CORRECTION_BASIC
    : RTXDI_BIAS_CORRECTION_OFF;
  tparams.depthThreshold = 0.01 / max(viewDirectionDotTriangleNormal, 0.01);
  tparams.normalThreshold = 0.8;
  tparams.discardEnlargedPixels = cb.enableRtxdiDiscardEnlargedPixels;
  tparams.discardInvisibleSamples = cb.enableRtxdiDiscardInvisibleSamples;
  tparams.enablePermutationSampling = cb.enableRtxdiPermutationSampling;
  tparams.enableCrossPortalReuse = cb.teleportationPortalIndex != 0;
  tparams.uniformRandomNumber = cb.uniformRandomNumber;

  int2 temporalSamplePixelPos;
  bool isGBufferSimilar;
  RTXDI_TemporalResampling(state, thread_id, surface, rng, tparams, temporalSamplePixelPos, isGBufferSimilar);

  float currentReprojectionConfidence = 0;
  if (isGBufferSimilar)
    currentReprojectionConfidence = ReprojectionConfidence[temporalSamplePixelPos];

  currentReprojectionConfidence = min(currentReprojectionConfidence + 1.f / kReprojectionConfidenceHistoryLength, 1.f);
  ReprojectionConfidence[thread_id] = currentReprojectionConfidence;

  // Remember which pixel the temporal reservoir belongs to, if it comes from a valid reprojection.
  // Needed for the gradients pass.
  if (!isGBufferSimilar)
    temporalSamplePixelPos = -1;
  TemporalPosition[thread_id] = pack32(u16vec2(temporalSamplePixelPos));

  RAB_StoreReservoir(state, thread_id, RTXDI_GetTemporalOutputPage());
}
