/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_INPUT)]]
Texture2D<float4> InColorTexture;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_EXPOSURE_INPUT)]]
RWTexture1D<float> InExposure;

[[vk::binding(TONEMAPPING_APPLY_TONEMAPPING_COLOR_OUTPUT)]]
RWTexture2D<float4> OutColorTexture;

layout(push_constant)
ConstantBuffer<FastToneMappingArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  uint width;
  uint height;
  OutColorTexture.GetDimensions(width, height);
  if (threadId.x >= width || threadId.y >= height)
  {
    return;
  }

  float3 inputColor = sanitizeRadiance(InColorTexture[threadId].xyz);

  const float exposure = sanitizeExposure(getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor));
  inputColor *= exposure;
  float3 outputColor = ACESFilmApproximation(inputColor);

  outputColor = linearToGamma(outputColor);
  OutColorTexture[threadId] = vec4(outputColor.rgb, 1.0f);
}
