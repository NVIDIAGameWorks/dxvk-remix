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
#pragma once

#include "MathLib/MathLib.h"
#include "../util/util_matrix.h"

static inline bool rayIntersectsPlane(
  const dxvk::Vector3& s0,  // ray segment start
  const dxvk::Vector3& d,   // ray direction
  const dxvk::Vector3& n,   // plane normal
  const dxvk::Vector3& p0,  // point on a plane
  float* t) {

  float denom = dot(n, d);
  if (abs(denom) > 1e-6) {
    *t = dot(p0 - s0, n) / denom;
    return (*t >= 0);
  }

  return false;
}

static inline bool inRange(float a, float minValue, float maxValue)
{
  return a >= minValue && a <= maxValue;
}

static inline bool lineSegmentIntersectsQuad(
  const dxvk::Vector3& l0,  // line segment start
  const dxvk::Vector3& l1,  // line segment end
  const dxvk::Vector3& n,   // quad plane normal 
  const dxvk::Vector3& centroid,    // quad center point 
  const dxvk::Vector3 basis[2],     // quad basis vectors
  const dxvk::Vector2 halfExtents) {// quad halfExtents

  dxvk::Vector3 d = l1 - l0;
  float tMax = length(d);
  d /= tMax;  // normalize

  float t = 0;
  if (rayIntersectsPlane(l0, d, n, centroid, &t) && t <= tMax) {
    dxvk::Vector3 p = l0 + d * t;
    dxvk::Vector3 cToP = p - centroid;
    float u = dot(cToP, basis[0]);
    float v = dot(cToP, basis[1]);
    return inRange(u, -halfExtents.x, halfExtents.x) 
        && inRange(v, -halfExtents.y, halfExtents.y);
  }

  return false;
}

// Projects a point onto a quad and returns whether it lies within quad's bounds
static inline bool projectedPointLiesInsideQuad(
  const dxvk::Vector3& p ,  // point
  const dxvk::Vector3& n,   // quad plane normal 
  const dxvk::Vector3& centroid,    // quad center point 
  const dxvk::Vector3 basis[2],     // quad basis vectors
  const dxvk::Vector2 halfExtents) {// quad halfExtents

  dxvk::Vector3 cToP = p - centroid;
  float u = dot(cToP, basis[0]);
  float v = dot(cToP, basis[1]);
  return inRange(u, -halfExtents.x, halfExtents.x)
      && inRange(v, -halfExtents.y, halfExtents.y);
}

// Fast BoundingBox-Frustum intersection check
static inline bool boundingBoxIntersectsFrustum(
  cFrustum& frustum,                   // The frustum check for intersection
  const dxvk::Vector3& minPos,         // The minimum position of AABB bounding box of the object
  const dxvk::Vector3& maxPos,         // The maximum position of AABB bounding box of the object
  const dxvk::Matrix4& objectToView) { // Object to viewspace transform matrix
  const dxvk::Vector4 minPosView = objectToView * dxvk::Vector4(minPos, 1.0f);
  const dxvk::Vector4 maxPosView = objectToView * dxvk::Vector4(maxPos, 1.0f);

  float4 obbVertices[8] = {
    float4(minPosView.x, minPosView.y, minPosView.z, 1.0f),
    float4(maxPosView.x, minPosView.y, minPosView.z, 1.0f),
    float4(minPosView.x, maxPosView.y, minPosView.z, 1.0f),
    float4(minPosView.x, minPosView.y, maxPosView.z, 1.0f),
    float4(maxPosView.x, maxPosView.y, minPosView.z, 1.0f),
    float4(minPosView.x, maxPosView.y, maxPosView.z, 1.0f),
    float4(maxPosView.x, minPosView.y, maxPosView.z, 1.0f),
    float4(minPosView.x, minPosView.y, minPosView.z, 1.0f)
  };

  for (uint32_t planeIdx = 0; planeIdx < PLANES_NUM; ++planeIdx) {
    bool insidePlane = false;
    const float4 plane = frustum.GetPlane(planeIdx);
    for (uint32_t obbVerticesIdx = 0; obbVerticesIdx < 8; ++obbVerticesIdx) {
      // Fast in-out plane check with SIMD
      if (Dot44(plane, obbVertices[obbVerticesIdx]) >= 0.0f) {
        insidePlane = true;
        break;
      }
    }
    if (!insidePlane) {
      return false;
    }
  }
  return true;
}