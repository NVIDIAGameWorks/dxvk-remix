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
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Development only: dispatched when the GPU crash hotkey is triggered to cause a
// GPU hang/TDR for Aftermath capture. Single variant, minimal bindings.
// Loop condition is cb.trigger so the compiler does not see a literal infinite loop.

#include "gpu_crash_trigger.h"

layout(binding = GPU_CRASH_TRIGGER_OUTPUT)
RWTexture2D<float4> DebugOutput;

layout(push_constant)
ConstantBuffer<GpuCrashTriggerArgs> cb;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  // Intentionally cause GPU hang/TDR so Aftermath can capture (OOB write in loop).
  // Data-dependent loop avoids compiler stall; we pass trigger=1 when dispatching.
  while (cb.trigger != 0) {
    DebugOutput[int2(0x7FFFFFFF, 0x7FFFFFFF)] = float4(0.0);
  }
}
