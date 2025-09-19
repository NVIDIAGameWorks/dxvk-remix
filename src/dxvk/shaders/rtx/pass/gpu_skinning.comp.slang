/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "gpu_skinning_binding_indices.h"
#include "skinning.h"

layout(binding = BINDING_SKINNING_CONSTANTS)
ConstantBuffer<SkinningArgs> cb;

layout(binding = BINDING_POSITION_OUTPUT)
RWStructuredBuffer<float> dstPosition;

layout(binding = BINDING_NORMAL_OUTPUT)
RWStructuredBuffer<float> dstNormal;

layout(binding = BINDING_POSITION_INPUT)
StructuredBuffer<float> srcPosition;

layout(binding = BINDING_BLEND_WEIGHT_INPUT) 
StructuredBuffer<float> srcBlendWeight;

layout(binding = BINDING_BLEND_INDICES_INPUT) 
ByteAddressBuffer srcBlendIndices;

layout(binding = BINDING_NORMAL_INPUT)
StructuredBuffer<float> srcNormal;

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
  if (idx >= cb.numVertices)
  {
    return;
  }

  skinning(idx, dstPosition, dstNormal, srcPosition, srcBlendWeight, srcBlendIndices, srcNormal, cb);
}