/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/demodulate/demodulate_bindings.slangh"

#include "rtx/pass/raytrace_args.h"

#include "rtx/utility/math.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/common.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/demodulate_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtx/external/NRD.slangh"

struct DemodulationData
{
  vec3 diffuseAlbedo;
  vec3 specularAlbedo;
  float perceptualRoughness;
  float linearViewZ;
};

vec3 demodulateColor(vec3 light, vec3 color)
{
  const vec3 demodulationEpsilon = vec3(0.01f);
  return light / max(color, demodulationEpsilon);
}

void demodulate(
  NrdArgs nrd,
  DemodulationData demodulationData,
  vec4 diffuseInput,
  vec4 specularInput,
  out vec4 packedDiffuseAndHitT,
  out vec4 packedSpecularAndHitT,
  bool demodulateRoughness)
{
  vec3 diffuseRadiance = diffuseInput.xyz;
  float diffuseHitT = diffuseInput.w;
  vec3 specularRadiance = specularInput.xyz;
  float specularHitT = specularInput.w;

  diffuseRadiance = demodulateColor(diffuseRadiance, demodulationData.diffuseAlbedo);
  specularRadiance = demodulateColor(specularRadiance, demodulationData.specularAlbedo);

  if (demodulateRoughness)
  {
    specularRadiance *= getRoughnessDemodulationFactor(demodulationData.perceptualRoughness, cb.roughnessDemodulationOffset);
  }

  packedDiffuseAndHitT = vec4(0.f);
  packedSpecularAndHitT = vec4(0.f);
  bool sanitize = true;
  if (nrd.isReblurEnabled > 0)
  {
    float normHitDist = 0;
    normHitDist = REBLUR_FrontEnd_GetNormHitDist(
      diffuseHitT, demodulationData.linearViewZ, nrd.hitDistanceParams, 1.0);
    packedDiffuseAndHitT = REBLUR_FrontEnd_PackRadianceAndNormHitDist(diffuseRadiance, normHitDist, sanitize);

    normHitDist = REBLUR_FrontEnd_GetNormHitDist(
      specularHitT, demodulationData.linearViewZ, nrd.hitDistanceParams, demodulationData.perceptualRoughness);
    packedSpecularAndHitT = REBLUR_FrontEnd_PackRadianceAndNormHitDist(specularRadiance, normHitDist, sanitize);
  }
  else 
  {
    packedDiffuseAndHitT = RELAX_FrontEnd_PackRadianceAndHitDist(diffuseRadiance, diffuseHitT, sanitize);
    packedSpecularAndHitT = RELAX_FrontEnd_PackRadianceAndHitDist(specularRadiance, specularHitT, sanitize);
  }
}

float2 filterAverageNeighborNonzeroHitT(Texture2D<float2> tex, float2 centerHitTs, int2 pixelCoordinate)
{
  const float invalidHitTValue = 0.0f;

  // Note: Early out to avoid sampling if no filtering needs to happen.
  if (all(centerHitTs != float2(invalidHitTValue, invalidHitTValue)))
  {
    return centerHitTs;
  }

  // Average the neighboring Hit T values

  float2 sum = float2(0.0f, 0.0f);
  float2 count = 0;

  const int r = 1;
  for (int xx = -r; xx <= r; xx++)
  {
    for (int yy = -r; yy <= r; yy++)
    {
      // Note: Skip center pixel as it is known to be an invalid Hit T (for at least one of the
      // channels).
      if (xx != 0 || yy != 0)
      {
        const float2 value = tex[pixelCoordinate + int2(xx, yy)].xy;

        sum += value;
        // Note: Ignore HitT values of 0 as these signal invalid samples (since not all pixel will
        // have a valid sample to accumulate from).
        count.x += (value.x > invalidHitTValue) ? 1 : 0;
        count.y += (value.y > invalidHitTValue) ? 1 : 0;
      }
    }
  }

  const float2 averagedHitTs = sum / max(count, 1);

  return float2(
    (centerHitTs.x == invalidHitTValue) ? averagedHitTs.x : centerHitTs.x,
    (centerHitTs.y == invalidHitTValue) ? averagedHitTs.y : centerHitTs.y);
}

