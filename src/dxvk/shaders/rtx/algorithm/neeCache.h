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

struct NEECache
{
  static int cellToAddress(int3 cellID)
  {
    int idx =
      cellID.z * RADIANCE_CACHE_PROBE_RESOLUTION * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.y * RADIANCE_CACHE_PROBE_RESOLUTION +
      cellID.x;

    return idx * RADIANCE_CACHE_ELEMENTS * RADIANCE_CACHE_ELEMENT_SIZE;
  }

  static int pointToAddress(vec3 position)
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
    return cellToAddress(UVWi);
  }

  static void clearTask(int3 cellID)
  {
    int address = cellToAddress(cellID);
    RadianceCacheTask.Store(address, 0);
  }

  static int getTaskCount(int3 cellID)
  {
    int address = cellToAddress(cellID);
    uint count = RadianceCacheTask.Load(address);
    return min(count, RADIANCE_CACHE_ELEMENTS - 1);
  }

  static int getTaskAddress(int3 cellID, int taskID)
  {
    if (taskID >= RADIANCE_CACHE_ELEMENTS - 1)
    {
      return -1;
    }

    int baseAddress = cellToAddress(cellID);
    return baseAddress + (taskID + 1) * RADIANCE_CACHE_ELEMENT_SIZE;
  }

  static int2 getTask(int3 cellID, int taskID)
  {
    int address = getTaskAddress(cellID, taskID);
    if (address == -1)
    {
      return int2(-1);
    }
    return RadianceCacheTask.Load2(address);
  }

  static bool insertTask(int3 cellID, uint2 task)
  {
    int baseAddress = cellToAddress(cellID);
    uint oldLength;
    RadianceCacheTask.InterlockedAdd(baseAddress, 1, oldLength);

    if (oldLength < RADIANCE_CACHE_ELEMENTS - 1)
    {
      RadianceCacheTask.Store2(baseAddress + (oldLength + 1) * RADIANCE_CACHE_ELEMENT_SIZE, task);
      return true;
    }
    return false;
  }
  
}
#endif
