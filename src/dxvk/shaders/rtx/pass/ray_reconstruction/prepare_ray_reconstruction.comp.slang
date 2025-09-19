/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

layout(r32ui, binding = RAY_RECONSTRUCTION_NORMALS_INPUT)
Texture2D<uint> Normals;

layout(rgba16f, binding = RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT)
Texture2D<float4> VirtualNormals;

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

layout(binding = RAY_RECONSTRUCTION_COMBINED_INPUT)
Texture2D<float4> CombinedInput;

layout(rgba16f, binding = RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT)
Texture2D<float4> InNormalsDLSSRR;

layout(r32f, binding = RAY_RECONSTRUCTION_DEPTHS_INPUT)
Texture2D<float4> InDepth;

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

// Outputs
layout(rgba16f, binding = RAY_RECONSTRUCTION_NORMALS_OUTPUT)
RWTexture2D<float4> NormalsOutput;

layout(r16f, binding = RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT)
RWTexture2D<float> HitDistanceOutput;

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
void blurDisocclusionMask(uint2 threadId) 
{
  if (!cb.enableDisocclusionMaskBlur)
  {
    return;
  }

  const float2 centerPixel = threadId + 0.5f;
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

    // Sample the texture at the offset position
    float sample = DisocclusionMask[int2(centerPixel + offset)];

    // Calculate Gaussian weight based on distance
    float weight = calcGaussianWeight(squaredNormalizedDistance, cb.rcpSquaredDisocclusionMaskBlurGaussianWeightSigma);

    sum += sample * weight;
    weightSum += weight;
  }

  // Normalize the result
  float blurredValue = sum / weightSum;

  // Write the blurred value to the output
  DisocclusionMaskOutput[threadId] = convertBlurredDisocclusionMaskForRRUse(blurredValue);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  const Camera camera = cb.camera;

  // Early out for pixels outside the camera

  if (any(threadId >= camera.resolution))
  {
    return;
  }

  const bool isPrimaryGBufferMiss = PrimaryConeRadius[threadId].x == 0.0f;

  // Choose normal inputs for DLSS-RR
  {
    // RR expects 0 normal on misses
    float3 normal = float3(0);

    if (!isPrimaryGBufferMiss)
    {
      if (cb.enableDLSSRRInputs)
      {
        normal = InNormalsDLSSRR[threadId].xyz;
      }
      else if (cb.rayReconstructionUseVirtualNormals)
      {
        normal = texelFetch(VirtualNormals, threadId, 0).xyz * 2.0 - 1.0;
      }
      else
      {
        vec2 octNormal = snorm2x16ToFloat2x32(texelFetch(Normals, threadId, 0));
        normal = signedOctahedralToSphereDirection(octNormal);
      }
    }

    imageStore(NormalsOutput, threadId, float4(normal, 0.0));
  }

  const float primaryPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
  const float secondaryPerceptualRoughness = SecondaryVirtualWorldNormalPerceptualRoughness[threadId].w;

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);
  if (cb.combineSpecularAlbedo)
  {
    bool hasPSR = geometryFlags.performPSTR || geometryFlags.performPSRR;

    vec4 primaryAttenuation = vec4(r11g11b10ToColor(PrimaryAttenuation[threadId]), 1.0);
    vec4 secondaryAttenuation = hasPSR ? vec4(r11g11b10ToColor(SecondaryAttenuation[threadId]), 1.0) : vec4(0,0,0,1);

    // Normalize attenuation to get the color tinting without any dimmming
    const float maxPrimaryAttenuation = max3(primaryAttenuation.r, primaryAttenuation.g, primaryAttenuation.b);
    primaryAttenuation = maxPrimaryAttenuation <= 0.f ? vec4(0.f) : primaryAttenuation / maxPrimaryAttenuation;
    const float maxSecondaryAttenuation = max3(secondaryAttenuation.r, secondaryAttenuation.g, secondaryAttenuation.b);
    secondaryAttenuation = maxSecondaryAttenuation <= 0.f ? vec4(0.f) : secondaryAttenuation / maxSecondaryAttenuation;

    vec4 primaryAlbedo          = PrimaryAlbedo[threadId] * primaryAttenuation;
    vec4 primarySpecularAlbedo  = PrimarySpecularAlbedo[threadId] * primaryAttenuation;

    vec4 secondaryAlbedo         = vec4(SecondaryAlbedo[threadId].xyz, 0.0) * secondaryAttenuation;
    vec4 secondarySpecularAlbedo = vec4(SecondarySpecularAlbedo[threadId].xyz, 0.0) * secondaryAttenuation;

    primarySpecularAlbedo *= getRoughnessFactor(primaryPerceptualRoughness);
    
    // Combine secondary albedo to improve reflection quality.
    if (hasPSR)
    {
      primaryAlbedo += secondaryAlbedo;
      primarySpecularAlbedo += secondarySpecularAlbedo * getRoughnessFactor(secondaryPerceptualRoughness);
    }

    // If the primary albedo is very close to 0, clamp it to 0.  This avoids a bug where
    // near 0 albedos cause RR to think the pixel is supposed to be very brightly lit.
    if (all(primaryAlbedo.rgb < (0.5f / 255.0f))) {
      primaryAlbedo = vec4(0.f, 0.f, 0.f, 1.f);
    }

    PrimaryAlbedo[threadId] = primaryAlbedo;
    PrimarySpecularAlbedo[threadId] = primarySpecularAlbedo;
  }

  blurDisocclusionMask(threadId);

  // DLSS-RR doesn't expect hitT with holes, here a simple box filter is used to provide a dense hitT input.
  // The filter calculates average hitT in 3x3 area, excluding the minimum and the maximum ones.
  float hitDistance = 0;

  if (!isPrimaryGBufferMiss)
  {
    hitDistance = PrimaryIndirectSpecular[threadId].a;
    
    if (cb.filterHitT)
    {
      const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);
      // Calculate average specular hitT
      float totalHitT = 0;
      float totalWeight = 0;
      float minHitT = 1e10;
      float maxHitT = 0;
      for (int i = 0; i < 9; ++i)
      {
        int2 offset = int2(i % 3, i / 3) - 1;
        const GeometryFlags neighborFlags = geometryFlagsReadFromGBuffer(threadId + offset, SharedFlags);
        if (neighborFlags.firstSampledLobeIsSpecular)
        {
          float hitT = PrimaryIndirectSpecular[threadId + offset].a;
          totalHitT += hitT;
          totalWeight += 1;
          minHitT = min(minHitT, hitT);
          maxHitT = max(maxHitT, hitT);
        }
      }

      float hitT = hitDistance;
      if (!geometryFlags.firstSampledLobeIsSpecular && totalWeight > 0)
      {
        // If this pixel doesn't have specular hitT, use average specular hitT from neighbors
        hitT = totalHitT / totalWeight;
      }
      else if ((hitDistance == minHitT || hitDistance == maxHitT) && totalWeight > 2)
      {
        // Remove minimum or maximum hitT and calculate average hitT
        totalHitT -= minHitT + maxHitT;
        totalWeight -= 2;
        hitT = totalHitT / totalWeight;
      }
      hitDistance = hitT;
    }
  }

  HitDistanceOutput[threadId] = hitDistance;

  switch (cb.debugViewIdx)
  {
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK:
    if (cb.enableDisocclusionMaskBlur)
    {
      storeInDebugView(threadId, DisocclusionMaskOutput[threadId]);
    }
    else 
    {
      storeInDebugView(threadId, DisocclusionMask[threadId]);
    }
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO:
    storeInDebugView(threadId, PrimaryAlbedo[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO:
    storeInDebugView(threadId, PrimarySpecularAlbedo[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_HIT_DISTANCE:
    storeInDebugView(threadId, hitDistance / 1000.0);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL:
    storeInDebugView(threadId, NormalsOutput[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH:
    storeInDebugView(threadId, InDepth[threadId].x);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR:
    storeInDebugView(threadId, InMotionVector[threadId]);
    break;
  case DEBUG_VIEW_NAN:
    bool isValid = true;

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_MASK
    isValid &= cb.enableDisocclusionMaskBlur
      ? isValidValue(DisocclusionMaskOutput[threadId])
      : isValidValue(DisocclusionMask[threadId]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO
    isValid &= isValidValue(PrimaryAlbedo[threadId]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO
    isValid &= isValidValue(PrimarySpecularAlbedo[threadId]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_HIT_DISTANCE
    isValid &= isValidValue(hitDistance);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL
    isValid &= isValidValue(NormalsOutput[threadId]);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH
    isValid &= isValidValue(InDepth[threadId].x);

    // DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR
    isValid &= isValidValue(InMotionVector[threadId]);
    
    accumulateInDebugViewAnd(threadId, isValid);
  }
}
