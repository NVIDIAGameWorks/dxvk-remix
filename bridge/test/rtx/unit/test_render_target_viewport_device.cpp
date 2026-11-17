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
    windowClass.lpszClassName = L"BridgeRenderTargetViewportTest";
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

  bool expectFullTarget(IDirect3DDevice9* pDevice, UINT size, const char* pWhen) {
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    check(pDevice->GetViewport(&viewport), "GetViewport");
    check(pDevice->GetScissorRect(&scissor), "GetScissorRect");
    const bool viewportMatches = viewport.X == 0 && viewport.Y == 0 && viewport.Width == size && viewport.Height == size
                              && viewport.MinZ == 0.0f && viewport.MaxZ == 1.0f;
    const bool scissorMatches = scissor.left == 0 && scissor.top == 0
                             && scissor.right == static_cast<LONG>(size) && scissor.bottom == static_cast<LONG>(size);
    if (!viewportMatches || !scissorMatches) {
      std::printf("FAILED: %s the viewport is (%lu, %lu, %lu, %lu, %g, %g) and the scissor rect is (%ld, %ld, %ld, %ld), "
                  "expected the full %ux%u target.\n", pWhen, viewport.X, viewport.Y, viewport.Width, viewport.Height,
                  viewport.MinZ, viewport.MaxZ, scissor.left, scissor.top, scissor.right, scissor.bottom, size, size);
      return false;
    }
    return true;
  }

  void setSmallViewport(IDirect3DDevice9* pDevice) {
    const D3DVIEWPORT9 viewport = { 1, 2, 3, 4, 0.25f, 0.75f };
    const RECT scissor = { 1, 2, 4, 6 };
    check(pDevice->SetViewport(&viewport), "SetViewport");
    check(pDevice->SetScissorRect(&scissor), "SetScissorRect");
  }

  bool runTest(HWND hWnd) {
    D3DPRESENT_PARAMETERS present = presentParameters(hWnd, kSize);
    ComPtr<IDirect3DDevice9> pDevice;
    pDevice.Attach(createDevice(hWnd, present));
    bool passed = expectFullTarget(pDevice.Get(), kSize, "On a new device");

    logLine("Testing that SetRenderTarget(0) resets the viewport and the scissor rect...");
    ComPtr<IDirect3DSurface9> pBackBuffer;
    ComPtr<IDirect3DSurface9> pTarget;
    check(pDevice->GetRenderTarget(0, &pBackBuffer), "GetRenderTarget");
    check(pDevice->CreateRenderTarget(kSize / 2, kSize / 2, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
                                      0, FALSE, &pTarget, nullptr), "CreateRenderTarget");
    setSmallViewport(pDevice.Get());
    check(pDevice->SetRenderTarget(0, pTarget.Get()), "SetRenderTarget");
    passed = expectFullTarget(pDevice.Get(), kSize / 2, "After SetRenderTarget(0) to a smaller target") && passed;
    setSmallViewport(pDevice.Get());
    check(pDevice->SetRenderTarget(0, pBackBuffer.Get()), "SetRenderTarget");
    passed = expectFullTarget(pDevice.Get(), kSize, "After SetRenderTarget(0) to the back buffer") && passed;
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
