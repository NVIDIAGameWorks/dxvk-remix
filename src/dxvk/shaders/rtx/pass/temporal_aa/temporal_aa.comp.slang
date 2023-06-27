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
#include "rtx/pass/temporal_aa/temporal_aa.h"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/utility/math.slangh"

layout(rgba16f, binding = TAA_INPUT)
Texture2D<float4> InColorTexture;

layout(rgba32f, binding = TAA_FEEDBACK_INPUT)
Texture2D<float4> InFeedbackColorTexture;

layout(rg16f, binding = TAA_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
Texture2D<float2> PrimaryScreenSpaceMotionVector;

layout(rgba16f, binding = TAA_OUTPUT)
RWTexture2D<float4> OutColorTexture;

layout(rgba32f, binding = TAA_FEEDBACK_OUTPUT)
RWTexture2D<float4> OutFeedbackColorTexture;

layout(binding = TAA_LINEAR_SAMPLER)
SamplerState LinearSampler;

layout(push_constant)
ConstantBuffer<TemporalAaArgs> cb;

#define BUFFER_X (TAA_TILE_SIZE_X + 3)
#define BUFFER_Y (TAA_TILE_SIZE_Y + 3)
#define RENAMED_GROUP_Y ((TAA_TILE_SIZE_X * TAA_TILE_SIZE_Y) / BUFFER_X)

groupshared float4 s_ColorsAndLengths[BUFFER_Y][BUFFER_X];
groupshared float2 s_MotionVectors[BUFFER_Y][BUFFER_X];

// Perceptual Quantizer Transform: https://en.wikipedia.org/wiki/Perceptual_quantizer
// Implementation: https://github.com/NVIDIAGameWorks/donut/blob/main/shaders/passes/taa_cs.hlsl
static const float pq_m1 = 0.1593017578125;
static const float pq_m2 = 78.84375;
static const float pq_c1 = 0.8359375;
static const float pq_c2 = 18.8515625;
static const float pq_c3 = 18.6875;

#define SHARED_MEM_OPT 1

float3 PQDecode(const float3 image, const float3 pqC)
{
  float3 Np = pow(max(image, 0.0), 1.0 / pq_m2);
  float3 L = Np - pq_c1;
  L = L / (pq_c2 - pq_c3 * Np);
  L = pow(max(L, 0.0), 1.0 / pq_m1);

  return L * pqC; // returns cd/m^2
}

float3 PQEncode(float3 image, const float3 invPqC)
{
  float3 L = image * invPqC;
  float3 Lm = pow(max(L, 0.0), pq_m1);
  float3 N = (pq_c1 + pq_c2 * Lm) / (1.0 + pq_c3 * Lm);
  image = pow(N, pq_m2);

  return saturate(image);
}

float2 OutputToInput(const int2 pixelPos, const float2 inputOverOutputViewSize)
{
  return (float2(pixelPos) + 0.5f) * inputOverOutputViewSize - 0.5f + cb.jitterOffset;
}

void Preload(int2 sharedID, int2 globalID, const float3 invPqC)
{
  float3 color = PQEncode(InColorTexture[globalID].rgb, invPqC);
  float2 motion = PrimaryScreenSpaceMotionVector[globalID].xy;
  float motionLength = dot(motion, motion);

  s_ColorsAndLengths[sharedID.y][sharedID.x] = float4(color.rgb, motionLength);
  s_MotionVectors[sharedID.y][sharedID.x] = motion;
}

[shader("compute")]
[numthreads(TAA_TILE_SIZE_X, TAA_TILE_SIZE_Y, 1)]
void main(
  in uint2 groupId  : SV_GroupID,
  in uint  groupIdx : SV_GroupIndex,
  in uint2 threadId : SV_GroupThreadID,
  in uint2 pixelPos : SV_DispatchThreadID)
{
  const int2 groupBase = int2(OutputToInput(groupId * int2(TAA_TILE_SIZE_X, TAA_TILE_SIZE_Y), cb.inputOverOutputViewSize) - 1);

#if SHARED_MEM_OPT == 1
  if (cb.isTaaUpscale)
  {
    // Rename the 16x16 group into a 19x13 group + 9 idle threads in the end

    int2 newID = 0;
    const float linearID = (groupIdx + 0.5) / float(BUFFER_X);
    newID.y = int(floor(linearID));
    newID.x = int(floor(frac(linearID) * BUFFER_X));

    // Preload the colors and motion vectors into shared memory
    if (newID.y < RENAMED_GROUP_Y)
    {
      Preload(newID, groupBase + newID, cb.invMaximumRadiance);
    }

    newID.y += RENAMED_GROUP_Y;

    if (newID.y < BUFFER_Y)
    {
      Preload(newID, groupBase + newID, cb.invMaximumRadiance);
    }

    GroupMemoryBarrierWithGroupSync();
  }
#endif

  const int2 imageSizeInput = int2(cb.inputOverOutputViewSize * cb.imageSizeOutput);

  const float2 inputPos = OutputToInput(pixelPos, cb.inputOverOutputViewSize);
  const int2 inputPosInt = int2(round(inputPos));
  const int2 inputPosShared = inputPosInt - groupBase;

  const float2 uv = (pixelPos + 0.5f) * (float2)cb.invImageSizeOutput;
  float3 pixelColor = 0;

  float4 colorMoment1 = 0.0f;
  float4 colorMoment2 = 0.0f;
  float  longestMotionVectorLength = -1.0f;
  int2   longestMotionVectorPos = 0;
  float2 longestMotionVector = 0.0f;

  [unroll]
  for (int dy = -1; dy <= 1; ++dy)
  {
    [unroll]
    for (int dx = -1; dx <= 1; ++dx)
    {
#if SHARED_MEM_OPT == 1
      int2 samplePos = 0;
      float3 sampleColor = 0.0f;
      float2 motionVector = 0.0f;
      float motionVectorLength = 0.0f;

      if (cb.isTaaUpscale)
      {
        samplePos = inputPosShared + int2(dy, dx);
        if (any(samplePos < 0) || any(samplePos >= int2(BUFFER_X, BUFFER_Y)))
          continue;
        const float4 colorAndLength = s_ColorsAndLengths[samplePos.y][samplePos.x];

        sampleColor = colorAndLength.rgb;
        motionVectorLength = colorAndLength.w;
      }
      else
      {
        samplePos = inputPosInt + int2(dx, dy);
        if (any(samplePos < 0) || any(samplePos >= imageSizeInput))
          continue;
        sampleColor = PQEncode(InColorTexture[samplePos].rgb, cb.invMaximumRadiance);
        motionVector = PrimaryScreenSpaceMotionVector[samplePos].xy;
        motionVectorLength = dot(motionVector, motionVector);
      }
#else
      const int2 samplePos = inputPosInt + int2(dx, dy);
      if (any(samplePos < 0) || any(samplePos >= imageSizeInput))
        continue;

      const float3 sampleColor = PQEncode(InColorTexture[samplePos].rgb, cb.invMaximumRadiance);
      const float2 motionVector = PrimaryScreenSpaceMotionVector[samplePos].xy;
      float motionVectorLength = dot(motionVector, motionVector);
#endif
      if (dx == 0 && dy == 0)
      {
          pixelColor = sampleColor;
      }

      colorMoment1 += float4(sampleColor, 1.0f);
      colorMoment2 += float4(sampleColor * sampleColor, 1.0f);

      if (motionVectorLength > longestMotionVectorLength)
      {
        longestMotionVectorPos = samplePos;
        longestMotionVectorLength = motionVectorLength;
#if SHARED_MEM_OPT == 1
        if (!cb.isTaaUpscale)
        {
            longestMotionVector = motionVector;
        }
#else
        longestMotionVector = motionVector;
#endif
      }
    }
  }

#if SHARED_MEM_OPT == 1
  if (cb.isTaaUpscale)
  {
    longestMotionVector = s_MotionVectors[longestMotionVectorPos.y][longestMotionVectorPos.x] * cb.invMainCameraResolution;
  }
  else
  {
    longestMotionVector *= cb.invMainCameraResolution;
  }
#else
  longestMotionVector *= cb.invMainCameraResolution;
#endif

  colorMoment1.rgb /= colorMoment1.w;
  colorMoment2.rgb /= colorMoment2.w;
  const float3 colorVariance = colorMoment2.rgb - colorMoment1.rgb * colorMoment1.rgb;
  // Note: Square root of variance represents the standard deviation of the neighborhood color signal. Scaling this by
  // the clamping factor is essentially how many standard deviations of tolerence to allow for the temporally accumulated
  // color signal.
  const float3 colorSigma = sqrt(max(0, colorVariance)) * cb.colorClampingFactor;
  const float3 colorMin = colorMoment1.rgb - colorSigma;
  const float3 colorMax = colorMoment1.rgb + colorSigma;

  const float2 sourcePos = pixelPos + 0.5f + longestMotionVector * cb.imageSizeOutput;

  float3 resultPQ = 0.0f;
  if (all(sourcePos >= 0) && all(sourcePos < cb.imageSizeOutput))
  {
    const float3 historyColor = cb.isTaaUpscale ?
      BicubicSampleCatmullRom(InFeedbackColorTexture, LinearSampler, sourcePos, (float2)cb.invImageSizeOutput) :
      InFeedbackColorTexture.SampleLevel(LinearSampler, sourcePos, 0).rgb;
    const float3 historyColorClamped = min(colorMax, max(colorMin, historyColor));

    const float motionWeight = smoothstep(0, 1, length(longestMotionVector));
    const float2 distanceToLowResPixel = inputPos - float2(inputPosInt);
    const float sampleWeight = saturate(1.0 - cb.upscalingFactor * dot(distanceToLowResPixel, distanceToLowResPixel));
    const float blendWeight = saturate(max(motionWeight, sampleWeight) * cb.newFrameWeight);

    resultPQ = lerp(historyColorClamped, pixelColor, blendWeight);
  }
  else
  {
    resultPQ = pixelColor;
  }
  const float3 result = PQDecode(resultPQ, cb.maximumRadiance);

  OutColorTexture[pixelPos] = float4(result, 1.0f);
  OutFeedbackColorTexture[pixelPos] = float4(resultPQ, 0.0f);
}
