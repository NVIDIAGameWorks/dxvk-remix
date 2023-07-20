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

#include "rtx/pass/common_binding_indices.h"

#define UPDATE_NEE_CACHE_BINDING_NEE_CACHE 16
#define UPDATE_NEE_CACHE_BINDING_NEE_CACHE_TASK 17
#define UPDATE_NEE_CACHE_BINDING_NEE_CACHE_SAMPLE 18
#define UPDATE_NEE_CACHE_BINDING_NEE_CACHE_THREAD_TASK 19
#define UPDATE_NEE_CACHE_BINDING_PRIMITIVE_ID_PREFIX_SUM 20

#define UPDATE_NEE_CACHE_MIN_BINDING                           UPDATE_NEE_CACHE_BINDING_NEE_CACHE

#if UPDATE_NEE_CACHE_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of update NEE cache bindings to avoid overlap with common bindings!"
#endif

