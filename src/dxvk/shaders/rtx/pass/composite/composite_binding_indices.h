/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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

#define COMPOSITE_THREAD_GROUP_WIDTH 16
#define COMPOSITE_THREAD_GROUP_HEIGHT 8

// Inputs

#define COMPOSITE_SHARED_FLAGS_INPUT                         0
#define COMPOSITE_SHARED_RADIANCE_RG_INPUT                   1
#define COMPOSITE_SHARED_RADIANCE_B_INPUT                    2

#define COMPOSITE_PRIMARY_ATTENUATION_INPUT                  3
#define COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT              4
#define COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT                5
#define COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT 6

#define COMPOSITE_SECONDARY_ATTENUATION_INPUT                7
#define COMPOSITE_SECONDARY_ALBEDO_INPUT                     8
#define COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT            9

#define COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT            10
#define COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT           11
#define COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT          12
#define COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT         13
#define COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT        14
#define COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT       15

#define COMPOSITE_CONSTANTS_INPUT                                   16

#define COMPOSITE_BSDF_FACTOR_INPUT                                 17
#define COMPOSITE_BSDF_FACTOR2_INPUT                                18

#define COMPOSITE_VOLUME_FILTERED_RADIANCE_AGE_INPUT                20
#define COMPOSITE_VOLUME_FILTERED_RADIANCE_Y_INPUT                  21
#define COMPOSITE_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT              22
#define COMPOSITE_ALPHA_GBUFFER_INPUT                               23

#define COMPOSITE_BLUE_NOISE_TEXTURE                                24
#define COMPOSITE_VALUE_NOISE_SAMPLER                               25
#define COMPOSITE_SKY_LIGHT_TEXTURE                                 26
#define COMPOSITE_ACTIVE_PIXEL_MASK_INPUT                           27
#define COMPOSITE_ACTIVE_LOCAL_PIXEL_COORDS_INPUT                   28

// These bindings map a pixel to its compacted slot under sparse rendering, for reads at pixels other than the thread's own.
#define COMPOSITE_COMPACTED_PIXEL_INDICES_INPUT                     29
#define COMPOSITE_TILE_ACTIVE_COUNTS_INPUT                          30

#define COMPOSITE_PIXEL_SAMPLING_RATE_INPUT                         31
#define COMPOSITE_PRIMARY_SURFACE_FLAGS_INPUT                       32
#define COMPOSITE_SKY_PROBE_TEXTURE                                 33

// These are bound only by the sky provider variant of the composite, and number from 70.
#define COMPOSITE_SKY_FOREGROUND_INPUT                              70
#define COMPOSITE_SKY_SECONDARY_HIT_DISTANCE_INPUT                  71
#define COMPOSITE_SKY_ARGS_INPUT                                    72
#define COMPOSITE_SKY_CLOUD_RENDER_INPUT                            73
#define COMPOSITE_SKY_CLOUD_DEPTH_INPUT                             74
#define COMPOSITE_SKY_HAZE_NEAR_SCATTER_INPUT                       75
#define COMPOSITE_SKY_HAZE_FAR_SCATTER_INPUT                        76
#define COMPOSITE_SKY_HAZE_FAR_TRANSMITTANCE_INPUT                  77
#define COMPOSITE_SKY_HAZE_FROXEL_AUX_INPUT                         78
#define COMPOSITE_SKY_HAZE_LIGHTS_INPUT                             79
#define COMPOSITE_SKY_HAZE_LIGHT_CLUSTERS_INPUT                     80
#define COMPOSITE_SKY_HAZE_SAMPLER_INPUT                            81
#define COMPOSITE_SKY_TRANSMITTANCE_LUT_INPUT                       82
#define COMPOSITE_SKY_MULTISCATTERING_LUT_INPUT                     83
#define COMPOSITE_SKY_VIEW_LUT_INPUT                                84
#define COMPOSITE_SKY_VIEW_SAMPLER_INPUT                            85
#define COMPOSITE_SKY_CLOUD_TRANSMITTANCE_LUT_INPUT                 86
#define COMPOSITE_SKY_CLOUD_SHADOW_MAP_INPUT                        87
#define COMPOSITE_SKY_CLOUD_NOISE_SAMPLER_INPUT                     88
#define COMPOSITE_SKY_CLOUD_SECONDARY_LUT_INPUT                     89
#define COMPOSITE_SKY_STAR_CELLS_INPUT                              90
#define COMPOSITE_SKY_STARS_INPUT                                   91
#define COMPOSITE_SKY_MILKY_WAY_INPUT                               92
#define COMPOSITE_SKY_STARLIGHT_LOW_INPUT                           93
#define COMPOSITE_SKY_MOON_INPUT                                    94

// Inputs/Outputs

#define COMPOSITE_PRIMARY_ALBEDO_INPUT_OUTPUT                       40
#define COMPOSITE_ACCUMULATED_FINAL_OUTPUT_INPUT_OUTPUT             41

// These are bound only by the sky provider variant of the composite.
#define COMPOSITE_SKY_BIAS_MASK_INPUT_OUTPUT                        42
#define COMPOSITE_SKY_DEPTH_INPUT_OUTPUT                            43
#define COMPOSITE_SKY_MOTION_VECTOR_INPUT_OUTPUT                    44
#define COMPOSITE_SKY_DEPTH_DLSSRR_INPUT_OUTPUT                     45
#define COMPOSITE_SKY_MOTION_VECTOR_DLSSRR_INPUT_OUTPUT             46

// Outputs

#define COMPOSITE_FINAL_OUTPUT                                      51
#define COMPOSITE_LAST_FINAL_OUTPUT                                 52
#define COMPOSITE_ALPHA_BLEND_RADIANCE_OUTPUT                       53

#define COMPOSITE_DEBUG_VIEW_OUTPUT                                 54
#define COMPOSITE_RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT            55
#define COMPOSITE_PRIMARY_COVERAGE_OUTPUT                           56
#define COMPOSITE_DLSS_NR_CONTROL_MASK_OUTPUT                       59
