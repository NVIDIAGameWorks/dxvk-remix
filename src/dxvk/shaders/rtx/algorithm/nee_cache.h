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
#pragma once

#include "rtx/algorithm/nee_cache_light.slangh"
#include "rtx/algorithm/nee_cache_data.h"
#include "rtx/algorithm/rtxdi/rtxdi.slangh"

struct NEESample
{
  vec3 position;
  float pdf;
  f16vec3 normal;
  f16vec3 radiance;

  NeeCache_PackedSample pack()
  {
    NeeCache_PackedSample packed;
    packed.hitGeometry.xyz = asuint(position);
    packed.hitGeometry.w = float2x32ToSnorm2x16(sphereDirectionToSignedOctahedral(normal));
    packed.lightInfo.x = f32tof16(radiance.x) | (f32tof16(radiance.y) << 16);
    packed.lightInfo.y = f32tof16(radiance.z);
    packed.lightInfo.z = asuint(pdf);
    return packed;
  }

  [mutating] void unpack(const NeeCache_PackedSample packed)
  {
    position = asfloat(packed.hitGeometry.xyz);
    normal = signedOctahedralToSphereDirection(snorm2x16ToFloat2x32(packed.hitGeometry.w));
    radiance.x = f16tof32(packed.lightInfo.x & 0xffff);
    radiance.y = f16tof32(packed.lightInfo.x >> 16);
    radiance.z = f16tof32(packed.lightInfo.y & 0xffff);
    pdf = asfloat(packed.lightInfo.z);
  }

  static NEESample createEmpty()
  {
    NEESample sample = {};
    sample.position = float3(0.f, 0.f, 1.f);
    sample.normal = float3(0.f, 0.f, -1.f);
    sample.radiance = float3(0.f);
    sample.pdf = 0.f;
    return sample;
  }

  static NEESample createFromPacked(const NeeCache_PackedSample packed)
  {
    NEESample sample = {};
    sample.unpack(packed);
    return sample;
  }

  LightSample convertToLightSample()
  {
    LightSample sample = {};
    sample.position = position;
    sample.normal = normal;
    sample.radiance = radiance;
    sample.solidAnglePdf = pdf;
    return sample;
  }
}

struct NEELightCandidate
{
  uint2 m_data;

  static float getOffsetRange()
  {
    const float rangeCellCount = 16;
    return cb.neeCacheArgs.range * (rangeCellCount / NEE_CACHE_PROBE_RESOLUTION);
  }

  static float getOffsetDelta()
  {
    return getOffsetRange() / 128.0;
  }

  static uint encodeOffset(vec3 offset)
  {
    float range = getOffsetRange();
    float maxOffset = max(max(abs(offset.x), abs(offset.y)), abs(offset.z));
    float scaleFactor = max(maxOffset, range);
    offset /= scaleFactor;
    vec3 uvw = offset * 0.5 + 0.5;

    uint encodedOffset = 0;
    encodedOffset |= uint(uvw.x * 255 + 0.5);
    encodedOffset <<= 8;
    encodedOffset |= uint(uvw.y * 255 + 0.5);
    encodedOffset <<= 8;
    encodedOffset |= uint(uvw.z * 255 + 0.5);
    return encodedOffset;
  }

  static vec3 decodeOffset(uint encodedOffset)
  {
    float range = getOffsetRange();
    vec3 uvw;
    uvw.z = (encodedOffset & 0xff);
    encodedOffset >>= 8;
    uvw.y = (encodedOffset & 0xff);
    encodedOffset >>= 8;
    uvw.x = (encodedOffset & 0xff);
    uvw /= 255.0;
    uvw = uvw * 2.0 - 1.0;
    return uvw * range;
  }

  bool isValid()
  {
    return m_data.x != 0xffffffff;
  }

  [mutating] void setInvalid()
  {
    m_data.x = 0xffffffff;
  }

  int getLightID()
  {
    return m_data.x & 0xffff;
  }

  [mutating] void setLightID(int lightIdx)
  {
    m_data.x = (m_data.x & 0xffff0000) | lightIdx;
  }

  float16_t getRadiance()
  {
    return uint16BitsToHalf(m_data.x >> 16);
  }

  [mutating] void setRadiance(float16_t radiance)
  {
    uint encodedRadiance = float16BitsToUint16(radiance);
    m_data.x = (encodedRadiance << 16) | (m_data.x & 0xffff);
  }

  vec3 getOffset()
  {
    return decodeOffset(m_data.y);
  }

