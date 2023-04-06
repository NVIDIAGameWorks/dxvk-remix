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
#include "rtxdi/ConfidenceFunctions.slangh"
#include "rtx/pass/rtxdi/rtxdi_filter_gradients_bindings.h"

// This shader implements an A-trous spatial filter on the gradients texture.
// The filter is applied repeatedly to get a wide blur with relatively few texture samples.

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 globalIdx : SV_DispatchThreadID)
{
  if (any(globalIdx.xy >= cb.gradientImageSize))
    return;

  // The filtering step increases with each pass
  int step = 1l << cb.passIndex;
  const int inputBuffer = cb.passIndex & 1;

  RTXDI_FilterGradients(RtxdiGradients, globalIdx.xy, inputBuffer, !inputBuffer, step,
    cb.gradientImageSize, cb.hitDistanceSensitivity);
}
