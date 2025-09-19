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
#include "util_devicecommand.h"

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
  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("Direct3DQuery9_LSS::Issue()", D3DERR_INVALIDCALL, currentUID);

  return S_OK;
}

HRESULT Direct3DQuery9_LSS::GetData(void* pData, DWORD dwSize, DWORD dwGetDataFlags) {
  LogFunctionCall();

  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DQuery9_GetData, getId());
    currentUID = c.get_uid();
    c.send_data(dwSize);
    c.send_data(dwGetDataFlags);
  }

  WAIT_FOR_SERVER_RESPONSE("Direct3DQuery9_LSS::GetData()", D3DERR_INVALIDCALL, currentUID);
  HRESULT hresult = DeviceBridge::get_data();
  if (SUCCEEDED(hresult) && dwSize > 0 && pData != NULL) {
    void* pDataReturned = NULL;
    DeviceBridge::get_data(&pDataReturned);
    memcpy(pData, pDataReturned, dwSize);
  }

  DeviceBridge::pop_front();

  return hresult;
}
