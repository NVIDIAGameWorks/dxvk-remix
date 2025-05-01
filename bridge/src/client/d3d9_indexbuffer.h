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

#include "d3d9_resource.h"
#include "lockable_buffer.h"

class Direct3DIndexBuffer9_LSS: public LockableBuffer<IDirect3DIndexBuffer9> {
  void onDestroy() override;

public:
  Direct3DIndexBuffer9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice, const D3DINDEXBUFFER_DESC& desc)
    : LockableBuffer((IDirect3DIndexBuffer9*)nullptr, pDevice, desc) {
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3DResource9 methods ***/
  STDMETHOD_(D3DRESOURCETYPE, GetType)(THIS) {
    return D3DRTYPE_INDEXBUFFER;
  }

  STDMETHOD(Lock)(THIS_ UINT OffsetToLock, UINT SizeToLock, void** ppbData, DWORD Flags);
  STDMETHOD(Unlock)(THIS);
  STDMETHOD(GetDesc)(THIS_ D3DINDEXBUFFER_DESC* pDesc);
};