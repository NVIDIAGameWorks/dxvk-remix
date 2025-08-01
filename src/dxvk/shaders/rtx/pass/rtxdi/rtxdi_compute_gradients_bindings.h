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

#define RTXDI_COMPUTE_GRADIENTS_BINDING_RTXDI_RESERVOIR                 20
#define RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_WORLD_POSITION_INPUT    21
#define RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_WORLD_POSITION_INPUT   22
#define RTXDI_COMPUTE_GRADIENTS_BINDING_CONE_RADIUS_INPUT               23
#define RTXDI_COMPUTE_GRADIENTS_BINDING_MVEC_INPUT                      24
#define RTXDI_COMPUTE_GRADIENTS_BINDING_POSITION_ERROR_INPUT            25
#define RTXDI_COMPUTE_GRADIENTS_BINDING_TEMPORAL_POSITION_INPUT         26
#define RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_ILLUMINANCE_INPUT       27
#define RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_ILLUMINANCE_INPUT      28
#define RTXDI_COMPUTE_GRADIENTS_BINDING_HIT_DISTANCE_INPUT              29
#define RTXDI_COMPUTE_GRADIENTS_BINDING_SHARED_FLAGS_INPUT              30
#define RTXDI_COMPUTE_GRADIENTS_BINDING_GRADIENTS_OUTPUT                31
#define RTXDI_COMPUTE_GRADIENTS_BINDING_BEST_LIGHTS_OUTPUT              32

struct ComputeGradientsArgs
{
  float darknessBias;
  uint usePreviousIlluminance;
  uint computeGradients;
};

#ifdef __cplusplus

#define RTXDI_COMPUTE_GRADIENTS_BINDINGS \
    COMMON_RAYTRACING_BINDINGS \
    RW_STRUCTURED_BUFFER(RTXDI_COMPUTE_GRADIENTS_BINDING_RTXDI_RESERVOIR) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_WORLD_POSITION_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_WORLD_POSITION_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_CONE_RADIUS_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_MVEC_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_POSITION_ERROR_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_TEMPORAL_POSITION_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_ILLUMINANCE_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_ILLUMINANCE_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_HIT_DISTANCE_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_SHARED_FLAGS_INPUT) \
    RW_TEXTURE2DARRAY(RTXDI_COMPUTE_GRADIENTS_BINDING_GRADIENTS_OUTPUT) \
    RW_TEXTURE2D(RTXDI_COMPUTE_GRADIENTS_BINDING_BEST_LIGHTS_OUTPUT)

#else // __cplusplus

#include "rtxdi/RtxdiParameters.h"

layout(push_constant)
ConstantBuffer<ComputeGradientsArgs> push;

layout(binding = RTXDI_COMPUTE_GRADIENTS_BINDING_RTXDI_RESERVOIR)
RWStructuredBuffer<RTXDI_PackedReservoir> RtxdiReservoirBuffer;

layout(rgba32f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_WORLD_POSITION_INPUT)
Texture2D<float4> CurrentWorldPosition_WorldTriangleNormal;

layout(rgba32f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_WORLD_POSITION_INPUT)
Texture2D<float4> PreviousWorldPosition_WorldTriangleNormal;

layout(r16f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_CONE_RADIUS_INPUT)
Texture2D<float> PrimaryConeRadius;

layout(rgba16f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_MVEC_INPUT)
Texture2D<float4> PrimaryVirtualMotionVector;

layout(r32f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_POSITION_ERROR_INPUT)
Texture2D<float> PrimaryPositionError;

layout(r32ui, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_TEMPORAL_POSITION_INPUT)
Texture2D<uint> TemporalPosition;

layout(r16f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_CURRENT_ILLUMINANCE_INPUT)
Texture2D<float> CurrentRtxdiIlluminance;

layout(r16f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_PREVIOUS_ILLUMINANCE_INPUT)
Texture2D<float> PreviousRtxdiIlluminance;

layout(r32f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_HIT_DISTANCE_INPUT)
Texture2D<float> PrimaryHitDistance;

layout(r16ui, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_SHARED_FLAGS_INPUT)
Texture2D<uint16_t> SharedFlags;

layout(rg16f, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_GRADIENTS_OUTPUT)
RWTexture2DArray<float2> RtxdiGradients;

layout(rg16ui, binding = RTXDI_COMPUTE_GRADIENTS_BINDING_BEST_LIGHTS_OUTPUT)
RWTexture2D<uint2> RtxdiBestLights;

#endif // __cplusplus
