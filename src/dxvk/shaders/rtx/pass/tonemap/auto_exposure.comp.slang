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
#include "rtx/pass/tonemap/tonemapping.slangh"

layout(binding = AUTO_EXPOSURE_HISTOGRAM_INPUT_OUTPUT)
RWTexture1D<uint> InOutHistogram;

layout(binding = AUTO_EXPOSURE_EXPOSURE_INPUT_OUTPUT)
RWTexture1D<float> OutExposure;

layout(binding = AUTO_EXPOSURE_EC_INPUT)
Sampler1D<float> InExposureCompensation;

layout(binding = AUTO_EXPOSURE_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> DebugView;

layout(push_constant)
ConstantBuffer<ToneMappingAutoExposureArgs> cb;

groupshared float g_localData[EXPOSURE_HISTOGRAM_SIZE];

// Computes the inclusive prefix sum of val across the entire group.
// For instance, [3, 10, 5, 1, ...] maps to [3, 13, 18, 19, ...]
float computePrefixSum(float val, const uint linearIndex)
{
  GroupMemoryBarrierWithGroupSync();

  // NOTE: WavePrefixSum computes an _ex_clusive prefix sum, so add the original 'val'
  val = WavePrefixSum(val) + val;
  g_localData[linearIndex] = val;
  GroupMemoryBarrierWithGroupSync();

  for (uint k = WaveGetLaneCount(); k < EXPOSURE_HISTOGRAM_SIZE; k *= 2)
  {
    uint blockIndex = linearIndex / k;
    // Propagate prefixSum to the next block for every other block so as to avoid 
    // writing into shared memory cells that are being read in this iteration.
    // This way ith + 1 block will contain prefixSum considering ith block. 
    // Then the next iteration will double the block size.    
    if ((blockIndex % 2) == 1)
        g_localData[linearIndex] += g_localData[k*blockIndex - 1];
    
    GroupMemoryBarrierWithGroupSync();
  }

  return g_localData[EXPOSURE_HISTOGRAM_SIZE-1];
}

float getHistogramWeight(uint index) {
  float weightUV = (index + 0.5) / float(EXPOSURE_HISTOGRAM_SIZE);
  return InExposureCompensation.SampleLevel(weightUV, 0).r;
}

[shader("compute")]
[numthreads(EXPOSURE_HISTOGRAM_SIZE, 1, 1)]
void main(uint2 threadId : SV_DispatchThreadID, uint linearIndex : SV_GroupIndex)
{
  float weight = cb.useExposureCompensation ? getHistogramWeight(linearIndex) : 1.0f;

  float countThisBucket = (float)InOutHistogram[linearIndex] * weight;
  float totalWeight = computePrefixSum(countThisBucket, linearIndex);

  float adaptedCountThisBucket = countThisBucket;
  if (cb.averageMode == 0) // mean
  {
    adaptedCountThisBucket *= linearIndex; // factor in the index so we can compute the mean bucket quickly
  }

  // Populate the g_localData with   
  const float weightedSum = computePrefixSum(adaptedCountThisBucket, linearIndex);

  float normalizedEV = 0;
  if (linearIndex == 0) 
  {

    uint discardSampleCount = countThisBucket; // discarded (e.g. black) pixel count, since this is the 0th bucket

    float weightedAverage;

    if (cb.averageMode == 1) 
    {
      float median = (weightedSum - discardSampleCount) / 2.f;
      weightedAverage = 0.5f; // Start with the first bucket center

      for (uint i=1 ; i<EXPOSURE_HISTOGRAM_SIZE ; i++) 
      {
        if (g_localData[i] > median)
        {
          break;
        }
        
        weightedAverage = i + 0.5f; // +0.5 as we want the center of the bucket
      }
    }
    else 
    {
      // Weight our sum by the number of pixels (account for the almost black pixels in bucket[0] - which shouldnt contribute to the average)
      weightedAverage = weightedSum / totalWeight;
    }

    // Undo histogram transform
    normalizedEV = histogramOffsetToNormalizedEV(weightedAverage);

    const float currentEV100 = normalizedEV * cb.evRange + cb.evMinValue;
    
    const float currentLum = calcEV100Luminance(currentEV100);

    // Map average luminance to middle grey
    const float middleGrey = 0.18f;

    const float currentExposure = middleGrey / max(0.001f, currentLum * (1.0 - middleGrey));

    // Smooth out large exposure swings over time
    const float previousExposure = OutExposure[0];
    const float adaptedExposure = previousExposure + (currentExposure - previousExposure) * (1.f - exp2(-cb.autoExposureSpeed));

    OutExposure[0] = adaptedExposure;
  }
  
  if (cb.debugMode)
  {
    DebugView[uint2(linearIndex, 0)] = vec4(float(countThisBucket) / 10000, 0, weight, 1.0);
    if (linearIndex == 0) 
    {
      // Remap exposure to bucket (1, size-1)
      int avgIdx = uint(normalizedEVToHistogramOffset(normalizedEV));
      DebugView[uint2(avgIdx, 0)] = float4(0,1,weight,1);
    }
  }
  
  // Reset the histogram
  InOutHistogram[linearIndex] = 0;
}
