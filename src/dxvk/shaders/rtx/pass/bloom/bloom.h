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
#ifndef BLOOM_H
#define BLOOM_H

#include "rtx/utility/shader_types.h"

#define BLOOM_DOWNSCALE_INPUT              0
#define BLOOM_DOWNSCALE_OUTPUT             1

#define BLOOM_BLUR_INPUT                   0
#define BLOOM_BLUR_OUTPUT                  1

#define BLOOM_COMPOSITE_COLOR_INPUT_OUTPUT 0
#define BLOOM_COMPOSITE_BLOOM              1

// Constant buffers

struct BloomDownscaleArgs {
  int2 inputSize;
};

struct BloomBlurArgs {
  int2 imageSize;
  float2 invImageSize;
  float2 pixstep;
  float argumentScale;
  float normalizationScale;
  int numSamples;
};

struct BloomCompositeArgs {
  int2 imageSize;
  float2 invImageSize;
  float blendFactor;
};


#endif  // BLOOM_H