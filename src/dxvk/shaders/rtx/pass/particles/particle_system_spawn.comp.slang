/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/particles/particle_system_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/procedural_noise.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/pass/particles/particle_spawn_distribution.h"
#include "rtx/pass/particles/particle_spawn_mask.h"
    
layout(binding = PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT)
StructuredBuffer<uint16_t> SpawnContextParticleMapping;

layout(binding = PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT)
StructuredBuffer<GpuSpawnContext> SpawnContexts;

layout(binding = PARTICLE_SYSTEM_BINDING_SPAWN_DISTRIBUTION_INPUT)
StructuredBuffer<float> SpawnDistribution;

// Outputs

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
RWStructuredBuffer<GpuParticle> Particles;


f16vec3 sampleDirectionInCone(f16vec3 axis, float cosConeHalfAngle, float2 rndUV)
{
  // Sample a direction in a cone about +Z, and get its PDF (we ignore PDF here)
  float solidAnglePdf;
  f16vec3 localDir = calcUniformConeDirectionSample(vec2(rndUV), cosConeHalfAngle, solidAnglePdf);

  f16vec3 tangent, bitangent;
  calcOrthonormalBasis(axis, tangent, bitangent);

  // Rotate the local-Z sample into the world-axis frame
  return localDir.x * tangent + localDir.y * bitangent + localDir.z * axis;
}

// Generate a new particle in-place with random properties
GpuParticle respawn(uint particleIdx, uint spawnCtxIdx, const GpuParticleSystem particleSystem)
{
  GpuParticle newParticle = (GpuParticle)0;
  
  // Get spawn context
  GpuSpawnContext spawnCtx = SpawnContexts[spawnCtxIdx];
  
  // Get random triangle
  
  float3 positions[3]; 
  float3 prevPositions[3];
  vec2 emitterTexcoords[3];
  float4 color = (spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID) ? 0.0f : 1.0f;
  float4 uvMinMax = float4(0..xx, 1..xx);
  
  uint randomSeed = particleIdx;
    
  const float u = randomFloat(uint2(randomSeed++, particleCb.frameIdx));
  const float v = randomFloat(uint2(randomSeed++, particleCb.frameIdx));
  const float3 randomBary = particleTriangleBarycentrics(u, v);

  const float triangleSample = randomFloat(uint2(randomSeed++, particleCb.frameIdx));
  const uint triIdx = particleSystem.desc.spawnDistribution == ParticleSpawnDistribution::BySurfaceArea
    ? sampleParticleTriangle(SpawnDistribution, 0, spawnCtx.numTriangles, triangleSample)
    : (spawnCtx.numTriangles > 0 ? min(uint(triangleSample * spawnCtx.numTriangles), spawnCtx.numTriangles - 1) : kInvalidParticleTriangle);
  if (triIdx == kInvalidParticleTriangle) {
    // Rejected slots retain their normal age so the conservative ring cannot skip older live particles.
    newParticle.reset(particleSystem, float3(0.0f), float3(0.0f), float4(0.0f), float4(0.0f), triangleSample);
    newParticle.rejectSpawn();
    return newParticle;
  }
  
  for (uint i=0 ; i<3 ; i++)
  {
    uint indexIdx = triIdx * 3 + i;
    uint index = spawnCtx.indices32bit ? BUFFER_ARRAY(indices32, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx) : BUFFER_ARRAY(indices, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx);
    
    const uint baseSrcPositionOffset = (spawnCtx.spawnMeshPositionsOffset + index * spawnCtx.spawnMeshPositionsStride) / 4;
    positions[i] = float3(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 0),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 1),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 2));
    
    if (spawnCtx.spawnMeshPrevPositionsIdx != BINDING_INDEX_INVALID)
    {
      prevPositions[i].x = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 0);
      prevPositions[i].y = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 1);
      prevPositions[i].z = BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPrevPositionsIdx, baseSrcPositionOffset + 2);
    }
    else
    {
      prevPositions[i] = positions[i];
    }
    
    if(spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID)
    {
      const uint baseSrcColorOffset = (spawnCtx.spawnMeshColorsOffset + index * spawnCtx.spawnMeshColorsStride) / 4;
      const uint colorBits = floatBitsToUint(BUFFER_ARRAY(geometries, (uint) spawnCtx.spawnMeshColorsIdx, baseSrcColorOffset));
      // Format is B8G8R8A8_UNORM
      color += unorm4x8ToFloat4x32(colorBits).zyxw * randomBary[i];
    }
    
    // Only fetch emitter UVs when a valid texcoord buffer is bound. For a mesh without UVs,
    // spawnMeshTexcoordsIdx == BINDING_INDEX_INVALID (0xFFFF); reading geometries[0xFFFF] is an
    // out-of-range bindless descriptor access and faults the GPU (device loss). Mirrors the
    // existing guards on the color and prev-position reads above.
    float2 texcoord = float2(0.0f, 0.0f);
    if (spawnCtx.spawnMeshTexcoordsIdx != BINDING_INDEX_INVALID)
    {
      const uint baseSrcTexcoordOffset = (spawnCtx.spawnMeshTexcoordsOffset + index * spawnCtx.spawnMeshTexcoordsStride) / 4;
      texcoord = float2(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 0),
                        BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 1));
    }
    
    emitterTexcoords[i] = texcoord;
    if (particleSystem.desc.useSpawnTexcoords && spawnCtx.spawnMeshTexcoordsIdx != BINDING_INDEX_INVALID)
    {
      if (i == 0)
      {
        uvMinMax.xy = texcoord;
        uvMinMax.zw = texcoord;
      }
      else
      {
        uvMinMax.xy = min(uvMinMax.xy, texcoord);
        uvMinMax.zw = max(uvMinMax.zw, texcoord);
      }
    }
  }
  
  if (particleSystem.desc.spawnMaskSource)
  {
    bool valid = spawnCtx.spawnMaskStatus;
    float4 mask = spawnCtx.spawnMaskConstant;
    if (spawnCtx.spawnMaskStatus == 2)
    {
      const vec2 rawUv = particleSpawnMaskInterpolateUv(emitterTexcoords[0], emitterTexcoords[1], emitterTexcoords[2], vec3(randomBary));
      const vec2 maskUv = particleSpawnMaskUv(rawUv, spawnCtx.spawnMaskUvU, spawnCtx.spawnMaskUvV, spawnCtx.spawnMaskSprite, spawnCtx.spawnMaskAnimated != 0);
      valid = all(isfinite(maskUv)) && spawnCtx.spawnMaskTextureIdx != BINDING_INDEX_INVALID && spawnCtx.spawnMaskSamplerIdx != BINDING_INDEX_INVALID;
      if (valid)
      {
        mask = textures[nonuniformEXT(uint(spawnCtx.spawnMaskTextureIdx))].SampleLevel(samplers[nonuniformEXT(uint(spawnCtx.spawnMaskSamplerIdx))], maskUv, 0.f);
      }
    }
    float luminance = 0.f;
    if (particleSystem.desc.spawnMaskSource == 2 || particleSystem.desc.spawnMaskSource == 3)
    {
      float3 linearColor = mask.rgb;
      if (spawnCtx.spawnMaskStatus != 2 || !spawnCtx.spawnMaskSrgb)
      {
        linearColor = gammaToLinear(max(mask.rgb, 0.f));
      }
      luminance = calcBt709Luminance(linearColor);
    }
    const float sample = randomFloat(uint2(randomSeed++, particleCb.frameIdx));
    if (!valid || !particleSpawnMaskAccept(mask.a, luminance, particleSystem.desc.spawnMaskSource,
        particleSystem.desc.spawnMaskResponse, particleSystem.desc.spawnMaskThreshold, sample))
    {
      newParticle.reset(particleSystem, float3(0.f), float3(0.f), float4(0.f), float4(0.f), randomFloat(uint2(randomSeed, particleCb.frameIdx)));
      newParticle.rejectSpawn();
      return newParticle;
    }
  }

  // Sample random point on triangle
  const float randSeed = randomFloat(uint2(randomSeed++, particleCb.frameIdx));

  float3 prevWorldPosition = mul(spawnCtx.spawnPrevObjectToWorld, float4(interpolateHitAttribute(prevPositions, randomBary), 1.f)).xyz;
  float3 worldPosition = mul(spawnCtx.spawnObjectToWorld, float4(interpolateHitAttribute(positions, randomBary), 1.f));
  
  float3 worldSpawnVelocity = (worldPosition - prevWorldPosition) * particleCb.invDeltaTimeSecs;
  
  const float3 kFallbackNormal = float3(0.f, 0.f, 1.f);
  float3 localNormal = cross(positions[1] - positions[0], positions[2] - positions[0]);
  float3 worldNormal = safeNormalize(mul(spawnCtx.spawnObjectToWorld, float4(localNormal, 0.f)), kFallbackNormal);
  
  float cosCone = cos(radians(particleSystem.desc.initialVelocityConeAngleDegrees));
  f16vec3 dir = sampleDirectionInCone(worldNormal, cosCone, float2(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx))));
  
  float3 initialVelocity = worldSpawnVelocity * particleSystem.desc.initialVelocityFromMotion + dir * particleSystem.desc.initialVelocityFromNormal * particleCb.sceneScale;
  
  float3 spawnPosition = lerp(worldPosition, prevWorldPosition, randSeed);
  
  // Spawn a particle
  newParticle.reset(particleSystem, spawnPosition, initialVelocity, uvMinMax, color, randSeed);
  newParticle.emitterObjectPickingValue = spawnCtx.emitterObjectPickingValue;
  
  return newParticle;
}


[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if(threadIdx.x >= particleSystem.spawnParticleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.spawnParticleOffset) % particleSystem.desc.maxNumParticles;
  const uint spawnCtxIdx = SpawnContextParticleMapping[threadIdx.x + particleCb.spawnContextMapOffset];
  const GpuParticle previous = Particles[particleIdx];
  const bool shouldRespawn = particleSpawnMaskCanRespawn(particleSystem.desc.spawnMaskSource,
    previous.isSpawnRejected(), previous.isDead(), previous.isSleeping(), float(previous.timeToLive));
  if (shouldRespawn)
  {
    const GpuParticle particle = respawn(particleIdx, spawnCtxIdx, particleSystem);
    Particles[particleIdx] = particle;
  }
}
