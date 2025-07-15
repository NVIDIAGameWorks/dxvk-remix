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

// Disable Slang warning about deprecation of inheritance in favor of composition
// struct Base { int a; };
// struct Inherited : public Base { int b; }; <- no more this
// struct Composed { Base base; int b; }; <- only this
#pragma warning(disable:30816)

#include "surface_shared.h"
#include "rtx/utility/packing.slangh"

struct Surface
{
  // Note: Currently aligned nicely to 240 bytes, avoid changing the size of this structure (as it will require
  // more 128 byte cachelines to be brought in for a single Surface read).
  u16vec4 data0a;
  u16vec4 data0b;
  uint4 data1;
  uint4 data2;
  uint4 data3;
  uint4 data4;
  uint4 data5;
  uint4 data6;
  uint4 data7;
  uint4 data8;
  uint4 data9;
  uint4 data10;
  uint4 data11;
  uint4 data12;
  uint4 data13;
  uint4 data14;

  // flags and properties

  // Note: Potentially temporary flag for "fullbright" rendered things (e.g. the skybox) which should appear emissive-like.
  // This may be able to be determined by some sort of fixed function state in the future, but for now this flag can be used.
  property bool isEmissive
  {
    get { return packedFlagGet(data2.w, 1 << 0); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 0) : packedFlagUnset(data2.w, 1 << 0); }
  }

  // Note: Indicates that there are no opacity-related blend modes (or translucency) on the associated Surface Material.
  // This allows for optimizations in hit logic by being able to early out before expensive material decoding is done.
  property bool isFullyOpaque
  {
    get { return packedFlagGet(data2.w, 1 << 1); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 1) : packedFlagUnset(data2.w, 1 << 1); }
  }

  property bool isStatic
  {
    get { return packedFlagGet(data2.w, 1 << 2); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 2) : packedFlagUnset(data2.w, 1 << 2); }
  }

  property bool invertedBlend
  {
    get { return packedFlagGet(data2.w, 1 << 18); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 18) : packedFlagUnset(data2.w, 1 << 18);  }
  }

  property bool isBlendingDisabled
  {
    get { return packedFlagGet(data2.w, 1 << 19); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 19) : packedFlagUnset(data2.w, 1 << 19); }
  }

  // Note: Not to be confused with isEmissive, this flag indicates that an emissive blend mode is in use. This can be
  // calculated on the GPU if needed but since space is currently available in the MemorySurface struct it is fine to precompute.
  property bool isEmissiveBlend
  {
    get { return packedFlagGet(data2.w, 1 << 20); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 20) : packedFlagUnset(data2.w, 1 << 20);  }
  }

  property bool isParticle
  {
    get { return packedFlagGet(data2.w, 1 << 21); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 21)  : packedFlagUnset(data2.w, 1 << 21); }
  }

  property bool isDecal
  {
    get { return packedFlagGet(data2.w, 1 << 22); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 22) : packedFlagUnset(data2.w, 1 << 22); }
  }

  property bool hasMaterialChanged
  {
    get { return packedFlagGet(data2.w, 1 << 23); }
    set { data2.w = newValue ? packedFlagSet(data2.w,1 << 23) : packedFlagUnset(data2.w, 1 << 23); }
  }

  property bool isAnimatedWater
  {
    get { return packedFlagGet(data2.w, 1 << 24); }
    set { data2.w = newValue ? packedFlagSet(data2.w,1 << 24) : packedFlagUnset(data2.w, 1 << 24); }
  }

  property bool isClipPlaneEnabled
  {
    get { return packedFlagGet(data2.w, 1 << 25); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 25) : packedFlagUnset(data2.w, 1 << 25); }
  }

  property bool isMatte
  {
    get { return packedFlagGet(data2.w, 1 << 26); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 26) : packedFlagUnset(data2.w, 1 << 26); }
  }

  property bool isTextureFactorBlend
  {
    get { return packedFlagGet(data2.w, 1 << 27); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 27) : packedFlagUnset(data2.w, 1 << 27); }
  }

  property bool isMotionBlurMaskOut
  {
    get { return packedFlagGet(data2.w, 1 << 28); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 28) : packedFlagUnset(data2.w, 1 << 28); }
  }

  // Note: A flag to indicate that spritesheet adjustment shouldn't be done in the Surface Interaction, typically
  // because it will be done elsewhere for another reason (e.g. for the Ray Portal case where it is done in the Surface
  // Material Interaction instead).
  property bool skipSurfaceInteractionSpritesheetAdjustment
  {
    get { return packedFlagGet(data2.w, 1 << 29); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 29) : packedFlagUnset(data2.w, 1 << 29); }
  }

  property bool ignoreTransparencyLayer
  {
    get { return packedFlagGet(data2.w, 1 << 30); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 30) : packedFlagUnset(data2.w, 1 << 30); }
  }

  property bool isInsideFrustum
  {
    get { return packedFlagGet(data2.w, 1 << 31); }
    set { data2.w = newValue ? packedFlagSet(data2.w, 1 << 31) : packedFlagUnset(data2.w, 1 << 31); }
  }

  // Matrices

  property mat4x3 prevObjectToWorld
  {
    get 
    {
      const vec4 m0 = asfloat(data3);
      const vec4 m1 = asfloat(data4);
      const vec4 m2 = asfloat(data5);
      return transpose(mat3x4(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2.x),
        m2.yzw));
    }
    set 
    {
      mat3x4 transposed = transpose(newValue);
      data3 = asuint(vec4(transposed[0], transposed[1].x));
      data4 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data5 = asuint(vec4(transposed[2].z, transposed[3]));
    }
  }

  property mat3x3 normalObjectToWorld
  {
    get
    {
      const  vec4 m0 = asfloat(data6);
      const  vec4 m1 = asfloat(data7);
      const float m2 = asfloat(data13.w);
      return transpose(mat3x3(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2)));
    }
    set
    {
      mat3x3 transposed = transpose(newValue);
      data6 = asuint(vec4(transposed[0], transposed[1].x));
      data7 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data13.w = asuint(transposed[2].z);
    }
  }

  property mat4x3 objectToWorld
  {
    get 
    {
      const vec4 m0 = asfloat(data8);
      const vec4 m1 = asfloat(data9);
      const vec4 m2 = asfloat(data10);
      return transpose(mat3x4(
        m0.xyz,
        vec3(m0.w, m1.xy),
        vec3(m1.zw, m2.x),
        m2.yzw));
    }
    set 
    {
      mat3x4 transposed = transpose(newValue);
      data8 = asuint(vec4(transposed[0], transposed[1].x));
      data9 = asuint(vec4(transposed[1].yz, transposed[2].xy));
      data10 = asuint(vec4(transposed[2].z, transposed[3]));
    }
  }

  // Note: This is only a 4x2 matrix as currently our texture transform implementation only supports <= 2 elements, so the 3rd and 4th
  // elements this matrix may generate are currently never used (nor is the perspective division when projection is enabled, though this may
  // justify increasing this to a 4x3 matrix in the future for things projecting 3D coordinates down to 2D coordinates as this should be doable
  // to support).
  property mat4x2 textureTransform
  {
    get 
    {
      const vec4 m0 = asfloat(data11);
      const vec4 m1 = asfloat(data12);
      return mat4x2(m0, m1);
    }
    set 
    {
      data11 = asuint(newValue[0]);
      data12 = asuint(newValue[1]);
    }
  }

  // Buffers

  property uint16_t positionBufferIndex
  {
    get { return data0a.x; }
    set { data0a.x = newValue; }
  }

  property uint16_t previousPositionBufferIndex
  {
    get { return data0a.y; }
    set { data0a.y = newValue; }
  }

  property uint16_t normalBufferIndex
  {
    get { return data0a.z; }
    set { data0a.z = newValue; }
  }

  property uint16_t texcoordBufferIndex
  {
    get { return data0a.w; }
    set { data0a.w = newValue; }
  }

  property uint16_t indexBufferIndex
  {
    get { return data0b.x; }
    set { data0b.x = newValue; }
  }

  property uint16_t color0BufferIndex
  {
    get { return data0b.y; }
    set { data0b.y = newValue; }
  }

  property bool normalsEncoded
  {
    get { return (data0b.z & 1) != 0; }
    // Todo
    set { data0b.z = 0; }
  }

  property uint16_t hashPacked
  {
    get { return data0b.w; }
    set { data0b.w = newValue; }
  }

  property uint positionOffset
  {
    get { return data1.x; }
    set { data1.x = newValue; }
  }

  property uint normalOffset
  {
    get { return data1.y; }
    set { data1.y = newValue; }
  }

  property uint texcoordOffset
  {
    get { return data1.z; }
    set { data1.z = newValue; }
  }

  property uint color0Offset
  {
    get { return data1.w; }
    set { data1.w = newValue; }
  }

  // Note: Position stride between current and previous position buffer
  property uint8_t positionStride
  {
    get { return (uint8_t)(data2.y & 0xFF); }
    set { data2.y = (data2.y & 0xFFFFFF00) | uint32_t(newValue & 0xFF); }
  }

  property uint8_t normalStride
  {
    get { return (uint8_t) (data2.y >> 8) & 0xFF; }
    set { data2.y = (data2.y & 0xFFFF00FF) | (uint32_t(newValue & 0xFF) << 8); }
  }

  property uint8_t texcoordStride
  {
    get { return (uint8_t) (data2.y >> 16) & 0xFF; }
    set { data2.y = (data2.y & 0xFF00FFFF) | (uint32_t(newValue & 0xFF) << 16); }
  }

  property uint8_t color0Stride
  {
    get { return (uint8_t) (data2.y >> 24) & 0xFF; }
    set { data2.y = (data2.y & 0x00FFFFFF) | (uint32_t(newValue & 0xFF) << 24); }
  }

  property uint firstIndex
  {
    get { return (data2.z & 0xFFFFFF); }
    set { data2.z = (data2.z & 0xFF000000) | (newValue & 0xFFFFFF); }
  }

  property uint8_t indexStride
  {
    get { return (uint8_t) (data2.z >> 24) & 0xFF; }
    set { data2.z = (data2.z & 0x00FFFFFF) | (uint32_t(newValue & 0xFF) << 24); }
  }

  // Sprite sheets

  property uint8_t spriteSheetRows
  {
    get { return uint8_t(data13.x & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 0)) | uint32_t(newValue & 0xFF) << 0; }
  }

  property uint8_t spriteSheetCols
  {
    get { return uint8_t((data13.x >> 8) & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 8)) | uint32_t(newValue & 0xFF) << 8; }
  }

  property uint8_t spriteSheetFPS
  {
    get { return uint8_t((data13.x >> 16) & 0xFF); }
    set { data13.x = (data13.x & ~(0xFF << 16)) | uint32_t(newValue & 0xFF) << 16; }
  }

  // Fixed function

  property uint8_t alphaTestType
  {
    get { return uint8_t((data2.w >> 3) & alphaTestTypeMask); }
    set { data2.w = (data2.w & ~(alphaTestTypeMask << 3)) | ((newValue & alphaTestTypeMask) << 3); }
  }

  property float16_t alphaTestReferenceValue
  {
    get { return float16_t((uint8_t) ((data2.w >> 6) & 0xFFu)) / float16_t(0xFFu); }
    set
    {
      uint8_t v = uint8_t(float(newValue) * 0xFF);
      data2.w = (data2.w & ~(0xFF << 6)) | (v << 6); 
    }
  }

  property uint8_t blendType
  {
    get { return uint8_t((data2.w >> 14) & surfaceBlendTypeMask); }
    set { data2.w = (data2.w & ~(surfaceBlendTypeMask << 14)) | (uint32_t(newValue & surfaceBlendTypeMask) << 14); }
  }

  property uint8_t textureColorArg1Source
  {
    get { return uint8_t(data13.z & 0x3); }
    set { data13.z = (data13.z & ~0x3) | uint32_t(newValue & 0x3); }
  }

  property uint8_t textureColorArg2Source
  {
    get { return uint8_t((data13.z >> 2) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 2)) | (uint32_t(newValue & 0x3) << 2); }
  }

  property uint8_t textureColorOperation
  {
    get { return uint8_t((data13.z >> 4) & 0x7); }
    set { data13.z = (data13.z & ~(0x7 << 4)) | (uint32_t(newValue & 0x7) << 4); }
  }

  property uint8_t textureAlphaArg1Source
  {
    get { return uint8_t((data13.z >> 7) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 7)) | (uint32_t(newValue & 0x3) << 7); }
  }

  property uint8_t textureAlphaArg2Source
  {
    get { return uint8_t((data13.z >> 9) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 9)) | (uint32_t(newValue & 0x3) << 9); }
  }

  property uint8_t textureAlphaOperation
  {
    get { return uint8_t((data13.z >> 11) & 0x7); }
    set { data13.z = (data13.z & ~(0x7 << 11)) | (uint32_t(newValue & 0x7) << 11); }
  }

  property uint8_t texcoordGenerationMode
  {
    get { return uint8_t((data13.z >> 17) & 0x3); }
    set { data13.z = (data13.z & ~(0x3 << 17)) | (uint32_t(newValue & 0x3) << 17); }
  }

  property uint tFactor
  {
    get { return data13.y; }
    set { data13.y = newValue; }
  }

  // Misc

  property vec4 clipPlane
  {
    get { return asfloat(data14); }
    set { data14 = asuint(newValue); }
  }

  property uint8_t decalSortOrder
  {
    get { return uint8_t((data13.x >> 24) & 0xFF); }
    set { data13.x = (data13.x & 0x00FFFFFF) | (uint32_t(newValue & 0xFF) << 24); }
  }

  property uint objectPickingValue
  {
    get { return data2.x; }
    set { data2.x = newValue; }
  }
};

