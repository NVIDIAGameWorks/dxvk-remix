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
#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_pending_slot_queues.h"

namespace dxvk {
  // Note: Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_pending_slot_queues.log");
}

namespace {
  struct SlotRun {
    uint32_t firstSlot;
    uint32_t count;

    bool operator==(const SlotRun& other) const {
      return firstSlot == other.firstSlot && count == other.count;
    }
  };

  using Runs = std::vector<SlotRun>;

  constexpr uint32_t kCopyCount = 3;

  void check(bool condition, const char* message) {
    if (!condition) {
      throw dxvk::DxvkError(message);
    }
  }

  Runs take(PendingSlotQueues& queues, uint32_t copy) {
    Runs runs;
    queues.takeRuns(copy, [&runs](uint32_t firstSlot, uint32_t count) {
      runs.push_back({ firstSlot, count });
    });
    return runs;
  }

  void testClearDropsQueuedSlots() {
    PendingSlotQueues queues(kCopyCount);
    queues.mark(5);
    take(queues, 0);
    queues.mark(7);
    queues.clear();

    for (uint32_t copy = 0; copy < kCopyCount; ++copy) {
      check(take(queues, copy).empty(), "clear must drop the queued slots of every copy");
    }

    queues.mark(5);
    for (uint32_t copy = 0; copy < kCopyCount; ++copy) {
      check(take(queues, copy) == Runs { { 5, 1 } }, "a slot queued after clear must reach every copy");
    }
  }

  void testChangeReachesEveryCopyOnce() {
    PendingSlotQueues queues(kCopyCount);

    queues.mark(7);
    for (uint32_t copy = 0; copy < kCopyCount; ++copy) {
      check(take(queues, copy) == Runs { { 7, 1 } }, "every copy must receive a changed slot");
    }
    for (uint32_t copy = 0; copy < kCopyCount; ++copy) {
      check(take(queues, copy).empty(), "a rotation without changes must not write anything");
    }
  }

  void testRepeatedChangeBeforeRotationIsQueuedOnce() {
    PendingSlotQueues queues(kCopyCount);

    queues.mark(4);
    check(take(queues, 0) == Runs { { 4, 1 } }, "the current copy must receive the change");

    // The slot is released and reused before the lagging copies are written.
    queues.mark(4);
    queues.mark(4);
    check(take(queues, 0) == Runs { { 4, 1 } }, "the current copy must receive the change again");
    check(take(queues, 1) == Runs { { 4, 1 } }, "a lagging copy must receive the slot once");
    check(take(queues, 2) == Runs { { 4, 1 } }, "a lagging copy must receive the slot once");
  }

  void testRunsAreSortedAndCoalesced() {
    PendingSlotQueues queues(kCopyCount);

    for (uint32_t slot : { 12u, 3u, 11u, 2u, 1u, 10u, 20u }) {
      queues.mark(slot);
    }
    check(take(queues, 0) == Runs { { 1, 3 }, { 10, 3 }, { 20, 1 } }, "adjacent slots must be coalesced and gaps excluded");
  }

  void testLaterChangesMergeInOrder() {
    PendingSlotQueues queues(kCopyCount);

    queues.mark(5);
    queues.mark(6);
    check(take(queues, 0) == Runs { { 5, 2 } }, "the current copy must receive the first changes");

    // Lower and higher slots change after copy 0 was written, while copies 1 and 2 still lag.
    queues.mark(9);
    queues.mark(1);
    queues.mark(4);
    check(take(queues, 1) == Runs { { 1, 1 }, { 4, 3 }, { 9, 1 } }, "a lagging copy must receive old and new changes in order");
    check(take(queues, 0) == Runs { { 1, 1 }, { 4, 1 }, { 9, 1 } }, "an up-to-date copy must receive only the newer changes");
    check(take(queues, 2) == Runs { { 1, 1 }, { 4, 3 }, { 9, 1 } }, "a lagging copy must receive old and new changes in order");
    for (uint32_t copy = 0; copy < kCopyCount; ++copy) {
      check(take(queues, copy).empty(), "every change must be handed out once per copy");
    }
  }

  void testCopiesDrainIndependently() {
    PendingSlotQueues queues(kCopyCount);

    queues.mark(1);
    check(take(queues, 0) == Runs { { 1, 1 } }, "the current copy must receive the first change");
    queues.mark(2);
    check(take(queues, 1) == Runs { { 1, 2 } }, "a lagging copy must receive every change since its last write");
    check(take(queues, 0) == Runs { { 2, 1 } }, "an up-to-date copy must receive only the newer change");
    check(take(queues, 2) == Runs { { 1, 2 } }, "a lagging copy must receive every change since its last write");
  }
}

int main() {
  try {
    testClearDropsQueuedSlots();
    testChangeReachesEveryCopyOnce();
    testRepeatedChangeBeforeRotationIsQueuedOnce();
    testRunsAreSortedAndCoalesced();
    testLaterChangesMergeInOrder();
    testCopiesDrainIndependently();
    std::cout << "All passed\n";
  } catch (const dxvk::DxvkError& error) {
    std::cerr << error.message() << std::endl;
    return 1;
  }

  return 0;
}
