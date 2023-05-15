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

// groupshared uint2 s_srcTaskList[RADIANCE_CACHE_ELEMENTS];
// groupshared uint2 s_tarTaskList[RADIANCE_CACHE_ELEMENTS * 2];
// groupshared float s_tarLight[RADIANCE_CACHE_ELEMENTS * 2];
groupshared uint3 s_task[16*8];
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
  int primitiveIndex = data.y;// = int4(rayInteraction.surfaceIndex, rayInteraction.primitiveIndex,0,0);
  float triangleLight = 0;
  if (surfaceIndex >= 0 && surfaceIndex < BINDING_INDEX_INVALID && surfaceIndex < cb.surfaceCount && primitiveIndex >= 0)
  {
    SURFACE_CREATE_READ(emissiveSurface0, surfaceIndex, surfaces);    
    Surface emissiveSurface = emissiveSurface0;
    RayInteraction emissiveRayInteracton = {};// rayInteraction;
    emissiveRayInteracton.frontHit = true;
    emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(uvToBary(vec2(0.3)));
    //emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(uvToBary(vec2(getNextSampleBlueNoise(randomState), getNextSampleBlueNoise(randomState))));
    emissiveRayInteracton.primitiveIndex = primitiveIndex;
    //emissiveSurface.isEmissive = false;
    //emissiveSurface.isEmissiveBlend = false;

    Ray ray;
    ray.direction = f16vec3(1.0);
    SurfaceInteraction emissiveSurfaceInteraction = surfaceInteractionCreate(emissiveSurface, emissiveRayInteracton, ray);
    emissiveSurfaceInteraction.textureGradientX = vec2(0.5,0);
    emissiveSurfaceInteraction.textureGradientY = vec2(0,0.5);

    const MemoryPolymorphicSurfaceMaterial memoryPolymorphicSurfaceMaterial = surfaceMaterials[emissiveSurface.surfaceMaterialIndex];
    const uint8_t polymorphicType = memoryPolymorphicSurfaceMaterialGetTypeHelper(memoryPolymorphicSurfaceMaterial);

    if (polymorphicType == surfaceMaterialTypeOpaque)
    {
      OpaqueSurfaceMaterial emissiveOpaqueSurfaceMaterial = opaqueSurfaceMaterialCreate(emissiveSurface.surfaceMaterialIndex, memoryPolymorphicSurfaceMaterial);
      //emissiveOpaqueSurfaceMaterial.emissiveIntensity = 1.0;
      const OpaqueSurfaceMaterialInteraction emissiveOpaqueSurfaceMaterialInteraction =
        opaqueSurfaceMaterialInteractionCreate(emissiveOpaqueSurfaceMaterial, emissiveSurface, emissiveSurfaceInteraction, emissiveRayInteracton);

      if (any(emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance > f16vec3(0)))
      {
        vec3 offset = emissiveSurfaceInteraction.position - cellCenter;
        float cellSize = NEECache.getCellSize() * 0.5;
        //float cellDistance = length(offset);
        //float cosTheta = cellDistance < cellSize ? 1.0 : abs(dot(emissiveSurfaceInteraction.triangleNormal, normalize(offset)));
        //cellDistance = max(cellDistance, cellSize);
        float g = 0;
#if 0
        g = getGeometricTerm(cellCenter, emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
#else
        g += getGeometricTerm(cellCenter + vec3(cellSize, 0, 0), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g += getGeometricTerm(cellCenter + vec3(-cellSize, 0, 0), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g += getGeometricTerm(cellCenter + vec3(0, cellSize, 0), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g += getGeometricTerm(cellCenter + vec3(0, -cellSize, 0), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g += getGeometricTerm(cellCenter + vec3(0, 0, cellSize), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g += getGeometricTerm(cellCenter + vec3(0, 0, -cellSize), emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal);
        g /= 6.0;
#endif
        float triangleArea = emissiveSurfaceInteraction.triangleArea;
        vec3 radiance = emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance;
        f16vec4 ecolor;
        //surfaceMaterialInteractionTextureReadHelper(emissiveOpaqueSurfaceMaterial.emissiveColorTextureIndex, emissiveSurfaceInteraction, ecolor);
        //radiance = ecolor.rgb;// * emissiveOpaqueSurfaceMaterial.emissiveIntensity;
      
        float luminance = calcBt709Luminance(radiance);
        triangleLight = luminance * triangleArea * g;//cosTheta / (cellDistance * cellDistance);
        //triangleLight = sqrt(triangleLight);
        triangleLight = clamp(triangleLight,0, floatMax / 32.0);
      }
    }
  }
  return triangleLight;
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(int2 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_tarTaskCount = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  uint4 threadTask = NEECache.loadThreadTask(threadIndex);

  // if(0)
  // {
  //   //int3 cellID = int3(threadIndex.xy % 20, (threadIndex.y / 20 * 16 + threadIndex.x / 20));//
  //   int3 cellID = NEECache.offsetToCell(threadTask.x);
  //   if (all(cellID != -1))
  //   {      
  //     NEECell cell = NEECache.getCell(cellID);
  //     if (cell.isValid())
  //     {
  //       //uint2 task = uint2(rayInteraction.surfaceIndex, rayInteraction.primitiveIndex);
  //       //cell.insertTask(threadTask.yz);
  //       //cell.setTaskCount(0);
  //       //cell.setCandidateCount(0);
  //     }
  //   }
  //   return;
  // }

  if (threadTask.x != 0xffffffff && threadTask.y != 0xffffffff && threadTask.z != 0xffffffff)
  {
    int oldCount;
    InterlockedAdd(s_tarTaskCount, 1, oldCount);
    s_task[oldCount] = threadTask.xyz;
  }
  GroupMemoryBarrierWithGroupSync();

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

  if (shouldBeAdded)
  {
    //    uint4 threadTask = NEECache.loadThreadTask(pathState.pixelCoordinate);
    int3 cellID = NEECache.offsetToCell(localTask.x);
    if (all(cellID != -1))
    {      
      NEECell cell = NEECache.getCell(cellID);
      if (cell.isValid())
      {
        //uint2 task = uint2(rayInteraction.surfaceIndex, rayInteraction.primitiveIndex);
        cell.insertTask(localTask.yz);
      }
    }
  }
  // int3 cellID = threadIndex / int3(RADIANCE_CACHE_ELEMENTS,1,1);
  // NEECell cell = NEECache.getCell(cellID);
  // vec3 cellCenter = NEECache.cellToCenterPoint(cellID);

  // RNG rng = createRNG(uvec2(threadIndex.xy /*+ threadIndex.z * RADIANCE_CACHE_ELEMENTS * 20 * 20*/), cb.frameIdx, threadIndex.z);

  // if (localIndex.x == 0 && localIndex.y == 0)
  // {
  //   s_tarTaskCount = 0;
  // }
  // GroupMemoryBarrierWithGroupSync();

  // // load new id to shared memory
  // int taskCount = cell.getTaskCount();
  // int taskID = localIndex.x;  
  // int2 task = cell.getTask(taskID);
  // s_srcTaskList[taskID] = mapIDData(task);

  // // load old id to shared memory
  // int oldIDCount = cell.getCandidateCount();
  // if (localIndex.x < oldIDCount /*&& getNextSampleBlueNoise(rng) < 0.99*/)
  // {
  //   int oldCount = 0;
  //   //InterlockedAdd(s_tarTaskCount, 1, oldCount);
  //   s_tarTaskList[localIndex.x] = mapIDData(cell.getCandidate(localIndex.x).getIDData());
  // }

  // if (localIndex.x == 0 && localIndex.y == 0)
  // {
  //   s_tarTaskCount = oldIDCount;
  // }
  // GroupMemoryBarrierWithGroupSync();

  // // merge tasks
  // bool insertTask = false;
  // if (taskID < taskCount && all(task != -1))
  // {
  //   bool isNewTask = true;
  //   for (int i = 0; i < taskID -1; ++i)
  //   {
  //     if (all(s_srcTaskList[i] == task))
  //     {
  //       isNewTask = false;
  //     }
  //   }
  //   for (int i = 0; i < s_tarTaskCount; ++i)
  //   {
  //     if (all(s_tarTaskList[i] == task))
  //     {
  //       isNewTask = false;
  //     }
  //   }
  //   insertTask = isNewTask;
  // }
  // GroupMemoryBarrierWithGroupSync();

  // if (insertTask /*&& s_tarTaskCount < RADIANCE_CACHE_ELEMENTS-1*/)
  // {
  //   int oldCount = 0;
  //   InterlockedAdd(s_tarTaskCount, 1, oldCount);
  //   //if (oldCount < RADIANCE_CACHE_ELEMENTS-1)
  //   {
  //     s_tarTaskList[oldCount] = task;
  //   }
  // }
  // GroupMemoryBarrierWithGroupSync();

  // // get triangle light
  // uint triangleID = localIndex.x;
  // if (triangleID < s_tarTaskCount)
  // {
  //   uint2 data = s_tarTaskList[triangleID];
  //   s_tarLight[triangleID] = getTriangleLight(data, cellCenter, rng);
  // }
  // triangleID = localIndex.x + RADIANCE_CACHE_ELEMENTS;
  // if (triangleID < s_tarTaskCount)
  // {
  //   uint2 data = s_tarTaskList[triangleID];
  //   s_tarLight[triangleID] = getTriangleLight(data, cellCenter, rng);
  // }
  // GroupMemoryBarrierWithGroupSync();

  // // sort tasks
  // if (localIndex.x == 0 && localIndex.y == 0)
  // {
  //   // if (cb.debugKnob.z == 0)
  //   if(1)
  //   {
  //     for (int i = 0; i < s_tarTaskCount-1; ++i)
  //     {
  //       for (int j = i+1; j < s_tarTaskCount; ++j)
  //       {
  //         if (s_tarLight[i] < s_tarLight[j])
  //         {
  //           float lightI = s_tarLight[i];
  //           s_tarLight[i] = s_tarLight[j];
  //           s_tarLight[j] = lightI;

  //           uint2 dataI = s_tarTaskList[i];
  //           s_tarTaskList[i] = s_tarTaskList[j];
  //           s_tarTaskList[j] = dataI;
  //         }
  //       }
  //     }
  //   }
  //   else
  //   {
  //     int validCount = min(s_tarTaskCount, NEECell.getMaxCandidateCount());
  //     int fixedCount = 0;//NEECell.getMaxCandidateCount() / 2;
  //     for (int i = NEECell.getMaxCandidateCount(); i < s_tarTaskCount; ++i)
  //     {
  //       //int targetID = (s_tarTaskList[i].y + cb.frameIdx) % validCount;//min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
  //       //int targetID = min(getNextSampleBlueNoise(rng) * validCount, validCount-1);
  //       int targetID = fixedCount + (cb.frameIdx + i) % (validCount - fixedCount);
  //       float ft = s_tarLight[i];
  //       float fs = s_tarLight[targetID];
  //       float rnd = getNextSampleBlueNoise(rng);
  //       //rnd = rnd * 0.5 + float(cb.frameIdx & 0x1) * 0.5;
  //       if (rnd * fs < ft)
  //       {
  //         s_tarLight[targetID] = s_tarLight[i];
  //         s_tarTaskList[targetID] = s_tarTaskList[i];
  //       }
  //     }
  //   }

  //   s_tarTaskCount = cb.enableNeeCache != 0 ? min(s_tarTaskCount, NEECell.getMaxCandidateCount()) : 0;
  //   cell.setTaskCount(0);
  //   cell.setCandidateCount(s_tarTaskCount);

  //   float totalLight0 = 0;
  //   for (int i = 0; i < s_tarTaskCount; ++i)
  //   {
  //     //maxLight = max(maxLight, s_tarLight[i]);
  //     totalLight0 += s_tarLight[i];
  //   }
  //   float avgLight = s_tarTaskCount == 0 ? 0 : totalLight0 / s_tarTaskCount;
  //   float totalLight = 0;
  //   for (int i = 0; i < s_tarTaskCount; ++i)
  //   {
  //     //if (cb.debugKnob.z > 0) s_tarLight[i] = 1;//sqrt(s_tarLight[i]);//clamp(s_tarLight[i],0.1,1);
  //     //s_tarLight[i] = pow(s_tarLight[i], 0.3);
  //     //s_tarLight[i] = clamp(s_tarLight[i], avgLight * cb.debugKnob.z, avgLight * cb.debugKnob.w);
  //     totalLight += s_tarLight[i];
  //   }
  //   for (int i = 0; i < s_tarTaskCount; ++i)
  //   {
  //     if (cb.enableNeeCacheImportanceSampling > 0)
  //     {
  //       s_tarLight[i] = saturate(s_tarLight[i] / totalLight);
  //     }
  //     else
  //     {
  //       s_tarLight[i] = 1.0 / s_tarTaskCount;
  //     }
  //   }
  // }
  // GroupMemoryBarrierWithGroupSync();

  // // update nee cache
  // if (localIndex.x < s_tarTaskCount)
  // {
  //   int candidateID = localIndex.x;
  //   NEECandidate candidate = NEECandidate.create(s_tarTaskList[candidateID]);

  //   float sampleThreshold = 0;
  //   for (int i = 0; i <= candidateID; ++i)
  //   {
  //     sampleThreshold += s_tarLight[i];
  //   }
  //   candidate.setSampleThreshold(sampleThreshold);

  //   cell.setCandidate(candidateID, candidate);
  // }
}
