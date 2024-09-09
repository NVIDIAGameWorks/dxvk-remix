/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtx/external/NRD.slangh"

#define UPDATE_NEE_CACHE 1
#include "rtx/algorithm/nee_cache.h"

#define INVALID_ID 0xffffff

#define CANDIDATE_LENGTH NEE_CACHE_ELEMENTS * 2
#define CANDIDATE_GROUP_SIZE 16
#define BATCH_SIZE 8
groupshared uint2 s_candidateList[CANDIDATE_LENGTH][BATCH_SIZE];
groupshared float s_candidateLight[CANDIDATE_LENGTH][BATCH_SIZE];
groupshared int s_candidateCount[BATCH_SIZE];

int convertToThisFramePrefixSumID(int lastID, out int surfaceID, out int primitiveID)
{
  surfaceID = NEE_CACHE_INVALID_ID;
  primitiveID = NEE_CACHE_INVALID_ID;
  if (lastID == NEE_CACHE_INVALID_ID)
  {
    return NEE_CACHE_INVALID_ID;
  }
  int surfaceIDLast, primitiveIDLast;
  if (!NEECacheUtils.convertPrefixSumIDToID(lastID, LastPrimitiveIDPrefixSum, surfaceIDLast, primitiveIDLast))
  {
    return NEE_CACHE_INVALID_ID;
  }

  int surfaceIDThis = NEECacheUtils.convertToCurrentSurfaceID(surfaceIDLast);
  surfaceID = surfaceIDThis;
  primitiveID = primitiveIDLast;
  return NEECacheUtils.convertIDToPrefixSumID(surfaceIDThis, primitiveIDLast, PrimitiveIDPrefixSum);
}

int convertLightIndex(int lightIndex)
{
  if (lightIndex == NEE_CACHE_INVALID_ID)
  {
    return NEE_CACHE_INVALID_ID;
  }

  bool currentToPrevious = false;
  return lightMapping[lightIndex + (currentToPrevious ? 0 : cb.lightCount)];
}

// Sort tasks in descending order based on y component
void sortTask(uint batchIndex)
{
  int listCount = min(32, s_candidateCount[batchIndex]);
  int totalCount = 0;
  for (int i = 0; i < listCount; ++i)
  {
    bool sorted = true;
    for (int j = i+1; j < listCount; ++j)
    {
      if (sorted && s_candidateList[j-1][batchIndex].y < s_candidateList[j][batchIndex].y)
      {
        sorted = false;
      }

      if (s_candidateList[i][batchIndex].y < s_candidateList[j][batchIndex].y)
      {
        uint2 tempCandidate = s_candidateList[i][batchIndex];
        s_candidateList[i][batchIndex] = s_candidateList[j][batchIndex];
        s_candidateList[j][batchIndex] = tempCandidate;
      }
    }

    if (s_candidateList[i][batchIndex].y == 0 || sorted)
    {
      break;
    }
  }
}

// Clear task list in shared memory
void clearTask(uint2 localIndex)
{
  for (int i = localIndex.x; i < CANDIDATE_LENGTH; i += CANDIDATE_GROUP_SIZE)
  {
    s_candidateList[i][localIndex.y] = 0;
  }

  if (localIndex.x == 0)
  {
    s_candidateCount[localIndex.y] = 0;
  }
}

// Put a new task to shared memory.
void insertTask(uint2 value, uint batchIndex)
{
  int oldCount = 0;
  InterlockedAdd(s_candidateCount[batchIndex], 1, oldCount);
  oldCount = min(oldCount, CANDIDATE_LENGTH - 1);
  s_candidateList[oldCount][batchIndex] = value;
}

// Insert this frame's new tasks, merge them if there is an existing one.
void mergeTask(uint2 value, uint existingCount, uint batchIndex)
{
  if (value.x == NEE_CACHE_INVALID_ID || value.y == 0)
  {
    return;
  }

  for (int i = 0; i < existingCount; ++i)
  {
    if (s_candidateList[i][batchIndex].x == value.x)
    {
      s_candidateList[i][batchIndex].y += value.y;
      return;
    }
  }

  insertTask(value, batchIndex);
}

// Remove unimportant tasks and calculate normalized light contribution.
int finalizeTask(uint batchIndex)
{
  // Calculate total light contribution
  float totalLight = 0;
  int count = s_candidateCount[batchIndex];
  for (int i = 0; i < count; ++i)
  {
    totalLight += float(s_candidateList[i][batchIndex].y);
  }

  // Cull unimportant tasks
  int validCount = 0;
  float validTotalLight = 0;
  for (int i = 0; i < count; ++i)
  {
    if (s_candidateList[i][batchIndex].y > totalLight * cb.neeCacheArgs.cullingThreshold)
    {
      validTotalLight += float(s_candidateList[i][batchIndex].y);
      validCount++;
    }
  }
  s_candidateCount[batchIndex] = validCount;

  // Calculate light
  for (int i = 0; i < validCount; ++i)
  {
    s_candidateLight[i][batchIndex] = validTotalLight > 0 ? saturate(float(s_candidateList[i][batchIndex].y) / validTotalLight) : 0.0;
  }
  return validCount;
}

