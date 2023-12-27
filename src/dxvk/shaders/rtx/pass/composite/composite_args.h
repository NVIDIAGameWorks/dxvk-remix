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

#include "rtx/utility/shader_types.h"
#include "rtx/pass/volume_args.h"

#define DENOISER_MODE_OFF 0
#define DENOISER_MODE_RELAX 1
#define DENOISER_MODE_REBLUR 2

struct CompositeArgs {
  Camera camera;

  mat4 projectionToViewJittered;
  mat4 viewToWorld;

  vec2 resolution;
  float nearPlane;
  float postFilterThreshold;

  // Fog
  vec3 fogColor;
  uint fogMode;

  float fogScale;
  float fogEnd;
  float fogDensity;
  float maxFogDistance;

  vec4 debugKnob;

  uint usePostFilter;
  uint demodulateRoughness;
  float roughnessDemodulationOffset;
  uint combineLightingChannels;

  VolumeArgs volumeArgs;

  // One of DENOISER_MODE constants, affects signal conversion
  uint primaryDirectDenoiser;
  uint primaryIndirectDenoiser;
  uint secondaryCombinedDenoiser;  
  uint enableRtxdi;

  float primaryDirectMissLinearViewZ;
  uint enableReSTIRGI;
  float pixelHighlightReuseStrength;
  uint debugViewIdx;

  uint8_t compositePrimaryDirectDiffuse;
  uint8_t compositePrimaryDirectSpecular;
  uint8_t compositePrimaryIndirectDiffuse;
  uint8_t compositePrimaryIndirectSpecular;
  uint8_t compositeSecondaryCombinedDiffuse;
  uint8_t compositeSecondaryCombinedSpecular;
  uint16_t pad;
  uint enableSeparatedDenoisers;
  uint frameIdx;

  uint enableStochasticAlphaBlend;
  uint stochasticAlphaBlendEnableFilter;
  uint stochasticAlphaBlendUseNeighborSearch;
  uint stochasticAlphaBlendSearchTheSameObject;

  uint stochasticAlphaBlendSearchIteration;
  float stochasticAlphaBlendInitialSearchRadius;
  float stochasticAlphaBlendRadiusExpandFactor;
  uint stochasticAlphaBlendShareNeighbors;

  float stochasticAlphaBlendNormalSimilarity;
  float stochasticAlphaBlendDepthDifference;
  float stochasticAlphaBlendPlanarDifference;
  uint stochasticAlphaBlendUseRadianceVolume;

  float stochasticAlphaBlendRadianceVolumeMultiplier;
  uint stochasticAlphaBlendDiscardBlackPixel;
  uint pad1;
  uint pad2;

  vec3 clearColorFinalColor;
  uint pad3;
};
