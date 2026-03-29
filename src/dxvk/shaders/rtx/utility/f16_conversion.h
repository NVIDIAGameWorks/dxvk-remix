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

// CPU-side half-float (IEEE 754 binary16) to float conversion.
// On the GPU, f16tof32 is a built-in intrinsic; this header provides
// the equivalent for C++ host code.

#ifdef __cplusplus
#include <cstdint>
#include <cstring>

inline float f16tof32(uint32_t h16) {
  const uint32_t sign     = (h16 & 0x8000u) << 16u;
  const uint32_t exponent = (h16 >> 10u) & 0x1fu;
  const uint32_t mantissa =  h16 & 0x3ffu;
  uint32_t result;
  if (exponent == 0u) {
    if (mantissa == 0u) {
      result = sign; // +-zero
    } else {
      // Denormalized half -> normalized float
      uint32_t shiftedMantissa = mantissa;
      uint32_t shiftCount = 0u;
      while (!(shiftedMantissa & 0x400u)) {
        shiftedMantissa <<= 1u;
        ++shiftCount;
      }
      shiftedMantissa &= 0x3ffu;
      result = sign | ((113u - shiftCount) << 23u) | (shiftedMantissa << 13u);
    }
  } else if (exponent == 31u) {
    result = sign | 0x7f800000u | (mantissa << 13u); // +-Inf or NaN
  } else {
    result = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
  }
  float f;
  std::memcpy(&f, &result, sizeof(f));
  return f;
}

#endif // __cplusplus
