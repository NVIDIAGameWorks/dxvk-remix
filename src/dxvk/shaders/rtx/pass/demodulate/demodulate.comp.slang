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
#include "rtx/pass/sparse_rendering/sparse_rendering.slangh"
#include "rtx/pass/sparse_rendering/compact_active_pixels.slangh"
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

  // Calculate average luminance value for pixels belonging to the same key
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

struct PrimaryDemodulationSignals
{
  vec4 directDiffuseInput;
  vec4 directSpecularInput;
  vec4 indirectDiffuseInput;
  vec4 indirectSpecularInput;
  vec4 directDiffuseOutput;
  vec4 directSpecularOutput;
  vec4 indirectDiffuseOutput;
  vec4 indirectSpecularOutput;
};

struct SecondaryDemodulationSignals
{
  vec4 combinedDiffuseInput;
  vec4 combinedSpecularInput;
  vec4 indirectRadianceHitDistanceInput;
  vec4 combinedDiffuseOutput;
  vec4 combinedSpecularOutput;
};

PrimaryDemodulationSignals makeEmptyPrimaryDemodulationSignals()
{
  PrimaryDemodulationSignals primarySignals;
  primarySignals.directDiffuseInput     = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.directSpecularInput    = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.indirectDiffuseInput   = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.indirectSpecularInput  = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.directDiffuseOutput    = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.directSpecularOutput   = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.indirectDiffuseOutput  = vec4(vec3(0), kEmptyPixelHitDistance);
  primarySignals.indirectSpecularOutput = vec4(vec3(0), kEmptyPixelHitDistance);
  return primarySignals;
}

SecondaryDemodulationSignals makeEmptySecondaryDemodulationSignals()
{
  SecondaryDemodulationSignals secondarySignals;
  secondarySignals.combinedDiffuseInput             = vec4(vec3(0), kEmptyPixelHitDistance);
  secondarySignals.combinedSpecularInput            = vec4(vec3(0), kEmptyPixelHitDistance);
  secondarySignals.indirectRadianceHitDistanceInput = vec4(vec3(0), kEmptyPixelHitDistance);
  secondarySignals.combinedDiffuseOutput            = vec4(vec3(0), kEmptyPixelHitDistance);
  secondarySignals.combinedSpecularOutput           = vec4(vec3(0), kEmptyPixelHitDistance);
  return secondarySignals;
}

