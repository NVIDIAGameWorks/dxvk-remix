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

  int2 getCandidate(int idx)
  {
    return RadianceCache.Load2(m_baseAddress + indexToOffset(idx));
  }

  void setCandidate(int idx, int2 candidate)
  {
    return RadianceCache.Store2(m_baseAddress + indexToOffset(idx), candidate);
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

  static int3 pointToCell(vec3 position)
  {
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    float extend = 1000;
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = (position - origin) / extend;
    if (any(UVW < 0) || any(UVW >= 1))
    {
      return -1;
    }
    ivec3 UVWi = UVW * RADIANCE_CACHE_PROBE_RESOLUTION;
    return UVWi;
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
