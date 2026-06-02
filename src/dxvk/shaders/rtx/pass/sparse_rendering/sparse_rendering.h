/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx/pass/nrc_args.h"

enum class SparseRenderingMode : uint32_t {
  Off = 0,
  Uniform = 1,
};

// Selects the [0,1) sample source used to threshold per-pixel sampling rates.
enum class PerPixelRateNoiseSource : uint32_t {
  WhiteNoise = 0,    //  wangHash ~ white-noise per-pixel hash.
  BlueNoise128x128x64x8 = 1, // 8 bit 128x128 blue-noise 64 frame length.
};

// Note: ensure 16B alignment
struct SparseRenderingArgs
{
  NrcArgs nrcArgs;

  SparseRenderingMode mode;
  PerPixelRateNoiseSource perPixelRateNoiseSource;
  float directPixelSamplingRate;
  float indirectPixelSamplingRate;

  uint forceNrcTrainingPixelsActive;
  uint enableSparsePrimaryRayMissComposition;
  uint enableSparseSecondaryLighting;
  uint enableRtxdiReuseForInactivePixels;

  uint enableSparseVolumetricsPrimaryHit;
  uint enableSparseVolumetricsPrimaryMiss;
  uint enableSparsePrimarySpecularAlbedo;
  uint pad0;

  // Dimensions of the active-pixel mask buffer in mask elements (ceil(resolution / blockSize)).
  uvec2 activePixelMaskExtent;
  uvec2 pad1;
};
