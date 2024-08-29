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
#include "rtx/utility/color.slangh"

layout(binding = BLOOM_DOWNSAMPLE_INPUT)
Sampler2D<float4> InColorBuffer;

layout(binding = BLOOM_DOWNSAMPLE_OUTPUT)
RWTexture2D<float4> OutColorBuffer;

layout(push_constant)
ConstantBuffer<BloomDownsampleArgs> cb;

float2 calcUV(const int2 downsampledPix)
{
  return (float2(downsampledPix) + float2(0.5)) * cb.downsampledOutputSizeInverse;
}

float3 downsample13tap(const int2 downsampledPix)
{
  const float2 centerUv = calcUV(downsampledPix);

  // For reference, see SIGGRAPH 2014 https://advances.realtimerendering.com/s2014/index.html
  // 'Next Generation Post Processing in Call of Duty: Advanced Warfare' - Bloom - Downsampling
  // Weighted average of 5 overplapped blocks of 4-texel regions

  float3 taps_0  = InColorBuffer.SampleLevel(centerUv + float2(-2,-2) * cb.inputSizeInverse, 0).rgb;
  float3 taps_1  = InColorBuffer.SampleLevel(centerUv + float2( 0,-2) * cb.inputSizeInverse, 0).rgb;
  float3 taps_2  = InColorBuffer.SampleLevel(centerUv + float2( 2,-2) * cb.inputSizeInverse, 0).rgb;
  float3 taps_3  = InColorBuffer.SampleLevel(centerUv + float2(-1,-1) * cb.inputSizeInverse, 0).rgb;
  float3 taps_4  = InColorBuffer.SampleLevel(centerUv + float2( 1,-1) * cb.inputSizeInverse, 0).rgb;
  float3 taps_5  = InColorBuffer.SampleLevel(centerUv + float2(-2, 0) * cb.inputSizeInverse, 0).rgb;
  float3 taps_6  = InColorBuffer.SampleLevel(centerUv + float2( 0, 0) * cb.inputSizeInverse, 0).rgb;
  float3 taps_7  = InColorBuffer.SampleLevel(centerUv + float2( 2, 0) * cb.inputSizeInverse, 0).rgb;
  float3 taps_8  = InColorBuffer.SampleLevel(centerUv + float2(-1, 1) * cb.inputSizeInverse, 0).rgb;
  float3 taps_9  = InColorBuffer.SampleLevel(centerUv + float2( 1, 1) * cb.inputSizeInverse, 0).rgb;
  float3 taps_10 = InColorBuffer.SampleLevel(centerUv + float2(-2, 2) * cb.inputSizeInverse, 0).rgb;
  float3 taps_11 = InColorBuffer.SampleLevel(centerUv + float2( 0, 2) * cb.inputSizeInverse, 0).rgb;
  float3 taps_12 = InColorBuffer.SampleLevel(centerUv + float2( 2, 2) * cb.inputSizeInverse, 0).rgb;

  float3 a = 0.25 * (taps_3 + taps_4 + taps_8  + taps_9);
  float3 b = 0.25 * (taps_0 + taps_1 + taps_5  + taps_6);
  float3 c = 0.25 * (taps_1 + taps_2 + taps_6  + taps_7);
  float3 d = 0.25 * (taps_5 + taps_6 + taps_10 + taps_11);
  float3 e = 0.25 * (taps_6 + taps_7 + taps_11 + taps_12);

  return 0.5   * a +
         0.125 * b +
         0.125 * c +
         0.125 * d +
         0.125 * e;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 ipos : SV_DispatchThreadID)
{
  if (any(ipos >= cb.downsampledOutputSize))
  {
    return;
  }

  float3 result = downsample13tap(int2(ipos));

  if (cb.threshold > 0)
  {
    // smoothly suppress values under threshold
    result *= smoothstep(0.0, cb.threshold, calcBt709Luminance(result));
  }

  OutColorBuffer[ipos] = float4(result, 0);
}
