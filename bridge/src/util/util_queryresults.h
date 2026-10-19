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

#include <atomic>
#include <cstdint>

// Shared memory through which IDirect3DQuery9::GetData is answered without waiting for the server.
// The server writes a slot's result before it publishes answeredSeq.
// All fields are 32-bit, so the layout is the same in the 32-bit client and the 64-bit server.

namespace bridge_util {

  struct QueryResultSlot {
    static constexpr uint32_t kMaxDataSize = 32;

    std::atomic<uint32_t> answeredSeq;
    int32_t hresult;
    uint32_t size;
    uint8_t data[kMaxDataSize];
  };
  static_assert(sizeof(QueryResultSlot) == 44, "QueryResultSlot must have the same layout in both processes");

  struct QuerySharedBlock {
    static constexpr uint32_t kMaxSlots = 4096;

    // Holds the UID of the last IDirect3DQuery9::Issue that the server executed.
    std::atomic<uint32_t> issuedProcessedUid;
    uint32_t pad[15];
    QueryResultSlot slots[kMaxSlots];
  };

  class QueryResults {
  public:
    static constexpr uint32_t kNoSlot = ~0u;

    static void init();

    // Returns nullptr when the mapping failed, and then queries wait for server responses.
    static QuerySharedBlock* block();

    // Returns kNoSlot when all slots are in use, and then that query waits for server responses.
    static uint32_t allocSlot();
    static void freeSlot(uint32_t slot);

    // Sequence numbers are never 0, because 0 marks an unanswered slot.
    static uint32_t nextSeq();
  };

  inline bool uidReached(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) >= 0;
  }
}
