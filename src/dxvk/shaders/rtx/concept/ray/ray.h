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
#ifndef RAY_H
#define RAY_H

// Disable Slang warning about deprecation of inheritance in favor of composition
// struct Base { int a; };
// struct Inherited : public Base { int b; }; <- no more this
// struct Composed { Base base; int b; }; <- only this
#pragma warning(disable:30816)

#ifndef USE_32BIT_RAY_DIRECTION
#define USE_32BIT_RAY_DIRECTION 0
#endif

struct Ray
{
  vec3 origin;
  // Note: Cone radius at the origin of the ray, not a hit point
  float16_t coneRadius;
  float16_t spreadAngle;
  // Note: Assumed to be normalized in advance
#if USE_32BIT_RAY_DIRECTION
  vec3 direction;
#else
  f16vec3 direction;
#endif
  // Note: No tMin available as it must always be 0 for now to minimize live state in TraceRay paths.
  float tMax;
};

RayDesc rayToRayDesc(Ray ray)
{
  RayDesc rayDesc;
  rayDesc.Origin = ray.origin;
  rayDesc.Direction = ray.direction;
  rayDesc.TMin = 0;
  rayDesc.TMax = ray.tMax;
  return rayDesc;
}

struct GBufferMemoryMinimalRay
{
  float16_t spreadAngle = 0.h;
};

struct MinimalRay
{
  float16_t spreadAngle = 0.h;
};

// Note: Minimal version of typical Ray Interaction for transmission across passes.
struct MinimalRayInteraction
{
  // Note: Cone radius at the hit point (after spreading over a distance from the view ray)
  float16_t coneRadius = 0.h;
  // Note: Do not use direction for anything highly precise (e.g. hit position derivation) as it is only
  // 16 bit and will lack enough precision to get highly accurate results. Generally acceptable for lighting
  // however unless significant artifacting is seen, in which case it may be justifiable to bump the precision
  // up (for primary rays mainly the concern exists).
  f16vec3 viewDirection = 0.h;
};

struct RayInteraction : MinimalRayInteraction
{
  float hitDistance = 0.f;
  uint barycentricCoordinates = 0u;
  uint primitiveIndex = 0u;
  uint customIndex = 0u;
  uint16_t surfaceIndex = 0u;
  uint8_t materialType = 0u;
  uint8_t frontHit = 0u; // Todo: Pack this into some other value to not take up extra space
};

struct GBufferMemoryMinimalRayInteraction
{
  // Note: Only write out the View Direction when the alteredViewDirection flag was passed to the packing function,
  // otherwise it will be undefined.
  vec2 encodedViewDirection;
  float16_t encodedConeRadius;
};


#endif
