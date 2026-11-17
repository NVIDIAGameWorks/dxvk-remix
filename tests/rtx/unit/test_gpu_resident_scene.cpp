/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include <cstdint>
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_generation_array.h"
#include "../../../src/dxvk/rtx_render/rtx_upload_pages.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_gpu_resident_scene.log");
}

namespace {
  using MaterialHandle = struct MaterialHandle_T*;
  using Values = dxvk::GenerationArray<dxvk::MaterialId, int>;

  static_assert(!std::is_copy_constructible_v<Values>);
  static_assert(!std::is_move_constructible_v<Values>);

  void check(bool condition, const char* message) {
    if (!condition) {
      throw dxvk::DxvkError(message);
    }
  }

  void testSlotLifetime() {
    Values values(2);
    check(values.capacity() == 2, "capacity did not match the constructor argument");
    const dxvk::MaterialId first = values.allocId();
    check(first && values.lget(first) != nullptr && *values.lget(first) == 0,
          "allocated ID did not resolve to its default value");

    values.lset(first, 7);
    check(*values.lget(first) == 7 && values.freeId(first)
          && !values.freeId(first) && values.lget(first) == nullptr
          && values.lset(first, 9) == nullptr,
          "queued free remained publicly accessible");

    const dxvk::MaterialId pending = values.allocId();
    check(pending && pending.m_arrayIndex != first.m_arrayIndex
          && values.processFree(first) != nullptr
          && values.processFree(first) == nullptr
          && !values.allocId(),
          "GPU-pending slot was allocated prematurely");

    uint32_t allocatedCount = 0;
    bool foundLive = false;
    bool foundPending = false;
    values.forEachAllocated([&](dxvk::MaterialId liveId, int& value) {
      allocatedCount++;
      foundLive |= liveId == pending && value == 0;
      foundPending |= !liveId && value == 7;
    });
    check(allocatedCount == 2 && foundLive && foundPending,
          "allocated iteration did not preserve slot lifetimes");

    check(values.completeFree(first) == 7, "finished free did not preserve its value");

    const dxvk::MaterialId second = values.allocId();
    values.lset(second, 11);
    check(second.m_arrayIndex == first.m_arrayIndex
          && second.m_generation != first.m_generation
          && values.lget(first) == nullptr
          && values.lget(second) != nullptr
          && *values.lget(second) == 11,
          "reused slot did not advance its generation");

    check(values.liveIdAt(second.m_arrayIndex)
          && values.freeId(second)
          && !values.liveIdAt(second.m_arrayIndex)
          && values.lset(second, 3) == nullptr
          && values.processFree(second) != nullptr,
          "delayed set was not rejected after free");

    check(values.freeId(pending) && values.processFree(pending) != nullptr,
          "live slot could not be freed");
    values.completeFree(second);
    values.completeFree(pending);
    allocatedCount = 0;
    values.forEachAllocated([&](dxvk::MaterialId, int&) {
      allocatedCount++;
    });
    check(allocatedCount == 0 && values.empty(),
          "available slots remained visible to allocated iteration");

    const MaterialHandle packed = dxvk::packGenerationArrayId<MaterialHandle>(second);
    check(dxvk::unpackGenerationArrayId<dxvk::MaterialId>(packed) == second,
          "ID handle packing did not round trip");
    check(!dxvk::unpackGenerationArrayId<dxvk::MaterialId>(MaterialHandle {}),
          "null handle unpacked to a valid ID");
  }

  void testClearAdvancesGenerations() {
    Values values(1);
    const dxvk::MaterialId id = values.allocId();
    values.clear();
    check(values.empty() && values.lget(id) == nullptr,
          "clear kept a live slot");

    const dxvk::MaterialId reused = values.allocId();
    check(reused.m_arrayIndex == id.m_arrayIndex && reused.m_generation != id.m_generation,
          "clear did not advance the generation of a used slot");
  }

  void testUploadPagesCoalescing() {
    // Each page holds 4 slots, so the 10 slots span 3 pages and the last page is partially used.
    using Pages = dxvk::UploadPages<16, 64>;
    std::vector<std::pair<size_t, size_t>> regions;
    const auto collect = [&](size_t offset, size_t size) {
      regions.emplace_back(offset, size);
    };

    Pages pages(10);
    check(pages.empty(), "new upload pages were dirty");
    pages.markSlot(1);
    pages.markSlot(5);
    pages.markSlot(9);
    pages.forEachRegion(10, collect);
    check(regions.size() == 1 && regions[0] == std::make_pair(size_t(0), size_t(160)),
          "adjacent dirty pages were not coalesced, or the last page was not clipped");

    regions.clear();
    pages.clear();
    pages.markSlot(0);
    pages.markSlot(8);
    pages.forEachRegion(10, collect);
    check(regions.size() == 2
          && regions[0] == std::make_pair(size_t(0), size_t(64))
          && regions[1] == std::make_pair(size_t(128), size_t(32)),
          "separate dirty pages were merged");

    regions.clear();
    pages.forEachRegion(4, collect);
    check(regions.size() == 1 && regions[0] == std::make_pair(size_t(0), size_t(64)),
          "pages beyond the used slot count were uploaded");

    regions.clear();
    pages.clear();
    pages.forEachRegion(10, collect);
    check(pages.empty() && regions.empty(), "clear left dirty pages");

    // The TLAS instance buffers default-construct their pages and assign the capacity when the scene is created.
    Pages assigned;
    assigned = Pages(10);
    assigned.markSlot(9);
    assigned.forEachRegion(10, collect);
    check(regions.size() == 1 && regions[0] == std::make_pair(size_t(128), size_t(32)),
          "assigned pages did not cover their capacity");
  }
}

int main() {
  try {
    testSlotLifetime();
    testClearAdvancesGenerations();
    testUploadPagesCoalescing();
    std::cout << "All passed\n";
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return 1;
  }

  return 0;
}
