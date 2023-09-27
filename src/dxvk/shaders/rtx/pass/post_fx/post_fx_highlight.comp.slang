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

#include "rtx/pass/post_fx/post_fx.h"
#include "rtx/utility/shader_types.slangh"
#include "rtx/utility/color.slangh"

layout(binding = POST_FX_HIGHLIGHT_INPUT)
Texture2D<float4> InColorTexture;

layout(r16ui, binding = POST_FX_HIGHLIGHT_SHARED_SURFACE_INDEX_INPUT)
Texture2D<uint> SharedSurfaceIndex;

layout(r16f, binding = POST_FX_HIGHLIGHT_PRIMARY_CONE_RADIUS_INPUT)
Texture2D<float> PrimaryConeRadius;

layout(binding = POST_FX_HIGHLIGHT_OUTPUT)
RWTexture2D<float4> OutColorTexture;

layout(push_constant)
ConstantBuffer<PostFxHighlightingArgs> cb;

uint2 toRaytracePixel(const uint2 pixelPos)
{
  return uint2((float2(pixelPos) + 0.5f) * cb.inputOverOutputViewSize);
}

// from #include "rtx/utility/gbuffer_helpers.slangh"
bool isGBufferMiss(const uint2 position, Texture2D<float> coneRadiusTexture)
{
  return coneRadiusTexture[position].x == 0.0f;
}

bool requiresHighlight(const uint2 pixelPos)
{
  const uint2 rtpixel = toRaytracePixel(pixelPos);

  if (isGBufferMiss(rtpixel, PrimaryConeRadius))
  {
    return false;
  }

  const uint surfaceMaterialIndex = SharedSurfaceIndex[rtpixel];
  return surfaceMaterialIndex == cb.surfaceMaterialIndexToHighlight;
}

f16vec3 highlightColor(uint highlightColorId)
{
  switch(highlightColorId)
  {
    case 1: return f16vec3(118, 185, 0) / float16_t(255.0);
    case 2: return f16vec3(66, 150, 250) / float16_t(255.0);
    default: return f16vec3(0.0);
  }
}

float highlightIntensity()
{
  const float ymax = 0.65;
  float t10 = 1.0 - fract(cb.timeSinceStartMS / 1000.0);
  return clamp(t10 > ymax ? t10 - (1.0 - ymax) : t10, 0.0, 1.0) / ymax;
}

uint makeEncodedForBilinear(int2 basePix, bool valueAtCenter)
{
  // 4x4 pixels encoded into one 32-bit value
  uint encoded = 0;
  encoded |= requiresHighlight(basePix + int2(-1, -1)) ? (1 <<  0) : 0;
  encoded |= requiresHighlight(basePix + int2(-1,  0)) ? (1 <<  1) : 0;
  encoded |= requiresHighlight(basePix + int2(-1, +1)) ? (1 <<  2) : 0;
  encoded |= requiresHighlight(basePix + int2(-1, +2)) ? (1 <<  3) : 0;
  encoded |= requiresHighlight(basePix + int2( 0, -1)) ? (1 <<  4) : 0;
  encoded |= valueAtCenter                             ? (1 <<  5) : 0;
  encoded |= requiresHighlight(basePix + int2( 0, +1)) ? (1 <<  6) : 0;
  encoded |= requiresHighlight(basePix + int2( 0, +2)) ? (1 <<  7) : 0;
  encoded |= requiresHighlight(basePix + int2(+1, -1)) ? (1 <<  8) : 0;
  encoded |= requiresHighlight(basePix + int2(+1,  0)) ? (1 <<  9) : 0;
  encoded |= requiresHighlight(basePix + int2(+1, +1)) ? (1 << 10) : 0;
  encoded |= requiresHighlight(basePix + int2(+1, +2)) ? (1 << 11) : 0;
  encoded |= requiresHighlight(basePix + int2(+2, -1)) ? (1 << 12) : 0;
  encoded |= requiresHighlight(basePix + int2(+2,  0)) ? (1 << 13) : 0;
  encoded |= requiresHighlight(basePix + int2(+2, +1)) ? (1 << 14) : 0;
  encoded |= requiresHighlight(basePix + int2(+2, +2)) ? (1 << 15) : 0;
  return encoded;
}

// to access a value in 'encoded', x=[-1..2], y=[-1..2]
#define ACCESS_PIX( encoded, x, y ) float16_t(                \
    ((encoded) & ( 1 << ( ((x) + 1) * 4 + ((y) + 1) ) )) != 0 \
      ? 1.0                                                   \
      : 0.0 )

// x=[-1..1], y=[-1..1]
float16_t accessBilinearly(const uint encoded, int16_t x, int16_t y)
{
    float16_t value00 = ACCESS_PIX(encoded, x    , y    );
    float16_t value10 = ACCESS_PIX(encoded, x + 1, y    );
    float16_t value01 = ACCESS_PIX(encoded, x    , y + 1);
    float16_t value11 = ACCESS_PIX(encoded, x + 1, y + 1);

    return mix(
      mix(value00, value10, float16_t(0.5)), 
      mix(value01, value11, float16_t(0.5)), 
      float16_t(0.5));
}

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
  {
    return;
  }

  float4 result = InColorTexture[pixelPos];
  const bool toHighlight = requiresHighlight(pixelPos);

  // deaturate if not highlighted
  if (cb.desaturateNonHighlighted)
  {
    if (!toHighlight)
    {
      float lum = calcBt709Luminance(result.rgb);
      result.rgb = float3(lum + 0.1);
    }
  }

  // basic outline
  {
    uint encoded = makeEncodedForBilinear(pixelPos, toHighlight);

    f16vec2 gradient = f16vec2(0.0);
    for (int16_t i = -1; i <= 1; i++)
    {
      for (int16_t j = -1; j <= 1; j++)
      {
        // sobel x, y kernels + normalization
        // (-1,0,+1)    (-1,-2,-1)
        // (-2,0,+2)    ( 0, 0, 0)
        // (-1,0,+1)    (+1,+2,+1)
        const f16vec2 sobel = f16vec2(
          float16_t(j) * (i == 0 ? float16_t(2.0) : float16_t(1.0)),          
          float16_t(i) * (j == 0 ? float16_t(2.0) : float16_t(1.0))          
        );

        gradient += sobel * accessBilinearly(encoded, i, j);
      }
    }
    const float16_t magnitude = saturate(length(gradient));

    result.rgb = lerp(
      result.rgb,
      highlightColor(cb.highlightColorId),
      magnitude * highlightIntensity());
  }

  OutColorTexture[pixelPos] = result;
}
