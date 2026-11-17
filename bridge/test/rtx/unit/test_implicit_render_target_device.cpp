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
    windowClass.lpszClassName = L"BridgeImplicitRenderTargetTest";
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

  bool runTest(HWND hWnd) {
    D3DPRESENT_PARAMETERS present = presentParameters(hWnd, kSize);
    IDirect3DDevice9* pDevice = createDevice(hWnd, present);
    bool passed = true;

    logLine("Testing that the implicit surfaces are not counted as references to a new device...");
    const ULONG added = pDevice->AddRef();
    const ULONG released = pDevice->Release();
    if (added != 2 || released != 1) {
      std::printf("FAILED: AddRef returned %lu and Release returned %lu, expected 2 and 1.\n", added, released);
      passed = false;
    }

    logLine("Testing that render target 0 is the back buffer 0 object...");
    {
      ComPtr<IDirect3DSurface9> pRenderTarget;
      ComPtr<IDirect3DSurface9> pBackBuffer;
      check(pDevice->GetRenderTarget(0, &pRenderTarget), "GetRenderTarget");
      check(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer), "GetBackBuffer");
      if (pRenderTarget != pBackBuffer) {
        logLine("FAILED: Render target 0 is not the back buffer 0 object.");
        passed = false;
      }
    }

    const ULONG last = pDevice->Release();
    if (last != 0) {
      std::printf("FAILED: The last Release returned %lu, expected 0.\n", last);
      passed = false;
    }
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
