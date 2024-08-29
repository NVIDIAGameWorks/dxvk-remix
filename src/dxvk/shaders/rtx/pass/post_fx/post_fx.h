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

#define POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_INPUT           0
#define POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_FILTERED_OUTPUT 1

#define POST_FX_MOTION_BLUR_PRIMARY_SCREEN_SPACE_MOTION_INPUT 0
#define POST_FX_MOTION_BLUR_PRIMARY_SURFACE_FLAGS_INPUT       1
#define POST_FX_MOTION_BLUR_PRIMARY_LINEAR_VIEW_Z_INPUT       2
#define POST_FX_MOTION_BLUR_BLUE_NOISE_TEXTURE_INPUT          3
#define POST_FX_MOTION_BLUR_INPUT                             4
#define POST_FX_MOTION_BLUR_OUTPUT                            5
#define POST_FX_MOTION_BLUR_NEAREST_SAMPLER                   6
#define POST_FX_MOTION_BLUR_LINEAR_SAMPLER                    7

#define POST_FX_INPUT  0
#define POST_FX_OUTPUT 1

#define POST_FX_HIGHLIGHT_INPUT                       0
#define POST_FX_HIGHLIGHT_OBJECT_PICKING_INPUT        1
#define POST_FX_HIGHLIGHT_PRIMARY_CONE_RADIUS_INPUT   2
#define POST_FX_HIGHLIGHT_OUTPUT                      3
#define POST_FX_HIGHLIGHT_VALUES                      4

#define POST_FX_TILE_SIZE 8

struct PostFxArgs {
  // Display image information
  uint2  imageSize;
  float2 invImageSize;

  // Camera Resolution
  float2 invMainCameraResolution;
  float2 inputOverOutputViewSize;

  // Post Fx Attributes
  // Motion Blur
  uint   motionBlurSampleCount;
  float  blurDiameterFraction;
  bool   enableMotionBlurNoiseSample;
  float  motionBlurMinimumVelocityThresholdInPixel;

  // Chromatic Aberration
  float2 chromaticAberrationScale;
  float  chromaticCenterAttenuationAmount;
  float  exposureFraction;
  
  // Vignette
  float  vignetteIntensity;
  float  vignetteRadius;
  float  vignetteSoftness;
  uint   frameIdx;

  float  motionBlurDynamicDeduction;
  bool   enableMotionBlurEmissive;
  float  jitterStrength;
  float  motionBlurDlfgDeduction;
};

struct PostFxMotionBlurPrefilterArgs {
  uint2 imageSize;
  int2  pixelStep;
};

#define POST_FX_HIGHLIGHTING_MAX_VALUES_POW 14
#define POST_FX_HIGHLIGHTING_MAX_VALUES     (1 << POST_FX_HIGHLIGHTING_MAX_VALUES_POW)
#define POST_FX_HIGHLIGHTING_INVALID_VALUE  0xFFFFFFFF

struct PostFxHighlightingArgs
{
  // Display image information
  uint2 imageSize;
  // If need to highlight an object under this pixel
  int2  pixel;
  // Highlighting params
  uint  desaturateNonHighlighted;
  float timeSinceStartMS;
  uint  highlightColorPacked;
  uint  valuesToHighlightCountPow;
};
