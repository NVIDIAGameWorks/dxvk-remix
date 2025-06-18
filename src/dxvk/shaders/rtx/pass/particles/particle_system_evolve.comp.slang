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
#include "rtx/concept/camera/camera.slangh"
    
uint getSpawnContextParticleMappingIdx(uint particleIdx, const GpuParticleSystem particleSystem)
{
  return (particleIdx - particleSystem.spawnParticleOffset) % particleSystem.desc.maxNumParticles;
}

bool shouldRespawn(uint particleIdx, const GpuParticleSystem particleSystem)
{
  return particleSystem.spawnParticleCount > 0 && getSpawnContextParticleMappingIdx(particleIdx, particleSystem) < particleSystem.spawnParticleCount;
}

// Generate a new particle in-place with random properties
GpuParticle respawn(uint particleIdx, const GpuParticleSystem particleSystem)
{
  GpuParticle newParticle;
  
  // Get spawn context
  uint spawnCtxIdx = SpawnContextParticleMapping[getSpawnContextParticleMappingIdx(particleIdx, particleSystem)];
  GpuSpawnContext spawnCtx = SpawnContexts[spawnCtxIdx];
  
  // Get random triangle
  
  float3 positions[3]; 
  float16_t4 colors[3];
  float4 uvMinMax;
  
  uint randomSeed = particleIdx;
  
  uint triIdx = randomFloat(uint2(randomSeed++, particleCb.frameIdx)) * spawnCtx.numTriangles;
  
  for (uint i=0 ; i<3 ; i++)
  {
    uint indexIdx = triIdx * 3 + i;
    uint index = spawnCtx.indices32bit ? BUFFER_ARRAY(indices32, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx) : BUFFER_ARRAY(indices, (uint)spawnCtx.spawnMeshIndexIdx, indexIdx);
    
    const uint baseSrcPositionOffset = (spawnCtx.spawnMeshPositionsOffset + index * spawnCtx.spawnMeshPositionsStride) / 4;
    positions[i] = float3(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 0),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 1),
                          BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshPositionsIdx, baseSrcPositionOffset + 2));
    positions[i] = mul(spawnCtx.spawnObjectToWorld, float4(positions[i], 1.f)).xyz;
    
    const uint baseSrcColorOffset = (spawnCtx.spawnMeshColorsOffset + index * spawnCtx.spawnMeshColorsStride) / 4;
    
    if(spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID)
    {
      // VK_FORMAT_B8G8R8A8_UNORM
      const uint colorBits = floatBitsToUint(BUFFER_ARRAY(geometries, (uint) spawnCtx.spawnMeshColorsIdx, baseSrcColorOffset));
      colors[i].x = unorm8ToF16(uint8_t(colorBits >> 16));
      colors[i].y = unorm8ToF16(uint8_t(colorBits >> 8));
      colors[i].z = unorm8ToF16(uint8_t(colorBits));
      colors[i].w = unorm8ToF16(uint8_t(colorBits >> 24));
    }
    else
    {
      colors[i] = 1.h.xxxx;
    }
    
    const uint baseSrcTexcoordOffset = (spawnCtx.spawnMeshTexcoordsOffset + index * spawnCtx.spawnMeshTexcoordsStride) / 4;
    float2 texcoord = float2(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 0),
                             BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 1));
    
    if(i==0)
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
  
  // Sample random point on triangle
  
  float3 random01 = float3(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
  float3 randomBary = saturate(random01 / dot(random01, 1..xxx));
  float3 worldPosition = interpolateHitAttribute(positions, randomBary);
  float3 worldNormal = cross(normalize(positions[1] - positions[0]), normalize(positions[2] - positions[0]));
  float16_t4 color = interpolateHitAttribute(colors, randomBary);
  float16_t size = 1.f;
  
  // Variation
  
  color = particleSystem.varyColor(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), color);
  size = particleSystem.varySize(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), size);
  
  // Spawn a particle
  
  newParticle.position = worldPosition;
  newParticle.velocity = worldNormal * particleSystem.desc.initialVelocityFromNormal;
  
  newParticle.color = float4x16ToUnorm4x8(color);
  newParticle.uvMinMax = float16_t4(uvMinMax);
  newParticle.initialTimeToLive = newParticle.timeToLive = lerp(particleSystem.desc.minTtl, particleSystem.desc.maxTtl, randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
  newParticle.size = size;
  newParticle.rotation = 0;
  newParticle.rotationSpeed = lerp(0, pi, randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
  
  return newParticle;
}

void clearParticleVertices(uint particleIdx)
{
  for (uint i=0 ; i<4 ; i++)
  {
    Vertices[particleIdx * 4 + i].position = 0..xxx;
  }
}

float2 rotate2D(float2 inputVector, float radians)
{
  float s, c;
  sincos(radians, s, c);
    
  // Apply the rotation matrix
  return float2(c * inputVector.x - s * inputVector.y, s * inputVector.x + c * inputVector.y);
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const uint particleSystemIdx = args.particleSystemIdx;
  const uint particleIdx = threadIdx.x;
  
  const GpuParticleSystem particleSystem = particleCb.particleSystems[particleSystemIdx];
  
  if(particleIdx >= particleSystem.desc.maxNumParticles)
  {
    return;
  }
  
  GpuParticle particle = Particles[particleIdx];
  
  if (shouldRespawn(particleIdx, particleSystem))
  {
    particle = respawn(particleIdx, particleSystem);
  }
    
  if(particle.timeToLive <= 0.f)
  {
    clearParticleVertices(particleIdx);
    return;
  }
  
  if (particleSystem.desc.useTurbulence)
  {
    particle.velocity += curlOfValueNoise(particle.position * particleSystem.desc.turbulenceFrequency, particleCb.absoluteTimeSecs * particleSystem.desc.turbulenceFrequency) * particleSystem.desc.turbulenceAmplitude * particleCb.deltaTimeSecs;
  }
  
  particle.velocity += particleCb.upDirection * particleSystem.desc.gravityForce * particleCb.deltaTimeSecs;
  
  if (dot(particle.velocity, particle.velocity) > float16_t(0.f))
  {
    particle.velocity = min(particleSystem.desc.maxSpeed, length(particle.velocity)) * normalize(particle.velocity); 
  }
  
  particle.position += particle.velocity * particleCb.deltaTimeSecs;
  particle.rotation = (particle.rotation + particle.rotationSpeed * particleCb.deltaTimeSecs) % twoPi;
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  Particles[particleIdx] = particle;
  
  const float3 right = particleCb.worldToView[0].xyz;
  const float3 up = particleCb.worldToView[1].xyz;
  
  static const float2 offsets[4] =
  {
    float2(-0.5f, -0.5f),
    float2(-0.5f,  0.5f),
    float2( 0.5f, -0.5f),
    float2( 0.5f,  0.5f)
  };
  
  float16_t4 color = float16_t4(unorm8ToF16(uint8_t(particle.color)),
                                unorm8ToF16(uint8_t(particle.color >> 8)),
                                unorm8ToF16(uint8_t(particle.color >> 16)),
                                unorm8ToF16(uint8_t(particle.color >> 24)));
  
  color.w *= particleSystem.calculateOpacity(saturate(particle.timeToLive / particle.initialTimeToLive));
  
  uint colorEnc = float4x16ToUnorm4x8(color.zyxw);
  
  float size = particle.size * saturate(particle.timeToLive/particle.initialTimeToLive);
  
  [unroll]
  for (uint i=0 ; i<4 ; i++)
  {
    ParticleVertex vertex = Vertices[particleIdx * 4 + i];
    
    float2 offset = rotate2D(offsets[i], particle.rotation);
    vertex.position = particle.position + (offset.x* right + offset.y * up) * size;
    vertex.color = colorEnc;
    vertex.texcoord = offsets[i] + 0.5.xx;
    
    Vertices[particleIdx * 4 + i] = vertex;
  }
}
