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
#include "rtx/pass/demodulate/update_nee_cache_bindings.slangh"

#include "rtx/pass/raytrace_args.h"

#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/common.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/demodulate_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtx/external/nrd.slangh"
#include "rtx/algorithm/neecache.h"

groupshared uint2 s_srcTaskList[RADIANCE_CACHE_ELEMENTS];
groupshared uint2 s_tarTaskList[RADIANCE_CACHE_ELEMENTS];
groupshared int s_tarTaskCount;

[shader("compute")]
[numthreads(RADIANCE_CACHE_ELEMENTS, 1, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  int3 cellID = threadIndex / int3(RADIANCE_CACHE_ELEMENTS,1,1);
  NEECell cell = NEECache.createCell(cellID);

  RNG rng = createRNG(uvec2(threadIndex.xy + threadIndex.z * RADIANCE_CACHE_ELEMENTS * 20 * 20), cb.frameIdx);

  // load new id to shared memory
  int taskCount = cell.getTaskCount();
  int taskID = localIndex.x;  
  int2 task = cell.getTask(taskID);
  s_srcTaskList[taskID] = task;
  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // load old id to shared memory
  int oldIDCount = cell.getCandidateCount();
  if (localIndex.x < oldIDCount && getNextSampleBlueNoise(rng) < 0.99)
  {
    int oldCount = 0;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_tarTaskList[oldCount] = cell.getCandidate(localIndex.x);
  }
  GroupMemoryBarrierWithGroupSync();

  // merge tasks
  bool insertTask = false;
  if (taskID < taskCount && all(task != -1))
  {
    bool isNewTask = true;
    for (int i = 0; i < taskID -1; ++i)
    {
      if (all(s_srcTaskList[i] == task))
      {
        isNewTask = false;
      }
    }
    for (int i = 0; i < min(s_tarTaskCount, RADIANCE_CACHE_ELEMENTS-1); ++i)
    {
      if (all(s_tarTaskList[i] == task))
      {
        isNewTask = false;
      }
    }
    insertTask = isNewTask;
  }

  if (insertTask && s_tarTaskCount < RADIANCE_CACHE_ELEMENTS-1)
  {
    int oldCount = 0;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    if (oldCount < RADIANCE_CACHE_ELEMENTS-1)
    {
      s_tarTaskList[oldCount] = task;
    }
  }
  GroupMemoryBarrierWithGroupSync();

  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = min(s_tarTaskCount, RADIANCE_CACHE_ELEMENTS-1);
  }

  // update nee cache
  if (localIndex.x < s_tarTaskCount)
  {
    int candidateID = localIndex.x;
    cell.setCandidate(candidateID, s_tarTaskList[candidateID]);
  }

  if (localIndex.x == 0 && localIndex.y == 0)
  {
    cell.setTaskCount(0);
    if (cb.debugKnob.x != 0)
    {
      cell.setCandidateCount(min(RADIANCE_CACHE_ELEMENTS-1, s_tarTaskCount));
    }
    else
    {
      cell.setCandidateCount(0);
    }
  }
}