void outputToDebugViewAtTheEnd(
  uint2 pixelCoordinate,
  PrimaryDemodulationSignals primarySignals,
  SecondaryDemodulationSignals secondarySignals)
{
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(pixelCoordinate, SharedFlags);
  const bool performedAnyPSR = geometryFlags.performedAnyPSR();
  // Matches the gate inside demodulateSecondaryPath that actually populates the secondary radiance signals.
  const bool secondaryRadianceValid = performedAnyPSR
    && geometryFlags.secondarySurfaceMask
    && !geometryFlags.primarySelectedIntegrationSurface;

  const vec4 indirectRadianceHitDistanceInput     = secondarySignals.indirectRadianceHitDistanceInput;
  const vec4 primaryDirectDiffuseInput            = primarySignals.directDiffuseInput;
  const vec4 primaryDirectSpecularInput           = primarySignals.directSpecularInput;
  const vec4 primaryIndirectDiffuseInput          = primarySignals.indirectDiffuseInput;
  const vec4 primaryIndirectSpecularInput         = primarySignals.indirectSpecularInput;
  const vec4 secondaryCombinedDiffuseInput        = secondarySignals.combinedDiffuseInput;
  const vec4 secondaryCombinedSpecularInput       = secondarySignals.combinedSpecularInput;
  const vec4 primaryDirectDiffuseOutput           = primarySignals.directDiffuseOutput;
  const vec4 primaryDirectSpecularOutput          = primarySignals.directSpecularOutput;
  const vec4 primaryIndirectDiffuseOutput         = primarySignals.indirectDiffuseOutput;
  const vec4 primaryIndirectSpecularOutput        = primarySignals.indirectSpecularOutput;
  const vec4 secondaryCombinedDiffuseOutput       = secondarySignals.combinedDiffuseOutput;
  const vec4 secondaryCombinedSpecularOutput      = secondarySignals.combinedSpecularOutput;

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
    storeInDebugView(pixelCoordinate, vec4(
      primaryIndirectDiffuseConsistencyCheckFailure ? 1.0f : 0.0f,
      primaryIndirectSpecularConsistencyCheckFailure ? 1.0f : 0.0f,
      (secondaryCombinedDiffuseConsistencyCheckFailure || secondaryCombinedSpecularConsistencyCheckFailure) ? 1.0f : 0.0f,
      1.0f
    ));

    break;
  case DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO:
    storeInDebugView(pixelCoordinate, aliasedData0.PrimarySpecularAlbedo[pixelCoordinate].xyz);
    break;
  case DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO:
    if (performedAnyPSR)
    {
      storeInDebugView(pixelCoordinate, aliasedData1.SecondarySpecularAlbedo[pixelCoordinate].xyz);
    }
    break;
  // Primary Direct Data
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryDirectDiffuseInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryDirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    storeInDebugView(pixelCoordinate, primaryDirectDiffuseInput.w);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_HIT_T:
    storeInDebugView(pixelCoordinate, primaryDirectSpecularInput.w);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryDirectDiffuseOutput.xyz);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryDirectSpecularOutput.xyz);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    storeInDebugView(pixelCoordinate, primaryDirectDiffuseOutput.w);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_SPECULAR_HIT_T:
    storeInDebugView(pixelCoordinate, primaryDirectSpecularOutput.w);
    break;
  // Primary Indirect Data
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryIndirectDiffuseInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    storeInDebugView(pixelCoordinate, primaryIndirectDiffuseInput.w);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    storeInDebugView(pixelCoordinate, primaryIndirectSpecularInput.w);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryIndirectDiffuseOutput.xyz);
    break;
  case DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    storeInDebugView(pixelCoordinate, primaryIndirectSpecularOutput.xyz);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    storeInDebugView(pixelCoordinate, primaryIndirectDiffuseOutput.w);
    break;
  case DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    storeInDebugView(pixelCoordinate, primaryIndirectSpecularOutput.w);
    break;
  case DEBUG_VIEW_NOISY_PATHRACED_RAW_INDIRECT_RADIANCE:
    storeInDebugView(pixelCoordinate, indirectRadianceHitDistanceInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate, indirectRadianceHitDistanceInput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_DIFFUSE_RADIANCE:
    if (secondaryRadianceValid
      && !geometryFlags.firstSampledLobeIsSpecular)
    {
      storeInDebugView(pixelCoordinate, indirectRadianceHitDistanceInput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_SPECULAR_RADIANCE:
    if (secondaryRadianceValid
      && geometryFlags.firstSampledLobeIsSpecular)
    {
      storeInDebugView(pixelCoordinate, indirectRadianceHitDistanceInput.xyz);
    }
    break;    
  case DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate, secondaryCombinedDiffuseInput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate, secondaryCombinedSpecularInput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate, secondaryCombinedDiffuseOutput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate, secondaryCombinedSpecularOutput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_DIRECT_RADIANCE:
    storeInDebugView(pixelCoordinate, 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_RADIANCE:
    storeInDebugView(pixelCoordinate, 
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_PRIMARY_RADIANCE:
    storeInDebugView(pixelCoordinate, 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz +
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz);
    break;
  case DEBUG_VIEW_NOISY_SECONDARY_RADIANCE:
    if (secondaryRadianceValid)
    {
      storeInDebugView(pixelCoordinate,
        secondaryCombinedDiffuseInput.xyz + secondaryCombinedSpecularInput.xyz);
    }
    break;
  case DEBUG_VIEW_NOISY_RADIANCE:
    storeInDebugView(pixelCoordinate, 
      primaryDirectDiffuseInput.xyz + primaryDirectSpecularInput.xyz +
      primaryIndirectDiffuseInput.xyz + primaryIndirectSpecularInput.xyz +
      secondaryCombinedDiffuseInput.xyz + secondaryCombinedSpecularInput.xyz);
    break;

  case DEBUG_VIEW_NAN:
  {
    bool isValid = true;
    
    // DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO
    isValid &= isValidValue(aliasedData0.PrimarySpecularAlbedo[pixelCoordinate].xyz);
    // DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO
    if (performedAnyPSR)
    {
      isValid &= isValidValue(aliasedData1.SecondarySpecularAlbedo[pixelCoordinate].xyz);
    }
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
    // DEBUG_VIEW_NOISY_PATHRACED_RAW_INDIRECT_RADIANCE
    // DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_RADIANCE
    // DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_DIFFUSE_RADIANCE
    // DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_SPECULAR_RADIANCE
    if (secondaryRadianceValid)
    {
      isValid &= isValidValue(indirectRadianceHitDistanceInput);
      // DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE
      isValid &= isValidValue(secondaryCombinedDiffuseInput.xyz);
      // DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE
      isValid &= isValidValue(secondaryCombinedSpecularInput.xyz);
      // DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE
      isValid &= isValidValue(secondaryCombinedDiffuseOutput.xyz);
      // DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE
      isValid &= isValidValue(secondaryCombinedSpecularOutput.xyz);
    }

    // Skipped - since these represent combined values from above
    // DEBUG_VIEW_NOISY_PRIMARY_DIRECT_RADIANCE
    // DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_RADIANCE
    // DEBUG_VIEW_NOISY_PRIMARY_RADIANCE
    // DEBUG_VIEW_NOISY_SECONDARY_RADIANCE
    // DEBUG_VIEW_NOISY_RADIANCE
    
    accumulateInDebugViewAnd(pixelCoordinate, isValid);
  }
  }
}

// Demodulates the primary surface direct and indirect signals and writes them to the per-signal
// denoiser input buffers. Loads primary GBuffer data, computes and stores the primary specular
// albedo (re-read for the debug view), and runs the per-lobe demodulation. The helper internally
// gates on isPixelActive — the direct/indirect integrator does not produce valid inputs for
// sparse-inactive pixels, so it returns empty signals on those.
PrimaryDemodulationSignals demodulatePrimaryPath(
  uvec2 pixelCoordinate,
  uvec2 localIndex,
  GeometryFlags geometryFlags,
  CameraDirections primaryRayDirections,
  bool isPixelActive,
  bool isDirectActive,
  bool isIndirectActive)
{
  PrimaryDemodulationSignals primarySignals = makeEmptyPrimaryDemodulationSignals();

  // Note: Decode from unorm encoded representation.
  const vec3 primaryVirtualWorldNormal = PrimaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].xyz * 2.0f - 1.0f;
  const float primaryPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;
  const float primaryLinearViewZ = PrimaryLinearViewZ[pixelCoordinate].x;
  const bool isPrimaryMiss = primaryLinearViewZ == cb.primaryDirectMissLinearViewZ;

  vec3 primarySpecularAlbedo = vec3(0);
  // Compute specular albedo only for primary-hit pixels that need it: every active pixel, plus all
  // primary-hit pixels in dense mode. Sparse-inactive pixels stay zero.
  const bool needSpecularAlbedo = isPixelActive || !cb.sparseRenderingArgs.enableSparsePrimarySpecularAlbedo;
  if (needSpecularAlbedo && !isPrimaryMiss)
  {
    const vec3 primaryBaseReflectivity = aliasedData0.PrimaryBaseReflectivity[pixelCoordinate].xyz;
    // Note: Virtual world normal used so direction does not need to be read from the GBuffer; the camera direction
    // can be re-derived since virtual normals are oriented with respect to the camera.
    const float primaryNormalDotViewDirection = dot(primaryVirtualWorldNormal, -primaryRayDirections.worldDirection);
    primarySpecularAlbedo = evalSpecularAlbedoGGXSchlick(
      primaryBaseReflectivity, calcRoughness(primaryPerceptualRoughness), primaryNormalDotViewDirection);
  }

  // Note: Overwrites the base reflectivity in this buffer; composite remodulates with this value.
  aliasedData0.PrimarySpecularAlbedo[pixelCoordinate].xyz = primarySpecularAlbedo;

  if (!isPixelActive)
  {
    return primarySignals;
  }

  const vec3 primaryDiffuseAlbedo = PrimaryAlbedo[pixelCoordinate].xyz;

  DemodulationData primaryDemodulationData;
  primaryDemodulationData.diffuseAlbedo = primaryDiffuseAlbedo;
  primaryDemodulationData.specularAlbedo = primarySpecularAlbedo;
  primaryDemodulationData.perceptualRoughness = primaryPerceptualRoughness;
  primaryDemodulationData.linearViewZ = primaryLinearViewZ;

  // If rtxdi and restir gi are enabled, the primary surface input is dense and demodulation can always be enabled.
  const bool primaryInputValid =
    (cb.enableRtxdi && cb.enableReSTIRGI) || geometryFlags.primarySelectedIntegrationSurface;
  const bool demodulatePrimaryDirectInput = primaryInputValid && isDirectActive;
  const bool demodulatePrimaryIndirectInput = primaryInputValid && isIndirectActive;

  if (demodulatePrimaryDirectInput)
  {
    primarySignals.directDiffuseInput = PrimaryDirectDiffuseRadiance[pixelCoordinate];
    primarySignals.directSpecularInput = PrimaryDirectSpecularRadiance[pixelCoordinate];
  }

  if (demodulatePrimaryIndirectInput)
  {
    primarySignals.indirectDiffuseInput = PrimaryIndirectDiffuseRadiance[pixelCoordinate];
    primarySignals.indirectSpecularInput = PrimaryIndirectSpecularRadiance[pixelCoordinate];
  }

  // Pre-scale per-lobe radiance by the inverse of its own sparse-rendering sampling rate so
  // every downstream stage (boiling filter, NRD packing, combined-mode merge, denoiser) sees an
  // unbiased estimate without needing further scale logic. HitT stays unscaled.
  if (cb.sparseRenderingArgs.mode != SparseRenderingMode::Off)
  {
    const float directRadianceScale = calculateRadianceScaleFromSamplingRate(SparseRenderingDirectPixelSamplingRate[pixelCoordinate]);
    const float indirectRadianceScale = calculateRadianceScaleFromSamplingRate(SparseRenderingIndirectPixelSamplingRate[pixelCoordinate]);
    primarySignals.directDiffuseInput.xyz *= directRadianceScale;
    primarySignals.directSpecularInput.xyz *= directRadianceScale;
    primarySignals.indirectDiffuseInput.xyz *= indirectRadianceScale;
    primarySignals.indirectSpecularInput.xyz *= indirectRadianceScale;
  }

  // Overwrite the direct specular HitT with the indirect specular HitT. The denoiser uses HitT
  // mostly for calculating motion vectors for reflections; the indirect specular HitT is always
  // from rays traced in the specular lobe, while the direct HitT often reflects the distance
  // to a NEE-selected light that may not be in the specular lobe.
  if (cb.enableReplaceDirectSpecularHitTWithIndirectSpecularHitT && cb.enableSecondaryBounces && demodulatePrimaryIndirectInput)
  {
    primarySignals.directSpecularInput.a = primarySignals.indirectSpecularInput.a;
  }

  // Luminance-weighted mix of direct and indirect HitT for the diffuse channel — sharper shadows
  // without losing too much spatial detail. Only relevant when separated denoisers are off.
  if (!cb.enableSeparatedDenoisers && demodulatePrimaryIndirectInput)
  {
    const float primaryDirectDiffuseLuminance = calcBt709Luminance(primarySignals.directDiffuseInput.rgb);
    const float primaryIndirectDiffuseLuminance = calcBt709Luminance(primarySignals.indirectDiffuseInput.rgb);

    float directHitTContribution =
      primaryDirectDiffuseLuminance /
      max(primaryDirectDiffuseLuminance + primaryIndirectDiffuseLuminance, 0.001);
    directHitTContribution = clamp(directHitTContribution, 0.0, cb.primaryDirectNrd.maxDirectHitTContribution);

    primarySignals.directDiffuseInput.a = mix(primarySignals.indirectDiffuseInput.a, primarySignals.directDiffuseInput.a, directHitTContribution);
  }

  if (demodulatePrimaryDirectInput)
  {
    demodulate(
      cb.primaryDirectNrd, primaryDemodulationData,
      primarySignals.directDiffuseInput, primarySignals.directSpecularInput,
      primarySignals.directDiffuseOutput, primarySignals.directSpecularOutput, cb.enableDemodulateRoughness);

    if (cb.enableDirectLightBoilingFilter)
    {
      applyDirectLightBoilingFilter(localIndex, primaryLinearViewZ, primaryVirtualWorldNormal,
        primarySignals.directDiffuseOutput.xyz, primarySignals.directSpecularOutput.xyz);
    }
  }

  if (demodulatePrimaryIndirectInput)
  {
    demodulate(
      cb.primaryIndirectNrd, primaryDemodulationData,
      primarySignals.indirectDiffuseInput, primarySignals.indirectSpecularInput,
      primarySignals.indirectDiffuseOutput, primarySignals.indirectSpecularOutput, cb.enableDemodulateRoughness);
  }

  // If separate denoisers are disabled, combine direct and indirect radiance signals here.
  // Composite gates primary direct/indirect reads with the per-signal active masks, so skip the
  // writes for inactive pixels — they would never be read.
  if (primaryInputValid && !cb.enableSeparatedDenoisers)
  {
    primarySignals.directDiffuseOutput.xyz += primarySignals.indirectDiffuseOutput.xyz;
    primarySignals.directSpecularOutput.xyz += primarySignals.indirectSpecularOutput.xyz;

    PrimaryDirectDiffuseRadiance[pixelCoordinate] = primarySignals.directDiffuseOutput;
    PrimaryDirectSpecularRadiance[pixelCoordinate] = primarySignals.directSpecularOutput;
  }

  // Composite gates primary direct/indirect reads with the per-signal active masks, so skip the
  // writes for inactive pixels — they would never be read.
    if (isDirectActive)
    {
      PrimaryDirectDiffuseRadiance[pixelCoordinate] = primarySignals.directDiffuseOutput;
      PrimaryDirectSpecularRadiance[pixelCoordinate] = primarySignals.directSpecularOutput;
    }
    if (cb.enableSeparatedDenoisers && isIndirectActive)
    {
      PrimaryIndirectDiffuseRadiance[pixelCoordinate] = primarySignals.indirectDiffuseOutput;
      PrimaryIndirectSpecularRadiance[pixelCoordinate] = primarySignals.indirectSpecularOutput;
    }

  return primarySignals;
}

// Demodulates the secondary (PSR) surface signal and writes it to the secondary denoiser input
// buffers. Should only be called on PSR pixels (geometryFlags.performedAnyPSR()). Runs regardless of
// sparse active state — the integrator produces secondary radiance for all PSR pixels, so the
// output stays dense and the RR particle layer matches the SR-off baseline.
// Loads secondary GBuffer data, computes and stores the secondary specular albedo (returned for
// the dense debug view), and runs the lobe-combined demodulation.
SecondaryDemodulationSignals demodulateSecondaryPath(
  uvec2 pixelCoordinate,
  GeometryFlags geometryFlags,
  CameraDirections primaryRayDirections)
{
  SecondaryDemodulationSignals secondarySignals = makeEmptySecondaryDemodulationSignals();

  // Only PSR pixels carry valid secondary GBuffer data and produce secondary radiance.
  if (!geometryFlags.performedAnyPSR())
  {
    return secondarySignals;
  }

  const vec3 secondaryBaseReflectivity = aliasedData1.SecondaryBaseReflectivity[pixelCoordinate].xyz;
  // Note: Decode from unorm encoded representation.
  const vec3 secondaryVirtualWorldNormal = SecondaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].xyz * 2.0f - 1.0f;
  const float secondaryPerceptualRoughness = SecondaryVirtualWorldNormalPerceptualRoughness[pixelCoordinate].w;
  // Note: Virtual world normal used so direction does not need to be read from the GBuffer; the camera direction
  // can be re-derived since virtual normals are oriented with respect to the camera.
  const float secondaryNormalDotViewDirection = dot(secondaryVirtualWorldNormal, -primaryRayDirections.worldDirection);
  const vec3 secondarySpecularAlbedo = evalSpecularAlbedoGGXSchlick(
    secondaryBaseReflectivity, calcRoughness(secondaryPerceptualRoughness), secondaryNormalDotViewDirection);
  // Note: Overwrites the base reflectivity in this buffer; composite remodulates with this value.
  aliasedData1.SecondarySpecularAlbedo[pixelCoordinate].xyz = secondarySpecularAlbedo;

  const vec3 secondaryDiffuseAlbedo = SecondaryAlbedo[pixelCoordinate].xyz;

  if (geometryFlags.secondarySurfaceMask && !geometryFlags.primarySelectedIntegrationSurface)
  {
    DemodulationData secondaryDemodulationData;
    secondaryDemodulationData.diffuseAlbedo = secondaryDiffuseAlbedo;
    secondaryDemodulationData.specularAlbedo = secondarySpecularAlbedo;
    secondaryDemodulationData.perceptualRoughness = secondaryPerceptualRoughness;
    secondaryDemodulationData.linearViewZ = SecondaryLinearViewZ[pixelCoordinate].x;

    secondarySignals.indirectRadianceHitDistanceInput = IndirectRadianceHitDistance[pixelCoordinate];
    secondarySignals.combinedDiffuseInput = SecondaryCombinedDiffuseRadiance[pixelCoordinate];
    secondarySignals.combinedSpecularInput = SecondaryCombinedSpecularRadiance[pixelCoordinate];

    // For secondary surfaces combine direct and indirect radiance for the current lobe.
    if (geometryFlags.firstSampledLobeIsSpecular)
    {
      secondarySignals.combinedSpecularInput = sanitizeRadianceHitDistance(
        secondarySignals.combinedSpecularInput.xyz + secondarySignals.indirectRadianceHitDistanceInput.xyz, secondarySignals.indirectRadianceHitDistanceInput.w);
    }
    else
    {
      secondarySignals.combinedDiffuseInput = sanitizeRadianceHitDistance(
        secondarySignals.combinedDiffuseInput.xyz + secondarySignals.indirectRadianceHitDistanceInput.xyz, secondarySignals.indirectRadianceHitDistanceInput.w);
    }

    // The secondary signal doesn't have advanced techniques like ReSTIR; a tighter clamp removes fireflies.
    secondarySignals.combinedSpecularInput.xyz = fireflyFiltering(secondarySignals.combinedSpecularInput.xyz, cb.secondarySpecularFireflyFilteringThreshold);

    demodulate(
      cb.secondaryCombinedNrd, secondaryDemodulationData,
      secondarySignals.combinedDiffuseInput, secondarySignals.combinedSpecularInput,
      secondarySignals.combinedDiffuseOutput, secondarySignals.combinedSpecularOutput, false);
  }

  // ToDo: scale secondaries  prior to being denoised by NRD when sparse secondary lighting gets added.
  SecondaryCombinedDiffuseRadiance[pixelCoordinate] = secondarySignals.combinedDiffuseOutput;
  SecondaryCombinedSpecularRadiance[pixelCoordinate] = secondarySignals.combinedSpecularOutput;

  return secondarySignals;
}

void outputToDebugViewAtTheStart(
  uint2 pixelCoordinate,
  GeometryFlags geometryFlags)
{
  // The base reflectivity aliases hold the post-integrate adjusted value at this point; they get
  // overwritten by Primary/Secondary SpecularAlbedo later in this pass, so the views must be
  // emitted here, before demodulatePrimary/SecondaryPath run.
  const bool primaryHit = PrimaryLinearViewZ[pixelCoordinate].x != cb.primaryDirectMissLinearViewZ;

  switch (cb.debugView)
  {
  default:
    break;
  case DEBUG_VIEW_PRIMARY_BASE_REFLECTIVITY_ADJUSTED:
    if (primaryHit)
    {
      storeInDebugView(pixelCoordinate, aliasedData0.PrimaryBaseReflectivity[pixelCoordinate].xyz);
    }
    break;
  case DEBUG_VIEW_SECONDARY_BASE_REFLECTIVITY_ADJUSTED:
    if (geometryFlags.performedAnyPSR())
    {
      storeInDebugView(pixelCoordinate, aliasedData1.SecondaryBaseReflectivity[pixelCoordinate].xyz);
    }
    break;
  case DEBUG_VIEW_NAN:
  {
    bool isValid = true;
    if (primaryHit)
    {
      isValid &= isValidValue(aliasedData0.PrimaryBaseReflectivity[pixelCoordinate].xyz);
    }
    if (geometryFlags.performedAnyPSR())
    {
      isValid &= isValidValue(aliasedData1.SecondaryBaseReflectivity[pixelCoordinate].xyz);
    }
    accumulateInDebugViewAnd(pixelCoordinate, isValid);
    break;
  }
  }
}

[shader("compute")]
[numthreads(DEMODULATE_THREAD_GROUP_WIDTH, DEMODULATE_THREAD_GROUP_HEIGHT, 1)]
void main(uint2 threadIndex : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  if (any(threadIndex >= cb.camera.resolution))
  {
    return;
  }

  uvec2 pixelCoordinate = threadIndex;
  bool isDirectActive = true;
  bool isIndirectActive = true;
  if (cb.sparseRenderingArgs.mode != SparseRenderingMode::Off)
  {
    isDirectActive = isActivePixelFromMask(SparseRenderingDirectActivePixelMask, pixelCoordinate);
    isIndirectActive = isActivePixelFromMask(SparseRenderingIndirectActivePixelMask, pixelCoordinate);
  }
  const bool isPixelActive = isDirectActive || isIndirectActive;

  const CameraDirections primaryRayDirections = cameraPixelCoordinateToDirection(cb.camera, pixelCoordinate);
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(pixelCoordinate, SharedFlags);

  // Emit debug views that read from resources soon to be overwritten by this pass.
  outputToDebugViewAtTheStart(pixelCoordinate, geometryFlags);

  // Primary surface
  PrimaryDemodulationSignals primarySignals = demodulatePrimaryPath(
    pixelCoordinate, localIndex, geometryFlags, primaryRayDirections,
    isPixelActive, isDirectActive, isIndirectActive);

  // Secondary surface - runs on every PSR pixel regardless of sparse active state so the secondary signal stays dense
  SecondaryDemodulationSignals secondarySignals = demodulateSecondaryPath(
    pixelCoordinate, geometryFlags, primaryRayDirections);

  // Output to debug view
  outputToDebugViewAtTheEnd(pixelCoordinate, primarySignals, secondarySignals);
}
