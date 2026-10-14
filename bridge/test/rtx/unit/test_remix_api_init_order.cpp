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

// Calls remixapi_InitializeLibrary on the 32-bit bridge client before and after Direct3DCreate9.
// The bridge only starts its server in Direct3DCreate9, so the early call must not wait for the server
// or leave anything in the command queue that blocks the handshake.

#include "test_remix_api_bridge_app.h"

using namespace bridge_app_test;

int main() {
  const HMODULE hClient = loadBridgeClient();
  if (hClient == nullptr) {
    return 1;
  }

  const PFN_remixapi_InitializeLibrary pfnInitializeLibrary = getInitializeLibrary(hClient);
  const auto pfnDirect3DCreate9 =
    reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hClient, "Direct3DCreate9"));
  if (pfnInitializeLibrary == nullptr || pfnDirect3DCreate9 == nullptr) {
    logLine("FAILED: The bridge client does not export remixapi_InitializeLibrary and Direct3DCreate9.");
    return 1;
  }

  bool passed = true;

  logLine("Calling remixapi_InitializeLibrary before Direct3DCreate9...");
  const remixapi_ErrorCode earlyStatus = initializeLibrary(pfnInitializeLibrary);
  logStatus(earlyStatus);
  if (earlyStatus != REMIXAPI_ERROR_CODE_NOT_INITIALIZED) {
    logLine("FAILED: Expected REMIXAPI_ERROR_CODE_NOT_INITIALIZED because the server is not running yet.");
    passed = false;
  }

  logLine("Calling Direct3DCreate9...");
  IDirect3D9* pD3D = pfnDirect3DCreate9(D3D_SDK_VERSION);
  if (pD3D == nullptr) {
    logLine("FAILED: Direct3DCreate9 returned null.");
    passed = false;
  }

  // The client returns GENERAL_FAILURE or NOT_INITIALIZED when the server did not answer.
  // Any other code is the server's own initialization result.
  logLine("Calling remixapi_InitializeLibrary after Direct3DCreate9...");
  const remixapi_ErrorCode lateStatus = initializeLibrary(pfnInitializeLibrary);
  logStatus(lateStatus);
  if (lateStatus == REMIXAPI_ERROR_CODE_GENERAL_FAILURE || lateStatus == REMIXAPI_ERROR_CODE_NOT_INITIALIZED) {
    logLine("FAILED: Expected the server's initialization status.");
    passed = false;
  }

  if (pD3D != nullptr) {
    pD3D->Release();
  }

  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
