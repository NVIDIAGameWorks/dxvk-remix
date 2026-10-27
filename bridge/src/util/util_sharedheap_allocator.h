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
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace bridge_util {
  // A shared allocation must fit within a single mapped segment.
  template<typename Segments>
  uint32_t findSharedHeapAllocation(const Segments& segments,
                                    const std::map<uint32_t, uint32_t>& allocations,
                                    size_t numChunks) {
    constexpr uint32_t kInvalidChunk = std::numeric_limits<uint32_t>::max();
    if (numChunks == 0 || numChunks > kInvalidChunk) {
      return kInvalidChunk;
    }

    for (const auto& segment : segments) {
      uint64_t candidate = segment.getBaseChunkId();
      const uint64_t segmentEnd = std::min<uint64_t>(candidate + segment.getNumChunks(), kInvalidChunk);
      if (candidate >= segmentEnd || numChunks > segmentEnd - candidate) {
        continue;
      }

      auto next = allocations.lower_bound((uint32_t) candidate);
      if (next != allocations.begin()) {
        auto previous = next;
        --previous;
        candidate = std::max<uint64_t>(candidate, uint64_t(previous->second) + 1);
      }
      for (; next != allocations.end() && next->first < segmentEnd; ++next) {
        if (candidate < next->first && numChunks <= next->first - candidate) {
          return (uint32_t) candidate;
        }
        candidate = std::max<uint64_t>(candidate, uint64_t(next->second) + 1);
        if (candidate >= segmentEnd) {
          break;
        }
      }
      if (candidate < segmentEnd && numChunks <= segmentEnd - candidate) {
        return (uint32_t) candidate;
      }
    }
    return kInvalidChunk;
  }
}
