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
#include <stdio.h>
#include <stdint.h>

#include "d3d9_lss.h"

#include "detours.cpp"
#include "creatwth.cpp"
#include "disasm.cpp"
#include "image.cpp"
#include "modules.cpp"

#define API_HOOK_DECL(x) decltype(x)* orig_##x = x
#define API_HOOK_DECL2(x) decltype(x)* orig_##x = nullptr
#define API_ATTACH(x) DetourAttach(&(PVOID&)orig_##x, Hooked##x)
#define API_DETACH(x) DetourDetach(&(PVOID&)orig_##x, Hooked##x)
#define GETPROC_ORIG(h, x) \
  orig_##x = reinterpret_cast<decltype(x)*>(::GetProcAddress(h, #x))

API_HOOK_DECL2(Direct3DCreate9);
API_HOOK_DECL2(Direct3DCreate9Ex);

#ifdef WITH_FULL_D3D9_HOOK
API_HOOK_DECL(D3DPERF_BeginEvent);
API_HOOK_DECL(D3DPERF_EndEvent);
API_HOOK_DECL(D3DPERF_SetMarker);
API_HOOK_DECL(D3DPERF_SetRegion);
API_HOOK_DECL(D3DPERF_QueryRepeatFrame);
API_HOOK_DECL(D3DPERF_SetOptions);
API_HOOK_DECL(D3DPERF_GetStatus);
#endif

extern bool RemixAttach(HMODULE);
extern void RemixDetach();
extern std::chrono::steady_clock::time_point gTimeStart;

static HMODULE ghSystemD3D9;
static bool gRemixAttached = false;


static HRESULT WINAPI HookedDirect3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDeviceEx) {
  return LssDirect3DCreate9Ex(SDKVersion, ppDeviceEx);
}

static IDirect3D9* WINAPI HookedDirect3DCreate9(UINT SDKVersion) {
  return LssDirect3DCreate9(SDKVersion);
}

#ifdef WITH_FULL_D3D9_HOOK
static int WINAPI HookedD3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
  return 0;
}

static int WINAPI HookedD3DPERF_EndEvent(void) {
  return 0;
}

static void WINAPI HookedD3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
}

static void WINAPI HookedD3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
}

static BOOL WINAPI HookedD3DPERF_QueryRepeatFrame(void) {
  return FALSE;
}

static void WINAPI HookedD3DPERF_SetOptions(DWORD dwOptions) {
}

static DWORD WINAPI HookedD3DPERF_GetStatus(void) {
  return 0;
}
#endif

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
  (void) hinst;
  (void) reserved;

  bool InitRemixFolder(HMODULE hModule);

  if (DetourIsHelperProcess()) {
    return TRUE;
  }

  if (dwReason == DLL_PROCESS_ATTACH) {
    OutputDebugStringA("Attaching Remix...\n");

    if (!InitRemixFolder(hinst)) {
      OutputDebugStringA("Fatal: Unable to initialize Remix folder...\n");
      return FALSE;
    }

    // Attempt to pull in system imports first.
    char szSystemD3D9[1024];
    GetSystemDirectoryA(szSystemD3D9, sizeof(szSystemD3D9));
    StringCchCatA(szSystemD3D9, sizeof(szSystemD3D9), "\\d3d9.dll");

    if (ghSystemD3D9 = LoadLibrary(szSystemD3D9)) {
      GETPROC_ORIG(ghSystemD3D9, Direct3DCreate9);
      GETPROC_ORIG(ghSystemD3D9, Direct3DCreate9Ex);
    } else {
      OutputDebugStringA("Fatal: system d3d9.dll cannot be loaded. Unable to attach Remix...\n");
      return FALSE;
    }

    DetourRestoreAfterWith();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    API_ATTACH(Direct3DCreate9Ex);
    API_ATTACH(Direct3DCreate9);
#ifdef WITH_FULL_D3D9_HOOK
    API_ATTACH(D3DPERF_BeginEvent);
    API_ATTACH(D3DPERF_EndEvent);
    API_ATTACH(D3DPERF_SetMarker);
    API_ATTACH(D3DPERF_SetRegion);
    API_ATTACH(D3DPERF_QueryRepeatFrame);
    API_ATTACH(D3DPERF_SetOptions);
    API_ATTACH(D3DPERF_GetStatus);
#endif
    const LONG error = DetourTransactionCommit();

    if (error != NO_ERROR) {
      OutputDebugStringA("Error detouring d3d9...\n");
    } else if (RemixAttach(hinst)) {
      gTimeStart = std::chrono::high_resolution_clock::now();
      gRemixAttached = true;
      return TRUE;
    }
    return FALSE;
  } else if (dwReason == DLL_PROCESS_DETACH) {
    if (gRemixAttached) {
      RemixDetach();

      const auto timeEnd = std::chrono::high_resolution_clock::now();
      std::stringstream uptimeSS;
      uptimeSS << "[Uptime]: ";
      uptimeSS <<
        std::chrono::duration_cast<std::chrono::seconds>(timeEnd - gTimeStart).count();
      uptimeSS << "s";
      Logger::info(uptimeSS.str());
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    API_DETACH(Direct3DCreate9Ex);
    API_DETACH(Direct3DCreate9);
#ifdef WITH_FULL_D3D9_HOOK
    API_DETACH(D3DPERF_BeginEvent);
    API_DETACH(D3DPERF_EndEvent);
    API_DETACH(D3DPERF_SetMarker);
    API_DETACH(D3DPERF_SetRegion);
    API_DETACH(D3DPERF_QueryRepeatFrame);
    API_DETACH(D3DPERF_SetOptions);
    API_DETACH(D3DPERF_GetStatus);
#endif
    DetourTransactionCommit();
  }

  return TRUE;
}
