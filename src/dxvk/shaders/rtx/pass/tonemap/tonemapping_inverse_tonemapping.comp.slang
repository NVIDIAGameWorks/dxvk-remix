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
RWTexture2D<float4> InOutColorTexture;

layout(push_constant)
ConstantBuffer<FastToneMappingArgs> cb;

float orderedSmoothstep(float edge0, float edge1, float value)
{
  const float lowerEdge = min(edge0, edge1);
  const float upperEdge = max(edge0, edge1);
  if (upperEdge - lowerEdge <= 1e-6f)
  {
    return value >= upperEdge ? 1.0f : 0.0f;
  }

  return smoothstep(lowerEdge, upperEdge, value);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  uint width;
  uint height;
  InOutColorTexture.GetDimensions(width, height);
  if (threadId.x >= width || threadId.y >= height)
  {
    return;
  }

  float4 inputColor = InColorTexture[threadId];
  const bool isValidInputColor = isValidValue(inputColor.xyz);
  inputColor.xyz = isValidInputColor ? saturate(inputColor.xyz) : float3(0.0f);

  inputColor.xyz = gammaToLinear(inputColor.xyz);

  // The output retains the original HDR until this invocation overwrites its pixel.
  const float3 originalHdrColor = sanitizeRadiance(imageLoad(InOutColorTexture, threadId).rgb);
  const float exposure = sanitizeExposure(getExposure(InExposure, cb.enableAutoExposure, cb.exposureFactor));
  const float3 toneMappingInput = originalHdrColor * exposure;
  const float3 originalToneMapped = ACESFilmApproximation(toneMappingInput);
  const float3 inverseScalar = toneMappingInput / max(originalToneMapped, float3(1e-6f));
  const bool isValidInverseScalar =
    isValidValue(inverseScalar) &&
    all(inverseScalar >= float3(0.0f));

  float3 outputColor = isValidInverseScalar
    ? inputColor.xyz * inverseScalar
    : originalHdrColor;
  if (isValidInverseScalar)
  {
    outputColor /= exposure;
  }

  // Highlight recovery: DLSS-NR runs in LDR so it can never recover the intensity of pixels that
  // ACES has already crushed into its shoulder. For pixels that were very bright
  // in HDR AND fall in that shoulder, blend back to the original pre-NR HDR.
  //   preserve  -> "was this very bright HDR?"  (smoothstep on exposed max-channel intensity)
  //   shoulder  -> "would the active curve compress it heavily?"  (smoothstep on LDR intensity)
  if (cb.enableHighlightRecovery)
  {
    const float3 originalLdr = linearToGamma(originalToneMapped);
    const float hdrIntensity = max(toneMappingInput.x, max(toneMappingInput.y, toneMappingInput.z));
    const float shoulderSignal = max(originalLdr.x, max(originalLdr.y, originalLdr.z));

    const float preserve = orderedSmoothstep(
      cb.highlightRecoveryThresholds.x,
      cb.highlightRecoveryThresholds.y,
      hdrIntensity);
    const float shoulder = orderedSmoothstep(
      cb.highlightRecoveryThresholds.z,
      cb.highlightRecoveryThresholds.w,
      shoulderSignal);
    const float highlightMask = preserve * shoulder;

    outputColor = lerp(outputColor, originalHdrColor, highlightMask);
  }

  const bool isValidOutputColor = isValidValue(outputColor);
  outputColor = isValidInputColor && isValidOutputColor
    ? clamp(outputColor, 0.0f, float16Max)
    : originalHdrColor;

  const float outputAlpha = isValidValue(inputColor.a)
    ? saturate(inputColor.a)
    : 1.0f;
  InOutColorTexture[threadId] = vec4(outputColor, outputAlpha);
}
