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

#include <cstdint>
#include <cmath>
#include <cassert>
#include <algorithm>

#include "util_vector.h"
#include "util_matrix.h"

namespace dxvk {

  // Note: GLM packing functions could be used instead of these, but these are specifically
  // designed to match the exact way our implementation on the GPU encodes/decodes them in case there
  // are some differences.

  template<unsigned int OutputSize, typename TOut>
  constexpr TOut packUnorm(const float x, const float d = 0.5f) {
    constexpr auto outputTypeSize = sizeof(TOut) * CHAR_BIT;
    static_assert(OutputSize <= outputTypeSize);

    // Note: Ensure the input float is within the proper unorm range
    assert(x <= 1.0f && x >= 0.0f);

    const TOut mask = (1 << OutputSize) - 1;
    const TOut normalizationFactor = mask;

    return static_cast<TOut>(std::floor(x * normalizationFactor + d)) & mask;
  }

  template<unsigned int OutputSize, typename TOut>
  constexpr TOut packSnorm(const float x, const float d = 0.5f) {
    constexpr auto outputTypeSize = sizeof(TOut) * CHAR_BIT;
    static_assert(OutputSize <= outputTypeSize);

    // Note: Ensure the input float is within the proper snorm range
    assert(x <= 1.0f && x >= -1.0f);

    const TOut mask = (1 << OutputSize) - 1;
    // Note: Done to allow for better encoding of 0 with low bit packing
    const TOut normalizationFactor = mask - 1;

    const auto remappedX = x * 0.5f + 0.5f;

    return static_cast<TOut>(std::floor(remappedX * normalizationFactor + d)) & mask;
  }

  // Note: Made to match GPU expectation for LogLuv32 decoding.
  inline uint32_t packLogLuv32(const Vector3& radiance) {
    // Note: Matrix column vectors given just like on GPU side.
    const Matrix3 srgbToModXYZ = Matrix3(
      Vector3(0.2209f, 0.3390f, 0.4184f),
      Vector3(0.1138f, 0.6780f, 0.7319f),
      Vector3(0.0102f, 0.1130f, 0.2969f)
    );

    Vector3 modXYZ = srgbToModXYZ * radiance;
    modXYZ[0] = std::max(modXYZ[0], 1e-6f);
    modXYZ[1] = std::max(modXYZ[1], 1e-6f);
    modXYZ[2] = std::max(modXYZ[2], 1e-6f);

    const float ue = modXYZ[0] / modXYZ[2];
    const float ve = modXYZ[1] / modXYZ[2];
    const float le = log2(modXYZ.y) / 48.0f;

    // Note: Range check asserts implicitly part of snorm/unorm packing, no need to check within this function
    return
      ((uint32_t)packSnorm<16, uint16_t>(std::clamp(le, -1.0f, 1.0f)) << 16) |
      ((uint32_t)packUnorm<8, uint8_t>(ue) << 8) |
      (uint32_t)packUnorm<8, uint8_t>(ve);
  }

}