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
#include <cassert>
#include <cstdint>
#include <vector>

// Tracks the table slots that each of several copies of a table still has to receive.
// Every copy gets each changed slot once, however often the slot changes before that copy is next written.
// The copies share one list of changed slots, and each slot keeps one bit per copy that still needs it.
class PendingSlotQueues {
public:
  explicit PendingSlotQueues(uint32_t copyCount)
    : m_allCopies(static_cast<uint8_t>((1u << copyCount) - 1)) {
    assert(copyCount >= 1 && copyCount <= 8);
  }

  // Queues the slot for every copy.
  void mark(uint32_t slot) {
    if (m_pendingCopies.size() <= slot) {
      m_pendingCopies.resize(slot + 1);
    }
    if (!m_pendingCopies[slot]) {
      m_slots.push_back(slot);
    }
    m_pendingCopies[slot] = m_allCopies;
  }

  // Drops every queued slot.
  void clear() {
    m_slots.clear();
    m_sortedCount = 0;
    m_pendingCopies.clear();
  }

  // Calls run(firstSlot, count) for each run of consecutive slots queued for the copy, in ascending order,
  // and empties the copy's queue.
  template<typename RunFn>
  void takeRuns(uint32_t copy, RunFn&& run) {
    // Slots queued since the last call are appended unsorted after the sorted ones.
    if (m_sortedCount < m_slots.size()) {
      const auto sortedEnd = m_slots.begin() + m_sortedCount;
      std::sort(sortedEnd, m_slots.end());
      std::inplace_merge(m_slots.begin(), sortedEnd, m_slots.end());
    }

    // Each slot is handed out at most once per copy, and slots no copy needs any more are dropped from the list.
    const uint8_t copyBit = static_cast<uint8_t>(1u << copy);
    uint32_t runStart = 0;
    uint32_t runCount = 0;
    size_t kept = 0;
    for (uint32_t slot : m_slots) {
      uint8_t& pending = m_pendingCopies[slot];
      if (pending & copyBit) {
        if (runCount != 0 && runStart + runCount == slot) {
          ++runCount;
        } else {
          if (runCount != 0) {
            run(runStart, runCount);
          }
          runStart = slot;
          runCount = 1;
        }
        pending &= ~copyBit;
      }
      if (pending) {
        m_slots[kept++] = slot;
      }
    }
    if (runCount != 0) {
      run(runStart, runCount);
    }

    m_slots.resize(kept);
    m_sortedCount = kept;
  }

private:
  std::vector<uint32_t> m_slots;
  std::vector<uint8_t> m_pendingCopies;
  size_t m_sortedCount = 0;
  uint8_t m_allCopies;
};
