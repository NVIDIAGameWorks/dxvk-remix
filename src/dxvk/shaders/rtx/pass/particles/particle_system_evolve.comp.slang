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
    
uint getSpawnContextParticleMappingIdx(uint particleIdx, const GpuParticleSystem particleSystem)
{
  return (particleIdx - particleSystem.spawnParticleOffset) % particleSystem.desc.maxNumParticles;
}

bool shouldRespawn(uint particleIdx, const GpuParticleSystem particleSystem)
{
  return particleSystem.spawnParticleCount > 0 && getSpawnContextParticleMappingIdx(particleIdx, particleSystem) < particleSystem.spawnParticleCount;
}

f16vec3 sampleDirectionInCone(f16vec3 axis, float cosConeHalfAngle, float2 rndUV)
{
  // Sample a direction in a cone about +Z, and get its PDF (we ignore PDF here)
  float solidAnglePdf;
  f16vec3 localDir = calcUniformConeDirectionSample(f16vec2(rndUV), cosConeHalfAngle, solidAnglePdf);

  f16vec3 tangent, bitangent;
  calcOrthonormalBasis(axis, tangent, bitangent);

  // Rotate the local-Z sample into the world-axis frame
  return localDir.x * tangent + localDir.y * bitangent + localDir.z * axis;
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
  float3 prevPositions[3];
  float16_t4 color = (spawnCtx.spawnMeshColorsIdx != BINDING_INDEX_INVALID) ? 0.h : 1.h;
  float4 uvMinMax = float4(0..xx, 1..xx);
  
  uint randomSeed = particleIdx;
    
  float3 random01 = float3(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx)));
  float3 randomBary = saturate(random01 / dot(random01, 1..xxx));
  
  uint triIdx = randomFloat(uint2(randomSeed++, particleCb.frameIdx)) * spawnCtx.numTriangles;
  
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
      color += unorm4x8ToFloat4x16(colorBits).zyxw * randomBary[i];
    }
    
    const uint baseSrcTexcoordOffset = (spawnCtx.spawnMeshTexcoordsOffset + index * spawnCtx.spawnMeshTexcoordsStride) / 4;
    float2 texcoord = float2(BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 0),
                             BUFFER_ARRAY(geometries, (uint)spawnCtx.spawnMeshTexcoordsIdx, baseSrcTexcoordOffset + 1));
    
    if (particleSystem.desc.useSpawnTexcoords)
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
  
  // Sample random point on triangle
  const float randSeed = randomFloat(uint2(randomSeed++, particleCb.frameIdx));

  float3 prevWorldPosition = mul(spawnCtx.spawnPrevObjectToWorld, float4(interpolateHitAttribute(prevPositions, randomBary), 1.f)).xyz;
  float3 worldPosition = mul(spawnCtx.spawnObjectToWorld, float4(interpolateHitAttribute(positions, randomBary), 1.f));
  
  float3 worldSpawnVelocity = (worldPosition - prevWorldPosition) * particleCb.invDeltaTimeSecs;
  
  float3 localNormal = cross(normalize(positions[1] - positions[0]), normalize(positions[2] - positions[0]));
  float3 worldNormal = mul(spawnCtx.spawnObjectToWorld, float4(localNormal, 0.f));
  
  float cosCone = cos(radians(particleSystem.desc.initialVelocityConeAngleDegrees));
  f16vec3 dir = sampleDirectionInCone(worldNormal, cosCone, float2(randomFloat(uint2(randomSeed++, particleCb.frameIdx)), randomFloat(uint2(randomSeed++, particleCb.frameIdx))));
  
  float3 initialVelocity = worldSpawnVelocity * particleSystem.desc.initialVelocityFromMotion + dir * particleSystem.desc.initialVelocityFromNormal;
  
  float3 spawnPosition = lerp(worldPosition, prevWorldPosition, randSeed);
  
  // Spawn a particle
  newParticle.reset(particleSystem, spawnPosition, initialVelocity, uvMinMax, color, randSeed);

  return newParticle;
}

void clearParticleVertices(uint particleIdx, const GpuParticleSystem particleSystem)
{
  [unroll]
  for (uint i=0 ; i<particleSystem.numVerticesPerParticle ; i++)
  {
    Vertices[particleIdx * particleSystem.numVerticesPerParticle + i].position = 0..xxx;
  }
}

float2 calcBillboardSinCos(float3 velocity, float4x4 worldToView)
{
  float len = length(velocity);
  if (len < 1e-6f) {
    return float2(0, 1);
  }
  float3 v = velocity / len;

  float3 camRight = worldToView[0].xyz;
  float3 camUp    = worldToView[1].xyz;

  // screenspace velocity
  float2 vel2D = float2(dot(v, camRight), -dot(v, camUp));
  vel2D = normalize(vel2D);

  return vel2D;
}

// small bias to avoid re-penetration
static const float kCollisionBias = 1e-3f;

