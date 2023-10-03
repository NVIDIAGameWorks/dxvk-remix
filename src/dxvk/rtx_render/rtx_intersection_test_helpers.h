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

static inline bool sphereIntersectsFrustum(
  cFrustum& frustum,           // The frustum check for intersection
  const dxvk::Vector3& center, // The center position of the sphere bounding box of the object
  const float radius) {        // The radius of the sphere bounding box of the object
  return frustum.CheckSphere(float3(center.x, center.y, center.z), radius);
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
    for (uint32_t obbVertexIdx = 0; obbVertexIdx < 8; ++obbVertexIdx) {
      // Fast in-out plane check with SIMD
      if (Dot44(plane, obbVertices[obbVertexIdx]) >= 0.0f) {
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

// Internal function for Robust BoundingBox-Frustum intersection check with Separation Axis Theorem (SAT)
static bool boundingBoxIntersectsFrustumSATInternal(
  const dxvk::Vector3& minPos,                 // The minimum position of AABB bounding box of the object
  const dxvk::Vector3& maxPos,                 // The maximum position of AABB bounding box of the object
  const dxvk::Matrix4& objectToView,           // Object to viewspace transform matrix
  cFrustum& frustum,                           // Cached frustum
  const float nearPlane,                       // Camera near plane
  const float farPlane,                        // Camera far plane
  const float nearPlaneRightExtent,            // The half extent along right axis on the camera near plane
  const float nearPlaneUpExtent,               // The half extent along up axis on the camera near plane
  const dxvk::Vector3(&frustumEdgeVectors)[4], // Normalized vector from near plane vertex to corresponding far plane vertex
  const bool isLHS,                            // Is the camera frustum left-hand system
  const bool isInfFrustum) {                   // Is the camera frustum has infinity far plane

  // Calculate 3 normalized Oriented Bounding-Box(OBB) axis, which are 3 normals that are not on the same line of OBB faces.
  // These are also treated as OBB edge vectors, because they are all aligned to these 3 axis and no need to check again.
  const dxvk::Vector4 obbCenterView(objectToView * dxvk::Vector4((minPos + maxPos) * 0.5f, 1.0f));

  // Note: When the OBB has same coordinate value on 1 or more dimensions, it will become a plane/line/point.
  //       In such case, we still need to check the axis of the missing dimension(s).
  //       So, we just set the unit length axis to represent axis direction (normalized axis), then revert extent back to 0 after transformation.
  const dxvk::Vector3 extentScale = {
    maxPos.x - minPos.x > FLT_EPSILON ? 0.5f : 0.0f,
    maxPos.y - minPos.y > FLT_EPSILON ? 0.5f : 0.0f,
    maxPos.z - minPos.z > FLT_EPSILON ? 0.5f : 0.0f
  };
  const dxvk::Vector4 obbAxisView[3] = {
    objectToView * (extentScale.x != 0.0f ? dxvk::Vector4(maxPos.x - minPos.x, 0.0f, 0.0f, 0.0f) : dxvk::Vector4(1.0f, 0.0f, 0.0f, 0.0f)),
    objectToView * (extentScale.y != 0.0f ? dxvk::Vector4(0.0f, maxPos.y - minPos.y, 0.0f, 0.0f) : dxvk::Vector4(0.0f, 1.0f, 0.0f, 0.0f)),
    objectToView * (extentScale.z != 0.0f ? dxvk::Vector4(0.0f, 0.0f, maxPos.z - minPos.z, 0.0f) : dxvk::Vector4(0.0f, 0.0f, 1.0f, 0.0f))
  };
  dxvk::Vector4 obbExtents(dxvk::length(obbAxisView[0]), dxvk::length(obbAxisView[1]), dxvk::length(obbAxisView[2]), 0.0f);

  // Calculate the view space OBB extent.
  // Note: We scale the extents here to avoid dividing 0.
  const dxvk::Vector4 obbAxisNormalized[3] = {
    obbAxisView[0] / obbExtents.x * extentScale.x,
    obbAxisView[1] / obbExtents.y * extentScale.y,
    obbAxisView[2] / obbExtents.z * extentScale.z
  };

  // Project OBB extent to axis
  auto calProjectedObbExtent = [&](const dxvk::Vector4& axis) -> float {
    const dxvk::Vector4 projObbAxisToAxis(std::fabs(dxvk::dot(obbAxisNormalized[0], axis)),
                                          std::fabs(dxvk::dot(obbAxisNormalized[1], axis)),
                                          std::fabs(dxvk::dot(obbAxisNormalized[2], axis)),
                                          0.0f);
    return dxvk::dot(projObbAxisToAxis, obbExtents);
  };

  // Fast Frustum Projection Algorithm:
  // https://www.geometrictools.com/Documentation/IntersectionBox3Frustum3.pdf
  auto calProjectedFrustumExtent = [&](const dxvk::Vector4& axis, float& p0, float& p1) -> void {
    const float MoX = std::fabs(axis.x);
    const float MoY = std::fabs(axis.y);
    const float MoZ = isLHS ? axis.z : -axis.z;

    const float p = nearPlaneRightExtent * MoX + nearPlaneUpExtent * MoY;

    const float farNearRatio = farPlane / nearPlane;

    p0 = nearPlane * MoZ - p;
    if (p0 < 0.0f) {
      if (isInfFrustum) {
        p0 = -std::numeric_limits<float>::infinity();
      } else {
        p0 *= farNearRatio;
      }
    }

    p1 = nearPlane * MoZ + p;
    if (p1 > 0.0f) {
      if (isInfFrustum) {
        p1 = std::numeric_limits<float>::infinity();
      } else {
        p1 *= farNearRatio;
      }
    }
  };

  auto checkSeparableAxis = [&](const dxvk::Vector4& axis) -> bool {
    const float projObbCenter = dxvk::dot(obbCenterView, axis);
    const float projObbExtent = calProjectedObbExtent(axis);
    const float obbMin = projObbCenter - projObbExtent;
    const float obbMax = projObbCenter + projObbExtent;

    float p0 = 0.0f, p1 = 0.0f;
    calProjectedFrustumExtent(axis, p0, p1);

    if (obbMin > p1 || obbMax < p0) { // Find an axis that the frustum and bbox can be separated with a line perpendicular to the axis
      return true;
    } else {
      return false;
    }
  };

  // Check frustum normals (5 axis)
  {
    // Z
    const float projObbCenter = obbCenterView.z;
    const float obbExtent = calProjectedObbExtent(dxvk::Vector4(0.0f, 0.0f, 1.0f, 0.0f));

    if (isLHS) {
      if (projObbCenter + obbExtent < nearPlane ||
          (!isInfFrustum && projObbCenter - obbExtent > farPlane)) {
        return false;
      }
    } else {
      if (projObbCenter - obbExtent > nearPlane ||
          (!isInfFrustum && projObbCenter - obbExtent > farPlane)) {
        return false;
      }
    }

    // Side planes
    for (uint32_t planeIdx = 0; planeIdx <= PLANE_TOP; ++planeIdx) {
      const float3& planeNormal = frustum.GetPlane(planeIdx).To3d();
      const dxvk::Vector4 planeNormalVec4(planeNormal.x, planeNormal.y, planeNormal.z, 0.0f);
      if (checkSeparableAxis(planeNormalVec4)) {
        return false;
      }
    }
  }

  // Check OBB axis (3 axis)
  for (uint32_t obbAxisIdx = 0; obbAxisIdx < 3; ++obbAxisIdx) {
    if (checkSeparableAxis(obbAxisNormalized[obbAxisIdx])) {
      return false;
    }
  }

  // Check cross-product between OBB edges and frustum edges (18 axis)
  {
    // obbEdges x frustumRight (1, 0, 0)
    for (uint32_t obbAxisIdx = 0; obbAxisIdx < 3; ++obbAxisIdx) {
      if (checkSeparableAxis(dxvk::Vector4(0.0f, obbAxisNormalized[obbAxisIdx].z, -obbAxisNormalized[obbAxisIdx].y, 0.0f))) {
        return false;
      }
    }

    // obbEdges x frustumUp (0, 1, 0)
    for (uint32_t obbAxisIdx = 0; obbAxisIdx < 3; ++obbAxisIdx) {
      if (checkSeparableAxis(dxvk::Vector4(-obbAxisNormalized[obbAxisIdx].z, 0.0f, obbAxisNormalized[obbAxisIdx].x, 0.0f))) {
        return false;
      }
    }

    // obbEdges x frustumEdges
    for (uint32_t obbAxisIdx = 0; obbAxisIdx < 3; ++obbAxisIdx) {
      for (uint32_t frustumEdgeIdx = 0; frustumEdgeIdx < 4; ++frustumEdgeIdx) {
        const dxvk::Vector4 crossProductAxis = dxvk::Vector4(dxvk::cross(obbAxisNormalized[obbAxisIdx].xyz(), frustumEdgeVectors[frustumEdgeIdx]), 0.0f);
        if (dxvk::dot(crossProductAxis, crossProductAxis) > 0.1f && // Make sure the 2 edges are NOT parallel with each other
            checkSeparableAxis(crossProductAxis)) {
          return false;
        }
      }
    }
  }

  return true;
}

static inline bool rectIntersectsFrustum(
  cFrustum& frustum,               // The frustum check for intersection
  const dxvk::Vector3& pos,        // The center position of the rectangle
  const dxvk::Vector2& dimensions, // Object space extents of rectangle
  const dxvk::Vector3& xAxis,      // xAxis vector in world space
  const dxvk::Vector3& yAxis) {    // yAxis vector in world space
  constexpr int RectVerticeNumber = 4;

  const dxvk::Vector3 vertices[RectVerticeNumber] = {
    pos + dimensions.x * xAxis,
    pos - dimensions.x * xAxis,
    pos + dimensions.y * yAxis,
    pos - dimensions.y * yAxis
  };

  const float4 verticesSimd[RectVerticeNumber] = {
    float4(vertices[0].x, vertices[0].y, vertices[0].z, 1.0f),
    float4(vertices[1].x, vertices[1].y, vertices[1].z, 1.0f),
    float4(vertices[2].x, vertices[2].y, vertices[2].z, 1.0f),
    float4(vertices[3].x, vertices[3].y, vertices[3].z, 1.0f),
  };

  // Loop all planes. If all 4 vertices of rectangle are outside of any of these 6 planes,
  // the rectangle is not intersecting with the frustum.
  for (uint32_t planeIdx = 0; planeIdx < PLANES_NUM; ++planeIdx) {
    const float4 plane = frustum.GetPlane(planeIdx);
    for (uint32_t vertexIdx = 0; vertexIdx < RectVerticeNumber; ++vertexIdx) {
      // Fast in-out plane check with SIMD
      if (Dot44(plane, verticesSimd[vertexIdx]) >= 0.0f) {
        continue;
      }
      return false;
    }
  }

  return true;
}