groupshared uint s_hashedValue1[12];
groupshared uint s_hashedValue2[12];
groupshared uint s_hashedCount[12];

uint encodeLuminance(float luminance)
{
  return uint(luminance * 1024);
}

float decodeLuminance(float encodedLuminance)
{
  float l = encodedLuminance / 1024.0;
  return l;
}

uint encodeDepth(float depth)
{
  return uint(sqrt(depth) * 65536);
}

float decodeDepth(uint depth)
{
  float l = float(depth) / 65536.0;
  return l * l;
}

uint getNormalDepthKey(f16vec3 normal, float depth, float depthSplit)
{
  f16vec3 absNormal = abs(normal);
  float16_t maxNormal = max(absNormal.x, max(absNormal.y, absNormal.z));
  uint normalKey = (maxNormal == absNormal.x ? 0 : (maxNormal == absNormal.y ? 1 : 2));
  normalKey += maxNormal == normal[int(normalKey)] ? 0 : 3;

  uint depthKey = depth > depthSplit ? 0 : 6;
  return normalKey + depthKey;
}

// The boiling filter divides pixels in the same block into several clusters, calculates average
// luminance for each area, then remove outliers whose luminance is too high 
void applyDirectLightBoilingFilter(
  uint2 localIndex,
  float primaryLinearViewZ, vec3 primaryVirtualWorldNormal,
  inout vec3 primaryDirectDiffuseOutput,
  inout vec3 primaryDirectSpecularOutput)
{
  // Calculate depth range inside a block
  uint dummyI;
  float absDepth = abs(primaryLinearViewZ);
  uint encodedDepth = encodeDepth(absDepth);      
  if (localIndex.x == 0 && localIndex.y == 0)
  {
    s_hashedValue1[0] = encodedDepth;
    s_hashedValue1[1] = 0;
  }
  GroupMemoryBarrierWithGroupSync();
  InterlockedMin(s_hashedValue1[0], encodedDepth, dummyI);
  InterlockedMax(s_hashedValue1[1], encodedDepth, dummyI);
  GroupMemoryBarrierWithGroupSync();

  float minDepth = decodeDepth(s_hashedValue1[0]);
  float maxDepth = decodeDepth(s_hashedValue1[1]);
  float depthSplit = maxDepth > 1.5 * minDepth ? lerp(minDepth, maxDepth, 0.5) : 0;

  // Use depth and normal to calculate a key
  GroupMemoryBarrierWithGroupSync();
  if (localIndex.x < 6 && localIndex.y == 0)
  {
    s_hashedValue1[localIndex.x] = 0;
    s_hashedValue2[localIndex.x] = 0;
    s_hashedCount[localIndex.x] = 0;
  }
  uint key = getNormalDepthKey(primaryVirtualWorldNormal, absDepth, depthSplit);
  GroupMemoryBarrierWithGroupSync();

  // Calculate average luminance value for pixels belong to the same key
  float diffuseLuminance = calcBt709Luminance(primaryDirectDiffuseOutput.xyz);
  float specularLuminance = calcBt709Luminance(primaryDirectSpecularOutput.xyz);
  InterlockedAdd(s_hashedValue1[key], encodeLuminance(diffuseLuminance), dummyI);
  InterlockedAdd(s_hashedValue2[key], encodeLuminance(specularLuminance), dummyI);
  InterlockedAdd(s_hashedCount[key], 1, dummyI);
  GroupMemoryBarrierWithGroupSync();
  float avgDiffuseLuminance = decodeLuminance(float(s_hashedValue1[key]) / s_hashedCount[key]);
  float avgSpecularLuminance = decodeLuminance(float(s_hashedValue2[key]) / s_hashedCount[key]);

  // Discard specular firefly signal based on average luminance value
  if (s_hashedCount[key] <= 60 && specularLuminance > avgSpecularLuminance * cb.directLightBoilingThreshold)
  {
    primaryDirectSpecularOutput.xyz *= 0;
  }
  if (s_hashedCount[key] <= 60 && diffuseLuminance > avgDiffuseLuminance * cb.directLightBoilingThreshold)
  {
    primaryDirectDiffuseOutput.xyz *= 0;
  }
}

