/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#define INTEGRATE_DIRECT_BINDING_SKYPROBE                                           39

#define INTEGRATE_DIRECT_BINDING_SHARED_INTEGRATION_SURFACE_PDF_INPUT               40
#define INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA0_INPUT                        41
#define INTEGRATE_DIRECT_BINDING_SHARED_MATERIAL_DATA1_INPUT                        42
#define INTEGRATE_DIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT                         43
#define INTEGRATE_DIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT                         44
#define INTEGRATE_DIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT                       45

#define INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_SHADING_NORMAL_INPUT                 46
#define INTEGRATE_DIRECT_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_INPUT                 47
#define INTEGRATE_DIRECT_BINDING_PRIMARY_ALBEDO_INPUT                               48
#define INTEGRATE_DIRECT_BINDING_PRIMARY_VIEW_DIRECTION_INPUT                       49
#define INTEGRATE_DIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT                          50
#define INTEGRATE_DIRECT_BINDING_PRIMARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT 51
#define INTEGRATE_DIRECT_BINDING_PRIMARY_POSITION_ERROR_INPUT                       52
#define INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR                            53

#define INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_SHADING_NORMAL_INPUT               54
#define INTEGRATE_DIRECT_BINDING_SECONDARY_ALBEDO_INPUT                             55
#define INTEGRATE_DIRECT_BINDING_SECONDARY_VIEW_DIRECTION_INPUT                     56
#define INTEGRATE_DIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT                        57

#define INTEGRATE_DIRECT_BINDING_NEE_CACHE                                          58
#define INTEGRATE_DIRECT_BINDING_NEE_CACHE_THREAD_TASK                              59
#define INTEGRATE_DIRECT_BINDING_NEE_CACHE_TASK                                     60

// Inputs/Outputs

#define INTEGRATE_DIRECT_BINDING_SHARED_FLAGS_INPUT_OUTPUT                          61
#define INTEGRATE_DIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT_OUTPUT          62
#define INTEGRATE_DIRECT_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT_OUTPUT             63
#define INTEGRATE_DIRECT_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT_OUTPUT           64

// Outputs

#define INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_DIFFUSE_LOBE_RADIANCE_OUTPUT        65
#define INTEGRATE_DIRECT_BINDING_PRIMARY_DIRECT_SPECULAR_LOBE_RADIANCE_OUTPUT       66
#define INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_DIFFUSE_LOBE_RADIANCE_OUTPUT    67
#define INTEGRATE_DIRECT_BINDING_SECONDARY_COMBINED_SPECULAR_LOBE_RADIANCE_OUTPUT   68

#define INTEGRATE_DIRECT_BINDING_PRIMARY_RTXDI_ILLUMINANCE_OUTPUT                   69

#define INTEGRATE_DIRECT_BINDING_INDIRECT_THROUGHPUT_CONE_RADIUS_OUTPUT             70

#define INTEGRATE_DIRECT_BINDING_NEE_CACHE_SAMPLE                                   71

// Aliased Inputs/Outputs 

#define INTEGRATE_DIRECT_BINDING_ALIASED_DATA_0   80
#define INTEGRATE_DIRECT_BINDING_SECONDARY_WORLD_POSITION_WORLD_TRIANGLE_NORMAL_INPUT (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_0 + 0)
#define INTEGRATE_DIRECT_BINDING_INDIRECT_RAY_ORIGIN_DIRECTION_OUTPUT                 (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_0 + 1)
#define INTEGRATE_DIRECT_BINDING_ALIASED_DATA_1   82
#define INTEGRATE_DIRECT_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_INPUT             (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_1 + 0)
#define INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_HIT_PERCEPTUAL_ROUGHNESS_OUTPUT   (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_1 + 1)
#define INTEGRATE_DIRECT_BINDING_ALIASED_DATA_2   84
#define INTEGRATE_DIRECT_BINDING_SECONDARY_POSITION_ERROR_INPUT            (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_2 + 0)
#define INTEGRATE_DIRECT_BINDING_INDIRECT_FIRST_SAMPLED_LOBE_DATA_OUTPUT   (INTEGRATE_DIRECT_BINDING_ALIASED_DATA_2 + 1)

#define INTEGRATE_DIRECT_MIN_BINDING                           INTEGRATE_DIRECT_BINDING_SKYPROBE

#if INTEGRATE_DIRECT_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of Integrate Direct bindings to avoid overlap with common bindings!"
#endif
