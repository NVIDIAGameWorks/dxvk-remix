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

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#include "util_sharedheap_allocator.h"

namespace {
  constexpr uint32_t kNoAllocation = std::numeric_limits<uint32_t>::max();

  struct Segment {
    uint32_t first;
    size_t size;
    uint32_t getBaseChunkId() const { return first; }
    size_t getNumChunks() const { return size; }
  };

  void check(bool condition, const char* pMessage) {
    if (!condition) {
      throw std::runtime_error(pMessage);
    }
  }

  void testPlacement() {
    const std::vector<Segment> none;
    const std::vector<Segment> single = { { 0, 8 } };
    const std::vector<Segment> multiple = { { 0, 8 }, { 8, 8 } };
    const std::map<uint32_t, uint32_t> empty;
    using bridge_util::findSharedHeapAllocation;

    check(findSharedHeapAllocation(none, empty, 1) == kNoAllocation, "An empty heap accepted an allocation.");
    check(findSharedHeapAllocation(single, empty, 0) == kNoAllocation, "A zero-sized request was accepted.");
    check(findSharedHeapAllocation(single, empty, 8) == 0, "An allocation that exactly fills a segment was rejected.");
    check(findSharedHeapAllocation(multiple, empty, 9) == kNoAllocation, "An allocation crossed a segment boundary.");
    check(findSharedHeapAllocation(single, empty, std::numeric_limits<size_t>::max()) == kNoAllocation,
          "An oversized request wrapped into the heap.");

    const std::map<uint32_t, uint32_t> leading = { { 2, 3 }, { 6, 7 } };
    check(findSharedHeapAllocation(single, leading, 2) == 0, "The leading free range was not used.");
    const std::map<uint32_t, uint32_t> internal = { { 0, 1 }, { 4, 5 } };
    check(findSharedHeapAllocation(single, internal, 2) == 2, "An internal free range was not used.");
    const std::map<uint32_t, uint32_t> trailing = { { 0, 4 } };
    check(findSharedHeapAllocation(single, trailing, 3) == 5, "The trailing free range was not used.");
    check(findSharedHeapAllocation(single, trailing, 4) == kNoAllocation, "A request exceeded the trailing free range.");
    const std::map<uint32_t, uint32_t> full = { { 0, 7 }, { 8, 15 } };
    check(findSharedHeapAllocation(multiple, full, 1) == kNoAllocation, "The allocator reused an owned chunk.");

    const std::vector<Segment> offset = { { 10, 8 }, { 30, 4 } };
    check(findSharedHeapAllocation(offset, empty, 8) == 10, "The segment base was ignored.");
    const std::map<uint32_t, uint32_t> firstFull = { { 10, 17 } };
    check(findSharedHeapAllocation(offset, firstFull, 4) == 30, "A later empty segment was not used.");
  }

  void testSegmentBoundaries() {
    const std::vector<Segment> segments = { { 0, 8 }, { 8, 8 } };
    using bridge_util::findSharedHeapAllocation;

    const std::map<uint32_t, uint32_t> gapAcrossBoundary = { { 0, 5 }, { 12, 15 } };
    check(findSharedHeapAllocation(segments, gapAcrossBoundary, 4) == 8,
          "A fitting range after a boundary inside a free gap was missed.");

    const std::map<uint32_t, uint32_t> invalidFirstGap = { { 0, 5 }, { 10, 11 } };
    check(findSharedHeapAllocation(segments, invalidFirstGap, 4) == 12,
          "A boundary-crossing first gap hid a later valid range.");

    const std::map<uint32_t, uint32_t> trailingAcrossBoundary = { { 0, 5 } };
    check(findSharedHeapAllocation(segments, trailingAcrossBoundary, 8) == 8,
          "The allocator did not skip an unusable segment tail.");

    const std::map<uint32_t, uint32_t> narrowRanges = { { 0, 2 }, { 6, 9 }, { 13, 15 } };
    check(findSharedHeapAllocation(segments, narrowRanges, 4) == kNoAllocation,
          "Separate narrow ranges were combined into one allocation.");
  }