void updateTriangleTask(NEECell cell, uint2 localIndex)
{
  clearTask(localIndex);

  GroupMemoryBarrierWithGroupSync();

  // load old id to shared memory
  if (cb.neeCacheArgs.clearCache == 0)
  {
    int taskID = localIndex.x;
    uint2 value = cell.getSlotTaskValue(taskID);
    int surfaceID, primitiveID;
    uint oldRange = (value.x & 0xff000000);
    value.x = convertToThisFramePrefixSumID(value.x & 0xffffff, surfaceID, primitiveID);
    value.x |= oldRange;
    int delta = max(value.y * cb.neeCacheArgs.learningRate, 1);
    value.y = clamp(int(value.y) - delta, 0, 1 << 25);
    if (value.x != NEE_CACHE_INVALID_ID && value.y > 0)
    {
      insertTask(value, localIndex.y);
    }
  }

  GroupMemoryBarrierWithGroupSync();

  int maxDeltaValue = max(100, s_candidateList[0][localIndex.y].y);
  int existingCount = s_candidateCount[localIndex.y];

  // insert new task
  for (int i = localIndex.x; i < NEE_CACHE_HASH_TASK_COUNT; i += CANDIDATE_GROUP_SIZE)
  {
    uint2 value = cell.getHashSlotTaskValue(i);
    if (cb.neeCacheArgs.clearCache != 0 || cell.isLightTask(value))
    {
      continue;
    }
    value.x &= 0xffffff;
    int surfaceID, primitiveID;
    value.x = convertToThisFramePrefixSumID(value.x, surfaceID, primitiveID);
    value.y = min(maxDeltaValue, value.y);

    // Remove non-emissive triangles, these triangles may be in the candidate list because of large mipmap footprint caused by diffuse ray
    if (value.x != NEE_CACHE_INVALID_ID)
    {
      vec3 triangleCenter, lightIntensity;
      f16vec3 triangleNormal;
      NEECacheUtils.calculateTriangleLightIntensity(surfaceID, primitiveID, triangleCenter, triangleNormal, lightIntensity);
      if (all(lightIntensity == 0))
      {
        value.x = NEE_CACHE_INVALID_ID;
      }
    }

    // Search if the given triangle is inside an existing candidate range.
    // If not, insert it to the candidate list.
    if (value.x != NEE_CACHE_INVALID_ID && value.y != 0)
    {
      bool found = false;
      for (int j = 0; j < existingCount; ++j)
      {
        int firstPrimitiveID = (s_candidateList[j][localIndex.y].x & 0xffffff);
        int range = (s_candidateList[j][localIndex.y].x >> 24);
        if (value.x >= firstPrimitiveID && value.x < firstPrimitiveID + range)
        {
          s_candidateList[j][localIndex.y].y += value.y;
          found = true;
          break;
        }
      }

      if (!found)
      {
        value.x = ((1 << 24) | value.x);
        insertTask(value, localIndex.y);
      }
    }

  }


  GroupMemoryBarrierWithGroupSync();

  // Change format. Put primitive ID to y component for sorting.
  for (int i = localIndex.x; i < NEE_CACHE_HASH_TASK_COUNT && cb.neeCacheArgs.triangleExplorationProbability > 0; i += CANDIDATE_GROUP_SIZE)
  {
    uint2 data = s_candidateList[i][localIndex.y];
    uint range = (data.x >> 24);
    uint primitiveID = (data.x & 0xffffff);
    uint2 newData = uint2(data.y, (primitiveID << 8) | range);
    s_candidateList[i][localIndex.y] = newData;
  }

  GroupMemoryBarrierWithGroupSync();

  // sort task based on primitive id
  if (localIndex.x == 0 && cb.neeCacheArgs.triangleExplorationProbability > 0)
  {
    // Sort candidates based on their triangle IDs, which is in the y component.
    sortTask(localIndex.y);

    int count = s_candidateCount[localIndex.y];

    uint totalValue = 0;
    for (int i = 0; i < count; i++)
    {
      uint2 data = s_candidateList[i][localIndex.y];
      uint newRange = (data.y & 0xff);
      uint newPrefixSumID = (data.y >> 8);
      uint newValue = data.x;
      totalValue += newValue;
    }
    uint maxMergeValue = totalValue * 0.1;
    int oldSurfaceID = -1;
    int oldPrimitiveID = -1;
    int oldPrefixSumID = -1;
    int oldRange = 0;
    int oldIndex = -1;
    int oldValue = 0;
    for (int i = 0; i < count; i++)
    {
      uint2 data = s_candidateList[i][localIndex.y];
      uint newRange = (data.y & 0xff);
      uint newPrefixSumID = (data.y >> 8);
      uint newValue = data.x;

      int newSurfaceID, newPrimitiveID;
      NEECacheUtils.convertPrefixSumIDToID(newPrefixSumID, PrimitiveIDPrefixSum, newSurfaceID, newPrimitiveID);
      int primitiveCount = NEECacheUtils.getSurfacePrimitiveCount(newSurfaceID, PrimitiveIDPrefixSum);
      int gab = min(cb.neeCacheArgs.triangleExplorationMaxRange, cb.neeCacheArgs.triangleExplorationRangeRatio * primitiveCount) * 
        cb.neeCacheArgs.triangleExplorationAcceptRangeRatio + 1;
      if (newSurfaceID == oldSurfaceID && oldPrimitiveID < newPrimitiveID + newRange + gab && oldPrimitiveID + oldRange - newPrimitiveID < 255 &&
        oldValue + newValue < maxMergeValue)
      {
        // Merge
        oldRange = oldPrimitiveID + oldRange - newPrimitiveID;
        oldPrefixSumID = newPrefixSumID;
        oldPrimitiveID = newPrimitiveID;
        oldValue += newValue;
        s_candidateList[oldIndex][localIndex.y] = uint2((oldRange << 24) | oldPrefixSumID, oldValue);
        newValue = 0;
        newRange = 0;
      }
      else
      {
        // New entry
        oldSurfaceID = newSurfaceID;
        oldPrimitiveID = newPrimitiveID;
        oldPrefixSumID = newPrefixSumID;
        oldRange = newRange;
        oldIndex = i;
        oldValue = newValue;
      }

      s_candidateList[i][localIndex.y] = uint2((newRange << 24) | newPrefixSumID, newValue);
    }
  }

  GroupMemoryBarrierWithGroupSync();

  // sort tasks
  if (localIndex.x == 0)
  {
    sortTask(localIndex.y);
    s_candidateCount[localIndex.y] = cb.neeCacheArgs.enable != 0 ? min(s_candidateCount[localIndex.y], NEECell.getMaxCandidateCount()) : 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // Force every candidate contain only 1 triangle, if triangle exploration is disabled.
  if (cb.neeCacheArgs.triangleExplorationProbability == 0.0)
  {
    uint2 task = s_candidateList[localIndex.x][localIndex.y];
    task.x = (task.x & 0xffffff) | (1 << 24);

    s_candidateList[localIndex.x][localIndex.y].x = task.x;
    GroupMemoryBarrierWithGroupSync();
  }

  cell.setSlotTaskValue(localIndex.x, s_candidateList[localIndex.x][localIndex.y]);

  if (localIndex.x == 0)
  {
    int validCount = finalizeTask(localIndex.y);
    cell.setCandidateCount(validCount);
  }
  GroupMemoryBarrierWithGroupSync();

  // Update candidate
  if (localIndex.x < s_candidateCount[localIndex.y])
  {
    int candidateID = localIndex.x;
    uint2 task = s_candidateList[candidateID][localIndex.y];
    int surfaceID, primitiveID;
    NEECacheUtils.convertPrefixSumIDToID(task.x & 0xffffff, PrimitiveIDPrefixSum, surfaceID, primitiveID);
    NEECandidate candidate = NEECandidate.create(surfaceID, primitiveID, max(1, task.x >> 24));

    candidate.setSampleProbability(s_candidateLight[candidateID][localIndex.y]);
    cell.setCandidate(candidateID, candidate);
  }
}

void updateTriangleSample(NEECell cell, uint2 localIndex, uint3 threadIndex)
{
  // Update sample
  vec3 cameraPosition = cameraGetWorldPosition(cb.camera);
  RNG rng = createRNG(uvec2(threadIndex.xy + threadIndex.z * NEE_CACHE_ELEMENTS * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION), cb.frameIdx, threadIndex.z);
  for (int i = localIndex.x; i < NEE_CACHE_SAMPLES; i += NEE_CACHE_ELEMENTS)
  {
    float lightObjectPdf = 0;
    NEESample sample = NEESample.createEmpty();
    float randomNumber = float(i + getNextSampleBlueNoise(rng)) / NEE_CACHE_SAMPLES;
    NEECandidate candidate = cell.sampleCandidate(randomNumber, lightObjectPdf);
    if (candidate.isValid())
    {
      float16_t spreadAngle = float16_t(cb.screenSpacePixelSpreadHalfAngle);
      float16_t coneRadius = 0.0;
      vec3 uvw = vec3(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng));
      float area;
      int primitiveID = candidate.getPrimitiveID();
      LightSample lightSample = NEECacheUtils.calculateLightSampleFromCandidate(
        candidate.getSurfaceID(), primitiveID, candidate.getRange(), PrimitiveIDPrefixSum, uvw, lightObjectPdf,
        /*cellCenter*/ 0, coneRadius, spreadAngle, area, false, kFootprintFromTextureCoordDiff);

      sample.position = lightSample.position;
      sample.normal = lightSample.normal;
      sample.pdf = lightSample.solidAnglePdf;
      sample.radiance = lightSample.radiance;
      sample.triangleID = NEECacheUtils.convertIDToPrefixSumID(candidate.getSurfaceID(), primitiveID, PrimitiveIDPrefixSum);
    }
    cell.setSample(i, sample);
  }
  GroupMemoryBarrierWithGroupSync();
}

