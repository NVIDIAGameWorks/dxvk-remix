/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "pch.h"
#include "d3d9_query.h"
#include "client_options.h"
#include "util_devicecommand.h"
#include "util_queryresults.h"

#include <algorithm>
#include <cstring>

Direct3DQuery9_LSS::Direct3DQuery9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice, D3DQUERYTYPE Type)
  : D3DBase<IDirect3DQuery9>((IDirect3DQuery9*) nullptr, pDevice)
  , m_type(Type)
  , m_pDevice(pDevice) {
  if (!ClientOptions::getAsyncQueries() || bridge_util::QueryResults::block() == nullptr) {
    return;
  }
  if (m_type == D3DQUERYTYPE_EVENT) {
    m_mode = Mode::ClientEvent;
  } else if (GetDataSize() <= bridge_util::QueryResultSlot::kMaxDataSize) {
    m_slot = bridge_util::QueryResults::allocSlot();
    if (m_slot != bridge_util::QueryResults::kNoSlot) {
      m_mode = Mode::AsyncSlot;
    }
  }
}

HRESULT Direct3DQuery9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DQuery9)) {
    *ppvObj = bridge_cast<IDirect3DQuery9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DQuery9_LSS::AddRef() {
  LogMissingFunctionCall();
  return D3DBase::AddRef();
}

ULONG Direct3DQuery9_LSS::Release() {
  LogMissingFunctionCall();
  return D3DBase::Release();
}

void Direct3DQuery9_LSS::onDestroy() {
  LogFunctionCall();
  ClientMessage { Commands::IDirect3DQuery9_Destroy, getId() };
  // The slot can be reused at once, because a later owner waits for its own sequence number.
  bridge_util::QueryResults::freeSlot(m_slot);
  m_slot = bridge_util::QueryResults::kNoSlot;
}

HRESULT Direct3DQuery9_LSS::GetDevice(IDirect3DDevice9** ppDevice) {
  LogFunctionCall();
  m_pDevice->AddRef();
  (*ppDevice) = m_pDevice;
  return S_OK;
}

D3DQUERYTYPE Direct3DQuery9_LSS::GetType() {
  LogFunctionCall();
  return m_type;
}

DWORD Direct3DQuery9_LSS::GetDataSize() {
  LogFunctionCall();
  switch (m_type) {
  case D3DQUERYTYPE_VCACHE:               return sizeof(D3DDEVINFO_VCACHE);
  case D3DQUERYTYPE_RESOURCEMANAGER:      return sizeof(D3DDEVINFO_RESOURCEMANAGER);
  case D3DQUERYTYPE_VERTEXSTATS:          return sizeof(D3DDEVINFO_D3DVERTEXSTATS);
  case D3DQUERYTYPE_EVENT:                return sizeof(BOOL);
  case D3DQUERYTYPE_OCCLUSION:            return sizeof(DWORD);
  case D3DQUERYTYPE_TIMESTAMP:            return sizeof(UINT64);
  case D3DQUERYTYPE_TIMESTAMPDISJOINT:    return sizeof(BOOL);
  case D3DQUERYTYPE_TIMESTAMPFREQ:        return sizeof(UINT64);
  case D3DQUERYTYPE_PIPELINETIMINGS:      return sizeof(D3DDEVINFO_D3D9PIPELINETIMINGS);
  case D3DQUERYTYPE_INTERFACETIMINGS:     return sizeof(D3DDEVINFO_D3D9INTERFACETIMINGS);
  case D3DQUERYTYPE_VERTEXTIMINGS:        return sizeof(D3DDEVINFO_D3D9STAGETIMINGS);
  case D3DQUERYTYPE_PIXELTIMINGS:         return sizeof(D3DDEVINFO_D3D9PIPELINETIMINGS);
  case D3DQUERYTYPE_BANDWIDTHTIMINGS:     return sizeof(D3DDEVINFO_D3D9BANDWIDTHTIMINGS);
  case D3DQUERYTYPE_CACHEUTILIZATION:     return sizeof(D3DDEVINFO_D3D9CACHEUTILIZATION);
  default:                                return 0;
  }
}

HRESULT Direct3DQuery9_LSS::Issue(DWORD dwIssueFlags) {
  LogFunctionCall();
  
  UID currentUID = 0;
  
  {
    ClientMessage c(Commands::IDirect3DQuery9_Issue, getId());
    currentUID = c.get_uid();
    c.send_data(dwIssueFlags);
  }

  m_issued = true;
  m_issueUid = (uint32_t) currentUID;
  m_requestSeq = 0;
  m_haveResult = false;

  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("Direct3DQuery9_LSS::Issue()", D3DERR_INVALIDCALL, currentUID);

  return S_OK;
}

void Direct3DQuery9_LSS::copyResult(void* pData, DWORD dwSize) const {
  if (pData != nullptr && dwSize > 0) {
    memcpy(pData, m_result, std::min<size_t>(dwSize, m_resultSize));
  }
}

HRESULT Direct3DQuery9_LSS::GetData(void* pData, DWORD dwSize, DWORD dwGetDataFlags) {
  LogFunctionCall();
  switch (m_mode) {
  case Mode::ClientEvent:
    return getDataClientEvent(pData, dwSize);
  case Mode::AsyncSlot:
    return getDataAsync(pData, dwSize, dwGetDataFlags);
  default:
    return getDataBlocking(pData, dwSize, dwGetDataFlags);
  }
}

HRESULT Direct3DQuery9_LSS::getDataClientEvent(void* pData, DWORD dwSize) {
  // The runtime issues a query that the game did not issue, so the client does the same.
  if (!m_issued) {
    Issue(D3DISSUE_END);
  }
  // The event is signalled once the server has executed the Issue, and the runtime's frame pacing
  // bounds how far the GPU runs behind.
  const uint32_t processed = bridge_util::QueryResults::block()->issuedProcessedUid.load(std::memory_order_acquire);
  const bool signalled = !gbBridgeRunning || bridge_util::uidReached(processed, m_issueUid);
  if (pData != nullptr && dwSize >= sizeof(BOOL)) {
    *static_cast<BOOL*>(pData) = signalled ? TRUE : FALSE;
  }
  if (bridge_util::perf::enabled()) {
    bridge_util::perf::noteQuery(bridge_util::perf::QueryPath::ClientEvent, signalled);
  }
  return signalled ? S_OK : S_FALSE;
}

HRESULT Direct3DQuery9_LSS::getDataAsync(void* pData, DWORD dwSize, DWORD dwGetDataFlags) {
  using bridge_util::perf::QueryPath;
  const bool perfOn = bridge_util::perf::enabled();

  // D3D9 keeps returning the result once it is available, until the next Issue.
  if (m_haveResult) {
    copyResult(pData, dwSize);
    if (perfOn) {
      bridge_util::perf::noteQuery(QueryPath::AsyncCached, true);
    }
    return S_OK;
  }

  if (m_requestSeq != 0) {
    const bridge_util::QueryResultSlot& slot = bridge_util::QueryResults::block()->slots[m_slot];
    if (slot.answeredSeq.load(std::memory_order_acquire) != m_requestSeq) {
      if (perfOn) {
        bridge_util::perf::noteQuery(QueryPath::AsyncRequest, false);
      }
      return gbBridgeRunning ? S_FALSE : D3DERR_DEVICELOST;
    }
    const HRESULT hresult = (HRESULT) slot.hresult;
    m_requestSeq = 0;
    if (hresult == S_OK) {
      m_resultSize = std::min<uint32_t>(slot.size, bridge_util::QueryResultSlot::kMaxDataSize);
      memcpy(m_result, slot.data, m_resultSize);
      m_haveResult = true;
      copyResult(pData, dwSize);
      if (perfOn) {
        bridge_util::perf::noteQuery(QueryPath::AsyncCached, true);
      }
      return S_OK;
    }
    if (FAILED(hresult)) {
      if (perfOn) {
        bridge_util::perf::noteQuery(QueryPath::AsyncCached, false);
      }
      return hresult;
    }
  }

  // Each query has at most one request in flight, so a game that polls in a loop paces the requests.
  m_requestSeq = bridge_util::QueryResults::nextSeq();
  {
    ClientMessage c(Commands::IDirect3DQuery9_GetData, getId(), Commands::FlagBits::AsyncResult);
    c.send_data(GetDataSize());
    c.send_data(dwGetDataFlags);
    c.send_data(m_slot);
    c.send_data(m_requestSeq);
  }
  if (perfOn) {
    bridge_util::perf::noteQuery(QueryPath::AsyncRequest, false);
  }
  return gbBridgeRunning ? S_FALSE : D3DERR_DEVICELOST;
}

HRESULT Direct3DQuery9_LSS::getDataBlocking(void* pData, DWORD dwSize, DWORD dwGetDataFlags) {
  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DQuery9_GetData, getId());
    currentUID = c.get_uid();
    c.send_data(dwSize);
    c.send_data(dwGetDataFlags);
  }

  WAIT_FOR_SERVER_RESPONSE("Direct3DQuery9_LSS::GetData()", D3DERR_INVALIDCALL, currentUID);
  HRESULT hresult = DeviceBridge::get_data();
  // The blob is read even when pData is null, so the response queue stays in step.
  if (SUCCEEDED(hresult) && dwSize > 0) {
    void* pDataReturned = NULL;
    DeviceBridge::get_data(&pDataReturned);
    if (pData != NULL && pDataReturned != NULL) {
      memcpy(pData, pDataReturned, dwSize);
    }
  }

  DeviceBridge::pop_front();

  if (bridge_util::perf::enabled()) {
    bridge_util::perf::noteQuery(bridge_util::perf::QueryPath::Blocking, hresult == S_OK);
  }
  return hresult;
}
