/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include "smooth_normals_binding_indices.h"
#include "smooth_normals.h"

layout(binding = SMOOTH_NORMALS_BINDING_POSITION_RO)
StructuredBuffer<float> positionData;

layout(binding = SMOOTH_NORMALS_BINDING_NORMAL_RW)
RWStructuredBuffer<float> normalData;

layout(binding = SMOOTH_NORMALS_BINDING_INDEX_INPUT)
StructuredBuffer<uint> srcIndex;

layout(binding = SMOOTH_NORMALS_BINDING_HASH_TABLE)
RWStructuredBuffer<int> hashTable;

layout(push_constant)
ConstantBuffer<SmoothNormalsArgs> cb;

[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
  uint phase = cb.phase;

  // Hash table is pre-cleared to zero by the host via vkCmdFillBuffer.
  if (phase == 1) {
    if (idx >= cb.numTriangles) return;
    smoothNormalsAccumulate(idx, positionData, srcIndex, hashTable, cb);
  }
  else if (phase == 2) {
    if (idx >= cb.numVertices) return;
    smoothNormalsScatter(idx, positionData, normalData, hashTable, cb);
  }
}
