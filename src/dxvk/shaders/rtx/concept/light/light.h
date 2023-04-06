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
#ifndef LIGHT_H
#define LIGHT_H

#include "light_types.h"

// Note: Shaping parameters to generally match Omniverse's light model
struct LightShaping
{
  bool enabled;

  f16vec3 primaryAxis;
  // Using OneMinus to get the improved float precision near 0.  With this, the smallest angle we support is around 0.25 degrees.
  float16_t oneMinusCosConeAngle;
  float16_t coneSoftness;
  float16_t focusExponent;
};

struct MemoryPolymorphicLight
{
  // Note: Currently aligned nicely to 64 bytes, avoid changing the size of this structure. Note however since this is smaller
  // than a L1 cacheline the actual size doesn't matter as much, so it is not packed and is in fact "wasteful" with some of what
  // is being loaded (precomputed values to save work) as the cache hitrate will be low and the random access nature does not
  // facilitate much memory coalescing. Most scenes will not have too many lights either so L2 caching is not as big a concern
  // either, plus L2 can only fetch in 32 byte increments so using extra space if the struct is bigger than a 32 byte boundary
  // is not a problem.

  uvec4 data0;
  uvec4 data1;
  uvec4 data2;
  uvec4 data3;
};

struct DecodedPolymorphicLight
{
  uint8_t polymorphicType;
  vec3 radiance;
  LightShaping shaping;
  uvec4 data0;
  uvec4 data2;
  uvec4 data3;
};

struct SphereLight
{
  vec3 position;
  float16_t radius; // Note: Assumed to be >0 always to avoid point light case
  vec3 radiance;
  LightShaping shaping;
};

struct RectLight
{
  vec3 position;
  f16vec2 dimensions; // Note: Assumed to be >0 always to avoid point light case
  f16vec3 xAxis;
  f16vec3 yAxis;
  // Note: Precomputed normal stored for less runtime computation, remove if packing ever should be more intense
  // (and derive from cross product of axes).
  f16vec3 normal;
  vec3 radiance;
  LightShaping shaping;
};

struct DiskLight
{
  vec3 position;
  f16vec2 halfDimensions; // Note: Assumed to be >0 always to avoid point light case
  f16vec3 xAxis;
  f16vec3 yAxis;
  // Note: Precomputed normal stored for less runtime computation, remove if packing ever should be more intense
  // (and derive from cross product of axes).
  f16vec3 normal;
  vec3 radiance;
  LightShaping shaping;
};

struct CylinderLight
{
  vec3 position;
  float16_t radius; // Note: Assumed to be >0 always to avoid line light case
  f16vec3 axis;
  float16_t axisLength; // Note: Assumed to be >0 always to avoid ring light case
  vec3 radiance;
  // Note: No shaping as it has little reasonable meaning for a Cylinder light (Omniverse has it though, but it doesn't work)
};

struct DistantLight
{
  f16vec3 direction;
  // Note: Precomputed orientation stored for less runtime computation, remove if packing ever should be more intense
  // (and derive from direction, or perhaps derive direction from the quaternion itself).
  f16vec4 orientation;
  // Note: Both cos/sin are stored instead of a single angle as Distant lights are not paticularly heavy
  // on their memory budget and can afford this for free essentially. Additionally these are stored as
  // 32 bit floats to avoid precision issues when cosine is near 1 (as a 16 bit float is not precise enough to store
  // such values when the half angle is small).
  float cosHalfAngle; // Note: Assumed to be != 1 to avoid delta light case
  float sinHalfAngle;
  vec3 radiance;
  // Note: No shaping as it has little reasonable meaning for a Distant light
};

struct LightInteraction
{
  // Note: This position may be different than the position from the geometry ray interaction used to construct
  // this light interaction from a hit, hence why it has to be calculated and stored here whereas the surface
  // interaction for example does not as it would be redundant. This is because the hit position on low poly
  // light geometry may not be on the actual surface of the light and needs to be corrected.
  vec3 position;
  f16vec3 normal;
  vec3 radiance;
  // Note: 32 bit floating point used to avoid precision issues with some kinds of sampling on lights.
  float solidAnglePdf;
};

#endif
