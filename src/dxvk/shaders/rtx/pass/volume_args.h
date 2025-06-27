/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

static const uint froxelVolumeMain = 0;
static const uint froxelVolumePortal0 = 1;
static const uint froxelVolumePortal1 = 2;
static const uint froxelVolumeCount = 3;

// Note: Ensure 16B alignment
struct VolumeArgs {
  VolumeDefinitionCamera cameras[froxelVolumeCount];
  VolumeDefinitionCamera restirCameras[froxelVolumeCount];

  uint2 froxelGridDimensions;
  vec2 inverseFroxelGridDimensions;

  uint2 restirFroxelGridDimensions;
  vec2 restirInverseFroxelGridDimensions;

  uint numFroxelVolumes;       // 1 if there is just the main camera volume in the texture, 3 if there are also per-portal volumes
  uint numActiveFroxelVolumes; // Same logic as numFroxelVolumes but only counting active volumes
  uint froxelDepthSlices;
  uint maxAccumulationFrames; // Note: Only an 8 bit value.

  float froxelDepthSliceDistributionExponent;
  float froxelMaxDistance;
  float froxelFireflyFilteringLuminanceThreshold;
  uint16_t enableVolumeRISInitialVisibility;
  uint16_t enableVolumeTemporalResampling;
  vec3 attenuationCoefficient;
  uint16_t enable;
  uint16_t enablevisibilityReuse;

  vec3 scatteringCoefficient;
  uint16_t enableVolumeSpatialResampling;
  uint16_t enableReferenceMode;

  // Note: Min/max filtered radiance U coordinate used to simulate clamp to edge behavior without artifacts
  // by clamping to the center of the first/last froxel on the U axis when dealing with the multiple side by
  // side froxel grids in a single 3D texture.
  float minFilteredRadianceU;
  float maxFilteredRadianceU;
  float inverseNumFroxelVolumes;
  uint numSpatialSamples;

  // Note: This value is already converted into linear space so it is fine to directly add in as a contribution to the volumetrics.
  vec3 multiScatteringEstimate;
  float spatialSamplingRadius;

  uint restirFroxelDepthSlices;
  float volumetricFogAnisotropy;
  uint16_t enableNoiseFieldDensity;
  uint16_t enableAtmosphere;
  float depthOffset;

  float noiseFieldSubStepSize;
  uint noiseFieldOctaves;
  // Note: When set to 0 this indicates that no time modulation of the noise field should be used.
  // Otherwise, scales the time modulation in noise coordinates per second.
  float noiseFieldTimeScale;
  float noiseFieldDensityScale;

  float noiseFieldDensityExponent;
  float noiseFieldInitialFrequency;
  float noiseFieldLacunarity;
  float noiseFieldGain;

  vec3 sceneUpDirection;
  float atmosphereHeight;

  vec3 planetCenter;
  // Note: Squared radius used as most functions involving atmospheric intersection use a squared radius
  // in their math, simplifying the work that needs to be done on the GPU.
  float atmosphereRadiusSquared;

  float maxAttenuationDistanceForNoAtmosphere;
  uint resetHistory;
  vec2 pad0;
};

#ifdef __cplusplus
// We're packing these into a constant buffer (see: raytrace_args.h), so need to remain aligned
static_assert((sizeof(VolumeArgs) & 15) == 0);
#endif
