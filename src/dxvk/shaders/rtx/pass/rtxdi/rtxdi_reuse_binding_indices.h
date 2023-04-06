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
#ifndef RTXDI_REUSE_BINDING_INDICES_H
#define RTXDI_REUSE_BINDING_INDICES_H

#include "rtx/pass/common_binding_indices.h"

// Outputs

#define RTXDI_REUSE_BINDING_RTXDI_RESERVOIR                 20
#define RTXDI_REUSE_BINDING_WORLD_SHADING_NORMAL_INPUT      21
#define RTXDI_REUSE_BINDING_PERCEPTUAL_ROUGHNESS_INPUT      22
#define RTXDI_REUSE_BINDING_HIT_DISTANCE_INPUT              23
#define RTXDI_REUSE_BINDING_ALBEDO_INPUT                    24
#define RTXDI_REUSE_BINDING_BASE_REFLECTIVITY_INPUT         25
#define RTXDI_REUSE_BINDING_WORLD_POSITION_INPUT            26
#define RTXDI_REUSE_BINDING_PREV_WORLD_POSITION_INPUT       27
#define RTXDI_REUSE_BINDING_VIEW_DIRECTION_INPUT            28
#define RTXDI_REUSE_BINDING_CONE_RADIUS_INPUT               29
#define RTXDI_REUSE_BINDING_WS_MVEC_INPUT_OUTPUT            30
#define RTXDI_REUSE_BINDING_SS_MVEC_INPUT                   31
#define RTXDI_REUSE_BINDING_POSITION_ERROR_INPUT            32
#define RTXDI_REUSE_BINDING_SHARED_FLAGS_INPUT              33
#define RTXDI_REUSE_BINDING_LAST_GBUFFER                    34
#define RTXDI_REUSE_BINDING_REPROJECTION_CONFIDENCE_OUTPUT  35
#define RTXDI_REUSE_BINDING_BSDF_FACTOR_OUTPUT              36
#define RTXDI_REUSE_BINDING_TEMPORAL_POSITION_OUTPUT        37
#define RTXDI_REUSE_BINDING_BEST_LIGHTS_INPUT               38

#endif
