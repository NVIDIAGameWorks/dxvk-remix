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

layout(r16ui, binding = POST_FX_HIGHLIGHT_OBJECT_PICKING_INPUT)
RWTexture2D<uint> ObjectPickingTexture;

layout(r16f, binding = POST_FX_HIGHLIGHT_PRIMARY_CONE_RADIUS_INPUT)
Texture2D<float> PrimaryConeRadius;

layout(binding = POST_FX_HIGHLIGHT_OUTPUT)
RWTexture2D<float4> OutColorTexture;

layout(binding = POST_FX_HIGHLIGHT_VALUES)
StructuredBuffer<uint> InValues;

layout(push_constant)
ConstantBuffer<PostFxHighlightingArgs> cb;

// size is based on makeEncodedForBilinear
groupshared uint s_objectPickingValue[1 + POST_FX_TILE_SIZE + 2][1 + POST_FX_TILE_SIZE + 2];

uint loadInput(const int2 downscaledPix)
{
  return ObjectPickingTexture[downscaledPix];
}

void preload(int2 pix)
{
  const int2 offsetInShared = pix % int2(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE);

  // center
  s_objectPickingValue[1 + offsetInShared.x][1 + offsetInShared.y] = loadInput(pix);

  // top 
  if (offsetInShared.y == 0)
  {
    s_objectPickingValue[1 + offsetInShared.x][0] = loadInput(int2(pix.x, pix.y - 1));
  }
  // bottom 
  else if (offsetInShared.y == POST_FX_TILE_SIZE - 1)
  {
    s_objectPickingValue[1 + offsetInShared.x][POST_FX_TILE_SIZE + 1] = loadInput(int2(pix.x, pix.y + 1));
  }
  // bottom 2 
  else if (offsetInShared.y == POST_FX_TILE_SIZE - 2)
  {
    s_objectPickingValue[1 + offsetInShared.x][POST_FX_TILE_SIZE + 2] = loadInput(int2(pix.x, pix.y + 2));
  }

  // left
  if (offsetInShared.x == 0)
  {
    s_objectPickingValue[0][1 + offsetInShared.y] = loadInput(int2(pix.x - 1, pix.y));
  }
  // right
  else if (offsetInShared.x == POST_FX_TILE_SIZE - 1)
  {
    s_objectPickingValue[POST_FX_TILE_SIZE + 1][1 + offsetInShared.y] = loadInput(int2(pix.x + 1, pix.y));
  }
  // right 2
  else if (offsetInShared.x == POST_FX_TILE_SIZE - 2)
  {
    s_objectPickingValue[POST_FX_TILE_SIZE + 2][1 + offsetInShared.y] = loadInput(int2(pix.x + 2, pix.y));
  }

  // top-left corner
  if (offsetInShared.x == 0 && offsetInShared.y == 0)
  {
    s_objectPickingValue[0][0] = loadInput(int2(pix.x - 1, pix.y - 1));
  }
  // top-right corner
  else if (offsetInShared.x == POST_FX_TILE_SIZE - 1 && offsetInShared.y == 0)
  {
    s_objectPickingValue[POST_FX_TILE_SIZE + 1][0] = loadInput(int2(pix.x + 1, pix.y - 1));
    s_objectPickingValue[POST_FX_TILE_SIZE + 2][0] = loadInput(int2(pix.x + 2, pix.y - 1));
  }
  // bottom-left corner
  else if (offsetInShared.x == 0 && offsetInShared.y == POST_FX_TILE_SIZE - 1)
  {
    s_objectPickingValue[0][POST_FX_TILE_SIZE + 1] = loadInput(int2(pix.x - 1, pix.y + 1));
    s_objectPickingValue[0][POST_FX_TILE_SIZE + 2] = loadInput(int2(pix.x - 1, pix.y + 2));
  }
  // bottom-right corner
  else if (offsetInShared.x == POST_FX_TILE_SIZE - 1 && offsetInShared.y == POST_FX_TILE_SIZE - 1)
  {
    s_objectPickingValue[POST_FX_TILE_SIZE + 1][POST_FX_TILE_SIZE + 1] = loadInput(int2(pix.x + 1, pix.y + 1));
    s_objectPickingValue[POST_FX_TILE_SIZE + 1][POST_FX_TILE_SIZE + 2] = loadInput(int2(pix.x + 1, pix.y + 2));
    s_objectPickingValue[POST_FX_TILE_SIZE + 2][POST_FX_TILE_SIZE + 1] = loadInput(int2(pix.x + 2, pix.y + 1));
    s_objectPickingValue[POST_FX_TILE_SIZE + 2][POST_FX_TILE_SIZE + 2] = loadInput(int2(pix.x + 2, pix.y + 2));
  }
}

