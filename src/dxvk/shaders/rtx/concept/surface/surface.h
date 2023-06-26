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
#pragma once

#include "surface_shared.h"

struct MemorySurface
{
  uvec4 data0;
  uvec4 data1;
  uvec4 data2;
  uvec4 data3;
  uvec4 data4;
  uvec4 data5;
  uvec4 data6;
  uvec4 data7;
  uvec4 data8;
  uvec4 data9;
  uvec4 data10;
  uvec4 data11;
  uvec4 data12;
  uvec4 data13;
  uvec4 data14;
  uvec4 data15;
};

struct Surface
{
  uint16_t positionBufferIndex;
  uint16_t previousPositionBufferIndex;
  uint positionOffset;
  // Note: Position stride between current and previous position buffer
  uint8_t positionStride;

  uint16_t normalBufferIndex;
  uint normalOffset;
  uint8_t normalStride;

  uint16_t texcoordBufferIndex;
  uint texcoordOffset;
  uint8_t texcoordStride;

  uint16_t indexBufferIndex;
  uint firstIndex;
  uint8_t indexStride;

  uint16_t color0BufferIndex;
  uint color0Offset;
  uint8_t color0Stride;

  uint16_t surfaceMaterialIndex;

  // Note: Potentially temporary flag for "fullbright" rendered things (e.g. the skybox) which should appear emissive-like.
  // This may be able to be determined by some sort of fixed function state in the future, but for now this flag can be used.
  bool isEmissive;
  // Note: Indicates that there are no opacity-related blend modes (or translucency) on the associated Surface Material.
  // This allows for optimizations in hit logic by being able to early out before expensive material decoding is done.
  bool isFullyOpaque;
  bool isStatic;
  uint8_t alphaTestType;
  float16_t alphaTestReferenceValue;
  uint8_t blendType;
  bool invertedBlend;
  uint8_t textureColorArg1Source;
  uint8_t textureColorArg2Source;
  uint8_t textureColorOperation;
  uint8_t textureAlphaArg1Source;
  uint8_t textureAlphaArg2Source;
  uint8_t textureAlphaOperation;
  uint8_t texcoordGenerationMode;
  uint16_t hashPacked;
  uint32_t tFactor;
  bool isBlendingDisabled;
  // Note: Not to be confused with isEmissive, this flag indicates that an emissive blend mode is in use. This can be
  // calculated on the GPU if needed but since space is currently available in the MemorySurface struct it is fine to precompute.
  bool isEmissiveBlend;
  bool isParticle;
  bool isDecal;
  bool isBlendedTerrain;
  bool isAnimatedWater;
  bool isClipPlaneEnabled;
  bool isMatte;
  bool isTextureFactorBlend;
  bool isMotionBlurMaskOut;

  mat4x3 objectToWorld;
  mat4x3 prevObjectToWorld;
  mat3x3 normalObjectToWorld;
  float4x4 textureTransform;

  vec4 clipPlane;
};

// Note: Minimal version of typical Surface Interaction for transmission across passes.
struct MinimalSurfaceInteraction
{
  vec3 position;
  // Floating-point error of position representation in object space or world space, whichever is larger.
  // Used for calculating ray offsets.
  float positionError;
  f16vec3 triangleNormal;
};

struct SurfaceInteraction : MinimalSurfaceInteraction
{
  vec3 motion;
  vec2 textureCoordinates;
  vec2 textureGradientX;
  vec2 textureGradientY;
  // Note: All normal, tangent and bitangent vectors are in world space.
  f16vec3 interpolatedNormal;
  f16vec3 interpolatedTangent;
  f16vec3 interpolatedBitangent;
  f16vec4 vertexColor;
  float triangleArea;
};

struct GBufferMemoryMinimalSurfaceInteraction
{
  vec4 encodedWorldPositionWorldTriangleNormal;
  float positionError;
};
