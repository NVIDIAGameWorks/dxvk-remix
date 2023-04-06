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
#define asfloat(x) *reinterpret_cast<const float*>(&x)
void interleave(const uint32_t idx, float* dst, const float* srcPosition, const float* srcNormal, const float* srcTexcoord, const uint32_t* srcColor0, const InterleaveGeometryArgs& cb)
#else
void interleave(const uint32_t idx, RWStructuredBuffer<float> dst, StructuredBuffer<float> srcPosition, StructuredBuffer<float> srcNormal, StructuredBuffer<float> srcTexcoord, StructuredBuffer<uint32_t> srcColor0, InterleaveGeometryArgs cb)
#endif
{
  const uint32_t srcVertexIndex = idx + cb.minVertexIndex;

  uint32_t writeOffset = 0;

  dst[idx* cb.outputStride + writeOffset++] = srcPosition[srcVertexIndex * cb.positionStride + cb.positionOffset + 0];
  dst[idx* cb.outputStride + writeOffset++] = srcPosition[srcVertexIndex * cb.positionStride + cb.positionOffset + 1];
  dst[idx* cb.outputStride + writeOffset++] = srcPosition[srcVertexIndex * cb.positionStride + cb.positionOffset + 2];

  if (cb.hasNormals) {
    dst[idx * cb.outputStride + writeOffset++] = srcNormal[srcVertexIndex * cb.normalStride + cb.normalOffset + 0];
    dst[idx * cb.outputStride + writeOffset++] = srcNormal[srcVertexIndex * cb.normalStride + cb.normalOffset + 1];
    dst[idx * cb.outputStride + writeOffset++] = srcNormal[srcVertexIndex * cb.normalStride + cb.normalOffset + 2];
  }

  if (cb.hasTexcoord) {
    dst[idx * cb.outputStride + writeOffset++] = srcTexcoord[srcVertexIndex * cb.texcoordStride + cb.texcoordOffset + 0];
    dst[idx * cb.outputStride + writeOffset++] = srcTexcoord[srcVertexIndex * cb.texcoordStride + cb.texcoordOffset + 1];
  } 

  if (cb.hasColor0) {
    dst[idx * cb.outputStride + writeOffset++] = asfloat(srcColor0[srcVertexIndex * cb.color0Stride + cb.color0Offset + 0]);
  }
}
