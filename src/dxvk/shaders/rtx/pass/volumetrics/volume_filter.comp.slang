/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/volumetrics/volume_filter_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/math.slangh"

[shader("compute")]
[numthreads(8, 8, 2)]
void main(uint3 ipos : SV_DispatchThreadID)
{
  const ivec3 threadIndex = ipos;
  const VolumeArgs volumeArgs = cb.volumeArgs;
  const uvec3 froxelDimensions = uvec3(volumeArgs.froxelGridDimensions, volumeArgs.froxelDepthSlices);
  const uint froxelVolume = threadIndex.x / froxelDimensions.x;

  if (any(threadIndex.yz >= froxelDimensions.yz) || froxelVolume > volumeArgs.numActiveFroxelVolumes)
  {
    return;
  }
  
  const uint froxelVolumeLeft = froxelVolume * volumeArgs.froxelGridDimensions.x;
  const uint froxelVolumeRight = froxelVolumeLeft + volumeArgs.froxelGridDimensions.x;

  // Sample from the current froxel cell

  const vec4 currentAccumulatedRadianceHistoryAge = AccumulatedRadiance[threadIndex];
  const float currentHistoryAge = currentAccumulatedRadianceHistoryAge.w;

  // Calculate the kernel radius based on the stability information

  const float linearKernelRadiusStabilityWeight = saturate((currentHistoryAge - volumeArgs.minKernelRadiusStabilityHistory) / volumeArgs.kernelRadiusStabilityHistoryRange);
  const float kernelRadiusStabilityWeight = pow(linearKernelRadiusStabilityWeight, volumeArgs.kernelRadiusStabilityHistoryPower);
  // Note: Max kernel radius used when the weight is 0, the minimum kernel radius is used when the weight is 1.
  const uint kernelRadius = mix(volumeArgs.maxKernelRadius, volumeArgs.minKernelRadius, kernelRadiusStabilityWeight);

  // Sample neighboring cells

  const int kernelWidth = kernelRadius;
  const int kernelHeight = kernelRadius;
  const int kernelDepth = 1;
  // Note: Accumulated radiance stored in gamma space for better filtering during accumulation, not too relevant here.
  float3 radianceSum = gammaToLinearFast(currentAccumulatedRadianceHistoryAge.xyz);
  // Note: Center sample always given a weight of 1
  float weightSum = 1.0f;

  for (int x = -kernelWidth; x <= kernelWidth; x++)
  {
    for (int y = -kernelHeight; y <= kernelHeight; y++)
    {
      for (int z = -kernelDepth; z <= kernelDepth; z++)
      {
        // Skip the center cell as it has already been accounted for

        if (all(int3(x, y, z) == int3(0, 0, 0)))
        {
          continue;
        }

        // Skip cells outside the froxel grid

        const int3 voxelIdx = threadIndex + int3(x, y, z);

        if (any(voxelIdx.yz < int2(0)) || any(voxelIdx.yz >= froxelDimensions.yz) ||
          voxelIdx.x < froxelVolumeLeft || voxelIdx.x >= froxelVolumeRight)
        {
          continue;
        }

        const vec4 neighborAccumulatedRadianceHistoryAge = AccumulatedRadiance[voxelIdx];
        const float neighborHistoryAge = neighborAccumulatedRadianceHistoryAge.w;

        // Weight samples by spatial position and age

        // Note: Use gaussian function to weight spatially.
        const float spatialWeight = calcGaussianWeight(threadIndex, voxelIdx, volumeArgs.froxelFilterGaussianSigma);

        // Note: Use a combination of relative age difference and absolute age to weight age wise. This
        // ensures that cells with a high age do not weight cells with a low weight very much, but also
        // ensures that cells with similar ages (like many brand new cells) weight eachother highly to
        // filter out initial noise quicker.
        // Plot with: z = min(x + (1 - y), 1) within [0, 0, 0] to [1, 1, 1]
        const float relativeWeight = abs(neighborHistoryAge - currentHistoryAge) / volumeArgs.maxAccumulationFrames;
        const float absoluteWeight = neighborHistoryAge / volumeArgs.maxAccumulationFrames;
        const float ageWeight = min((1.0f - relativeWeight) + absoluteWeight, 1.0f);

        const float weight = spatialWeight * ageWeight;

        // Note: Accumulated radiance stored in gamma space for better filtering during accumulation, not too relevant here.
        radianceSum += gammaToLinearFast(neighborAccumulatedRadianceHistoryAge.xyz) * weight;
        weightSum += weight;
      }
    }
  }

  // Average the accumulated radiance based on the weight

  const float kWeightEpsilon = 1e-6;

  // Note: Store radiance in gamma space for slightly better perceptual encoding and filtering for particle lighting (as particle lighting
  // reads directly from the filtered texture rather than the preintegrated texture).
  RWFilteredAccumulatedRadiance[threadIndex] = weightSum > kWeightEpsilon ? linearToGammaFast(radianceSum / weightSum) : 0.0f;
}
