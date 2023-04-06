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

#define RTXDI_COMPUTE_CONFIDENCE_BINDING_GRADIENTS_INPUT            0
#define RTXDI_COMPUTE_CONFIDENCE_BINDING_MVEC_INPUT                 1
#define RTXDI_COMPUTE_CONFIDENCE_BINDING_HIT_DISTANCE_INPUT         2
#define RTXDI_COMPUTE_CONFIDENCE_BINDING_PREVIOUS_CONFIDENCE_INPUT  3
#define RTXDI_COMPUTE_CONFIDENCE_BINDING_CURRENT_CONFIDENCE_OUTPUT  4

struct ComputeConfidenceArgs
{
    uint2 resolution;
    uint inputBufferIndex;
    float blendFactor;
    float gradientPower;
    float gradientScale;
    float minimumConfidence;
    float hitDistanceSensitivity;
    float confidenceHitDistanceSensitivity;
};

#ifdef __cplusplus

#define RTXDI_COMPUTE_CONFIDENCE_BINDINGS \
    TEXTURE2DARRAY(RTXDI_COMPUTE_CONFIDENCE_BINDING_GRADIENTS_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_CONFIDENCE_BINDING_MVEC_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_CONFIDENCE_BINDING_HIT_DISTANCE_INPUT) \
    TEXTURE2D(RTXDI_COMPUTE_CONFIDENCE_BINDING_PREVIOUS_CONFIDENCE_INPUT) \
    RW_TEXTURE2D(RTXDI_COMPUTE_CONFIDENCE_BINDING_CURRENT_CONFIDENCE_OUTPUT)

#else // __cplusplus

#include "rtxdi/RtxdiParameters.h"

layout(push_constant)
ConstantBuffer<ComputeConfidenceArgs> cb;

layout(rg16f, binding = RTXDI_COMPUTE_CONFIDENCE_BINDING_GRADIENTS_INPUT)
Texture2DArray<float2> RtxdiGradients;

layout(rg16f, binding = RTXDI_COMPUTE_CONFIDENCE_BINDING_MVEC_INPUT)
Texture2D<float2> PrimaryScreenSpaceMotionVector;

layout(r32f, binding = RTXDI_COMPUTE_CONFIDENCE_BINDING_HIT_DISTANCE_INPUT)
Texture2D<float> PrimaryHitDistance;

layout(r16f, binding = RTXDI_COMPUTE_CONFIDENCE_BINDING_PREVIOUS_CONFIDENCE_INPUT)
Texture2D<float> PreviousConfidence;

layout(r16f, binding = RTXDI_COMPUTE_CONFIDENCE_BINDING_CURRENT_CONFIDENCE_OUTPUT)
RWTexture2D<float> CurrentConfidence;

#endif // __cplusplus
