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
      {                                                                                                                               \
        {                                                                                                                             \
          std::cout << "Running: copySubtract"#bitwidth"_"#ISA" --> ";                                                                \
          Timer time;                                                                                                                 \
          fast::copySubtract##bitwidth##_##ISA##((uint##bitwidth##_t*) dstData2, (uint##bitwidth##_t*) srcData, count, value, ignoreSentinel, sentinelValue); \
        }                                                                                                                             \
        if (memcmp(dstData2, dstData, sizeof(T) * count) != 0)                                                                        \
          throw dxvk::DxvkError("Output not matching copySubtract"#bitwidth"_"#ISA);                                                  \
      }

#define TEST_CHECK(ISA, bitwidth) \
      if (fast::getSimdSupportLevel() >= SIMD::ISA) {                     \
        TEST(ISA, bitwidth);                                              \
      } else {                                                            \
        std::cout << #ISA" not supported by this processor" << std::endl; \
      }                                                                   \


namespace fast {
  template<typename T>
  extern void copySubtract_slow(T* dstData, const T* srcData, const uint32_t count, const T value, const bool ignoreSentinel, const T sentinelValue);

  extern void copySubtract16_SSE(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue);
  extern void copySubtract16_AVX2(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue);
  extern void copySubtract16_AVX512(uint16_t* dstData, const uint16_t* srcData, const uint32_t count, const uint16_t value, const bool ignoreSentinel, const uint16_t sentinelValue);
  extern void copySubtract32_SSE(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue);
  extern void copySubtract32_AVX2(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue);
  extern void copySubtract32_AVX512(uint32_t* dstData, const uint32_t* srcData, const uint32_t count, const uint32_t value, const bool ignoreSentinel, const uint32_t sentinelValue);

class CopySubtractTestApp {
public:
  static void run() { 
    std::cout << "Begin test (16-bit)" << std::endl;
    test_smoke<uint16_t>();
    test_correctness<uint16_t>();

    std::cout << "Begin test (32-bit)" << std::endl;
    test_smoke<uint32_t>();
    test_correctness<uint32_t>();
  }
  
private:
  template<typename T>
  static void test_smoke() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<T> uni(1, std::numeric_limits<T>::max());

    const uint32_t count = 64 * 1024 * 7 + 3;

    T* pData = new T[count];
    for (uint32_t i = 0; i < count; i++) {
      pData[i] = uni(rng);
    }

    T* pDst = new T[count];
    memset(pDst, 0, sizeof(T) * count);

    std::cout << "Running smoke check, number of indices: " << count << std::endl;

    std::cout << std::endl << "Testing regular" << std::endl;
    executeWithSentinel<T>(pDst, pData, count, 1, false, 0);
    std::cout << std::endl << "Testing with sentinel ignore" << std::endl;
    executeWithSentinel<T>(pDst, pData, count, 1, true, 0);

    delete[] pDst;
    delete[] pData;

    std::cout << "CopySubtract fast ops successfully smoke tested" << std::endl;
  }

  template<typename T>
  static void test_correctness() {
    const size_t kCount = 100;

    T dataSrc[kCount];
    for (uint32_t i = 0; i < kCount; i++) {
      dataSrc[i] = std::numeric_limits<T>::max();
    }

    T dataOut[kCount];
    memset(&dataOut[0], 0, sizeof(dataSrc));

    // Sub 1 from array
    fast::copySubtract<T>(dataOut, dataSrc, kCount, 0x1);

    for (uint32_t i = 0; i < kCount; i++) {
      if (dataOut[i] != std::numeric_limits<T>::max()-1)
        throw dxvk::DxvkError("Output not matching expected");
    }

    std::cout << "CopySubtract fast ops successfully tested for correctness" << std::endl;
  }

  template<typename T>
  static void executeWithSentinel(T* dstData, const T* srcData, const uint32_t count, const T value, const bool ignoreSentinel, const T sentinelValue) {
    // Now test regular CPU logic
    {
      std::cout << "Running: copySubtract_slow --> ";
      Timer time;
      fast::copySubtract_slow<T>(dstData, srcData, count, value, ignoreSentinel, sentinelValue);
    }

    T* dstData2 = new T[count];
    memset(dstData2, 0, sizeof(T) * count);
    if (std::is_same<T, uint16_t>::value) {
      TEST(SSE, 16);
      TEST_CHECK(AVX2, 16);
      TEST_CHECK(AVX512, 16);
    } else if (std::is_same<T, uint32_t>::value) {
      TEST(SSE, 32);
      TEST_CHECK(AVX2, 32);
      TEST_CHECK(AVX512, 32);
    } else {
      throw dxvk::DxvkError("Invalid test");
    }

    delete[] dstData2;
  }
};
}

int main() {
  try {
    fast::CopySubtractTestApp::run();
  }
  catch (const dxvk::DxvkError& e) {
    std::cerr << e.message() << std::endl;
    return -1;
  }

  return 0;
}

