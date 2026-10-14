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
#pragma once

// Helpers for the 32-bit test apps that test_remix_api_bridge_app.py runs against the bridge.
// The script deploys each app next to the bridge client d3d9.dll, with the server and bridge.conf in .trex.

#include <cstdio>
#include <string>

#include <d3d9.h>

#include <remixapi/bridge_remix_api.h>

namespace bridge_app_test {
  using PFN_Direct3DCreate9 = IDirect3D9* (WINAPI*)(UINT);
  using PFN_Direct3DCreate9Ex = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);

  inline void logLine(const char* message) {
    std::printf("%s\n", message);
    std::fflush(stdout);
  }

  inline void logStatus(const remixapi_ErrorCode status) {
    std::printf("  Returned %u.\n", static_cast<unsigned>(status));
    std::fflush(stdout);
  }

  // Loads the bridge client d3d9.dll from the folder of this executable.
  // Returns nullptr after logging the error when it cannot be loaded.
  inline HMODULE loadBridgeClient() {
    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring clientPath(exePath, len);
    clientPath = clientPath.substr(0, clientPath.find_last_of(L"\\/") + 1) + L"d3d9.dll";

    const HMODULE hClient = LoadLibraryW(clientPath.c_str());
    if (hClient == nullptr) {
      std::printf("FAILED: Could not load the bridge client from %ls (error %lu).\n", clientPath.c_str(), GetLastError());
      std::fflush(stdout);
    }
    return hClient;
  }

  inline PFN_remixapi_InitializeLibrary getInitializeLibrary(const HMODULE hClient) {
    return reinterpret_cast<PFN_remixapi_InitializeLibrary>(
      GetProcAddress(hClient, remixapi::exported_func_name::initRemixApi));
  }

  inline remixapi_ErrorCode initializeLibrary(const PFN_remixapi_InitializeLibrary pfnInitializeLibrary) {
    remixapi_InitializeLibraryInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
    info.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR,
                                         REMIXAPI_VERSION_MINOR,
                                         REMIXAPI_VERSION_PATCH);
    remixapi_Interface remixInterface = {};
    return pfnInitializeLibrary(&info, &remixInterface);
  }
}
