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
#include <smmintrin.h>
#include <math.h>
#include <intrin.h>
#include "util_math.h"
#include "util_fastops.h"
#include "vulkan/vk_platform.h"
#include <algorithm>
#include <ppl.h>
#include "util_fastops.h"

#define SSE_ENABLE ((fast::g_simdSupportLevel != fast::SIMD::None) && 1)

namespace fast {
  static SIMD initSimdSupport() {
    SIMD simdSupport = SIMD::None;

    // Query CPUID info with EAX = 7
    int result[4];
    __cpuid(result, 7);

    // TODO(REMIX-1112): AVX512 seems to have issues, disable while we get more HW to test
  #if 0
    if ((result[1] & (1 << 16)) != 0) {
      simdSupport = SIMD::AVX512; // Foundation only
    } else 
  #endif

    if ((result[1] & (1 << 5)) != 0) {
      simdSupport = SIMD::AVX2;
    } else {
      // Query CPUID info with EAX = 1
      __cpuid(result, 1);

      // See the Intel Architecture Instruction Set Extensions Programming Reference, Table 1-5.
      if ((result[2] & (1 << 19)) != 0) {
        simdSupport = SIMD::SSE4_1;
      } else if ((result[2] & (1 << 0)) != 0) {
        simdSupport = SIMD::SSE3;
      } else if ((result[3] & (1 << 26)) != 0) {
        simdSupport = SIMD::SSE2;
      } else {
        simdSupport = SIMD::None;
      }
    }

    return simdSupport;
  }

  static const SIMD g_simdSupportLevel = initSimdSupport();

  static bool initBmi2Support() {
    int result[4];
    __cpuid(result, 0x7);
    return (result[1] & (1 << 8));
  }

  static const bool g_supportsBMI2 = initBmi2Support();

  SIMD getSimdSupportLevel() {
    return g_simdSupportLevel;
  }

  __forceinline __m128i _mm_cmple_epu16(const __m128i& x, const __m128i& y) {
    return _mm_cmpeq_epi16(_mm_subs_epu16(x, y), _mm_setzero_si128());
  }

  __forceinline void minMax16_SSE2(const __m128i& values, __m128i& min, __m128i& max) {
    __m128i cmp = _mm_cmple_epu16(max, values);
    max = _mm_or_si128(_mm_and_si128(cmp, values), _mm_andnot_si128(cmp, max));
    cmp = _mm_cmple_epu16(values, min);
    min = _mm_or_si128(_mm_and_si128(cmp, values), _mm_andnot_si128(cmp, min));
  }

  __forceinline void minMax16_SSE41(const __m128i& values, __m128i& min, __m128i& max) {
    min = _mm_min_epu16(min, values);
    max = _mm_max_epu16(max, values);
  }

  template<SIMD V>
  __forceinline void minMax16_SSE(const __m128i& values, __m128i& min, __m128i& max) {
    switch (V) {
    case SSE4_1:
      minMax16_SSE41(values, min, max);
      break;
    case SSE3:
    case SSE2:
      minMax16_SSE2(values, min, max);
      break;
    default:
      throw;
    }
  }

  __forceinline __m128i _mm_min_epu32_SSE2(__m128i a, __m128i b) {
    static const __m128i sign_bit = _mm_set1_epi32(INT_MIN);
    __m128i mask = _mm_cmplt_epi32(_mm_xor_si128(a, sign_bit), _mm_xor_si128(b, sign_bit));
    return _mm_or_si128(_mm_andnot_si128(mask, b), _mm_and_si128(mask, a));
  }

  __forceinline __m128i _mm_max_epu32_SSE2(__m128i a, __m128i b) {
    static const __m128i sign_bit = _mm_set1_epi32(INT_MIN);
    __m128i mask = _mm_cmplt_epi32(_mm_xor_si128(a, sign_bit), _mm_xor_si128(b, sign_bit));
    return _mm_or_si128(_mm_andnot_si128(mask, a), _mm_and_si128(mask, b));
  }

  __forceinline void minMax32_SSE2(const __m128i& values, __m128i& min, __m128i& max) {
    // Fused _mm_min_epu32_SSE2 and _mm_max_epu32_SSE2 for better perf
    static const __m128i sign_bit = _mm_set1_epi32(INT_MIN);
    __m128i un_values = _mm_xor_si128(values, sign_bit);

    __m128i mask = _mm_cmplt_epi32(_mm_xor_si128(min, sign_bit), un_values);
    min = _mm_or_si128(_mm_andnot_si128(mask, values), _mm_and_si128(mask, min));

    mask = _mm_cmplt_epi32(_mm_xor_si128(max, sign_bit), un_values);
    max = _mm_or_si128(_mm_andnot_si128(mask, max), _mm_and_si128(mask, values));
  }

