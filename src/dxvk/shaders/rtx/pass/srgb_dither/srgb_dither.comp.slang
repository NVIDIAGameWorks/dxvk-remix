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
  float3 gammaColor = 0;
  if (cb.inversionAmount > 0.0f)
  {
    // Keep the legacy negative in display values, including for linear captures.
    // Note for future HDR addition: linear should be normalized to 1.0 being paperwhite and then the inversion proceeds as already done here, in SDR
    // The final linear color should be mix( inputColor, invertedColor, cb.inversionAmount ); so you get HDR values in the transition values
    // At the end, multiply your linearColor by the number of nits for paperwhite before converting from linearNits to displayReferred space
    const float3 displayColor = linearToGamma(saturate(inputColor));
    float3 modeColor = 1.0f - displayColor;
    if (cb.inversionMode == inversionModeThresholdInvert)
    {
      // Pixels whose average display value reaches the threshold become black.
      const float averageDisplayValue = (displayColor.r + displayColor.g + displayColor.b) / 3.0f;
      if (averageDisplayValue >= cb.inversionThreshold)
      {
        modeColor = float3(0.0f);
      }
    }

    const float3 invertedColor = mix(displayColor, modeColor, cb.inversionAmount);
    gammaColor = cb.performSRGBConversion ? invertedColor : gammaToLinear(invertedColor);
  }
  else
  {
    gammaColor = (cb.performSRGBConversion > 0) ? linearToGamma(inputColor) : inputColor;
  }

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
