/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
#include <set>
#include "../../test_utils.h"
#include "../../../src/util/util_spatial_map.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_spatial_map.log");
}

namespace dxvk {
  class TestApp {
  public:
    std::string ToString(const std::set<int>& input) {
      std::ostringstream result;
      std::copy(input.begin(), input.end(), std::ostream_iterator<int>(result, ", "));
      return result.str();
    }
    std::string ToString(const Vector3& pos) {
      return str::format("{", pos.x, ", ", pos.y, ", ", pos.z, "}");
    }

    void testPoint(const SpatialMap<int>& map, const Vector3& pos, const std::set<int>& expectedResult) {
      auto candidates = map.getDataNearPos(pos);
      std::set<int> candidatesSet;
      int count = 0;
      for (auto vec : candidates) {
        for (int value : *vec) {
          count++;
          candidatesSet.emplace(value);
        }
      }
      if (candidatesSet != expectedResult) {
        throw DxvkError(str::format("incorrect result: for pos ", ToString(pos), " expected [", ToString(expectedResult), "] but got [", ToString(candidatesSet), "]."));
      }
    }

    void run() {
      SpatialMap<int> map(2.0f);

      map.insert(Vector3(-1.f, -1.f, -1.f), -1);
      map.insert(Vector3(0.f, 0.f, 0.f), 0);
      map.insert(Vector3(1.f, 1.f, 1.f), 1);
      map.insert(Vector3(2.f, 2.f, 2.f), 2);
      map.insert(Vector3(3.f, 3.f, 3.f), 3);

      // corner of a cell
      testPoint(map, Vector3(0.f, 0.f, 0.f), {-1, 0, 1});
      // center of a cell
      testPoint(map, Vector3(1.f, 1.f, 1.f), { 0, 1, 2, 3});
      
      testPoint(map, Vector3(1.5f, 1.5f, 1.5f), { 0, 1, 2, 3});
      // near section of next cell
      testPoint(map, Vector3(2.5f, 2.5f, 2.5f), { 0, 1, 2, 3});
      // far section of next cell
      testPoint(map, Vector3(3.5f, 3.5f, 3.5f), { 2, 3});
      std::cout << "All passed\n";
    }
  };
}


int main() {
  try {
    dxvk::TestApp testApp;
    testApp.run();
  }
  catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    throw;
  }

  return 0;
}
