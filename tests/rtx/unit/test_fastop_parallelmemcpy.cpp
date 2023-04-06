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

namespace fast {

class ParallelMemcpyTestApp {
public:
  static void run() { 
    std::cout << "Begin test" << std::endl;
    test_smoke<uint32_t>();
  }
  
private:
  template<typename T>
  static void test_smoke() {
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<T> uni(1, std::numeric_limits<T>::max());

    const uint32_t count = 64 * 1024 * 1024 + 3;

    T* pData = new T[count];
    for (uint32_t i = 0; i < count; i++) {
      pData[i] = uni(rng);
    }

    T* pDst = new T[count];
    memset(pDst, 0, sizeof(T) * count);

    std::cout << "Running smoke check, number of bytes: " << count * sizeof(T) << std::endl;

    execute<T>(pDst, pData, count);

    delete[] pDst;
    delete[] pData;

    std::cout << "Parallel Memcpy fast ops successfully smoke tested" << std::endl;
  }


  template<typename T>
  static void execute(T* dstData, const T* srcData, const uint32_t count) {
    // Now test regular CPU logic
    {
      std::cout << "Running: memcpy --> ";
      Timer time;
      std::memcpy(dstData, srcData, sizeof(T) * count);
    }

    T* dstData2 = new T[count];
    memset(dstData2, 0, sizeof(T) * count);

    // Now test parallel memcpy
    {
      std::cout << "Running: parallel_memcpy --> ";
      Timer time;
      fast::parallel_memcpy(dstData2, srcData, sizeof(T) * count, count/8);
    }

    if (memcmp(dstData2, dstData, sizeof(T) * count) != 0)
      throw dxvk::DxvkError("Output not matching copySubtract16_SSE");


    delete[] dstData2;
  }
};
}

int main() {
  try {
    fast::ParallelMemcpyTestApp::run();
  }
  catch (const dxvk::DxvkError& e) {
    std::cerr << e.message() << std::endl;
    return -1;
  }

  return 0;
}

