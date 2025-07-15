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
#include "rtx/pass/post_fx/post_fx.h"
#include "rtx/pass/post_fx/post_fx_motion_blur_geometry_flags.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/packing.slangh"

[[vk::binding(POST_FX_MOTION_BLUR_BLUE_NOISE_TEXTURE_INPUT)]]
Texture2DArray BlueNoise;

#include "rtx/utility/noise.slangh"

[[vk::binding(POST_FX_MOTION_BLUR_PRIMARY_SCREEN_SPACE_MOTION_INPUT)]]
Texture2D<float2> PrimaryScreenSpaceMotionVector;

[[vk::binding(POST_FX_MOTION_BLUR_PRIMARY_SURFACE_FLAGS_INPUT)]]
Texture2D<uint> PrimarySurfaceFlags;

[[vk::binding(POST_FX_MOTION_BLUR_PRIMARY_LINEAR_VIEW_Z_INPUT)]]
Texture2D<float> PrimaryLinearViewZ;

[[vk::binding(POST_FX_MOTION_BLUR_INPUT)]]
Texture2D<float4> InColorTexture;

[[vk::binding(POST_FX_MOTION_BLUR_OUTPUT)]]
RWTexture2D<float4> OutColorTexture;

[[vk::binding(POST_FX_MOTION_BLUR_NEAREST_SAMPLER)]]
SamplerState NearestSampler;

[[vk::binding(POST_FX_MOTION_BLUR_LINEAR_SAMPLER)]]
SamplerState LinearSampler;

layout(push_constant)
ConstantBuffer<PostFxArgs> cb;

/////////////////////////////////////////////////////////////////////////////////////////////////////
// A Reconstruction Filter for Plausible Motion Blur
// https://casual-effects.com/research/McGuire2012Blur/McGuire12Blur.pdf
float coneFunc(const float sampleToCenterPixelDistance, const float recVelocity)
{
  return saturate(1.0f - sampleToCenterPixelDistance * recVelocity);
}

float cylinderFunc(const float sampleToCenterPixelDistance, const float velocity)
{
  return 1.0f - smoothstep(0.95f * velocity, 1.05f * velocity, sampleToCenterPixelDistance);
}

float calculateDepthWeight(const float centerDepth, const float sampleDepth, const float recZSoftExtent)
{
  return saturate(1.0f - (centerDepth - sampleDepth) * recZSoftExtent);
}

