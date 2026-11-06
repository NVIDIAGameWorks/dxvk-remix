// Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
// SPDX-License-Identifier: MIT

#include "rtx/pass/particles/particle_spawn_distribution.h"
#include "rtx/pass/particles/particle_system_common.h"

[[vk::binding(50)]] ConstantBuffer<ParticleSpawnDistributionConstants> constants;
[[vk::binding(63)]] StructuredBuffer<float> distribution;
[[vk::binding(64)]] RWStructuredBuffer<uint> samples;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
  if (index <= 4096) {
    samples[index] = sampleParticleTriangle(distribution, 0, constants.spawnContext.numTriangles, float(index) / 4096.0f);
  }
}