  __forceinline void minMax32_SSE41(const __m128i& values, __m128i& min, __m128i& max) {
    min = _mm_min_epu32(min, values);
    max = _mm_max_epu32(max, values);
  }

  template<SIMD V>
  __forceinline void minMax32_SSE(const __m128i& values, __m128i& min, __m128i& max) {
    switch (V) {
    case SIMD::AVX512:
    case SIMD::AVX2:
    case SIMD::SSE4_1:
      minMax32_SSE41(values, min, max);
      break;
    case SIMD::SSE3:
    case SIMD::SSE2:
      minMax32_SSE2(values, min, max);
      break;
    default:
      throw;
    }
  }

  void minMaxWithSentinelValue16_SSE2(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    static const __m128i all_one = _mm_set1_epi16((uint16_t)0xFFFF);
    __m128i cmp = _mm_cmpeq_epi16(values, ignore); // cmp = 1 if values[i] == ksentinelValue, 0 otherwise
    __m128i values_masked = _mm_and_si128(values, _mm_xor_si128(cmp, all_one));
    __m128i min_masked = _mm_or_si128(values_masked, _mm_and_si128(cmp, min));
    __m128i max_masked = _mm_or_si128(values_masked, _mm_and_si128(cmp, max));
    cmp = _mm_cmple_epu16(max, max_masked);
    max = _mm_or_si128(_mm_and_si128(cmp, max_masked), _mm_andnot_si128(cmp, max));
    cmp = _mm_cmple_epu16(min_masked, min);
    min = _mm_or_si128(_mm_and_si128(cmp, min_masked), _mm_andnot_si128(cmp, min));
  }

  __forceinline void minMaxWithSentinelValue16_SSE4_1(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    __m128i cmp = _mm_cmpeq_epi16(values, ignore);  // cmp = 1 if values[i] == ignore, 0 otherwise
    __m128i min_masked = _mm_blendv_epi8(values, min, cmp);  // min_masked[i] = values[i] if values[i] != ignore, min[i] otherwise
    __m128i max_masked = _mm_blendv_epi8(values, max, cmp);  // max_masked[i] = values[i] if values[i] != ignore, max[i] otherwise
    min = _mm_min_epu16(min, min_masked);
    max = _mm_max_epu16(max, max_masked);
  }

  template<SIMD V>
  __forceinline void minMaxWithSentinelValue16_SSE(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    switch (V) {
    case SIMD::AVX512:
    case SIMD::AVX2:
    case SIMD::SSE4_1:
      minMaxWithSentinelValue16_SSE4_1(values, ignore, min, max);
      break;
    case SIMD::SSE3:
    case SIMD::SSE2:
      minMaxWithSentinelValue16_SSE2(values, ignore, min, max);
      break;
    default:
      throw;
    }
  }

  __forceinline void minMaxWithSentinelValue32_SSE2(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    static const __m128i all_one = _mm_set1_epi32(0xFFFFFFFF);
    __m128i cmp = _mm_cmpeq_epi32(values, ignore); // cmp = 1 if values[i] == ksentinelValue, 0 otherwise
    __m128i values_masked = _mm_and_si128(values, _mm_xor_si128(cmp, all_one));
    __m128i min_masked = _mm_or_si128(values_masked, _mm_and_si128(cmp, min));
    __m128i max_masked = _mm_or_si128(values_masked, _mm_and_si128(cmp, max));
    
    max = _mm_max_epu32_SSE2(max, max_masked);
    min = _mm_min_epu32_SSE2(min, min_masked);
  }

  __forceinline void minMaxWithSentinelValue32_SSE4_1(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    __m128i cmp = _mm_cmpeq_epi32(values, ignore);  // cmp = 1 if values[i] == ignore, 0 otherwise
    __m128i min_masked = _mm_blendv_epi8(values, min, cmp);  // min_masked[i] = values[i] if values[i] != ignore, min[i] otherwise
    __m128i max_masked = _mm_blendv_epi8(values, max, cmp);  // max_masked[i] = values[i] if values[i] != ignore, max[i] otherwise
    min = _mm_min_epu32(min, min_masked);
    max = _mm_max_epu32(max, max_masked);
  }