  [mutating] void setOffset(vec3 offset)
  {
    uint encodedOffset = encodeOffset(offset);
    m_data.y = (m_data.y & 0xff000000) | encodedOffset;
  }

  uint getAge()
  {
    return (m_data.y >> 24) & 0xff;
  }

  [mutating] void setAge(uint age)
  {
    age = min(age, 255);
    m_data.y = (m_data.y & 0xffffff) | (age << 24);
  }

  static NEELightCandidate create(uint lightIdx)
  {
    NEELightCandidate nee;
    nee.m_data = 0;
    nee.setLightID(lightIdx);
    nee.setAge(0);
    nee.setOffset(0.0);
    nee.setRadiance(0);
    return nee;
  }

  static NEELightCandidate createFromPacked(uint2 data)
  {
    NEELightCandidate nee;
    nee.m_data = data;
    return nee;
  }
}

struct NEECandidate
{
  uint2 m_data;

  bool isValid()
  {
    return m_data.y != 0xffffffff;
  }

  [mutating] void setInvalid()
  {
    m_data.y = 0xffffffff;
  }

  int getSurfaceID()
  {
    return m_data.x & 0xffffff;
  }

  [mutating] void setSurfaceID(int surfaceID)
  {
    m_data.x = (m_data.x & 0xff000000) | surfaceID;
  }

  int getPrimitiveID()
  {
    return m_data.y & 0xffffff;
  }

  [mutating] void setPrimitiveID(int primitiveID)
  {
    m_data.y = (m_data.y & 0xff000000) | primitiveID;
  }

  uint2 getIDData()
  {
    return uint2(getSurfaceID(), getPrimitiveID());
  }

  float16_t getSampleThreshold()
  {
    uint8_t thresholdI = (m_data.x >> 24) & 0xff;
    return unorm8ToF16(thresholdI);
  }

  [mutating] void setSampleThreshold(float16_t threshold)
  {
    uint thresholdI = f16ToUnorm8(threshold);
    m_data.x = (m_data.x & 0xffffff) | (thresholdI << 24);
  }

  uint getAge()
  {
    return m_data.y >> 24;
  }

  [mutating] void setAge(int age)
  {
    age = min(age, 255);
    m_data.y = (m_data.y & 0xffffff) | (age << 24);
  }

  static NEECandidate create(uint surfaceID, uint primitiveID)
  {
    NEECandidate nee;
    nee.m_data = 0;
    nee.setSurfaceID(surfaceID);
    nee.setPrimitiveID(primitiveID);
    nee.setAge(0);
    return nee;
  }

  static NEECandidate create(uint2 data)
  {
    NEECandidate nee;
    nee.m_data = data;
    return nee;
  }
}

struct NEECell
{
  static const uint16_t s_invalidOffset = 0xffff;
  int m_offset;

  uint getBaseAddress()
  {
    return m_offset * NEE_CACHE_CELL_CANDIDATE_TOTAL_SIZE;
  }

  uint getTaskBaseAddress()
  {
    return m_offset * (NEE_CACHE_ELEMENTS * NEE_CACHE_ELEMENT_SIZE);
  }

  uint getTaskAddress(uint idx)
  {
    return getTaskBaseAddress() + idx * NEE_CACHE_TASK_SIZE;
  }

  uint getCandidateAddress(uint idx)
  {
    return getBaseAddress() + (idx + 1) * NEE_CACHE_ELEMENT_SIZE;
  }

  uint getLightCandidateBaseAddress()
  {
    return getBaseAddress() + NEE_CACHE_ELEMENTS * NEE_CACHE_ELEMENT_SIZE;
  }

  uint getLightCandidateAddress(uint idx)
  {
    return getLightCandidateBaseAddress() + (idx + 1) * NEE_CACHE_LIGHT_ELEMENT_SIZE;
  }

  bool isValid() { return m_offset != NEECell.s_invalidOffset; }

  int getTaskCount()
  {
    int count = 0;
    for (int i = 0; i < getMaxTaskCount(); ++i)
    {
      uint taskData = NeeCacheTask.Load(getTaskAddress(i));
      if (taskData != NEE_CACHE_EMPTY_TASK)
      {
        count++;
      }
    }
    return count;
  }

  void clearTasks()
  {
    for (int i = 0; i < getMaxTaskCount(); ++i)
    {
      NeeCacheTask.Store(getTaskAddress(i), NEE_CACHE_EMPTY_TASK);
    }
  }

