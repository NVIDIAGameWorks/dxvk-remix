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
#include <chrono>
#include <iostream>

#include "../../test_utils.h"
#include "../../../src/util/util_threadpool.h"
#include "../../../src/util/util_timer.h"
#include "../../../src/tracy/Tracy.hpp"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_util_threadpool.log");
}

using namespace dxvk;
using namespace std;
using namespace chrono;

class ThreadPoolTestApp {
public:
  static void run() { 
    cout << "Begin smoke test" << endl;
    test_smoke<0>();
    cout << "Begin task cancellation test" << endl;
    test_smoke<4>();
    cout << "Begin misc tests" << endl;
    test_misc();
    cout << "WorkerThreadPool successfully smoke tested" << endl;
  }
  
private:
  template<uint32_t cancelPeriod>
  static void test_smoke() {
    ZoneScoped;
    // We'll need to size the pool according to the number of tasks
    //  here since its a ring buffer and this is a single thread...
    const uint32_t numThreads = 8;
    const uint32_t numTasks = 2000;

    WorkerThreadPool<numTasks> threadPool(numThreads);
    cout << "Created thread pool with " << numThreads << " threads" << endl;

    // Create params for lambda ahead of schedule
    random_device rd;
    mt19937 rng(rd());
    uniform_int_distribution<uint32_t> uni(0, 100);
    uint32_t a[numTasks], b[numTasks], c[numTasks], d[numTasks];

    for (uint32_t i = 0; i < numTasks; i++) {
      a[i] = uni(rng);
      b[i] = uni(rng);
      c[i] = uni(rng);
      d[i] = uni(rng);
    }

    vector<Future<uint32_t>> results(numTasks);
    {
      Timer t;
      const uint64_t s = __rdtsc();

      for (uint32_t i = 0; i < numTasks; i++) {
        // Spawn N tasks that sleep a bit, and return a 1
        auto future = threadPool.Schedule([a = a[i], b = b[i], c = c[i], d = d[i]]()->uint32_t {
          ZoneScoped;
          // Create some varying workloads 
          double sleepFor = (double) (a + b + c + d) / 4;
          auto start = high_resolution_clock::now();
          while (duration_cast<milliseconds>(high_resolution_clock::now() - start).count() < sleepFor);

          return 1;
        });

        if (!future.valid()) {
          throw DxvkError("Failed to schedule task");
        }

        results[i] = future;

        if (cancelPeriod > 0 && (i % cancelPeriod) == 0) {
          future.cancel();
        }
      }

      const uint64_t e = __rdtsc();

      cout << "Scheduled " << numTasks << " tasks in " << e - s << " clocks" << endl;
    }

    // Count all the return values (1's) and make sure everyone made it home
    uint32_t resultCount = 0;
    for (Future<uint32_t>& result : results) {
      if (result.valid()) {
        resultCount += result.get();
      }
    }

    const uint32_t expectedNumTasks = cancelPeriod > 0 ? (numTasks - numTasks / cancelPeriod) : numTasks;

    if (resultCount != expectedNumTasks) {
      throw DxvkError("Results didnt match");
    }

    // Check for ABA problem by making sure the correct count returned
    cout << "Counted the result, expected:" << expectedNumTasks << ", got:" << resultCount << endl;

    FrameMark;
  }

  static void test_misc() {
    const uint32_t numThreads = 4;
    const uint32_t numTasks = 32;

    auto* threadPool = new WorkerThreadPool<numTasks>(numThreads);
    cout << "Created thread pool with " << numThreads << " threads" << endl;

    uint32_t result = 0;
    auto future = threadPool->Schedule([&result]() {
      cout << "Hello from a void() future!" << endl;
      result += 1;
    });

    if (!future.valid()) {
      throw DxvkError("Failed to schedule task");
    }

    future.get();

    if (result != 1) {
      throw DxvkError("Result didnt match");
    }

    class DestuctorTester {
      // Using unique ptr to emulate a destructive move
      std::unique_ptr<uint32_t> param;
    public:
      DestuctorTester(DestuctorTester&& v) = default;
      DestuctorTester& operator =(DestuctorTester&&) = default;

      explicit DestuctorTester(uint32_t& param)
      : param { &param }
      { }

      ~DestuctorTester() {
        if (auto v = param.get()) {
          ++(*v);
          cout << "Hello from task destructor!" << endl;
          param.release();
        }
      }
    };

    DestuctorTester tester(result);
    future = threadPool->Schedule([tester = std::move(tester)]() {});

    if (!future.valid()) {
      throw DxvkError("Failed to schedule task");
    }

    // The lambda destructor is executed _after_ the future result is set.
    // We need to either wait for the result to update, or finalize the thread pool.
    delete threadPool;

    if (result != 2) {
      throw DxvkError("Result didnt match");
    }
  }
};

int main() {
  try {
    ThreadPoolTestApp::run();
  }
  catch (const dxvk::DxvkError& e) {
    cerr << e.message() << endl;
    return -1;
  }

  return 0;
}

