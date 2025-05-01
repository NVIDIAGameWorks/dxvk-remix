/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "util_gdi.h"
#include "log/log.h"

namespace gdi {

  HMODULE GetGDIModule() {
    static HMODULE module = LoadLibraryA("gdi32.dll");
    return module;
  }

  NTSTATUS D3DKMTCreateDCFromMemory(D3DKMT_CREATEDCFROMMEMORY* Arg1) {
    static auto func = (D3DKMTCreateDCFromMemoryType)
      GetProcAddress(GetGDIModule(), "D3DKMTCreateDCFromMemory");

    if (func != nullptr)
      return func(Arg1);

    bridge_util::Logger::warn("D3DKMTCreateDCFromMemory: Unable to query proc address.");
    return -1;
  }

  NTSTATUS D3DKMTDestroyDCFromMemory(D3DKMT_DESTROYDCFROMMEMORY* Arg1) {
    static auto func = (D3DKMTDestroyDCFromMemoryType)
      GetProcAddress(GetGDIModule(), "D3DKMTDestroyDCFromMemory");

    if (func != nullptr)
      return func(Arg1);

    bridge_util::Logger::warn("D3DKMTDestroyDCFromMemory: Unable to query proc address.");
    return -1;
  }

}