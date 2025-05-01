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
#pragma once

#include "util_common.h"
#include "log/log.h"

#include <assert.h>
#include <winver.h>
#include <windows.h>

#ifndef _WIN64 
#ifdef HACK_D3D_DEBUG_MSG
#pragma pack(push)
#pragma pack(1)
typedef struct _JMPCODE {
  BYTE jmp;
  DWORD addr;
}JMPCODE, * PJMPCODE;
#pragma pack(pop)

int __cdecl D3DRecordHRESULT(char* na) {
  // NOTE: The error string (although loaded into a register, is never actually pushed to the stack...  hence why we load it directly from asm in preamble (x86)
  char* dataStr;
  __asm {
    mov dataStr, edx
  }

  // Filter messages we dont care, nor can do nothing, about
  if (std::strstr(dataStr, "OsThunkDDIQueryAdapterInfo failed") != nullptr)
    return 0;

  bridge_util::Logger::err(std::string("[D3D-ERROR]:") + std::string(dataStr));

  return 0;
}

#endif
#endif

template <typename WinStrT>
static uint32_t GetD3DRecordHRESULTPrivateFuncOffset(WinStrT d3d9SysPath) {
  DWORD minorVersion = 0;
  DWORD rsvd = 0;
  DWORD verSize = GetFileVersionInfoSize(d3d9SysPath, &rsvd);
  assert(verSize > 0);
  LPSTR verData = new char[verSize];
  if (GetFileVersionInfo(d3d9SysPath, rsvd, verSize, verData)) {
    UINT size = 0;
    LPBYTE lpBuffer = NULL;
    if (VerQueryValue(verData, TEXT("\\"), (LPVOID*) &lpBuffer, &size)) {
      assert(size > 0);
      VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*) lpBuffer;
      if (verInfo->dwSignature == 0xfeef04bd) {
        auto a = std::to_string((verInfo->dwFileVersionMS >> 16) & 0xffff);
        auto b = std::to_string((verInfo->dwFileVersionMS >> 0) & 0xffff);
        auto c = std::to_string((verInfo->dwFileVersionLS >> 16) & 0xffff);
        auto d = std::to_string((verInfo->dwFileVersionLS >> 0) & 0xffff);
        minorVersion = verInfo->dwFileVersionLS;
      }
    }
  }
  // Found this by comparing the base offset of the D3D9.DLL with the private function 'd3d9.dll!_D3DRecordHRESULT'
  switch (minorVersion) {
  case ((19041 << 16) | 1387): // 10.0.19041.1387
    return 0x5A26c;
  case ((19041 << 16) | 1566): // 10.0.19041.1566
    return 0x5926C;
  case ((19041 << 16) | 1806): // 10.0.19041.1806
  case ((19041 << 16) | 1865): // 10.0.19041.1865
    return 0x58C68;
  default:
    return -1;
  }
  delete[] verData;
}

template <typename WinStrT>
static void FixD3DRecordHRESULT(WinStrT d3d9SysPath, HMODULE d3d9SysModule) {
#ifndef _WIN64 

#ifdef HACK_D3D_DEBUG_MSG

  const uint32_t offsetToD3DRecordHRESULT = GetD3DRecordHRESULTPrivateFuncOffset(d3d9SysPath);
  if (offsetToD3DRecordHRESULT == -1) {
    bridge_util::Logger::warn("D3D9 debug outputs not supported on this version of D3D9.");
    bridge_util::Logger::warn("Please find the d3d9.dll!_D3DRecordHRESULT private func offset and add to the table in 'util_hack_d3d_debug.h'");
    return;
  }
  void* oldaddr = (void*) ((uint32_t) d3d9SysModule + offsetToD3DRecordHRESULT);
  JMPCODE shellcode;
  shellcode.jmp = 0xE9;
  shellcode.addr = (uint32_t) D3DRecordHRESULT - (uint32_t) oldaddr - 5u; // 5 is size of jmpcode + operand
  WriteProcessMemory(GetCurrentProcess(), oldaddr, &shellcode, sizeof(JMPCODE), NULL);

#endif
#endif
}