void outputToDebugViewAtTheEnd(
  uint2 pixelCoordinate,
  vec3 primarySpecularAlbedo,
  vec3 secondarySpecularAlbedo,
  vec4 primaryDirectDiffuseInput,
  vec4 primaryDirectSpecularInput,
  vec4 primaryIndirectDiffuseInput,
  vec4 primaryIndirectSpecularInput,
  vec4 secondaryCombinedDiffuseInput,
  vec4 secondaryCombinedSpecularInput,
  vec4 primaryDirectDiffuseOutput,
  vec4 primaryDirectSpecularOutput,
  vec4 primaryIndirectDiffuseOutput,
  vec4 primaryIndirectSpecularOutput,
  vec4 secondaryCombinedDiffuseOutput,
  vec4 secondaryCombinedSpecularOutput)
{
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(pixelCoordinate, SharedFlags);

  // Output Debug Information
  switch(cb.debugView)
  {
  default:
    break;
  case DEBUG_SURFACE_LOBE_CONSISTENCY:
    // Check for consistency between Geometry Flags and Primary/Secondary Diffuse/Specular surfaces
    // Note: Do you like nested if statements because I don't. This logic should encapsulate all the potential paths that can result
    // from combinations of geometry flags and things like ReSTIR GI's presence to catch radiance in output variables where it shouldn't be.
    // May need to be updated in the future if any of that changes though.

    // Note: Primary direct diffuse and specular will always be potentially written to in every path
    // so they do not need to be checked for consistency purposes.
    bool primaryIndirectDiffuseConsistencyCheckFailure = false;
    bool primaryIndirectSpecularConsistencyCheckFailure = false;
    bool secondaryCombinedDiffuseConsistencyCheckFailure = false;
    bool secondaryCombinedSpecularConsistencyCheckFailure = false;

    // Primary + Secondary Present
    if (geometryFlags.secondarySurfaceMask)
    {
      // Primary Only
      if (geometryFlags.primarySelectedIntegrationSurface)
      {
        // Primary Specular Indirect Only
        if (geometryFlags.firstSampledLobeIsSpecular)
        {
          if (cb.enableReSTIRGI)
          {
            // Note: ReSTIR GI may output indirect diffuse even when only indirect specular was selected to be integrated.
            primaryIndirectDiffuseConsistencyCheckFailure |= primaryIndirectDiffuseOutput.a != kEmptyPixelHitDistance;
          }
          else
          {
            primaryIndirectDiffuseConsistencyCheckFailure |= any(primaryIndirectDiffuseOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
          }
        }
        else // Primary Diffuse Indirect Only
        {
          if (cb.enableReSTIRGI)
          {
            // Note: ReSTIR GI may output indirect specular even when only indirect specular was selected to be integrated.
            primaryIndirectSpecularConsistencyCheckFailure |= primaryIndirectSpecularOutput.a != kEmptyPixelHitDistance;
          }
          else
          {
            primaryIndirectSpecularConsistencyCheckFailure |= any(primaryIndirectSpecularOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
          }
        }

        secondaryCombinedDiffuseConsistencyCheckFailure |= any(secondaryCombinedDiffuseOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
        secondaryCombinedSpecularConsistencyCheckFailure |= any(secondaryCombinedSpecularOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
      }
      else // Secondary Only
      {
        // Note: Secondary signals are combined so there is no way to check for which one the indirect
        // integrated lobe was factored into.

        // Note: Not even ReSTIR GI should be outputting indirect diffuse/specular data when the primary surface was not selected for integration.
        primaryIndirectDiffuseConsistencyCheckFailure |= any(primaryIndirectDiffuseOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
        primaryIndirectSpecularConsistencyCheckFailure |= any(primaryIndirectSpecularOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
      }
    }
    else // Primary Only
    {
      if (geometryFlags.primarySelectedIntegrationSurface)
      {
        if (geometryFlags.firstSampledLobeIsSpecular)
        {
          if (cb.enableReSTIRGI)
          {
            // Note: ReSTIR GI may output indirect diffuse even when only indirect diffuse was selected to be integrated.
            primaryIndirectDiffuseConsistencyCheckFailure |= primaryIndirectDiffuseOutput.a != kEmptyPixelHitDistance;
          }
          else
          {
            primaryIndirectDiffuseConsistencyCheckFailure |= any(primaryIndirectDiffuseOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
          }
        }
        else // Primary Diffuse Indirect Only
        {
          if (cb.enableReSTIRGI)
          {
            // Note: ReSTIR GI may output indirect specular even when only indirect specular was selected to be integrated.
            primaryIndirectSpecularConsistencyCheckFailure |= primaryIndirectSpecularOutput.a != kEmptyPixelHitDistance;
          }
          else
          {
            primaryIndirectSpecularConsistencyCheckFailure |= any(primaryIndirectSpecularOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
          }
        }

        secondaryCombinedDiffuseConsistencyCheckFailure |= any(secondaryCombinedDiffuseOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
        secondaryCombinedSpecularConsistencyCheckFailure |= any(secondaryCombinedSpecularOutput != vec4(0.0f, 0.0f, 0.0f, kEmptyPixelHitDistance));
      } 
      else 
      {
        // Note: Secondary surface should never be selected for integration when the secondary surface
        // mask is false (meaning no secondary surface exists).
        secondaryCombinedDiffuseConsistencyCheckFailure = true;
        secondaryCombinedSpecularConsistencyCheckFailure = true;
      }
    }

    // Note:
    // Red - Invalid Primary Indirect Diffuse Signal
    // Green - Invalid Primary Indirect Specular Signal
    // Blue - Invalid Secondary Combined Diffuse/Specular Signal
    storeInDebugView(ivec2(pixelCoordinate), vec4(
      primaryIndirectDiffuseConsistencyCheckFailure ? 1.0f : 0.0f,
      primaryIndirectSpecularConsistencyCheckFailure ? 1.0f : 0.0f,
      (secondaryCombinedDiffuseConsistencyCheckFailure || secondaryCombinedSpecularConsistencyCheckFailure) ? 1.0f : 0.0f,
      1.0f
    ));

    break;
  // Specular Albedos
  case DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO:
    storeInDebugView(ivec2(pixelCoordinate), primarySpecularAlbedo);
    break;
  case DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO:
    storeInDebugView(ivec2(pixelCoordinate), secondarySpecularAlbedo);
    break;
  // Primary Direct Data
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectDiffuseInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectDiffuseInput.w);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectSpecularInput.w);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectDiffuseOutput.xyz);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectSpecularOutput.xyz);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectDiffuseOutput.w);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_SPECULAR_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryDirectSpecularOutput.w);
    break;
  // Primary Indirect Data
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectDiffuseInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectDiffuseInput.w);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectSpecularInput.w);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectDiffuseOutput.xyz);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectSpecularOutput.xyz);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectDiffuseOutput.w);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    storeInDebugView(ivec2(pixelCoordinate), primaryIndirectSpecularOutput.w);
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), secondaryCombinedDiffuseInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), secondaryCombinedSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), secondaryCombinedDiffuseOutput.xyz);
    break;
  case DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), secondaryCombinedSpecularOutput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), 
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz +
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), 
      secondaryCombinedDiffuseInput.xyz + secondaryCombinedSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_RADIANCE:
    storeInDebugView(ivec2(pixelCoordinate), 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz +
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz +
      secondaryCombinedDiffuseInput.xyz + secondaryCombinedSpecularInput.xyz);
    break;

  case DEBUG_VIEW_NAN:
  {
    bool isValid = true;
    
    // DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO
    isValid &= isValidValue(primarySpecularAlbedo);
    // DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO
    isValid &= isValidValue(secondarySpecularAlbedo);
    // DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(primaryDirectDiffuseInput.xyz);
    // DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(primaryDirectSpecularInput.xyz);
    // DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(primaryDirectDiffuseInput.w);
    // DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(primaryDirectSpecularInput.w);
    // DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(primaryDirectDiffuseOutput.xyz);
    // DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(primaryDirectSpecularOutput.xyz);
    // DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(primaryDirectDiffuseOutput.w);
    // DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(primaryDirectSpecularOutput.w);
    // DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(primaryIndirectDiffuseInput.xyz);
    // DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(primaryIndirectSpecularInput.xyz);
    // DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(primaryIndirectDiffuseInput.w);
    // DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(primaryIndirectSpecularInput.w);
    // DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(primaryIndirectDiffuseOutput.xyz);
    // DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(primaryIndirectSpecularOutput.xyz);
    // DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(primaryIndirectDiffuseOutput.w);
    // DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(primaryIndirectSpecularOutput.w);
    // DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE
    isValid &= isValidValue(secondaryCombinedDiffuseInput.xyz);
    // DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE
    isValid &= isValidValue(secondaryCombinedSpecularInput.xyz);
    // DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE
    isValid &= isValidValue(secondaryCombinedDiffuseOutput.xyz);
    // DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE
    isValid &= isValidValue(secondaryCombinedSpecularOutput.xyz);
    
    accumulateInDebugViewAnd(ivec2(pixelCoordinate), isValid);
  }
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 pixelCoordinate : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  const Camera camera = cb.camera;

  // Early out for pixels outside the camera

  if (any(pixelCoordinate >= camera.resolution))
  {
    return;
  }

  // Reconstruct the primary ray direction for the current pixel

  const CameraDirections primaryRayDirections = cameraPixelCoordinateToDirection(camera, pixelCoordinate);

  // Deserialize flags
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(pixelCoordinate, SharedFlags);

  // Load Primary G-Buffer data
  const vec3 primaryDiffuseAlbedo = PrimaryAlbedo[pixelCoordinate].xyz;
  const vec3 primaryBaseReflectivity = aliasedData0.PrimaryBaseReflectivity[pixelCoordinate].xyz;
  // Note: Decode from unorm encoded representation.
  const vec3 primaryVirtualWorldNormal = PrimaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].xyz * 2.0f - 1.0f;
  const float primaryPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;
  const float primaryLinearViewZ = PrimaryLinearViewZ[pixelCoordinate].x;

  // Load Secondary G-Buffer data
  const vec3 secondaryDiffuseAlbedo = SecondaryAlbedo[pixelCoordinate].xyz;
  const vec3 secondaryBaseReflectivity = aliasedData1.SecondaryBaseReflectivity[pixelCoordinate].xyz;
  // Note: Decode from unorm encoded representation.
  const vec3 secondaryVirtualWorldNormal = SecondaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].xyz * 2.0f - 1.0f;
  const float secondaryPerceptualRoughness = SecondaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;
  const float secondaryLinearViewZ = SecondaryLinearViewZ[pixelCoordinate].x;

  // Calculate Primary and Secondary Specular Albedo Approximations
  // Note: This is done to have higher quality demodulation than using a simple base reflectivity.

  // Note: Virtual world normal used so direction does not need to be read from the GBuffer, simple camera direction can
  // be re-derived as virtual normals are oriented with respect to the camera.
  const float primaryNormalDotViewDirection = dot(primaryVirtualWorldNormal, -primaryRayDirections.worldDirection);
  const float secondaryNormalDotViewDirection = dot(secondaryVirtualWorldNormal, -primaryRayDirections.worldDirection);

  // Note: Somewhat costly math operations, but the secondary specular albedo will (hopefully) be skipped out in most cases
  // due to its sole usage in secondary surface demodulation which is only enabled if the secondary surface mask is set (though
  // the debug view may drag this contribution in anyways potentially, so remove debug view code ideally).
  const vec3 primarySpecularAlbedo = evalSpecularAlbedoGGXSchlick(
    primaryBaseReflectivity, calcRoughness(primaryPerceptualRoughness),
    primaryNormalDotViewDirection);
  const vec3 secondarySpecularAlbedo = geometryFlags.performPSTR || geometryFlags.performPSRR ? evalSpecularAlbedoGGXSchlick(
    secondaryBaseReflectivity, calcRoughness(secondaryPerceptualRoughness),
    secondaryNormalDotViewDirection) : vec3(0);

  // Write out the Primary and Secondary Specular Albedo
  // Note: This overwrites the base reflectivity used in these buffers up to this point. This is fine though as
  // only the composite pass will use this information later to remodulate the signal after it has been denoised.

  aliasedData0.PrimarySpecularAlbedo[pixelCoordinate].xyz = primarySpecularAlbedo;
  aliasedData1.SecondarySpecularAlbedo[pixelCoordinate].xyz = secondarySpecularAlbedo;

  // Set Primary G-Buffer data
  DemodulationData primaryDemodulationData;
  primaryDemodulationData.diffuseAlbedo = primaryDiffuseAlbedo;
  primaryDemodulationData.specularAlbedo = primarySpecularAlbedo;
  primaryDemodulationData.perceptualRoughness = primaryPerceptualRoughness;
  primaryDemodulationData.linearViewZ = primaryLinearViewZ;

  // Set Secondary Demodulation Data
  DemodulationData secondaryDemodulationData;
  secondaryDemodulationData.diffuseAlbedo = secondaryDiffuseAlbedo;
  secondaryDemodulationData.specularAlbedo = secondarySpecularAlbedo;
  secondaryDemodulationData.perceptualRoughness = secondaryPerceptualRoughness;
  secondaryDemodulationData.linearViewZ = secondaryLinearViewZ;

  // Inputs
  // Pixels with zero Hit T will have denoiser try to fill in missing hit T data, 0 radiance will be used in filtering as-is
  // Note: inactive pixel inputs are not initialized to valid PT outputs as they are not used to generate outputs for the denoiser,
  //  but we set them to 0 here for DebugView usage purposes 
  vec4 primaryDirectDiffuseInput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryDirectSpecularInput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryIndirectDiffuseInput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryIndirectSpecularInput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 secondaryCombinedDiffuseInput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 secondaryCombinedSpecularInput = vec4(vec3(0), kEmptyPixelHitDistance);
  
  // Load the primary direct and indirect signals
  // If rtxdi and restir gi are enabled, the primary surface input is dense and demodulation can always be enabled.
  bool demodulatePrimaryInput = (cb.enableRtxdi && cb.enableReSTIRGI) || geometryFlags.primarySelectedIntegrationSurface;
  if (demodulatePrimaryInput)
  {
    primaryDirectDiffuseInput = PrimaryDirectDiffuseRadiance[pixelCoordinate];
    primaryDirectSpecularInput = PrimaryDirectSpecularRadiance[pixelCoordinate];
  
    primaryIndirectDiffuseInput = PrimaryIndirectDiffuseRadiance[pixelCoordinate];
    primaryIndirectSpecularInput = PrimaryIndirectSpecularRadiance[pixelCoordinate];
  }
  
  // Load the secondary combined signals
  if (geometryFlags.secondarySurfaceMask && !geometryFlags.primarySelectedIntegrationSurface)
  {
    const vec4 indirectRadianceHitDistance = IndirectRadianceHitDistance[pixelCoordinate];
    
    secondaryCombinedDiffuseInput = SecondaryCombinedDiffuseRadiance[pixelCoordinate];
    secondaryCombinedSpecularInput = SecondaryCombinedSpecularRadiance[pixelCoordinate];

    // Note: For secondary surfaces combine direct and indirect radiance for the current lobe.
    // Note: Radiance only carried over on secondary surfaces, always through the sampled lobe (as this is the lobe indirect integration
    // is done on, the opposing lobe is only given direct lighting).
    if (geometryFlags.firstSampledLobeIsSpecular)
      secondaryCombinedSpecularInput = sanitizeRadianceHitDistance(
        secondaryCombinedSpecularInput.xyz + indirectRadianceHitDistance.xyz, indirectRadianceHitDistance.w);
    else
      secondaryCombinedDiffuseInput = sanitizeRadianceHitDistance(
        secondaryCombinedDiffuseInput.xyz + indirectRadianceHitDistance.xyz, indirectRadianceHitDistance.w);

    // The secondary signal doesn't have advanced techniques like ReSTIR and has more noise and fireflies, especially for specular signal.
    // A more restrictive clamping threshold is needed to remove fireflies.
    secondaryCombinedSpecularInput.xyz = fireflyFiltering(secondaryCombinedSpecularInput.xyz, cb.secondarySpecularFireflyFilteringThreshold);
  }

  // Overwrite the direct specular HitT with the indirect specular HitT.
  // The reason is that the denoiser uses HitT mostly for calculating motion vectors for reflections.
  // For that purpose, the denoiser expects to see HitT from surfaces that are in the specular reflection lobe.
  // When calculating direct lighting (NEE/RTXDI), we select a light per pixel, and the distance to that light
  // becomes the HitT for both diffuse and specular channels. In many cases, the light is selected 
  // for a surface because of its diffuse contribution, not specular, which makes the specular channel
  // contain the HitT of a diffuse light. That confuses the denoiser and breaks reprojection.
  // On the other hand, the indirect specular HitT is always computed by tracing rays in the specular lobe.
  //
  // If secondary bounces are disabled, the primaryIndirectSpecularInput will contain garbage data, which shouldn't be
  // used to replace the primaryDirectSpecularInput
  if (cb.enableReplaceDirectSpecularHitTWithIndirectSpecularHitT && cb.enableSecondaryBounces)
  {
    primaryDirectSpecularInput.a = primaryIndirectSpecularInput.a;
  }

  // If separate denoisers are disabled, combine the inputs into one channel. Note that this is only done here as this mode
  // is meant more for debugging and not actual production use, otherwise the summing of the radiance channels should happen
  // in the integrator to not waste memory bandwidth. Direct and indirect radiance will be combined after demodulation 
  // because of direct light boiling filter.
  if (!cb.enableSeparatedDenoisers)
  {
    const float primaryDirectDiffuseLuminance = calcBt709Luminance(primaryDirectDiffuseInput.rgb);
    const float primaryIndirectDiffuseLuminance = calcBt709Luminance(primaryIndirectDiffuseInput.rgb);

    // We mix the direct and indirect HitT's for the diffuse channel here, to get somewhat sharper shadows.
    // But sometimes they can get too sharp, so clamp the direct (NEE) HitT contribution to a user-defined percentage.
    float directHitTContribution =
      primaryDirectDiffuseLuminance /
      max(primaryDirectDiffuseLuminance + primaryIndirectDiffuseLuminance, 0.001);
    directHitTContribution = clamp(directHitTContribution, 0.0, cb.primaryDirectNrd.maxDirectHitTContribution);

    primaryDirectDiffuseInput.a = mix(primaryIndirectDiffuseInput.a, primaryDirectDiffuseInput.a, directHitTContribution);
  }

  // Demodulate and output information

  // Pixels with zero Hit T will have denoiser try to fill in missing hit T data, 0 radiance will be used in filtering as-is
  vec4 primaryDirectDiffuseOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryDirectSpecularOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryIndirectDiffuseOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 primaryIndirectSpecularOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 secondaryCombinedDiffuseOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  vec4 secondaryCombinedSpecularOutput = vec4(vec3(0), kEmptyPixelHitDistance);

  if (demodulatePrimaryInput)
  {
    // Demodulate primary direct inputs
    demodulate(
      cb.primaryDirectNrd, primaryDemodulationData,
      primaryDirectDiffuseInput, primaryDirectSpecularInput,
      primaryDirectDiffuseOutput, primaryDirectSpecularOutput, cb.enableDemodulateRoughness);

    if (cb.enableDirectLightBoilingFilter)
    {
      applyDirectLightBoilingFilter(localIndex, primaryLinearViewZ, primaryVirtualWorldNormal, 
        primaryDirectDiffuseOutput.xyz, primaryDirectSpecularOutput.xyz);
    }

    // Demodulate primary indirect inputs    
    demodulate(
      cb.primaryIndirectNrd, primaryDemodulationData,
      primaryIndirectDiffuseInput, primaryIndirectSpecularInput,
      primaryIndirectDiffuseOutput, primaryIndirectSpecularOutput, cb.enableDemodulateRoughness);
    
    // If separate denoisers are disabled, combine direct and indirect radiance signals.
    if (!cb.enableSeparatedDenoisers)
    {
      primaryDirectDiffuseOutput.xyz += primaryIndirectDiffuseOutput.xyz;
      primaryDirectSpecularOutput.xyz += primaryIndirectSpecularOutput.xyz;
      primaryIndirectDiffuseOutput = primaryIndirectSpecularOutput = 0;
    }
  }

  // Note: No need to demodulate any data outside the bounds of the secondary surface mask as they will not be read
  // or remodulated during compositing, so don't bother with it to save a bit of performance/memory bandwidth in the
  // common case (since secondary surfaces aren't super common).
  if (geometryFlags.secondarySurfaceMask && !geometryFlags.primarySelectedIntegrationSurface)
  {
    // Demodulate secondary combined inputs
    demodulate(
      cb.secondaryCombinedNrd, secondaryDemodulationData,
      secondaryCombinedDiffuseInput, secondaryCombinedSpecularInput,
      secondaryCombinedDiffuseOutput, secondaryCombinedSpecularOutput, false);
  }

  // Output to resources fed to the denoiser
  
  PrimaryDirectDiffuseRadiance[pixelCoordinate] = primaryDirectDiffuseOutput;
  PrimaryDirectSpecularRadiance[pixelCoordinate] = primaryDirectSpecularOutput;
  
  if (cb.enableSeparatedDenoisers) 
  {
    PrimaryIndirectDiffuseRadiance[pixelCoordinate] = primaryIndirectDiffuseOutput;
    PrimaryIndirectSpecularRadiance[pixelCoordinate] = primaryIndirectSpecularOutput;
  }
  
  SecondaryCombinedDiffuseRadiance[pixelCoordinate] = secondaryCombinedDiffuseOutput;
  SecondaryCombinedSpecularRadiance[pixelCoordinate] = secondaryCombinedSpecularOutput;

  outputToDebugViewAtTheEnd(
    pixelCoordinate,
    primarySpecularAlbedo, secondarySpecularAlbedo,
    primaryDirectDiffuseInput, primaryDirectSpecularInput,
    primaryIndirectDiffuseInput, primaryIndirectSpecularInput,
    secondaryCombinedDiffuseInput, secondaryCombinedSpecularInput, 
    primaryDirectDiffuseOutput, primaryDirectSpecularOutput,
    primaryIndirectDiffuseOutput, primaryIndirectSpecularOutput,
    secondaryCombinedDiffuseOutput, secondaryCombinedSpecularOutput);
}
