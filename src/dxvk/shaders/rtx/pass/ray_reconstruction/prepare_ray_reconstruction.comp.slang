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
#include "rtx/pass/ray_reconstruction/ray_reconstruction.h"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/utility/common.slangh"

// Inputs
layout(binding = RAY_RECONSTRUCTION_CONSTANTS_INPUT)
ConstantBuffer<RayReconstructionArgs> cb;

layout(rgba16f, binding = RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT)
Texture2D<float4> PrimaryIndirectSpecular;

layout(binding = RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT)
Texture2D<uint> PrimaryAttenuation;

layout(rgba16, binding = RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
Texture2D<float4> PrimaryVirtualWorldNormalPerceptualRoughness;

layout(binding = RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT)
Texture2D<float4> SecondaryAlbedo;

layout(rgb10_a2, binding = RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT)
Texture2D<float4> SecondarySpecularAlbedo;

layout(binding = RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT)
Texture2D<uint> SecondaryAttenuation;

layout(rgba16, binding = RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
Texture2D<float4> SecondaryVirtualWorldNormalPerceptualRoughness;

layout(r16ui, binding = RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT)
Texture2D<uint16_t> SharedFlags;

layout(rgba16f, binding = RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT)
Texture2D<float4> InNormalsDLSSRR;

layout(r32f, binding = RAY_RECONSTRUCTION_DEPTHS_INPUT)
Texture2D<float> InDepth;

layout(rg16f, binding = RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT)
Texture2D<float2> InMotionVector;

layout(r16f, binding = RAY_RECONSTRUCTION_PRIMARY_CONE_RADIUS_INPUT)
Texture2D<float> PrimaryConeRadius;

layout(r16f, binding = RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_INPUT)
Texture2D<float> DisocclusionMask;

// Input/Outputs
layout(binding = RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT)
RWTexture2D<float4> PrimaryAlbedo;

layout(rgb10_a2, binding = RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT)
RWTexture2D<float4> PrimarySpecularAlbedo;

layout(binding = RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> DebugView;

layout(r16f, binding = RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK_OUTPUT)
RWTexture2D<float> DisocclusionMaskOutput;

float getRoughnessFactor(float roughness)
{
  roughness = roughness + 0.1;
  float factor = cb.enableDemodulateRoughness ? cb.upscalerRoughnessDemodulationMultiplier * (pow(1.0 / roughness, cb.upscalerRoughnessDemodulationOffset)) : 1.0;
  return factor;
}

// Function to calculate Gaussian weight
float calcGaussianWeight(float squaredDistance, float rcpSquaredSigma) {
  return exp(-0.5 * squaredDistance * rcpSquaredSigma);
}

float convertBlurredDisocclusionMaskForRRUse(float disocclusionMask)
{
  // The disocclusion mask value is used with in a log() function in RR,
  // but it is blurred post log() for smoother results. So revert that now.
  return disocclusionMask > 0 ? exp(disocclusionMask) : 0;
}

// Blurs disocclusion mask using a spiral pattern
void blurDisocclusionMask(uint2 pixelCoordinate)
{
  if (!cb.enableDisocclusionMaskBlur)
  {
    return;
  }

  const float2 centerPixel = pixelCoordinate + 0.5f;
  const int numSamples = 8; // Number of samples in the spiral blur
  const float rcpNumSamples = 1.0f / numSamples;
  const float twoPi = 2 * 3.14159265359f;
  const float twoPiRcpNumSamples = twoPi * rcpNumSamples;
  
  const float numRotations = 2; // Number of rotations in the spiral
  const float angleDeltaPerSample = numRotations * twoPiRcpNumSamples;
  const float distanceDeltaPerSample = cb.disocclusionMaskBlurRadius * rcpNumSamples;
   
  // Introduce a frame-dependent offset to the starting angle
  const float numFramesToCycle = 16;
  const float baseAngleOffset = (cb.frameIdx % numFramesToCycle) / numFramesToCycle * twoPi;

  float sum = 0.0;
  float weightSum = 0.0;

  // Spiral blur kernel
  [unroll]
  for (int i = 0; i < numSamples; ++i) 
  {
    // Start with the center sample
    float2 offset = float2(0, 0);
    float squaredNormalizedDistance = 0.f;
    
    if (i > 0)
    {
      // Calculate angle and distance for the current sample
      float angle = i * angleDeltaPerSample + baseAngleOffset;
      float distance = i * distanceDeltaPerSample;
      float normalizedDistance = i * rcpNumSamples;
      squaredNormalizedDistance = normalizedDistance * normalizedDistance;

      // Compute sample offset in a spiral pattern
      offset = float2(cos(angle), sin(angle)) * distance;
    }

    int2 neighborCoordinate = centerPixel + offset;

    if (any(neighborCoordinate < 0) || any(neighborCoordinate >= int2(cb.camera.resolution)))
    {
      continue;
    }

    // Sample the texture at the offset position
    float sample = DisocclusionMask[neighborCoordinate];

    // Calculate Gaussian weight based on distance
    float weight = calcGaussianWeight(squaredNormalizedDistance, cb.rcpSquaredDisocclusionMaskBlurGaussianWeightSigma);

    sum += sample * weight;
    weightSum += weight;
  }

  // Normalize the result
  float blurredValue = sum / weightSum;

  // Write the blurred value to the output
  DisocclusionMaskOutput[pixelCoordinate] = convertBlurredDisocclusionMaskForRRUse(blurredValue);
}

void writeDebugViewOutputs(uint2 pixelCoordinate)
{
  switch (cb.debugView)
  {
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK:
    if (cb.enableDisocclusionMaskBlur)
    {
      storeInDebugView(pixelCoordinate, DisocclusionMaskOutput[pixelCoordinate]);
    }
    else
    {
      storeInDebugView(pixelCoordinate, DisocclusionMask[pixelCoordinate]);
    }
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO:
    storeInDebugView(pixelCoordinate, PrimaryAlbedo[pixelCoordinate]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO:
    storeInDebugView(pixelCoordinate, PrimarySpecularAlbedo[pixelCoordinate]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL:
    storeInDebugView(pixelCoordinate, InNormalsDLSSRR[pixelCoordinate]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH:
    storeInDebugView(pixelCoordinate, InDepth[pixelCoordinate]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR:
    storeInDebugView(pixelCoordinate, InMotionVector[pixelCoordinate]);
    break;
  case DEBUG_VIEW_NAN:
    bool isValid = true;

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK
    isValid &= cb.enableDisocclusionMaskBlur
      ? isValidValue(DisocclusionMaskOutput[pixelCoordinate])
      : isValidValue(DisocclusionMask[pixelCoordinate]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO
    isValid &= isValidValue(PrimaryAlbedo[pixelCoordinate]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO
    isValid &= isValidValue(PrimarySpecularAlbedo[pixelCoordinate]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL
    isValid &= isValidValue(InNormalsDLSSRR[pixelCoordinate]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH
    isValid &= isValidValue(InDepth[pixelCoordinate]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR
    isValid &= isValidValue(InMotionVector[pixelCoordinate]);

    accumulateInDebugViewAnd(pixelCoordinate, isValid);
  }
}

void preprocessInputsForRR(uint2 pixelCoordinate)
{
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(pixelCoordinate, SharedFlags);

  if (cb.combineSpecularAlbedo)
  {
    bool hasSecondarySurface = geometryFlags.hasSecondarySurface();

    vec4 primaryAttenuation = vec4(r11g11b10ToColor(PrimaryAttenuation[pixelCoordinate]), 1.0);
    vec4 secondaryAttenuation = hasSecondarySurface ? vec4(r11g11b10ToColor(SecondaryAttenuation[pixelCoordinate]), 1.0) : vec4(0,0,0,1);

    // Normalize attenuation to get the color tinting without any dimmming
    const float maxPrimaryAttenuation = max3(primaryAttenuation.r, primaryAttenuation.g, primaryAttenuation.b);
    primaryAttenuation = maxPrimaryAttenuation <= 0.f ? vec4(0.f) : primaryAttenuation / maxPrimaryAttenuation;
    const float maxSecondaryAttenuation = max3(secondaryAttenuation.r, secondaryAttenuation.g, secondaryAttenuation.b);
    secondaryAttenuation = maxSecondaryAttenuation <= 0.f ? vec4(0.f) : secondaryAttenuation / maxSecondaryAttenuation;

    // Read PrimaryAlbedo and SecondaryAlbedo from remapped coordinate, write to separate DLSS-RR output at screen-space
    vec4 primaryAlbedo          = PrimaryAlbedo[pixelCoordinate] * primaryAttenuation;
    vec4 primarySpecularAlbedo  = PrimarySpecularAlbedo[pixelCoordinate] * primaryAttenuation;

    vec4 secondaryAlbedo         = vec4(SecondaryAlbedo[pixelCoordinate].xyz, 0.0) * secondaryAttenuation;
    vec4 secondarySpecularAlbedo = vec4(SecondarySpecularAlbedo[pixelCoordinate].xyz, 0.0) * secondaryAttenuation;

    const float primaryPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;
    const float secondaryPerceptualRoughness = SecondaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;

    primarySpecularAlbedo *= getRoughnessFactor(primaryPerceptualRoughness);

    // Combine secondary albedo to improve reflection quality.
    if (hasSecondarySurface)
    {
      primaryAlbedo += secondaryAlbedo;
      primarySpecularAlbedo += secondarySpecularAlbedo * getRoughnessFactor(secondaryPerceptualRoughness);
    }

    // If the primary albedo is very close to 0, clamp it to 0.  This avoids a bug where
    // near 0 albedos cause RR to think the pixel is supposed to be very brightly lit.
    if (all(primaryAlbedo.rgb < (0.5f / 255.0f)))
    {
      primaryAlbedo = vec4(0.f, 0.f, 0.f, 1.f);
    }

    PrimaryAlbedo[pixelCoordinate] = primaryAlbedo;

    PrimarySpecularAlbedo[pixelCoordinate] = primarySpecularAlbedo;
  }

  blurDisocclusionMask(pixelCoordinate);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  // Early out for pixels outside the camera
  if (any(threadId >= cb.camera.resolution))
  {
    return;
  }

  uvec2 pixelCoordinate = threadId;

  preprocessInputsForRR(pixelCoordinate);

  writeDebugViewOutputs(pixelCoordinate);
}
