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

// Creates D3D9 devices through the 32-bit bridge after the server failed to initialize the Remix API.
// No Remix runtime is deployed, so the server loads the system d3d9.dll, which does not export the Remix API.
// D3D9 still works, and the server must create the devices without registering them with Remix.
// The test fails on machines where the system d3d9.dll cannot create devices, such as CI runners without a desktop.

#include "test_remix_api_bridge_app.h"

using namespace bridge_app_test;

namespace {
  constexpr UINT kWindowSize = 64;

  HWND createHiddenWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RemixApiDeviceRegistrationTest";
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                           0, 0, kWindowSize, kWindowSize,
                           nullptr, nullptr, windowClass.hInstance, nullptr);
  }

  D3DPRESENT_PARAMETERS getPresentParams(const HWND hWnd) {
    D3DPRESENT_PARAMETERS presentParams = {};
    presentParams.Windowed = TRUE;
    presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentParams.BackBufferFormat = D3DFMT_UNKNOWN;
    presentParams.BackBufferWidth = kWindowSize;
    presentParams.BackBufferHeight = kWindowSize;
    presentParams.hDeviceWindow = hWnd;
    return presentParams;
  }

  // The system d3d9.dll cannot create devices without an interactive desktop, which CI runners can lack.
  // Checking this first gives a clear failure instead of a failed bridge call.
  // This must run before the bridge client is loaded, because the client hooks the system Direct3DCreate9.
  bool canCreateSystemDevice(const HWND hWnd) {
    wchar_t systemDir[MAX_PATH] = {};
    GetSystemDirectoryW(systemDir, MAX_PATH);
    const HMODULE hSystemD3D9 = LoadLibraryW((std::wstring(systemDir) + L"\\d3d9.dll").c_str());
    if (hSystemD3D9 == nullptr) {
      return false;
    }
    const auto pfnDirect3DCreate9 =
      reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hSystemD3D9, "Direct3DCreate9"));
    IDirect3D9* pD3D = pfnDirect3DCreate9 != nullptr ? pfnDirect3DCreate9(D3D_SDK_VERSION) : nullptr;
    if (pD3D == nullptr) {
      return false;
    }

    D3DPRESENT_PARAMETERS presentParams = getPresentParams(hWnd);
    IDirect3DDevice9* pDevice = nullptr;
    const HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                          D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                          &presentParams, &pDevice);
    std::printf("  The system d3d9.dll returned 0x%08lX from CreateDevice.\n", static_cast<unsigned long>(hr));
    std::fflush(stdout);
    if (SUCCEEDED(hr)) {
      pDevice->Release();
    }
    pD3D->Release();
    return SUCCEEDED(hr);
  }

  // Each request is answered by the server, so the expected status also shows that the server is still running.
  bool checkInitializeLibrary(const PFN_remixapi_InitializeLibrary pfnInitializeLibrary, const char* when) {
    std::printf("Calling remixapi_InitializeLibrary %s...\n", when);
    std::fflush(stdout);
    const remixapi_ErrorCode status = initializeLibrary(pfnInitializeLibrary);
    logStatus(status);
    if (status != REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE) {
      std::printf("FAILED: Expected the server's REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE (%u).\n",
                  static_cast<unsigned>(REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE));
      std::fflush(stdout);
      return false;
    }
    return true;
  }

  bool checkCreated(const char* function, const HRESULT hr) {
    std::printf("  %s returned 0x%08lX.\n", function, static_cast<unsigned long>(hr));
    std::fflush(stdout);
    if (FAILED(hr)) {
      std::printf("FAILED: %s failed.\n", function);
      std::fflush(stdout);
      return false;
    }
    return true;
  }
}

int main() {
  const HWND hWnd = createHiddenWindow();
  if (hWnd == nullptr) {
    std::printf("FAILED: Could not create a window (error %lu).\n", GetLastError());
    return 1;
  }

  logLine("Checking that this machine can create a device with the system d3d9.dll...");
  if (!canCreateSystemDevice(hWnd)) {
    logLine("FAILED: This machine cannot create D3D9 devices with the system d3d9.dll. "
            "The test needs a machine with an interactive desktop.");
    DestroyWindow(hWnd);
    return 1;
  }

  const HMODULE hClient = loadBridgeClient();
  if (hClient == nullptr) {
    return 1;
  }

  const PFN_remixapi_InitializeLibrary pfnInitializeLibrary = getInitializeLibrary(hClient);
  const auto pfnDirect3DCreate9 =
    reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hClient, "Direct3DCreate9"));
  const auto pfnDirect3DCreate9Ex =
    reinterpret_cast<PFN_Direct3DCreate9Ex>(GetProcAddress(hClient, "Direct3DCreate9Ex"));
  if (pfnInitializeLibrary == nullptr || pfnDirect3DCreate9 == nullptr || pfnDirect3DCreate9Ex == nullptr) {
    logLine("FAILED: The bridge client does not export remixapi_InitializeLibrary, Direct3DCreate9 and Direct3DCreate9Ex.");
    return 1;
  }

  bool passed = true;

  logLine("Calling Direct3DCreate9...");
  IDirect3D9* pD3D = pfnDirect3DCreate9(D3D_SDK_VERSION);
  if (pD3D == nullptr) {
    logLine("FAILED: Direct3DCreate9 returned null.");
    return 1;
  }
  passed = checkInitializeLibrary(pfnInitializeLibrary, "after Direct3DCreate9") && passed;

  logLine("Calling CreateDevice...");
  D3DPRESENT_PARAMETERS presentParams = getPresentParams(hWnd);
  IDirect3DDevice9* pDevice = nullptr;
  const HRESULT createDeviceResult = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                                        &presentParams, &pDevice);
  if (checkCreated("CreateDevice", createDeviceResult)) {
    passed = checkInitializeLibrary(pfnInitializeLibrary, "after CreateDevice") && passed;
    pDevice->Release();
  } else {
    passed = false;
  }
  pD3D->Release();

  logLine("Calling Direct3DCreate9Ex...");
  IDirect3D9Ex* pD3DEx = nullptr;
  if (checkCreated("Direct3DCreate9Ex", pfnDirect3DCreate9Ex(D3D_SDK_VERSION, &pD3DEx))) {
    logLine("Calling CreateDeviceEx...");
    presentParams = getPresentParams(hWnd);
    IDirect3DDevice9Ex* pDeviceEx = nullptr;
    const HRESULT createDeviceExResult = pD3DEx->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                                                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                                                &presentParams, nullptr, &pDeviceEx);
    if (checkCreated("CreateDeviceEx", createDeviceExResult)) {
      passed = checkInitializeLibrary(pfnInitializeLibrary, "after CreateDeviceEx") && passed;
      pDeviceEx->Release();
    } else {
      passed = false;
    }
    pD3DEx->Release();
  } else {
    passed = false;
  }

  DestroyWindow(hWnd);

  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
