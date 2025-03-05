/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#ifdef __cplusplus
#include "rtx/concept/camera/camera.h"
#else
#include "rtx/concept/camera/camera.slangh"
#endif

// Inputs
#define RAY_RECONSTRUCTION_CONSTANTS_INPUT                    0
#define RAY_RECONSTRUCTION_NORMALS_INPUT                      1
#define RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT              2

#define RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT    3
#define RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT          4
#define RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT 5

#define RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT             6
#define RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT    7
#define RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT        8
#define RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT 9
#define RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_INPUT_OUTPUT 10

#define RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT                 20
#define RAY_RECONSTRUCTION_COMBINED_INPUT                     21
#define RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT               22
#define RAY_RECONSTRUCTION_DEPTHS_INPUT                       23
#define RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT                24
#define RAY_RECONSTRUCTION_PRIMARY_CONE_RADIUS_INPUT          25

// Outputs
#define RAY_RECONSTRUCTION_NORMALS_OUTPUT                     30
#define RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT                31
#define RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT                  32

// Input/Outputs
#define RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT        40
#define RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT 41

// Constant buffers
struct RayReconstructionArgs {
  Camera camera;

  vec4 debugKnob;

  uint enableDemodulateRoughness;
  float upscalerRoughnessDemodulationMultiplier;
  float upscalerRoughnessDemodulationOffset;
  uint debugViewIdx;

  uint enableDLSSRRInputs;
  uint particleBufferMode;
  uint enableDemodulateAttenuation;
  uint filterHitT;

  uint combineSpecularAlbedo;
  uint useExternalExposure;
  uint rayReconstructionUseVirtualNormals;
  uint pad;
};
