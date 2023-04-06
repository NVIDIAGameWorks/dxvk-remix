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

// Inputs

#define COMPOSITE_SHARED_FLAGS_INPUT                         2
#define COMPOSITE_SHARED_RADIANCE_RG_INPUT                   3
#define COMPOSITE_SHARED_RADIANCE_B_INPUT                    4

#define COMPOSITE_PRIMARY_ATTENUATION_INPUT                  5
#define COMPOSITE_PRIMARY_ALBEDO_INPUT                       6
#define COMPOSITE_PRIMARY_SPECULAR_ALBEDO_INPUT              7
#define COMPOSITE_PRIMARY_LINEAR_VIEW_Z_INPUT                8
#define COMPOSITE_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT 9

#define COMPOSITE_SECONDARY_ATTENUATION_INPUT                10
#define COMPOSITE_SECONDARY_ALBEDO_INPUT                     11
#define COMPOSITE_SECONDARY_SPECULAR_ALBEDO_INPUT            12

#define COMPOSITE_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT            13
#define COMPOSITE_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT           14
#define COMPOSITE_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT          15
#define COMPOSITE_PRIMARY_INDIRECT_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT         16
#define COMPOSITE_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT        17
#define COMPOSITE_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT       18

#define COMPOSITE_CONSTANTS_INPUT                                  19

#define COMPOSITE_BSDF_FACTOR_INPUT                                20
#define COMPOSITE_BSDF_FACTOR2_INPUT                               21

#define COMPOSITE_VOLUME_PREINTEGRATED_RADIANCE_INPUT              22

// Outputs

#define COMPOSITE_FINAL_OUTPUT                                     25
#define COMPOSITE_LAST_FINAL_OUTPUT                                26

#define COMPOSITE_DEBUG_VIEW_OUTPUT                                29
