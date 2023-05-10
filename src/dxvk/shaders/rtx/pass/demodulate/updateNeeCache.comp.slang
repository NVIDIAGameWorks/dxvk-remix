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

float getTriangleLight(uint2 data, vec3 cellCenter, inout RNG randomState)
{
  int surfaceIndex = data.x;
  int primitiveIndex = data.y;// = int4(rayInteraction.surfaceIndex, rayInteraction.primitiveIndex,0,0);
  float triangleLight = 0;
  if (surfaceIndex >= 0 && primitiveIndex >= 0)
  {
    SURFACE_CREATE_READ(emissiveSurface, surfaceIndex, surfaces);
    RayInteraction emissiveRayInteracton = {};// rayInteraction;
    emissiveRayInteracton.frontHit = true;
    emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(uvToBary(vec2(0.3)));
    //emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(uvToBary(vec2(getNextSampleBlueNoise(randomState), getNextSampleBlueNoise(randomState))));
    emissiveRayInteracton.primitiveIndex = primitiveIndex;
    Ray ray;
    ray.direction = f16vec3(1.0);
    SurfaceInteraction emissiveSurfaceInteraction = surfaceInteractionCreate(emissiveSurface, emissiveRayInteracton, ray);
    emissiveSurfaceInteraction.textureGradientX = 0.1;
    emissiveSurfaceInteraction.textureGradientY = 0.1;

    const MemoryPolymorphicSurfaceMaterial memoryPolymorphicSurfaceMaterial = surfaceMaterials[emissiveSurface.surfaceMaterialIndex];
    const uint8_t polymorphicType = memoryPolymorphicSurfaceMaterialGetTypeHelper(memoryPolymorphicSurfaceMaterial);

    if (polymorphicType == surfaceMaterialTypeOpaque)
    {
      const OpaqueSurfaceMaterial emissiveOpaqueSurfaceMaterial = opaqueSurfaceMaterialCreate(emissiveSurface.surfaceMaterialIndex, memoryPolymorphicSurfaceMaterial);
      const OpaqueSurfaceMaterialInteraction emissiveOpaqueSurfaceMaterialInteraction =
        opaqueSurfaceMaterialInteractionCreate(emissiveOpaqueSurfaceMaterial, emissiveSurface, emissiveSurfaceInteraction, emissiveRayInteracton);

      if (any(emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance > f16vec3(0)))
      {
        vec3 offset = emissiveSurfaceInteraction.position - cellCenter;
        float cellSize = NEECache.getCellSize() * 0.5;//cb.debugKnob.z > 0 ?  : 0;
        float distSq = max(dot(offset, offset), cellSize * cellSize);
        float cosTheta = abs(dot(emissiveSurfaceInteraction.triangleNormal, normalize(offset)));
        float triangleArea = emissiveSurfaceInteraction.sqrtTriangleArea * emissiveSurfaceInteraction.sqrtTriangleArea;
        vec3 radiance = emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance;
        //float luminance = max(radiance.x, max(radiance.y, radiance.z));
        float luminance = calcBt709Luminance(emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance);
        triangleLight = luminance * triangleArea * cosTheta / distSq;
        //triangleLight = 1.0 * emissiveSurfaceInteraction.triangleArea / distSq;
        //triangleLight = 1.0 / distSq;
        //triangleLight = log(1 + triangleLight);
        //triangleLight = sqrt(triangleLight);
      }
    }
  }
  return triangleLight;
}

[shader("compute")]
[numthreads(RADIANCE_CACHE_ELEMENTS, 1, 1)]
void main(uint3 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  int3 cellID = threadIndex / int3(RADIANCE_CACHE_ELEMENTS,1,1);
  NEECell cell = NEECache.createCell(cellID);
  vec3 cellCenter = NEECache.cellToPoint(cellID);

  RNG rng = createRNG(uvec2(threadIndex.xy /*+ threadIndex.z * RADIANCE_CACHE_ELEMENTS * 20 * 20*/), cb.frameIdx, threadIndex.z);

  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // load new id to shared memory
  int taskCount = cell.getTaskCount();
  int taskID = localIndex.x;  
  int2 task = cell.getTask(taskID);
  s_srcTaskList[taskID] = task;

  // load old id to shared memory
  int oldIDCount = cell.getCandidateCount();
  if (localIndex.x < oldIDCount /*&& getNextSampleBlueNoise(rng) < 0.99*/)
  {
    int oldCount = 0;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_tarTaskList[oldCount] = cell.getCandidate(localIndex.x).getIDData();
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
    // if (cb.debugKnob.z == 0)
    if(0)
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
      for (int i = NEECell.getMaxCandidateCount(); i < s_tarTaskCount; ++i)
      {
        //int targetID = (s_tarTaskList[i].y + 0) % validCount;//min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
        //int targetID = min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
        int targetID = (cb.frameIdx + i) % validCount;
        float ft = s_tarLight[i];
        float fs = s_tarLight[targetID];
        if (getNextSampleBlueNoise(rng) * fs < ft)
        {
          s_tarLight[targetID] = s_tarLight[i];
          s_tarTaskList[targetID] = s_tarTaskList[i];
        }
      }
    }

    s_tarTaskCount = cb.debugKnob.x != 0 ? min(s_tarTaskCount, NEECell.getMaxCandidateCount()) : 0;
    cell.setTaskCount(0);
    cell.setCandidateCount(s_tarTaskCount);

    float maxLight = 0;
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      maxLight = max(maxLight, s_tarLight[i]);
    }
    float totalLight = 0;
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      //if (cb.debugKnob.z > 0) s_tarLight[i] = 1;//sqrt(s_tarLight[i]);//clamp(s_tarLight[i],0.1,1);
      //s_tarLight[i] = pow(s_tarLight[i], 0.3);
      //s_tarLight[i] = max(maxLight * cb.debugKnob.z, s_tarLight[i]);
      totalLight += s_tarLight[i];
    }
    for (int i = 0; i < s_tarTaskCount; ++i)
    {
      if (cb.debugKnob.y > 0)
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
