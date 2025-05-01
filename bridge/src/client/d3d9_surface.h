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
#pragma once
#include "d3d9_util.h"
#include "d3d9_resource.h"

#include <unknwn.h>
#include <d3d9.h>
#include "util_gdi.h"

#include <queue>

/*
 * IDirect3DSurface9 LSS Interceptor Class
 */
class Direct3DSurface9_LSS: public Direct3DResource9_LSS<IDirect3DSurface9> {
  void onDestroy() override;

  const D3DSURFACE_DESC m_desc;
  const bool m_bUseSharedHeap = false;
  gdi::D3DKMT_DESTROYDCFROMMEMORY m_dcDesc;
  SharedHeap::AllocId m_bufferId = SharedHeap::kInvalidId;
  struct LockInfo {
    D3DLOCKED_RECT lockedRect;
    RECT rect;
    DWORD flags;
    SharedHeap::AllocId bufId = SharedHeap::kInvalidId;
    SharedHeap::AllocId discardBufId = SharedHeap::kInvalidId;
  };
  std::queue<LockInfo> m_lockInfoQueue;

  std::unique_ptr<uint8_t[]> m_shadow;
  inline static size_t g_totalSurfaceShadow = 0;

public:
  Direct3DSurface9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice,
                       const D3DSURFACE_DESC& desc, 
                       bool isBackBuffer = false);

  template<typename ContainerType>
  Direct3DSurface9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice,
                       ContainerType* const pContainer,
                       const D3DSURFACE_DESC& desc, 
                       bool isBackBuffer = false)
    : Direct3DResource9_LSS((IDirect3DSurface9*)nullptr, pDevice, pContainer)
    , m_bUseSharedHeap(GlobalOptions::getUseSharedHeapForTextures())
    , m_desc(desc)
    , m_isBackBuffer(isBackBuffer) {
  }

  ~Direct3DSurface9_LSS();

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) {
    return D3DRTYPE_SURFACE;
  }

  /*** IDirect3DSurface9 methods ***/
  STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer);
  STDMETHOD(GetDesc)(THIS_ D3DSURFACE_DESC* pDesc);
  STDMETHOD(LockRect)(THIS_ D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
  STDMETHOD(UnlockRect)(THIS);
  STDMETHOD(GetDC)(THIS_ HDC* phdc);
  STDMETHOD(ReleaseDC)(THIS_ HDC hdc);

  D3DSURFACE_DESC getDesc() const {
    return m_desc;
  }

private:
  /*** Lock/Unlock Functionality ***/
  bool m_isBackBuffer;
  bool lock(D3DLOCKED_RECT& lockedRect, const RECT* pRect, const DWORD& flags);
  void unlock();
  static RECT resolveLockInfoRect(const RECT* const pRect, const D3DSURFACE_DESC& desc);
  void* getBufPtr(const int pitch, const RECT& rect);
  void sendDataToServer(const LockInfo& lockInfo) const;
  static std::tuple<size_t, size_t> getRectDimensions(const RECT& box);
};