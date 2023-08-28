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
#include "rtx_intersection_test_helpers.h"

#include "dxvk_scoped_annotation.h"

bool boundingBoxIntersectsFrustumSAT(
  dxvk::RtCamera& camera,            // The main camera
  const dxvk::Vector3& minPos,       // The minimum position of AABB bounding box of the object
  const dxvk::Vector3& maxPos,       // The maximum position of AABB bounding box of the object
  const dxvk::Matrix4& objectToView, // Object to viewspace transform matrix
  const bool isInfFrustum) {         // Is the camera frustum has infinity far plane
  ScopedCpuProfileZone();

  // Calculate 3 OBB axis (And these are also treated as OBB edge vectors)
  const dxvk::Vector4 obbCenterView(objectToView * dxvk::Vector4((minPos + maxPos) * 0.5f, 1.0f));

  const dxvk::Vector4 obbAxisView[3] = {
    objectToView* dxvk::Vector4(maxPos.x - minPos.x, 0.0f, 0.0f, 0.0f),
    objectToView* dxvk::Vector4(0.0f, maxPos.y - minPos.y, 0.0f, 0.0f),
    objectToView* dxvk::Vector4(0.0f, 0.0f, maxPos.z - minPos.z, 0.0f)
  };
  dxvk::Vector4 obbExtents(dxvk::length(obbAxisView[0]), dxvk::length(obbAxisView[1]), dxvk::length(obbAxisView[2]), 0.0f);

  const dxvk::Vector4 obbAxisNormalized[3] = {
    obbExtents.x != 0.0f ? obbAxisView[0] / obbExtents.x : obbAxisView[0],
    obbExtents.y != 0.0f ? obbAxisView[1] / obbExtents.y : obbAxisView[1],
    obbExtents.z != 0.0f ? obbAxisView[2] / obbExtents.z : obbAxisView[2]
  };
  obbExtents *= 0.5f;

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
    const float MoZ = camera.isLHS() ? axis.z : -axis.z;

    const dxvk::RtFrustum& frustum = camera.getFrustum();
    const float p = frustum.getNearPlaneRightExtent() * MoX + frustum.getNearPlaneUpExtent() * MoY;

    const float farNearRatio = camera.getFarPlane() / camera.getNearPlane();

    p0 = camera.getNearPlane() * MoZ - p;
    if (p0 < 0.0f) {
      if (isInfFrustum) {
        p0 = -std::numeric_limits<float>::infinity();
      } else {
        p0 *= farNearRatio;
      }
    }

    p1 = camera.getNearPlane() * MoZ + p;
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

    if (camera.isLHS()) {
      if (projObbCenter + obbExtent < camera.getNearPlane() ||
          (!isInfFrustum && projObbCenter - obbExtent > camera.getFarPlane())) {
        return false;
      }
    } else {
      if (projObbCenter - obbExtent > camera.getNearPlane() ||
          (!isInfFrustum && projObbCenter - obbExtent > camera.getFarPlane())) {
        return false;
      }
    }

    // Side planes
    for (uint32_t planeIdx = 0; planeIdx <= PLANE_TOP; ++planeIdx) {
      const float3& planeNormal = camera.getFrustum().GetPlane(planeIdx).To3d();
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
        const dxvk::Vector4 crossProductAxis = dxvk::Vector4(dxvk::cross(obbAxisNormalized[obbAxisIdx].xyz(), camera.getFrustum().getFrustumEdgeVector(frustumEdgeIdx)), 0.0f);
        if (dxvk::dot(crossProductAxis, crossProductAxis) > 0.1f && // Make sure the 2 edges are NOT parallel with each other
            checkSeparableAxis(crossProductAxis)) {
          return false;
        }
      }
    }
  }

  return true;
}
