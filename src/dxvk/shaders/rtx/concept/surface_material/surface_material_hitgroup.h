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

// Material resolves to be included in shader code paths set by
// #define SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK 
// to some or a combination of below
#define SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE        (1 << 0)
#define SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT   (1 << 1)
#define SURFACE_MATERIAL_RESOLVE_TYPE_RAY_PORTAL    (1 << 2)

#define SURFACE_MATERIAL_RESOLVE_TYPE_ALL           (SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE | SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT | SURFACE_MATERIAL_RESOLVE_TYPE_RAY_PORTAL)
#define SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT (SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE | SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT)