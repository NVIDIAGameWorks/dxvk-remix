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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/particles/particle_system_binding_indices.h"
#include "rtx/pass/particles/particle_spawn_distribution_build.slangh"

[[vk::binding(PARTICLE_SYSTEM_BINDING_CONSTANTS)]]
ConstantBuffer<ParticleSpawnDistributionConstants> distributionCb;

[[vk::binding(PARTICLE_SYSTEM_BINDING_SPAWN_DISTRIBUTION_OUTPUT)]]
RWStructuredBuffer<float> SpawnDistribution;

float3 loadEmitterPosition(uint vertex) {
  const GpuSpawnContext spawn = distributionCb.spawnContext;
  const uint offset = (spawn.spawnMeshPositionsOffset + vertex * spawn.spawnMeshPositionsStride) / 4;
  return float3(BUFFER_ARRAY(geometries, (uint)spawn.spawnMeshPositionsIdx, offset),
                BUFFER_ARRAY(geometries, (uint)spawn.spawnMeshPositionsIdx, offset + 1),
                BUFFER_ARRAY(geometries, (uint)spawn.spawnMeshPositionsIdx, offset + 2));
}

// Keep component exponents separate through transforms and cross products.
// A vector-wide scale can erase a small component before it multiplies a large one.
// Store each scalar separately until the final normal is reduced to an area magnitude.
struct ParticleAreaValue {
  float mantissa;
  int exponent;
};

ParticleAreaValue makeParticleAreaValue(float value) {
  ParticleAreaValue result;
  uint bits = asuint(value);
  const uint magnitude = bits & 0x7fffffffu;
  if (magnitude == 0) {
    result.mantissa = 0.0f;
    result.exponent = -1024;
    return result;
  }
  int exponentOffset = 0;
  if ((magnitude >> 23) == 0) {
    // Decode subnormals through their integer significand without flushing them to zero.
    bits = asuint(float(magnitude)) | (bits & 0x80000000u);
    exponentOffset = -149;
  }
  result.mantissa = asfloat((bits & 0x807fffffu) | 0x3f800000u);
  result.exponent = int((bits >> 23) & 255u) - 127 + exponentOffset;
  return result;
}

ParticleAreaValue multiplyParticleAreaValues(ParticleAreaValue a, ParticleAreaValue b) {
  ParticleAreaValue result = makeParticleAreaValue(a.mantissa * b.mantissa);
  if (result.mantissa != 0.0f) {
    result.exponent += a.exponent + b.exponent;
  }
  return result;
}

ParticleAreaValue addParticleAreaValues(ParticleAreaValue a, ParticleAreaValue b) {
  if (a.mantissa == 0.0f) {
    return b;
  }
  if (b.mantissa == 0.0f) {
    return a;
  }
  const int exponent = max(a.exponent, b.exponent);
  ParticleAreaValue result = makeParticleAreaValue(ldexp(a.mantissa, a.exponent - exponent)
                                               + ldexp(b.mantissa, b.exponent - exponent));
  if (result.mantissa != 0.0f) {
    result.exponent += exponent;
  }
  return result;
}

ParticleAreaValue sumParticleAreaProducts(ParticleAreaValue a, ParticleAreaValue b, ParticleAreaValue c) {
  // Combine the largest terms first so their cancellation cannot erase the small third term.
  if (a.exponent < b.exponent) {
    const ParticleAreaValue temporary = a;
    a = b;
    b = temporary;
  }
  if (b.exponent < c.exponent) {
    const ParticleAreaValue temporary = b;
    b = c;
    c = temporary;
  }
  if (a.exponent < b.exponent) {
    const ParticleAreaValue temporary = a;
    a = b;
    b = temporary;
  }
  return addParticleAreaValues(addParticleAreaValues(a, b), c);
}

float particleAreaWeight(float3 edge0, float3 edge1, out int exponent) {
  ParticleAreaValue transformed0[3];
  ParticleAreaValue transformed1[3];
  for (uint row = 0; row < 3; row++) {
    ParticleAreaValue products0[3];
    ParticleAreaValue products1[3];
    for (uint column = 0; column < 3; column++) {
      const ParticleAreaValue coefficient = makeParticleAreaValue(distributionCb.spawnContext.spawnObjectToWorld[row][column]);
      products0[column] = multiplyParticleAreaValues(coefficient, makeParticleAreaValue(edge0[column]));
      products1[column] = multiplyParticleAreaValues(coefficient, makeParticleAreaValue(edge1[column]));
    }
    transformed0[row] = sumParticleAreaProducts(products0[0], products0[1], products0[2]);
    transformed1[row] = sumParticleAreaProducts(products1[0], products1[1], products1[2]);
  }
  ParticleAreaValue normal[3];
  exponent = -1024;
  for (uint axis = 0; axis < 3; axis++) {
    const uint next = (axis + 1) % 3;
    const uint last = (axis + 2) % 3;
    const ParticleAreaValue positive = multiplyParticleAreaValues(transformed0[next], transformed1[last]);
    ParticleAreaValue negative = multiplyParticleAreaValues(transformed0[last], transformed1[next]);
    negative.mantissa = -negative.mantissa;
    normal[axis] = addParticleAreaValues(positive, negative);
    exponent = max(exponent, normal[axis].exponent);
  }
  // At this point tiny components can be discarded safely relative to the normal's length.
  float3 scaledNormal;
  for (uint axis = 0; axis < 3; axis++) {
    scaledNormal[axis] = ldexp(normal[axis].mantissa, normal[axis].exponent - exponent);
  }
  return length(scaledNormal);
}

float maximumDistributionValue(float value, uint lane) {
  particleDistributionScan[lane] = value;
  GroupMemoryBarrierWithGroupSync();
  for (uint stride = kParticleDistributionBlockSize / 2; stride > 0; stride /= 2) {
    if (lane < stride) {
      particleDistributionScan[lane] = max(particleDistributionScan[lane], particleDistributionScan[lane + stride]);
    }
    GroupMemoryBarrierWithGroupSync();
  }
  return particleDistributionScan[0];
}

[shader("compute")]
[numthreads(kParticleDistributionBlockSize, 1, 1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const GpuSpawnContext spawn = distributionCb.spawnContext;
  const uint blockCount = particleDistributionBlockCount(spawn.numTriangles);
  // Prefix layout remains unchanged. Append one common exponent and one exponent per triangle.
  const uint scaleOffset = spawn.numTriangles + blockCount;
  const uint triangle = group.x * kParticleDistributionBlockSize + lane;
  if (distributionCb.pass == kParticleDistributionFinalize) {
    finalizeParticleDistribution(SpawnDistribution, 0, spawn.numTriangles, lane);
    return;
  }
  if (distributionCb.pass == kParticleDistributionScale) {
    float exponent = -1024.0f;
    for (uint block = lane; block < blockCount; block += kParticleDistributionBlockSize) {
      exponent = max(exponent, SpawnDistribution[spawn.numTriangles + block]);
    }
    const float commonExponent = maximumDistributionValue(exponent, lane);
    if (lane == 0) {
      SpawnDistribution[scaleOffset] = commonExponent;
    }
    return;
  }
  if (distributionCb.pass == kParticleDistributionBuild) {
    float area = 0.0f;
    if (triangle < spawn.numTriangles && SpawnDistribution[triangle] > 0.0f) {
      const int exponent = int(SpawnDistribution[scaleOffset + 1 + triangle] - SpawnDistribution[scaleOffset]);
      area = ldexp(SpawnDistribution[triangle], exponent);
    }
    buildParticleDistributionBlock(SpawnDistribution, 0, spawn.numTriangles, group.x, lane, area);
    return;
  }

  float area = 0.0f;
  int areaExponent = -1024;
  if (triangle < spawn.numTriangles) {
    uint vertices[3];
    for (uint i = 0; i < 3; i++) {
      const uint index = triangle * 3 + i;
      vertices[i] = spawn.indices32bit
        ? BUFFER_ARRAY(indices32, (uint)spawn.spawnMeshIndexIdx, index)
        : BUFFER_ARRAY(indices, (uint)spawn.spawnMeshIndexIdx, index);
    }
    const float3 origin = loadEmitterPosition(vertices[0]);
    float3 edge0 = loadEmitterPosition(vertices[1]) - origin;
    float3 edge1 = loadEmitterPosition(vertices[2]) - origin;
    if (all(isfinite(edge0)) && all(isfinite(edge1))) {
      area = particleAreaWeight(edge0, edge1, areaExponent);
      if (!isfinite(area) || area <= 0.0f) {
        area = 0.0f;
        areaExponent = -1024;
      }
    }
    SpawnDistribution[triangle] = area;
    SpawnDistribution[scaleOffset + 1 + triangle] = float(areaExponent);
  }
  const float blockExponent = maximumDistributionValue(float(areaExponent), lane);
  if (lane == 0) {
    SpawnDistribution[spawn.numTriangles + group.x] = blockExponent;
  }
}
