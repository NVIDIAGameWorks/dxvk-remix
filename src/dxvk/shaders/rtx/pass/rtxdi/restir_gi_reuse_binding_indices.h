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

// Inputs / Outputs

#define RESTIR_GI_REUSE_BINDING_RESERVOIR_INPUT_OUTPUT      20

// Inputs
#define RESTIR_GI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT  21
#define RESTIR_GI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT  22
#define RESTIR_GI_REUSE_BINDING_HIT_DISTANCE_INPUT          23
#define RESTIR_GI_REUSE_BINDING_ALBEDO_INPUT                24
#define RESTIR_GI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT     25
#define RESTIR_GI_REUSE_BINDING_WORLD_POSITION_INPUT        26
#define RESTIR_GI_REUSE_BINDING_VIEW_DIRECTION_INPUT        27
#define RESTIR_GI_REUSE_BINDING_CONE_RADIUS_INPUT           28
#define RESTIR_GI_REUSE_BINDING_LAST_GBUFFER                29
#define RESTIR_GI_REUSE_BINDING_MVEC_INPUT                  30
#define RESTIR_GI_REUSE_BINDING_RADIANCE_INPUT              31
#define RESTIR_GI_REUSE_BINDING_HIT_GEOMETRY_INPUT          32
#define RESTIR_GI_REUSE_BINDING_POSITION_ERROR_INPUT        33
#define RESTIR_GI_REUSE_BINDING_SHARED_FLAGS_INPUT          34
#define RESTIR_GI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT   35
#define RESTIR_GI_REUSE_BINDING_SHARED_SURFACE_INDEX_INPUT  36
#define RESTIR_GI_REUSE_BINDING_SUBSURFACE_DATA_INPUT       37
#define RESTIR_GI_REUSE_BINDING_GRADIENTS_INPUT             38
