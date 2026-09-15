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

#include "rtx/utility/shader_types.h"
#include "rtx/external/NRC.h"
// WAR: Specify a relative path to this file since a file including this file doesn't resolve a relative path to global include paths specified in meson
#include "../../../../../submodules/nrc/include/NrcStructures.h"
#include "../shaders/rtx/concept/surface/surface_shared.h"

// A training query reservoir entry holds the selection key in the high bits and the winning pixel's offset
// within its cell in the low bits. The offset limits how large a cell may be, which
// NeuralRadianceCache::clampTrainingDimensionsToReservoirOffsetRange enforces.
// 7 bits allows 128 query pixels per cell axis, several times the ~17 a 4K render resolution needs at default
// training settings, and leaves the key 18 of the 32 bits. Trading either way costs the other headroom.
#define NRC_TRAINING_QUERY_OFFSET_BITS_PER_AXIS 7
#define NRC_MAX_QUERY_PIXELS_PER_TRAINING_PIXEL_PER_AXIS (1u << NRC_TRAINING_QUERY_OFFSET_BITS_PER_AXIS)

// Note: Ensure 16B alignment
struct NrcArgs {
  NrcConstants nrcConstants;

  vec2 updatePixelJitter;
  uint updatePathMaxBounces;
  uint updateAllowRussianRoulette;

  vec2 activeTrainingDimensions;
  vec2 rcpActiveTrainingDimensions;

  vec2 queryToTrainingCoordinateSpace;
  vec2 trainingToQueryCoordinateSpace;

  vec3 sceneBoundsMin;
  uint numRowsForUpdate;

  vec3 sceneBoundsMax;
  float trainingLuminanceClamp;

  uint boostEmissives;
  uint pad0;
  uint pad1;
  uint pad2;
};
