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

groupshared uint2 s_srcTaskList[NEE_CACHE_ELEMENTS][8];
groupshared uint2 s_candidateList[NEE_CACHE_ELEMENTS * 2][8];
groupshared float s_candidateLight[NEE_CACHE_ELEMENTS * 2][8];
groupshared int s_candidateCount[8];

uint2 mapIDData(uint2 data)
{
  uint2 newData = data;
  newData.x = surfaceMapping[newData.x];
  return newData;
}

[shader("compute")]
[numthreads(NEE_CACHE_ELEMENTS, 8, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  int3 cellID = threadIndex / int3(NEE_CACHE_ELEMENTS,1,1);
  NEECell cell = NEECache.getCell(cellID);
  vec3 cellCenter = NEECache.cellToCenterPoint(cellID);

  RNG rng = createRNG(uvec2(threadIndex.xy + threadIndex.z * NEE_CACHE_ELEMENTS * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION), cb.frameIdx, threadIndex.z);

  if (localIndex.x == 0)
  {
    s_candidateCount[localIndex.y] = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // load new id to shared memory
  int taskCount = cell.getTaskCount();
  int taskID = localIndex.x;
  int encodedTask = taskID < taskCount ? cell.getTask(taskID) : -1;
  int2 task = -1;
  bool isLightSample = NEECache.isAnalyticalLight(encodedTask);
  if (!isLightSample && !NEECacheUtils.convertPrefixSumIDToID(encodedTask, PrimitiveIDPrefixSum, task.x, task.y))
  {
    task = -1;
  }
  s_srcTaskList[taskID][localIndex.y] = task;

  // load old id to shared memory
  int oldIDCount = cell.getCandidateCount();
  if (localIndex.x < oldIDCount)
  {
    int oldCount = 0;
    NEECandidate candidate = cell.getCandidate(localIndex.x);
    uint newSurfaceID = NEECacheUtils.convertToCurrentSurfaceID(candidate.getSurfaceID());
    candidate.setSurfaceID(newSurfaceID);
    candidate.setAge(candidate.getAge() + 1);
    s_candidateList[localIndex.x][localIndex.y] = candidate.m_data;
  }

  if (localIndex.x == 0)
  {
    s_candidateCount[localIndex.y] = oldIDCount;
  }
  GroupMemoryBarrierWithGroupSync();

  // merge tasks
  bool insertTask = false;
  if (taskID < taskCount && all(task != -1))
  {
    bool isNewTask = true;
    for (int i = 0; i < s_candidateCount[localIndex.y]; ++i)
    {
      NEECandidate candidate = NEECandidate.create(s_candidateList[i][localIndex.y]);
      if (all(candidate.getIDData() == task))
      {
        candidate.setAge(0);
        s_candidateList[i][localIndex.y] = candidate.m_data;
        isNewTask = false;
      }
    }
    insertTask = isNewTask;
  }
  GroupMemoryBarrierWithGroupSync();

  if (insertTask)
  {
    int oldCount = 0;
    InterlockedAdd(s_candidateCount[localIndex.y], 1, oldCount);
    NEECandidate newCandidate = NEECandidate.create(task.x, task.y);
    s_candidateList[oldCount][localIndex.y] = newCandidate.m_data;
  }
  GroupMemoryBarrierWithGroupSync();

  // get triangle light
  float neeCellSize = NEECache.getCellSize();
  int ithTriangle = localIndex.x;
  if (ithTriangle < s_candidateCount[localIndex.y])
  {
    NEECandidate candidate = NEECandidate.create(s_candidateList[ithTriangle][localIndex.y]);
    s_candidateLight[ithTriangle][localIndex.y] = NEECacheUtils.calculateTriangleLightToCell(candidate.getIDData(), candidate.getAge(), cellCenter, neeCellSize);
  }
  ithTriangle = localIndex.x + NEE_CACHE_ELEMENTS;
  if (ithTriangle < s_candidateCount[localIndex.y])
  {
    NEECandidate candidate = NEECandidate.create(s_candidateList[ithTriangle][localIndex.y]);
    s_candidateLight[ithTriangle][localIndex.y] = NEECacheUtils.calculateTriangleLightToCell(candidate.getIDData(), candidate.getAge(), cellCenter, neeCellSize);
  }
  GroupMemoryBarrierWithGroupSync();

  // sort tasks
  if (localIndex.x == 0)
  {
    for (int i = 0; i < s_candidateCount[localIndex.y]-1; ++i)
    {
      for (int j = i+1; j < s_candidateCount[localIndex.y]; ++j)
      {
        if (s_candidateLight[i][localIndex.y] < s_candidateLight[j][localIndex.y])
        {
          float lightI = s_candidateLight[i][localIndex.y];
          s_candidateLight[i][localIndex.y] = s_candidateLight[j][localIndex.y];
          s_candidateLight[j][localIndex.y] = lightI;

          uint2 tempCandidate = s_candidateList[i][localIndex.y];
          s_candidateList[i][localIndex.y] = s_candidateList[j][localIndex.y];
          s_candidateList[j][localIndex.y] = tempCandidate;
        }
      }
    }

    s_candidateCount[localIndex.y] = cb.neeCacheArgs.enable != 0 ? min(s_candidateCount[localIndex.y], NEECell.getMaxCandidateCount()) : 0;
    cell.setCandidateCount(s_candidateCount[localIndex.y]);

    float totalLight = 0;
    for (int i = 0; i < s_candidateCount[localIndex.y]; ++i)
    {
      totalLight += s_candidateLight[i][localIndex.y];
    }
    for (int i = 0; i < s_candidateCount[localIndex.y]; ++i)
    {
      if (cb.neeCacheArgs.enableImportanceSampling > 0)
      {
        s_candidateLight[i][localIndex.y] = saturate(s_candidateLight[i][localIndex.y] / totalLight);
      }
      else
      {
        s_candidateLight[i][localIndex.y] = 1.0 / s_candidateCount[localIndex.y];
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();

  // update nee cache
  if (localIndex.x < s_candidateCount[localIndex.y])
  {
    int candidateID = localIndex.x;
    NEECandidate candidate = NEECandidate.create(s_candidateList[candidateID][localIndex.y]);

    float sampleThreshold = 0;
    for (int i = 0; i <= candidateID; ++i)
    {
      sampleThreshold += s_candidateLight[i][localIndex.y];
    }
    candidate.setSampleThreshold(sampleThreshold);

    cell.setCandidate(candidateID, candidate);
  }

  GroupMemoryBarrierWithGroupSync();

  vec3 cameraPosition = cameraGetWorldPosition(cb.camera);
  for (int i = localIndex.x; i < NEE_CACHE_SAMPLES; i += NEE_CACHE_ELEMENTS)
  {
    float lightObjectPdf = 0;
    NEESample sample = NEESample.createEmpty();
    float randomNumber = float(i + getNextSampleBlueNoise(rng)) / NEE_CACHE_SAMPLES;
    NEECandidate candidate = cell.sampleCandidate(randomNumber, lightObjectPdf);
    if (candidate.isValid())
    {
      float16_t spreadAngle = float16_t(cb.screenSpacePixelSpreadHalfAngle);
      float16_t coneRadius = spreadAngle * length(cellCenter - cameraPosition);
      vec2 uv = vec2(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng));
      LightSample lightSample = NEECacheUtils.calculateLightSampleFromTriangle(
        candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, 
        cellCenter, coneRadius, spreadAngle, false, kFootprintFromTextureCoordDiff);

      sample.position = lightSample.position;
      sample.normal = lightSample.normal;
      sample.pdf = lightSample.solidAnglePdf;
      sample.radiance = lightSample.radiance;
    }
    cell.setSample(i, sample);
  }

  // update light candidate
  // load new id to shared memory
  task = -1;
  if (isLightSample)
  {
    task = NEECache.decodeAnalyticalLight(encodedTask);
    task = RAB_TranslateLightIndex(task.x, false);
    if (task.x == RTXDI_INVALID_LIGHT_INDEX)
    {
      task = -1;
    }
  }
  taskID = localIndex.x;
  s_srcTaskList[taskID][localIndex.y] = task;

  // load old id to shared memory
  oldIDCount = cell.getLightCandidateCount();
  if (localIndex.x < oldIDCount)
  {
    int oldCount = 0;
    NEELightCandidate candidate = cell.getLightCandidate(localIndex.x);
    int lightID = candidate.getLightID();
    lightID = RAB_TranslateLightIndex(lightID, false);
    if (lightID == RTXDI_INVALID_LIGHT_INDEX)
    {
      lightID = -1;
    }
    candidate.setLightID(lightID);
    candidate.setAge(candidate.getAge() + 1);
    s_candidateList[localIndex.x][localIndex.y] = candidate.m_data;
  }

  if (localIndex.x == 0)
  {
    s_candidateCount[localIndex.y] = oldIDCount;
  }
  GroupMemoryBarrierWithGroupSync();

  // merge tasks
  insertTask = false;
  if (taskID < taskCount && all(task != -1))
  {
    bool isNewTask = true;
    for (int i = 0; i < taskID -1; ++i)
    {
      if (all(s_srcTaskList[i][localIndex.y] == task))
      {
        isNewTask = false;
      }
    }
    for (int i = 0; i < s_candidateCount[localIndex.y]; ++i)
    {
      NEELightCandidate candidate = NEELightCandidate.createFromPacked(s_candidateList[i][localIndex.y]);
      if (all(candidate.getLightID() == task.x))
      {
        candidate.setAge(0);
        s_candidateList[i][localIndex.y] = candidate.m_data;
        isNewTask = false;
      }
    }
    insertTask = isNewTask;
  }
  GroupMemoryBarrierWithGroupSync();

  if (insertTask)
  {
    int oldCount = 0;
    InterlockedAdd(s_candidateCount[localIndex.y], 1, oldCount);
    NEELightCandidate newCandidate = NEELightCandidate.create(task.x);
    s_candidateList[oldCount][localIndex.y] = newCandidate.m_data;
  }
  GroupMemoryBarrierWithGroupSync();

  // get triangle light
  float cellSize = NEECache.getCellSize();
  uint triangleID = localIndex.x;
  if (triangleID < s_candidateCount[localIndex.y])
  {
    NEELightCandidate candidate = NEELightCandidate.createFromPacked(s_candidateList[triangleID][localIndex.y]);

    float16_t radiance;
    vec3 offset;
    NEECacheUtils.calculateLightSampleInfo(candidate.getLightID(), vec2(0.5), candidate.getAge(), cellCenter, cellSize, radiance, offset);
    candidate.setRadiance(radiance);
    candidate.setOffset(offset);

    s_candidateList[triangleID][localIndex.y] = candidate.m_data;
    s_candidateLight[triangleID][localIndex.y] = radiance;
  }
  triangleID = localIndex.x + NEE_CACHE_ELEMENTS;
  if (triangleID < s_candidateCount[localIndex.y])
  {
    NEELightCandidate candidate = NEELightCandidate.createFromPacked(s_candidateList[triangleID][localIndex.y]);

    float16_t radiance;
    vec3 offset;
    NEECacheUtils.calculateLightSampleInfo(candidate.getLightID(), vec2(0.5), candidate.getAge(), cellCenter, cellSize, radiance, offset);
    candidate.setRadiance(radiance);
    candidate.setOffset(offset);

    s_candidateList[triangleID][localIndex.y] = candidate.m_data;
    s_candidateLight[triangleID][localIndex.y] = radiance;
  }
  GroupMemoryBarrierWithGroupSync();

  // sort tasks
  if (localIndex.x == 0)
  {
    for (int i = 0; i < s_candidateCount[localIndex.y]-1; ++i)
    {
      for (int j = i+1; j < s_candidateCount[localIndex.y]; ++j)
      {
        if (s_candidateLight[i][localIndex.y] < s_candidateLight[j][localIndex.y])
        {
          float lightI = s_candidateLight[i][localIndex.y];
          s_candidateLight[i][localIndex.y] = s_candidateLight[j][localIndex.y];
          s_candidateLight[j][localIndex.y] = lightI;

          uint2 tempCandidate = s_candidateList[i][localIndex.y];
          s_candidateList[i][localIndex.y] = s_candidateList[j][localIndex.y];
          s_candidateList[j][localIndex.y] = tempCandidate;
        }
      }
    }

    int totalLightCount = cb.neeCacheArgs.enable != 0 ? min(s_candidateCount[localIndex.y], NEECell.getMaxLightCandidateCount()) : 0;
    float totalLight = 0;
    for (int i = 0; i < totalLightCount; ++i)
    {
      totalLight += s_candidateLight[i][localIndex.y];
    }

    float cullingThreshold = totalLight * cb.neeCacheArgs.cullingThreshold;
    int validLightCount = 0;
    float validTotalLight = 0;
    for (int i = 0; i < totalLightCount; ++i)
    {
      if (s_candidateLight[i][localIndex.y] > cullingThreshold)
      {
        validTotalLight += s_candidateLight[i][localIndex.y];
        validLightCount++;
      }
    }
    totalLight = validTotalLight;
    totalLightCount = validLightCount;
    s_candidateCount[localIndex.y] = totalLightCount;
    cell.setLightCandidateCount(totalLightCount);
  }
  GroupMemoryBarrierWithGroupSync();

  // update nee cache
  if (localIndex.x < s_candidateCount[localIndex.y])
  {
    int candidateID = localIndex.x;
    NEELightCandidate candidate = NEELightCandidate.createFromPacked(s_candidateList[candidateID][localIndex.y]);
    cell.setLightCandidate(candidateID, candidate);
  }

  if (localIndex.x == 0)
  {
    cell.clearTasks();
  }
}
