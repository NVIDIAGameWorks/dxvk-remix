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

using namespace dxvk;
using namespace std;
using namespace chrono;

class ThreadPoolTestApp {
public:
  static void run() { 
    cout << "Begin test" << endl;
    test_smoke();
    cout << "WorkerThreadPool successfully smoke tested" << endl;
  }
  
private:
  static void test_smoke() {
    ZoneScoped;
    // We'll need to size the pool according to the number of tasks
    //  here since its a ring buffer and this is a single thread...
    const uint32_t numThreads = 8;
    const uint32_t numTasks = 2000;

    WorkerThreadPool<numTasks> threadPool(numThreads);
    cout << "Created thread pool with " << numThreads << " threads" << endl;

    vector<shared_future<uint32_t>> results;
    {
      Timer t;
      results.resize(numTasks);
      for (uint32_t i = 0; i < numTasks; i++) {
        // Spawn N tasks that sleep a bit, and return a 1
        auto future = threadPool.Schedule([]() -> uint32_t {
            ZoneScoped;
            random_device rd;
            mt19937 rng(rd());
            uniform_int_distribution<DWORD> uni(0, 100);
            // Create some varying workloads 
            double sleepFor = (double) uni(rng);
            auto start = high_resolution_clock::now();
            while (duration_cast<milliseconds>(high_resolution_clock::now() - start).count() < sleepFor);

            return 1;
          });

        if (!future.valid())
          throw DxvkError("Failed to schedule task");

        results[i] = future;
      }
      cout << "Scheduled " << numTasks << " tasks" << endl;
    }

    // Count all the return values (1's) and make sure everyone made it home
    uint32_t resultCount = 0;
    for (shared_future<uint32_t> result : results) {
      resultCount += result.get();
    }
    FrameMark;

    // Check for ABA problem by making sure the correct count returned
    cout << "Counted the result, expected:" << numTasks << ", got:" << resultCount << endl;

    if (resultCount != numTasks)
      throw DxvkError("Results didnt match");
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

