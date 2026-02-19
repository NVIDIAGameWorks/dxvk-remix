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

#include "rtx/utility/shader_types.h"

// Separate RO and RW bindings to avoid shader-level read-write hazards.
// Binding 0: Read-only position data (used in all phases)
// Binding 1: Read-write normal output (used in Phase 2 only)
// Binding 2: Index buffer input (used in Phase 1)
// Binding 3: Hash table scratch (used in all phases)
#define SMOOTH_NORMALS_BINDING_POSITION_RO     0
#define SMOOTH_NORMALS_BINDING_NORMAL_RW       1
#define SMOOTH_NORMALS_BINDING_INDEX_INPUT     2
#define SMOOTH_NORMALS_BINDING_HASH_TABLE      3

/**
* \brief Args required to perform smooth normal generation
*/
struct SmoothNormalsArgs {
  uint positionOffset;
  uint positionStride;
  uint normalOffset;
  uint normalStride;
  uint indexOffset;
  uint indexStride;  // 2 for uint16, 4 for uint32
  uint numTriangles;
  uint numVertices;
  uint useShortIndices; // 1 = uint16, 0 = uint32
  uint phase;          // 1 = accumulate into hash table, 2 = scatter & normalize
  uint hashTableSize;  // must be power-of-two
  uint padding0;
};
