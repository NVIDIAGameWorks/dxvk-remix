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
#include "rtx/utility/buffer_helpers.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/concept/light/light.slangh"
#include "rtx/algorithm/resolve.slangh"
#include "rtx/algorithm/visibility.slangh"
#include "rtx/algorithm/rtxdi/rtxdi.slangh"
#include "rtxdi/DlssEnhancementFilterFunctions.slangh"

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
  if(!cb.enableReSTIRGI)
  {
    imageStore(GBufferLast, thread_id, RAB_PackLastFrameGBuffer(surface));
  }

  if (!RAB_IsSurfaceValid(surface))
  {
    return;
  }

  RAB_RandomSamplerState rtxdiRNG = RAB_InitRandomSampler(thread_id, cb.frameIdx, 3);
  
  RTXDI_Reservoir reservoir = RAB_LoadReservoir(thread_id, RTXDI_GetSpatialInputPage());

  // Calculate the average reprojection confidence in the neighborhood to reject individual pixels that failed to reproject
  const int filterRadius = 1;
  const int filterArea = (filterRadius * 2 + 1) * (filterRadius * 2 + 1);
  float sumConfidence = 0;
  for (int xx = -filterRadius; xx <= filterRadius; ++xx)
  for (int yy = -filterRadius; yy <= filterRadius; ++yy)
  {
    sumConfidence += ReprojectionConfidence[thread_id + int2(xx, yy)].r;
  }

  const float historyLength = sumConfidence * (float(kReprojectionConfidenceHistoryLength) / float(filterArea));

  // Use the average history length to decide if we need to apply disocclusion boost.
  int numSpatialSamples = historyLength < cb.rtxdiDisocclusionFrames
    ? cb.rtxdiDisocclusionSamples
    : cb.rtxdiSpatialSamples;

  RTXDI_SpatialResamplingParameters sparams;
  sparams.numSamples = cb.enableRtxdiSpatialReuse ? clamp(numSpatialSamples, 1, 16) : 0;
  sparams.sourceBufferIndex = RTXDI_GetSpatialInputPage();
  sparams.biasCorrectionMode = cb.enableRtxdiRayTracedBiasCorrection
    ? RTXDI_BIAS_CORRECTION_RAY_TRACED
    : RTXDI_BIAS_CORRECTION_BASIC;
  sparams.samplingRadius = 30;
  sparams.normalThreshold = 0.5;
  sparams.depthThreshold = 0.1;
  sparams.screenResolution = cb.camera.resolution;

  RTXDI_Reservoir result = RTXDI_SpatialResampling(
    thread_id,
    surface,
    reservoir,
    rtxdiRNG,
    sparams);

  RAB_StoreReservoir(result, thread_id, RTXDI_GetSpatialOutputPage());

  f16vec2 factor = f16vec2(1);
  if(cb.enableEnhanceBSDFDetail && RTXDI_IsValidReservoir(result))
  {
    const RAB_LightSample finalSample =
      RAB_GetPortalSpaceLightSample(result.lightIdx, f16vec2(result.uv), result.portalIdx, surface);
    const float3 cameraPosition = cameraGetWorldPosition(camera);
    const float neighborDepthThreshold = 0.02f;
    const float neighborNormalThreshold = 0.998f;
    const float brdfClampMin = 0.001f;
    const float brdfClampMax = 1000.0f;
    float3 finalSampleDirection = normalize(finalSample.position - surface.minimalSurfaceInteraction.position);
    factor = RTXDI_GetDlssEnhancementDirectFactor(thread_id, finalSample.position, finalSampleDirection, false, cameraPosition, neighborDepthThreshold, neighborNormalThreshold, brdfClampMin, brdfClampMax);

    factor = pow(factor, float16_t(cb.enhanceBSDFDirectLightPower));
    factor = clamp(factor, f16vec2(0), f16vec2(cb.enhanceBSDFDirectLightMaxValue));
  }
  BSDFFactor[thread_id] = vec2(factor);
}