uint accessObjectPickingValue(const int2 base, const int2 offset)
{
  int2 offsetInShared = base % int2(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE);

  offsetInShared += offset;
  offsetInShared = clamp(offsetInShared, int2(-1, -1), int2(POST_FX_TILE_SIZE + 2, POST_FX_TILE_SIZE + 2));

  return s_objectPickingValue[1 + offsetInShared.x][1 + offsetInShared.y];
}

// from #include "rtx/utility/gbuffer_helpers.slangh"
bool isGBufferMiss(const uint2 position, Texture2D<float> coneRadiusTexture)
{
  return coneRadiusTexture[position].x == 0.0f;
}

bool findValueInArray(uint target)
{
  if (target == POST_FX_HIGHLIGHTING_INVALID_VALUE)
  {
    return false;
  }

  // binary search with a uniform memory access pattern
  uint foundIndex = 0;

  for (int power = int(cb.valuesToHighlightCountPow) - 1; power >= 0; power--)
  {
    foundIndex += 
      (InValues[foundIndex + (1 << power)] <= target) 
        ? (1 << power)
        : 0;
  }

  return InValues[foundIndex] == target;
}

bool requiresHighlight(const int2 basePixInOutput, const int2 offsetInOutput)
{
  const int2 outputPix = basePixInOutput + offsetInOutput;
  if (isGBufferMiss(basePixInOutput + offsetInOutput, PrimaryConeRadius))
  {
    return false;
  }

  const uint valueAtPixel = accessObjectPickingValue(basePixInOutput, offsetInOutput);
  if (cb.pixel.x >= 0)
  {
    return valueAtPixel == ObjectPickingTexture[cb.pixel];
  }
  return findValueInArray(valueAtPixel);
}

f16vec3 highlightColor(uint packed)
{
  return f16vec3(
    float16_t((packed >>  0) & 0xFF) / float16_t(255),
    float16_t((packed >>  8) & 0xFF) / float16_t(255),
    float16_t((packed >> 16) & 0xFF) / float16_t(255)
  );
}

float highlightIntensity()
{
  const float ymax = 0.65;
  float t10 = 1.0 - fract(cb.timeSinceStartMS / 1000.0);
  return clamp(t10 > ymax ? t10 - (1.0 - ymax) : t10, 0.0, 1.0) / ymax;
}

uint makeEncodedForSobel(int2 basePix, bool valueAtCenter)
{
  // 3x3 pixels encoded into one 32-bit value
  uint encoded = 0;
  encoded |= requiresHighlight(basePix, int2(-1, -1)) ? (1 << 0) : 0;
  encoded |= requiresHighlight(basePix, int2(-1,  0)) ? (1 << 1) : 0;
  encoded |= requiresHighlight(basePix, int2(-1, +1)) ? (1 << 2) : 0;
  encoded |= requiresHighlight(basePix, int2( 0, -1)) ? (1 << 3) : 0;
  encoded |= valueAtCenter                            ? (1 << 4) : 0;
  encoded |= requiresHighlight(basePix, int2( 0, +1)) ? (1 << 5) : 0;
  encoded |= requiresHighlight(basePix, int2(+1, -1)) ? (1 << 6) : 0;
  encoded |= requiresHighlight(basePix, int2(+1,  0)) ? (1 << 7) : 0;
  encoded |= requiresHighlight(basePix, int2(+1, +1)) ? (1 << 8) : 0;
  return encoded;
}

// to access a value in 'encoded', x=[-1..1], y=[-1..1]
#define ACCESS_PIX( encoded, x, y ) float16_t(                \
    ((encoded) & ( 1 << ( ((x) + 1) * 3 + ((y) + 1) ) )) != 0 \
      ? 1.0                                                   \
      : 0.0 )

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
  {
    return;
  }

  preload(int2(pixelPos));
  GroupMemoryBarrierWithGroupSync();

  float4 result = InColorTexture[pixelPos];
  const bool toHighlight = requiresHighlight(pixelPos, int2(0, 0));

  // deaturate if not highlighted
  if (cb.desaturateNonHighlighted != 0)
  {
    if (!toHighlight)
    {
      float lum = calcBt709Luminance(result.rgb);
      result.rgb = float3(max(0.01, lum));
    }
  }

  // basic outline
  {
    uint encoded = makeEncodedForSobel(pixelPos, toHighlight);

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

        gradient += sobel * ACCESS_PIX(encoded, i, j);
      }
    }
    const float16_t magnitude = saturate(length(gradient));

    result.rgb = lerp(
      result.rgb,
      highlightColor(cb.highlightColorPacked),
      magnitude * highlightIntensity());
  }

  OutColorTexture[pixelPos] = result;
}
