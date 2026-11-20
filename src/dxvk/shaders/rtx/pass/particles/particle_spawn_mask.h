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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#ifdef __cplusplus
#include <cmath>
#endif

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx/utility/cpu_gpu_compat.h"
namespace dxvk {
#endif

inline bool particleSpawnMaskFinite(float value) {
#ifdef __cplusplus
  return std::isfinite(value);
#else
  return isfinite(value);
#endif
}

inline float particleSpawnMaskClamp(float value) {
  return particleSpawnMaskFinite(value) ? (value < 0.f ? 0.f : (value > 1.f ? 1.f : value)) : 0.f;
}

inline float particleSpawnMaskWeight(float alpha, float luminance, uint source) {
  if (source == 0) {
    return 1.f;
  }
  if (source == 1) {
    return particleSpawnMaskClamp(alpha);
  }
  if (source == 2) {
    return particleSpawnMaskClamp(luminance);
  }
  return source == 3 ? particleSpawnMaskClamp(alpha) * particleSpawnMaskClamp(luminance) : 0.f;
}

inline bool particleSpawnMaskAccept(float alpha, float luminance, uint source, uint response, float threshold, float sample) {
  if (source == 0) {
    return true;
  }
  if (source > 3) {
    return false;
  }
  const float weight = particleSpawnMaskWeight(alpha, luminance, source);
  if (response == 0) {
    return particleSpawnMaskFinite(sample) && sample >= 0.f && sample < 1.f && sample < weight;
  }
  return response == 1 && particleSpawnMaskFinite(threshold) && weight > 0.f && weight >= particleSpawnMaskClamp(threshold);
}

// Masked rejected slots keep their sampled lifetime even when every slot is visited each frame.
inline bool particleSpawnMaskCanRespawn(uint source, bool rejected, bool dead, bool sleeping, float timeToLive) {
  if (source == 0 || !rejected) {
    return dead || sleeping;
  }
  return dead || timeToLive <= 0.f;
}

inline vec2 particleSpawnMaskInterpolateUv(vec2 a, vec2 b, vec2 c, vec3 bary) {
  return a * bary.x + b * bary.y + c * bary.z;
}

inline vec2 particleSpawnMaskUv(vec2 uv, vec4 rowU, vec4 rowV, vec4 sprite, bool animated) {
  vec2 transformed = vec2(rowU.x * uv.x + rowU.y * uv.y + rowU.z + rowU.w,
    rowV.x * uv.x + rowV.y * uv.y + rowV.z + rowV.w);
  if (animated) {
#ifdef __cplusplus
    transformed.x -= std::floor(transformed.x);
    transformed.y -= std::floor(transformed.y);
#else
    transformed = frac(transformed);
#endif
    transformed.x = transformed.x * sprite.x + sprite.z;
    transformed.y = transformed.y * sprite.y + sprite.w;
  }
  return transformed;
}

#ifdef __cplusplus
}
#include "rtx/utility/cpu_gpu_compat_undef.h"
#endif
