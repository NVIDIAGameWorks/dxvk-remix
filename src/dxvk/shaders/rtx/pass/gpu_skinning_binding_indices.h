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

#include "rtx/utility/shader_types.h"

#define BINDING_SKINNING_CONSTANTS    0
#define BINDING_POSITION_OUTPUT       1
#define BINDING_POSITION_INPUT        2
#define BINDING_BLEND_WEIGHT_INPUT    3
#define BINDING_BLEND_INDICES_INPUT   4
#define BINDING_NORMAL_OUTPUT         5
#define BINDING_NORMAL_INPUT          6

/**
* \brief Args required to perform skinning
*/
struct SkinningArgs {
  mat4 bones[256]; // 256 is the max bone count in DX (swvp)

  uint dstPositionOffset;
  uint dstPositionStride;
  uint srcPositionOffset;
  uint srcPositionStride;

  uint dstNormalOffset;
  uint dstNormalStride;
  uint srcNormalOffset;
  uint srcNormalStride;

  uint blendWeightOffset;
  uint blendWeightStride;
  uint blendIndicesOffset;
  uint blendIndicesStride;

  uint numVertices;
  uint useIndices;
  uint numBones;
  uint pad0;
};