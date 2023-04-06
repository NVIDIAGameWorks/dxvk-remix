/*
* Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

namespace dxvk {
  
  // Note: From Omniverse
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
  
}