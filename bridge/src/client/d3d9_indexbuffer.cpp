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
#include "d3d9_indexbuffer.h"

#include "pch.h"
#include "d3d9_util.h"

#include "util_bridge_assert.h"

#include <assert.h>

/*
 * Direct3DIndexBuffer9_LSS Interface Implementation
 */

HRESULT Direct3DIndexBuffer9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DResource9)
    || riid == __uuidof(IDirect3DIndexBuffer9)) {
    *ppvObj = bridge_cast<IDirect3DIndexBuffer9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DIndexBuffer9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return Direct3DResource9_LSS::AddRef();
}

ULONG Direct3DIndexBuffer9_LSS::Release() {
  LogFunctionCall();
  return Direct3DResource9_LSS::Release();
}

void Direct3DIndexBuffer9_LSS::onDestroy() {
  ClientMessage { Commands::IDirect3DIndexBuffer9_Destroy, getId() };
}

HRESULT Direct3DIndexBuffer9_LSS::Lock(UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags) {
  LogFunctionCall();
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    const auto hresult = lock(OffsetToLock, SizeToLock, ppbData, Flags);
    if (!SUCCEEDED(hresult)) {
      std::stringstream ss;
      ss << "[Direct3DIndexBuffer9_LSS][Lock] Failed: {";
      ss << "OffsetToLock=" << OffsetToLock << ",";
      ss << "SizeToLock=" << SizeToLock << ",";
      ss << "Flags=" << Flags << "}";
      Logger::err(ss.str());
    }
    return hresult;
  }
  return S_OK;
}

HRESULT Direct3DIndexBuffer9_LSS::Unlock() {
  LogFunctionCall();
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    unlock();
  }
  return S_OK;
}

HRESULT Direct3DIndexBuffer9_LSS::GetDesc(D3DINDEXBUFFER_DESC* pDesc) {
  LogFunctionCall();
  if (pDesc == nullptr)
    return D3DERR_INVALIDCALL;

  (*pDesc) = m_desc;

  if (GlobalOptions::getSendReadOnlyCalls()) {
    ClientMessage c(Commands::IDirect3DIndexBuffer9_GetDesc, getId());
    c.send_data(sizeof(D3DINDEXBUFFER_DESC), pDesc);
  }
  return S_OK;
}