  uint getTask(int idx)
  {
    int count = 0;
    for (int i = 0; i < getMaxTaskCount(); ++i)
    {
      uint taskData = NeeCacheTask.Load(getTaskAddress(i));
      if (taskData == NEE_CACHE_EMPTY_TASK)
      {
        continue;
      }
      if (count == idx)
      {
        return taskData & 0xffffff;
      }
      count++;
    }
    return NEE_CACHE_EMPTY_TASK;
  }

  uint getTaskFromIdx(int idx)
  {
    uint taskData = NeeCacheTask.Load(getTaskAddress(idx));
    return taskData & 0xffffff;
  }

  void setTaskFromIdx(int idx, uint task, uint value)
  {
    task |= (value << 24);
    NeeCacheTask.Store(getTaskAddress(idx), task);
  }

  static uint getTaskHash(uint task)
  {
    return task;
  }

  static uint getBinHash(uint x)
  {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x & 0xf;
  }

  bool insertTask(uint task, uint value)
  {
    uint index = getBinHash(task + cb.frameIdx);
    int taskAddress = getTaskAddress(index);
    task |= (value << 24);

    // Remove duplicated tasks
    uint oldTask = NeeCacheTask.Load(taskAddress);
    if (oldTask == task || (oldTask != NEE_CACHE_EMPTY_TASK && getTaskHash(oldTask) >= getTaskHash(task)))
    {
      return false;
    }

    // Insert task with the largest hash value
    uint expectTask = NEE_CACHE_EMPTY_TASK;
    uint insertTask = task;
    while(true)
    {
      uint originalTask;
      NeeCacheTask.InterlockedCompareExchange(taskAddress, expectTask, insertTask, originalTask);
      if (originalTask == expectTask)
      {
        // successfully inserted
        return true;
      }

      // Only insert a task when its hash is larger
      // Because the value is in high bits, tasks with higher values will have higher priority
      uint insertTaskHash   = getTaskHash(insertTask);
      uint originalTaskHash = getTaskHash(originalTask);
      if (originalTaskHash >= insertTaskHash)
      {
        return false;
      }

      // Prefare for next insertion
      expectTask = originalTask;
    }
    return false;
  }

  int getSampleAddress(int i)
  {
    return m_offset * NEE_CACHE_SAMPLES + i;
  }

  NEESample getSample(int idx)
  {
    NeeCache_PackedSample packed = NeeCacheSample[getSampleAddress(idx)];
    return NEESample.createFromPacked(packed);
  }

  void setSample(int idx, NEESample sample)
  {
    NeeCache_PackedSample packed = sample.pack();
    NeeCacheSample[getSampleAddress(idx)] = packed;
  }

  static int getMaxLightCandidateCount()
  {
    return NEE_CACHE_LIGHT_ELEMENTS-1;
  }

  int getLightCandidateCount()
  {
    uint count = NeeCache.Load(getLightCandidateBaseAddress());
    return min(count, getMaxLightCandidateCount());
  }

  void setLightCandidateCount(int count)
  {
    NeeCache.Store(getLightCandidateBaseAddress(), count);
  }

  NEELightCandidate getLightCandidate(int idx)
  {
    return NEELightCandidate.createFromPacked(NeeCache.Load2(getLightCandidateAddress(idx)));
  }

  void setLightCandidate(int idx, NEELightCandidate candidate)
  {
    return NeeCache.Store2(getLightCandidateAddress(idx), candidate.m_data);
  }

  float calculateLightCandidateWeight(NEELightCandidate candidate, vec3 cellCenter, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness)
  {
    vec3 candidatePosition = candidate.getOffset() + cellCenter;
    f16vec3 inputDirection = normalize(candidatePosition - surfacePoint + normal * NEELightCandidate.getOffsetDelta());

    // Use a simplified GGX model to calculate light contribution
    // Offset diffuse so that light at grazing angles will not be culled due to low precision input direction.
    const float16_t cosOffset = 0.3;
    float16_t ndoti = saturate((dot(inputDirection, normal) + cosOffset) / (float16_t(1) + cosOffset));
    float16_t diffuseTerm = (1.0 - specularRatio) / pi;

    // The specular term consists of there parts: D, G, F
    // We simplify some of them to improve performance.
    // For D term, we use GGX normal distribution
    // For G term, we simply assume it to be 1
    // For F term, we assume it's the baseReflectivity, fresnel effect is not considered.
    f16vec3 halfVector = normalize(inputDirection + viewDirection);
    float ndotm = saturate(dot(halfVector, normal));
    float specularTerm = specularRatio * evalGGXNormalDistributionIsotropic(roughness, ndotm) * cb.neeCacheArgs.specularFactor * 0.25;

    float radiance = candidate.getRadiance();
    return radiance * (diffuseTerm + specularTerm) * ndoti;
  }

