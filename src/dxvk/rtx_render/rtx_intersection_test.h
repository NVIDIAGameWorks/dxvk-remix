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

#include "rtx_intersection_test_helpers.h"
#include "rtx_camera.h"

#include "dxvk_scoped_annotation.h"

// Robust BoundingBox-Frustum intersection check with Separation Axis Theorem (SAT)
static inline bool boundingBoxIntersectsFrustumSAT(
  dxvk::RtCamera& camera,            // The main camera
  const dxvk::Vector3& minPos,       // The minimum position of AABB bounding box of the object
  const dxvk::Vector3& maxPos,       // The maximum position of AABB bounding box of the object
  const dxvk::Matrix4& objectToView, // Object to viewspace transform matrix
  const bool isInfFrustum) {         // Is the camera frustum has infinity far plane

  ScopedCpuProfileZone();

  dxvk::RtFrustum& frustum = camera.getFrustum();
  const dxvk::Vector3 (&frustumEdgeVectors)[4] = {
    frustum.getFrustumEdgeVector(0),
    frustum.getFrustumEdgeVector(1),
    frustum.getFrustumEdgeVector(2),
    frustum.getFrustumEdgeVector(3)
  };
  return boundingBoxIntersectsFrustumSATInternal(minPos, maxPos,
                                                 objectToView,
                                                 frustum,
                                                 camera.getNearPlane(),
                                                 frustum.GetPlane(ePlaneType::PLANE_FAR).w,
                                                 frustum.getNearPlaneRightExtent(),
                                                 frustum.getNearPlaneUpExtent(),
                                                 frustumEdgeVectors,
                                                 camera.isLHS(),
                                                 isInfFrustum);
}
