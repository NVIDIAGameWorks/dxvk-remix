/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#ifndef DLSS_H
#define DLSS_H

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx/concept/camera/camera.h"
#else
#include "rtx/concept/camera/camera.slangh"
#endif

#define DLSS_NORMALS_INPUT                      0
#define DLSS_NORMALS_OUTPUT                     1
#define DLSS_VIRTUAL_NORMALS_INPUT              2
#define DLSS_CONSTANTS                          3
#define DLSS_NUM_BINDINGS                       4

// Constant buffers
struct DLSSArgs {
  Camera camera;

  uvec3 padding;
  uint useExternalExposure;
};

#endif  // TONEMAPPING_H