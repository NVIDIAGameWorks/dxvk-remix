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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/particles/dust_particles_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/procedural_noise.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/concept/camera/camera.slangh"
    
#define VOLUME_LIGHTING_NO_JITTER
#include "rtx/algorithm/volume_lighting.slangh"

struct VsOut
{
  float4 pos : SV_Position;
  float size : SV_PointSize;
  float4 radiance : COLOR;
  nointerpolation float3 centerPosSize : CENTER;
};

static RNG rng;

float3 randomWorldPointInFrustum()
{
  float3 random01 = float3(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng)) * float3(2..xx, 1.f) - float3(1..xx, 0.f);
  const float eps = 0.001f;
  
  // 'r' becomes cumulative volume with respect to Z (where Z is r*length of frustum)
  float r = random01.z * particleCb.frustumAreaAtZ;
  
  float part1 = particleCb.frustumB * (particleCb.frustumB * particleCb.frustumB - 6 * particleCb.frustumA * particleCb.frustumC) - 12 * particleCb.frustumA * particleCb.frustumA * r;
  float part2 = sqrt(part1 * part1 - particleCb.frustumDet * particleCb.frustumDet * particleCb.frustumDet);
  
  if (part1 < 0.0f)
  {
    part2 = -part2;
  }
  
  float part3 = part1 + part2;
  part3 = (part3 < 0.0f) ? -pow(-part3, (1.f / 3)) : pow(part3, (1.f / 3));
  
  float z = (particleCb.frustumB + particleCb.frustumDet / part3 + part3) / (2 * particleCb.frustumA);
  
  float3 viewPos = float3(lerp(particleCb.nearW, particleCb.farW, abs(z/particleCb.frustumDepth)) * random01.x, 
                          lerp(particleCb.nearH, particleCb.farH, abs(z/particleCb.frustumDepth)) * random01.y, 
                          z + particleCb.frustumMin);
  
  if(particleCb.isCameraLhs)
  {
    viewPos.z = -viewPos.z;
  }
      
  return mul(cb.camera.viewToWorld, float4(viewPos, 1.f)).xyz;
}

// Generate a new particle in-place with random properties
GpuDustParticle respawn()
{
  GpuDustParticle newParticle;
  float3 random01 = float3(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng)) * float3(2..xx, 1.f) - float3(1..xx, 0.f);
  newParticle.position = randomWorldPointInFrustum() + random01 * 100;
  newParticle.initialTimeToLive = newParticle.timeToLive = lerp(particleCb.minTtl, particleCb.maxTtl, getNextSampleBlueNoise(rng));
  newParticle.velocity = (float3(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng)) * 2 - 1) * 40;
  newParticle.size = lerp(particleCb.minParticleSize, particleCb.maxParticleSize, pow(getNextSampleBlueNoise(rng), 2));
  return newParticle;
}

bool shouldRespawn(GpuDustParticle particle)
{
  float3 relativePos = cameraGetWorldPosition(cb.camera) - particle.position;
  return particle.timeToLive <= float16_t(0.f) || dot(relativePos, relativePos) >= (particleCb.cullDistanceFromCamera * particleCb.cullDistanceFromCamera);
}

float3 evalLighting(GpuDustParticle particle)
{
  MinimalSurfaceInteraction surfaceInteraction;
  surfaceInteraction.position = particle.position;
  float3 viewDir = particle.position - cameraGetWorldPosition(cb.camera);
  
  return evalVolumetricNEE(FilteredAccumulatedRadianceY, FilteredAccumulatedRadianceCoCg, cb.volumeArgs, surfaceInteraction, normalize(viewDir), froxelVolumeMain, true, particleCb.anisotropy);
}

float calculateOpacity(GpuDustParticle particle)
{
  // Give the appearance of rotating dust particles
  float intensity = max(cos(particleCb.rotationSpeed * particle.timeToLive / particle.size) + 0.5f, 0);
  // Fade in/out across lifetime
  float x = float(saturate(particle.timeToLive/particle.initialTimeToLive)) * 2 - 1; // -1 to 1 based on lifetime
  return smoothstep(0.f, 1.f, 1.f-abs(x)) * particleCb.opacity * intensity / particle.size;
}

float3 calculateRadiance(GpuDustParticle particle)
{
  return max(0..xxx, evalLighting(particle));
}

[shader("vertex")]
VsOut main(uint vertexIdx : SV_VertexID)
{
  rng = createRNG(float2(vertexIdx / 256, vertexIdx % 256), cb.frameIdx + vertexIdx/(256 * 256));
  
  GpuDustParticle particle = Particles[vertexIdx];
  
  if(shouldRespawn(particle))
  {
    particle = respawn();
  }
  
  if(particleCb.useTurbulence)
  {
    particle.velocity += curlOfValueNoise(particle.position * particleCb.turbulenceFrequency, 0.f) * particleCb.turbulenceAmplitude * particleCb.deltaTimeSecs;
  }
  
  particle.velocity += particleCb.upDirection * particleCb.gravityForce * particleCb.deltaTimeSecs;
  
  if(dot(particle.velocity, particle.velocity) > float16_t(0.f))
  {
    particle.velocity = min(particleCb.maxSpeed, length(particle.velocity)) * normalize(particle.velocity);
  }
  
  particle.position += particle.velocity * particleCb.deltaTimeSecs;
  particle.timeToLive -= particleCb.deltaTimeSecs;
  
  Particles[vertexIdx] = particle;
  
  float4 viewPos = mul(cb.camera.worldToView, float4(particle.position, 1.f));
  float4 projPos = mul(cb.camera.viewToProjection, viewPos);
  
  projPos.y = -projPos.y;
  
  VsOut output;
  output.pos = projPos;
  output.size = particle.size;
  output.radiance = float4(calculateRadiance(particle), calculateOpacity(particle));
  output.centerPosSize = float3(projPos.xy/projPos.w * 0.5 + 0.5, particle.size);
  return output;
}