  void calculateLightCandidateNormalizedWeight(int ithCandidate, vec3 cellCenter, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness, out float pdf)
  {
    int count = getLightCandidateCount();
    float totalWeight = 0;
    float chosenWeight = 0;
    for (int i = 0; i < count; ++i)
    {
      NEELightCandidate candidate = getLightCandidate(i);
      float weight = calculateLightCandidateWeight(candidate, cellCenter, surfacePoint, viewDirection, normal, specularRatio, roughness);
      totalWeight += weight;
      if (i == ithCandidate)
      {
        chosenWeight = weight;
      }
    }
    pdf = chosenWeight / totalWeight;
  }

  void sampleLightCandidate(inout RAB_RandomSamplerState rtxdiRNG, vec2 uniformRandomNumber, vec3 cellCenter, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness, inout uint16_t lightIdx, out float invPdf)
  {
    int lightCount = cb.lightRanges[lightTypeCount-1].offset + cb.lightRanges[lightTypeCount-1].count;
    uint uniformLightIdx = clamp(uniformRandomNumber.y * lightCount, 0, lightCount-1);

    // Use weighted reservoir sampling to cached lights.
    // See "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" for more details.
    int count = getLightCandidateCount();
    float totalWeight = 0;
    float chosenWeight = 0;
    float uniformWeight = 0;
    for (int i = 0; i < count; ++i)
    {
      NEELightCandidate candidate = getLightCandidate(i);
      float weight = calculateLightCandidateWeight(candidate, cellCenter, surfacePoint, viewDirection, normal, specularRatio, roughness);
      totalWeight += weight;
      if (weight > totalWeight * RAB_GetNextRandom(rtxdiRNG))
      {
        lightIdx = candidate.getLightID();
        chosenWeight = weight;
      }
      if (candidate.getLightID() == uniformLightIdx)
      {
        uniformWeight = weight;
      }
    }

    // Conditionally use uniform sampling to ensure unbiasedness
    float uniformProbability = totalWeight > 0.0 ? cb.neeCacheArgs.uniformSamplingProbability : 1.0;
    if (uniformRandomNumber.x <= uniformProbability)
    {
      lightIdx = uniformLightIdx;
      chosenWeight = uniformWeight;
    }

    // wrsPdf is the probability for a given light chosen by WRS.
    // It would be 0 if a light is not in the cache.
    float wrsPdf = totalWeight > 0 ? chosenWeight / totalWeight : 0.0;
    float pdf = lerp(wrsPdf, 1.0 / lightCount, uniformProbability);
    invPdf = 1.0 / pdf;
  }

  int getCandidateCount()
  {
    uint count = NeeCache.Load(getBaseAddress());
    return min(count, getMaxCandidateCount());
  }

  void setCandidateCount(int count)
  {
    NeeCache.Store(getBaseAddress(), count);
  }

  NEECandidate getCandidate(int idx)
  {
    return NEECandidate.create(NeeCache.Load2(getCandidateAddress(idx)));
  }

  void setCandidate(int idx, NEECandidate candidate)
  {
    return NeeCache.Store2(getCandidateAddress(idx), candidate.m_data);
  }

  NEECandidate sampleCandidate(float sampleThreshold, out float pdf)
  {
    int count = getCandidateCount();

    NEECandidate candidate;
    candidate.setInvalid();
    int i = 0;
    float lastCdf = 0;
    pdf = 1;
    for (; i < count; ++i)
    {
      candidate = getCandidate(i);
      float cdf = candidate.getSampleThreshold();
      pdf = cdf - lastCdf;
      lastCdf = cdf;
      if (candidate.getSampleThreshold() >= sampleThreshold)
      {
        return candidate;
      }
    }
    return candidate;
  }

  float getCandidatePdf(int idx)
  {
    float lastCDF = 0;
    if (idx > 0)
    {
      lastCDF = getCandidate(idx-1).getSampleThreshold();
    }
    float thisCDF = getCandidate(idx).getSampleThreshold();
    return thisCDF - lastCDF;
  }

  static int getMaxCandidateCount()
  {
    return NEE_CACHE_ELEMENTS-1;
  }

  static int getMaxTaskCount()
  {
    return NEE_CACHE_ELEMENTS;
  }

