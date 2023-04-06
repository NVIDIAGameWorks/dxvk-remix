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
#include <stdint.h>

namespace dxvk {
  
  constexpr size_t CACHE_LINE_SIZE = 64;
  constexpr float FLOAT16_MAX = 6.5504e+4f;
  
  template<typename T>
  constexpr T clamp(T n, T lo, T hi) {
    if (n < lo) return lo;
    if (n > hi) return hi;
    return n;
  }
  
  template<typename T, typename U = T>
  constexpr T align(T what, U to) {
    return (what + to - 1) & ~(to - 1);
  }

  template<typename T, typename U = T>
  constexpr T align_safe(T what, U to, T maxValue) {
    return (what + std::min(maxValue - what, to - 1)) & ~(to - 1);
  }

  template<typename T, typename U = T>
  constexpr T alignDown(T what, U to) {
    return (what / to) * to;
  }

  // Equivalent of std::clamp for use with floating point numbers
  // Handles (-){INFINITY,NAN} cases.
  // Will return min in cases of NAN, etc.
  inline float fclamp(float value, float min, float max) {
    return std::fmin(
      std::fmax(value, min), max);
  }

  template<typename T>
  inline T divCeil(T dividend, T divisor) {
    return (dividend + divisor - 1) / divisor;
  }
  
  template<typename T, typename U = T>
  constexpr T lerp(T a, U b, const float t) {
    return a + t * (b - a);
  }

  // This function takes an integer input and returns the count of set bits n the input, at compile time!
  // Variation of bit counting from: https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetNaive
  constexpr uint8_t popcnt_uint8(uint8_t i) {
    i = i - ((i >> 1) & 0x55);
    i = (i & 0x33) + ((i >> 2) & 0x33);
    return ((i + (i >> 4)) & 0x0F);
  }
}