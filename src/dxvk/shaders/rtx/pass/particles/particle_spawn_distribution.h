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
#else
// Renderer shaders already define their packing helpers independently of the CPU compatibility header.
#define ReadBuffer(T) StructuredBuffer<T>
#endif

#ifdef __cplusplus
namespace dxvk {
#endif

static const uint kParticleDistributionBlockSize = 256;
// Range-safe area construction passes and scratch storage.
static const uint kParticleDistributionMeasure = 0;
static const uint kParticleDistributionScale = 1;
static const uint kParticleDistributionBuild = 2;
static const uint kParticleDistributionFinalize = 3;
static const uint kParticleDistributionPassCount = 4;
static const uint kInvalidParticleTriangle = 0xffffffffu;

inline bool particleDistributionIsFinite(float value) {
#ifdef __cplusplus
  return std::isfinite(value);
#else
  return isfinite(value);
#endif
}

inline float3 particleTriangleBarycentrics(float u, float v) {
  if (u + v > 1.0f) {
    u = 1.0f - u;
    v = 1.0f - v;
  }
  return float3(1.0f - u - v, u, v);
}

inline float particleTriangleAreaWeight(float3 edge0, float3 edge1) {
  const float area = length(cross(edge0, edge1));
  return particleDistributionIsFinite(area) && area > 0.0f ? area : 0.0f;
}

inline uint particleDistributionBlockCount(uint triangleCount) {
  return triangleCount / kParticleDistributionBlockSize
    + uint(triangleCount % kParticleDistributionBlockSize != 0);
}

// Keeps upper-bound searches inside the table even when multiplication rounds up to the total.
inline float particleDistributionTarget(float total, float randomValue) {
  const float target = total * min(max(randomValue, 0.0f), 1.0f);
  return target < total ? target : asfloat(asuint(total) - 1u);
}

inline uint particleDistributionUpperBound(ReadBuffer(float) distribution, uint offset, uint count, float target) {
  uint first = 0;
  uint remaining = count;
  while (remaining > 0) {
    const uint step = remaining / 2;
    const uint middle = first + step;
    if (distribution[offset + middle] <= target) {
      first = middle + 1;
      remaining -= step + 1;
    } else {
      remaining = step;
    }
  }
  return first;
}

// Triangle prefixes restart in each block. The final blockCount elements contain block-total prefixes.
inline uint sampleParticleTriangle(ReadBuffer(float) distribution, uint offset, uint triangleCount, float randomValue) {
  if (triangleCount == 0 || !particleDistributionIsFinite(randomValue)) {
    return kInvalidParticleTriangle;
  }
  const uint blockCount = particleDistributionBlockCount(triangleCount);
  const uint blockOffset = offset + triangleCount;
  const float total = distribution[blockOffset + blockCount - 1];
  if (!particleDistributionIsFinite(total) || total <= 0.0f) {
    return kInvalidParticleTriangle;
  }
  const float target = particleDistributionTarget(total, randomValue);
  const uint block = particleDistributionUpperBound(distribution, blockOffset, blockCount, target);
  if (block >= blockCount) {
    return kInvalidParticleTriangle;
  }
  const uint firstTriangle = block * kParticleDistributionBlockSize;
  const uint localCount = min(triangleCount - firstTriangle, kParticleDistributionBlockSize);
  const float localTotal = distribution[offset + firstTriangle + localCount - 1];
  if (!particleDistributionIsFinite(localTotal) || localTotal <= 0.0f) {
    return kInvalidParticleTriangle;
  }
  const float previousTotal = block > 0 ? distribution[blockOffset + block - 1] : 0.0f;
  const float localTarget = min(max(target - previousTotal, 0.0f), asfloat(asuint(localTotal) - 1u));
  const uint triangle = particleDistributionUpperBound(distribution, offset + firstTriangle, localCount, localTarget);
  return triangle < localCount ? firstTriangle + triangle : kInvalidParticleTriangle;
}

#ifdef __cplusplus
}
#endif

#include "rtx/utility/cpu_gpu_compat_undef.h"
