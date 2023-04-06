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
#pragma once

#include "rtx/pass/nrd_args.h"

// Display Types
static const uint displayTypeStandard = 0u;
static const uint displayTypeBGRExclusiveColor = 1u;
static const uint displayTypeEV100 = 2u;
static const uint displayTypeHDRWaveform = 3u;

struct DebugViewArgs {
  uint debugViewIdx;
  int colorCodeRadius;
  float animationTimeSec;
  // EV100 Display
  // Note: Center and range are in EV.
  int evMinValue;

  uvec2 debugViewResolution;
  uint displayType;
  uint frameIdx;

  // Standard Display
  float minValue;
  float maxValue;
  float scale;
  uint evRange;

  // HDR Waveform Display
  // Note: Log 10 of radiance value used rather than EV or a base 2 log scale.
  int log10MinValue;
  uint log10Range;
  // Note: Must be a scalar multiple of the debug view resolution, so either half, quarter, etc so that
  // math works properly without causing weird artifacts due to nearest neighbor sampling.
  uvec2 hdrWaveformResolution;
  uvec2 hdrWaveformPosition;
  uint hdrWaveformResolutionScaleFactor;
  float hdrWaveformHistogramNormalizationScale;

  NrdArgs nrd;

  // Common Display enable flags
  uint enableInfNanViewFlag;

  // Standard Display enable flags
  uint enablePseudoColorFlag;
  uint enableAlphaChannelFlag;

  // HDR Waveform Display enable flags
  uint enableLuminanceModeFlag;

  vec4 debugKnob;

  // Feature enablement
  uint isRTXDIConfidenceValid;
};
