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
#include <memory>
#include <mutex>
#include <vector>

#include "util_queryresults.h"

#include "util_sharedmemory.h"
#include "log/log.h"

namespace bridge_util {
  namespace {
    std::unique_ptr<SharedMemory> s_memory;
    QuerySharedBlock* s_block = nullptr;

    std::mutex s_slotMutex;
    std::vector<uint32_t> s_freeSlots;
    uint32_t s_nextUnusedSlot = 0;

    std::atomic<uint32_t> s_seq { 0 };
  }

  void QueryResults::init() {
    if (s_block != nullptr) {
      return;
    }
    s_memory = std::make_unique<SharedMemory>("QueryResults", sizeof(QuerySharedBlock));
    s_block = static_cast<QuerySharedBlock*>(s_memory->data());
    if (s_block == nullptr) {
      Logger::err("[Bridge] The query results block could not be mapped, so D3D9 queries wait for server responses.");
    }
  }

  QuerySharedBlock* QueryResults::block() {
    return s_block;
  }

  uint32_t QueryResults::allocSlot() {
    std::lock_guard<std::mutex> lock(s_slotMutex);
    if (!s_freeSlots.empty()) {
      const uint32_t slot = s_freeSlots.back();
      s_freeSlots.pop_back();
      return slot;
    }
    if (s_nextUnusedSlot < QuerySharedBlock::kMaxSlots) {
      return s_nextUnusedSlot++;
    }
    return kNoSlot;
  }

  void QueryResults::freeSlot(uint32_t slot) {
    if (slot == kNoSlot) {
      return;
    }
    std::lock_guard<std::mutex> lock(s_slotMutex);
    s_freeSlots.push_back(slot);
  }

  uint32_t QueryResults::nextSeq() {
    uint32_t seq = s_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (seq == 0) {
      seq = s_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    return seq;
  }
}
