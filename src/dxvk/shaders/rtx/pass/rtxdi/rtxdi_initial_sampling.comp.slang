/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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

//!variant rtxdi_initial_sampling.comp
//!>       RTXDI_NO_PORTALS=0
//!variant rtxdi_initial_sampling_no_portals.comp
//!>       RTXDI_NO_PORTALS=1
//!end-variants

#define USE_32BIT_RAY_DIRECTION 1
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RTXDI_RESERVOIRS 1
#define RTXDI_DEFER_INITIAL_VISIBILITY 1

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
#include "rtx/pass/rtxdi/rtxdi_best_light.slangh"
#include "rtx/pass/sparse_rendering/compact_active_pixels.slangh"

[shader("compute")]
[numthreads(COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_X, COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_Y, 1)]
void main(uint2 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
  int2 pixelCoordinate;
  uvec2 compactedCoordinate;
  uint launchIndex;
  RAB_Surface surface;
  if (!RAB_LoadLaunchSurface(dispatchThreadId, groupId.x, groupIndex, pixelCoordinate, compactedCoordinate, launchIndex, surface))
  {
    return;
  }

  // The host allocates the guide and runs spatial selection under the same condition.
  if (cb.sparseRenderingArgs.mode != SparseRenderingMode::Off && cb.enableRtxdiSpatialReuse)
  {
    uint4 guide = uint4(0u, 0u, 0u, asuint(surface.hitDistance));
    if (RAB_IsSurfaceValid(surface))
    {
      const uint3 normalBits = asuint(RAB_GetSurfaceNormal(surface));
      guide.xy = normalBits.xy;

      // Reserve two low mantissa bits of normal Z for portal space.
      guide.z = (normalBits.z & ~3u) | uint(surface.portalSpace);
    }
    // See RAB_GetSpatialCandidateSurface for the layout.
    SelectionGuide[launchIndex] = guide;
  }
  if (!RAB_IsSurfaceValid(surface))
  {
    return;
  }

  RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixelCoordinate, cb.frameIdx, 2);

  vec2 screenSpaceMotion = PrimaryScreenSpaceMotionVector[pixelCoordinate];
  ivec2 prevPixelCenter = pixelCoordinate + ivec2(screenSpaceMotion.xy);

  uint16_t previousBestLightIndex;
  uint8_t previousBestLightPortalIdx;
  loadPreviousBestLight(rng, prevPixelCenter, previousBestLightIndex, previousBestLightPortalIdx);

  RTXDI_Reservoir state = RTXDI_EmptyReservoir();
  if (cb.lightCount != 0)
  {
    RTXDI_SampleRandomLights(state, rng, surface, previousBestLightIndex, previousBestLightPortalIdx,
      cb.enableRtxdiInitialVisibility && !RTXDI_DEFER_INITIAL_VISIBILITY);

    RTXDI_Reservoir bestLightReservoir = RTXDI_SampleSingleLight(rng, surface, previousBestLightIndex, previousBestLightPortalIdx,
      cb.enableRtxdiInitialVisibility && !RTXDI_DEFER_INITIAL_VISIBILITY);
    float random = RAB_GetNextRandom(rng);
    RTXDI_CombineReservoirs(state, bestLightReservoir, random, bestLightReservoir.targetPdf);

#if RTXDI_DEFER_INITIAL_VISIBILITY
    if (cb.enableRtxdiInitialVisibility)
    {
      RTXDI_UpdateSelectedReservoirVisibility(state, surface);
    }
#endif

    // Random lights and best light are two non-overlapping techniques, don't normalize over their combination
    state.M = 1;
  }

  RAB_StoreReservoir(state, pixelCoordinate, RTXDI_GetTemporalOutputPage(), launchIndex);
}
