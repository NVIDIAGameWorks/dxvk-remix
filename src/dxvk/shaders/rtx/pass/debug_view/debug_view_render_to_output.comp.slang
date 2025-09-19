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

// Renders debug view onto a render output.
// This is done as a separate pass from postprocess since this pass
// runs at a  resolution of the render output

#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx/pass/debug_view/debug_view_render_to_output_binding_indices.h"
#include "rtx/utility/common.slangh"

// Inputs

layout(binding = DEBUG_VIEW_RENDER_TO_OUTPUT_BINDING_CONSTANTS_INPUT)
ConstantBuffer<DebugViewArgs> cb;

layout(rgba32f, binding = DEBUG_VIEW_RENDER_TO_OUTPUT_BINDING_DEBUG_VIEW_INPUT)
Texture2D<float4> DebugView;

// Inputs / Outputs

layout(rgba16f, binding = DEBUG_VIEW_RENDER_TO_OUTPUT_BINDING_RENDER_OUTPUT_INPUT_OUTPUT)
RWTexture2D<float4> RenderOutput;

void overlayOnTopOfRenderOutput(ivec2 threadId, inout vec4 value)
{
  // Overlay debug view's output on top of render output
  if (cb.overlayOnTopOfRenderOutput)
  {
    vec4 renderOutput = RenderOutput[threadId];
    
    // Overlay any 0 value pixels. This is simple method that works for overlaying NaN debug view over rendered output
    value = mix(renderOutput, value, any(value.xyz > 0));
  }
}

uint2 calculateDebugViewPixelCoordinate(uint2 threadId)
{ 
  // Calculate pixel coordinate in the debug view texture space
  // as the render to output pass can output at either downscaled or target extent.
  // Note: Nearest neighbor filtering is used to give a precise view of debug buffer when upscaling is used
  return (threadId + 0.5) * cb.renderToOutputToDebugViewResolution;
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.renderToOutputResolution))
  {
    return;
  }

  uint2 debugViewPixelCoordinate = calculateDebugViewPixelCoordinate(threadId);

  // Post-processing and overrides if applicable
  vec4 value = DebugView[debugViewPixelCoordinate];

  overlayOnTopOfRenderOutput(threadId, value);

  RenderOutput[threadId] = value;
}
