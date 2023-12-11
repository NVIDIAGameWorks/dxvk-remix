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
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

// Use 32 bit ray direction for gbuffer pass
#define USE_32BIT_RAY_DIRECTION 1

// Inputs
#define GBUFFER_BINDING_LINEAR_WRAP_SAMPLER                                     37
#define GBUFFER_BINDING_SKYPROBE                                                38
#define GBUFFER_BINDING_SKYMATTE                                                39
#define GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_INPUT                          40

// Outputs

#define GBUFFER_BINDING_SHARED_FLAGS_OUTPUT                                     41
#define GBUFFER_BINDING_SHARED_RADIANCE_RG_OUTPUT                               42
#define GBUFFER_BINDING_SHARED_RADIANCE_B_OUTPUT                                43
#define GBUFFER_BINDING_SHARED_INTEGRATION_SURFACE_PDF_OUTPUT                   44
#define GBUFFER_BINDING_SHARED_MATERIAL_DATA0_OUTPUT                            45
#define GBUFFER_BINDING_SHARED_MATERIAL_DATA1_OUTPUT                            46
#define GBUFFER_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_OUTPUT                     47
#define GBUFFER_BINDING_PRIMARY_ATTENUATION_OUTPUT                              48
#define GBUFFER_BINDING_PRIMARY_WORLD_SHADING_NORMAL_OUTPUT                     49
#define GBUFFER_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_OUTPUT                     50
#define GBUFFER_BINDING_PRIMARY_LINEAR_VIEW_Z_OUTPUT                            51
#define GBUFFER_BINDING_PRIMARY_ALBEDO_OUTPUT                                   52
#define GBUFFER_BINDING_PRIMARY_BASE_REFLECTIVITY_OUTPUT                        53
#define GBUFFER_BINDING_PRIMARY_VIRTUAL_MVEC_OUTPUT                             54
#define GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_OUTPUT                      55
#define GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT             56
#define GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT   57
// Todo: Remove this HitDistance, only temporary dependency for RTXDI.
#define GBUFFER_BINDING_PRIMARY_HIT_DISTANCE_OUTPUT                             58
#define GBUFFER_BINDING_PRIMARY_VIEW_DIRECTION_OUTPUT                           59
#define GBUFFER_BINDING_PRIMARY_CONE_RADIUS_OUTPUT                              60
#define GBUFFER_BINDING_PRIMARY_WORLD_POSITION_OUTPUT                           61
#define GBUFFER_BINDING_PRIMARY_POSITION_ERROR_OUTPUT                           62
#define GBUFFER_BINDING_SECONDARY_ATTENUATION_OUTPUT                            64
#define GBUFFER_BINDING_SECONDARY_WORLD_SHADING_NORMAL_OUTPUT                   65
#define GBUFFER_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_OUTPUT                   66
#define GBUFFER_BINDING_SECONDARY_LINEAR_VIEW_Z_OUTPUT                          67
#define GBUFFER_BINDING_SECONDARY_ALBEDO_OUTPUT                                 68
#define GBUFFER_BINDING_SECONDARY_BASE_REFLECTIVITY_OUTPUT                      69
#define GBUFFER_BINDING_SECONDARY_VIRTUAL_MVEC_OUTPUT                           70
#define GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT           71
#define GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT 72
#define GBUFFER_BINDING_SECONDARY_HIT_DISTANCE_OUTPUT                           73
#define GBUFFER_BINDING_SECONDARY_VIEW_DIRECTION_OUTPUT                         74
#define GBUFFER_BINDING_SECONDARY_CONE_RADIUS_OUTPUT                            75
#define GBUFFER_BINDING_SECONDARY_WORLD_POSITION_OUTPUT                         76
#define GBUFFER_BINDING_SECONDARY_POSITION_ERROR_OUTPUT                         77
#define GBUFFER_BINDING_PRIMARY_SURFACE_FLAGS_OUTPUT                            78
#define GBUFFER_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_OUTPUT                79
#define GBUFFER_BINDING_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_OUTPUT               80
#define GBUFFER_BINDING_PRIMARY_DEPTH_OUTPUT                                    81
#define GBUFFER_BINDING_SHARED_BIAS_CURRENT_COLOR_MASK_OUTPUT                   82

#define GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_2                           83
#define GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_0                         84
#define GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_1                         85
#define GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_2                         86
#define GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_3                         87

#define GBUFFER_BINDING_ALPHA_BLEND_GBUFFER_OUTPUT                              88
#define GBUFFER_BINDING_SHARED_TEXTURE_COORD_OUTPUT                             89
#define GBUFFER_BINDING_SHARED_SURFACE_INDEX_OUTPUT                             90
#define GBUFFER_BINDING_SHARED_SUBSURFACE_DATA_OUTPUT                           91

#define GBUFFER_BINDING_ALIASED_DATA_0                                          92
#define GBUFFER_BINDING_DECAL_MATERIAL_STORAGE          (GBUFFER_BINDING_ALIASED_DATA_0 + 0)
#define GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_0   (GBUFFER_BINDING_ALIASED_DATA_0 + 1)

#define GBUFFER_BINDING_ALIASED_DATA_1                                          94
#define GBUFFER_BINDING_DECAL_EMISSIVE_RADIANCE_STORAGE (GBUFFER_BINDING_ALIASED_DATA_1 + 0)
#define GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_1   (GBUFFER_BINDING_ALIASED_DATA_1 + 1)

#define GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT                           96

#define GBUFFER_MIN_BINDING                         GBUFFER_BINDING_SKYMATTE
#define GBUFFER_MAX_BINDING                         GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT

#if GBUFFER_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of G-buffer bindings to avoid overlap with common bindings!"
#endif

#define GBUFFER_SBT_OFFSET_STANDARD 0

struct GbufferPushConstants
{
  uint isTransmissionPSR;
};