  LightSample getLightSample(vec3 randomNumber, vec3 position, float16_t coneRadius, float16_t coneSpreadAngle, bool useCachedSamples = true)
  {
    LightSample lightSampleTriangle;
    if(useCachedSamples)
    {
      int sampleIdx = randomNumber.x * NEE_CACHE_SAMPLES;
      NEESample sample = getSample(sampleIdx);
      lightSampleTriangle = sample.convertToLightSample();
      lightSampleTriangle.solidAnglePdf *= NEECacheUtils.calculateLightSamplingSolidAnglePDF(1.0, lightSampleTriangle.position, lightSampleTriangle.normal, position);
    }
    else
    {
      // Sample cached triangles
      float lightObjectPdf = 0;
      NEECandidate candidate = sampleCandidate(randomNumber.x, lightObjectPdf);
      // Sample the selected triangle
      vec2 uv = vec2(randomNumber.y, randomNumber.z);
      lightSampleTriangle = NEECacheUtils.calculateLightSampleFromTriangle(
        candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, position, coneRadius, coneSpreadAngle);
    }
    return lightSampleTriangle;
  }
}

struct ThreadTask
{
  uint2 m_data;

  static const uint s_lightOffset   = (1 << 23);
  static const uint s_surfaceMask   = 0xffffff;
  static const uint s_primitiveMask = 0xffffff;
  static const uint s_invalidTask  = 0xffffffff;

  bool isValid()
  {
    return any(m_data != s_invalidTask);
  }

  bool isTriangleTask()
  {
    return isValid() && (m_data.x & 0xffffff) < s_lightOffset;
  }

  bool isLightTask()
  {
    return isValid() && (m_data.x & 0xffffff) >= s_lightOffset;
  }

  bool getTriangleTask(out uint surfaceID, out uint primitiveID)
  {
    surfaceID   = m_data.x & s_surfaceMask;
    primitiveID = m_data.y & s_primitiveMask;
    return surfaceID != s_surfaceMask && primitiveID != s_primitiveMask;
  }

  uint getLightTask()
  {
    return (m_data.x & s_surfaceMask) - s_lightOffset;
  }

  uint getCellOffset()
  {
    return ((m_data.y >> 24) << 8) | (m_data.x >> 24);
  }

  [mutating] void packFromTriangleTask(uint cellOffset, uint surfaceID, uint primitiveID)
  {
    m_data.x = ((cellOffset & 0xff) << 24) | (surfaceID & s_surfaceMask);
    m_data.y = ((cellOffset >> 8)   << 24) | (primitiveID & s_primitiveMask);
  }

  [mutating] void packFromLightTask(uint cellOffset, uint lightID)
  {
    lightID += s_lightOffset;
    m_data.x = ((cellOffset & 0xff) << 24) | (lightID & s_surfaceMask);
    m_data.y = ((cellOffset >> 8)   << 24);
  }

  static ThreadTask createFromTriangleTask(uint cellOffset, uint surfaceID, uint primitiveID)
  {
    ThreadTask task;
    task.packFromTriangleTask(cellOffset, surfaceID, primitiveID);
    return task;
  }

  static ThreadTask createFromLightTask(uint cellOffset, uint lightID)
  {
    ThreadTask task;
    task.packFromLightTask(cellOffset, lightID);
    return task;
  }

  static ThreadTask createEmpty()
  {
    ThreadTask task;
    task.m_data = s_invalidTask;
    return task;
  }
}

struct NEECache
{
  static bool isInsideCache(vec3 position)
  {
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 offset = abs(position - cameraPos);
    return all(offset < cb.neeCacheArgs.range * 0.5);
  }

  static int cellToOffset(int3 cellID)
  {
    if (any(cellID == -1))
    {
      return NEECell.s_invalidOffset;
    }

    int idx =
      cellID.z * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION +
      cellID.y * NEE_CACHE_PROBE_RESOLUTION +
      cellID.x;
    return idx;
  }

  static int3 offsetToCell(int offset)
  {
    if (offset == NEECell.s_invalidOffset)
    {
      return int3(-1);
    }
    int3 cellID;
    const int zSize = NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION;
    const int ySize = NEE_CACHE_PROBE_RESOLUTION;

    cellID.z = offset / zSize;
    offset -= cellID.z * zSize;

    cellID.y = offset / ySize;
    offset -= cellID.y * ySize;

    cellID.x = offset;
    return cellID;
  }

