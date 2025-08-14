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
  
  float2x2 rot2D = float2x2(1.f, 0.f, 0.f, 1.f);
  if (!particleSystem.desc.enableMotionTrail)
  {
    if (particleSystem.desc.alignParticlesToVelocity)
    {
      float2 sc = calcBillboardSinCos(particle.velocity, particleCb.worldToView);
      rot2D = float2x2(sc.y, -sc.x, sc.x, sc.y);
    }
    else
    {
      float s, c;
      sincos(particle.rotation, s, c);
      rot2D = float2x2(c, -s, s, c);
    }
  }
  
  const float3 right = particleCb.worldToView[0].xyz;
  const float3 up = particleCb.worldToView[1].xyz;
  
  const uint colorEnc = float4x16ToUnorm4x8(color.zyxw);
  
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
    
    vertex.position = chopMantissaLSB(particle.position + worldOffset, 4); // lop off some LSB to get better compression;
    vertex.color = colorEnc;
    vertex.texcoord = (offset + 0.5f) * (particle.uvMinMax.zw - particle.uvMinMax.xy) + particle.uvMinMax.xy;
    
    // Always keep active particle vertices in the 0 ~ N range of buffer (unlike particles themselves)
    Vertices[threadIdx.x * particleSystem.numVerticesPerParticle + i] = vertex;
  }
}
