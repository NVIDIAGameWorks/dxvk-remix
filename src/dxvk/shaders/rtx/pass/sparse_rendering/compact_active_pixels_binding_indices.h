/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

// Tile size: pixel area each workgroup covers. Can exceed thread count to process multiple pixels per thread.
// Must satisfy: TILE_SIZE_X <= 256, TILE_SIZE_Y <= 128 (uint16_t local coord packing constraint).
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE_X 256
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y 128
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE (COMPACT_ACTIVE_PIXELS_TILE_SIZE_X * COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y)

// Thread group dimensions (hardware limit: product must be <= 1024).
// Using maximum 1024 threads so that at even 1/16 spp we end up with at least 2 active warps of 32 active pixels each. Shouldn't be less than 1 warp.
#define COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X 32
#define COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y 32
#define COMPACT_ACTIVE_PIXELS_GROUP_SIZE (COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X * COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y)

// Derived: number of pixels each thread processes, and the 2D block each thread covers within the tile.
#define COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD (COMPACT_ACTIVE_PIXELS_TILE_SIZE / COMPACT_ACTIVE_PIXELS_GROUP_SIZE)
#define COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X (COMPACT_ACTIVE_PIXELS_TILE_SIZE_X / COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X)
#define COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y (COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y / COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y)

// Inputs

#define COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_PIXEL_MASK_INPUT            151
#define COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_INPUT          152

// Outputs

#define COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT    200
#define COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT  201
#define COMPACT_ACTIVE_PIXELS_BINDING_UNION_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT     203


#define COMPACT_ACTIVE_PIXELS_MIN_BINDING  COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_PIXEL_MASK_INPUT

#if COMPACT_ACTIVE_PIXELS_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of G-buffer bindings to avoid overlap with common bindings!"
#endif
