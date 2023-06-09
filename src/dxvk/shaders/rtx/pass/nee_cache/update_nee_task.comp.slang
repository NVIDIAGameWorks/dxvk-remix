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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/nee_cache/update_nee_cache_bindings.slangh"
#include "rtx/pass/raytrace_args.h"

#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/common.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/utility/demodulate_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/utility/buffer_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtx/algorithm/nee_cache.h"
#include "rtx/external/nrd.slangh"

groupshared uint3 s_task[16*8];
groupshared int s_tarTaskCount;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(int2 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  // Clear target task list
  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // Load recorded task for each pixel
  uint cellOffset, surfaceID, primitiveID;
  NEECache.loadThreadTask(threadIndex, cellOffset, surfaceID, primitiveID);
  surfaceID = NEECacheUtils.convertToCurrentSurfaceID(surfaceID);
  uint3 threadTask = uint3(cellOffset, surfaceID, primitiveID);
  if (threadTask.y != 0xffffffff && threadTask.z != 0xffffffff)
  {
    int oldCount;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_task[oldCount] = threadTask;
  }
  GroupMemoryBarrierWithGroupSync();

  // Skip repeated tasks
  bool shouldBeAdded = false;
  int localTaskID = localIndex.y * 16 + localIndex.x;
  uint3 localTask = s_task[localTaskID];
  if (localTaskID < s_tarTaskCount)
  {
    bool isNew = true;
    for (int i = 0; i < localTaskID-1; ++i)
    {
      if (all(s_task[i] == localTask))
      {
        isNew = false;
      }
    }
    shouldBeAdded = isNew;
  }

  // Add remaining tasks to the pixel's corresponding cell
  uint lightToCellI = -1;
  if (shouldBeAdded)
  {
    int3 cellID = NEECache.offsetToCell(localTask.x);
    if (all(cellID != -1))
    {      
      NEECell cell = NEECache.getCell(cellID);
      if (cell.isValid())
      {
        int prefixTask = NEECacheUtils.convertIDToPrefixSumID(localTask.y, localTask.z, PrimitiveIDPrefixSum);
        if (prefixTask != -1)
        {
          vec3 cellCenter = NEECache.cellToCenterPoint(cellID);
          float cellSize = NEECache.getCellSize();
          float lightToCell = NEECacheUtils.calculateTriangleLightToCell(localTask.yz, 0, cellCenter, cellSize);
          lightToCellI = clamp(log(lightToCell + 1e-10) + 10, 0, 255);
          cell.insertTask(prefixTask, lightToCellI);
        }
      }
    }
  }


  switch(cb.debugView)
  {
  case DEBUG_VIEW_NEE_CACHE_TASK:
    vec3 color = lightToCellI == -1 ? vec3(0,0,0.5) : lightToCellI.xxx;
    storeInDebugView(ivec2(threadIndex), color);
    break;
  }
}
