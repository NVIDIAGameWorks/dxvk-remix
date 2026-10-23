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
#include <iostream>
#include <set>
#include <string>

#include "util_guid.h"
#include "util_queryresults.h"

// The shared memory names are derived from this identifier, as in the bridge client and server.
bridge_util::Guid gUniqueIdentifier;

namespace {
  using bridge_util::QueryResults;
  using bridge_util::QuerySharedBlock;

  void check(const bool condition, const std::string& message) {
    if (!condition) {
      throw message;
    }
  }

  void testUidReached() {
    check(bridge_util::uidReached(5, 5), "A UID must count as reached by itself.");
    check(bridge_util::uidReached(6, 5), "A later UID must count as reached.");
    check(!bridge_util::uidReached(4, 5), "An earlier UID must not count as reached.");
    check(bridge_util::uidReached(2, 0xfffffffeu), "A UID after a wrap must count as reached.");
    check(!bridge_util::uidReached(0xfffffffeu, 2), "A UID before a wrap must not count as reached.");
  }

  void testSequenceNumbers() {
    uint32_t previous = QueryResults::nextSeq();
    check(previous != 0, "A sequence number must never be 0.");
    for (int i = 0; i < 1000; ++i) {
      const uint32_t seq = QueryResults::nextSeq();
      check(seq != 0, "A sequence number must never be 0.");
      check(seq != previous, "Consecutive sequence numbers must differ.");
      previous = seq;
    }
  }

  void testSlots() {
    std::set<uint32_t> slots;
    for (uint32_t i = 0; i < QuerySharedBlock::kMaxSlots; ++i) {
      const uint32_t slot = QueryResults::allocSlot();
      check(slot < QuerySharedBlock::kMaxSlots, "A slot must be in range while slots are free.");
      check(slots.insert(slot).second, "A slot must not be handed out twice.");
    }
    check(QueryResults::allocSlot() == QueryResults::kNoSlot, "kNoSlot must be returned when all slots are in use.");

    QueryResults::freeSlot(17);
    check(QueryResults::allocSlot() == 17, "A freed slot must be reused.");
    check(QueryResults::allocSlot() == QueryResults::kNoSlot, "kNoSlot must be returned when all slots are in use.");

    QueryResults::freeSlot(QueryResults::kNoSlot);
    check(QueryResults::allocSlot() == QueryResults::kNoSlot, "Freeing kNoSlot must not make a slot available.");
  }

  void testSharedBlock() {
    QueryResults::init();
    QuerySharedBlock* const pBlock = QueryResults::block();
    check(pBlock != nullptr, "The shared block must be mapped.");
    check(pBlock->issuedProcessedUid.load() == 0, "The shared block must start zeroed.");
    for (uint32_t i = 0; i < QuerySharedBlock::kMaxSlots; ++i) {
      check(pBlock->slots[i].answeredSeq.load() == 0, "Every slot must start without an answer.");
    }

    QueryResults::init();
    check(QueryResults::block() == pBlock, "A second init must keep the mapping.");
  }
}

int main() {
  try {
    testUidReached();
    testSequenceNumbers();
    testSlots();
    testSharedBlock();
  } catch (const std::string& errorMessage) {
    std::cerr << "Query results test failed: " << errorMessage << std::endl;
    return -1;
  }
  std::cout << "Query results tests passed." << std::endl;
  return 0;
}
