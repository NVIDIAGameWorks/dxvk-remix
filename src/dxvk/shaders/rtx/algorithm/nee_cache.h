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

#define NEE_CACHE_PROBE_RESOLUTION 32
#define NEE_CACHE_ELEMENTS 16
// Element size in bytes
#define NEE_CACHE_ELEMENT_SIZE 4 * 2
#define NEE_CACHE_TASK_SIZE 4
#define NEE_CACHE_EMPTY_TASK 0xffffffff

#ifndef __cplusplus

#include "rtx/algorithm/nee_cache_light.slangh"

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
  int m_baseAddress;

  static int indexToOffset(int idx)
  {
    return (idx + 1) * NEE_CACHE_ELEMENT_SIZE;
  }

  int getTaskAddress(int idx)
  {
    return m_baseAddress + idx * NEE_CACHE_TASK_SIZE;
  }

  bool isValid() { return m_baseAddress != -1; }

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

  int getCandidateCount()
  {
    uint count = NeeCache.Load(m_baseAddress);
    return min(count, getMaxCandidateCount());
  }

  void setCandidateCount(int count)
  {
    NeeCache.Store(m_baseAddress, count);
  }

  NEECandidate getCandidate(int idx)
  {
    return NEECandidate.create(NeeCache.Load2(m_baseAddress + indexToOffset(idx)));
  }

  void setCandidate(int idx, NEECandidate candidate)
  {
    return NeeCache.Store2(m_baseAddress + indexToOffset(idx), candidate.m_data);
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
      cellID.z * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION +
      cellID.y * NEE_CACHE_PROBE_RESOLUTION +
      cellID.x;

    return idx * NEE_CACHE_ELEMENTS * NEE_CACHE_ELEMENT_SIZE;
  }

  static int cellToOffset(int3 cellID)
  {
    int idx =
      cellID.z * NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION +
      cellID.y * NEE_CACHE_PROBE_RESOLUTION +
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
    const int zSize = NEE_CACHE_PROBE_RESOLUTION * NEE_CACHE_PROBE_RESOLUTION;
    const int ySize = NEE_CACHE_PROBE_RESOLUTION;

    cellID.z = offset / zSize;
    offset -= cellID.z * zSize;

    cellID.y = offset / ySize;
    offset -= cellID.y * ySize;

    cellID.x = offset;
    return cellID;
  }

  static int3 pointToCell(vec3 position, bool jittered)
  {
    float extend = cb.neeCacheArgs.range;
    vec3 cameraPos = cameraGetWorldPosition(cb.camera);
    vec3 origin = cameraPos - extend * 0.5;
    vec3 UVW = (position - origin) / extend;

    // jitter cell ID
    if(jittered && cb.neeCacheArgs.enableJittering)
    {
      vec3 UVWi = UVW * NEE_CACHE_PROBE_RESOLUTION - 0.5;
      vec3 fracUVWi = fract(UVWi);

      RNG rng = createRNGPosition(position, cb.frameIdx);
      ivec3 offset;
      offset.x = getNextSampleBlueNoise(rng) > fracUVWi.x ? 0 : 1;
      offset.y = getNextSampleBlueNoise(rng) > fracUVWi.y ? 0 : 1;
      offset.z = getNextSampleBlueNoise(rng) > fracUVWi.z ? 0 : 1;
      ivec3 cellID = ivec3(UVWi) + offset;
      if (any(cellID < 0) || any(cellID > NEE_CACHE_PROBE_RESOLUTION-1))
      {
        return int3(-1);
      }
      return cellID;
    }
    else
    {
      ivec3 UVWi = UVW * NEE_CACHE_PROBE_RESOLUTION;
      if (any(UVWi < 0) || any(UVWi > NEE_CACHE_PROBE_RESOLUTION-1))
      {
        return int3(-1);
      }
      return UVWi;
    }
  }

  static int pointToOffset(vec3 position, bool jittered)
  {
    int3 cellID = pointToCell(position, jittered);
    if (all(cellID >= 0) && all(cellID < NEE_CACHE_PROBE_RESOLUTION))
    {
      return cellToOffset(cellID);
    }
    return -1;
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
    vec3 UVW = vec3(cellID + 0.5) / NEE_CACHE_PROBE_RESOLUTION;
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
}

#endif
