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
#ifndef VOLUME_INTEGRATE_BINDING_INDICES_H
#define VOLUME_INTEGRATE_BINDING_INDICES_H

#include "rtx/pass/common_binding_indices.h"

// Inputs

#define VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_Y        40
#define VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_CO_CG    41
#define VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_AGE      42
#define VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT             43

// Outputs

#define VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_Y        44
#define VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_CO_CG    45
#define VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_AGE      46
#define VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT             47

#define VOLUME_INTEGRATE_MIN_BINDING                           VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_Y
#define VOLUME_INTEGRATE_MAX_BINDING                           VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT

#if VOLUME_INTEGRATE_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of Volume Integrate bindings to avoid overlap with common bindings!"
#endif

#endif