float calculateReconstructionWeight(
  const float2 centerPos,
  const float2 centerVelocity,
  const float  centerDepth,
  const float2 samplePos,
  const float2 sampleVelocity,
  const float  sampleDepth,
  const float  recZSoftExtent)
{
  const float sampleToCenterPixelDistance = distance(centerPos, samplePos);

  const float centerVelocityAmount = length(centerVelocity);
  const float sampleVelocityAmount = length(sampleVelocity);
  const float recCenterVelocityAmount = 1.0f / centerVelocityAmount;
  const float recSampleVelocityAmount = 1.0f / sampleVelocityAmount;

  const float f = calculateDepthWeight(centerDepth, sampleDepth, recZSoftExtent);
  const float b = calculateDepthWeight(sampleDepth, centerDepth, recZSoftExtent);

  return f * coneFunc(sampleToCenterPixelDistance, recSampleVelocityAmount) +
         b * coneFunc(sampleToCenterPixelDistance, recCenterVelocityAmount) +
         cylinderFunc(sampleToCenterPixelDistance, sampleVelocityAmount) * cylinderFunc(sampleToCenterPixelDistance, centerVelocityAmount) * 2.0f;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////

float2 OutputToInput(const uint2 pixelPos, const float2 inputOverOutputViewSize)
{
  return (float2(pixelPos) + 0.5f) * inputOverOutputViewSize - 0.5f;
}

bool isValidMotionBlurPixel(const uint2 pixelPos, inout bool isStaticPixel)
{
  const float2 inputPos = OutputToInput(pixelPos, cb.inputOverOutputViewSize);
  const int2 inputPosInt = int2(round(inputPos));

  MotionBlurSurfaceFlags motionBlurSurfaceFlags = motionBlurSurfaceFlagsReadFromGBuffer(PrimarySurfaceFlags[inputPosInt]);
  isStaticPixel = motionBlurSurfaceFlags.isStatic;

  // Valid motion blur pixels: emissive opaque (if it's enabled by setting), non-ViewModel
  return (cb.enableMotionBlurEmissive || !motionBlurSurfaceFlags.isEmissive) && (!motionBlurSurfaceFlags.isViewModel) && (!motionBlurSurfaceFlags.isMaskOut);
}

float3 calculatedBlurredColor(
  const uint2  pixelPos,
  const float2 uv,
  const float4 centerColor,
  const float  centerDepth,
  const uint   sampleCount,
  const float2 adjustedVelocity,
  const float  minimunVelocityLength)
{
  const float2 stepSize = adjustedVelocity / (float)sampleCount;

  float4 blurredColor = centerColor;
  float2 currentTexCoord[2] = { uv, uv };

  [unroll]
  for (uint i = 0; i < sampleCount; ++i)
  {
    // Extend the sample position in y direction to avoid same random value for all samples,
    // we logically treat the current position as pixel position in i-th texture of current texture:
    // virtualX = x, virtualY = i * imageSizeY + y
    const float2 virtualSamplePos = float2(pixelPos.x, i * (float)cb.imageSize.y + pixelPos.y);

    // Get random step with weighted dither pattern
    const float random = cb.enableMotionBlurNoiseSample ? temporalInterleavedGradientNoise(virtualSamplePos, cb.frameIdx) : 1.0f;
    const float2 stepRandom = 0.5f + float2(random - 0.5f, 0.5f - random);

    currentTexCoord[0] += stepSize * max(stepRandom.x, minimunVelocityLength);
    currentTexCoord[1] -= stepSize * max(stepRandom.y, minimunVelocityLength);

    [unroll]
    for (uint j = 0; j < 2; ++j)
    {
      // Extend on X direction according to sample direction
      const float2 directionalVirtualSamplePos = float2(j * (float)cb.imageSize.x + virtualSamplePos.x, virtualSamplePos.y);

      const float jitterRandom0 = temporalInterleavedGradientNoise(directionalVirtualSamplePos, cb.frameIdx);
      const float jitterRandom1 = temporalInterleavedGradientNoise(directionalVirtualSamplePos, cb.frameIdx + 1);
      const float2 sampleJitter = float2((jitterRandom0 - 0.5f) * 2.0f, (jitterRandom1 - 0.5f) * 2.0f) * cb.invImageSize;

      const float2 texCoord = currentTexCoord[j] + cb.jitterStrength * sampleJitter;
      const int2 samplePosInt = int2(round(texCoord * cb.imageSize));

      // Discard the sample when it's viewModel pixel or opaque emissives
      bool isStatic = false;
      const float isMotionBlurMultiplier = isValidMotionBlurPixel(samplePosInt, isStatic) ? 1.0f : 0.0f;

      // Do reconstruction with sample color, velocity and depth
      const float4 sampleColor = float4(InColorTexture.SampleLevel(NearestSampler, texCoord, 0.0f).rgb, 1.0f) * isMotionBlurMultiplier;
      const float2 sampleVelocity = PrimaryScreenSpaceMotionVector.SampleLevel(NearestSampler, texCoord, 0.0f).xy * cb.invMainCameraResolution;
      const float  sampleDepth = PrimaryLinearViewZ.SampleLevel(NearestSampler, texCoord, 0.0f);
      const float  reconstructionWeight = calculateReconstructionWeight(uv, adjustedVelocity, centerDepth, texCoord, sampleVelocity, sampleDepth, 0.01f);

      blurredColor += sampleColor * reconstructionWeight;
    }
  }
  blurredColor.rgb /= blurredColor.w;

  return blurredColor.rgb;
}

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(
  in uint2 threadId : SV_GroupThreadID,
  in uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
  {
    return;
  }

  const float2 uv = (pixelPos + 0.5f) * (float2)cb.invImageSize;
  const float4 centerColor = float4(InColorTexture[pixelPos].rgb, 1.0f);
  const float centerDepth = PrimaryLinearViewZ.SampleLevel(LinearSampler, uv, 0.0f);

  bool isStatic = false;
  bool isValidMotionBlur = isValidMotionBlurPixel(pixelPos, isStatic);
  const float dynamicMotionBlurDeduction = !isStatic ? cb.motionBlurDynamicDeduction : 1.0f;

  // Fetch velocity and adjust with exposure fraction
  const float2 velocity =
    PrimaryScreenSpaceMotionVector.SampleLevel(LinearSampler, uv, 0.0f).xy * cb.exposureFraction * cb.invMainCameraResolution * dynamicMotionBlurDeduction * 0.5f ;
  const float velocityLength = length(velocity);
  const float2 adjustedVelocity = velocityLength > 1e-7f ? normalize(velocity) * min(velocityLength, cb.blurDiameterFraction) * cb.motionBlurDlfgDeduction : velocity;
  const float2 minimunVelocity = (float2)cb.invImageSize * cb.motionBlurMinimumVelocityThresholdInPixel.xx;

  // Mask out pixels if the primary ray hits viewModel, player hold model or the velocity less than 1 pixel in all dimensions
  if (!isValidMotionBlur || all(abs(adjustedVelocity) < minimunVelocity))
  {
    OutColorTexture[pixelPos] = centerColor;
    return;
  }

  const float minimunVelocityLength = length(minimunVelocity);
  const float3 blurredColor = calculatedBlurredColor(
    pixelPos, uv, centerColor, centerDepth, cb.motionBlurSampleCount, adjustedVelocity, minimunVelocityLength);

  OutColorTexture[pixelPos] = float4(blurredColor, 1.0f);
}