void collideParticleWithScene(inout GpuParticle p)
{
  // Reproject current world-space pos into last frame's clip
  float4 prevClip = mul(particleCb.prevWorldToProjection, float4(p.position, 1.0f));
  float2 prevNdc  = prevClip.xy / prevClip.w;
  float2 prevUv   = prevNdc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

  // early-out if outside [0,1] UV
  if (prevUv.x < 0.0f || prevUv.x > 1.0f || prevUv.y < 0.0f || prevUv.y > 1.0f)
  {
    return;
  }

  // compute pixel coords in your depth/normal buffer
  uint2 pixel = uint2(
    prevUv.x * particleCb.renderingWidth  + 0.5f,
    prevUv.y * particleCb.renderingHeight + 0.5f
  );

  // fetch the MinimalSurfaceInteraction from last frame
  MinimalSurfaceInteraction si = minimalSurfaceInteractionReadFromGBuffer(pixel, PreviousWorldPosition_WorldTriangleNormal);

  if (!si.isValid)
  {
    return;
  }
  
  // unpack, normalize
  float3 n = normalize(float3(si.triangleNormal));
  
  float2 screenVelocity = PreviousPrimaryScreenSpaceMotionVector[pixel];
  
  // fetch the MinimalSurfaceInteraction from 2 frames ago
  MinimalSurfaceInteraction si2 = minimalSurfaceInteractionReadFromGBuffer(pixel + screenVelocity, PreviousWorldPosition_WorldTriangleNormal);

  float3 sceneVel = 0..xxx;
  if (si2.isValid)
  {
    float3 relativePos = (si.position - si2.position);
    sceneVel = relativePos;
  }
  
  float3 relVel = p.velocity - sceneVel;
  if (dot(relVel, n) >= 0.0f)
  {
    return;
  }

  // signed distance along that normal
  float signedDist = dot(p.position - si.position, n);

  // only collide if we've gone inside but not deeper than thickness
  float thickness = particleCb.particleSystem.desc.collisionThickness;
  if (signedDist < 0.0f && signedDist > -thickness)
  {
    // reflect & apply restitution
    float3 vRef = reflect(relVel, n);
    p.velocity = vRef * particleCb.particleSystem.desc.collisionRestitution + sceneVel;
  }
}


[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const uint particleIdx = threadIdx.x;
  
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
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
    clearParticleVertices(particleIdx, particleSystem);
    return;
  }
  
  if (particleSystem.desc.useTurbulence)
  {
    particle.velocity += curlOfValueNoise(particle.position * particleSystem.desc.turbulenceFrequency, particleCb.absoluteTimeSecs * particleSystem.desc.turbulenceFrequency) * particleSystem.desc.turbulenceForce * particleCb.deltaTimeSecs;
  }
  
  particle.velocity += particleCb.upDirection * particleSystem.desc.gravityForce * particleCb.deltaTimeSecs;
  
  if (particleSystem.desc.enableCollisionDetection)
  {
    collideParticleWithScene(particle);
  }
  
  if (dot(particle.velocity, particle.velocity) > float16_t(0.f) && particleSystem.desc.maxSpeed >= 0)
  {
    particle.velocity = min(particleSystem.desc.maxSpeed, length(particle.velocity)) * normalize(particle.velocity); 
  }
  
  particle.position += particle.velocity * particleCb.deltaTimeSecs;
  
  float2x2 rot2D = float2x2(1.f, 0.f, 0.f, 1.f);
  if (!particleSystem.desc.enableMotionTrail)
  {
    if (particleSystem.desc.alignParticlesToVelocity)
    {
      particle.rotation = 0.h;
    
      float2 sc = calcBillboardSinCos(particle.velocity, particleCb.worldToView);
      rot2D = float2x2(sc.y, -sc.x, sc.x, sc.y);
    }
    else
    {
      particle.rotation = (particle.rotation + particle.rotationSpeed(particleSystem) * particleCb.deltaTimeSecs) % twoPi;
    
      float s, c;
      sincos(particle.rotation, s, c);
      rot2D = float2x2(c, -s, s, c);
    }
  }
  
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  Particles[particleIdx] = particle;
  
  const float3 right = particleCb.worldToView[0].xyz;
  const float3 up = particleCb.worldToView[1].xyz;
  
  // precache some values
  const float size = particle.size(particleSystem);
  const uint colorEnc = float4x16ToUnorm4x8(particle.color(particleSystem).zyxw);
  
  [unroll]
  for (uint i=0 ; i<particleSystem.numVerticesPerParticle ; i++)
  {
    ParticleVertex vertex;
    
    float2 offset = particleSystem.particleVertexOffsets[i];

    float3 worldOffset;
    if (particleSystem.desc.enableMotionTrail)
    {
      // project velocity into billboard plane
      float2 motionPlane = -float2(dot(particle.velocity, right), dot(particle.velocity, up));
      float speed = length(motionPlane);

      // forward axis & width axis
      float2 motionDir = speed > 0 ? motionPlane / speed : float2(0, 1);
      float2 orthoDir  = float2(-motionDir.y, motionDir.x);

      float width = offset.x * size;

      // indices 0..3 -> tail
      // indices 4..7 -> head
      float len = (i >= 4) ? (speed * particleSystem.desc.motionTrailMultiplier * particleCb.deltaTimeSecs) : 0;                                       
      
      float height = offset.y * size + len;
      
      // reconstruct world space offset
      float2 planeOffset = width * orthoDir + height * motionDir;
      worldOffset = planeOffset.x * right + planeOffset.y * up;
    }
    else
    {
      // rotate sprite in 2d
      float2 localOffset = mul(rot2D, offset);
      
      // classic billboard
      worldOffset = (localOffset.x * right + localOffset.y * up) * size;
    }
    
    vertex.position = particle.position + worldOffset;
    vertex.color = colorEnc;
    vertex.texcoord = (offset + 0.5f) * (particle.uvMinMax.zw - particle.uvMinMax.xy) + particle.uvMinMax.xy;
    
    Vertices[particleIdx * particleSystem.numVerticesPerParticle + i] = vertex;
  }
}
