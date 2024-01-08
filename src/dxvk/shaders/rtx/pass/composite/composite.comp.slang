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
#include "rtx/pass/composite/composite_bindings.slangh"

#include "rtx/pass/composite/composite_args.h"

#include "rtx/utility/common.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/demodulate_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/surface/alpha_blend_surface.slangh"
#include "rtx/external/NRD.slangh"
#include "rtx/utility/geometry_flags.slangh"

#define VOLUME_LIGHTING_NO_NEE
#include "rtx/algorithm/volume_lighting.slangh"

#define D3DFOG_NONE   0
#define D3DFOG_EXP    1
#define D3DFOG_EXP2   2
#define D3DFOG_LINEAR 3

vec4 calculateFog(float viewDistance)
{
  if (cb.fogMode == D3DFOG_NONE)
    return vec4(0.0);

  // Hack: Lets the current fog system look fine on maps with a skybox, to be removed once we read fog parameters
  // properly from the game as they will specify how exactly the fog should look (if its even enabled rather than having
  // it enabled by default).
  if (viewDistance > cb.maxFogDistance)
    return vec4(0.0);

  // https://docs.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas

  float f = 0;

  if (cb.fogMode == D3DFOG_LINEAR)
  {
    // Note: Clamp the view distance to the range of the fog effect.
    // Todo: Add in the min fog distance too potentially as this is currently missing.
    viewDistance = clamp(viewDistance, 0.0f, cb.maxFogDistance);

    f = (cb.fogEnd - viewDistance) * cb.fogScale;
  }
  else if (cb.fogMode == D3DFOG_EXP)
  {
    float d = viewDistance * cb.fogDensity;
    f = exp(-d);
  }
  else if (cb.fogMode == D3DFOG_EXP2)
  {
    float d = viewDistance * cb.fogDensity;
    f = exp(-d * d);
  }

  f = clamp(f, 0.0, 1.0);

  return vec4(cb.fogColor, 1.0 - f);
}

void unpackDenoiserRadiance(inout float3 radiance, uint denoiserMode)
{
  switch (denoiserMode)
  {
    case DENOISER_MODE_RELAX:
      radiance = RELAX_BackEnd_UnpackRadiance(float4(radiance, 0)).xyz;
      break;
    case DENOISER_MODE_REBLUR:
      radiance = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(float4(radiance, 0)).xyz;
      break;
  }
}

vec3 applyPostFilter(Texture2D<float4> texture, uint2 pixel, uint denoiserMode)
{
  vec3 centerColor = texture[pixel].xyz;
  unpackDenoiserRadiance(centerColor, denoiserMode);
  vec3 totalColor = vec3(0);
  uint count = 0;
  for (int i = -1; i <= 1; ++i)
  {
    for (int j = -1; j <= 1; ++j)
    {
      if (i != 0 && j != 0)
      {
        vec3 color = texture[pixel + int2(i, j)].xyz;
        unpackDenoiserRadiance(color, denoiserMode);
        count += any(color > vec3(0)) ? 1 : 0;
        totalColor += color;
      }
    }
  }

  vec3 avgColor = totalColor / max(1, count);
  if (calcBt709Luminance(centerColor) > calcBt709Luminance(avgColor) * cb.postFilterThreshold)
  {
    centerColor = avgColor;
  }
  return centerColor;
}

void updateDebugViewAtStart(uint2 thread_id) 
{
  // Output Debug Information
  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    if (cb.enableSeparatedDenoisers)
      storeInDebugView(ivec2(thread_id), PrimaryIndirectDiffuseRadianceHitDistance[thread_id]);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    if (cb.enableSeparatedDenoisers)
      storeInDebugView(ivec2(thread_id), PrimaryIndirectSpecularRadianceHitDistance[thread_id]);
    break;
  }
}

