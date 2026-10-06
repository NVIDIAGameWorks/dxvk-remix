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
// TILE_SIZE_X and TILE_SIZE_Y must each be at most 256 so that a tile-local coordinate packs into a uint16_t.
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE_X 256
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y 128
#define COMPACT_ACTIVE_PIXELS_TILE_SIZE (COMPACT_ACTIVE_PIXELS_TILE_SIZE_X * COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y)

#if COMPACT_ACTIVE_PIXELS_TILE_SIZE_X > 256 || COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y > 256
  #error "COMPACT_ACTIVE_PIXELS_TILE_SIZE_X and _Y must be at most 256 to pack a tile-local coordinate into 16 bits."
#endif

// Thread group dimensions (hardware limit: product must be <= 1024).
// Using maximum 1024 threads so that at even 1/16 spp we end up with at least 2 active warps of 32 active pixels each. Shouldn't be less than 1 warp.
#define COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X 32
#define COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y 32
#define COMPACT_ACTIVE_PIXELS_GROUP_SIZE (COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X * COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y)

// Derived: number of pixels each thread processes, and the 2D block each thread covers within the tile.
#define COMPACT_ACTIVE_PIXELS_PIXELS_PER_THREAD (COMPACT_ACTIVE_PIXELS_TILE_SIZE / COMPACT_ACTIVE_PIXELS_GROUP_SIZE)
#define COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_X (COMPACT_ACTIVE_PIXELS_TILE_SIZE_X / COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_X)
#define COMPACT_ACTIVE_PIXELS_BLOCK_SIZE_Y (COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y / COMPACT_ACTIVE_PIXELS_THREADGROUP_SIZE_Y)

// Every compacted consumer declares this group size, and the host sizes compacted launches by it.
#define COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_X 16
#define COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_Y 8
#define COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE \
  (COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_X * COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE_Y)

// The compacted storage is a flat run of 64x64 Morton squares indexed by the global compacted index.
#define COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SHIFT 6
#define COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SIZE (1u << COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SHIFT)
#define COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_AREA \
  (COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SIZE * COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SIZE)
#define COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_AREA_SHIFT (COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SHIFT * 2)

// These are the slots of the count buffer.
// The last tile to finish publishes the total, writes the indirect args and resets the other slots.
#define COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACCUMULATOR 0
#define COMPACT_ACTIVE_PIXELS_COUNT_SLOT_TILES_DONE 1
#define COMPACT_ACTIVE_PIXELS_COUNT_SLOT_ACTIVE_PIXELS 2
#define COMPACT_ACTIVE_PIXELS_COUNT_SLOT_COUNT 3

// Inputs

#define COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_MASK_INPUT                   150

// Inputs/Outputs

#define COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COUNT_INPUT_OUTPUT           155

// Outputs

#define COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT          160
#define COMPACT_ACTIVE_PIXELS_BINDING_COMPACTED_PIXEL_INDICES_OUTPUT            161
// Holds one RG32_UINT texel per tile,
// with the active count in x and the start of the tile's run in the compacted storage in y.
#define COMPACT_ACTIVE_PIXELS_BINDING_TILE_ACTIVE_COUNTS_OUTPUT                 162
#define COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COORDS_OUTPUT                163
#define COMPACT_ACTIVE_PIXELS_BINDING_TRACE_RAYS_ARGS_OUTPUT                    164
#define COMPACT_ACTIVE_PIXELS_BINDING_DISPATCH_ARGS_OUTPUT                      165


#define COMPACT_ACTIVE_PIXELS_MIN_BINDING  COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_MASK_INPUT

#if COMPACT_ACTIVE_PIXELS_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of G-buffer bindings to avoid overlap with common bindings!"
#endif
