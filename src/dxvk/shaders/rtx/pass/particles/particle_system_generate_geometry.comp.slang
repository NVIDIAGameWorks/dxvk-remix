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

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT)
StructuredBuffer<GpuParticle> Particles;

layout(binding = PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT)
RWStructuredBuffer<ParticleVertex> Vertices;

void clearParticleVertices(uint particleIdx, const GpuParticleSystem particleSystem)
{
  [unroll]
  for (uint i=0 ; i<particleSystem.numVerticesPerParticle ; i++)
  {
    Vertices[particleIdx * particleSystem.numVerticesPerParticle + i].position = 0..xxx;
  }
}

float2 calcBillboardSinCos(float3 velocity, float3 camRight, float3 camUp)
{
  float len = length(velocity);
  if (len < 1e-6f) {
    return float2(0, 1);
  }
  float3 v = velocity / len;

  // screenspace velocity
  float2 vel2D = float2(dot(v, camRight), -dot(v, camUp));
  vel2D = normalize(vel2D);

  return vel2D;
}

vector<float, N> chopMantissaLSB<let N: int>(vector<float, N> value, int numLSB) 
{
  vector<uint, N> data = asuint(value);

  // The mantissa occupies the lower 23 bits.
  const uint32_t fraction_mask = 0x7FFFFF; 

  uint clearMask = ~((1u << numLSB) - 1) & fraction_mask;

  // Keep the original sign and exponent (upper N bits) intact,
  // and chop the LSB bits of the fraction.
  data = (data & ~fraction_mask) | ((data & fraction_mask) & clearMask);

  return asfloat(data);
}

// raw projection of v onto plane with normal n (no normalize)
float3 projectOntoPlane(float3 v, float3 n) 
{
  return v - n * dot(v, n);
}

// normalized projection with fallback if nearly parallel to n
float3 projectOntoPlaneDir(float3 v, float3 n, float3 fallbackDir) 
{
  float3 p = projectOntoPlane(v, n);
  float l2 = dot(p, p);
  return (l2 > 1e-12f) ? p * rsqrt(l2) : fallbackDir;
}

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  const GpuParticleSystem particleSystem = particleCb.particleSystem;
  
  if(threadIdx.x >= particleSystem.particleCount)
  {
    return;
  }
  
  const uint particleIdx = (threadIdx.x + particleSystem.particleTailOffset) % particleSystem.desc.maxNumParticles;
  
  const GpuParticle particle = Particles[particleIdx];
  
  // precache some values
  const float size = particle.size(particleSystem);
  const float16_t4 color = particle.color(particleSystem);
  
  if(particle.isDead() || particle.isSleeping() || color.w < particleCb.resolveTransparencyThreshold || size <= particleCb.minParticleSize)
  {
    clearParticleVertices(threadIdx.x, particleSystem);
    return;
  }
  
  // camera basis and world up
  const float3 camRightWS = mul(particleCb.viewToWorld, float4(1, 0, 0, 0)).xyz;
  const float3 camUpWS    = mul(particleCb.viewToWorld, float4(0, 1, 0, 0)).xyz;
  const float3 camPosWS   = mul(particleCb.viewToWorld, float4(0, 0, 0, 1)).xyz;
  
  const float3 worldUp = safeNormalize(particleCb.upDirection, float3(0,1,0));

  float3 basisRight, basisUp;
  switch (particleSystem.desc.billboardType)
  {
    default:
    case ParticleBillboardType::FaceCamera_Spherical:
    {
      // classic camera-facing
      basisRight = camRightWS;
      basisUp    = camUpWS;
    } break;

    case ParticleBillboardType::FaceCamera_UpAxisLocked:
    {
      // face camera around worldUp: project view-dir to this particle onto the up-plane
      float3 toCamDir = safeNormalize(camPosWS - particle.position, float3(0,0,1));
      float3 fPlane   = projectOntoPlaneDir(toCamDir, worldUp, float3(0,0,1));
      basisRight      = safeNormalize(cross(fPlane, worldUp), float3(1,0,0));
      basisUp         = worldUp;
    } break;

    case ParticleBillboardType::FaceCamera_Position:
    {
      // normal = camera->particle; resolve roll by projecting worldUp into that plane
      float3 n         = safeNormalize(particle.position - camPosWS, float3(0,0,1));
      float3 upInPlane = projectOntoPlaneDir(worldUp, n, float3(0,0,1));
      basisRight       = safeNormalize(cross(upInPlane, n), float3(1,0,0));
      basisUp          = upInPlane;
    } break;

    case ParticleBillboardType::FaceWorldUp:
    {
      // choose a stable world reference and make a full in-plane basis
      float3 ref      = (abs(dot(float3(0,0,1), worldUp)) < 0.98f) ? float3(0,0,1) : float3(1,0,0);
      float3 xAxis    = projectOntoPlaneDir(ref, worldUp, float3(1,0,0)); // in-plane X
      float3 yAxis    = safeNormalize(cross(worldUp, xAxis), float3(0,0,1)); // in-plane Y
      basisRight      = xAxis;
      basisUp         = yAxis;
    } break;
  }
  
  float2x2 rot2D = float2x2(1.f, 0.f, 0.f, 1.f);
  if (particleSystem.desc.alignParticlesToVelocity)
  {
    float2 sc = calcBillboardSinCos(particle.velocity, basisRight, basisUp);
    rot2D = float2x2(sc.y, -sc.x, sc.x, sc.y);
  }
  else
  {
    float s, c;
    sincos(particle.rotation, s, c);
    rot2D = float2x2(c, -s, s, c);
  }
  
  const uint colorEnc = float4x16ToUnorm4x8(color.zyxw);
  
  [unroll]
  for (uint i = 0; i < particleSystem.numVerticesPerParticle; i++) 
  {
    ParticleVertex vertex;
    
    float2 offset = particleSystem.particleVertexOffsets[i];
    float3 worldOffset = 0;
    
    if (particleSystem.desc.enableMotionTrail) 
    {
      // project velocity into billboard plane
      float2 motionPlane = -float2(dot(particle.velocity, basisRight), dot(particle.velocity, basisUp));
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
      worldOffset = planeOffset.x * basisRight + planeOffset.y * basisUp;
    } 
    else 
    {
      const float2 localOffset = mul(rot2D, offset);
      worldOffset = (localOffset.x * basisRight + localOffset.y * basisUp) * size;
    }
    
    vertex.position = chopMantissaLSB(particle.position + worldOffset, 4); // lop off some LSB to get better compression;
    vertex.color = colorEnc;
    vertex.texcoord = (offset + 0.5f) * (particle.uvMinMax.zw - particle.uvMinMax.xy) + particle.uvMinMax.xy;
    
    // Always keep active particle vertices in the 0 ~ N range of buffer (unlike particles themselves)
    Vertices[threadIdx.x * particleSystem.numVerticesPerParticle + i] = vertex;
  }
}
