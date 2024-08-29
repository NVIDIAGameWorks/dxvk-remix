/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

layout(binding = BLOOM_UPSAMPLE_INPUT)
Sampler2D InColorBuffer;

layout(binding = BLOOM_UPSAMPLE_OUTPUT)
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<BloomUpsampleArgs> cb;

float2 calcUV(const uint2 upsampledPix)
{
  return (float2(upsampledPix) + float2(0.5)) * cb.upsampledOutputSizeInverse;
}

float3 upsample(const uint2 upsampledPix)
{
  const float2 centerUV = calcUV(upsampledPix);
  float3 result = float3(0);
  
  // 3x3 Gaussian blur kernel
  result += 1.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2(-1,-1) * cb.inputSizeInverse, 0).rgb;
  result += 2.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 0,-1) * cb.inputSizeInverse, 0).rgb;
  result += 1.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 1,-1) * cb.inputSizeInverse, 0).rgb;
  result += 2.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2(-1, 0) * cb.inputSizeInverse, 0).rgb;
  result += 4.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 0, 0) * cb.inputSizeInverse, 0).rgb;
  result += 2.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 1, 0) * cb.inputSizeInverse, 0).rgb;
  result += 1.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2(-1, 1) * cb.inputSizeInverse, 0).rgb;
  result += 2.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 0, 1) * cb.inputSizeInverse, 0).rgb;
  result += 1.0 / 16.0 * InColorBuffer.SampleLevel(centerUV + float2( 1, 1) * cb.inputSizeInverse, 0).rgb;

  return result;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  if (any(ipos >= cb.upsampledOutputSize))
  {
    return;
  }

  // add, do not overwrite
  OutColorBuffer[ipos] = OutColorBuffer[ipos] + float4(upsample(ipos), 0);
}