// Note: Minimal version of typical Surface Interaction for transmission across passes.
struct MinimalSurfaceInteraction
{
  vec3 position = 0..xxx;
  // Floating-point error of position representation in object space or world space, whichever is larger.
  // Used for calculating ray offsets.
  float positionError = 0.f;
  // TODO this could just be a `quaternion triangleTBN` 
  f16vec3 triangleNormal = 0.h;
  f16vec3 triangleTangent = 0.h;
  f16vec3 triangleBitangent = 0.h;

  // Surfaces created from gbuffer may not be valid (i.e. if this pixel was a ray miss)
  property bool isValid
  {
    get { return all(!isinf(position)); }
  }
};

struct SurfaceInteraction : MinimalSurfaceInteraction
{
  vec3 motion = 0..xxx;
  vec2 textureCoordinates = 0..xx;
  vec2 textureGradientX = 0..xx;
  vec2 textureGradientY = 0..xx;
  // Note: All normal, tangent and bitangent vectors are in world space.
  // TODO this could just be a `quaternion interpolatedTBN` 
  f16vec3 interpolatedNormal = 0.h;
  f16vec3 interpolatedTangent = 0.h;
  f16vec3 interpolatedBitangent = 0.h;
  f16vec3 rawTangent = 0.h;
  f16vec3 rawBitangent = 0.h;
  f16vec4 vertexColor = 0.h;
  float triangleArea = 0.f;
};

struct GBufferMemoryMinimalSurfaceInteraction
{
  vec4 encodedWorldPositionWorldTriangleTBN;
  float positionError;
};