  static int3 pointToCell(vec3 position, bool jittered, vec3 jitteredNumber)
  {
    float extend = cb.neeCacheArgs.range;
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = (position - origin) / extend;
    vec3 UVWi = UVW * NEE_CACHE_PROBE_RESOLUTION;

    // jitter cell ID
    if(jittered)
    {
      vec3 fracUVWi = fract(UVWi);
      ivec3 cellID = ivec3(UVWi);
      cellID.x += jitteredNumber.x > fracUVWi.x ? 0 : 1;
      cellID.y += jitteredNumber.y > fracUVWi.y ? 0 : 1;
      cellID.z += jitteredNumber.z > fracUVWi.z ? 0 : 1;
      
      if (any(cellID < 0) || any(cellID > NEE_CACHE_PROBE_RESOLUTION-1))
      {
        return int3(-1);
      }
      return cellID;
    }
    else
    {
      UVWi += 0.5;
      if (any(UVWi < 0) || any(UVWi > NEE_CACHE_PROBE_RESOLUTION-1))
      {
        return int3(-1);
      }
      return UVWi;
    }
  }

  static int pointToOffset(vec3 position, bool jittered, vec3 jitteredNumber)
  {
    int3 cellID = pointToCell(position, jittered, jitteredNumber);
    return cellToOffset(cellID);
  }

  static float getCellSize()
  {
    return cb.neeCacheArgs.range / NEE_CACHE_PROBE_RESOLUTION;
  }

  static float getVolumeSize()
  {
    return cb.neeCacheArgs.range;
  }

  static vec3 cellToCenterPoint(ivec3 cellID)
  {
    float extend = cb.neeCacheArgs.range;
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = vec3(cellID) / NEE_CACHE_PROBE_RESOLUTION;
    vec3 position = UVW * extend + origin;
    return position;
  }

  static NEECell getCell(int3 cellID)
  {
    NEECell cell = {};
    cell.m_offset = cellToOffset(cellID);
    return cell;
  }

  static NEECell getCell(int offset) {
    NEECell cell = {};
    cell.m_offset = offset;
    return cell;
  }

  static NEECell findCell(vec3 point, bool jittered, vec3 jitteredNumber)
  {
    return getCell(pointToCell(point, jittered, jitteredNumber));
  }

  static const uint s_analyticalLightStartIdx = 0xff0000;

  static bool isAnalyticalLight(uint idx)
  {
    return idx >= s_analyticalLightStartIdx;
  }

  static uint encodeAnalyticalLight(uint lightIdx)
  {
    return s_analyticalLightStartIdx + lightIdx;
  }

  static uint decodeAnalyticalLight(uint idx)
  {
    return idx - s_analyticalLightStartIdx;
  }

  static void storeThreadTask(int2 pixel, uint cellOffset, uint surfaceID, uint primitiveID)
  {
    uint2 data = uint2(surfaceID, primitiveID) & 0xffffff;
    data.x = data.x | ((cellOffset & 0xff) << 24);
    data.y = data.y | ((cellOffset & 0xff00) << 16);
    NeeCacheThreadTask[pixel] = data;
  }

  static void loadThreadTask(int2 pixel, out uint cellOffset, out uint surfaceID, out uint primitiveID)
  {
    uint2 data = NeeCacheThreadTask[pixel];
    surfaceID   = data.x & 0xffffff;
    primitiveID = data.y & 0xffffff;
    if (surfaceID == 0xffffff || primitiveID == 0xffffff)
    {
      surfaceID = primitiveID = 0xffffffff;
    }
    cellOffset = (data.x >> 24) | ((data.y & 0xff000000) >> 16);
  }

  static void storeThreadTask(int2 pixel, ThreadTask task)
  {
    NeeCacheThreadTask[pixel] = task.m_data;
  }

  static ThreadTask loadThreadTask(int2 pixel)
  {
    ThreadTask task;
    task.m_data = NeeCacheThreadTask[pixel];
    return task;
  }

  static bool shouldUseHigherBounceNeeCache(bool isSpecularLobe, float16_t isotropicRoughness)
  {
    // ReSTIR GI can handle diffuse rays quite well, but not for highly specular surfaces.
    // Skip diffuse and rough specular surfaces to improve performance.
    // The roughness threshold here is based on experiment.
    const float16_t minRoughness = 0.1;
    return cb.neeCacheArgs.enableModeAfterFirstBounce == NeeEnableMode::SpecularOnly ? isSpecularLobe && isotropicRoughness < minRoughness : true;
  }
}

