/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "util_matrix.h"
#include "util_vector.h"
#include "xxHash/xxhash.h"

namespace dxvk {

struct AxisAlignedBoundingBox {
  Vector3 minPos{ FLT_MAX, FLT_MAX, FLT_MAX };
  Vector3 maxPos{ -FLT_MAX, -FLT_MAX, -FLT_MAX };

  const bool isValid() const {
    return minPos.x <= maxPos.x && minPos.y <= maxPos.y && minPos.z <= maxPos.z;
  }

  void invalidate() {
    minPos = Vector3{ FLT_MAX, FLT_MAX, FLT_MAX };
    maxPos = Vector3{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
  }

  void unionWith(const AxisAlignedBoundingBox& other) {
    for (uint32_t i = 0; i < 3; i++) {
      minPos[i] = std::min(minPos[i], other.minPos[i]);
      maxPos[i] = std::max(maxPos[i], other.maxPos[i]);
    }
  }

  Vector3 getCentroid() const {
    return (minPos + maxPos) * 0.5f;
  }

  // returns untransformed position if AABB is invalid
  Vector3 getTransformedCentroid(const Matrix4& transform) const {
    if (isValid()) {
      return (transform * Vector4(getCentroid(), 1.0f)).xyz();
    } else {
      return transform[3].xyz();
    }
  }

  const XXH64_hash_t calculateHash() const {
    return XXH3_64bits(this, sizeof(AxisAlignedBoundingBox));
  }

  float getVolume(const Matrix4& transform, float minimumThickness = 0.001f) const {
    const Vector3 minPosWorld = (transform * dxvk::Vector4(minPos, 1.0f)).xyz();
    const Vector3 maxPosWorld = (transform * dxvk::Vector4(maxPos, 1.0f)).xyz();

    // Assume some minimum thickness to work around the possibility of infinitely thin geometry
    const Vector3 size = max(Vector3(minimumThickness), abs(maxPosWorld - minPosWorld));

    return size.x * size.y * size.z;
  }
};
}  // namespace dxvk
