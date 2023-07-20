/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

// Inputs

#define INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT                   40
#define INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA0_INPUT          41
#define INTEGRATE_NEE_BINDING_SHARED_MATERIAL_DATA1_INPUT          42

#define INTEGRATE_NEE_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT    43
#define INTEGRATE_NEE_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT    44
#define INTEGRATE_NEE_BINDING_PRIMARY_ALBEDO_INPUT                  45
#define INTEGRATE_NEE_BINDING_PRIMARY_VIEW_DIRECTION_INPUT          46
#define INTEGRATE_NEE_BINDING_PRIMARY_CONE_RADIUS_INPUT             47
#define INTEGRATE_NEE_BINDING_PRIMARY_WORLD_POSITION_INPUT          48

#define INTEGRATE_NEE_BINDING_PRIMARY_POSITION_ERROR_INPUT                     51
#define INTEGRATE_NEE_BINDING_PRIMARY_HIT_DISTANCE_INPUT                       52
#define INTEGRATE_NEE_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_INPUT          53
#define INTEGRATE_NEE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT             54

#define INTEGRATE_NEE_BINDING_NEE_CACHE                                      56
#define INTEGRATE_NEE_BINDING_NEE_CACHE_TASK                                 57
#define INTEGRATE_NEE_BINDING_NEE_CACHE_SAMPLE                               58
#define INTEGRATE_NEE_BINDING_NEE_CACHE_THREAD_TASK                          59

#define INTEGRATE_NEE_BINDING_RADIANCE_INPUT                                      60
#define INTEGRATE_NEE_BINDING_HIT_GEOMETRY_INPUT                                  61

// Inputs/Outputs

#define INTEGRATE_NEE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT                    70

// Outputs

#define INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_OUTPUT     80
#define INTEGRATE_NEE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_OUTPUT    81
#define INTEGRATE_NEE_BINDING_RESTIR_GI_RESERVOIR_OUTPUT                                82
#define INTEGRATE_NEE_BINDING_BSDF_FACTOR2_OUTPUT                                       83

#define INTEGRATE_NEE_MIN_BINDING                           INTEGRATE_NEE_BINDING_SHARED_FLAGS_INPUT

#if INTEGRATE_NEE_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of ReSTIR GI Final Shading bindings to avoid overlap with common bindings!"
#endif
