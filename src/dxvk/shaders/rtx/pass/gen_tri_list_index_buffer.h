/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

// This function can be executed on the CPU or GPU!!
#ifdef __cplusplus
void generateIndices(const uint32_t idx, uint16_t* dst, const uint16_t* src, const GenTriListArgs& cb)
#else
void generateIndices(const uint32_t idx, RWStructuredBuffer<uint16_t> dst, StructuredBuffer<uint16_t> src, GenTriListArgs cb)
#endif
{
  uint32_t i0 = 0;
  uint32_t i1 = 0;
  uint32_t i2 = 0;
    
  switch (cb.topology)
  {
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      i0 = 0;
      i1 = idx + 1;
      i2 = idx + 2;
      break;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      i0 = idx + 0;
      i1 = idx + 1 + (idx & 1);
      i2 = idx + 2 - (idx & 1);
      break;
  case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      i0 = idx * 3 + 0;
      i1 = idx * 3 + 1;
      i2 = idx * 3 + 2;
      break;
  }
  
  // NOTE: firstIndex can be greater than 0xFFFF, be careful when applying to uint16 indices
  i0 += cb.firstIndex;
  i1 += cb.firstIndex;
  i2 += cb.firstIndex;

  if (cb.useIndexBuffer != 0) 
  {
    uint32_t idx0 = src[i0];
    uint32_t idx1 = src[i1];
    uint32_t idx2 = src[i2];

    // degen, or invalid
    if (idx0 == idx1 || idx0 == idx2 || idx1 == idx2 ||
        idx0 > cb.maxVertex || idx1 > cb.maxVertex || idx2 > cb.maxVertex)
    {
      idx0 = cb.minVertex;
      idx1 = cb.minVertex;
      idx2 = cb.minVertex;
    }
    
    dst[idx * 3 + 0] = idx0 - cb.minVertex;
    dst[idx * 3 + 1] = idx1 - cb.minVertex;
    dst[idx * 3 + 2] = idx2 - cb.minVertex;
  } 
  else 
  {
    dst[idx * 3 + 0] = i0 - cb.minVertex;
    dst[idx * 3 + 1] = i1 - cb.minVertex;
    dst[idx * 3 + 2] = i2 - cb.minVertex;
  }
}