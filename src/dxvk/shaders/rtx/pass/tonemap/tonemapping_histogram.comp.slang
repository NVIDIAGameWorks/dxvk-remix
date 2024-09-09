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

layout(rgba32f, binding = TONEMAPPING_HISTOGRAM_COLOR_INPUT)
RWTexture2D<float4> InColorBuffer; 

layout(r32f, binding = TONEMAPPING_HISTOGRAM_EXPOSURE_INPUT)
RWTexture1D<float> InExposure;

layout(r32ui, binding = TONEMAPPING_HISTOGRAM_HISTOGRAM_INPUT_OUTPUT)
RWTexture1D<uint> InOutHistogram;

layout(push_constant)
ConstantBuffer<ToneMappingHistogramArgs> cb;

static const uint kToneCurveSampleCount = TONEMAPPING_TONE_CURVE_SAMPLE_COUNT;

groupshared uint g_localHistogram[kToneCurveSampleCount];

// Computes the histogram of the current image and stores the result.

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID, uint linearIndex : SV_GroupIndex)
{
  // Add this pixel's contribution to the tone histogram:
  uvec2 screenSize;
  InColorBuffer.GetDimensions(screenSize.x, screenSize.y);

  if (linearIndex < kToneCurveSampleCount)
  {
    g_localHistogram[linearIndex] = 0;
  }

  GroupMemoryBarrierWithGroupSync();

  // Ignore pixels that are outside of the image:
  if (all(lessThan(threadId, screenSize)))
  {
    vec3 inputColor = InColorBuffer[threadId].rgb;
    
    // Exposure correction
    inputColor *= getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor);
        
    // Compute luminance of pixel
    const float pixelLuminance = calcBt709Luminance(inputColor);
    
    // Ignore completely black pixels, as well as pixels that are too bright:
    if ((pixelLuminance > 0.0) && (pixelLuminance < exp2(cb.toneCurveMaxStops)))
    {
      // Compute histogram bin (based on photographic stops)
      // The bins are numbered 0...kToneCurveSampleCount-1. When i > 0, we think of bin i
      // as sampling the histogram at the point
      // mix(kToneCurveMinStops, kToneCurveMaxStops, i/(kToneCurveSampleCount-1)).
      // Bin 0, however, also includes all values in (0, kToneCurveMinStops]).
      // We use a tent filter to sample each bin.
      
      // Get log luminance and clamp values below kToneCurveMinStops
      const float logLuminance = max(log2(pixelLuminance), cb.toneCurveMinStops);
      
      // Map values to [0,kToneCurveSampleCount-1], where 0 corresponds to min and
      // kToneCurveSampleCount-1 corresponds to max log luminance:
      const float scaledLogLuminance =  (kToneCurveSampleCount-1.0) * (logLuminance - cb.toneCurveMinStops) /  (cb.toneCurveMaxStops - cb.toneCurveMinStops);
      
      // Get the two bins we'll distribute importance between
      const uint leftBin = uint(scaledLogLuminance);
      const uint rightBin = leftBin + 1;
      
      // Compute pixel importance based on distance to the center of the screen:
      const vec2 normalizedCoordinate = vec2(threadId) / vec2(screenSize) - vec2(0.5f, 0.5f);
      const float weight = clamp(1.0f - length(normalizedCoordinate) * 1.5f, 0.01f, 1.0f);
      
      // Distribute between left and right bins
      const float rightWeight = fract(scaledLogLuminance) * weight;
      const float leftWeight = weight - rightWeight;
      
      // Accumulate into local histogram using atomic adds
      // Since 0 <= leftBin < kToneCurveSampleCount,
      InterlockedAdd(g_localHistogram[leftBin], uint(leftWeight * kHistogramFixedPointScale));
      if (rightBin < kToneCurveSampleCount) { // NOTE: Needed, already clamped to sampleCount -1
        InterlockedAdd(g_localHistogram[rightBin], uint(rightWeight * kHistogramFixedPointScale));
      }
    }
  }

  // Wait for all threads in the group to finish.
  GroupMemoryBarrierWithGroupSync();

  // Finally, add the group's histogram to the global histogram.
  // This code requires at least kToneCurveSampleCount threads in each group.
  if (linearIndex < kToneCurveSampleCount)
  {
    uint localBinValue = g_localHistogram[linearIndex];
    // Avoid performing atomic no-ops
    if (localBinValue != 0)
    {
      InterlockedAdd(InOutHistogram[int(linearIndex)], localBinValue);
    }
  }
}
