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
#pragma once

#define RADIANCE_CACHE_PROBE_RESOLUTION 20
#define RADIANCE_CACHE_ELEMENTS 16
// Element size in bytes
#define RADIANCE_CACHE_ELEMENT_SIZE 4 * 2

#ifndef __cplusplus

struct NEECandidate
{
  uint2 m_data;

  bool isValid()
  {
    return m_data.y != 0xffffffff;
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
    return m_data.y;
  }

  uint2 getIDData()
  {
    uint2 data = m_data;
    data.x &= 0xffffff;
    return data;
  }

  [mutating] void setPrimitiveID(int primitiveID)
  {
    m_data.y = primitiveID;
  }

  float16_t getSampleThreshold()
  {
    uint8_t threshold = (m_data.x >> 24) & 0xff;
    return float16_t(threshold) / 255.0f;
  }

  [mutating] void setSampleThreshold(float16_t threshold)
  {
    uint thresholdI = threshold * 255.0f;
    m_data.x = (m_data.x & 0xffffff) | (thresholdI << 24);
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
  int m_baseAddress;

  static int indexToOffset(int idx)
  {
    return (idx + 1) * RADIANCE_CACHE_ELEMENT_SIZE;
  }

  bool isValid() { return m_baseAddress != -1; }

  void setTaskCount(int count)
  {
    RadianceCacheTask.Store(m_baseAddress, count);
  }

  int getTaskCount()
  {
    uint count = RadianceCacheTask.Load(m_baseAddress);
    return min(count, RADIANCE_CACHE_ELEMENTS - 1);
  }

  int2 getTask(int idx)
  {
    return RadianceCacheTask.Load2(m_baseAddress + indexToOffset(idx));
  }

  bool insertTask(uint2 task)
  {
    uint oldLength;
    RadianceCacheTask.InterlockedAdd(m_baseAddress, 1, oldLength);

    if (oldLength < RADIANCE_CACHE_ELEMENTS - 1)
    {
      RadianceCacheTask.Store2(m_baseAddress + indexToOffset(oldLength), task);
      return true;
    }
    return false;
  }

  int getCandidateCount()
  {
    uint count = RadianceCache.Load(m_baseAddress);
    return min(count, RADIANCE_CACHE_ELEMENTS - 1);
  }

  void setCandidateCount(int count)
  {
    RadianceCache.Store(m_baseAddress, count);
  }

  NEECandidate getCandidate(int idx)
  {
    return NEECandidate.create(RadianceCache.Load2(m_baseAddress + indexToOffset(idx)));
  }

  void setCandidate(int idx, NEECandidate candidate)
  {
    return RadianceCache.Store2(m_baseAddress + indexToOffset(idx), candidate.m_data);
  }

  NEECandidate sampleCandidate(float sampleThreshold)
  {
    int count = getCandidateCount();

#define UNIFORM_SAMPLING 0
#if UNIFORM_SAMPLING
    return getCandidate(min(sampleThreshold * count, count-1));
#else
    for (int i = 0; i < count; ++i)
    {
      NEECandidate candidate = getCandidate(i);
      if (candidate.getSampleThreshold() >= sampleThreshold)
      {
        return candidate;
      }
    }
    return NEECandidate.create(uint2(0xffffffff));
#endif
  }
}

struct NEECache
{
  static float s_extend = 2000;
  static int cellToAddress(int3 cellID)
  {
    if (any(cellID == -1))
    {
      return -1;
    }

    int idx =
      cellID.z * RADIANCE_CACHE_PROBE_RESOLUTION * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.y * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.x;

    return idx * RADIANCE_CACHE_ELEMENTS * RADIANCE_CACHE_ELEMENT_SIZE;
  }

  static int3 pointToCell(vec3 position)
  {
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - s_extend * 0.5;
    vec3 UVW = (position - origin) / s_extend;

    // jitter or not
#if 1
    vec3 UVWi = UVW * RADIANCE_CACHE_PROBE_RESOLUTION - 0.5;
    vec3 fracUVWi = fract(UVWi);

    RNG rng = createRNGPosition(position, cb.frameIdx);
    ivec3 offset;
    offset.x = getNextSampleBlueNoise(rng) > fracUVWi.x ? 0 : 1;
    offset.y = getNextSampleBlueNoise(rng) > fracUVWi.y ? 0 : 1;
    offset.z = getNextSampleBlueNoise(rng) > fracUVWi.z ? 0 : 1;
    ivec3 cellID = ivec3(UVWi) + offset;
    if (any(cellID < 0) || any(cellID > RADIANCE_CACHE_PROBE_RESOLUTION-1))
    {
      return int3(-1);
    }
    return cellID;
#else
    ivec3 UVWi = UVW * RADIANCE_CACHE_PROBE_RESOLUTION;
    if (any(UVWi < 0) || any(UVWi > RADIANCE_CACHE_PROBE_RESOLUTION-1))
    {
      return int3(-1);
    }
    return UVWi;
#endif
  }

  static float getCellSize()
  {
    return s_extend / RADIANCE_CACHE_PROBE_RESOLUTION;
  }

  static vec3 cellToPoint(ivec3 cellID)
  {
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - s_extend * 0.5;
    vec3 UVW = vec3(cellID + 0.5) / RADIANCE_CACHE_PROBE_RESOLUTION;
    vec3 position = UVW * s_extend + origin;
    return position;
  }

  static int pointToAddress(vec3 position)
  {
    ivec3 UVWi = pointToCell(position);
    if (any(UVWi == -1))
    {
      return -1;
    }
    return cellToAddress(UVWi);
  }

  static NEECell createCell(int3 cellID)
  {
    NEECell cell = {};
    cell.m_baseAddress = cellToAddress(cellID);
    return cell;
  }

  static NEECell createCell(vec3 point)
  {
    return createCell(pointToCell(point));
  }
}
#endif
