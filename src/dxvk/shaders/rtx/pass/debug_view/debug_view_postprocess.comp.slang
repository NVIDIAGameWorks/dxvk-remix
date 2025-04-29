/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

// Loads debug view input and applies any postprocess transformations.
// Note: this shader code is split from debug_view.slangh
// to support gather operations on the result from that shader.

#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx/pass/debug_view/debug_view_postprocess_binding_indices.h"
#include "rtx/utility/debug_view_indices.h"

#include "rtx/utility/color.slangh"

// Inputs

layout(binding = DEBUG_VIEW_POSTPROCESS_BINDING_CONSTANTS_INPUT)
ConstantBuffer<DebugViewArgs> cb;

layout(rgba32f, binding = DEBUG_VIEW_POSTPROCESS_BINDING_DEBUG_VIEW_INPUT)
Texture2D<float4> DebugView;

// Outputs

layout(r32ui, binding = DEBUG_VIEW_POSTPROCESS_BINDING_HDR_WAVEFORM_RED_OUTPUT)
RWTexture2D<uint> HDRWaveformRed;
layout(r32ui, binding = DEBUG_VIEW_POSTPROCESS_BINDING_HDR_WAVEFORM_GREEN_OUTPUT)
RWTexture2D<uint> HDRWaveformGreen;
layout(r32ui, binding = DEBUG_VIEW_POSTPROCESS_BINDING_HDR_WAVEFORM_BLUE_OUTPUT)
RWTexture2D<uint> HDRWaveformBlue;

