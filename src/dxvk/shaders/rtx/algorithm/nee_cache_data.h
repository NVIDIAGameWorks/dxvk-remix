/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#define NEE_CACHE_PROBE_RESOLUTION 32
#define NEE_CACHE_ELEMENTS 16
// Element size in bytes
#define NEE_CACHE_ELEMENT_SIZE 4 * 2
#define NEE_CACHE_TASK_SIZE 4
#define NEE_CACHE_EMPTY_TASK 0xffffffff
#define NEE_CACHE_SAMPLES 16
#define NEE_CACHE_LIGHT_ELEMENTS 16
#define NEE_CACHE_LIGHT_ELEMENT_SIZE 4 * 2
#define NEE_CACHE_CELL_CANDIDATE_TOTAL_SIZE (NEE_CACHE_ELEMENTS * NEE_CACHE_ELEMENT_SIZE + NEE_CACHE_LIGHT_ELEMENTS * NEE_CACHE_LIGHT_ELEMENT_SIZE)
#define NEE_CACHE_CELL_TASK_TOTAL_SIZE NEE_CACHE_TASK_SIZE * NEE_CACHE_ELEMENTS * 2

struct NeeCache_PackedSample
{
  uint4 hitGeometry;              ///< position and normal.
  uint4 lightInfo;                ///< radiance and pdf
};

enum class NeeEnableMode
{
  None = 0,
  SpecularOnly = 1,
  All = 2,
};
