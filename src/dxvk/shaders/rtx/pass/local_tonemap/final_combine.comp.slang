/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/local_tonemap/local_tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping.slangh"
#include "rtx/pass/tonemap/tonemapping_noise.slangh"

[[vk::binding(FINAL_COMBINE_BLUE_NOISE_TEXTURE_INPUT)]]
Texture2DArray BlueNoise;

[[vk::binding(FINAL_COMBINE_MIP_ASSEMBLE)]]
Sampler2D MipAssembleTexture;

[[vk::binding(FINAL_COMBINE_ORIGINAL_MIP0)]]
Texture2D<vec4> OriginalMip0Texture;

[[vk::binding(FINAL_COMBINE_WEIGHT_MIP0)]]
Texture2D<vec4> WeightMip0Texture;

[[vk::binding(FINAL_COMBINE_ORIGINAL_MIP)]]
Sampler2D OriginalMipTexture;

[[vk::binding(FINAL_COMBINE_OUTPUT)]]
RWTexture2D<vec4> OutputTexture;

[[vk::binding(FINAL_COMBINE_DEBUG_VIEW_OUTPUT)]]
RWTexture2D<float4> DebugView;

[[vk::binding(FINAL_COMBINE_EXPOSURE)]]
RWTexture1D<float> AutoExposureTexture;

#include "rtx/utility/debug_view_helpers.slangh"

layout(push_constant)
ConstantBuffer<FinalCombineArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  uint width, height;
  OutputTexture.GetDimensions(width, height);
  if (threadId.x >= width || threadId.y >= height)
  {
    return;
  }

  vec2 vUv = (threadId + 0.5) / vec2(width, height);
  float avgExposure = getExposure(AutoExposureTexture, cb.enableAutoExposure, cb.exposure);
  vec4 originalColor = OutputTexture[threadId] * avgExposure;

  // Guided upsampling.
  // See https://bartwronski.com/2019/09/22/local-linear-models-guided-filter/
  float momentX = 0.0;
  float momentY = 0.0;
  float momentX2 = 0.0;
  float momentXY = 0.0;
  float ws = 0.0;
  for (int dy = -1; dy <= 1; dy += 1) {
    for (int dx = -1; dx <= 1; dx += 1) {
        float x = OriginalMipTexture.SampleLevel(vUv + vec2(dx, dy) * cb.mipPixelSize.zw, 0).y;
        float y = MipAssembleTexture.SampleLevel(vUv + vec2(dx, dy) * cb.mipPixelSize.zw, 0).x;
        float w = exp(-0.5 * float(dx*dx + dy*dy) / (0.7*0.7));
        momentX += x * w;
        momentY += y * w;
        momentX2 += x * x * w;
        momentXY += x * y * w;
        ws += w;
    }
  }
  momentX /= ws;
  momentY /= ws;
  momentX2 /= ws;
  momentXY /= ws;
  float A = (momentXY - momentX * momentY) / (max(momentX2 - momentX * momentX, 0.0) + 0.00001);
  float B = momentY - A * momentX;


  // Apply local exposure adjustment as a crude multiplier on all RGB channels.
  // This is... generally pretty wrong, but enough for the demo purpose.
  vec3 texelOriginal = linearToGamma(ACESFilm(originalColor.xyz, cb.useLegacyACES, true));
  float luminance = calcBt709Luminance(texelOriginal) + 0.00001;
  float finalMultiplier = max(A * luminance + B, 0.0) / luminance;
  // This is a hack to prevent super dark pixels getting boosted by a lot and showing compression artifacts.
  float lerpToUnityThreshold = 0.007;
  finalMultiplier = luminance > lerpToUnityThreshold ? finalMultiplier : 
    mix(1.0, finalMultiplier, (luminance / lerpToUnityThreshold) * (luminance / lerpToUnityThreshold));

  vec3 texelFinal = originalColor.xyz * finalMultiplier;
  if (cb.finalizeWithACES)
  {
    texelFinal = ACESFilm(texelFinal, cb.useLegacyACES);
  }
  if (cb.performSRGBConversion)
  {
    texelFinal = linearToGamma(texelFinal);
  }

  // Dither as a last step before final output to avoid banding artifacts.
  texelFinal = ditherTo8Bit(
    texelFinal,
    threadId,
    cb.frameIndex,
    cb.ditherMode != ditherModeNone,
    cb.ditherMode == ditherModeSpatialTemporal
  );

  OutputTexture[threadId] = vec4(texelFinal, 1.0);

  if (cb.debugView == DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT)
  {
    OutputTexture[threadId] = vec4(finalMultiplier.xxx, 1.0);
  }
  else if (cb.debugView == DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT)
  {
    vec4 luminance = OriginalMip0Texture[threadId];
    float outputValue = luminance.x;
    if (threadId.x < width / 3)
    {
      outputValue = luminance.x;
    }
    else if (threadId.x < width * 2 / 3)
    {
      outputValue = luminance.y;
    }
    else
    {
      outputValue = luminance.z;
    }
    OutputTexture[threadId] = vec4(outputValue.xxx, 1.0);
  }
  else if (cb.debugView == DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT)
  {
    OutputTexture[threadId] = WeightMip0Texture[threadId];
  }
  else if (cb.debugView == DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT)
  {
    OutputTexture[threadId] = vec4(MipAssembleTexture.SampleLevel(vUv, 0).xxx, 1.0);
  }
}