layout(rgba32f, binding = DEBUG_VIEW_POSTPROCESS_BINDING_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> Output;

void hdrWaveformOutput(ivec2 threadId, RWTexture2D<uint> HDRWaveformInputOutput, float normalizedOutCoordinate)
{
  // Early out if the coordinate is out of range
  // Note: Using >= 1 as the coordinate value is floored, meaning a value of 1 would be one pixel outside the output texture.

  if (normalizedOutCoordinate >= 1.0f || normalizedOutCoordinate < 0.0f)
  {
    return;
  }

  const uvec2 outCoordinate = uvec2(
    uint(threadId.x / cb.hdrWaveformResolutionScaleFactor),
    uint(normalizedOutCoordinate * cb.hdrWaveformResolution.y));

  uint dummyOldValue;
  InterlockedAdd(HDRWaveformInputOutput[outCoordinate], 1, dummyOldValue);
}

// Color codes a value into BGR, with only one channel being used to color code at a time
// Black: 0 value
// Red: values >= than max value
// Blue/Green (0,1]: the remaining values, with blue representing the lower half of valid range and green the rest
vec3 colorCodeIntoBGRexclusive(uint value, uint maxValue)
{
  if (value == 0)
  {
    return vec3(0);
  }
  else if (value >= maxValue)
  {
    return vec3(1, 0, 0);   
  }

  // Color code the rest, [1, maxValue-1], to a non-black BG color, one channel at a time

  value -= 1;
  const uint numValues = maxValue - 1;

  const float numChannels = 2;
  const uint ceilValuesPerChannel = ceil(numValues / numChannels);

  vec3 color = 0;
  const uint channel = value / ceilValuesPerChannel;
  const uint numValuesInCurrentChannel = min(numValues - channel * ceilValuesPerChannel, ceilValuesPerChannel);

  // Color code as (0,1]
  color[channel] = (float(value % numValuesInCurrentChannel) + 1) / numValuesInCurrentChannel;

  // Reswizzle to show lowest values in Blue and highest in Red
  return color.bgr;
}

void postprocess(ivec2 threadId, inout vec4 outValue)
{
  // Color code inf, nan values
  if (cb.enableInfNanViewFlag)
  { 
    bool nanDetected = false;
    bool infDetected = false;

    // Detect nans and infs in pixel neighborhood
    if (cb.colorCodeRadius > 0) 
    {
      int iRadius = cb.colorCodeRadius;
      for (int y = threadId.y - iRadius; y <= threadId.y + iRadius; y++ )
      {
        for (int x = threadId.x - iRadius; x <= threadId.x + iRadius; x++ )
        {
          if (any(ivec2(x, y) >= cb.debugViewResolution) || any(ivec2(x, y) < 0))
          {
            continue;
          }

          vec4 temp = DebugView[ivec2(x, y)];
          
          // Convert invalid NAN debug view value to NaN for NaN detection logic below.
          // This is done here rather than in debug view comp so that this conversion doesn't make it into the accumulation
          if (cb.debugViewIdx == DEBUG_VIEW_NAN && temp.x != 0.0)
          {
            temp = 0.0 / 0.0;
          }

          nanDetected |= any(isnan(temp));
          infDetected |= any(isinf(temp));
        }
      }
    }

    if (nanDetected || infDetected)
    {
      // Note: Override usual warning colors when pseudo color mode is in use as its visualization will not
      // display these properly.
      const bool usePseudoColorWarningColors =
        (cb.displayType == DebugViewDisplayType::Standard) &&
        (cb.pseudoColorMode != PseudoColorMode::Disabled);
      const vec3 kNanWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(1, 0, 0);
      const vec3 kInfWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(0, 0, 1);
      const vec3 kCombinedWarningColor =
        select(nanDetected, kNanWarningColor, vec3(0, 0, 0)) +
        select(infDetected, kInfWarningColor, vec3(0, 0, 0));

      outValue = cb.animationTimeSec > 0 
                ? vec4(sin(cb.animationTimeSec * 8) * kCombinedWarningColor, 1) // flash warning color
                : vec4(kCombinedWarningColor, 1);
      return;
    }
  }

  // Switch between Display mode

  if (cb.displayType == DebugViewDisplayType::Standard)
  {
    outValue *= cb.scale;
    outValue = (outValue - vec4(cb.minValue)) / (vec4(cb.maxValue - cb.minValue));

    if (!cb.enableAlphaChannelFlag)
    {
      outValue.a = 1;
    }

    if (cb.pseudoColorMode != PseudoColorMode::Disabled)
    {
      // Determine the input value to colormap based on the specified mode

      float pseudoColorInput;

      switch (cb.pseudoColorMode)
      {
      case PseudoColorMode::Luminance:
        pseudoColorInput = calcBt709Luminance(outValue.rgb);
        break;
      case PseudoColorMode::Red:
        pseudoColorInput = outValue.r;
        break;
      case PseudoColorMode::Green:
        pseudoColorInput = outValue.g;
        break;
      case PseudoColorMode::Blue:
        pseudoColorInput = outValue.b;
        break;
      case PseudoColorMode::Alpha:
        pseudoColorInput = outValue.a;
        break;
      }

      outValue.rgb = turboColormap(pseudoColorInput);
    }

    if (cb.enableGammaCorrectionFlag)
    {
      outValue.rgb = linearToGamma(outValue.rgb);
    }
  }
  else if (cb.displayType == DebugViewDisplayType::BGRExclusiveColor)
  {
    outValue.rgb = colorCodeIntoBGRexclusive(outValue.r, cb.maxValue);
  }
  else if (cb.displayType == DebugViewDisplayType::EV100)
  {
    const float luminance = calcBt709Luminance(outValue.rgb);
    const float ev100 = calcLuminanceEV100(luminance);
    const float normalizedEV = float(ev100 - cb.evMinValue) / cb.evRange;

    outValue = vec4(turboColormap(normalizedEV), 1.0f);
  }
  else if (cb.displayType == DebugViewDisplayType::HDRWaveform)
  {
    vec3 log10OutValue;

    if (cb.enableLuminanceModeFlag)
    {
      log10OutValue = log10(calcBt709Luminance(outValue.rgb));
    }
    else
    {
      log10OutValue = log10(outValue.rgb);
    }

    const vec3 normalizedOutCoordinate = (log10OutValue - cb.log10MinValue) / cb.log10Range;

    hdrWaveformOutput(threadId, HDRWaveformRed, normalizedOutCoordinate.r);
    hdrWaveformOutput(threadId, HDRWaveformGreen, normalizedOutCoordinate.g);
    hdrWaveformOutput(threadId, HDRWaveformBlue, normalizedOutCoordinate.b);
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.debugViewResolution))
  {
    return;
  }
  
  vec4 value = DebugView[threadId];

  // Post-processing and overrides if applicable
  postprocess(threadId, value);

  Output[threadId] = value;
}
