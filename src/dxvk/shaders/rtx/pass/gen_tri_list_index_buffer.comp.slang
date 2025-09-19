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
#include "gen_tri_list_index_buffer_indices.h"

// Borrowed from the vulkan_core.h file: 
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST  3
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP 4
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN   5

#include "gen_tri_list_index_buffer.h"

layout(binding = GEN_TRILIST_BINDING_OUTPUT)
RWStructuredBuffer<uint16_t> dst;

layout(binding = GEN_TRILIST_BINDING_INPUT) 
StructuredBuffer<uint16_t> src;

layout(push_constant)
ConstantBuffer<GenTriListArgs> cb;


[shader("compute")]
[numthreads(128, 1, 1)]
void main(uint idx : SV_DispatchThreadID)
{
  if (idx >= cb.primCount)
  {
    return;
  }

  generateIndices(idx, dst, src, cb);
}