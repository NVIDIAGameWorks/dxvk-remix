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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include "rtx/pass/particles/particle_system_binding_indices.h"

#include "rtx/utility/common.slangh"
#include "rtx/utility/packing.slangh"

layout(push_constant)
ConstantBuffer<ParticleSystemPopulateObjectPickingArgs> cb;

layout(binding = PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT)
StructuredBuffer<GpuParticle> Particles;

layout(binding = PARTICLE_SYSTEM_BINDING_OBJECT_PICKING_OUTPUT)
RWStructuredBuffer<uint> ObjectPickingValues;

// Write the particle ids in the draw order of the last geometry pass
[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint2 threadIdx : SV_DispatchThreadID)
{
  if (threadIdx.x >= cb.particleCount)
  {
    return;
  }

  const uint particleIdx = (threadIdx.x + cb.particleTailOffset) % cb.maxNumParticles;

  ObjectPickingValues[threadIdx.x] = Particles[particleIdx].emitterObjectPickingValue;
}
