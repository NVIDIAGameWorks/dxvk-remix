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

// Cross-compile compatibility layer for headers shared between C++ and Slang/HLSL.
//
// Include this at the top of any dual-compile .h file.  At the end of the file,
// include cpu_gpu_compat_undef.h to clean up the macros.
//
// NOTE: This header intentionally avoids #pragma once.  Macro definitions
// (ReadBuffer, WriteBuffer, abs, min, max) are re-established on every include
// so that a second consumer in the same translation unit still has them after
// a previous consumer called cpu_gpu_compat_undef.h.  Type aliases, functions,
// and normal-encoding utilities are guarded and defined only once.

// ===========================================================================
// Macros — re-evaluated on every include
// ===========================================================================

#ifdef __cplusplus
#define ReadBuffer(T)   const T*
#define WriteBuffer(T)  T*
#define abs(x)          std::abs(x)
#define min(a, b)       std::min(a, b)
#define max(x, y)       std::max(x, y)
#else
#define ReadBuffer(T)   StructuredBuffer<T>
#define WriteBuffer(T)  RWStructuredBuffer<T>
#endif

// ===========================================================================
// Type aliases, bit-cast functions, and shared utilities — once per TU
// ===========================================================================

#ifndef RTX_CPU_GPU_COMPAT_H
#define RTX_CPU_GPU_COMPAT_H

#ifdef __cplusplus
#include <cstring>
#include <algorithm>
#include <cmath>

namespace dxvk {

typedef Vector3  float3;
typedef Vector4  float4;
typedef Vector4i uint4;

// Strict-aliasing safe bit casts (prefer over reinterpret_cast)
uint asuint(float f) {
  uint u;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

float asfloat(uint u) {
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}

#endif // __cplusplus

// ---------------------------------------------------------------------------
// Normal encoding — octahedral encode/decode (shared by skinning, smooth normals, etc.)
// ---------------------------------------------------------------------------

uint f32ToUnorm16(float x) {
  const float scalar = (1 << 16) - 1;
  const float conv = x * scalar + 0.5f;
  return uint(conv);
}

float unorm16ToF32(uint x) {
  const float scalar = (1 << 16) - 1;
  const float conv = float(x & ((1 << 16) - 1)) / scalar;
  return conv;
}

uint encodeNormal(float3 n) {
  const float maxMag = abs(n.x) + abs(n.y) + abs(n.z);
  const float inverseMag = maxMag == 0.0f ? 0.0f : (1.0f / maxMag);
  float x = n.x * inverseMag;
  float y = n.y * inverseMag;

  if (n.z < 0.0f) {
    const float originalXSign = x < 0.0f ? -1.0f : 1.0f;
    const float originalYSign = y < 0.0f ? -1.0f : 1.0f;
    const float inverseAbsX = 1.0f - abs(x);
    const float inverseAbsY = 1.0f - abs(y);

    x = inverseAbsY * originalXSign;
    y = inverseAbsX * originalYSign;
  }

  // Signed->Unsigned octahedral
  x = x * 0.5f + 0.5f;
  y = y * 0.5f + 0.5f;

  return f32ToUnorm16(x) | (f32ToUnorm16(y) << 16);
}

float3 decodeNormal(uint e) {
  float x = unorm16ToF32(e);
  float y = unorm16ToF32(e >> 16);

  // Unsigned->Signed octahedral
  x = x * 2.0f - 1.0f;
  y = y * 2.0f - 1.0f;

  float3 v = float3(x, y, 1.0f - abs(x) - abs(y));
  const float t = max(-v.z, 0.0f);

  v.x += (v.x >= 0.0f) ? -t : t;
  v.y += (v.y >= 0.0f) ? -t : t;

  return normalize(v);
}

#ifdef __cplusplus
} // namespace dxvk
#endif

#endif // RTX_CPU_GPU_COMPAT_H
