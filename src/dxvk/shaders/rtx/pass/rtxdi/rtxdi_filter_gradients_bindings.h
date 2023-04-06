/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#ifndef RTXDI_FILTER_GRADIENTS_BINDING_INDICES_H
#define RTXDI_FILTER_GRADIENTS_BINDING_INDICES_H

#define RTXDI_FILTER_GRADIENTS_BINDING_GRADIENTS_INPUT_OUTPUT 0

struct FilterGradientsArgs
{
    uint2 gradientImageSize;
    uint passIndex;
    float hitDistanceSensitivity;
};

#ifdef __cplusplus

#define RTXDI_FILTER_GRADIENTS_BINDINGS \
    RW_TEXTURE2DARRAY(RTXDI_FILTER_GRADIENTS_BINDING_GRADIENTS_INPUT_OUTPUT)

#else // __cplusplus

#include "rtxdi/RtxdiParameters.h"

layout(push_constant)
ConstantBuffer<FilterGradientsArgs> cb;

layout(rg16f, binding = RTXDI_FILTER_GRADIENTS_BINDING_GRADIENTS_INPUT_OUTPUT)
RWTexture2DArray<float2> RtxdiGradients;

#endif // __cplusplus

#endif
