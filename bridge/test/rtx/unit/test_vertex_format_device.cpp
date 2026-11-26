/*
 * Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdio>
#include <stdexcept>

#include <wrl/client.h>

#include "test_remix_api_bridge_app.h"

using Microsoft::WRL::ComPtr;
using namespace bridge_app_test;

namespace {
  constexpr UINT kSize = 32;

  void check(HRESULT result, const char* pCall) {
    if (FAILED(result)) {
      std::printf("FAILED: %s returned 0x%08lX.\n", pCall, static_cast<unsigned long>(result));
      throw std::runtime_error(pCall);
    }
  }

  HWND createWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BridgeVertexFormatTest";
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, kSize, kSize,
                           nullptr, nullptr, windowClass.hInstance, nullptr);
  }

  D3DPRESENT_PARAMETERS presentParameters(HWND hWnd, UINT size) {
    D3DPRESENT_PARAMETERS present = {};
    present.Windowed = TRUE;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.BackBufferFormat = D3DFMT_UNKNOWN;
    present.BackBufferWidth = size;
    present.BackBufferHeight = size;
    present.hDeviceWindow = hWnd;
    return present;
  }

  // Creates a device through the bridge client. The caller owns the returned reference.
  IDirect3DDevice9* createDevice(HWND hWnd, D3DPRESENT_PARAMETERS& present) {
    const HMODULE hClient = loadBridgeClient();
    if (hClient == nullptr) {
      throw std::runtime_error("loadBridgeClient");
    }
    const auto pfnCreate = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hClient, "Direct3DCreate9"));
    ComPtr<IDirect3D9> pD3D;
    pD3D.Attach(pfnCreate != nullptr ? pfnCreate(D3D_SDK_VERSION) : nullptr);
    if (pD3D == nullptr) {
      throw std::runtime_error("Direct3DCreate9");
    }
    IDirect3DDevice9* pDevice = nullptr;
    check(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                             &present, &pDevice), "CreateDevice");
    return pDevice;
  }

  constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;

  // Checks GetFVF, and whether GetVertexDeclaration returns pDecl.
  bool expectVertexInput(IDirect3DDevice9* pDevice, DWORD fvf, IDirect3DVertexDeclaration9* pDecl, bool declBound,
                         const char* pWhen) {
    DWORD actualFvf = ~0u;
    ComPtr<IDirect3DVertexDeclaration9> pActualDecl;
    check(pDevice->GetFVF(&actualFvf), "GetFVF");
    check(pDevice->GetVertexDeclaration(&pActualDecl), "GetVertexDeclaration");
    const bool actualBound = pActualDecl.Get() == pDecl;
    if (actualFvf != fvf || actualBound != declBound) {
      std::printf("FAILED: %s GetFVF returned 0x%lX, expected 0x%lX, and the declaration is %s, expected %s.\n",
                  pWhen, actualFvf, fvf, actualBound ? "bound" : "not bound", declBound ? "bound" : "not bound");
      return false;
    }
    return true;
  }

  bool runTest(HWND hWnd) {
    D3DPRESENT_PARAMETERS present = presentParameters(hWnd, kSize);
    ComPtr<IDirect3DDevice9> pDevice;
    pDevice.Attach(createDevice(hWnd, present));
    const D3DVERTEXELEMENT9 elements[] = {
      { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
      { 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
      { 1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
      D3DDECL_END()
    };
    ComPtr<IDirect3DVertexDeclaration9> pDecl;
    check(pDevice->CreateVertexDeclaration(elements, &pDecl), "CreateVertexDeclaration");
    bool passed = true;

    logLine("Testing that SetFVF and SetVertexDeclaration replace each other...");
    check(pDevice->SetFVF(kFvf), "SetFVF");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After SetFVF") && passed;
    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After SetVertexDeclaration") && passed;
    check(pDevice->SetFVF(0), "SetFVF(0)");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After SetFVF(0), which d3d9 ignores,") && passed;
    check(pDevice->SetFVF(kFvf), "SetFVF");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After SetFVF over a declaration") && passed;

    logLine("Testing that a state block restores the FVF together with the declaration...");
    ComPtr<IDirect3DStateBlock9> pFvfBlock;
    check(pDevice->CreateStateBlock(D3DSBT_ALL, &pFvfBlock), "CreateStateBlock");
    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    ComPtr<IDirect3DStateBlock9> pDeclBlock;
    check(pDevice->CreateStateBlock(D3DSBT_ALL, &pDeclBlock), "CreateStateBlock");
    check(pFvfBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After a state block with an FVF") && passed;
    check(pDeclBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After a state block with a declaration") && passed;

    logLine("Testing that recording a state block leaves the device's vertex input as it is...");
    check(pDevice->SetFVF(kFvf), "SetFVF");
    check(pDevice->BeginStateBlock(), "BeginStateBlock");
    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    ComPtr<IDirect3DStateBlock9> pRecordedDeclBlock;
    check(pDevice->EndStateBlock(&pRecordedDeclBlock), "EndStateBlock");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After recording a declaration") && passed;
    check(pRecordedDeclBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After applying a recorded declaration") && passed;

    check(pDevice->BeginStateBlock(), "BeginStateBlock");
    check(pDevice->SetFVF(kFvf), "SetFVF");
    ComPtr<IDirect3DStateBlock9> pRecordedFvfBlock;
    check(pDevice->EndStateBlock(&pRecordedFvfBlock), "EndStateBlock");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After recording an FVF") && passed;
    check(pRecordedFvfBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After a recorded block with an FVF") && passed;

    logLine("Testing that Capture takes the FVF together with the declaration...");
    check(pRecordedDeclBlock->Capture(), "StateBlock::Capture");
    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    check(pRecordedDeclBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After capturing an FVF") && passed;

    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    check(pRecordedFvfBlock->Capture(), "StateBlock::Capture");
    check(pDevice->SetFVF(kFvf), "SetFVF");
    check(pRecordedFvfBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), 0, pDecl.Get(), true, "After capturing a declaration") && passed;

    ComPtr<IDirect3DStateBlock9> pVertexBlock;
    check(pDevice->CreateStateBlock(D3DSBT_VERTEXSTATE, &pVertexBlock), "CreateStateBlock");
    check(pDevice->SetFVF(kFvf), "SetFVF");
    check(pVertexBlock->Capture(), "StateBlock::Capture");
    check(pDevice->SetVertexDeclaration(pDecl.Get()), "SetVertexDeclaration");
    check(pVertexBlock->Apply(), "StateBlock::Apply");
    passed = expectVertexInput(pDevice.Get(), kFvf, pDecl.Get(), false, "After a vertex state capture") && passed;
    return passed;
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const HWND hWnd = createWindow();
  if (hWnd == nullptr) {
    return 1;
  }
  bool passed = false;
  try {
    passed = runTest(hWnd);
  } catch (const std::exception& error) {
    std::printf("FAILED: %s.\n", error.what());
  }
  DestroyWindow(hWnd);
  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
