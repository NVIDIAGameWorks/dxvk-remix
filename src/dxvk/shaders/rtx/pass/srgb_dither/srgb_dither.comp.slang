/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/srgb_dither/srgb_dither.h"
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping_noise.slangh"
#include "rtx/utility/color.slangh"

[[vk::binding(SRGB_DITHER_BLUE_NOISE_TEXTURE_INPUT)]]
Texture2DArray BlueNoise;

[[vk::binding(SRGB_DITHER_COLOR_INPUT_OUTPUT)]]
RWTexture2D<float4> InOutColorBuffer;

layout(push_constant)
ConstantBuffer<SRGBDitherArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= uint2(imageSize(InOutColorBuffer))))
  {
    return;
  }

  const float3 inputColor = imageLoad(InOutColorBuffer, threadId).xyz;
  const float3 gammaColor = (cb.performSRGBConversion > 0) ? linearToGamma(inputColor) : inputColor;

  // Apply dithering as the last step before output to break up banding artifacts.
  // Dithering is performed in gamma space (when SRGB conversion is enabled) so the
  // perceptual error is approximately one 8-bit unorm step.
  const float3 ditheredColor = ditherTo8Bit(
    gammaColor,
    threadId,
    cb.frameIndex,
    cb.ditherMode != ditherModeNone,
    cb.ditherMode == ditherModeSpatialTemporal
  );

  InOutColorBuffer[threadId] = float4(saturate(ditheredColor), 1.0f);
}
