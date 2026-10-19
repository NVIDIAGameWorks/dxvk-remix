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

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace bridge_util {
  // Surface uploads through the data queue are split into parts and blobs.
  //
  // The reader of the data queue gets no further than the start of the command being written, so one command, the
  // tail its blobs skip at the end of the queue included, must fit in the queue. A surface lock therefore travels as
  // consecutive IDirect3DSurface9_UnlockRect commands of at most kMaxUploadPartBytes of rows each. The server locks
  // the surface for the first part and unlocks it after the last one.
  //
  // Within a part the rows travel in blobs of at most kMaxUploadBlobBytes. A blob that does not fit before the end of
  // the queue starts over at its beginning, so small blobs keep the skipped tail small.
  constexpr size_t kMaxUploadPartBytes = size_t(16) << 20;
  constexpr size_t kMaxUploadBlobBytes = size_t(1) << 20;

  // Whole rows of rowSize bytes that fit in maxBytes, and at least one.
  inline uint32_t rowsPerChunk(const size_t rowSize, const size_t maxBytes) {
    return rowSize != 0 ? (uint32_t) std::max<size_t>(1, maxBytes / rowSize) : 1;
  }
}
