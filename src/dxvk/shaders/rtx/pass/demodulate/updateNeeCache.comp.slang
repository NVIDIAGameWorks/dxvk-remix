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
#include "rtx/algorithm/neecache.h"
#include "rtx/external/nrd.slangh"

groupshared uint2 s_srcTaskList[RADIANCE_CACHE_ELEMENTS];
groupshared uint2 s_tarTaskList[RADIANCE_CACHE_ELEMENTS * 2];
groupshared float s_tarLight[RADIANCE_CACHE_ELEMENTS * 2];
groupshared int s_tarTaskCount;

uint2 mapIDData(uint2 data)
{
  uint2 newData = data;
  newData.x = surfaceMapping[newData.x];
  return newData;
}

float getGeometricTerm(vec3 surfacePosition, vec3 samplePosition, f16vec3 sampleNormal)
{
  float cellSize = NEECache.getCellSize() * 1;
  vec3 offset = samplePosition - surfacePosition;
  float cellDistance = length(offset);
  float cosTheta = lerp(1.0, abs(dot(sampleNormal, normalize(offset))), saturate(cellDistance / cellSize - 1));
  cellDistance = max(cellDistance, cellSize);
  return cosTheta / (cellDistance * cellDistance);
}

float getTriangleLight(uint2 data, vec3 cellCenter, inout RNG randomState)
{
  int surfaceIndex = data.x;
  int primitiveIndex = data.y;
  float triangleLight = 0;
  if (surfaceIndex >= 0 && surfaceIndex != BINDING_INDEX_INVALID && /*surfaceIndex < cb.surfaceCount &&*/ primitiveIndex >= 0)
  {
    vec3 triangleCenter;
    vec3 lightFlux;
    f16vec3  triangleNormal;
    getTriangleLightFlux(surfaceIndex, primitiveIndex, triangleCenter, triangleNormal, lightFlux);

    vec3 offset = triangleCenter - cellCenter;
    float cellSize = NEECache.getCellSize() * 0.5;
    float g = 0;
#if 0
    g = getGeometricTerm(cellCenter, triangleCenter, triangleNormal);
#else
    g += getGeometricTerm(cellCenter + vec3(cellSize, 0, 0), triangleCenter, triangleNormal);
    g += getGeometricTerm(cellCenter + vec3(-cellSize, 0, 0), triangleCenter, triangleNormal);
    g += getGeometricTerm(cellCenter + vec3(0, cellSize, 0), triangleCenter, triangleNormal);
    g += getGeometricTerm(cellCenter + vec3(0, -cellSize, 0), triangleCenter, triangleNormal);
    g += getGeometricTerm(cellCenter + vec3(0, 0, cellSize), triangleCenter, triangleNormal);
    g += getGeometricTerm(cellCenter + vec3(0, 0, -cellSize), triangleCenter, triangleNormal);
    g /= 6.0;
#endif
    triangleLight = calcBt709Luminance(lightFlux) * g;
    triangleLight = clamp(triangleLight, 0, floatMax / 32.0);
  }
  return triangleLight;
}

