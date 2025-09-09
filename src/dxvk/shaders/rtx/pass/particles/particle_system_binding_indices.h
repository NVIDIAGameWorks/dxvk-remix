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
#pragma once

#include "rtx/pass/common_binding_indices.h"
#include "rtx/utility/shader_types.h"
#include "particle_system_common.h"

const static float kMinimumParticleLife = 1.f / 30; // assume this so we dont have spawned particles that live shorter than a frame

struct GpuParticle {
  vec3 position;
  uint enBaseColor;

  vec3 velocity;
  float randSeed;

  f16vec4 uvMinMax;
  half rotation;
  half timeToLive;
  half pad0;
  half pad1;

  const static uint16_t kDeadTimeToLiveSentinel = 0x7C00;

#ifdef __cplusplus
  // This ensures that `timeToLive` is initialized with the correct value.
  static const uint kBufferClearValue = (kDeadTimeToLiveSentinel << 16) | kDeadTimeToLiveSentinel;
#else
  [mutating]
  void reset(GpuParticleSystem system, float3 worldPosition, float3 worldVelocity, f16vec4 _uvMinMax, f16vec4 color, float seed) {
    randSeed = seed;
    rotation = 0.h;
    timeToLive = initialTimeToLive(system);
    enBaseColor = float4x16ToUnorm4x8(color);
    position = worldPosition;
    velocity = worldVelocity;
    uvMinMax = _uvMinMax;
  }

  [mutating]
  void setDead() {
    timeToLive = reinterpret<half>(kDeadTimeToLiveSentinel);
  }

  bool isDead() {
    return timeToLive == reinterpret<half>(kDeadTimeToLiveSentinel);
  }

  bool isSleeping() {
    return timeToLive <= 0;
  }

  half initialTimeToLive(GpuParticleSystem system) { 
    return max(kMinimumParticleLife, system.varyTimeToLive(randSeed));
  }

  f16vec4 color(GpuParticleSystem system) {
    return unorm4x8ToFloat4x16(enBaseColor) * lerp(targetColor(system), spawnColor(system), normalizedLife(system));
  }

  half rotationSpeed(GpuParticleSystem system) {
    return lerp(targetRotationSpeed(system), spawnRotationSpeed(system), normalizedLife(system));
  }

  half size(GpuParticleSystem system) {
    return lerp(targetSize(system), spawnSize(system), normalizedLife(system));
  }

  half spawnRotationSpeed(GpuParticleSystem system) { 
    return system.varySpawnRotationSpeed(randSeed);
  }
  half targetRotationSpeed(GpuParticleSystem system) { 
    return system.varyTargetRotationSpeed(randSeed);
  }
  
  half spawnSize(GpuParticleSystem system) {
    return system.varySpawnSize(randSeed);
  }
  half targetSize(GpuParticleSystem system) {
    return system.varyTargetSize(randSeed);
  }

  f16vec4 spawnColor(GpuParticleSystem system) {
    return system.varySpawnColor(randSeed);
  }
  f16vec4 targetColor(GpuParticleSystem system) {
    return system.varyTargetColor(randSeed);
  }

  // Gradually moves from 1 when born to 0 when dead over lifetime
  half normalizedLife(GpuParticleSystem system) {
    return timeToLive / initialTimeToLive(system);
  }
#endif
};

struct ParticleVertex {
  vec3 position;
  uint color;
  vec2 texcoord;
};


#define PARTICLE_SYSTEM_BINDING_CONSTANTS                              50
#define PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT   51
#define PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT                   52
#define PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT              53
#define PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT 54
#define PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT                 55

#define PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT  60
#define PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT           61
#define PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT                 62

#define PARTICLE_SYSTEM_MIN_BINDING                           PARTICLE_SYSTEM_BINDING_CONSTANTS

#if PARTICLE_SYSTEM_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of dust particles bindings to avoid overlap with common bindings!"
#endif
