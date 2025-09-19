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

layout(r32ui, binding = TONEMAPPING_TONE_CURVE_HISTOGRAM_INPUT_OUTPUT)
RWTexture1D<uint> InOutHistogram;

layout(r32f, binding = TONEMAPPING_TONE_CURVE_TONE_CURVE_INPUT_OUTPUT)
RWTexture1D<float> InOutToneCurve;

layout(push_constant)
ConstantBuffer<ToneMappingCurveArgs> cb;

static const uint kToneCurveSampleCount = TONEMAPPING_TONE_CURVE_SAMPLE_COUNT;

// Local data for tone curve generation
groupshared float g_localData[kToneCurveSampleCount];

// Here are two functions for performing arithmetic across groups.
// They use wavefront operations to perform operations extremely quickly across waves,
// and then use shared memory to consolidate their results.

// Computes the sum of val across the entire group.
float computeSharedSum(float val, const uint linearIndex) {
  
  GroupMemoryBarrierWithGroupSync();

  val = WaveActiveSum(val);
  g_localData[linearIndex] = val;
  GroupMemoryBarrierWithGroupSync();

  for (uint k = kToneCurveSampleCount / 2; k >= WaveGetLaneCount(); k /= 2)
  {
    if (linearIndex < k)
    {
      g_localData[linearIndex] += g_localData[linearIndex + k];
    }
    
    GroupMemoryBarrierWithGroupSync();
  }

  return g_localData[0];
}

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

  return g_localData[linearIndex];
}

// Computes the slopes of a noise-aware tone mapping curve, based off of Eilertsen, Mantiuk,
// and Unger's Real-Time Noise-Aware Tone Mapping paper, with some modifications.
float computeSlopes(
  float hist, 
  const uint minBin, 
  const uint maxBinExclusive, 
  const float minLogOutput, 
  const float maxLogOutput, 
  const uint linearIndex) {

  // Compute local histogram for this section of the curve
  if (linearIndex < minBin || linearIndex >= maxBinExclusive)
  {
    hist = 0.0f;
  }
    
  // Normalize the histogram:
  {
    const float histSum = computeSharedSum(hist, linearIndex);
    hist /= max(histSum, 1e-6);
  }
    
  // Compute noise-aware tone mapping curve, based off of Eilertsen, Mantiuk,
  // and Unger's Real-Time Noise-Aware Tone Mapping paper, with some modifications.
    
  // Dynamic range of section in log2 of candelas; sum of delta*slopes will be equal to r
  const float r = maxLogOutput - minLogOutput;
    
  // Stops between each bin
  const float delta = (cb.toneCurveMaxStops - cb.toneCurveMinStops)/float(kToneCurveSampleCount);
  const float rOverDelta = r / delta;
    
  // We need to make sure that bins whose slopes would otherwise be set to negative values
  // instead have their slopes set to 0. To do this, we compute a threshold using the iterative
  // method from Equation (17), using 16 iterations (which can be changed).
  const float rcpHist = hist > 0.0f ? 1.0f / hist : 0.0f;
  float thresh = 1e-16;
  float sumRecip;
  float lenOmega = float(kToneCurveSampleCount);
  float threshPassed;
  for (uint i = 0; i < 16; ++i)
  {
    threshPassed = step(thresh, hist);
    lenOmega = computeSharedSum(threshPassed, linearIndex);
        
    sumRecip = computeSharedSum(rcpHist * threshPassed, linearIndex);
    thresh = (lenOmega - rOverDelta)/max(sumRecip, 1e-6);
  }
    
  // Compute slopes from Equation (14), using N = lenOmega.
  threshPassed = step(thresh, hist);
  lenOmega = computeSharedSum(threshPassed, linearIndex);
  sumRecip = computeSharedSum(rcpHist * threshPassed, linearIndex);
  float mySlope = (1.0 + rcpHist * (rOverDelta - lenOmega)/max(sumRecip, 1e-6)) * threshPassed;
    
  return mySlope;
}

