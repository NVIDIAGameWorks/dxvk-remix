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
#include <cstring>
#include <random>
#include "../../test_utils.h"
#include "../../../src/util/util_fastops.h"
#include "../../../src/util/util_timer.h"

using namespace dxvk;

#define TEST(ISA, bitwidth) \
      {                                                                    \
        {                                                                  \
          std::cout << "Running: findMinMax"#bitwidth"_"#ISA" --> ";       \
          Timer time;                                                      \
          fast::findMinMax##bitwidth##_##ISA##(count, (uint##bitwidth##_t*) pData, min2, max2);    \
        }                                                                  \
        if (min2 != min || max2 != max)                                    \
          throw dxvk::DxvkError("Min/Max not matching findMinMax"#bitwidth"_"#ISA);  \
      }                                                                    \

#define TEST_CHECK(ISA, bitwidth) \
      if (fast::getSimdSupportLevel() >= SIMD::ISA) {                     \
        TEST(ISA, bitwidth);                                              \
      } else {                                                            \
        std::cout << #ISA" not supported by this processor" << std::endl; \
      }                                                                   \


#define TEST_SENTINEL(ISA, bitwidth) \
      {                                                                    \
        {                                                                  \
          std::cout << "Running: findMinMaxWithsentinelValue"#bitwidth"_"#ISA" --> ";       \
          Timer time;                                                      \
          fast::findMinMaxWithsentinelValue##bitwidth##_##ISA##(count, (uint##bitwidth##_t*) pData, min2, max2, sentinel);    \
        }                                                                  \
        if (min2 != min || max2 != max)                                    \
          throw dxvk::DxvkError("Min/Max not matching findMinMaxWithsentinelValue"#bitwidth"_"#ISA);  \
      }                                                                    \

#define TEST_SENTINEL_CHECK(ISA, bitwidth) \
      if (fast::getSimdSupportLevel() >= SIMD::ISA) {                     \
        TEST_SENTINEL(ISA, bitwidth);                                              \
      } else {                                                            \
        std::cout << #ISA" not supported by this processor" << std::endl; \
      }                                                                   \

namespace fast {

  extern void findMinMax16_slow(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut);
  template<SIMD V>
  extern void findMinMax16_SSE(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut);

  extern void findMinMax16_AVX2(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut);

  extern void findMinMax32_slow(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut);
  template<SIMD V>
  extern void findMinMax32_SSE(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut);

  extern void findMinMax32_AVX2(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut);

  extern void findMinMaxWithsentinelValue16_slow(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue);
  template<SIMD V>
  extern void findMinMaxWithsentinelValue16_SSE(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue);
  extern void findMinMaxWithsentinelValue16_AVX2(const uint32_t count, const uint16_t* data, uint32_t& minOut, uint32_t& maxOut, const uint16_t sentinelValue);

  extern void findMinMaxWithsentinelValue32_slow(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue);
  template<SIMD V>
  extern void findMinMaxWithsentinelValue32_SSE(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue);
  extern void findMinMaxWithsentinelValue32_AVX2(const uint32_t count, const uint32_t* data, uint32_t& minOut, uint32_t& maxOut, const uint32_t sentinelValue);

class MinMaxTestApp {
public:
  static void run() { 
    std::cout << std::endl << "Begin test (16-bit)" << std::endl;
    test_smoke<uint16_t>();
    test_correctness<uint16_t>();

    std::cout << std::endl << "Begin test (32-bit)" << std::endl;
    test_smoke<uint32_t>();
    test_correctness<uint32_t>();
  }
  
private:
  template<typename T>
  static void test_smoke() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<T> uni(0, std::numeric_limits<T>::max() / 2);

    const uint32_t count =  64 * 1024 * 7 + 3;

    T* pData = new T[count];
    for (uint32_t i = 0; i < count; i++) {
      pData[i] = uni(rng);
    }

    std::cout << "Running smoke check, number of indices: " << count << std::endl;
    execute(count, pData);
    executeWithSentinel(count, pData);

    delete[] pData;

    std::cout << "Min/Max fast ops successfully smoke tested" << std::endl;
  }

  template<typename T>
  static void test_correctness() {
    T data1[] = { 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 1, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29 };
    uint32_t min, max;
    fast::findMinMax<T>(sizeof(data1) / sizeof(data1[0]), data1, min, max);

    if (1 != min || 29 != max)
      throw dxvk::DxvkError("Min/Max not matching correctness check 1");

    const T kTestSentinel = 0xFFFF;
    T data2[] = { 10, 2, 3, 3, 3, 3, 500, 7, 203, 209, 40005, 2, 3, 3, 3, 3, 500, 7, 203, 209, 40005, 2, 3, 3, 3, 3, 500, 7, 203, 209, 40005, kTestSentinel, 54, 7777, 1100, 130, 107, 109, 109, 109, 109, 109, 109 };
    fast::findMinMax<T>(sizeof(data2) / sizeof(data2[0]), data2, min, max, true, kTestSentinel);

    if (2 != min || 40005 != max)
      throw dxvk::DxvkError("Min/Max not matching correctness check 2");

    std::cout << "Min/Max fast ops successfully tested for correctness" << std::endl;
  }

  template<typename T>
  static void execute(const uint32_t count, T* pData) {
    uint32_t min, max;
    uint32_t min2, max2;
    if (std::is_same<T, uint16_t>::value) {
      // Now test regular CPU logic
      {
        std::cout << "Running: findMinMax16_slow --> ";
        Timer time;
        fast::findMinMax16_slow(count, (uint16_t*) pData, min, max);
      }

      TEST(SSE<SIMD::SSE2>, 16);
      TEST(SSE<SIMD::SSE4_1>, 16);
      TEST_CHECK(AVX2, 16);
    } else if (std::is_same<T, uint32_t>::value) {
      // Now test regular CPU logic
      {
        std::cout << "Running: findMinMax32_slow --> ";
        Timer time;
        fast::findMinMax32_slow(count, (uint32_t*) pData, min, max);
      }

      TEST(SSE<SIMD::SSE2>, 32);
      TEST(SSE<SIMD::SSE4_1>, 32);
      TEST_CHECK(AVX2, 32);
    } else {
      throw dxvk::DxvkError("Invalid test");
    }
  }

  template<typename T>
  static void executeWithSentinel(const uint32_t count, T* pData) {
    const T sentinel = 1;
    uint32_t min, max;
    uint32_t min2, max2;
    if (std::is_same<T, uint16_t>::value) {
      // Now test regular CPU logic
      {
        std::cout << "Running: findMinMaxWithsentinelValue16_slow --> ";
        Timer time;
        fast::findMinMaxWithsentinelValue16_slow(count, (uint16_t*) pData, min, max, sentinel);
      }

      TEST_SENTINEL(SSE<SIMD::SSE2>, 16);
      TEST_SENTINEL(SSE<SIMD::SSE4_1>, 16);
      TEST_SENTINEL_CHECK(AVX2, 16);
    } else if (std::is_same<T, uint32_t>::value) {
      // Now test regular CPU logic
      {
        std::cout << "Running: findMinMaxWithsentinelValue32_slow --> ";
        Timer time;
        fast::findMinMaxWithsentinelValue32_slow(count, (uint32_t*) pData, min, max, sentinel);
      }

      TEST_SENTINEL(SSE<SIMD::SSE2>, 32);
      TEST_SENTINEL(SSE<SIMD::SSE4_1>, 32);
      TEST_SENTINEL_CHECK(AVX2, 32);
    } else {
      throw dxvk::DxvkError("Invalid test");
    }
  }
};
}

int main() {
  try {
    fast::MinMaxTestApp::run();
  }
  catch (const dxvk::DxvkError& e) {
    std::cerr << e.message() << std::endl;
    return -1;
  }

  return 0;
}
