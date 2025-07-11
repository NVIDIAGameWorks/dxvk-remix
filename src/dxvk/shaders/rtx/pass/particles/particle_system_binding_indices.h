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

struct GpuParticle {
  vec3 position;
  uint color;

  vec3 velocity;
  half timeToLive;
  half initialTimeToLive;

  f16vec4 uvMinMax;
  half size;
  half rotation;
  half rotationSpeed;
  half pad0;
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

#define PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT  60
#define PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT       61

#define PARTICLE_SYSTEM_MIN_BINDING                           PARTICLE_SYSTEM_BINDING_CONSTANTS

#if PARTICLE_SYSTEM_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of dust particles bindings to avoid overlap with common bindings!"
#endif
