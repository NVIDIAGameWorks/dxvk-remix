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
#ifndef VOLUME_ARGS_H
#define VOLUME_ARGS_H

#include "rtx/utility/shader_types.h"

static const uint froxelVolumeMain = 0;
static const uint froxelVolumePortal0 = 1;
static const uint froxelVolumePortal1 = 2;
static const uint froxelVolumeCount = 3;

// Note: Ensure 16B alignment
struct VolumeArgs {
  VolumeDefinitionCamera cameras[froxelVolumeCount];

  uint2 froxelGridDimensions;
  vec2 inverseFroxelGridDimensions;

  uint numFroxelVolumes;       // 1 if there is just the main camera volume in the texture, 3 if there are also per-portal volumes
  uint numActiveFroxelVolumes; // Same logic as numFroxelVolumes but only counting active volumes
  uint froxelDepthSlices;
  uint maxAccumulationFrames; // Note: Only an 8 bit value.

  float froxelDepthSliceDistributionExponent;
  float froxelMaxDistance;
  float froxelFireflyFilteringLuminanceThreshold;
  float froxelFilterGaussianSigma;

  vec3 attenuationCoefficient;
  uint enableVolumetricLighting;

  vec3 scatteringCoefficient;
  uint8_t minReservoirSamples;
  uint8_t maxReservoirSamples;
  uint8_t minKernelRadius;
  uint8_t maxKernelRadius;

  // Note: The range of history values is [0, maxAccumulationFrames].
  float minReservoirSamplesStabilityHistory;
  float reservoirSamplesStabilityHistoryRange;
  float minKernelRadiusStabilityHistory;
  float kernelRadiusStabilityHistoryRange;

  float reservoirSamplesStabilityHistoryPower;
  float kernelRadiusStabilityHistoryPower;
  uint enableVolumeRISInitialVisibility;
  uint enableVolumeTemporalResampling;

  // Note: Min/max filtered radiance U coordinate used to simulate clamp to edge behavior without artifacts
  // by clamping to the center of the first/last froxel on the U axis when dealing with the multiple side by
  // side froxel grids in a single 3D texture.
  float minFilteredRadianceU;
  float maxFilteredRadianceU;
  float inverseNumFroxelVolumes;
  float pad;

  // Note: This value is already converted into linear space so it is fine to directly add in as a contribution to the volumetrics.
  vec3 multiScatteringEstimate;
  float pad1;
};

#ifdef __cplusplus
// We're packing these into a constant buffer (see: raytrace_args.h), so need to remain aligned
static_assert((sizeof(VolumeArgs) & 15) == 0);
#endif

#endif
