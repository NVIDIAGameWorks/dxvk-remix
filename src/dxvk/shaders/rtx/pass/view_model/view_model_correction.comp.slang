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
#include "view_model_correction_binding_indices.h"

layout(binding = BINDING_VMC_CONSTANTS)
ConstantBuffer<ViewModelCorrectionArgs> cb;

layout(binding = BINDING_VMC_POSITION_INPUT_OUTPUT)
RWStructuredBuffer<float> Positions;

layout(binding = BINDING_VMC_NORMAL_INPUT_OUTPUT)
RWStructuredBuffer<float> Normals;

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
  if (idx >= cb.numVertices) return;

  const uint basePositionOffset = (cb.positionOffset + idx * cb.positionStride) / 4;
  float4 position = float4(Positions[basePositionOffset + 0], 
                           Positions[basePositionOffset + 1], 
                           Positions[basePositionOffset + 2], 1.f);
  
  const uint baseNormalOffset = (cb.normalOffset + idx * cb.normalStride) / 4;
  float4 normal;
  if (cb.normalStride != 0)
  {
    normal = float4(Normals[baseNormalOffset + 0], 
                    Normals[baseNormalOffset + 1], 
                    Normals[baseNormalOffset + 2], 0.f);
  }

  position = mul(cb.positionTransform, position);

  position.xyz /= position.w;
  position.w = 1.0;

  normal = mul(cb.vectorTransform, normal);
  float normalLength = length(normal);
  if (normalLength > 0) normal /= normalLength;
  
  Positions[basePositionOffset + 0] = position.x;
  Positions[basePositionOffset + 1] = position.y;
  Positions[basePositionOffset + 2] = position.z;

  if (cb.normalStride != 0)
  {
    Normals[baseNormalOffset + 0] = normal.x;
    Normals[baseNormalOffset + 1] = normal.y;
    Normals[baseNormalOffset + 2] = normal.z;
  }
}