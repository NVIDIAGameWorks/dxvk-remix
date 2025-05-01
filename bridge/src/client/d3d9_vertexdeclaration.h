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

#include "d3d9_util.h"
#include "base.h"
#include "d3d9_device_base.h"

#include <vector>

class Direct3DVertexDeclaration9_LSS: public D3DBase<IDirect3DVertexDeclaration9> {
  void onDestroy() override;
  std::vector<D3DVERTEXELEMENT9> m_elements;

protected:
  BaseDirect3DDevice9Ex_LSS* const m_pDevice = nullptr;
public:
  Direct3DVertexDeclaration9_LSS(BaseDirect3DDevice9Ex_LSS* const pDevice, CONST D3DVERTEXELEMENT9* pVertexElements)
    : D3DBase((IDirect3DVertexDeclaration9*) nullptr, pDevice)
    , m_pDevice(pDevice) {

    const D3DVERTEXELEMENT9* counter = pVertexElements;
    while (counter->Stream != 0xFF) {
      m_elements.push_back(*counter);
      ++counter;
    }
    m_elements.push_back(*counter);
  }

  /*** IUnknown methods ***/
  STDMETHOD(QueryInterface)(THIS_ REFIID riid, void** ppvObj);
  STDMETHOD_(ULONG, AddRef)(THIS);
  STDMETHOD_(ULONG, Release)(THIS);

  /*** IDirect3DVertexDeclaration9 methods ***/
  STDMETHOD(GetDevice)(THIS_ IDirect3DDevice9** ppDevice);
  STDMETHOD(GetDeclaration)(THIS_ D3DVERTEXELEMENT9* pElement, UINT* pNumElements);
};