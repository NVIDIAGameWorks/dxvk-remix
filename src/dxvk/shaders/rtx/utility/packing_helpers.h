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

// These helpers can be executed on the CPU or GPU!!
#ifdef __cplusplus
#include "glm/detail/func_common.hpp"

#define min(a,b) std::min(a,b)
typedef float float32_t;
typedef float float16_t;
#endif

// Signed/Unsigned Normalized Float Packing Helpers

// Pack [0.0, 1.0] or [-1.0, 1.0] float to a uint of a given bit depth, and store into the nearest representable format
// Note: Input size represents the size of the float (16 or 32 bit), output size represents the size
// of the encoded unorm output, and the nearest output size is the corresponding integer format to store
// the output in. For example a 2 bit output's nearest format is 8 bit integers. Values in and out will always be masked
// so passing in merely a shifted value is sufficent for decoding. Additionally casts to a lower precision integer cast
// so casting to the proper type to give as the input when decoding will also act as a mask without issue.
// Note: For snorm encodings one value is left out to allow for 0 to be encoded perfectly (as this is often important
// with extremely low precision representations, e.g. encoding anisotropy in a 4 bit representation).
#define GENERIC_FLOAT_TO_NORM(inputSize, outputSize, nearestOutputSize)  \
uint ## nearestOutputSize ## _t f ## inputSize ## ToUnorm ## outputSize( \
  float ## inputSize ## _t x,                                            \
  float ## inputSize ## _t d)                                            \
{                                                                        \
  const uint ## nearestOutputSize ## _t mask =                           \
    uint ## nearestOutputSize ## _t((1 << outputSize) - 1);              \
  const float ## inputSize ## _t normalizationFactor =                   \
    float ## inputSize ## _t (mask);                                     \
                                                                         \
  return min(uint ## nearestOutputSize ## _t(                            \
    floor(x * normalizationFactor + d)), mask);                          \
}                                                                        \
uint ## nearestOutputSize ## _t f ## inputSize ## ToUnorm ## outputSize( \
  float ## inputSize ## _t x)                                            \
{                                                                        \
  return f ## inputSize ## ToUnorm ## outputSize(                        \
    x, float ## inputSize ## _t(0.5));                                   \
}                                                                        \
float ## inputSize ## _t unorm ## outputSize ## ToF ## inputSize(        \
  uint ## nearestOutputSize ## _t x)                                     \
{                                                                        \
  const uint ## nearestOutputSize ## _t mask =                           \
    uint ## nearestOutputSize ## _t((1 << outputSize) - 1);              \
  const uint ## inputSize ## _t normalizationFactor = mask;              \
                                                                         \
  return                                                                 \
    float ## inputSize ## _t(x & mask) /                                 \
    float ## inputSize ## _t(normalizationFactor);                       \
}                                                                        \
uint ## nearestOutputSize ## _t f ## inputSize ## ToSnorm ## outputSize( \
  float ## inputSize ## _t x,                                            \
  float ## inputSize ## _t d)                                            \
{                                                                        \
  const uint ## nearestOutputSize ## _t mask =                           \
    uint ## nearestOutputSize ## _t((1 << outputSize) - 1);              \
  const float ## inputSize ## _t normalizationFactor =                   \
    float ## inputSize ## _t((1 << outputSize) - 2);                     \
                                                                         \
  const float ## inputSize ## _t m =                                     \
    x * float ## inputSize ## _t(0.5) + float ## inputSize ## _t(0.5);   \
                                                                         \
  return min(uint ## nearestOutputSize ## _t(                            \
    floor(m * normalizationFactor + d)), mask);                          \
}                                                                        \
uint ## nearestOutputSize ## _t f ## inputSize ## ToSnorm ## outputSize( \
  float ## inputSize ## _t x)                                            \
{                                                                        \
  return f ## inputSize ## ToSnorm ## outputSize(                        \
    x, float ## inputSize ## _t(0.5));                                   \
}                                                                        \
float ## inputSize ## _t snorm ## outputSize ## ToF ## inputSize(        \
  uint ## nearestOutputSize ## _t x)                                     \
{                                                                        \
  const uint ## nearestOutputSize ## _t mask =                           \
    uint ## nearestOutputSize ## _t((1 << outputSize) - 1);              \
  const uint ## inputSize ## _t normalizationFactor =                    \
    uint ## inputSize ## _t((1 << outputSize) - 2);                      \
                                                                         \
  const float ## inputSize ## _t m =                                     \
    float ## inputSize ## _t(x & mask) /                                 \
    float ## inputSize ## _t(normalizationFactor);                       \
                                                                         \
  return                                                                 \
    m * float ## inputSize ## _t(2.0) - float ## inputSize ## _t(1.0);   \
}

GENERIC_FLOAT_TO_NORM(32, 2, 8)
GENERIC_FLOAT_TO_NORM(16, 2, 8)
GENERIC_FLOAT_TO_NORM(32, 3, 8)
GENERIC_FLOAT_TO_NORM(16, 3, 8)
GENERIC_FLOAT_TO_NORM(32, 4, 8)
GENERIC_FLOAT_TO_NORM(16, 4, 8)
GENERIC_FLOAT_TO_NORM(32, 5, 8)
GENERIC_FLOAT_TO_NORM(16, 5, 8)
GENERIC_FLOAT_TO_NORM(32, 6, 8)
GENERIC_FLOAT_TO_NORM(16, 6, 8)
GENERIC_FLOAT_TO_NORM(32, 7, 8)
GENERIC_FLOAT_TO_NORM(16, 7, 8)
GENERIC_FLOAT_TO_NORM(32, 8, 8)
GENERIC_FLOAT_TO_NORM(16, 8, 8)
GENERIC_FLOAT_TO_NORM(32, 10, 16)
GENERIC_FLOAT_TO_NORM(16, 10, 16)
GENERIC_FLOAT_TO_NORM(32, 11, 16)
GENERIC_FLOAT_TO_NORM(16, 11, 16)
GENERIC_FLOAT_TO_NORM(32, 16, 16)
// Note: 16 norm->16 float and vice versa conversion is pointless for the most part in code we control as
// 16 bit floats can almost always be used in this case over 16 bit norms which avoids precision loss. Additionally
// this function is broken due to an overflow of the largest 16 bit number casted to float 16, which generates infinity.
//GENERIC_FLOAT_TO_NORM(16, 16, 16)

#ifdef __cplusplus
#undef min
#endif