  void testPendingSnapshots() {
    const std::vector<Segment> segments = { { 0, 8 }, { 8, 8 } };
    std::map<uint32_t, uint32_t> allocations = { { 0, 3 }, { 4, 7 }, { 8, 11 }, { 12, 15 } };
    std::array<uint32_t, 16> memory = {};
    std::fill(memory.begin() + 4, memory.begin() + 8, 101);
    std::fill(memory.begin() + 8, memory.begin() + 12, 202);
    check(bridge_util::findSharedHeapAllocation(segments, allocations, 4) == kNoAllocation,
          "A full bounded heap reused a snapshot before consumption.");

    check(std::all_of(memory.begin() + 4, memory.begin() + 8, [](uint32_t value) { return value == 101; }),
          "The first queued snapshot changed before consumption.");
    allocations.erase(4);
    const auto reused = bridge_util::findSharedHeapAllocation(segments, allocations, 4);
    check(reused == 4, "A consumed snapshot's range was not reclaimed.");
    allocations[reused] = reused + 3;
    std::fill(memory.begin() + reused, memory.begin() + reused + 4, 303);
    check(std::all_of(memory.begin() + 8, memory.begin() + 12, [](uint32_t value) { return value == 202; }),
          "Reusing a consumed snapshot overwrote another pending snapshot.");
    check(bridge_util::findSharedHeapAllocation(segments, allocations, 1) == kNoAllocation,
          "Reclaimed storage remained available after it was allocated again.");
  }

  void testBoundedChurn() {
    constexpr uint32_t kChunks = 48;
    constexpr uint32_t kCanonicalValue = 0xffffeeee;
    const std::vector<Segment> segments = { { 0, 16 }, { 16, 16 }, { 32, 16 } };
    const std::array<uint32_t, 6> canonical = { 0, 15, 16, 31, 32, 47 };
    std::map<uint32_t, uint32_t> allocations;
    std::array<uint32_t, kChunks> memory = {};
    for (const auto chunk : canonical) {
      allocations[chunk] = chunk;
      memory[chunk] = kCanonicalValue;
    }

    struct Snapshot {
      uint32_t first;
      uint32_t size;
      uint32_t value;
    };
    std::deque<Snapshot> pending;
    const auto verifyPending = [&]() {
      for (const auto& snapshot : pending) {
        check(allocations.at(snapshot.first) == snapshot.first + snapshot.size - 1,
              "A pending snapshot lost its owned range.");
        for (uint32_t chunk = snapshot.first; chunk < snapshot.first + snapshot.size; ++chunk) {
          check(memory[chunk] == snapshot.value, "Snapshot churn corrupted pending upload data.");
        }
      }
      for (const auto chunk : canonical) {
        check(memory[chunk] == kCanonicalValue, "Snapshot churn overwrote canonical storage.");
      }
    };
    size_t retired = 0;
    const auto consume = [&]() {
      check(!pending.empty(), "The bounded heap cannot make progress by retiring a snapshot.");
      verifyPending();
      const auto snapshot = pending.front();
      pending.pop_front();
      allocations.erase(snapshot.first);
      ++retired;
    };

    const uint32_t sizes[] = { 3, 5, 1, 7, 2, 4, 6, 8 };
    for (uint32_t iteration = 0; iteration < 1024; ++iteration) {
      const uint32_t size = sizes[iteration % std::size(sizes)];
      auto first = bridge_util::findSharedHeapAllocation(segments, allocations, size);
      while (first == kNoAllocation) {
        consume();
        first = bridge_util::findSharedHeapAllocation(segments, allocations, size);
      }
      check(first < kChunks && size <= kChunks - first, "A churn allocation escaped the bounded heap.");
      const uint32_t final = first + size - 1;
      check(std::any_of(segments.begin(), segments.end(), [&](const Segment& segment) {
        return first >= segment.first && final < segment.first + segment.size;
      }), "A churn allocation crossed a segment boundary.");
      for (const auto& [ownedFirst, ownedFinal] : allocations) {
        check(final < ownedFirst || first > ownedFinal, "A churn allocation overlapped a live range.");
      }
      allocations[first] = final;
      const uint32_t value = iteration + 1;
      std::fill(memory.begin() + first, memory.begin() + first + size, value);
      pending.push_back({ first, size, value });
      verifyPending();
    }
    while (!pending.empty()) {
      consume();
    }
    check(retired == 1024, "The churn test did not consume every queued snapshot.");
    check(allocations.size() == canonical.size(), "Consumed snapshots retained owned ranges.");
  }
}

int main() {
  try {
    testPlacement();
    testSegmentBoundaries();
    testPendingSnapshots();
    testBoundedChurn();
  } catch (const std::exception& error) {
    std::cerr << "Shared heap allocator test failed: " << error.what() << std::endl;
    return 1;
  }
  std::cout << "Shared heap allocator tests passed." << std::endl;
  return 0;
}
