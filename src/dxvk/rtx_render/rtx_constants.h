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
#include "../../util/xxHash/xxhash.h"

namespace dxvk {
  static constexpr uint32_t kInvalidFrameIndex = UINT32_MAX;

  // Sentinel value representing a null hash
  static constexpr XXH64_hash_t kEmptyHash = 0;

  // Note: 0xFFFF used for inactive buffer and surface material index to indicate to the GPU that no buffer/material is in use
  // for a specific variable (as some are optional). Also used for debugging to provide wildly out of range values in case one is not set.
  constexpr static uint32_t kSurfaceInvalidBufferIndex = 0xFFFFu;
  constexpr static uint32_t kSurfaceInvalidSurfaceMaterialIndex = 0xFFFFu;

  // Limit for unique buffers minus some padding
  constexpr static uint32_t kBufferCacheLimit = kSurfaceInvalidBufferIndex - 10; 

} // namespace dxvk
