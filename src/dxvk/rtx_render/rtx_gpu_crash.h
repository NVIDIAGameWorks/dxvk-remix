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

#include "../dxvk_include.h"

namespace dxvk {

  class RtxContext;

#ifdef REMIX_DEVELOPMENT
  /**
   * Development-only: dispatches a compute shader that intentionally causes a GPU hang/TDR
   * so Aftermath can capture a crash dump (e.g. when triggered via hotkey).
   */
  class GpuCrashPass {

  public:

    GpuCrashPass(DxvkDevice*) {}
    ~GpuCrashPass() = default;

    void dispatch(RtxContext* ctx);

  };
#else
  /** Stub when REMIX_DEVELOPMENT is not defined; only needs to be constructible for Active<>. */
  class GpuCrashPass {

  public:

    GpuCrashPass(DxvkDevice*) {}

  };
#endif

} // namespace dxvk
