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

#include "util_vector.h"

namespace dxvk {
  constexpr float kSRGBGamma = 2.2f;

  // Converts a sRGB color encoded in gamma space to linear space.
  inline Vector3 sRGBGammaToLinear(const Vector3& c) {
    // Note: Approximation of actual sRGB OETF.
    return {
      std::pow(c.x, kSRGBGamma),
      std::pow(c.y, kSRGBGamma),
      std::pow(c.z, kSRGBGamma),
    };
  }

  // Converts a sRGB color encoded in linear space to gamma space.
  inline Vector3 sRGBLinearToGamma(const Vector3& c) {
    // Note: Approximation of actual sRGB EOTF.
    return {
      std::pow(c.x, 1.0f / kSRGBGamma),
      std::pow(c.y, 1.0f / kSRGBGamma),
      std::pow(c.z, 1.0f / kSRGBGamma),
    };
  }

  // Converts a sRGB color to luminance based on the BT.709 standard.
  inline float sRGBLuminance(const Vector3& c) {
    return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
  }
}