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

#include <stdint.h>

namespace fast {
  enum SIMD {
    Invalid = 0,
    None,
    SSE2,
    SSE3,
    SSE4_1,
    AVX2,
    AVX512,
  };
  /**
    * \brief Query the SIMD support level of the CPU currently in use by this process
    */
  SIMD getSimdSupportLevel();

  /**
    * \brief Finds minimum and maximum value in an array of unsigned integers
    *
    * count: number of integers
    * data: array of unsigned integers
    * minOut: minimum value in array determined by operation
    * maxOut: maximum value in array determined by operation
    * optional:
    *   sentinelIgnore: enable/disable mode to ignore specific values in array
    *   sentinelValue: specific value to ignore if mode enabled
    * 
    * Supports unsigned 32-bit and 16-bit integers.  All other uses undefined.
    */
  template<typename T>
  void findMinMax(const uint32_t count, const T* data, uint32_t& minOut, uint32_t& maxOut, const bool sentinelIgnore = false, const T sentinelValue = 0);

  /**
    * \brief Performs the following operation on an array of unsigned integers, (D[i] = S[i] - V)
    *
    * dstData: array of unsigned integers to write data
    * srcData: array of unsigned integers to read data
    * count: number of integers
    * value: value to subtract from array
    * optional:
    *   sentinelIgnore: enable/disable mode to ignore specific values in array
    *   sentinelValue: specific value to ignore if mode enabled
    *
    * Supports unsigned 32-bit and 16-bit integers.  All other uses undefined.
    */
  template<typename T>
  void copySubtract(T* dstData, const T* srcData, const uint32_t count, const T value, const bool ignoreSentinel = false, const T sentinelValue = 0);

  /**
    * \brief Memory copy function that uses threads internally, can be useful for very large memcpy's
    *
    * dest: memory to write to
    * src: memory to copy from
    * count: number of bytes to copy
    * chunkSize: how many bytes to process per thread
    */
  void parallel_memcpy(void* dest, const void* src, const size_t count, const size_t chunkSize = 4096);

  /**
    * \brief Returns the index of the nth set bit
    *
    * num: input number to search within
    * n: which set bit
    * 
    * Supports unsigned 32-bit, 16-bit 8-bit integers.  All other uses undefined.
    */
  template<typename T>
  T findNthBit(const T num, const T n);
}