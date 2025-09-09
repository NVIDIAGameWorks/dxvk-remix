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
    
layout(binding = PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT)
Texture2D<float4> PreviousWorldPosition_WorldTriangleNormal;

layout(binding = PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
Texture2D<float2> PreviousPrimaryScreenSpaceMotionVector;

// Outputs

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
RWStructuredBuffer<GpuParticle> Particles;

layout(binding = PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT)
RWStructuredBuffer<int> Counter;

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

// Updates a particles state for one time step
void evolve(inout GpuParticle particle, const GpuParticleSystem particleSystem)
{
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
  
  if (!particleSystem.desc.enableMotionTrail)
  {
    if (particleSystem.desc.alignParticlesToVelocity)
    {
      particle.rotation = 0.h;
    }
    else
    {
      particle.rotation = (particle.rotation + particle.rotationSpeed(particleSystem) * particleCb.deltaTimeSecs) % twoPi;
    }
  }
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if (threadIdx.x >= particleSystem.simulateParticleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.particleTailOffset) % particleSystem.desc.maxNumParticles;
  
  GpuParticle particle = Particles[particleIdx];
  
  if (!WaveActiveAllTrue(particle.isSleeping() || particle.isDead()))
  {
    // Save cycles if there are whole warps of sleeping particles
    evolve(particle, particleSystem);
  }
  
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  // Our counting strategy is conservative, therefore, we must also account for the non-random, max lifetime
  float targetLifetimeForCounter = (particle.initialTimeToLive(particleSystem) - particleSystem.desc.maxTtl);
  if (particle.timeToLive <= targetLifetimeForCounter && !particle.isDead()) 
  {
    particle.setDead();
    
    // Decrease the running count for this frame by 1
    InterlockedAdd(Counter[0], -1);
  }
  
  Particles[particleIdx] = particle;
}
