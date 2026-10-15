/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cassert>

#include "../../util/util_matrix.h"

namespace dxvk {

  inline Matrix4 makeProjectionMatrixForCube(const Matrix4& projection) {
    Matrix4 result = projection;
    result[0][0] = 1.f;
    result[1][1] = 1.f;
    result[2][2] = 1.f;
    result[2][3] = 1.f;
    return result;
  }

  inline Matrix4 makeViewMatrixForCubePlane(uint32_t plane, const Vector3& cameraPosition) {
    assert(plane < 6);

    constexpr static Vector3 kTargets[] {
      Vector3 { +1,  0,  0 },
      Vector3 { -1,  0,  0 },
      Vector3 {  0, +1,  0 },
      Vector3 {  0, -1,  0 },
      Vector3 {  0,  0, +1 },
      Vector3 {  0,  0, -1 },
    };
    constexpr static Vector3 kUps[] {
      Vector3 { 0, +1,  0 },
      Vector3 { 0, +1,  0 },
      Vector3 { 0,  0, -1 },
      Vector3 { 0,  0, +1 },
      Vector3 { 0, +1,  0 },
      Vector3 { 0, +1,  0 },
    };

    const Vector3 axisZ = kTargets[plane];
    const Vector3 axisX = cross(kUps[plane], axisZ);
    const Vector3 axisY = cross(axisZ, axisX);
    const Vector3 translation {
      dot(axisX, -cameraPosition),
      dot(axisY, -cameraPosition),
      dot(axisZ, -cameraPosition),
    };

    return Matrix4 {
      axisX.x, axisY.x, axisZ.x, 0.f,
      axisX.y, axisY.y, axisZ.y, 0.f,
      axisX.z, axisY.z, axisZ.z, 0.f,
      translation.x, translation.y, translation.z, 1.f,
    };
  }

} // namespace dxvk
