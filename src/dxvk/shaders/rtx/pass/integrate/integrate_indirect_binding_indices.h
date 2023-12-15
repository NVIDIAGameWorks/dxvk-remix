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

#include "rtx/pass/common_binding_indices.h"

// Inputs
#define INTEGRATE_BINDING_LINEAR_WRAP_SAMPLER                           38
#define INTEGRATE_INDIRECT_BINDING_SKYPROBE                             39

#define INTEGRATE_INDIRECT_BINDING_SHARED_FLAGS_INPUT                   40
#define INTEGRATE_INDIRECT_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_INPUT   41
#define INTEGRATE_INDIRECT_BINDING_SHARED_TEXTURE_COORD_INPUT           42
#define INTEGRATE_INDIRECT_BINDING_SHARED_SURFACE_INDEX_INPUT           43
#define INTEGRATE_INDIRECT_BINDING_SHARED_SUBSURFACE_DATA_INPUT         44

// Todo: Remove, temporary but needed for now for the miss flag encoded in these values.
#define INTEGRATE_INDIRECT_BINDING_PRIMARY_CONE_RADIUS_INPUT               45
#define INTEGRATE_INDIRECT_BINDING_SECONDARY_CONE_RADIUS_INPUT             46
#define INTEGRATE_INDIRECT_BINDING_PRIMARY_WORLD_POSITION_INPUT            47
#define INTEGRATE_INDIRECT_BINDING_PRIMARY_RTXDI_RESERVOIR                 48

#define INTEGRATE_INDIRECT_BINDING_RAY_ORIGIN_DIRECTION_INPUT                          49
#define INTEGRATE_INDIRECT_BINDING_FIRST_HIT_PERCEPTUAL_ROUGHNESS_INPUT                50

#define INTEGRATE_INDIRECT_BINDING_LAST_GBUFFER_INPUT                                  51
#define INTEGRATE_INDIRECT_BINDING_PREV_WORLD_POSITION_INPUT                           52
#define INTEGRATE_INDIRECT_BINDING_VOLUME_FILTERED_RADIANCE_INPUT                      53
#define INTEGRATE_INDIRECT_BINDING_PRIMARY_HIT_DISTANCE_INPUT                          54
#define INTEGRATE_INDIRECT_BINDING_SECONDARY_HIT_DISTANCE_INPUT                        55
#define INTEGRATE_INDIRECT_BINDING_LAST_COMPOSITE_INPUT                                56
#define INTEGRATE_INDIRECT_BINDING_FIRST_SAMPLED_LOBE_DATA_INPUT                       57

#define INTEGRATE_INDIRECT_BINDING_NEE_CACHE                                      58
#define INTEGRATE_INDIRECT_BINDING_NEE_CACHE_SAMPLE                               59
#define INTEGRATE_INDIRECT_BINDING_NEE_CACHE_THREAD_TASK                          60
#define INTEGRATE_INDIRECT_BINDING_NEE_CACHE_TASK                                 61
#define INTEGRATE_INDIRECT_BINDING_PRIMITIVE_ID_PREFIX_SUM                        62

#define INTEGRATE_INDIRECT_BINDING_GRADIENTS_INPUT                                63

// Storage

#define INTEGRATE_INDIRECT_BINDING_DECAL_MATERIAL_STORAGE                              64

// Outputs

#define INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RESERVOIR_OUTPUT                          70
#define INTEGRATE_INDIRECT_BINDING_RESTIR_GI_RADIANCE_OUTPUT                           71
#define INTEGRATE_INDIRECT_BINDING_RESTIR_GI_HIT_GEOMETRY_OUTPUT                       72


// Aliased Inputs/Outputs 

#define INTEGRATE_INDIRECT_BINDING_ALIASED_DATA_0                                  90
#define INTEGRATE_INDIRECT_BINDING_THROUGHPUT_CONE_RADIUS_INPUT                    (INTEGRATE_INDIRECT_BINDING_ALIASED_DATA_0 + 0)
#define INTEGRATE_INDIRECT_BINDING_DECAL_EMISSIVE_RADIANCE_STORAGE                 (INTEGRATE_INDIRECT_BINDING_ALIASED_DATA_0 + 1)
#define INTEGRATE_INDIRECT_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_OUTPUT           (INTEGRATE_INDIRECT_BINDING_ALIASED_DATA_0 + 2)

// Auxilary Inputs/Outputs

#define INTEGRATE_INSTRUMENTATION                                                   100

#define INTEGRATE_INDIRECT_MIN_BINDING                           INTEGRATE_INDIRECT_BINDING_SKYPROBE

#if INTEGRATE_INDIRECT_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of Integrate Indirect bindings to avoid overlap with common bindings!"
#endif

#define INTEGRATE_INDIRECT_SBT_OFFSET_STANDARD 0

