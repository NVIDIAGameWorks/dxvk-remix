/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "d3d9_lss.h"

class Direct3DVolume9_LSS: public D3DBase<IDirect3DVolume9> {
  void onDestroy() override;
  D3DVOLUME_DESC m_desc;
  struct LockInfo {
    D3DLOCKED_BOX lockedVolume;
    D3DBOX box;
    DWORD flags;
  };
  std::queue<LockInfo> m_lockInfoQueue;
protected:
  BaseDirect3DDevice9Ex_LSS* const m_pDevice = nullptr;
  PrivateDataFactory m_privateData;
public:
  Direct3DVolume9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice, const D3DVOLUME_DESC& desc)
    : D3DBase((IDirect3DVolume9*) nullptr, pDevice)
    , m_pDevice(pDevice)
    , m_desc(desc) {
    assert(0 && "Must not be called!");
  }

  template<typename ContainerType>
  Direct3DVolume9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice,
                      ContainerType* const pContainerVolumeTexture,
                      const D3DVOLUME_DESC& desc)
    : D3DBase((IDirect3DVolume9*) nullptr, pDevice, pContainerVolumeTexture)
    , m_pDevice(pDevice)
    , m_desc(desc) {
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice);
  STDMETHOD(SetPrivateData)(THIS_ REFGUID refguid, CONST void* pData, DWORD SizeOfData, DWORD Flags);
  STDMETHOD(GetPrivateData)(THIS_ REFGUID refguid, void* pData, DWORD* pSizeOfData);
  STDMETHOD(FreePrivateData)(THIS_ REFGUID refguid);
  STDMETHOD(GetContainer)(THIS_ REFIID riid, void** ppContainer);
  STDMETHOD(GetDesc)(THIS_ D3DVOLUME_DESC* pDesc);
  STDMETHOD(LockBox)(THIS_ D3DLOCKED_BOX* pLockedVolume, CONST D3DBOX* pBox, DWORD Flags);
  STDMETHOD(UnlockBox)(THIS);

private:
  bool lock(D3DLOCKED_BOX& lockedVolume, const D3DBOX* const box, const DWORD flags);
  void unlock();
  static D3DBOX resolveLockInfoBox(const D3DBOX* const pBox, const D3DVOLUME_DESC& desc);
  static std::tuple<size_t, size_t, size_t> getBoxDimensions(const D3DBOX& box);
};