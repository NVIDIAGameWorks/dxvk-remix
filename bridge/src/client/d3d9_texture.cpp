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

#include "d3d9_texture.h"

#include "d3d9_util.h"
#include "d3d9_surface.h"
#include "shadow_map.h"
#include "util_bridge_assert.h"

#include <d3d9.h>

/*
 * Direct3DTexture9_LSS Interface Implementation
 */

HRESULT Direct3DTexture9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DResource9)
    || riid == __uuidof(IDirect3DBaseTexture9)
    || riid == __uuidof(IDirect3DTexture9)) {
    *ppvObj = bridge_cast<IDirect3DTexture9*>(this);
    AddRef();
    return S_OK;
  }

  return Direct3DBaseTexture9_LSS::QueryInterface(riid, ppvObj);
}

ULONG Direct3DTexture9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return Direct3DContainer9_LSS::AddRef();
}

ULONG Direct3DTexture9_LSS::Release() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::Release();
}

void Direct3DTexture9_LSS::onDestroy() {
  ClientMessage { Commands::IDirect3DTexture9_Destroy, getId() };
}

HRESULT Direct3DTexture9_LSS::GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) {
  LogFunctionCall();

  if (Level >= GetLevelCount()) {
    return D3DERR_INVALIDCALL;
  }
  if (pDesc == nullptr) {
    return D3DERR_INVALIDCALL;
  }

  *pDesc = getLevelDesc(Level);

  if (GlobalOptions::getSendReadOnlyCalls()) {
    ClientMessage c(Commands::IDirect3DTexture9_GetLevelDesc, getId());
    c.send_data(sizeof(D3DSURFACE_DESC), pDesc);
    c.send_data(Level);
  }
  return S_OK;
}

D3DSURFACE_DESC Direct3DTexture9_LSS::getLevelDesc(const UINT level) const {
  const TEXTURE_DESC& desc = getDesc();
  return { desc.Format,
           D3DRTYPE_SURFACE,
           desc.Usage,
           desc.Pool,
           D3DMULTISAMPLE_NONE,
           0,
           std::max(1u, desc.Width >> level),
           std::max(1u, desc.Height >> level) };
}

HRESULT Direct3DTexture9_LSS::GetSurfaceLevel(UINT Level, IDirect3DSurface9** ppSurfaceLevel) {
  LogFunctionCall();

  if (Level >= GetLevelCount())
    return D3DERR_INVALIDCALL;

  if (ppSurfaceLevel == nullptr)
    return D3DERR_INVALIDCALL;

  if (auto surface = getChild(Level)) {
    surface->AddRef();
    *ppSurfaceLevel = surface;
    return D3D_OK;
  }

  // Insert our own IDirect3DSurface9 interface implementation
  D3DSURFACE_DESC desc;
  GetLevelDesc(Level, &desc);

  Direct3DSurface9_LSS* pLssSurface = trackWrapper(new Direct3DSurface9_LSS(m_pDevice, this, desc));
  setChild(Level, pLssSurface);
    
  (*ppSurfaceLevel) = pLssSurface;

  // Add handles for both the texture and surface
  {
    ClientMessage c(Commands::IDirect3DTexture9_GetSurfaceLevel, getId());
    c.send_data(Level);
    c.send_data(pLssSurface->getId());
  }
  
  return S_OK;
}

HRESULT Direct3DTexture9_LSS::LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;

  // Fast path: fetch and use child surface if it was previously initialized
  if (auto surface = getChild(Level)) {
    return surface->LockRect(pLockedRect, pRect, Flags);
  }
  
  // Child surface was not initialized - use getter and initialize child in the process
  IDirect3DSurface9* pSurface;
  HRESULT hresult = GetSurfaceLevel(Level, &pSurface);

  if (SUCCEEDED(hresult)) {
    hresult = pSurface->LockRect(pLockedRect, pRect, Flags);

    // Release surface interface
    pSurface->Release();

    return hresult;
  }

  // LockRect may only return INVALIDCALL
  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DTexture9_LSS::UnlockRect(UINT Level) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;

  if (auto surface = getChild(Level)) {
    return surface->UnlockRect();
  }
  
  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DTexture9_LSS::AddDirtyRect(CONST RECT* pDirtyRect) {
  LogFunctionCall();
  
  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DTexture9_AddDirtyRect, getId());
    currentUID = c.get_uid();
    c.send_data(sizeof(RECT), (void*) pDirtyRect);
  }
  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("AddDirtyRect()", D3DERR_INVALIDCALL, currentUID);
}