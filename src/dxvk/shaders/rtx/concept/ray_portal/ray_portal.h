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

#include "rtx/utility/shader_types.h"

// Todo: Potentially make these configurable by an option in the future or auto-detected. This will require the ray portal
// info to be in its own buffer though likely to not make the RaytraceArgs bigger than it needs to be.
static const uint8_t maxRayPortalPairCount = uint8_t(1);
// WARNING! When increasing maxRayPortalCount, also change invalidRayPortalIndex to something higher,
// because RTXDI gradient computation relies on there being (2*maxRayPortalCount) portals in the RaytraceArgs array,
// where the upper half comes from the previous frame.
static const uint32_t maxRayPortalCount = 2;
// Note: Ray portal index only given 3 bits for packing reasons, giving a max of 7 ray portals that can be active at once,
// or 3 pairs of 2 which should be enough for most things.
static const uint8_t invalidRayPortalIndex = uint8_t(0x7);

struct PortalTransform
{
#define PORTAL_TRANSFORM_INACTIVE_VALUE 3.402823466e+38f

  vec4 rows[3]; // stored in transposed form with translation in .w channels

#ifdef __cplusplus

  void set(const dxvk::Matrix4& m)
  {
    mat4 transposedMat = dxvk::transpose(m);

    rows[0] = transposedMat[0];
    rows[1] = transposedMat[1];
    rows[2] = transposedMat[2];
  }

  void setInactive()
  {
    rows[0].x = PORTAL_TRANSFORM_INACTIVE_VALUE;
  }

#else

  mat4 unpack()
  {
    mat4 ret;

    ret[0] = rows[0];
    ret[1] = rows[1];
    ret[2] = rows[2];
    ret[3] = vec4(0, 0, 0, 1);

    return ret;
  }

  f16mat3 unpackAsf16mat3()
  {
    f16mat3 ret;

    ret[0] = rows[0].xyz;
    ret[1] = rows[1].xyz;
    ret[2] = rows[2].xyz;

    return ret;
  }
#endif
  bool isActive() {
    return rows[0].x != PORTAL_TRANSFORM_INACTIVE_VALUE;
  }
};

struct RayPortalHitInfo
{
  PortalTransform encodedPortalToOpposingPortalDirection;

  vec3 centroid;
  uint8_t spriteSheetRows;
  uint8_t spriteSheetCols;
  uint8_t spriteSheetFPS;
  uint8_t rayPortalIndex;

  vec3 normal;
  float sampleThreshold;

  vec3 xAxis;
  float inverseHalfWidth;

  vec3 yAxis;
  float inverseHalfHeight;

  uvec3 textureTransform; // packed f16mat3x2
  uint16_t samplerIndex;
  uint16_t samplerIndex2;

  uint16_t maskTextureIndex;
  uint16_t maskTextureIndex2;
  uint16_t rotationSpeed;
  uint16_t emissiveIntensity;
  uint2 pad;
};

#ifdef __cplusplus
inline
#endif
uint8_t getOpposingRayPortalIndex(uint8_t portalIndex) {
  // Flip the least significant bit
  return portalIndex ^ uint8_t(1);
}
