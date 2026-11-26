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
#include <cstring>
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
    windowClass.lpszClassName = L"BridgeSurfaceShadowTest";
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

  constexpr UINT kTextureSize = 64;
  constexpr UINT kTextureCount = 16;
  constexpr size_t kRowBytes = kTextureSize * 4;

  bool runTest(HWND hWnd) {
    D3DPRESENT_PARAMETERS present = presentParameters(hWnd, kSize);
    ComPtr<IDirect3DDevice9> pDevice;
    pDevice.Attach(createDevice(hWnd, present));

    logLine("Testing that the first lock of a new texture level returns zeroed memory...");
    for (UINT index = 0; index < kTextureCount; ++index) {
      ComPtr<IDirect3DTexture9> pTexture;
      check(pDevice->CreateTexture(kTextureSize, kTextureSize, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                   &pTexture, nullptr), "CreateTexture");
      D3DLOCKED_RECT locked = {};
      check(pTexture->LockRect(0, &locked, nullptr, 0), "LockRect");
      size_t nonZeroBytes = 0;
      for (UINT y = 0; y < kTextureSize; ++y) {
        BYTE* const pRow = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
        for (size_t x = 0; x < kRowBytes; ++x) {
          nonZeroBytes += pRow[x] != 0;
        }
        // The next texture can get this memory back, and it must still start zeroed.
        std::memset(pRow, 0xff, kRowBytes);
      }
      check(pTexture->UnlockRect(0), "UnlockRect");
      if (nonZeroBytes != 0) {
        std::printf("FAILED: Texture %u started with %zu bytes that are not zero.\n", index, nonZeroBytes);
        return false;
      }
    }
    return true;
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
