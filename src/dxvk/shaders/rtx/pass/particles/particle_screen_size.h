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

#include "rtx/utility/shader_types.h"

#ifdef __cplusplus
#include "rtx/utility/cpu_gpu_compat.h"
namespace dxvk {
#endif

struct ParticleScreenSizeAdjustment {
  float scale;
  float opacityScale;
};

inline bool particleScreenSizeFinite(float value) {
#ifdef __cplusplus
  return std::isfinite(value);
#else
  return isfinite(value);
#endif
}

enum class ParticleScreenSizeAction {
  Clear,
  KeepWorldSize,
  Adjust
};

struct ParticleScreenSizeMeasurement {
  ParticleScreenSizeAction action;
  float projectedSize;
};

inline bool particleScreenSizeFiniteClip(float4 clip) {
  return particleScreenSizeFinite(clip.x) && particleScreenSizeFinite(clip.y)
    && particleScreenSizeFinite(clip.z) && particleScreenSizeFinite(clip.w);
}

inline ParticleScreenSizeMeasurement measureParticleScreenSize(
    float4 centerClip, float4 rightClip, float4 upClip, vec2 renderSize) {
  ParticleScreenSizeMeasurement result;
  result.action = ParticleScreenSizeAction::Clear;
  result.projectedSize = 0.f;
  if (!particleScreenSizeFiniteClip(centerClip)) {
    return result;
  }
  // Secondary rays can see finite particles whose primary-camera projection is unsuitable.
  if (centerClip.w <= 1e-5f) {
    result.action = ParticleScreenSizeAction::KeepWorldSize;
    return result;
  }
  if (!particleScreenSizeFiniteClip(rightClip) || !particleScreenSizeFiniteClip(upClip)
      || !particleScreenSizeFinite(renderSize.x) || !particleScreenSizeFinite(renderSize.y)
      || renderSize.x <= 0.f || renderSize.y <= 0.f) {
    return result;
  }

  const float inverseW = 1.f / centerClip.w;
  const float ndcX = centerClip.x * inverseW;
  const float ndcY = centerClip.y * inverseW;
  const float pixelScaleX = 0.5f * renderSize.x * inverseW;
  const float pixelScaleY = 0.5f * renderSize.y * inverseW;
  const float rightX = (rightClip.x - ndcX * rightClip.w) * pixelScaleX;
  const float rightY = (rightClip.y - ndcY * rightClip.w) * pixelScaleY;
  const float upX = (upClip.x - ndcX * upClip.w) * pixelScaleX;
  const float upY = (upClip.y - ndcY * upClip.w) * pixelScaleY;
  const float squaredSize = max(rightX * rightX + rightY * rightY, upX * upX + upY * upY);
  if (!particleScreenSizeFinite(rightX) || !particleScreenSizeFinite(rightY)
      || !particleScreenSizeFinite(upX) || !particleScreenSizeFinite(upY)
      || !particleScreenSizeFinite(squaredSize) || squaredSize <= 0.f) {
    return result;
  }
#ifdef __cplusplus
  result.projectedSize = std::sqrt(squaredSize);
#else
  result.projectedSize = sqrt(squaredSize);
#endif
  result.action = ParticleScreenSizeAction::Adjust;
  return result;
}

inline float particleScreenSizeBound(float value) {
  return particleScreenSizeFinite(value) && value > 0.f ? value : 0.f;
}

inline bool particleScreenSizeEnabled(float minimum, float maximum) {
  return particleScreenSizeBound(minimum) > 0.f || particleScreenSizeBound(maximum) > 0.f;
}

inline float particleScreenSizeOpacity(float scale, float coverage, float baseHeight, float trailLength) {
  if (!particleScreenSizeFinite(scale) || scale <= 1.f) {
    return 1.f;
  }
  coverage = particleScreenSizeFinite(coverage) ? min(max(coverage, 0.f), 1.f) : 1.f;
  if (coverage == 0.f) {
    return 1.f;
  }
  const float inverseScale = 1.f / scale;
  float areaRatio = inverseScale * inverseScale;
  if (particleScreenSizeFinite(trailLength) && trailLength > 0.f
      && particleScreenSizeFinite(baseHeight) && baseHeight >= 0.f) {
    const float normalizer = max(baseHeight, trailLength);
    const float normalizedHeight = baseHeight / normalizer;
    const float normalizedTrail = trailLength / normalizer;
    const float heightWeight = normalizedHeight / (normalizedHeight + normalizedTrail);
    areaRatio = inverseScale / (heightWeight * scale + (1.f - heightWeight));
  }
#ifdef __cplusplus
  return std::pow(areaRatio, coverage);
#else
  return pow(areaRatio, coverage);
#endif
}

inline ParticleScreenSizeAdjustment adjustParticleScreenSize(float projectedSize, float minimum, float maximum, float coverage) {
  ParticleScreenSizeAdjustment adjustment;
  adjustment.scale = 1.f;
  adjustment.opacityScale = 1.f;
  if (!particleScreenSizeFinite(projectedSize) || projectedSize <= 0.f) {
    return adjustment;
  }

  minimum = particleScreenSizeBound(minimum);
  maximum = particleScreenSizeBound(maximum);
  if (maximum > 0.f && maximum < minimum) {
    maximum = minimum;
  }
  float target = max(projectedSize, minimum);
  if (maximum > 0.f) {
    target = min(target, maximum);
  }
  const float scale = target / projectedSize;
  if (!particleScreenSizeFinite(scale) || scale <= 0.f) {
    return adjustment;
  }
  adjustment.scale = scale;
  adjustment.opacityScale = particleScreenSizeOpacity(scale, coverage, 1.f, 0.f);
  return adjustment;
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "rtx/utility/cpu_gpu_compat_undef.h"
#endif
