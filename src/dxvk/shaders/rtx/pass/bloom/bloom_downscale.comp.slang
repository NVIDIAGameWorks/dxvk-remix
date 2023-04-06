/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/bloom/bloom.h"

layout(binding = BLOOM_DOWNSCALE_INPUT)
Texture2D<float4> InColorBuffer;

layout(binding = BLOOM_DOWNSCALE_OUTPUT)
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<BloomDownscaleArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  float4 result = float4(0);
  int count = 0;

  [unroll]
  for (int yy = 0; yy < 4; yy++)
  {
    [unroll]
    for (int xx = 0; xx < 4; xx++)
    {
      int2 samplePos = ipos * 4 + int2(xx, yy);

      if (all(samplePos < cb.inputSize)) {
        result += InColorBuffer[samplePos];
        count += 1;
      }
    }
  }

  if (count > 0)
    result /= float(count);

  OutColorBuffer[ipos] = result;
}
