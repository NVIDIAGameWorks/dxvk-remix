/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/external/NRC.h"
#include "nrc/include/NrcStructures.h"

// Inputs

#define NRC_RESOLVE_BINDING_NRC_QUERY_PATH_INFO_INPUT                            0
#define NRC_RESOLVE_BINDING_NRC_QUERY_RADIANCE_INPUT                             1
#define NRC_RESOLVE_BINDING_NRC_TRAINING_PATH_INFO_INPUT                         2

#define NRC_RESOLVE_BINDING_SHARED_FLAGS_INPUT                                   5
#define NRC_RESOLVE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT                 6

#define NRC_RESOLVE_BINDING_RAYTRACE_ARGS_INPUT                                  9

// Inputs / Outputs

#define NRC_RESOLVE_BINDING_NRC_DEBUG_TRAINING_PATH_INFO_INPUT_OUTPUT            15

#define NRC_RESOLVE_BINDING_PRIMARY_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT   20
#define NRC_RESOLVE_BINDING_PRIMARY_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT  21
#define NRC_RESOLVE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT          22

// Outputs

#define NRC_RESOLVE_BINDING_DEBUG_VIEW_TEXTURE_OUTPUT                            30
#define NRC_RESOLVE_BINDING_GPU_PRINT_BUFFER_OUTPUT                              31

struct NrcResolvePushConstants
{
  uvec2 resolution;
  uint addPathtracedRadiance;
  uint addNrcRadiance;

  uint useNrcResolvedRadianceResult;
  NrcResolveMode resolveMode;
  uint samplesPerPixel;
  float resolveModeAccumulationWeight;

  uint debugBuffersAreEnabled;
};
