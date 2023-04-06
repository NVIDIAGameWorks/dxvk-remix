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
#include "rtx/pass/post_fx/post_fx.h"

layout(binding = POST_FX_INPUT)
Sampler2D InColorSampler;

layout(binding = POST_FX_OUTPUT)
RWTexture2D<float4> OutColorTexture;

layout(push_constant)
ConstantBuffer<PostFxArgs> cb;

float3 calculateColorFringe(const uint2  pixelPos,
                            const uint2  imageSize,
                            const float2 invImageSize,
                            const float2 chromaticAberrationScale,
                            const float  chromaticCenterAttenuationAmount)
{
  // Basic center based attenuation, mitigate the chromatic aberration effect from center
  const float2 centerAttenuation = (((float2)pixelPos + 0.5f) * 2.0f - (float2)imageSize) * max(invImageSize.x, invImageSize.y);
  const float  centerAttenuationSquare = dot(centerAttenuation, centerAttenuation);
  const float  chromaticCenterAttenuation = max(0.0f, 1.0f - centerAttenuationSquare * (1.0f - chromaticCenterAttenuationAmount));

  const float2 uv = (pixelPos + 0.5f) * (float2)invImageSize;
  const float2 chromaticAberrationUvScale = 1.0f + chromaticAberrationScale * (1.0f - chromaticCenterAttenuation);

  // Simulate Lens Color Shift
  const float chromaticAberrationR = InColorSampler.SampleLevel((uv - 0.5f) / chromaticAberrationUvScale + 0.5f, 0.0f).r;
  const float chromaticAberrationG = InColorSampler.SampleLevel(uv, 0.0f).g;
  const float chromaticAberrationB = InColorSampler.SampleLevel((uv - 0.5f) * chromaticAberrationUvScale + 0.5f, 0.0f).b;

  return float3(chromaticAberrationR, chromaticAberrationG, chromaticAberrationB);
}

float calculateVignetteAttenuation(const uint2  pixelPos,
                                   const uint2  imageSize,
                                   const float2 invImageSize,
                                   const float  vignetteIntensity,
                                   const float  vignetteRadius,
                                   const float  vignetteSoftness)
{
  const float2 ndc = (((float2)pixelPos + 0.5f) * 2.0f - (float2)imageSize) * min(invImageSize.x, invImageSize.y);

  const float2 maxRadius = length((1.0f - (float2)imageSize) * invImageSize);
  const float2 factor = smoothstep(vignetteRadius.xx, maxRadius, length(ndc));
  const float2 centerAttenuation = lerp(0.0f, 1.0f - vignetteSoftness, factor);
  const float  centerAttenuationSquare = dot(centerAttenuation, centerAttenuation);

  // https://grail.cs.washington.edu/projects/vignette/vign.iccv05.pdf
  return max(0.0f, 1.0f - centerAttenuationSquare * vignetteIntensity);
}

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
  {
    return;
  }

  float3 result = 0.0f;
  // 1. Chromatic Aberration
  result = calculateColorFringe(pixelPos, cb.imageSize, cb.invImageSize, cb.chromaticAberrationScale, cb.chromaticCenterAttenuationAmount);

  // 2. Vignette
  result *= calculateVignetteAttenuation(pixelPos, cb.imageSize, cb.invImageSize, cb.vignetteIntensity, cb.vignetteRadius, cb.vignetteSoftness);

  // Write back to final display texture
  OutColorTexture[pixelPos] = float4(result, 1.0f);
}
