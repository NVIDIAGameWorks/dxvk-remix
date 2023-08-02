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

groupshared ThreadTask s_task[16*8];
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
  ThreadTask task = NEECache.loadThreadTask(threadIndex);
  if (task.isValid())
  {
    int oldCount;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_task[oldCount] = task;
  }
  GroupMemoryBarrierWithGroupSync();

  // Skip repeated tasks
  bool shouldBeAdded = false;
  int localTaskID = localIndex.y * 16 + localIndex.x;
  ThreadTask localTask = ThreadTask.createEmpty();
  if (localTaskID < s_tarTaskCount)
  {
    localTask = s_task[localTaskID];
    bool isNew = true;
    for (int i = 0; i < localTaskID-1; ++i)
    {
      if (all(s_task[i].m_data == localTask.m_data))
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
    int3 cellID = NEECache.offsetToCell(localTask.getCellOffset());
    if (all(cellID != -1))
    {
      NEECell cell = NEECache.getCell(cellID);
      if (cell.isValid())
      {
        vec3 cellCenter = NEECache.cellToCenterPoint(cellID);
        float cellSize = NEECache.getCellSize();
        float lightToCell = 0;
        uint encodedTask = 0;

        if (localTask.isLightTask())
        {
          uint lightIdx = localTask.getLightTask();
          encodedTask = NEECache.encodeAnalyticalLight(lightIdx);
          lightToCell = NEECacheUtils.calculateAnalyticLightToCell(lightIdx, 0, cellCenter, cellSize);
        }
        else
        {
          uint surfaceID, primitiveID;
          if(localTask.getTriangleTask(surfaceID, primitiveID))
          {
            surfaceID = NEECacheUtils.convertToCurrentSurfaceID(surfaceID);
            if (surfaceID != 0xffffffff)
            {
              int prefixTask = NEECacheUtils.convertIDToPrefixSumID(surfaceID, primitiveID, PrimitiveIDPrefixSum);
              if (prefixTask != -1)
              {
                lightToCell = NEECacheUtils.calculateTriangleLightToCell(uint2(surfaceID, primitiveID), 0, cellCenter, cellSize);
                encodedTask = prefixTask;
              }
            }
          }
        }

        if (lightToCell > 0)
        {
          lightToCellI = clamp(log(lightToCell + 1e-10) + 10, 0, 255);
          cell.insertTask(encodedTask, lightToCellI);
        }
      }
    }
  }

  switch(cb.debugView)
  {
    case DEBUG_VIEW_NEE_CACHE_TASK:
    {
      vec3 color = 0;

      if (task.isLightTask())
      {
        color = vec3(0,0,1);
      }
      else if(task.isTriangleTask())
      {
        color = vec3(1,0,0);
      }

      storeInDebugView(ivec2(threadIndex), color);
      break;
    }
  }
}