  template<SIMD V>
  __forceinline void minMaxWithSentinelValue32_SSE(const __m128i& values, const __m128i& ignore, __m128i& min, __m128i& max) {
    switch (V) {
    case SIMD::AVX512:
    case SIMD::AVX2:
    case SIMD::SSE4_1:
      minMaxWithSentinelValue32_SSE4_1(values, ignore, min, max);
      break;
    case SIMD::SSE3:
    case SIMD::SSE2:
      minMaxWithSentinelValue32_SSE2(values, ignore, min, max);
      break;
    default:
      throw;
    }
  }

  template<SIMD V>
  __forceinline uint16_t extractMin16_SSE(const __m128i& min) {
    if (V <= SIMD::SSE3) {
      uint16_t minOut16 = (uint16_t) _mm_extract_epi16(min, 0);
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 1));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 2));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 3));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 4));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 5));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 6));
      minOut16 = std::min(minOut16, (uint16_t) _mm_extract_epi16(min, 7));
      return minOut16;
    } else {
      return _mm_extract_epi16(_mm_minpos_epu16(min), 0);
    }
  }

  template<SIMD V>
  __forceinline uint16_t extractMax16_SSE(const __m128i& max) {
    if (V <= SIMD::SSE3) {
      uint16_t maxOut16 = (uint16_t) _mm_extract_epi16(max, 0);
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 1));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 2));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 3));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 4));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 5));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 6));
      maxOut16 = std::max(maxOut16, (uint16_t) _mm_extract_epi16(max, 7));
      return maxOut16;
    } else {
      static const __m128i all_one = _mm_set1_epi16((uint16_t) 0xFFFF);
      __m128i not_maxpos = _mm_minpos_epu16(_mm_xor_si128(all_one, max));
      return (~_mm_cvtsi128_si32(not_maxpos)) & 0xFFFF;
    }
  }

  __forceinline uint32_t extractMin32_SSE(const __m128i& min) {
    uint32_t minOut32 = (uint32_t) _mm_extract_epi32(min, 0);
    minOut32 = std::min(minOut32, (uint32_t) _mm_extract_epi32(min, 1));
    minOut32 = std::min(minOut32, (uint32_t) _mm_extract_epi32(min, 2));
    minOut32 = std::min(minOut32, (uint32_t) _mm_extract_epi32(min, 3));
    return minOut32;
  }

  __forceinline uint32_t extractMax32_SSE(const __m128i& max) {
    uint32_t maxOut32 = (uint32_t) _mm_extract_epi32(max, 0);
    maxOut32 = std::max(maxOut32, (uint32_t) _mm_extract_epi32(max, 1));
    maxOut32 = std::max(maxOut32, (uint32_t) _mm_extract_epi32(max, 2));
    maxOut32 = std::max(maxOut32, (uint32_t) _mm_extract_epi32(max, 3));
    return maxOut32;
  }

  template<SIMD V>
  __forceinline void findMinMax16_SSE(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];

    __m128i min = _mm_set1_epi16(minOut16);
    __m128i max = _mm_set1_epi16(maxOut16);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m128i values = _mm_loadu_si128((__m128i*) & data[i]);
      minMax16_SSE<V>(values, min, max);
    }

    // Use extract to find the minimum and maximum values in the registers
    minOut16 = extractMin16_SSE<V>(min);
    maxOut16 = extractMax16_SSE<V>(max);

    // Process the remainder (if count not aligned to 8)
    for (uint32_t i = alignedCount; i < count; ++i) {
      minOut16 = std::min(minOut16, data[i]);
      maxOut16 = std::max(maxOut16, data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }

  __forceinline void findMinMax16_AVX2(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut) {
    const uint32_t numLanes = 16;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);
    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];

    __m256i min = _mm256_set1_epi16(data[0]);
    __m256i max = _mm256_set1_epi16(data[0]);

    for (uint32_t i = 0; i < alignedCount; i+=numLanes) {
      __m256i values = _mm256_loadu_si256((__m256i*) & data[i]);
      min = _mm256_min_epu16(min, values);
      max = _mm256_max_epu16(max, values);
    }

    // Extract the min and max values from the minHr and maxHr vectors
    __m128i minMax128 = _mm_min_epu16(_mm256_castsi256_si128(min), _mm256_extracti128_si256(min, 1));
    minOut16 = extractMin16_SSE<SIMD::AVX2>(minMax128);

    minMax128 = _mm_max_epu16(_mm256_castsi256_si128(max), _mm256_extracti128_si256(max, 1));
    maxOut16 = extractMax16_SSE<SIMD::AVX2>(minMax128);

    // Process the remaining elements, if any
    for (uint32_t i = alignedCount; i < count; i++) {
      minOut16 = std::min(minOut16, data[i]);
      maxOut16 = std::max(maxOut16, data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }

  __forceinline void findMinMax16_slow(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut) {
    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];
    for (uint32_t i = 1; i < count; i++) {
      minOut16 = std::min(minOut16, data[i]);
      maxOut16 = std::max(maxOut16, data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }


  __forceinline void findMinMaxWithsentinelValue16_AVX2(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue) {
    const uint32_t numLanes = 16;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);
    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];

    __m256i min = _mm256_set1_epi16(data[0]);
    __m256i max = _mm256_set1_epi16(data[0]);
    __m256i sentinel = _mm256_set1_epi16(sentinelValue);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m256i values = _mm256_loadu_si256((__m256i*) & data[i]);
      // Create a mask based on the comparison of src with sentinel
      __m256i cmp = _mm256_cmpeq_epi16(values, sentinel);
      __m256i minMask = _mm256_blendv_epi8(values, min, cmp);
      __m256i maxMask = _mm256_blendv_epi8(values, max, cmp);

      min = _mm256_min_epu16(minMask, min);
      max = _mm256_max_epu16(maxMask, max);
    }

    // Horizontally reduce the min and max vectors
    __m128i minMax128 = _mm_min_epu16(_mm256_castsi256_si128(min), _mm256_extracti128_si256(min, 1));
    minOut16 = extractMin16_SSE<SIMD::AVX2>(minMax128);

    minMax128 = _mm_max_epu16(_mm256_castsi256_si128(max), _mm256_extracti128_si256(max, 1));
    maxOut16 = extractMax16_SSE<SIMD::AVX2>(minMax128);

    // Process the remaining elements, if any
    for (uint32_t i = alignedCount; i < count; i++) {
      minOut16 = std::min(minOut16, (data[i] == sentinelValue) ? minOut16 : data[i]);
      maxOut16 = std::max(maxOut16, (data[i] == sentinelValue) ? maxOut16 : data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }

  template<SIMD V>
  __forceinline void findMinMaxWithsentinelValue16_SSE(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];

    __m128i min = _mm_set1_epi16(minOut16);
    __m128i max = _mm_set1_epi16(maxOut16);
    __m128i ignore = _mm_set1_epi16(sentinelValue);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m128i values = _mm_loadu_si128((__m128i*) & data[i]);
      minMaxWithSentinelValue16_SSE<V>(values, ignore, min, max);
    }

    // Use extract to find the minimum and maximum values in the registers
    minOut16 = extractMin16_SSE<V>(min);
    maxOut16 = extractMax16_SSE<V>(max);

    // Process remaining elements (if count not aligned to 8)
    for (uint32_t i = alignedCount; i < count; ++i) {
      minOut16 = std::min(minOut16, (data[i] == sentinelValue) ? minOut16 : data[i]);
      maxOut16 = std::max(maxOut16, (data[i] == sentinelValue) ? maxOut16 : data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }

  __forceinline void findMinMaxWithsentinelValue16_slow(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue) {
    uint16_t minOut16 = data[0];
    uint16_t maxOut16 = data[0];
    for (uint32_t i = 1; i < count; i++) {
      minOut16 = std::min(minOut16, (data[i] == sentinelValue) ? minOut16 : data[i]);
      maxOut16 = std::max(maxOut16, (data[i] == sentinelValue) ? maxOut16 : data[i]);
    }

    minOut = (uint32_t) minOut16;
    maxOut = (uint32_t) maxOut16;
  }

  template<SIMD V>
  __forceinline void findMinMax32_SSE(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut) {
    const uint32_t numLanes = 4;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    minOut = data[0];
    maxOut = data[0];

    __m128i min = _mm_set1_epi32(minOut);
    __m128i max = _mm_set1_epi32(maxOut);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m128i values = _mm_loadu_si128((__m128i*) & data[i]);
      minMax32_SSE<V>(values, min, max);
    }

    // Use extract to find the minimum and maximum values in the registers
    minOut = extractMin32_SSE(min);
    maxOut = extractMax32_SSE(max);

    // Process the remainder (if count not aligned to 4)
    for (uint32_t i = alignedCount; i < count; ++i) {
      minOut = std::min(minOut, data[i]);
      maxOut = std::max(maxOut, data[i]);
    }
  }

  __forceinline void findMinMax32_AVX2(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    minOut = data[0];
    maxOut = data[0];

    __m256i min = _mm256_set1_epi32(data[0]);
    __m256i max = _mm256_set1_epi32(data[0]);

    for (uint32_t i = 0; i < alignedCount; i+= numLanes) {
      __m256i values = _mm256_loadu_si256((__m256i*) & data[i]);
      min = _mm256_min_epu32(min, values);
      max = _mm256_max_epu32(max, values);
    }

    // Extract the min and max values from the minHr and maxHr vectors
    __m128i minMax128 = _mm_min_epu32(_mm256_castsi256_si128(min), _mm256_extracti128_si256(min, 1));
    minOut = extractMin32_SSE(minMax128);

    minMax128 = _mm_max_epu32(_mm256_castsi256_si128(max), _mm256_extracti128_si256(max, 1));
    maxOut = extractMax32_SSE(minMax128);
    
    // Process the remaining elements, if any
    for (uint32_t i = alignedCount; i < count; ++i) {
      minOut = std::min(minOut, data[i]);
      maxOut = std::max(maxOut, data[i]);
    }
  }

  __forceinline void findMinMax32_slow(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut) {
    minOut = data[0];
    maxOut = data[0];
    for (uint32_t i = 1; i < count; i++) {
      minOut = std::min(minOut, data[i]);
      maxOut = std::max(maxOut, data[i]);
    }
  }

  __forceinline void findMinMaxWithsentinelValue32_AVX2(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);
    minOut = data[0];
    maxOut = data[0];

    __m256i min = _mm256_set1_epi32(data[0]);
    __m256i max = _mm256_set1_epi32(data[0]);
    __m256i sentinel = _mm256_set1_epi32(sentinelValue);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m256i values = _mm256_loadu_si256((__m256i*) & data[i]);
      // Create a mask based on the comparison of src with sentinel
      __m256i cmp = _mm256_cmpeq_epi32(values, sentinel);
      __m256i minMask = _mm256_blendv_epi8(values, min, cmp);
      __m256i maxMask = _mm256_blendv_epi8(values, max, cmp);

      min = _mm256_min_epu32(minMask, min);
      max = _mm256_max_epu32(maxMask, max);
    }

    // Horizontally reduce the min and max vectors
    __m128i minMax128 = _mm_min_epu32(_mm256_castsi256_si128(min), _mm256_extracti128_si256(min, 1));
    minOut = extractMin32_SSE(minMax128);

    minMax128 = _mm_max_epu32(_mm256_castsi256_si128(max), _mm256_extracti128_si256(max, 1));
    maxOut = extractMax32_SSE(minMax128);

    // Process the remaining elements, if any
    for (uint32_t i = alignedCount; i < count; i++) {
      minOut = std::min(minOut, (data[i] == sentinelValue) ? minOut : data[i]);
      maxOut = std::max(maxOut, (data[i] == sentinelValue) ? maxOut : data[i]);
    }
  }

  template<SIMD V>
  __forceinline void findMinMaxWithsentinelValue32_SSE(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue) {
    const uint32_t numLanes = 4;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    minOut = data[0];
    maxOut = data[0];

    __m128i min = _mm_set1_epi32(minOut);
    __m128i max = _mm_set1_epi32(maxOut);
    __m128i ignore = _mm_set1_epi32(sentinelValue);

    for (uint32_t i = 0; i < alignedCount; i += numLanes) {
      __m128i values = _mm_loadu_si128((__m128i*) & data[i]);
      minMaxWithSentinelValue32_SSE<V>(values, ignore, min, max);
    }

    // Use extract to find the minimum and maximum values in the registers
    minOut = extractMin32_SSE(min);
    maxOut = extractMax32_SSE(max);

    // Process remaining elements (if count not aligned to 4)
    for (uint32_t i = alignedCount; i < count; ++i) {
      minOut = std::min(minOut, (data[i] == sentinelValue) ? minOut : data[i]);
      maxOut = std::max(maxOut, (data[i] == sentinelValue) ? maxOut : data[i]);
    }
  }

  void findMinMaxWithsentinelValue32_slow(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue) {
    minOut = data[0];
    maxOut = data[0];
    for (uint32_t i = 1; i < count; i++) {
      minOut = std::min(minOut, (data[i] == sentinelValue) ? minOut : data[i]);
      maxOut = std::max(maxOut, (data[i] == sentinelValue) ? maxOut : data[i]);
    }
  }

  template<typename T>
  __forceinline void findMinMax(const uint32_t count, const T* data, uint32_t& minOut, uint32_t& maxOut, bool ignoreSentinel, const T sentinelValue/* = 0*/) {
    const bool useSSE = SSE_ENABLE && count >= 32;

    if (std::is_same<T, uint16_t>::value) {
      if (ignoreSentinel) {
        if (useSSE) {
          switch (g_simdSupportLevel) {
          case SIMD::AVX512:
          case SIMD::AVX2:
            findMinMaxWithsentinelValue16_AVX2(count, (uint16_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE4_1:
            findMinMaxWithsentinelValue16_SSE<SIMD::SSE4_1>(count, (uint16_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE3:
            findMinMaxWithsentinelValue16_SSE<SIMD::SSE3>(count, (uint16_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE2:
            findMinMaxWithsentinelValue16_SSE<SIMD::SSE2>(count, (uint16_t*) data, minOut, maxOut, sentinelValue);
            break;
          default:
            throw;
          }
        } else {
          findMinMaxWithsentinelValue16_slow(count, (uint16_t*) data, minOut, maxOut, sentinelValue);
        }
      } else {
        if (useSSE) {
          switch (g_simdSupportLevel) {
          case SIMD::AVX512:
          case SIMD::AVX2:
            findMinMax16_AVX2(count, (uint16_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE4_1:
            findMinMax16_SSE<SIMD::SSE4_1>(count, (uint16_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE3:
            findMinMax16_SSE<SIMD::SSE3>(count, (uint16_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE2:
            findMinMax16_SSE<SIMD::SSE2>(count, (uint16_t*) data, minOut, maxOut);
            break;
          default:
            throw;
          }
        } else {
          findMinMax16_slow(count, (uint16_t*) data, minOut, maxOut);
        }
      }
    } else if (std::is_same<T, uint32_t>::value) {
      if (ignoreSentinel) {
        if (useSSE) {
          switch (g_simdSupportLevel) {
          case SIMD::AVX512:
          case SIMD::AVX2:
            findMinMaxWithsentinelValue32_AVX2(count, (uint32_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE4_1:
            findMinMaxWithsentinelValue32_SSE<SIMD::SSE4_1>(count, (uint32_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE3:
            findMinMaxWithsentinelValue32_SSE<SIMD::SSE3>(count, (uint32_t*) data, minOut, maxOut, sentinelValue);
            break;
          case SIMD::SSE2:
            findMinMaxWithsentinelValue32_SSE<SIMD::SSE2>(count, (uint32_t*) data, minOut, maxOut, sentinelValue);
            break;
          default:
            throw;
          }
        } else {
          findMinMaxWithsentinelValue32_slow(count, (uint32_t*) data, minOut, maxOut, sentinelValue);
        }
      } else {
        if (useSSE) {
          switch (g_simdSupportLevel) {
          case SIMD::AVX512:
          case SIMD::AVX2:
            findMinMax32_AVX2(count, (uint32_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE4_1:
            findMinMax32_SSE<SIMD::SSE4_1>(count, (uint32_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE3:
            findMinMax32_SSE<SIMD::SSE3>(count, (uint32_t*) data, minOut, maxOut);
            break;
          case SIMD::SSE2:
            findMinMax32_SSE<SIMD::SSE2>(count, (uint32_t*) data, minOut, maxOut);
            break;
          default:
            throw;
          }
        } else {
          findMinMax32_slow(count, (uint32_t*) data, minOut, maxOut);
        }
      }
    } else {
      throw; // not a supported type
    }
  }


  template<typename T>
  __forceinline void copySubtract_slow(T* dstData, const T* srcData, const uint32_t count, const T value, const bool ignoreSentinel, const T sentinelValue) {
    if (ignoreSentinel) {
      for (uint32_t i = 0; i < count; i++) {
        dstData[i] = srcData[i] - ((srcData[i] == sentinelValue) ? 0 : value);
      }
    } else {
      for (uint32_t i = 0; i < count; i++) {
        dstData[i] = srcData[i] - value;
      }
    }
  }

  __forceinline void copySubtract16_SSE(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m128i subtract = _mm_set1_epi16(value);

    if (ignoreSentinel) {
      __m128i sentinel = _mm_set1_epi16(sentinelValue);

      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m128i src = _mm_loadu_si128((__m128i*) & srcData[i]);

        // Create a mask based on the comparison of src with sentinel
        __m128i mask = _mm_cmpeq_epi16(src, sentinel);
        mask = _mm_andnot_si128(mask, subtract);

        // Perform the subtraction using the mask
        __m128i dst = _mm_sub_epi16(src, mask);
        _mm_storeu_si128((__m128i*)(dstData + i), dst);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += 8) {
        __m128i src = _mm_loadu_si128((__m128i*) & srcData[i]);
        __m128i dst = _mm_sub_epi16(src, subtract);  // dst[i] = src[i] - value
        _mm_storeu_si128((__m128i*) & dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }

  __forceinline void copySubtract16_AVX2(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue) {
    const uint32_t numLanes = 16;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m256i subtract = _mm256_set1_epi16(value);

    if (ignoreSentinel) {
      __m256i sentinel = _mm256_set1_epi16(sentinelValue);
      __m256i zero = _mm256_setzero_si256();

      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m256i src = _mm256_loadu_si256((__m256i*) &srcData[i]);
        __m256i mask = _mm256_cmpeq_epi16(src, sentinel);
        mask = _mm256_andnot_si256(mask, subtract);
        __m256i dst = _mm256_sub_epi16(src, mask);
        _mm256_storeu_si256((__m256i*) &dstData[i], dst);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m256i src = _mm256_loadu_si256((__m256i*) &srcData[i]);
        __m256i dst = _mm256_sub_epi16(src, subtract);
        _mm256_storeu_si256((__m256i*) &dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }
  
  __forceinline void copySubtract32_AVX2(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue) {
    const uint32_t numLanes = 8;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m256i subtract = _mm256_set1_epi32(value);

    if (ignoreSentinel) {
      __m256i sentinel = _mm256_set1_epi32(sentinelValue);
      __m256i zero = _mm256_setzero_si256();

      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m256i src = _mm256_loadu_si256((__m256i*) &srcData[i]);
        __m256i mask = _mm256_cmpeq_epi32(src, sentinel);
        mask = _mm256_andnot_si256(mask, subtract);
        __m256i dst = _mm256_sub_epi32(src, mask);
        _mm256_storeu_si256((__m256i*) &dstData[i], dst);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m256i src = _mm256_loadu_si256((__m256i*) &srcData[i]);
        __m256i dst = _mm256_sub_epi32(src, subtract);
        _mm256_storeu_si256((__m256i*) &dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }

  __forceinline void copySubtract16_AVX512(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue) {
    const uint32_t numLanes = 32;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m512i subtract = _mm512_set1_epi16(value);

    if (ignoreSentinel) {
      __m512i sentinel = _mm512_set1_epi16(sentinelValue);
      __m512i zero = _mm512_setzero_si512();

      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m512i src = _mm512_loadu_si512((__m512i*)&srcData[i]);
        __mmask32 cmp = _mm512_cmpneq_epu16_mask(src, sentinel);
        __m512i masked = _mm512_mask_sub_epi16(zero, cmp, src, subtract);
        _mm512_storeu_si512((__m512i*)&dstData[i], masked);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m512i src = _mm512_loadu_si512((__m512i*) &srcData[i]);
        __m512i dst = _mm512_sub_epi16(src, subtract);
        _mm512_storeu_si512((__m512i*)& dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }
  __forceinline void copySubtract32_AVX512(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue) {
    const uint32_t numLanes = 16;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m512i subtract = _mm512_set1_epi32(value);

    if (ignoreSentinel) {
      __m512i sentinel = _mm512_set1_epi32(sentinelValue);
      __m512i zero = _mm512_setzero_si512();

      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m512i src = _mm512_loadu_si512((__m512i*)&srcData[i]);
        __mmask16 cmp = _mm512_cmpneq_epu32_mask(src, sentinel);
        __m512i masked = _mm512_mask_sub_epi32(zero, cmp, src, subtract);
        _mm512_storeu_si512((__m512i*)&dstData[i], masked);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m512i src = _mm512_loadu_si512((__m512i*) &srcData[i]);
        __m512i dst = _mm512_sub_epi32(src, subtract);
        _mm512_storeu_si512((__m512i*)& dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }

  __forceinline void copySubtract32_SSE(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue) {
    const uint32_t numLanes = 4;
    const uint32_t alignedCount = dxvk::alignDown(count, numLanes);

    __m128i subtract = _mm_set1_epi32(value);
    __m128i sentinel = _mm_set1_epi32(sentinelValue);
    __m128i zero = _mm_setzero_si128();

    if (ignoreSentinel) {
      for (uint32_t i = 0; i < alignedCount; i += numLanes) {
        __m128i src = _mm_loadu_si128((__m128i*) & srcData[i]);

        // Create a mask based on the comparison of src with sentinel
        __m128i mask = _mm_cmpeq_epi32(src, sentinel);
        mask = _mm_andnot_si128(mask, subtract);

        // Perform the subtraction using the mask
        __m128i dst = _mm_sub_epi32(src, mask);
        _mm_storeu_si128((__m128i*)(dstData + i), dst);
      }
    } else {
      for (uint32_t i = 0; i < alignedCount; i += 4) {
        __m128i src = _mm_loadu_si128((__m128i*) & srcData[i]);
        __m128i dst = _mm_sub_epi32(src, subtract);  // dst[i] = src[i] - value
        _mm_storeu_si128((__m128i*) & dstData[i], dst);
      }
    }

    // Process remaining elements
    for (uint32_t i = alignedCount; i < count; ++i) {
      dstData[i] = srcData[i] - ((ignoreSentinel && srcData[i] == sentinelValue) ? 0 : value);
    }
  }

  template<typename T>
  void copySubtract(T* dstData, const T* srcData, const uint32_t count, const T value, const  bool ignoreSentinel/* = false*/, const T sentinelValue/* = 0*/) {
    const bool useSSE = SSE_ENABLE && count >= 32;

    if (useSSE) {
      if (std::is_same<T, uint16_t>::value) {
        switch (g_simdSupportLevel) {
        case SIMD::AVX512:
          copySubtract16_AVX512((uint16_t*) dstData, (uint16_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        case SIMD::AVX2:
          copySubtract16_AVX2((uint16_t*) dstData, (uint16_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        case SIMD::SSE4_1:
        case SIMD::SSE3:
        case SIMD::SSE2:
          copySubtract16_SSE((uint16_t*) dstData, (uint16_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        default:
          throw;
        }
      } else if (std::is_same<T, uint32_t>::value) {
        switch (g_simdSupportLevel) {
        case SIMD::AVX512:
          copySubtract32_AVX512((uint32_t*) dstData, (uint32_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        case SIMD::AVX2:
          copySubtract32_AVX2((uint32_t*) dstData, (uint32_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        case SIMD::SSE4_1:
        case SIMD::SSE3:
        case SIMD::SSE2:
          copySubtract32_SSE((uint32_t*) dstData, (uint32_t*) srcData, count, value, ignoreSentinel, sentinelValue);
          break;
        default:
          throw;
        }
      } else {
        throw; // not a supported type
      }
    } else {
      copySubtract_slow<T>(dstData, srcData, count, value, ignoreSentinel, sentinelValue);
    }
  }

  template void findMinMax<uint16_t>(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const bool sentinelIgnore, const uint16_t sentinelValue);
  template void findMinMax<uint32_t>(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const bool sentinelIgnore, const uint32_t sentinelValue);

  template void copySubtract<uint16_t>(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue);
  template void copySubtract<uint32_t>(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue);

  void parallel_memcpy(void* dst, const void* src, const size_t count, const size_t chunkSize) {
    const uint8_t* srcBytes = static_cast<const uint8_t*>(src);
    uint8_t* dstBytes = static_cast<uint8_t*>(dst);

    size_t numChunks = count / chunkSize;

    // It's only worth the effort if theres at least 3 threads saturated
    if (numChunks > 3) {
      concurrency::parallel_for<size_t>(0, numChunks, [&](size_t i) {
        size_t offset = i * chunkSize;
        std::memcpy(dstBytes + offset, srcBytes + offset, chunkSize);
      });

      // Copy any remaining bytes
      std::memcpy(dstBytes + numChunks * chunkSize, srcBytes + numChunks * chunkSize, count % chunkSize);
    } else {
      std::memcpy(dst, src, count);
    }
  }


  template<typename T>
  __forceinline T findNthBit_BMI2(const T num, const T n) {
    return _tzcnt_u32(_pdep_u32(1 << n, num));
  }

  template<typename T>
  __forceinline T findNthBit_slow(const T num, const T n) {
    T count = 0;
    for (uint32_t i = 0; i < sizeof(T) * 8; i++) {
      if (num & (1 << i)) {
        if (count++ == n) {
          return i;
        }
      }
    }
    return sizeof(T) * 8; // out of range, same as what the BMI method returns
  }

  template<typename T>
  T findNthBit(const T num, const T n) {
    if (g_supportsBMI2)
      return findNthBit_BMI2(num, n);

    return findNthBit_slow(num, n);
  }

  template uint8_t findNthBit(const uint8_t num, const uint8_t n);
  template uint16_t findNthBit(const uint16_t num, const uint16_t n);
  template uint32_t findNthBit(const uint32_t num, const uint32_t n);
}