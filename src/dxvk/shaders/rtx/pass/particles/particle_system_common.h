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
  mat4x3 spawnPrevObjectToWorld;

  uint spawnMeshPositionsOffset;
  uint spawnMeshColorsOffset;
  uint spawnMeshTexcoordsOffset;
  uint numTriangles : 31;
  uint indices32bit : 1;

  uint16_t spawnMeshPositionsStride;
  uint16_t spawnMeshColorsStride;
  uint16_t spawnMeshTexcoordsStride;
  uint16_t spawnMeshPositionsIdx;

  uint16_t spawnMeshPrevPositionsIdx;
  uint16_t spawnMeshColorsIdx;
  uint16_t spawnMeshIndexIdx;
  uint16_t spawnMeshTexcoordsIdx;
};

struct RtxParticleSystemDesc { 
  vec4 minSpawnColor;

  vec4 maxSpawnColor;

  vec4 minTargetColor;

  vec4 maxTargetColor;

  float minTargetSize;
  float maxTargetSize;
  float minTargetRotationSpeed;
  float maxTargetRotationSpeed;

  float minTtl;
  float maxTtl;
  float initialVelocityFromNormal;
  float initialVelocityConeAngleDegrees;

  float minSpawnSize;
  float maxSpawnSize;
  float gravityForce;
  float maxSpeed;

  float turbulenceFrequency;
  float turbulenceForce;
  float motionTrailMultiplier;
  float minSpawnRotationSpeed;

  float maxSpawnRotationSpeed;
  float spawnRate;
  float collisionThickness;
  float collisionRestitution;
  

  float initialVelocityFromMotion;
  uint maxNumParticles;
  uint16_t hideEmitter;
  uint16_t enableMotionTrail;
  uint8_t useTurbulence;
  uint8_t alignParticlesToVelocity;
  uint8_t useSpawnTexcoords;
  uint8_t enableCollisionDetection;

#ifdef __cplusplus
  RtxParticleSystemDesc() {
    // This struct can be hashed so ensure its initialized
    memset(this, 0, sizeof(*this));
  }

  XXH64_hash_t calcHash() const {
    return XXH3_64bits(this, sizeof(*this));
  }

  void applySceneScale(const float centimetersToUnits) {
    // These params are in centimeters
    minTargetSize *= centimetersToUnits;
    maxTargetSize *= centimetersToUnits;
    minSpawnSize *= centimetersToUnits;
    maxSpawnSize *= centimetersToUnits;
    collisionThickness *= centimetersToUnits;
    gravityForce *= centimetersToUnits;
    initialVelocityFromNormal *= centimetersToUnits;
    maxSpeed *= centimetersToUnits;
    turbulenceForce *= centimetersToUnits;
    turbulenceFrequency *= centimetersToUnits;
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
  uint particleTailOffset = 0;

  uint2 pad;
  uint particleHeadOffset = 0;
  uint particleCount = 0;

#ifndef __cplusplus
  float16_t varyTimeToLive(float rand) {
    return lerp(desc.minTtl, desc.maxTtl, rand);
  }

  float16_t4 varySpawnColor(float rand) {
    return float16_t4(lerp(desc.minSpawnColor, desc.maxSpawnColor, rand));
  }

  float16_t varySpawnSize(float rand) {
    return lerp(desc.minSpawnSize, desc.maxSpawnSize, rand);
  }

  float16_t varySpawnRotationSpeed(float rand) {
    return lerp(desc.minSpawnRotationSpeed, desc.maxSpawnRotationSpeed, rand) * twoPi;
  }

  float16_t4 varyTargetColor(float rand) {
    return float16_t4(lerp(desc.minTargetColor, desc.maxTargetColor, rand));
  }

  float16_t varyTargetSize(float rand) {
    return lerp(desc.minTargetSize, desc.maxTargetSize, rand);
}

  float16_t varyTargetRotationSpeed(float rand) {
    return lerp(desc.minTargetRotationSpeed, desc.maxTargetRotationSpeed, rand) * twoPi;
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

  float resolveTransparencyThreshold;
  float minParticleSize;
  uint pad1;
  uint pad2;
};
