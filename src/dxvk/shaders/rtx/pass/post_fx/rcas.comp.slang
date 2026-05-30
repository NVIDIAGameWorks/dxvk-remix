/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/post_fx/rcas.h"

layout(binding = RCAS_INPUT)
Sampler2D<float4> InColorSampler;

layout(binding = RCAS_OUTPUT)
RWTexture2D<float4> OutColorTexture;

layout(push_constant)
ConstantBuffer<RcasArgs> cb;

float computeLuma(float3 c) {
  return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float4 sampleColorClamped(float2 uv, float2 invSize) {
  // Keep all taps inside valid texel centers regardless of external sampler address mode.
  const float2 uvMin = 0.5f * invSize;
  const float2 uvMax = 1.0f - uvMin;
  return InColorSampler.SampleLevel(clamp(uv, uvMin, uvMax), 0.0f);
}

[shader("compute")]
[numthreads(RCAS_TILE_SIZE, RCAS_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID) {
  if (any(pixelPos >= cb.imageSize)) {
    return;
  }

  const float2 uv = (float2(pixelPos) + 0.5f) * cb.invImageSize;
  const float2 step = cb.invImageSize;

  const float4 c = sampleColorClamped(uv, cb.invImageSize);
  const float3 n = sampleColorClamped(uv + float2(0.0f, -step.y), cb.invImageSize).rgb;
  const float3 s = sampleColorClamped(uv + float2(0.0f, step.y), cb.invImageSize).rgb;
  const float3 w = sampleColorClamped(uv + float2(-step.x, 0.0f), cb.invImageSize).rgb;
  const float3 e = sampleColorClamped(uv + float2(step.x, 0.0f), cb.invImageSize).rgb;
  const float3 nw = sampleColorClamped(uv + float2(-step.x, -step.y), cb.invImageSize).rgb;
  const float3 ne = sampleColorClamped(uv + float2(step.x, -step.y), cb.invImageSize).rgb;
  const float3 sw = sampleColorClamped(uv + float2(-step.x, step.y), cb.invImageSize).rgb;
  const float3 se = sampleColorClamped(uv + float2(step.x, step.y), cb.invImageSize).rgb;

  // Weighted 3x3 blur stabilizes sharpening across diverse upscaler outputs.
  const float3 blur = (4.0f * c.rgb + 2.0f * (n + s + w + e) + (nw + ne + sw + se)) * (1.0f / 16.0f);
  float3 minNeighbor = min(min(min(n, s), min(w, e)), min(min(nw, ne), min(sw, se)));
  float3 maxNeighbor = max(max(max(n, s), max(w, e)), max(max(nw, ne), max(sw, se)));
  minNeighbor = min(minNeighbor, c.rgb);
  maxNeighbor = max(maxNeighbor, c.rgb);

  const float lumaC = computeLuma(c.rgb);
  const float lumaMin = min(min(min(computeLuma(n), computeLuma(s)), min(computeLuma(w), computeLuma(e))),
                            min(min(computeLuma(nw), computeLuma(ne)), min(computeLuma(sw), computeLuma(se))));
  const float lumaMax = max(max(max(computeLuma(n), computeLuma(s)), max(computeLuma(w), computeLuma(e))),
                            max(max(computeLuma(nw), computeLuma(ne)), max(computeLuma(sw), computeLuma(se))));
  const float localContrast = lumaMax - lumaMin;

  // Suppress sharpening on strong edges to avoid halos/ringing, but keep a minimum
  // contribution so the pass remains visible across different upscaler outputs.
  const float edgeSuppression = max(0.35f, 1.0f - smoothstep(0.12f, 0.55f, localContrast));

  // Keep detail enhancement in low/mid-contrast regions.
  const float detail = abs(lumaC - computeLuma(blur));
  const float detailAdapt = saturate(detail / (lumaC + 1e-4f));
  const float strength = cb.sharpness * edgeSuppression * (0.30f + 0.70f * detailAdapt);

  float3 sharpened = c.rgb + strength * (c.rgb - blur);

  // Soft anti-ringing clamp with slight range expansion to avoid texture crunch.
  const float3 range = maxNeighbor - minNeighbor;
  const float3 expand = max(float3(0.01f, 0.01f, 0.01f), 0.125f * range);
  sharpened = clamp(sharpened, minNeighbor - expand, maxNeighbor + expand);

  OutColorTexture[pixelPos] = float4(sharpened, c.a);
}
