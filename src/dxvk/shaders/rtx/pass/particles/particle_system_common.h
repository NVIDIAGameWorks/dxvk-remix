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


struct GpuSpawnContext {
  mat4x3 spawnObjectToWorld;

  uint spawnMeshPositionsOffset;
  uint spawnMeshColorsOffset;
  uint spawnMeshTexcoordsOffset;
  uint numTriangles;

  uint16_t spawnMeshPositionsStride;
  uint16_t spawnMeshColorsStride;
  uint16_t spawnMeshTexcoordsStride;
  uint16_t indices32bit;
  uint16_t spawnMeshPositionsIdx;
  uint16_t spawnMeshColorsIdx;
  uint16_t spawnMeshIndexIdx;
  uint16_t spawnMeshTexcoordsIdx;
};

struct RtxParticleSystemDesc { 
  vec4 minSpawnColor;

  vec4 maxSpawnColor;

  float minTtl;
  float maxTtl;
  float initialVelocityFromNormal;
  float initialVelocityConeAngleDegrees;

  float minParticleSize;
  float maxParticleSize;
  float gravityForce;
  float maxSpeed;

  float turbulenceFrequency;
  float turbulenceAmplitude;
  uint maxNumParticles;
  float minRotationSpeed;

  float maxRotationSpeed;
  float spawnRate;
  float collisionThickness;
  uint8_t useTurbulence;
  uint8_t alignParticlesToVelocity;
  uint8_t useSpawnTexcoords;
  uint8_t enableCollisionDetection;

  float collisionRestitution;
  uint enableMotionTrail;
  float motionTrailMultiplier;
  uint hideEmitter;

#ifdef __cplusplus
  RtxParticleSystemDesc() {
    // This struct can be hashed so ensure its initialized
    memset(this, 0, sizeof(*this));
  }

  XXH64_hash_t calcHash() const {
    return XXH3_64bits(this, sizeof(*this));
  }
#endif
};

struct GpuParticleSystem { 
  RtxParticleSystemDesc desc; // TODO: Can compress this further.

  // These members aren't hashed
  float2 particleVertexOffsets[8];

  uint spawnParticleOffset = 0;
  uint spawnParticleCount = 0;
  uint numVerticesPerParticle = 4;
  uint pad1;

#ifndef __cplusplus
  float16_t4 varyColor(float rand, float16_t4 color) {
    return float16_t4(saturate(lerp(desc.minSpawnColor, desc.maxSpawnColor, rand))) * color;
  }

  float16_t varySize(float rand, float16_t size) {
    float16_t rand = lerp(desc.minParticleSize, desc.maxParticleSize, rand);
    return rand * size;
  }

  float16_t varyRotationSpeed(float rand) {
    return lerp(desc.minRotationSpeed, desc.maxRotationSpeed, rand) * twoPi;
  }

  float calculateOpacity(float particleNormalizedLife) {
    float x = particleNormalizedLife * 2 - 1; // -1 to 1 based on lifetime
    return smoothstep(0.f, 1.f, 1.f - abs(x));
  }

#else
  GpuParticleSystem() = default;
  GpuParticleSystem(const GpuParticleSystem& other) = default;
  explicit GpuParticleSystem(const RtxParticleSystemDesc& desc)
    : desc(desc) { 
  }
#endif
};

struct ParticleSystemConstants {
  GpuParticleSystem particleSystem;

  mat4 worldToView;

  mat4 viewToWorld;

  mat4 prevWorldToProjection;

  vec3 upDirection;
  float deltaTimeSecs;

  float absoluteTimeSecs;
  float invDeltaTimeSecs;
  uint frameIdx;
  uint16_t renderingWidth;
  uint16_t renderingHeight;
};