// Computes the tone curve from the histogram.
[shader("compute")]
[numthreads(TONEMAPPING_TONE_CURVE_SAMPLE_COUNT, 1, 1)]
void main(uint linearIndex : SV_GroupIndex)
{
  if (linearIndex >= kToneCurveSampleCount)
  {
    return;
  }
    
  // This is the log luminance of the histogram bin this thread is processing.
  const float binLogLuminance = (float(linearIndex) / float(kToneCurveSampleCount)) *  (cb.toneCurveMaxStops - cb.toneCurveMinStops) + cb.toneCurveMinStops;
    
  // Read the histogram and normalize it.
  // When the original histogram value is exactly 0, we set the slope to 0. But this can actually
  // cause temporal problems - imagine a bin oscillating between 0 and 1 - so we add 1
  // saliency to each bin.
  const float histogramValue = float(InOutHistogram[int(linearIndex)]) / kHistogramFixedPointScale;

  const float originalHist = 1.0f + float(histogramValue);
    
  const float r = cb.dynamicRange;
  const float delta = (cb.toneCurveMaxStops - cb.toneCurveMinStops)/float(kToneCurveSampleCount);
    
  // Determine the slope of the tone curve at this point.
  // To preserve exposure, we split the tone curve into two sections: those below middle gray
  // (linear = 0.18), and those above middle gray. This allows us to make it so that middle gray
  // will map to middle gray.
  // 0.18 maps most closely to kToneCurveSampleCount * (log2(0.18) - kToneCurveMinStops)/(kToneCurveMaxStops - kToneCurveMinStops) = 196.8.
  const uint kCrossover = kToneCurveSampleCount * (log2(0.18) - cb.toneCurveMinStops) / (cb.toneCurveMaxStops - cb.toneCurveMinStops);
  const float slopeLo = computeSlopes(originalHist, 0, kCrossover, min(-r, log2(0.17)), log2(0.18), linearIndex);
  const float slopeHi = computeSlopes(originalHist, kCrossover, kToneCurveSampleCount, log2(0.18), 0.0f, linearIndex);
  float mySlope = linearIndex < kCrossover ? slopeLo : slopeHi;
    
  if (linearIndex < kCrossover) 
  {
    mySlope = max(mySlope, cb.shadowMinSlope);
  }
    
  // Compute tone curve.
  // Turn slopes into an inclusive prefix sum:
  float myToneCurve = computePrefixSum(mySlope, linearIndex);
  const float sumOfSlopes = g_localData[kToneCurveSampleCount - 1]; // Sum of all slopes; used to account for sum(slope) != r.
  // Turn this into an exclusive prefix sum and scale:
  myToneCurve = (myToneCurve - mySlope - sumOfSlopes) * delta;

  // Apply shadow contrast:
  // In log-log space, this amounts to subtracting cb.shadowContrast * max(0, cb.shadowContrastEnd - binLogLuminance)
  // from each bin.
  myToneCurve -= cb.shadowContrast * max(0.0f, cb.shadowContrastEnd - binLogLuminance);
    
  // Say that the luminance can be brought up no more than cb.maxExposureIncrease stops.
  myToneCurve = min(myToneCurve, binLogLuminance + cb.maxExposureIncrease);
    
  // Now, blur the tone curve.
  // We'll do this using a box filter, using a summed area table - i.e. a prefix sum again!
  // Side effect: Stores prefix sum in g_localData, which we'll use.    
  {
    computePrefixSum(myToneCurve, linearIndex);

    // g_localData[j] = sum(myToneCurve[i], {i, 0, j})
    // So, sum(myToneCurve[i], {i, j-r, j+r}) = g_localData[j + r] - g_localData[j - r - 1]
    const uint radius = 12;
    const uint highIndex = min(linearIndex + radius, kToneCurveSampleCount - 1);
    const uint lowIndex = max(0, int(linearIndex) - int(radius) - 1);
    const float hiSAT = g_localData[highIndex];
    const float loSAT = g_localData[lowIndex];
    myToneCurve = (hiSAT - loSAT) / max(float(highIndex - lowIndex), 1e-6);

    // Shift the entire curve up or down:
    myToneCurve += cb.curveShift;
  }
    
  // Temporal filtering
  {
    const float myOldToneCurve = InOutToneCurve[linearIndex].r;

    if (cb.needsReset == 0)
    {
      myToneCurve = mix(myToneCurve, myOldToneCurve, exp(-0.07)); // Ad-hoc constant for interpolation
    }
  }
    
  // Write to the output!
  InOutToneCurve[linearIndex] = myToneCurve;
    
  // Finally, reset the histogram for the next time the program calls tone mapping:
  InOutHistogram[linearIndex] = 0;
} 