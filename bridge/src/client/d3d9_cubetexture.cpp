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

#include "d3d9_cubetexture.h"
#include "shadow_map.h"

#include "util_devicecommand.h"

static inline uint32_t getCubeSurfaceIndex(D3DCUBEMAP_FACES FaceType, UINT Level) {
  return FaceType + Level * caps::MaxCubeFaces;
}

/*
 * Direct3DCubeTexture9_LSS Interface Implementation
 */

HRESULT Direct3DCubeTexture9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DResource9)
    || riid == __uuidof(IDirect3DBaseTexture9)
    || riid == __uuidof(IDirect3DCubeTexture9)) {
    *ppvObj = bridge_cast<IDirect3DResource9*>(this);
    AddRef();
    return S_OK;
  }

  return Direct3DBaseTexture9_LSS::QueryInterface(riid, ppvObj);
}

ULONG Direct3DCubeTexture9_LSS::AddRef() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::AddRef();
}

ULONG Direct3DCubeTexture9_LSS::Release() {
  LogFunctionCall();
  return Direct3DContainer9_LSS::Release();
}

void Direct3DCubeTexture9_LSS::onDestroy() {
   ClientMessage { Commands::IDirect3DCubeTexture9_Destroy, getId() };
}

HRESULT Direct3DCubeTexture9_LSS::GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) {
  LogFunctionCall();
  if (Level >= GetLevelCount()) {
    return D3DERR_INVALIDCALL;
  }
  if (pDesc == nullptr) {
    return D3DERR_INVALIDCALL;
  }

  *pDesc = getLevelDesc(Level);

  if (GlobalOptions::getSendReadOnlyCalls()) {
    ClientMessage c(Commands::IDirect3DCubeTexture9_GetLevelDesc, getId());
    c.send_data(sizeof(D3DSURFACE_DESC), pDesc);
    c.send_data(Level);
  }
  return S_OK;
}

D3DSURFACE_DESC Direct3DCubeTexture9_LSS::getLevelDesc(const UINT level) const {
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

HRESULT Direct3DCubeTexture9_LSS::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9** ppCubeMapSurface) {
  LogFunctionCall();

  if (Level >= GetLevelCount())
    return D3DERR_INVALIDCALL;

  if (FaceType > D3DCUBEMAP_FACE_NEGATIVE_Z)
    return D3DERR_INVALIDCALL;

  if (ppCubeMapSurface == nullptr)
    return D3DERR_INVALIDCALL;

  const uint32_t surfaceIndex = getCubeSurfaceIndex(FaceType, Level);

  if (auto surface = getChild(surfaceIndex)) {
    surface->AddRef();
    *ppCubeMapSurface = surface;
    return D3D_OK;
  }

  Direct3DSurface9_LSS* pLssCubeMapSurface = nullptr;
  D3DSURFACE_DESC desc;
  {
    BRIDGE_PARENT_DEVICE_LOCKGUARD();
    // Insert our own IDirect3DSurface9 interface implementation

    GetLevelDesc(Level, &desc);

    pLssCubeMapSurface = trackWrapper(new Direct3DSurface9_LSS(m_pDevice, this, desc));
    (*ppCubeMapSurface) = (IDirect3DSurface9*) pLssCubeMapSurface;

    setChild(surfaceIndex, pLssCubeMapSurface);
  }

  {
    ClientMessage c(Commands::IDirect3DCubeTexture9_GetCubeMapSurface, getId());
    c.send_many(FaceType, Level);
    c.send_data(pLssCubeMapSurface->getId());
  }
  return S_OK;
}

HRESULT Direct3DCubeTexture9_LSS::LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;

  if (FaceType >= caps::MaxCubeFaces)
    return D3DERR_INVALIDCALL;

  const uint32_t surfaceIndex = getCubeSurfaceIndex(FaceType, Level);

  // Fast path: fetch and use child surface if it was previously initialized
  if (auto surface = getChild(surfaceIndex)) {
    return surface->LockRect(pLockedRect, pRect, Flags);
  }

  // Child surface was not initialized - use getter and initialize child in the process
  IDirect3DSurface9* pSurface;
  HRESULT hresult = GetCubeMapSurface(FaceType, Level, &pSurface);

  if (SUCCEEDED(hresult)) {
    hresult = pSurface->LockRect(pLockedRect, pRect, Flags);

    // Release surface interface
    pSurface->Release();

    return hresult;
  }

  // LockRect may only return INVALIDCALL if unsuccessful
  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DCubeTexture9_LSS::UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) {
  LogFunctionCall();

  if (Level >= getDesc().Levels)
    return D3DERR_INVALIDCALL;

  if (FaceType >= caps::MaxCubeFaces)
    return D3DERR_INVALIDCALL;

  if (auto child = m_children[getCubeSurfaceIndex(FaceType, Level)]) {
    return child->UnlockRect();
  }

  return D3DERR_INVALIDCALL;
}

HRESULT Direct3DCubeTexture9_LSS::AddDirtyRect(D3DCUBEMAP_FACES FaceType, CONST RECT* pDirtyRect) {
  LogFunctionCall();

  UID currentUID = 0;
  {
    ClientMessage c(Commands::IDirect3DCubeTexture9_AddDirtyRect, getId());
    currentUID = c.get_uid();
    c.send_data(FaceType);
    c.send_data(sizeof(RECT), (void*) pDirtyRect);
  }
  WAIT_FOR_OPTIONAL_SERVER_RESPONSE("AddDirtyRect()", D3DERR_INVALIDCALL, currentUID);
}