void updateLightTask(NEECell cell, uint2 localIndex)
{
  // Update light candadate
  clearTask(localIndex);

  GroupMemoryBarrierWithGroupSync();

  if (cb.neeCacheArgs.clearCache == 0)
  {
    uint2 value = cell.getLightSlotTaskValue(localIndex.x);
    value.x = convertLightIndex(value.x);
    int delta = max(value.y * cb.neeCacheArgs.learningRate, 1);
    value.y = clamp(int(value.y) - delta, 0, 1 << 25);

    if (value.x != NEE_CACHE_INVALID_ID && value.y > 0)
    {
      insertTask(value, localIndex.y);
    }
  }

  GroupMemoryBarrierWithGroupSync();
  int maxDeltaValue = max(100, s_candidateList[0][localIndex.y].y);
  int existingCount = s_candidateCount[localIndex.y];

  // insert new light task
  for (int i = localIndex.x; i < NEE_CACHE_HASH_TASK_COUNT; i += CANDIDATE_GROUP_SIZE)
  {
    uint2 value = cell.getHashSlotTaskValue(i);
    if (cb.neeCacheArgs.clearCache != 0 || !cell.isLightTask(value))
    {
      continue;
    }
    value.x &= 0xffffff;
    value.x = convertLightIndex(value.x);
    value.y = min(maxDeltaValue, value.y);

    mergeTask(value, existingCount, localIndex.y);
  }

  GroupMemoryBarrierWithGroupSync();

  // sort tasks
  if (localIndex.x == 0)
  {
    sortTask(localIndex.y);
    s_candidateCount[localIndex.y] = cb.neeCacheArgs.enable != 0 ? min(s_candidateCount[localIndex.y], NEECell.getMaxCandidateCount()) : 0;
  }
  GroupMemoryBarrierWithGroupSync();

  cell.setLightSlotTaskValue(localIndex.x, s_candidateList[localIndex.x][localIndex.y]);
  for (int i = localIndex.x; i < NEE_CACHE_HASH_TASK_COUNT; i += CANDIDATE_GROUP_SIZE)
  {
    cell.setHashSlotTaskValue(i, 0);
  }

  if (localIndex.x == 0)
  {
    int validCount = finalizeTask(localIndex.y);
    cell.setLightCandidateCount(validCount);
  }
  GroupMemoryBarrierWithGroupSync();

  // update light candidate
  if (localIndex.x < s_candidateCount[localIndex.y])
  {
    int candidateID = localIndex.x;
    uint2 task = s_candidateList[candidateID][localIndex.y];
    NEELightCandidate candidate = NEELightCandidate.create(task.x & 0xffffff);

    MemoryPolymorphicLight memoryPolymorphicLight = lights[candidate.getLightID()];
    candidate.setOffset(memoryPolymorphicLightGetPosition(memoryPolymorphicLight) - NEECache.getCenter());
    candidate.setRadiance(s_candidateLight[localIndex.x][localIndex.y]);

    cell.setLightCandidate(candidateID, candidate);
  }
}

[shader("compute")]
[numthreads(CANDIDATE_GROUP_SIZE, BATCH_SIZE, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  int3 cellID = threadIndex / int3(NEE_CACHE_ELEMENTS, 1, 1);
  int cellOffset = cellID.z * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION +
    cellID.y * NEE_CACHE_PROBE_RESOLUTION +
    cellID.x;
  NEECell cell = NEECache.getCell(cellOffset);

  updateTriangleTask(cell, localIndex);

  updateTriangleSample(cell, localIndex, threadIndex);

  updateLightTask(cell, localIndex);
}