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

#ifdef UPDATE_NEE_CACHE
#define NEE_CACHE_WRITE_SAMPLE 1
#define NEE_CACHE_WRITE_CANDIDATE 1
#else
#define NEE_CACHE_WRITE_SAMPLE 0
#define NEE_CACHE_WRITE_CANDIDATE 0
#endif

struct NEESample
{
  vec3 position;
  float pdf;
  f16vec3 normal;
  f16vec3 radiance;
  uint triangleID;

  NeeCache_PackedSample pack()
  {
    NeeCache_PackedSample packed;
    packed.hitGeometry.xyz = asuint(position);
    packed.hitGeometry.w = float2x32ToSnorm2x16(sphereDirectionToSignedOctahedral(normal));
    packed.lightInfo.x = f32tof16(radiance.x) | (f32tof16(radiance.y) << 16);
    packed.lightInfo.y = f32tof16(radiance.z);
    packed.lightInfo.z = asuint(pdf);
    packed.lightInfo.w = triangleID;
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
    triangleID = packed.lightInfo.w;
  }

  static NEESample createEmpty()
  {
    NEESample sample = {};
    sample.position = float3(0.f, 0.f, 1.f);
    sample.normal = float3(0.f, 0.f, -1.f);
    sample.radiance = float3(0.f);
    sample.pdf = 0.f;
    sample.triangleID = 0;
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
    // Normalization factor for offset, in order to adapt to different scene scale.
    // The 0.1 factor is based on experiment.
    return cb.neeCacheArgs.minRange * 0.1;
  }

  static uint encodeOffset(vec3 offset)
  {
    // Encode light position offset to the camera center.
    // Put the encoding/decoding functions here because it's specific to NEE Cache.
    float range = getOffsetRange();
    float offsetLength = length(offset);
    offset /= (offsetLength + 1e-10);
    vec2 uv = sphereDirectionToSignedOctahedral(offset);
    uv = (uv + 1) * 0.5;
    uint result = float16BitsToUint16(min(offsetLength / range, float16Max));
    result |= (uint(uv.x * 255.0) << 24);
    result |= (uint(uv.y * 255.0) << 16);
    return result;
  }

  static vec3 decodeOffset(uint encodedOffset)
  {
    // Decode light position offset to the camera center.
    // Put the encoding/decoding functions here because it's specific to NEE Cache.
    float range = getOffsetRange();
    vec2 uv = vec2(encodedOffset >> 24, (encodedOffset >> 16) & 0xff) / 255.0;
    uv = uv * 2.0 - 1.0;
    vec3 offset = signedOctahedralToSphereDirection(uv);
    float offsetLength = float(uint16BitsToHalf(encodedOffset & 0xffff)) * range;
    return offset * offsetLength;
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
    m_data.y = encodeOffset(offset);
  }

