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
#include <cstring>

#include <d3d9.h>

#include "util_queryresults.h"

class AsyncQueryResult {
public:
  void init(uint32_t size) {
    if (size <= bridge_util::QueryResultSlot::kMaxDataSize && bridge_util::QueryResults::block() != nullptr) {
      m_slot = bridge_util::QueryResults::allocSlot();
      m_dataSize = size;
    }
  }

  bool enabled() const {
    return m_slot != bridge_util::QueryResults::kNoSlot;
  }

  void release() {
    bridge_util::QueryResults::freeSlot(m_slot);
    m_slot = bridge_util::QueryResults::kNoSlot;
  }

  void reset() {
    m_requestSeq = 0;
    m_pendingFlags = 0;
    m_haveResult = false;
  }

  template<typename SendRequest>
  HRESULT getData(void* pData, DWORD size, DWORD flags, bool bridgeRunning, bool& cached, SendRequest&& sendRequest) {
    cached = false;

    // D3D9 retains a completed result until the next Issue.
    if (m_haveResult) {
      copyResult(pData, size);
      cached = true;
      return S_OK;
    }

    // A flush requested during an outstanding poll must reach the next server poll.
    m_pendingFlags |= flags;

    if (m_requestSeq != 0) {
      const auto& slot = bridge_util::QueryResults::block()->slots[m_slot];
      if (slot.answeredSeq.load(std::memory_order_acquire) != m_requestSeq) {
        return bridgeRunning ? S_FALSE : D3DERR_DEVICELOST;
      }
      const HRESULT hresult = (HRESULT) slot.hresult;
      m_requestSeq = 0;
      if (hresult == S_OK) {
        m_resultSize = std::min<uint32_t>(slot.size, bridge_util::QueryResultSlot::kMaxDataSize);
        memcpy(m_result, slot.data, m_resultSize);
        m_haveResult = true;
        copyResult(pData, size);
        cached = true;
        return S_OK;
      }
      if (FAILED(hresult)) {
        cached = true;
        return hresult;
      }
    }

    if (!bridgeRunning) {
      return D3DERR_DEVICELOST;
    }

    // One outstanding request per query bounds IPC traffic during polling.
    m_requestSeq = bridge_util::QueryResults::nextSeq();
    const DWORD requestFlags = m_pendingFlags;
    m_pendingFlags = 0;
    sendRequest(m_dataSize, requestFlags, m_slot, m_requestSeq);
    return S_FALSE;
  }

private:
  void copyResult(void* pData, DWORD size) const {
    if (pData != nullptr && size > 0) {
      memcpy(pData, m_result, std::min<size_t>(size, m_resultSize));
    }
  }

  uint32_t m_slot = bridge_util::QueryResults::kNoSlot;
  uint32_t m_dataSize = 0;
  uint32_t m_requestSeq = 0;
  DWORD m_pendingFlags = 0;
  bool m_haveResult = false;
  uint32_t m_resultSize = 0;
  uint8_t m_result[bridge_util::QueryResultSlot::kMaxDataSize] = {};
};
