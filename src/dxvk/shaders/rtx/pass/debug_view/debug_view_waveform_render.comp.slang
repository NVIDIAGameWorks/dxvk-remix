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
#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx/pass/debug_view/debug_view_waveform_render_binding_indices.h"

layout(r32ui, binding = DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_RED_INPUT)
Texture2D<uint> HDRWaveformRedInputOutput;
layout(r32ui, binding = DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_GREEN_INPUT)
Texture2D<uint> HDRWaveformGreenInputOutput;
layout(r32ui, binding = DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_BLUE_INPUT)
Texture2D<uint> HDRWaveformBlueInputOutput;

layout(rgba32f, binding = DEBUG_VIEW_WAVEFORM_RENDER_BINDING_INPUT_OUTPUT)
RWTexture2D<float4> DebugView;

layout(binding = DEBUG_VIEW_WAVEFORM_RENDER_BINDING_CONSTANTS_INPUT)
ConstantBuffer<DebugViewArgs> cb;

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.hdrWaveformResolution))
    return;

  // Normalize the accumulated HDR Waveform values for display

  const uvec2 flippedCoordinate = uvec2(threadId.x, cb.hdrWaveformResolution.y - threadId.y - 1);
  const uvec3 rawWaveformValue = uvec3(
    HDRWaveformRedInputOutput[flippedCoordinate],
    HDRWaveformGreenInputOutput[flippedCoordinate],
    HDRWaveformBlueInputOutput[flippedCoordinate]);
  // Note: Normalized by the number of pixels in a rectangular section of the debug view image which map to the HDR Waveform
  // output. Equal to debugViewSize.y / scaleFactor.y * scaleFactor.x which simplifies to debugViewSize.y.
  const vec3 normalizedWaveformValue = vec3(rawWaveformValue) / cb.debugViewResolution.y;
  const vec3 scaledWaveformValue = clamp(normalizedWaveformValue * cb.hdrWaveformHistogramNormalizationScale, vec3(0.0f), vec3(1.0f));

  vec3 displayValue = scaledWaveformValue;

  if (fmod(float(threadId.y), float(cb.hdrWaveformResolution.y) / float(cb.log10Range)) < 1.0f)
  {
    displayValue = lerp(displayValue, vec3(1.0f, 1.0f, 1.0f), 0.5f);
  }

  imageStore(DebugView, threadId + cb.hdrWaveformPosition, vec4(displayValue, 1.0f));
}
