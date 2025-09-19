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
#include "d3d9_volumetexture.h"
#include "shadow_map.h"

#include "util_bridge_assert.h"

/*
 * Direct3DVolumeTexture9_LSS Interface Implementation
 */

HRESULT Direct3DVolumeTexture9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DResource9)
    || riid == __uuidof(IDirect3DBaseTexture9)
    || riid == __uuidof(IDirect3DVolumeTexture9)) {
    *ppvObj = bridge_cast<IDirect3DVolumeTexture9*>(this);
    AddRef();
    return S_OK;
  }

  return Direct3DBaseTexture9_LSS::QueryInterface(riid, ppvObj);
}

ULONG Direct3DVolumeTexture9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return Direct3DContainer9_LSS::AddRef();
}

ULONG Direct3DVolumeTexture9_LSS::Release() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::Release();
}

void Direct3DVolumeTexture9_LSS::onDestroy() {
  ClientMessage c(Commands::IDirect3DVolumeTexture9_Destroy, getId());
}

HRESULT Direct3DVolumeTexture9_LSS::GetLevelDesc(UINT Level, D3DVOLUME_DESC* pDesc) {
  LogFunctionCall();

  if (Level >= GetLevelCount()) {
    return D3DERR_INVALIDCALL;
  }
  if (pDesc == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  const TEXTURE_DESC& desc = getDesc();

  // Fill in the surface desc 
  pDesc->Format = desc.Format;
  pDesc->Type = D3DRTYPE_VOLUME;
  pDesc->Usage = desc.Usage;
  pDesc->Pool = desc.Pool;
  pDesc->Width = std::max(1u, desc.Width >> Level);
  pDesc->Height = std::max(1u, desc.Height >> Level);
  pDesc->Depth = std::max(1u, desc.Depth >> Level);

  if (GlobalOptions::getSendReadOnlyCalls()) {
    ClientMessage c(Commands::IDirect3DVolumeTexture9_GetLevelDesc, getId());
    c.send_data(sizeof(D3DVOLUME_DESC), pDesc);
    c.send_data(Level);
  }
  return S_OK;
}

HRESULT Direct3DVolumeTexture9_LSS::GetVolumeLevel(UINT Level, IDirect3DVolume9** ppVolumeLevel) {
  LogFunctionCall();

  if (Level >= GetLevelCount()) {
    return D3DERR_INVALIDCALL;
  }
  if (ppVolumeLevel == nullptr) {
    return D3DERR_INVALIDCALL;
  }

  if (auto pVolume = getChild(Level)) {
    pVolume->AddRef();
    *ppVolumeLevel = pVolume;
    return D3D_OK;
  }

  {
    Direct3DVolume9_LSS* pLssVolume = nullptr;

    {
      BRIDGE_PARENT_DEVICE_LOCKGUARD();

      D3DVOLUME_DESC desc;
      GetLevelDesc(Level, &desc);

      pLssVolume = trackWrapper(new Direct3DVolume9_LSS(m_pDevice, this, desc));
      setChild(Level, pLssVolume);
    }

    (*ppVolumeLevel) = pLssVolume;

    {
      ClientMessage c(Commands::IDirect3DVolumeTexture9_GetVolumeLevel, getId());
      c.send_data(Level);
      c.send_data((uint32_t) pLssVolume->getId());
    }
  }
  return S_OK;
}

HRESULT Direct3DVolumeTexture9_LSS::LockBox(UINT Level, D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;
  
  // Fast path: fetch and use child volume if it was previously initialized
  if (auto pVolume = getChild(Level)) {
    return pVolume->LockBox(pLockedVolume, pBox, Flags);
  }
  
  // Child volume was not initialized - use getter and initialize child in the process
  IDirect3DVolume9* pVolume;
  HRESULT hresult = GetVolumeLevel(Level, &pVolume);
  if (SUCCEEDED(hresult)) {
    hresult = pVolume->LockBox(pLockedVolume, pBox, Flags);
    // Release volume interface
    pVolume->Release();
    return hresult;
  }
  // LockRect may only return INVALIDCALL
  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DVolumeTexture9_LSS::UnlockBox(UINT Level) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;

  if (auto pVolume = getChild(Level)) {
    return pVolume->UnlockBox();
  }
  
  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DVolumeTexture9_LSS::AddDirtyBox(CONST D3DBOX* pDirtyBox) {
  LogFunctionCall();

  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DVolumeTexture9_AddDirtyBox, getId());
    currentUID = c.get_uid();
    c.send_data(sizeof(D3DBOX), (void*) pDirtyBox);
  }
  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("AddDirtyBox()", D3DERR_INVALIDCALL, currentUID);
}
