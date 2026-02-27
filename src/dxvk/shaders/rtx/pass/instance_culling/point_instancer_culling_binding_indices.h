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
#pragma once

#include "rtx/pass/common_binding_indices.h"
#include "rtx/utility/shader_types.h"

struct PointInstancerCullingConstants {
  mat4 objectToWorld;         // Column-major object-to-world matrix for this instancer
  mat4 prevObjectToWorld;     // Column-major previous-frame object-to-world (for motion vectors)
  vec3 cameraPosition;
  float cullingRadius;        // Maximum distance from camera before culling
  uint totalInstanceCount;    // Number of input transforms
  uint baseSurfaceIndex;      // surfaceIndexOfFirstInstance for instanceCustomIndex
  float fadeStartRadius;      // Distance at which density starts reducing (0 = no fade)
  uint customIndexFlags;      // Upper bits of instanceCustomIndex (material type, view model flag)
  uint instanceMask;          // 8-bit visibility mask for this instance
  uint sbtOffsetAndFlags;     // Packed: instanceShaderBindingTableRecordOffset:24 | flags:8
  uint blasRefLo;             // Lower 32 bits of BLAS device address
  uint blasRefHi;             // Upper 32 bits of BLAS device address
  uint instanceBufferOffset;  // Byte offset of first placeholder in the TLAS instance buffer
  uint pad0;
  uint pad1;
  uint pad2;
};

#define POINT_INSTANCER_CULLING_BINDING_CONSTANTS         50
#define POINT_INSTANCER_CULLING_BINDING_TRANSFORMS_INPUT  51
#define POINT_INSTANCER_CULLING_BINDING_INSTANCE_BUFFER   52
#define POINT_INSTANCER_CULLING_BINDING_SURFACE_BUFFER    53
#define POINT_INSTANCER_CULLING_BINDING_MATERIAL_BUFFER   54

#define POINT_INSTANCER_CULLING_MIN_BINDING  POINT_INSTANCER_CULLING_BINDING_CONSTANTS

#if POINT_INSTANCER_CULLING_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of point instancer culling bindings to avoid overlap with common bindings!"
#endif
