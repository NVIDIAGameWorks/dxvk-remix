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

#define ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH 8
#define ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT 8

#define ACTIVE_PIXEL_MASK_BLOCK_WIDTH 4
#define ACTIVE_PIXEL_MASK_BLOCK_HEIGHT 2

// Inputs

#define ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_INPUT     204
#define ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_INPUT   205
#define ACTIVE_PIXEL_MASK_BINDING_SHARED_FLAGS_INPUT                   210

// Outputs

#define ACTIVE_PIXEL_MASK_BINDING_DIRECT_ACTIVE_PIXEL_MASK_OUTPUT      202
#define ACTIVE_PIXEL_MASK_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_OUTPUT    208
#define ACTIVE_PIXEL_MASK_BINDING_UNION_ACTIVE_PIXEL_MASK_OUTPUT       209
#define ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_OUTPUT    206
#define ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_OUTPUT  207

#define ACTIVE_PIXEL_MASK_MIN_BINDING ACTIVE_PIXEL_MASK_BINDING_DIRECT_ACTIVE_PIXEL_MASK_OUTPUT

#if ACTIVE_PIXEL_MASK_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of Sparse Pixel Mask bindings to avoid overlap with common bindings!"
#endif