void updateAlphaBlendDebugView(uint2 thread_id, AlphaBlendSurface surface)
{
  // Output Debug Information
  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_COLOR:
    storeInDebugView(ivec2(thread_id), surface.isValid() ? surface.color.rgb : f16vec3(1,0,1));
    break;
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_NORMAL:
    storeInDebugView(ivec2(thread_id), surface.isValid() ? surface.normal : f16vec3(0));
    break;
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_GEOMETRY_HASH:
    storeInDebugView(ivec2(thread_id), r5g6b5ToColor(surface.geometryHash));
    break;    
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_BACKGROUND_TRANSPARENCY:
    storeInDebugView(ivec2(thread_id), surface.backgroundTransparency);
    break;
  }
}

void postCompositeDebugView(uint2 thread_id, GeometryFlags geometryFlags)
{
  // Output Debug Information
  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_GEOMETRY_FLAGS_FIRST_SAMPLED_LOBE_IS_SPECULAR:
    storeInDebugView(ivec2(thread_id), geometryFlags.firstSampledLobeIsSpecular);
    break;
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 thread_id : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  const VolumeArgs volumeArgs = cb.volumeArgs;
  // Note: Fake camera constructed due to Composite pass's lack of access to typical constant buffer members. A bit hacky
  // and may result in issues in the future if the Camera API is updated to use new members internally (not super likely)
  // but avoids code duplication.
  Camera fakeCamera = { 0 };

  fakeCamera.resolution = cb.resolution;
  fakeCamera.projectionToViewJittered = cb.projectionToViewJittered;
  fakeCamera.viewToWorld = cb.viewToWorld;
  fakeCamera.nearPlane = cb.nearPlane;

  // Early out for pixels outside the camera

  if (any(thread_id >= fakeCamera.resolution))
  {
    return;
  }

  updateDebugViewAtStart(thread_id);

  // Load GBuffer Information

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(thread_id, SharedFlags);
  const vec3 sharedRadiance = vec3(
    texelFetch(SharedRadianceRG, thread_id.xy, 0).xy,
    texelFetch(SharedRadianceB, thread_id.xy, 0).x);

  const vec3 primaryAttenuation = r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy));
  const vec3 primaryAlbedo = texelFetch(PrimaryAlbedo, thread_id.xy, 0).xyz;
  const vec3 primarySpecularAlbedo = texelFetch(PrimarySpecularAlbedo, thread_id.xy, 0).xyz;
  const float primaryLinearViewZ = texelFetch(PrimaryLinearViewZ, thread_id.xy, 0).x;
  const vec4 primaryVirtualWorldNormalPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[thread_id.xy];
  const vec3 primaryVirtualNormal = primaryVirtualWorldNormalPerceptualRoughness.xyz * 2.0 - 1.0;
  const float perceptualRoughness = primaryVirtualWorldNormalPerceptualRoughness.w;
  // Note: Relying on the same sentinel value outputted to the linear view Z which NRD relies on, so this is safe (and lets us not have to read
  // in the cone radius texture which typically contains the flag we use to determine this).
  const bool primaryMiss = primaryLinearViewZ == cb.primaryDirectMissLinearViewZ;

  const vec3 secondaryAttenuation = r11g11b10ToColor(imageLoad(SecondaryAttenuation, thread_id.xy));
  const vec3 secondaryAlbedo = texelFetch(SecondaryAlbedo, thread_id.xy, 0).xyz;
  const vec3 secondarySpecularAlbedo = texelFetch(SecondarySpecularAlbedo, thread_id.xy, 0).xyz;

  // Reconstruct the primary ray direction and view hit distance for the current pixel

  const CameraDirections primaryRayDirections = cameraPixelCoordinateToDirection(fakeCamera, thread_id.xy);
  const vec3 viewPosition = cameraReconstructViewPosition(fakeCamera, primaryRayDirections.viewDirection, primaryLinearViewZ);
  // Note: Should be equal to the virtual hit distance just without needing to read it (since we need the linear view Z in world space for
  // other calculations here anyways and can just use it to reconstruct this).
  const float viewDistance = length(viewPosition);

  // Load Input Radiance

  vec3 primaryDirectDiffuseRadiance = texelFetch(PrimaryDirectDiffuseRadianceHitDistance, thread_id.xy, 0).xyz;
  vec3 primaryDirectSpecularRadiance = texelFetch(PrimaryDirectSpecularRadianceHitDistance, thread_id.xy, 0).xyz;

  unpackDenoiserRadiance(primaryDirectDiffuseRadiance, cb.primaryDirectDenoiser);
  unpackDenoiserRadiance(primaryDirectSpecularRadiance, cb.primaryDirectDenoiser);

  vec3 secondaryCombinedDiffuseRadiance = vec3(0);
  vec3 secondaryCombinedSpecularRadiance = vec3(0);

  if (geometryFlags.secondarySurfaceMask)
  {
    secondaryCombinedDiffuseRadiance = texelFetch(SecondaryCombinedDiffuseRadianceHitDistance, thread_id.xy, 0).xyz;
    secondaryCombinedSpecularRadiance = texelFetch(SecondaryCombinedSpecularRadianceHitDistance, thread_id.xy, 0).xyz;

    unpackDenoiserRadiance(secondaryCombinedDiffuseRadiance, cb.secondaryCombinedDenoiser);
    unpackDenoiserRadiance(secondaryCombinedSpecularRadiance, cb.secondaryCombinedDenoiser);
  }

  vec3 primaryIndirectDiffuseRadiance = vec3(0);
  vec3 primaryIndirectSpecularRadiance = vec3(0);

  if (cb.enableSeparatedDenoisers)
  {
    if (cb.usePostFilter)
    {
      primaryIndirectDiffuseRadiance = applyPostFilter(PrimaryIndirectDiffuseRadianceHitDistance, thread_id.xy, cb.primaryIndirectDenoiser);
      primaryIndirectSpecularRadiance = applyPostFilter(PrimaryIndirectSpecularRadianceHitDistance, thread_id.xy, cb.primaryIndirectDenoiser);
    }
    else
    {
      primaryIndirectDiffuseRadiance = texelFetch(PrimaryIndirectDiffuseRadianceHitDistance, thread_id.xy, 0).xyz;
      primaryIndirectSpecularRadiance = texelFetch(PrimaryIndirectSpecularRadianceHitDistance, thread_id.xy, 0).xyz;

      unpackDenoiserRadiance(primaryIndirectDiffuseRadiance, cb.primaryIndirectDenoiser);
      unpackDenoiserRadiance(primaryIndirectSpecularRadiance, cb.primaryIndirectDenoiser);
    }
  }

  // Signal enablement overrides
  if (!cb.compositePrimaryDirectDiffuse) primaryDirectDiffuseRadiance = 0;
  if (!cb.compositePrimaryDirectSpecular) primaryDirectSpecularRadiance = 0;
  if (!cb.compositePrimaryIndirectDiffuse) primaryIndirectDiffuseRadiance = 0;
  if (!cb.compositePrimaryIndirectSpecular) primaryIndirectSpecularRadiance = 0;
  if (!cb.compositeSecondaryCombinedDiffuse) secondaryCombinedDiffuseRadiance = 0;
  if (!cb.compositeSecondaryCombinedSpecular) secondaryCombinedSpecularRadiance = 0;

  // Deserialize flags

  // Combine and remodulate demodulated radiance values
  // Note: Apply throughput here to account for any attenuation before the primary hit. Note this throughput is
  // multiplied in here so that it does not interfere with the diffuse/specular radiance signals in denoising and
  // need to be demodulated, only possible because it is a noise-free quantity.

  vec3 primaryCombinedDiffuseRadiance;
  vec3 primaryCombinedSpecularRadiance;

  vec2 bsdfFactor = cb.enableRtxdi ? BSDFFactor[thread_id.xy].xy : vec2(1);
  vec2 bsdfFactor2 = cb.enableReSTIRGI? BSDFFactor2[thread_id.xy].xy : vec2(1);
  if (cb.combineLightingChannels != 0) {
    primaryCombinedDiffuseRadiance = primaryDirectDiffuseRadiance * bsdfFactor.x + primaryIndirectDiffuseRadiance * bsdfFactor2.x;
    primaryCombinedSpecularRadiance = primaryDirectSpecularRadiance * bsdfFactor.y + primaryIndirectSpecularRadiance * bsdfFactor2.y;
  } else {
    bsdfFactor.x = clamp(bsdfFactor.x, 0.7, 1.3); 
    bsdfFactor = 0.5 * (bsdfFactor + bsdfFactor2);
    primaryCombinedDiffuseRadiance = primaryDirectDiffuseRadiance * bsdfFactor.x;
    primaryCombinedSpecularRadiance = primaryDirectSpecularRadiance * bsdfFactor.y;
  }

  vec3 remodulatedTotalPrimaryRadiance = vec3(0.0f, 0.0f, 0.0f);
  vec3 remodulatedTotalSecondaryRadiance = vec3(0.0f, 0.0f, 0.0f);

  {
    float roughnessFactor = 1.0;
    if (cb.demodulateRoughness) {
      roughnessFactor = getRoughnessDemodulationFactor(perceptualRoughness, cb.roughnessDemodulationOffset);
    }

    remodulatedTotalPrimaryRadiance =
      primaryCombinedDiffuseRadiance * primaryAlbedo +
      primaryCombinedSpecularRadiance * primarySpecularAlbedo / roughnessFactor;
    remodulatedTotalPrimaryRadiance *= primaryAttenuation;

    remodulatedTotalPrimaryRadiance = primaryMiss ? cb.clearColorFinalColor : remodulatedTotalPrimaryRadiance;

    float specularWeight = pow(perceptualRoughness, cb.pixelHighlightReuseStrength);
    LastFinalOutput[thread_id.xy] = vec4(primaryCombinedDiffuseRadiance, calcBt709Luminance(primaryCombinedSpecularRadiance) / roughnessFactor * specularWeight);
  }

  if (geometryFlags.secondarySurfaceMask)
  {
    remodulatedTotalSecondaryRadiance =
      secondaryCombinedDiffuseRadiance * secondaryAlbedo +
      secondaryCombinedSpecularRadiance * secondarySpecularAlbedo;
    remodulatedTotalSecondaryRadiance *= secondaryAttenuation;
  }

  // Calculate volumetric radiance and attenuation if volumetric lighting is enabled

  vec3 volumetricPreintegratedRadiance = vec3(0.0f, 0.0f, 0.0f);
  vec3 volumeAttenuation = vec3(1.0f, 1.0f, 1.0f);
  const vec2 screenUV = cameraPixelCoordinateToScreenUV(fakeCamera, thread_id);

  // Note: This is only evaluated when volumetrics are enabled, no external check is needed.
  // Todo: Using Primary linear view Z for now, not the best option as this does not work with PSR (among other effects like
  // Portals and particles), but fine for initial testing.
  evalVolumetricPreintegratedNEE(
    VolumePreintegratedRadiance, volumeArgs,
    screenUV, !primaryMiss, primaryLinearViewZ, viewDistance,
    volumetricPreintegratedRadiance, volumeAttenuation);

  // Calculate final combined radiance output
  // Note: The primary hit accumulated radiance value is not affected by the throughput (rather takes attenuation into account
  // while accumulating already), so it can be added in here without issue.

  vec3 radianceOutput =
    (remodulatedTotalPrimaryRadiance + remodulatedTotalSecondaryRadiance) +
    volumetricPreintegratedRadiance;

  // Apply D3D9-style fog when volumetric lighting is disabled

  vec4 alphaBlendOutput = 0;
  float backgroundAlpha = 1;
  bool isAlphaBlend = false;
  if (cb.enableStochasticAlphaBlend)
  {    
    AlphaBlendSurface surface = AlphaBlendSurface.createFromPacked(AlphaBlendGBuffer[thread_id]);
    vec3 cameraPosition = cameraGetWorldPosition(fakeCamera);
    vec4 centerLight = AlphaBlendRadiance[thread_id];
    bool discardPixel = cb.stochasticAlphaBlendDiscardBlackPixel && all(centerLight.xyz == 0);

    if (!discardPixel && surface.isValid())
    {
      isAlphaBlend = true;
      RNG randomState = createRNG(thread_id, cb.frameIdx, 5);
      surface.color = surface.color;
      vec3 particleNormal = surface.normal;
      backgroundAlpha = surface.backgroundTransparency;

      vec4 totalLight = vec4(centerLight.xyz, 1);

      if(cb.stochasticAlphaBlendEnableFilter)
      {
        float minHitT = surface.hitT;
        float maxHitT = surface.hitT;
        float4 multilayerLight = totalLight;
        for (int i = 0; i < 25; ++i)
        {
          int2 offset = int2(i % 5, i / 5) - 2;
          if (all(offset == 0))
          {
            continue;
          }
          int2 neighborPixel = clamp(thread_id.xy + offset * 2,0,cb.resolution-1);

          AlphaBlendSurface neighborSurface = AlphaBlendSurface.createFromPacked(AlphaBlendGBuffer[neighborPixel]);
          if (neighborSurface.isValid())
          {
            const CameraDirections neighborRayDirections = cameraPixelCoordinateToDirection(fakeCamera, neighborPixel.xy);
            vec3 neighborPosition = cameraPosition + neighborSurface.hitT * neighborRayDirections.worldDirection;

            const float transparencyBandwidth = 0.05;
            const float distanceWeight = 0.5;
            float weight = exp(-abs(neighborSurface.backgroundTransparency - surface.backgroundTransparency) / transparencyBandwidth + -distanceWeight* dot(offset, offset));

            minHitT = min(minHitT, neighborSurface.hitT);
            maxHitT = max(maxHitT, neighborSurface.hitT);

            vec4 neighborLight = AlphaBlendRadiance[neighborPixel];
            multilayerLight += vec4(neighborLight.xyz * weight, weight);
          }
        }
        multilayerLight.rgb /= multilayerLight.w;

        float depthRange = maxHitT - minHitT;
        float depthBandwidth = 0.05;
        totalLight.xyz = lerp(totalLight.xyz, multilayerLight.xyz, saturate(depthRange / surface.hitT / depthBandwidth));
      }
      alphaBlendOutput.xyz = totalLight.xyz;

      if (volumeArgs.enableVolumetricLighting)
      {
        float alphaBlendSurfaceLinearViewZ = surface.hitT * primaryRayDirections.viewDirection.z;
        vec3 alphaBlendPreintegratedRadiance = vec3(0.0f, 0.0f, 0.0f);
        vec3 alphaBlendAttenuation = vec3(1.0f, 1.0f, 1.0f);
        evalVolumetricPreintegratedNEE(
          VolumePreintegratedRadiance, volumeArgs,
          screenUV, true, alphaBlendSurfaceLinearViewZ, surface.hitT,
          alphaBlendPreintegratedRadiance, alphaBlendAttenuation);
        alphaBlendOutput.xyz += alphaBlendPreintegratedRadiance * (1 - backgroundAlpha);
      }

      if (surface.hasEmissive)
      {
        alphaBlendOutput.xyz += sharedRadiance;
      }
      else
      {
        radianceOutput += sharedRadiance;
      }

      if (!volumeArgs.enableVolumetricLighting)
      {
        const vec4 fogColor = calculateFog(surface.hitT);
        alphaBlendOutput.xyz = alphaBlendOutput.xyz * (1.0f - fogColor.a) + fogColor.rgb * fogColor.a;
        alphaBlendOutput.xyz *= (1 - backgroundAlpha);
      }
    }

    updateAlphaBlendDebugView(thread_id.xy, surface);
  }

  if (!isAlphaBlend)
  {
    radianceOutput += sharedRadiance;
  }

  vec3 attenuatedOutput = radianceOutput;
  if (!volumeArgs.enableVolumetricLighting)
  {
    // Todo: Using view distance is a bit of a hack here, the primary emissive contribution will not be fully weighted correctly
    // as emissive hits will be over-attenuated.
    const vec4 fogColor = calculateFog(viewDistance);

    // Blend fog over output
    attenuatedOutput = radianceOutput * (1.0f - fogColor.a) + fogColor.rgb * fogColor.a;
  }

  imageStore(FinalOutput, thread_id.xy, vec4(attenuatedOutput * backgroundAlpha + alphaBlendOutput.rgb, 1.0));

  postCompositeDebugView(thread_id, geometryFlags);
}
