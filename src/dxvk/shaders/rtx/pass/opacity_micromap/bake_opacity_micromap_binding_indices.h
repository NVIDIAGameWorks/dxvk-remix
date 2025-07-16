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
#pragma once

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx_materials.h"
#endif

#define BINDING_BAKE_OPACITY_MICROMAP_TEXCOORD_INPUT              0
#define BINDING_BAKE_OPACITY_MICROMAP_OPACITY_INPUT               1
#define BINDING_BAKE_OPACITY_MICROMAP_SECONDARY_OPACITY_INPUT     2
#define BINDING_BAKE_OPACITY_MICROMAP_BINDING_SURFACE_DATA_INPUT  3
#define BINDING_BAKE_OPACITY_MICROMAP_CONSTANTS                   4

#define BINDING_BAKE_OPACITY_MICROMAP_ARRAY_OUTPUT                5

#define BAKE_OPACITY_MICROMAP_NUM_THREAD_PER_COMPUTE_BLOCK        256

struct BakeOpacityMicromapArgs {
#ifdef __cplusplus
  uint8_t surface[dxvk::kSurfaceGPUSize];
#else
  Surface surface;
#endif

  uint numTriangles;
  uint numMicroTrianglesPerTriangle;
  uint is2StateOMMFormat;
  uint subdivisionLevel;

  uint texcoordOffset;
  uint texcoordStride;
  float resolveTransparencyThreshold;  // Anything smaller or equal is transparent
  float resolveOpaquenessThreshold;    // Anything greater or equal is opaque

  uint useConservativeEstimation;
  uint applyVertexAndTextureOperations;
  uint numMicroTrianglesPerThread;
  uint16_t isOpaqueMaterial;
  uint16_t isRayPortalMaterial;

  vec2 textureResolution;
  vec2 rcpTextureResolution;

  uint triangleOffset;
  uint threadIndexOffset;
  uint conservativeEstimationMaxTexelTapsPerMicroTriangle;
  uint numActiveThreads;
};