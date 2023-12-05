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
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RESTIR_GI_RESERVOIRS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/rtxdi/restir_gi_reuse_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/texture.slangh"
#include "rtx/utility/sampling.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/buffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/concept/light/light.slangh"
#include "rtx/algorithm/resolve.slangh"
#include "rtx/algorithm/visibility.slangh"
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
  imageStore(GBufferLast, thread_id, RAB_PackLastFrameGBuffer(surface));

  if (!RAB_IsSurfaceValid(surface))
  {
    return;
  }

  RAB_RandomSamplerState rtxdiRNG = RAB_InitRandomSampler(thread_id, cb.frameIdx, 3);

  ReSTIRGI_Reservoir spatialReservoir = RAB_LoadGIReservoir(thread_id, ReSTIRGI_GetSpatialInputPage());

  ReSTIRGI_SpatialResamplingParameters sparams = {};
  sparams.fastHistoryLength = cb.temporalHistoryLength;
  sparams.frameIndex = cb.frameIdx;
  sparams.biasCorrectionMode = cb.reSTIRGIBiasCorrectionMode;
  sparams.numActiveRayPortals = cb.numActiveRayPortals;
  sparams.resolution = camera.resolution;
  sparams.sourceBufferIndex = ReSTIRGI_GetSpatialInputPage();
  sparams.pairwiseMISCentralWeight = cb.reSTIRGIMISModePairwiseMISCentralWeight;
  // Use large search radius to ensure diffuse quality and suppress boiling, small radius to reduce noise. Values are based on experiment.
  sparams.initialSearchRadius = spatialReservoir.M < cb.temporalHistoryLength || (cb.frameIdx + thread_id.x / 16 + thread_id.y / 8) % 2 == 0 ? 200.f : 85.f;
  sparams.numSamples = spatialReservoir.M < cb.temporalHistoryLength ? 4 : 1;
  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_SpatialResampling(spatialReservoir, thread_id, surface, rtxdiRNG, sparams);

  if (cb.debugView == DEBUG_VIEW_RESTIR_GI_SPATIAL_REUSE)
  {
    storeInDebugView(thread_id, resultReservoir.radiance * resultReservoir.avgWeight);
  }
  else if(cb.debugView == DEBUG_VIEW_NAN)
  {
    bool isValid = true;
    isValid &= isValidValue(resultReservoir.radiance * resultReservoir.avgWeight);
    accumulateInDebugViewAnd(thread_id, isValid);
  }

  if (!cb.enableReSTIRGISpatialReuse)
  {
    resultReservoir = RAB_LoadGIReservoir(thread_id, ReSTIRGI_GetSpatialInputPage());
  }
  RAB_StoreGIReservoir(resultReservoir, thread_id, ReSTIRGI_GetSpatialOutputPage());
}