[shader("compute")]
[numthreads(RADIANCE_CACHE_ELEMENTS, 1, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  int3 cellID = threadIndex / int3(RADIANCE_CACHE_ELEMENTS,1,1);
  NEECell cell = NEECache.getCell(cellID);
  vec3 cellCenter = NEECache.cellToCenterPoint(cellID);

  RNG rng = createRNG(uvec2(threadIndex.xy /*+ threadIndex.z * RADIANCE_CACHE_ELEMENTS * 20 * 20*/), cb.frameIdx, threadIndex.z);

  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // load new id to shared memory
  int taskCount = cell.getTaskCount();
  int taskID = localIndex.x;  
  int2 task = taskID < taskCount ? mapIDData(cell.getTask(taskID)) : -1;
  s_srcTaskList[taskID] = task;

  // load old id to shared memory
  int oldIDCount = cell.getCandidateCount();
  if (localIndex.x < oldIDCount /*&& getNextSampleBlueNoise(rng) < 0.99*/)
  {
    int oldCount = 0;
    //InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_tarTaskList[localIndex.x] = mapIDData(cell.getCandidate(localIndex.x).getIDData());
  }

  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = oldIDCount;
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
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      if (all(s_tarTaskList[i] == task))
      {
        isNewTask = false;
      }
    }
    insertTask = isNewTask;
  }
  GroupMemoryBarrierWithGroupSync();

  if (insertTask /*&& s_tarTaskCount < RADIANCE_CACHE_ELEMENTS-1*/)
  {
    int oldCount = 0;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    //if (oldCount < RADIANCE_CACHE_ELEMENTS-1)
    {
      s_tarTaskList[oldCount] = task;
    }
  }
  GroupMemoryBarrierWithGroupSync();

  // get triangle light
  uint triangleID = localIndex.x;
  if (triangleID < s_tarTaskCount)
  {
    uint2 data = s_tarTaskList[triangleID];
    s_tarLight[triangleID] = getTriangleLight(data, cellCenter, rng);
  }
  triangleID = localIndex.x + RADIANCE_CACHE_ELEMENTS;
  if (triangleID < s_tarTaskCount)
  {
    uint2 data = s_tarTaskList[triangleID];
    s_tarLight[triangleID] = getTriangleLight(data, cellCenter, rng);
  }
  GroupMemoryBarrierWithGroupSync();

  // sort tasks
  if (localIndex.x == 0 && localIndex.y == 0)
  {
    if(!cb.enableNeeCacheRandomReplacement)
    {
      for (int i = 0; i < s_tarTaskCount-1; ++i)
      {
        for (int j = i+1; j < s_tarTaskCount; ++j)
        {
          if (s_tarLight[i] < s_tarLight[j])
          {
            float lightI = s_tarLight[i];
            s_tarLight[i] = s_tarLight[j];
            s_tarLight[j] = lightI;

            uint2 dataI = s_tarTaskList[i];
            s_tarTaskList[i] = s_tarTaskList[j];
            s_tarTaskList[j] = dataI;
          }
        }
      }
    }
    else
    {
      int validCount = min(s_tarTaskCount, NEECell.getMaxCandidateCount());
      int fixedCount = 0;//NEECell.getMaxCandidateCount() / 2;
      for (int i = NEECell.getMaxCandidateCount(); i < s_tarTaskCount; ++i)
      {
        //int targetID = (s_tarTaskList[i].y + cb.frameIdx) % validCount;//min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
        //int targetID = min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
        int targetID = fixedCount + (cb.frameIdx + i) % (validCount - fixedCount);
        float ft = s_tarLight[i];
        float fs = s_tarLight[targetID];
        float rnd = getNextSampleBlueNoise(rng);
        //rnd = rnd * 0.5 + float(cb.frameIdx & 0x1) * 0.5;
        if (rnd * fs < ft)
        {
          s_tarLight[targetID] = s_tarLight[i];
          s_tarTaskList[targetID] = s_tarTaskList[i];
        }
      }
    }

    s_tarTaskCount = cb.enableNeeCache != 0 ? min(s_tarTaskCount, NEECell.getMaxCandidateCount()) : 0;
    cell.setTaskCount(0);
    cell.setCandidateCount(s_tarTaskCount);

    float totalLight0 = 0;
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      //maxLight = max(maxLight, s_tarLight[i]);
      totalLight0 += s_tarLight[i];
    }
    float avgLight = s_tarTaskCount == 0 ? 0 : totalLight0 / s_tarTaskCount;
    float totalLight = 0;
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      totalLight += s_tarLight[i];
    }
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      if (cb.enableNeeCacheImportanceSampling > 0)
      {
        s_tarLight[i] = saturate(s_tarLight[i] / totalLight);
      }
      else
      {
        s_tarLight[i] = 1.0 / s_tarTaskCount;
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();

  // update nee cache
  if (localIndex.x < s_tarTaskCount)
  {
    int candidateID = localIndex.x;
    NEECandidate candidate = NEECandidate.create(s_tarTaskList[candidateID]);

    float sampleThreshold = 0;
    for (int i = 0; i <= candidateID; ++i)
    {
      sampleThreshold += s_tarLight[i];
    }
    candidate.setSampleThreshold(sampleThreshold);

    cell.setCandidate(candidateID, candidate);
  }
}
