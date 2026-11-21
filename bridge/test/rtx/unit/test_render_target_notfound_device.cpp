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
    windowClass.lpszClassName = L"BridgeRenderTargetNotFoundTest";
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

  bool expectNotFound(HRESULT result, const IDirect3DSurface9* pSurface, const char* pCall) {
    if (result != D3DERR_NOTFOUND || pSurface != nullptr) {
      std::printf("FAILED: %s returned 0x%08lX and surface %p, expected D3DERR_NOTFOUND and no surface.\n",
                  pCall, static_cast<unsigned long>(result), static_cast<const void*>(pSurface));
      return false;
    }
    return true;
  }

  bool runTest(HWND hWnd) {
    // The device has no depth-stencil surface, because EnableAutoDepthStencil stays FALSE.
    D3DPRESENT_PARAMETERS present = presentParameters(hWnd, kSize);
    ComPtr<IDirect3DDevice9> pDevice;
    pDevice.Attach(createDevice(hWnd, present));
    bool passed = true;

    // Each call starts from a stale pointer, because the call must also clear its output.
    IDirect3DSurface9* const pStale = reinterpret_cast<IDirect3DSurface9*>(static_cast<UINT_PTR>(1));

    logLine("Testing GetDepthStencilSurface without a depth-stencil surface...");
    IDirect3DSurface9* pSurface = pStale;
    HRESULT result = pDevice->GetDepthStencilSurface(&pSurface);
    passed = expectNotFound(result, pSurface, "GetDepthStencilSurface") && passed;

    logLine("Testing GetDepthStencilSurface with a depth-stencil surface, and after it is removed...");
    ComPtr<IDirect3DSurface9> pDepth;
    check(pDevice->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                             &pDepth, nullptr), "CreateDepthStencilSurface");
    check(pDevice->SetDepthStencilSurface(pDepth.Get()), "SetDepthStencilSurface");
    ComPtr<IDirect3DSurface9> pBound;
    check(pDevice->GetDepthStencilSurface(&pBound), "GetDepthStencilSurface");
    if (pBound != pDepth) {
      logLine("FAILED: GetDepthStencilSurface did not return the surface that was set.");
      passed = false;
    }
    pBound.Reset();
    check(pDevice->SetDepthStencilSurface(nullptr), "SetDepthStencilSurface(nullptr)");
    pSurface = pStale;
    result = pDevice->GetDepthStencilSurface(&pSurface);
    passed = expectNotFound(result, pSurface, "GetDepthStencilSurface") && passed;

    logLine("Testing GetRenderTarget on an empty slot...");
    pSurface = pStale;
    result = pDevice->GetRenderTarget(1, &pSurface);
    passed = expectNotFound(result, pSurface, "GetRenderTarget(1)") && passed;
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
