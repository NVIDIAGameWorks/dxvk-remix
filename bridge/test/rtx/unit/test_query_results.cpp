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
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "d3d9_query_result.h"
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
    for (const auto slot : slots) {
      QueryResults::freeSlot(slot);
    }
  }

  void testSharedBlock() {
    QueryResults::init();
    QuerySharedBlock* const pBlock = QueryResults::block();
    check(pBlock != nullptr, "The shared block must be mapped.");
    for (uint32_t i = 0; i < QuerySharedBlock::kMaxSlots; ++i) {
      check(pBlock->slots[i].answeredSeq.load() == 0, "Every slot must start without an answer.");
    }

    QueryResults::init();
    check(QueryResults::block() == pBlock, "A second init must keep the mapping.");
  }

  struct QueryRequest {
    uint32_t size;
    DWORD flags;
    uint32_t slot;
    uint32_t sequence;
  };

  struct DelayedGpuEvent {
    bool issued = false;
    bool complete = false;
    HRESULT error = S_OK;
    DWORD lastFlags = 0;
    uint32_t polls = 0;

    void issue() {
      issued = true;
      complete = false;
    }

    HRESULT getData(void* pData, DWORD size, DWORD flags) {
      check(issued, "The server must consume Issue before polling the GPU event.");
      check(size == sizeof(BOOL), "Event requests must retrieve the GPU's BOOL result.");
      ++polls;
      lastFlags = flags;
      if (FAILED(error)) {
        return error;
      }
      if (!complete) {
        return S_FALSE;
      }
      *static_cast<BOOL*>(pData) = TRUE;
      return S_OK;
    }
  };

  struct EventPolling {
    AsyncQueryResult result;
    DelayedGpuEvent gpu;
    std::vector<QueryRequest> requests;
    size_t processed = 0;
    bool cached = false;

    EventPolling() {
      result.init(sizeof(BOOL));
      check(result.enabled(), "Event queries must use an asynchronous slot when one is available.");
    }

    ~EventPolling() {
      result.release();
    }

    void issue() {
      result.reset();
      gpu.issue();
    }

    HRESULT poll(void* pData, DWORD size, DWORD flags = 0, bool bridgeRunning = true) {
      return result.getData(pData, size, flags, bridgeRunning, cached,
        [this](uint32_t dataSize, DWORD dataFlags, uint32_t slot, uint32_t sequence) {
          requests.push_back({dataSize, dataFlags, slot, sequence});
        });
    }

    void processRequest() {
      check(processed < requests.size(), "There must be a request for the server to process.");
      const auto request = requests[processed++];
      BOOL data = FALSE;
      const HRESULT hresult = gpu.getData(&data, request.size, request.flags);
      auto& slot = QueryResults::block()->slots[request.slot];
      slot.hresult = hresult;
      slot.size = request.size;
      if (hresult == S_OK) {
        memcpy(slot.data, &data, sizeof(data));
      }
      slot.answeredSeq.store(request.sequence, std::memory_order_release);
    }
  };

  void testDelayedGpuEvent() {
    EventPolling query;
    query.issue();
    BOOL complete = 42;
    check(query.poll(&complete, sizeof(complete)) == S_FALSE, "Consuming Issue must not imply GPU completion.");
    check(query.poll(&complete, sizeof(complete)) == S_FALSE, "An unanswered request must stay pending.");
    check(query.requests.size() == 1, "Polling an unanswered request must not enqueue another request.");
    check(complete == 42, "Pending event polls must not overwrite the caller's data.");

    query.processRequest();
    check(query.poll(&complete, sizeof(complete), D3DGETDATA_FLUSH) == S_FALSE, "A pending GPU event must stay pending after the server responds.");
    check(query.requests.size() == 2, "A pending GPU response must allow another poll.");
    query.processRequest();
    check(query.gpu.lastFlags == D3DGETDATA_FLUSH, "D3DGETDATA_FLUSH must reach the GPU query.");
    check(query.poll(&complete, sizeof(complete), D3DGETDATA_FLUSH) == S_FALSE, "Flushing must not falsely complete a delayed GPU event.");
    check(complete == 42, "A flushed pending event must not write TRUE.");

    query.gpu.complete = true;
    query.processRequest();
    check(query.poll(&complete, sizeof(complete)) == S_OK && complete == TRUE, "Only a real completed GPU event may return S_OK and TRUE.");
    const auto requestCount = query.requests.size();
    complete = FALSE;
    check(query.poll(&complete, sizeof(complete)) == S_OK && complete == TRUE, "A completed event result must remain available.");
    check(query.cached && query.requests.size() == requestCount, "A cached event must not send another request.");
  }

  void testStatusOnlyEvent() {
    EventPolling query;
    query.issue();
    check(query.poll(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE, "A status-only poll must wait for the actual GPU event.");
    query.processRequest();
    check(query.poll(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE, "A status-only pending GPU response must remain S_FALSE.");
    query.gpu.complete = true;
    query.processRequest();
    check(query.poll(nullptr, 0) == S_OK, "A status-only poll must complete after the GPU event signals.");
    BOOL complete = FALSE;
    check(query.poll(&complete, sizeof(complete)) == S_OK && complete == TRUE, "Status-only polling must retain the result for later data polling.");
  }

  void testInFlightFlush() {
    EventPolling query;
    query.issue();
    check(query.poll(nullptr, 0) == S_FALSE, "The initial event request must be asynchronous.");
    check(query.poll(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE, "A flush during an outstanding request must remain asynchronous.");
    check(query.requests.size() == 1, "An in-flight flush must not duplicate the outstanding request.");
    query.processRequest();
    check(query.gpu.lastFlags == 0, "The initial server poll must retain its original flags.");
    check(query.poll(nullptr, 0) == S_FALSE, "A pending result must allow the latched flush to be sent.");
    query.processRequest();
    check(query.gpu.lastFlags == D3DGETDATA_FLUSH, "The next server poll must retain FLUSH even when the next client call has flags zero.");
    check(query.poll(nullptr, 0) == S_FALSE, "The next poll must remain pending until the GPU completes.");
    query.processRequest();
    check(query.gpu.lastFlags == 0, "A delivered flush must not stick to later requests.");
  }

  void testReissuedEvent() {
    EventPolling query;
    query.issue();
    check(query.poll(nullptr, 0) == S_FALSE, "The initial event request must be asynchronous.");
    query.issue();
    check(query.poll(nullptr, 0) == S_FALSE, "Reissuing an event must discard its previous request.");
    query.gpu.complete = true;
    query.processRequest();
    check(query.poll(nullptr, 0) == S_FALSE, "A previous issue's completed response must not complete the new issue.");
    query.processRequest();
    check(query.poll(nullptr, 0) == S_OK, "The current issue's completed response must complete the query.");
    query.issue();
    check(query.poll(nullptr, 0) == S_FALSE, "Issue must invalidate a cached completed result.");
  }

  void testQueryErrors() {
    EventPolling query;
    query.issue();
    check(query.poll(nullptr, 0) == S_FALSE, "A query error must be obtained from the server.");
    query.gpu.error = D3DERR_INVALIDCALL;
    query.processRequest();
    check(query.poll(nullptr, 0) == D3DERR_INVALIDCALL, "A server query error must reach the client.");
    check(query.poll(nullptr, 0, 0, false) == D3DERR_DEVICELOST, "A stopped bridge must not report an uncompleted event as successful.");
  }

  void testEventSlotExhaustion() {
    std::vector<std::unique_ptr<AsyncQueryResult>> queries;
    for (uint32_t i = 0; i < QuerySharedBlock::kMaxSlots; ++i) {
      auto pQuery = std::make_unique<AsyncQueryResult>();
      pQuery->init(sizeof(BOOL));
      check(pQuery->enabled(), "Every available slot must support an event query.");
      queries.push_back(std::move(pQuery));
    }
    AsyncQueryResult fallback;
    fallback.init(sizeof(BOOL));
    check(!fallback.enabled(), "Event queries must select the blocking path when slots are exhausted.");
    queries.back()->release();
    fallback.init(sizeof(BOOL));
    check(fallback.enabled(), "An event query must be able to use a freed slot.");
    fallback.release();
    for (auto& pQuery : queries) {
      pQuery->release();
    }
  }
}

int main() {
  try {
    AsyncQueryResult unmapped;
    unmapped.init(sizeof(BOOL));
    check(!unmapped.enabled(), "Queries must select the blocking path when shared memory is unavailable.");
    testSharedBlock();
    testSequenceNumbers();
    testSlots();
    testDelayedGpuEvent();
    testStatusOnlyEvent();
    testInFlightFlush();
    testReissuedEvent();
    testQueryErrors();
    testEventSlotExhaustion();
  } catch (const std::string& errorMessage) {
    std::cerr << "Query results test failed: " << errorMessage << std::endl;
    return -1;
  }
  std::cout << "Query results tests passed." << std::endl;
  return 0;
}
