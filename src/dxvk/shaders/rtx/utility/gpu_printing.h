/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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

static const int16_t kInvalidThreadIndex = 32767; // ~ int16_t max

// Note: ensure alignment for C++ and Slang to match
struct GpuPrintBufferElement
{  
  float4 writtenData;

  u16vec2 threadIndex;    // Thread index of the written data
  uint frameIndex;        // Frame index when the data was written
  uint2 pad;

#ifndef __cplusplus
  [mutating]
#endif
  void invalidate()
  {
    threadIndex.x = kInvalidThreadIndex;
  }

  bool isValid() { return threadIndex.x != kInvalidThreadIndex; }
};
