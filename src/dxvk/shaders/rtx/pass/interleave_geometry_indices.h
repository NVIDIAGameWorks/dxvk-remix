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

struct InterleaveGeometryArgs {
  uint32_t positionOffset;
  uint32_t positionStride;
  uint32_t positionFormat;

  uint32_t hasNormals;
  uint32_t normalOffset;
  uint32_t normalStride;
  uint32_t normalFormat;

  uint32_t hasTexcoord;
  uint32_t texcoordOffset;
  uint32_t texcoordStride;
  uint32_t texcoordFormat;

  uint32_t hasColor0;
  uint32_t color0Offset;
  uint32_t color0Stride;
  uint32_t color0Format;

  uint32_t minVertexIndex;
  uint32_t outputStride;
  uint32_t vertexCount;
};

#define INTERLEAVE_GEOMETRY_BINDING_OUTPUT           0
#define INTERLEAVE_GEOMETRY_BINDING_POSITION_INPUT   1
#define INTERLEAVE_GEOMETRY_BINDING_NORMAL_INPUT     2
#define INTERLEAVE_GEOMETRY_BINDING_TEXCOORD_INPUT   3
#define INTERLEAVE_GEOMETRY_BINDING_COLOR0_INPUT     4
