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
#ifndef VOLUME_PREINTEGRATE_BINDING_INDICES_H
#define VOLUME_PREINTEGRATE_BINDING_INDICES_H

#include "rtx/pass/common_binding_indices.h"

// Inputs

#define VOLUME_PREINTEGRATE_BINDING_FILTERED_RADIANCE_INPUT 40

// Outputs

#define VOLUME_PREINTEGRATE_BINDING_PREINTEGRATED_RADIANCE_OUTPUT   41

#define VOLUME_PREINTEGRATE_MIN_BINDING                           VOLUME_PREINTEGRATE_BINDING_FILTERED_RADIANCE_INPUT
#define VOLUME_PREINTEGRATE_MAX_BINDING                           VOLUME_PREINTEGRATE_BINDING_PREINTEGRATED_RADIANCE_OUTPUT

#if VOLUME_PREINTEGRATE_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of Volume Preintegrate bindings to avoid overlap with common bindings!"
#endif

#endif