  static NEELightCandidate create(uint lightIdx)
  {
    NEELightCandidate nee;
    nee.m_data = 0;
    nee.setLightID(lightIdx);
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

  float16_t getSampleProbability()
  {
    uint8_t thresholdI = (m_data.x >> 24) & 0xff;
    return unorm8ToF16(thresholdI);
  }

  [mutating] void setSampleProbability(float16_t threshold)
  {
    uint thresholdI = f16ToUnorm8(threshold);
    m_data.x = (m_data.x & 0xffffff) | (thresholdI << 24);
  }

  static NEECandidate create(uint surfaceID, uint primitiveID)
  {
    NEECandidate nee;
    nee.m_data = 0;
    nee.setSurfaceID(surfaceID);
    nee.setPrimitiveID(primitiveID);
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
    return m_offset * NEE_CACHE_TASK_COUNT * NEE_CACHE_TASK_SIZE;
  }

  uint getTaskAddress(uint idx)
  {
    return getTaskBaseAddress() + idx * NEE_CACHE_TASK_SIZE;
  }

  uint getHashTaskAddress(uint idx)
  {
    return getTaskAddress(idx) + NEE_CACHE_HASH_TASK_BASE;
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

  static uint getSlotBinHash(uint x)
  {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x & 0x1f;
  }

  uint2 getSlotTaskValue(int index)
  {
    int taskAddress = getTaskAddress(index);
    return NeeCacheTask.Load2(taskAddress);
  }

  void setSlotTaskValue(int index, uint2 value)
  {
    int taskAddress = getTaskAddress(index);
    NeeCacheTask.Store2(taskAddress, value);
  }

  uint2 getLightSlotTaskValue(int index)
  {
    int taskAddress = getTaskAddress(index + 16);
    return NeeCacheTask.Load2(taskAddress);
  }

  void setLightSlotTaskValue(int index, uint2 value)
  {
    int taskAddress = getTaskAddress(index + 16);
    NeeCacheTask.Store2(taskAddress, value);
  }

  uint2 getHashSlotTaskValue(int index)
  {
    int taskAddress = getHashTaskAddress(index);
    return NeeCacheTask.Load2(taskAddress);
  }

  void setHashSlotTaskValue(int index, uint2 value)
  {
    int taskAddress = getHashTaskAddress(index);
    NeeCacheTask.Store2(taskAddress, value);
  }

  static bool isLightTask(uint2 value)
  {
    return (value.x & (1 << 24)) != 0;
  }

  void insertSlotTask(uint task, float16_t accumulateValue, float16_t randomOffset, bool isLightTask) {
    uint index = getSlotBinHash(task + cb.frameIdx);
    int taskAddress = getHashTaskAddress(index);
    uint sortValueI = firstbithigh(uint(min(accumulateValue, 50) / 0.001));
    task |= (sortValueI << 25) | (isLightTask ? (1 << 24) : 0);

    // Clamp min/max value before accumulation to improve stability.
    // The min value is required because floating point atomics is not supported on all platforms,
    // so quantization is necessary to convert floating point values to integers.
    // The max value is required to suppress the impact from fireflies, otherwise a firefly may inject a
    // useless triangle / light to the cache.
    const float16_t minValue = 0.05;
    const float16_t maxValue = 5000;
    int accumulateValueI = clamp(accumulateValue, 0.0, maxValue) / minValue + randomOffset;

    if (accumulateValueI == 0)
    {
      return;
    }

    uint originalValue;
    NeeCacheTask.InterlockedMax(taskAddress, task, originalValue);
    NeeCacheTask.InterlockedAdd(taskAddress + 4, accumulateValueI, originalValue);
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

#if NEE_CACHE_WRITE_SAMPLE
  void setSample(int idx, NEESample sample)
  {
    NeeCache_PackedSample packed = sample.pack();
    NeeCacheSample[getSampleAddress(idx)] = packed;
  }
#endif

  static int getMaxLightCandidateCount()
  {
    return NEE_CACHE_LIGHT_ELEMENTS-1;
  }

  int getLightCandidateCount()
  {
    uint count = NeeCache.Load(getLightCandidateBaseAddress());
    return min(count, getMaxLightCandidateCount());
  }

  NEELightCandidate getLightCandidate(int idx)
  {
    return NEELightCandidate.createFromPacked(NeeCache.Load2(getLightCandidateAddress(idx)));
  }

#if NEE_CACHE_WRITE_CANDIDATE
  void setLightCandidateCount(int count)
  {
    NeeCache.Store(getLightCandidateBaseAddress(), count);
  }

  void setLightCandidate(int idx, NEELightCandidate candidate)
  {
    return NeeCache.Store2(getLightCandidateAddress(idx), candidate.m_data);
  }
#endif

  float calculateLightCandidateWeight(NEELightCandidate candidate, vec3 cellCenter, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness, bool isSubsurface)
  {
    vec3 candidateOffset = candidate.getOffset();
    f16vec3 inputDirection = normalize(candidateOffset + cellCenter + normal * length(candidateOffset) * 0.01 - surfacePoint);

    // Use a simplified GGX model to calculate light contribution
    float16_t ndoti = dot(inputDirection, normal);
    float16_t diffuseTerm = !isSubsurface ? (1.0 - specularRatio) / pi : (1.0 - specularRatio) / twoPi;
    float specularTerm = 0.0f;

    if (!isSubsurface || ndoti > 0.0h) {
      ndoti = saturate(ndoti);

      // The specular term consists of there parts: D, G, F
      // We simplify some of them to improve performance.
      // For D term, we use GGX normal distribution
      // For G term, we simply assume it to be 1
      // For F term, we assume it's the baseReflectivity, fresnel effect is not considered.
      f16vec3 halfVector = normalize(inputDirection + viewDirection);
      float ndotm = saturate(dot(halfVector, normal));
      specularTerm = specularRatio * evalGGXNormalDistributionIsotropic(roughness, ndotm) * cb.neeCacheArgs.specularFactor * 0.25;
    } else // isSubsurface && ndoti < 0
    {
      ndoti = -ndoti;
    }

    const float radiance = candidate.getRadiance();
    return radiance * (diffuseTerm + specularTerm) * ndoti;
  }

  void calculateLightCandidateNormalizedWeight(int ithCandidate, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness, bool isSubsurface, out float pdf)
  {
    int count = getLightCandidateCount();
    float totalWeight = 0;
    float chosenWeight = 0;
    vec3 cellCenter = NEECache.getCenter();
    pdf = 0.0;
    for (int i = 0; i < count; ++i)
    {
      NEELightCandidate candidate = getLightCandidate(i);
      float weight = calculateLightCandidateWeight(candidate, cellCenter, surfacePoint, viewDirection, normal, specularRatio, roughness, isSubsurface);
      totalWeight += weight;
      if (i == ithCandidate)
      {
        chosenWeight = weight;
      }
    }
    pdf = chosenWeight / totalWeight;
  }

  void sampleLightCandidate(inout RAB_RandomSamplerState rtxdiRNG, vec2 uniformRandomNumber, vec3 surfacePoint, f16vec3 viewDirection, f16vec3 normal, float16_t specularRatio, float16_t roughness, bool isSubsurface, inout uint16_t lightIdx, out float invPdf)
  {
    int lightCount = cb.lightRanges[lightTypeCount-1].offset + cb.lightRanges[lightTypeCount-1].count;
    uint uniformLightIdx = clamp(uniformRandomNumber.y * lightCount, 0, lightCount-1);

    // Use weighted reservoir sampling to cached lights.
    // See "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting" for more details.
    int count = getLightCandidateCount();
    float totalWeight = 0;
    float chosenWeight = 0;
    float uniformWeight = 0;
    vec3 cellCenter = NEECache.getCenter();
    for (int i = 0; i < count; ++i)
    {
      NEELightCandidate candidate = getLightCandidate(i);
      float weight = calculateLightCandidateWeight(candidate, cellCenter, surfacePoint, viewDirection, normal, specularRatio, roughness, isSubsurface);
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

#if NEE_CACHE_WRITE_CANDIDATE
  void setCandidateCount(int count)
  {
    NeeCache.Store(getBaseAddress(), count);
  }

  void setCandidate(int idx, NEECandidate candidate)
  {
    return NeeCache.Store2(getCandidateAddress(idx), candidate.m_data);
  }
#endif

  NEECandidate getCandidate(int idx)
  {
    return NEECandidate.create(NeeCache.Load2(getCandidateAddress(idx)));
  }

  NEECandidate sampleCandidate(float sampleThreshold, out float pdf)
  {
    int count = getCandidateCount();

    NEECandidate candidate;
    candidate.setInvalid();
    int i = 0;
    float cdf = 0;
    pdf = 1;
    for (; i < count; ++i)
    {
      candidate = getCandidate(i);
      pdf = candidate.getSampleProbability();
      cdf += pdf;
      if (sampleThreshold <= cdf)
      {
        return candidate;
      }
    }
    return candidate;
  }

  float getCandidatePdf(int idx)
  {
    return getCandidate(idx).getSampleProbability();
  }

  static int getMaxCandidateCount()
  {
    return NEE_CACHE_ELEMENTS-1;
  }

  static int getMaxTaskCount()
  {
    return NEE_CACHE_ELEMENTS;
  }

  LightSample getLightSample(vec3 randomNumber, vec3 position, float16_t coneRadius, float16_t coneSpreadAngle, out uint triangleID, bool useCachedSamples = true)
  {
    LightSample lightSampleTriangle;
    if(useCachedSamples)
    {
      int sampleIdx = randomNumber.x * NEE_CACHE_SAMPLES;
      NEESample sample = getSample(sampleIdx);
      lightSampleTriangle = sample.convertToLightSample();
      lightSampleTriangle.solidAnglePdf *= NEECacheUtils.calculateLightSamplingSolidAnglePDF(1.0, lightSampleTriangle.position, lightSampleTriangle.normal, position);
      triangleID = sample.triangleID;
    }
    else
    {
      // Sample cached triangles
      float lightObjectPdf = 0;
      NEECandidate candidate = sampleCandidate(randomNumber.x, lightObjectPdf);
      // Sample the selected triangle
      vec2 uv = vec2(randomNumber.y, randomNumber.z);
      float area;
      lightSampleTriangle = NEECacheUtils.calculateLightSampleFromTriangle(
        candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, position, coneRadius, coneSpreadAngle, area);
      triangleID = -1;
    }
    return lightSampleTriangle;
  }
}

struct NEECache
{
  static vec3 getCenter()
  {
    return cameraGetWorldPosition(cb.camera);
  }

  // This is an advanced form from Johannes Jendersie (aligned log grid):
  // https://confluence.nvidia.com/display/~jjendersie/Spatial+Cache+Placement
  // It discretizes the transitions such that there are less degenerate cases at the LOD boundaries.
  // triangleNormal: A normal to apply jittering in the tangential plane. If (0), jittering is disabled.
  //      TODO: it would be possible to use an unidirectional jitter in this case... e.g. for volumes
  // jitterScale: 1.0 will jitter by the cell width while larger numbers will increase the blur and 0 will disable jittering.
  #define HASH_GRID_MIN_LOG_LEVEL -127 // Exponent bias. See hash function below, uses 8 Bits for the level
  static int4 computeLogGridPos(
      float3 samplePos,
      const float3 cameraPos,
      const float distance /*Euclidean*/,
      const float base,
      const float baseLog,
      const float resolution,
      const f16vec3 triangleNormal,
      uint jitterRnd,        // 32 bit random number used for jittering
      const float jitterScale)
  {
    // Compute the initial level for the hit point.
    float lvlRnd = 0.0;
    if (jitterScale != 0)
    {
        // Jittering the level helps when moving around. Caches from the next level are then discovered early and
        // can be populated with new information, before they get the major contributor for the current queries.
        // const float lvlRnd = ((jitterRnd & 0xFF) / float(0xFF)) - 0.5; // Linear interpolation
        // More focussed on the central level than linear interpolation:
        lvlRnd = ((jitterRnd & 0x7F) / float(0x7F));
        lvlRnd = lvlRnd * lvlRnd * 0.5;
        if (jitterRnd & 0x80)
            lvlRnd = -lvlRnd;
    }
    int lvl = floor(log(max(1e-30f, distance)) / baseLog + lvlRnd);
    lvl = max(lvl, HASH_GRID_MIN_LOG_LEVEL);  // Safetynet when log() returns something too small.
    // Get the distance to where the level begins and derive a voxel size from it.
    // (If we would use exp(lvl+1) we would get the distance to the end, but the resolution parameter is somewhat
    // arbitrary anyways. However, we need this minimum distance below for the alignment.
    float levelDist = exp(lvl * baseLog);
    float voxelSize = levelDist / resolution;

    // Jittering to reduce grid artifacts.
    // Note that the current version is not working for volumes (which would be simple to add by sampling a general
    // direction on the sphere).
    if (jitterScale != 0 && dot(triangleNormal, triangleNormal) > 0.f)
    {
        // Add a translation in the geometric tangential plane to avoid jumping away from surfaces.
        f16vec3 b0 = 0;
        f16vec3 b1 = 0;
        calcOrthonormalBasis(triangleNormal, b0, b1);
        float continousSize = jitterScale * distance / resolution;
        // We use 8 random bits per dimension which is enough for a cosmetic jittering
        const float u0 = ((jitterRnd >> 8) & 0xFF) / float(0xFF);
        const float u1 = ((jitterRnd >> 16) & 0xFF) / float(0xFF);
        const float u2 = ((jitterRnd >> 24) & 0xFF) / float(0xFF);
        samplePos += (vec3(u0, u1, u2) - 0.5) * 1.0 * continousSize;
    }

    // Add an irrational number as an offset to avoid that objects in the 0-planes will lie on the boundary
    // between two voxels.
    const float3 offPos = samplePos + 1.6180339887;
    const float3 offCam = cameraPos + 1.6180339887;
    const int3 gridPos = floor(offPos / voxelSize);
    return int4(gridPos, lvl - HASH_GRID_MIN_LOG_LEVEL);
  }

  static uint computeDirectionalHash(f16vec3 normal)
  {
#if 1
    return 0;
#else
    f16vec3 absNormal = abs(normal);
    float16_t maxAbsNormal = max(absNormal.x, max(absNormal.y, absNormal.z));
    uint result = 0;
    result |= (maxAbsNormal == absNormal.x) ? 1 : 0;
    result |= (maxAbsNormal == absNormal.y) ? 2 : 0;
    result |= (maxAbsNormal == absNormal.z) ? 4 : 0;
    maxAbsNormal = -maxAbsNormal;
    result |= (maxAbsNormal == absNormal.x) ? 8 : 0;
    result |= (maxAbsNormal == absNormal.y) ? 16 : 0;
    result |= (maxAbsNormal == absNormal.z) ? 32 : 0;
    return result;
#endif
  }

  static uint getSpatialHashValue(float3 position, f16vec3 normal, uint jitterRnd)
  {
    float resolution = cb.neeCacheArgs.resolution;
    float minDistance = cb.neeCacheArgs.minRange;
    int4 spatialHash = computeLogGridPos(position,
                                    getCenter(),
                                    max(minDistance, length(position - getCenter())),// const float distance /*Euclidean*/,
                                    0.0,                                             //const float base,
                                    1.0,                                             //const float baseLog,
                                    resolution,                                      //const float resolution,
                                    normal,
                                    jitterRnd,                                       // 32 bit random number used for jittering
                                    1.0                                              //const float jitterScale
                                    );

    uint hashDir8Bit = computeDirectionalHash(normal);

    //
    // 64 bit shading key:
    // 16 bits x
    // 16 bits y
    // 16 bits z
    //  8 bits logGridLevel
    //  8 bits normal
    //
    // 16 bits per component are more than enough!
    // It allows a resolution parameter of >20000 for base 1.5 or >16000 for base 2.
    const uint shadingKey0 = ((spatialHash.x & 0xFFFF) << 16)
                           | ((spatialHash.y & 0xFFFF));
    const uint shadingKey1 = ((spatialHash.z & 0xFFFF) << 16)
                           | ((spatialHash.w & 0xFF) << 8)
                           | ((hashDir8Bit & 0xFF));
    uint2 hashKey2 = uint2(shadingKey0, shadingKey1);
    uint hashKey = prospectorHash(hashKey2.x) ^ prospectorHash(hashKey2.y);
    return hashKey & (NEE_CACHE_TOTAL_PROBE - 1);
  }

  static uint getHashValue(int3 positionI)
  {
    uint hash = 0;
    hash ^= hashJenkins(positionI.x);
    hash ^= hashJenkins(positionI.y);
    hash ^= hashJenkins(positionI.z);
    return hash & (NEE_CACHE_TOTAL_PROBE - 1);
  }

  static uint getAddressJittered(vec3 position, f16vec3 normal, uint jitter)
  {
    return getSpatialHashValue(position, normal, jitter);
  }

  static int pointToOffset(vec3 position, f16vec3 normal, uint jitteredNumber)
  {
    return getAddressJittered(position, normal, jitteredNumber);
  }

  static NEECell getCell(int offset) {
    NEECell cell = {};
    cell.m_offset = offset;
    return cell;
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

  static bool shouldUseHigherBounceNeeCache(bool isSpecularLobe, float16_t isotropicRoughness)
  {
    // ReSTIR GI can handle diffuse rays quite well, but not for highly specular surfaces.
    // Skip diffuse and rough specular surfaces to improve performance.
    // The roughness threshold here is based on experiment.
    const float16_t minRoughness = 0.1;
    return cb.neeCacheArgs.enableModeAfterFirstBounce == NeeEnableMode::SpecularOnly ? isSpecularLobe && isotropicRoughness < minRoughness : true;
  }
}

