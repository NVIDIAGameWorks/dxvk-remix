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
#include "d3d9_vertexdeclaration.h"

#include "pch.h"

#include "util_devicecommand.h"

HRESULT Direct3DVertexDeclaration9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr) {
    return E_POINTER;
  }

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DVertexDeclaration9)) {
    *ppvObj = bridge_cast<IDirect3DVertexDeclaration9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DVertexDeclaration9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return D3DBase::AddRef();
}

ULONG Direct3DVertexDeclaration9_LSS::Release() {
  LogFunctionCall();
  return D3DBase::Release();
}

void Direct3DVertexDeclaration9_LSS::onDestroy() {
  ClientMessage { Commands::IDirect3DVertexDeclaration9_Destroy, getId() };
}

HRESULT Direct3DVertexDeclaration9_LSS::GetDevice(IDirect3DDevice9** ppDevice) {
  LogFunctionCall();
  m_pDevice->AddRef();
  (*ppDevice) = m_pDevice;
  return S_OK;
}

HRESULT Direct3DVertexDeclaration9_LSS::GetDeclaration(D3DVERTEXELEMENT9* pElement, UINT* pNumElements) {
  LogFunctionCall();

  if (pNumElements == nullptr)
    return D3DERR_INVALIDCALL;

  if (pElement != nullptr)
    memcpy(pElement, m_elements.data(), m_elements.size() * sizeof(D3DVERTEXELEMENT9));

  (*pNumElements) = m_elements.size();

  return S_OK;
}
