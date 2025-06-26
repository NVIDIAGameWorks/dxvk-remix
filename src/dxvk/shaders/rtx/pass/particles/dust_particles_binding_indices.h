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

struct GpuDustParticle {
  vec3 position;
  uint color;

  f16vec3 velocity;
  half timeToLive;
  f16vec2 texcoord;
  half size;
  half initialTimeToLive;
};

struct DustParticleSystemConstants {
  vec3 upDirection;
  float minTtl;

  uint2 renderResolution;
  float maxTtl;
  float opacity;

  float cullDistanceFromCamera;
  float minParticleSize;
  float maxParticleSize;
  float frustumAreaAtZ;

  float maxSpeed;
  float turbulenceFrequency;
  float turbulenceAmplitude;
  float rotationSpeed;

  float deltaTimeSecs;
  uint useTurbulence;
  float gravityForce;
  float frustumDepth;

  float frustumMin;
  float frustumA;
  float frustumB;
  float frustumC;

  float frustumDet;
  float nearH;
  float nearW;
  float farW;

  float farH;
  float isCameraLhs;
  float anisotropy;
  uint pad2;
};

// Inputs

#define DUST_PARTICLES_BINDING_FILTERED_RADIANCE_Y_INPUT 40
#define DUST_PARTICLES_BINDING_DEPTH_INPUT 41
#define DUST_PARTICLES_BINDING_FILTERED_RADIANCE_CO_CG_INPUT 42

// Outputs

#define DUST_PARTICLES_BINDING_PARTICLES_BUFFER_INOUT   50

#define DUST_PARTICLES_MIN_BINDING                           DUST_PARTICLES_BINDING_FILTERED_RADIANCE_Y_INPUT
#define DUST_PARTICLES_MAX_BINDING                           DUST_PARTICLES_BINDING_PARTICLES_BUFFER_INOUT

#if DUST_PARTICLES_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of dust particles bindings to avoid overlap with common bindings!"
#endif
