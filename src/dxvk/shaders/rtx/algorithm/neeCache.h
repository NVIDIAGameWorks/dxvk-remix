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

  [mutating] void setInvalid()
  {
    m_data.y = 0xffffffff;
  }

  int getSurfaceID()
  {
    uint surfaceID = m_data.x & 0xffffff;
    return surfaceID;
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
    uint threshold = (m_data.x >> 24) & 0xff;
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
    return min(count, getMaxCandidateCount());
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

  NEECandidate sampleCandidate(float sampleThreshold, out float pdf)
  {
    int count = getCandidateCount();

#if 0
    pdf = 1.0 / count;
    return getCandidate(min(sampleThreshold * count, count-1));
#else
    NEECandidate candidate;
    candidate.setInvalid();
    int i = 0;
    float lastCdf = 0;
    pdf = 1;
    for (; i < count; ++i)
    {
      candidate = getCandidate(i);
      pdf = candidate.getSampleThreshold() - lastCdf;
      lastCdf = pdf;
      if (candidate.getSampleThreshold() >= sampleThreshold)
      {
        return candidate;
      }
    }
    return candidate;
#endif
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
    return RADIANCE_CACHE_ELEMENTS-1;
  }
}

struct NEECache
{
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

  static int cellToOffset(int3 cellID)
  {
    int idx =
      cellID.z * RADIANCE_CACHE_PROBE_RESOLUTION * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.y * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.x;
    return idx;
  }

  static int3 offsetToCell(int offset)
  {
    if (offset == -1)
    {
      return int3(-1);
    }
    int3 cellID;
    const int zSize = RADIANCE_CACHE_PROBE_RESOLUTION * RADIANCE_CACHE_PROBE_RESOLUTION;
    const int ySize = RADIANCE_CACHE_PROBE_RESOLUTION;

    cellID.z = offset / zSize;
    offset -= cellID.z * zSize;

    cellID.y = offset / ySize;
    offset -= cellID.y * ySize;

    cellID.x = offset;
    return cellID;
  }

  static int3 pointToCell(vec3 position, bool jittered)
  {
    float extend = cb.neeCacheRange;
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = (position - origin) / extend;

    // jitter or not
    if(jittered)
    {
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
    }
    else
    {
      ivec3 UVWi = UVW * RADIANCE_CACHE_PROBE_RESOLUTION;
      if (any(UVWi < 0) || any(UVWi > RADIANCE_CACHE_PROBE_RESOLUTION-1))
      {
        return int3(-1);
      }
      return UVWi;
    }
  }

  static int pointToOffset(vec3 position, bool jittered)
  {
    int3 cellID = pointToCell(position, jittered);
    if (all(cellID >= 0) && all(cellID < RADIANCE_CACHE_PROBE_RESOLUTION))
    {
      return cellToOffset(cellID);
    }
    return -1;
  }

  static float getCellSize()
  {
    return cb.neeCacheRange / RADIANCE_CACHE_PROBE_RESOLUTION;
  }

  static float getVolumeSize()
  {
    return cb.neeCacheRange;
  }

  static vec3 cellToCenterPoint(ivec3 cellID)
  {
    float extend = cb.neeCacheRange;
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = vec3(cellID + 0.5) / RADIANCE_CACHE_PROBE_RESOLUTION;
    vec3 position = UVW * extend + origin;
    return position;
  }

  static int pointToAddress(vec3 position, bool jittered)
  {
    ivec3 UVWi = pointToCell(position, jittered);
    if (any(UVWi == -1))
    {
      return -1;
    }
    return cellToAddress(UVWi);
  }

  static NEECell getCell(int3 cellID)
  {
    NEECell cell = {};
    cell.m_baseAddress = cellToAddress(cellID);
    return cell;
  }

  static NEECell findCell(vec3 point, bool jittered)
  {
    return getCell(pointToCell(point, jittered));
  }

  static void storeThreadTask(int2 pixel, /*vec3 point, bool jittered,*/ uint4 data)
  {
    //int offset = pointToOffset(point, jittered);
    RadianceCacheThreadTask[pixel] = data;//uint4(offset, -1,-1,-1);
  }

  static uint4 loadThreadTask(int2 pixel)
  {
    //int offset = RadianceCacheThreadTask[pixel].x;
    //return offsetToCell(offset);
    return RadianceCacheThreadTask[pixel];
  }
}

vec2 uvToBary(vec2 uv)
{
  return uv.x + uv.y < 1 ? uv : 1 - uv.yx;
}

float getLightSamplingSolidAnglePDF(float triangleArea, vec3 samplePosition, f16vec3 sampleNormal, vec3 surfacePosition)
{
  float areaPdf = 1.0 / triangleArea;
  vec3 offset = samplePosition - surfacePosition;
  float r2 = dot(offset, offset);
  float cosPhi = abs(dot(normalize(offset), sampleNormal));
  return cosPhi > 0 ? areaPdf * r2 / cosPhi : 0;
}
#endif
