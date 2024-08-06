/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include <cmath>
#include "util_vector.h"
#include "util_matrix.h"

namespace dxvk {
  
  // Note: From Omniverse. This function should return a normalized quaternion when working properly.
  inline Vector4 getOrientation(Vector3 src, Vector3 dst) {
    // If the rotation is larger than pi/2 then do it from the other side.
    // 1. rotate by pi around (1,0,0)
    // 2. find shortest rotation from there
    // 3. return the quaternion which does the full rotation
    float tmp = dot(src, dst);
    bool flip = tmp < 0;

    if (flip) {
      src = Vector3(src.x, -src.y, -src.z);
    }

    const Vector3 v = cross(src, dst);
    Vector4 q;
    q.w = std::sqrt((1.0f + std::abs(tmp)) / 2.0f);
    const Vector3 t = v / (2.0f * q.w);
    q.x = t.x;
    q.y = t.y;
    q.z = t.z;

    if (flip) {
      q = Vector4(q.w, q.z, -q.y, -q.x);
    }

    return q;
  }

  // This produces a quaternion with a positive w, then inverts the quaternion if the input
  // TBN was right handed.  As q == -q for rotation purposes, this is a safe way to store
  // an extra flag along with the quaternion.
  // `isQuatRightHanded` can be used to easily check if the input TBN was right handed.
  inline Vector4 matrixToQuaternion(Matrix4 mat) {
    Vector3 tangent = mat[0].xyz();
    Vector3 bitangent = mat[1].xyz();
    Vector3 normal = mat[2].xyz();
    const bool rightHanded = (dot(cross(tangent, bitangent), normal) >= 0.0);
    if (!rightHanded) {
      tangent = -tangent;
    }

    Matrix3 rotationMatrix = { tangent, bitangent, normal };
    rotationMatrix = transpose(rotationMatrix);

    const float tr = rotationMatrix[0][0] + rotationMatrix[1][1] + rotationMatrix[2][2];
    Vector4 quaternion;
    if (tr > 0) {
      float s = sqrt(tr + 1.0) * 2; // s=4*qw
      quaternion.w = 0.25 * s;
      quaternion.x = (rotationMatrix[2][1] - rotationMatrix[1][2]) / s;
      quaternion.y = (rotationMatrix[0][2] - rotationMatrix[2][0]) / s;
      quaternion.z = (rotationMatrix[1][0] - rotationMatrix[0][1]) / s;
    } else if ((rotationMatrix[0][0] > rotationMatrix[1][1]) && (rotationMatrix[0][0] > rotationMatrix[2][2])) {
      float s = sqrt(1.0 + rotationMatrix[0][0] - rotationMatrix[1][1] - rotationMatrix[2][2]) * 2; // s=4*qx
      quaternion.w = (rotationMatrix[2][1] - rotationMatrix[1][2]) / s;
      quaternion.x = 0.25 * s;
      quaternion.y = (rotationMatrix[0][1] + rotationMatrix[1][0]) / s;
      quaternion.z = (rotationMatrix[0][2] + rotationMatrix[2][0]) / s;
    } else if (rotationMatrix[1][1] > rotationMatrix[2][2]) {
      float s = sqrt(1.0 + rotationMatrix[1][1] - rotationMatrix[0][0] - rotationMatrix[2][2]) * 2; // s=4*qy
      quaternion.w = (rotationMatrix[0][2] - rotationMatrix[2][0]) / s;
      quaternion.x = (rotationMatrix[0][1] + rotationMatrix[1][0]) / s;
      quaternion.y = 0.25 * s;
      quaternion.z = (rotationMatrix[1][2] + rotationMatrix[2][1]) / s;
    } else {
      float s = sqrt(1.0 + rotationMatrix[2][2] - rotationMatrix[0][0] - rotationMatrix[1][1]) * 2; // s=4*qz
      quaternion.w = (rotationMatrix[1][0] - rotationMatrix[0][1]) / s;
      quaternion.x = (rotationMatrix[0][2] + rotationMatrix[2][0]) / s;
      quaternion.y = (rotationMatrix[1][2] + rotationMatrix[2][1]) / s;
      quaternion.z = 0.25 * s;
    }

    // If sign bit of w is negative but the original TBN was right handed 
    // (or vice versa) flip the quaternion.
    if ((quaternion.w < 0) != rightHanded) {
      quaternion *= -1.f;
    }

    return quaternion;
  }
}