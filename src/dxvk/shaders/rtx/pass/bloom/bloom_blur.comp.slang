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

layout(binding = BLOOM_BLUR_INPUT)
Sampler2D InColorBuffer;

layout(binding = BLOOM_BLUR_OUTPUT)
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<BloomBlurArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  if (any(ipos >= cb.imageSize)) {
    OutColorBuffer[ipos] = float4(0);
    return;
  }

  float4 result = InColorBuffer[ipos];

  for(float x = 1; x < cb.numSamples; x += 2)
  {
    float w1 = exp(pow(x, 2) * cb.argumentScale);
    float w2 = exp(pow(x + 1.0, 2) * cb.argumentScale);

    float w12 = w1 + w2;
    float p = w2 / w12;
    float2 offset = cb.pixstep * (x + p);

    float2 pos1 = clamp(float2(ipos) + float2(0.5) + offset, float2(0.0), float2(cb.imageSize) - float2(0.5));
    float2 pos2 = clamp(float2(ipos) + float2(0.5) - offset, float2(0.0), float2(cb.imageSize) - float2(0.5));

    float2 uv1 = pos1 * cb.invImageSize;
    float2 uv2 = pos2 * cb.invImageSize;

    float4 sample1 = InColorBuffer.SampleLevel(uv1, 0);
    float4 sample2 = InColorBuffer.SampleLevel(uv2, 0);

    result += (sample1 + sample2) * w12;
  }

  result *= cb.normalizationScale;

  OutColorBuffer[ipos] = result;
}
