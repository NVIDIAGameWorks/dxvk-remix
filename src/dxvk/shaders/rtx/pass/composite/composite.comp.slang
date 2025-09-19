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

#include "rtx/algorithm/volume_composite_helpers.slangh"
#include "rtx/algorithm/accumulate.slangh"

#define D3DFOG_NONE   0
#define D3DFOG_EXP    1
#define D3DFOG_EXP2   2
#define D3DFOG_LINEAR 3

float3 sampleDomeLightTexture(Sampler2D<float3> DomeLight, float3 worldDirection, float4x4 worldToDomeLightTransform)
{
  float3 domeSampleDirection = mul(worldToDomeLightTransform, float4(worldDirection, 0.0f)).xyz;
  float2 sampleUV = cartesianDirectionToLatLongSphere(domeSampleDirection);
  return DomeLight.SampleLevel(sampleUV, 0).xyz;
}

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
      storeInDebugView(thread_id, PrimaryIndirectDiffuseRadianceHitDistance[thread_id]);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    if (cb.enableSeparatedDenoisers)
      storeInDebugView(thread_id, PrimaryIndirectSpecularRadianceHitDistance[thread_id]);
    break;
  }
}

void updateAlphaBlendDebugView(uint2 thread_id, AlphaBlendSurface surface)
{
  // Output Debug Information
  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_COLOR:
    storeInDebugView(thread_id, surface.isValid() ? surface.color.rgb : f16vec3(1,0,1));
    break;
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_NORMAL:
    storeInDebugView(thread_id, surface.isValid() ? surface.normal : f16vec3(0));
    break;
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_GEOMETRY_HASH:
    storeInDebugView(thread_id, r5g6b5ToColor(surface.geometryHash));
    break;    
  case DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_BACKGROUND_TRANSPARENCY:
    storeInDebugView(thread_id, surface.backgroundTransparency);
    break;
  }
}

void postCompositeDebugView(uint2 thread_id, GeometryFlags geometryFlags, vec4 particleLayer)
{
  // Output Debug Information
  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_GEOMETRY_FLAGS_FIRST_SAMPLED_LOBE_IS_SPECULAR:
    storeInDebugView(thread_id, geometryFlags.firstSampledLobeIsSpecular);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PARTICLE_LAYER:
    storeInDebugView(thread_id, particleLayer);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PARTICLE_LAYER_ALPHA:
    storeInDebugView(thread_id, 1.0 - particleLayer.w);
    break;
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 thread_id : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
  // Override the default volume sampling RNG seed offset
  setVolumeSamplingRngSeed(1);

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

  RNG randomState = createRNG(thread_id, cb.frameIdx, 0);

  // Load GBuffer Information

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(thread_id, SharedFlags);
  const vec3 sharedRadiance = vec3(
    texelFetch(SharedRadianceRG, thread_id.xy, 0).xy,
    texelFetch(SharedRadianceB, thread_id.xy, 0).x);

  const vec3 primaryAttenuation = r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy));
  const vec3 primaryAlbedo = PrimaryAlbedo[thread_id.xy].xyz;
  const vec3 primarySpecularAlbedo = texelFetch(PrimarySpecularAlbedo, thread_id.xy, 0).xyz;
  const float primaryLinearViewZ = texelFetch(PrimaryLinearViewZ, thread_id.xy, 0).x;
  const vec4 primaryVirtualWorldNormalPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[thread_id.xy];
  const vec3 primaryVirtualNormal = primaryVirtualWorldNormalPerceptualRoughness.xyz * 2.0 - 1.0;
  const float perceptualRoughness = primaryVirtualWorldNormalPerceptualRoughness.w;
  // Note: Relying on the same sentinel value outputted to the linear view Z which NRD relies on, so this is safe (and lets us not have to read
  // in the cone radius texture which typically contains the flag we use to determine this).
  const bool primaryMiss = primaryLinearViewZ == cb.primaryDirectMissLinearViewZ
                           && !geometryFlags.performPSTR
                           && !geometryFlags.performPSRR;  // must not be PSR

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

  if (cb.enhanceAlbedo)
  {
    PrimaryAlbedo[thread_id.xy].xyz = primaryAlbedo * sqrt(bsdfFactor2.x);
  }

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
    LastFinalOutput[thread_id.xy] = primaryMiss ? vec4(0) : vec4(primaryIndirectDiffuseRadiance, calcBt709Luminance(primaryIndirectSpecularRadiance) / roughnessFactor * specularWeight);
  }

  if (geometryFlags.secondarySurfaceMask)
  {
    remodulatedTotalSecondaryRadiance =
      secondaryCombinedDiffuseRadiance * secondaryAlbedo +
      secondaryCombinedSpecularRadiance * secondarySpecularAlbedo;
    remodulatedTotalSecondaryRadiance *= secondaryAttenuation;
  }

  vec3 radianceOutput = vec3(0.0f, 0.0f, 0.0f);
  
  // Calculate volumetric radiance and attenuation if volumetric lighting is enabled

  vec3 volumetricPreintegratedRadiance = vec3(0.0f, 0.0f, 0.0f);
  vec3 volumeAttenuation = vec3(1.0f, 1.0f, 1.0f);
  float volumetricIntegrationOffset = 0.f;
  
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
      surface.color = surface.color;
      vec3 particleNormal = surface.normal;
      backgroundAlpha = surface.backgroundTransparency;

      vec4 totalLight = vec4(centerLight.xyz, 1);

      if (cb.stochasticAlphaBlendEnableFilter)
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
            float weight = exp(
              -abs(neighborSurface.backgroundTransparency - surface.backgroundTransparency) / transparencyBandwidth +
              // Note: Convert offset to float to avoid need for integer dot product Vulkan feature to be enabled.
              -distanceWeight * dot(float2(offset), float2(offset))
            );

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

      if (volumeArgs.enable)
      {
        float alphaBlendSurfaceLinearViewZ = surface.hitT * primaryRayDirections.viewDirection.z;
        
        volumetricIntegrationOffset = surface.hitT;

        vec2 volumeUV = cameraPixelCoordinateToScreenUV(fakeCamera, thread_id);
        volumeUV.x /= volumeArgs.numFroxelVolumes;
        integrateVolumetricNEE(
          randomState, VolumeFilteredRadianceAge, VolumeFilteredRadianceY, VolumeFilteredRadianceCoCg,
          volumeArgs, cb.timeSinceStartMS, thread_id, volumeUV, false, cameraGetWorldPosition(fakeCamera), surface.hitT, primaryRayDirections.worldDirection,
          volumetricPreintegratedRadiance, volumeAttenuation);

        alphaBlendOutput.xyz *= volumeAttenuation * (1 - backgroundAlpha);
        alphaBlendOutput.xyz += volumetricPreintegratedRadiance * (1 - backgroundAlpha);
      }
      else
      {
        const vec4 fogColor = calculateFog(surface.hitT);
        alphaBlendOutput.xyz = alphaBlendOutput.xyz * (1.0f - fogColor.a) + fogColor.rgb * fogColor.a;
        alphaBlendOutput.xyz *= (1 - backgroundAlpha);
      }
      
      if (surface.hasEmissive)
      {
        alphaBlendOutput.xyz += sharedRadiance;
      }
    }

    updateAlphaBlendDebugView(thread_id.xy, surface);
  }

  // Note: This is only evaluated when volumetrics are enabled, no external check is needed.
  // Todo: Using Primary linear view Z for now, not the best option as this does not work with PSR (among other effects like
  // Portals and particles), but fine for initial testing.
  
  // Note here we apply an offset to the volume integration, this is so we can reuse the attenuation and radiance from earlier parts of the shader.
  {
    const float3 volumeIntegrationOrigin = cameraGetWorldPosition(fakeCamera) + primaryRayDirections.worldDirection * volumetricIntegrationOffset;
    const float volumeOpaqueIntegrationLength = max(0, viewDistance - volumetricIntegrationOffset);

    vec2 volumeUV = cameraPixelCoordinateToScreenUV(fakeCamera, thread_id);
    volumeUV.x /= volumeArgs.numFroxelVolumes;
    integrateVolumetricNEE(
      randomState, VolumeFilteredRadianceAge, VolumeFilteredRadianceY, VolumeFilteredRadianceCoCg,
      volumeArgs, cb.timeSinceStartMS, thread_id, volumeUV, primaryMiss, volumeIntegrationOrigin, volumeOpaqueIntegrationLength, primaryRayDirections.worldDirection,
      volumetricPreintegratedRadiance, volumeAttenuation);
  }

  // Calculate final combined radiance output
  // Note: The primary hit accumulated radiance value is not affected by the throughput (rather takes attenuation into account
  // while accumulating already), so it can be added in here without issue.
  radianceOutput += remodulatedTotalPrimaryRadiance * volumeAttenuation;
  
  // DLSS-RR cannot handle multiple layers of noisy signals, so a separate layer is needed for secondary signals like glass
  // reflection and particles.
  vec4 particleLayerOutput = 0.0;
  if (cb.outputParticleLayer)
  {
    particleLayerOutput.xyz = RayReconstructionParticleOutput[thread_id].xyz;
  
    if (cb.outputSecondarySignalToParticleLayer)
    {
      // Secondary signal and preintegrated signal have no noise
      if (cb.compositeVolumetricLight)
      {
        particleLayerOutput.xyz += remodulatedTotalSecondaryRadiance * volumeAttenuation;
        radianceOutput += volumetricPreintegratedRadiance;
      }
      else
      {
        particleLayerOutput.xyz += remodulatedTotalSecondaryRadiance * volumeAttenuation;
      }
    }
    else
    {
      radianceOutput += remodulatedTotalSecondaryRadiance * volumeAttenuation;
    }
  }
  else
  {
    radianceOutput += volumetricPreintegratedRadiance;
    radianceOutput += remodulatedTotalSecondaryRadiance * volumeAttenuation;
  }

  // Note: Shared radiance should always be pre-attenuated so no need to factor that in here - it contains multiple layers of signal so each contribution must be attenuated inline.
  radianceOutput += sharedRadiance;
  
  // Add sky radiance
  if (primaryMiss)
  {
    if (cb.domeLightArgs.active)
    {
      radianceOutput += cb.domeLightArgs.radiance * sampleDomeLightTexture(SkyLight, primaryRayDirections.worldDirection, cb.domeLightArgs.worldToLightTransform) * volumeAttenuation;
    }
    else
    {
      vec2 screenUV = cameraPixelCoordinateToScreenUV(cb.camera, thread_id);
      radianceOutput += cb.skyBrightness * SkyLight.SampleLevel(screenUV, 0) * volumeAttenuation;
    }
  }
  
  vec3 attenuatedOutput = radianceOutput;
  if (!volumeArgs.enable)
  {
    // Todo: Using view distance is a bit of a hack here, the primary emissive contribution will not be fully weighted correctly
    // as emissive hits will be over-attenuated.
    const vec4 fogColor = calculateFog(viewDistance);

    // Blend fog over output
    attenuatedOutput = radianceOutput * (1.0f - fogColor.a) + fogColor.rgb * fogColor.a;

    // TODO: Disable fog for particle layer to avoid flickering, need to find a proper way to apply fog to particles
    // particleLayerOutput.rgb = lerp(particleLayerOutput.rgb, fogColor.rgb, fogColor.a);
  }

  vec4 finalOutput = vec4(0,0,0,1);
  if (cb.outputParticleLayer)
  {
    vec4 particleInput = RayReconstructionParticleOutput[thread_id];
    // DLSS-RR needs to do alpha blending using the backgroundAlpha, so the input is untouched here.
    finalOutput.xyz = attenuatedOutput * backgroundAlpha + alphaBlendOutput.xyz;

    // Filter attenuation to avoid abrupt change and improve stability.
    vec3 neighborAttenuation = 0;
    neighborAttenuation += r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy + int2(1,0)));
    neighborAttenuation += r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy + int2(-1,0)));
    neighborAttenuation += r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy + int2(0,1)));
    neighborAttenuation += r11g11b10ToColor(imageLoad(PrimaryAttenuation, thread_id.xy + int2(0,-1)));

    const float neighborWeight = 0.5;
    vec3 blurredAttenuation = (primaryAttenuation + neighborAttenuation * neighborWeight) / (1.0 + neighborWeight * 4.0);

    // Output particles to a separate layer for DLSS-RR.
    // Demodulate attenuation
    if (cb.enableDemodulateAttenuation)
    {
      float primaryAlpha = max(blurredAttenuation.r, max(blurredAttenuation.g, blurredAttenuation.b));
      float minAlpha = 0.3;
      primaryAlpha = max(primaryAlpha, minAlpha);
      finalOutput.xyz /= primaryAlpha;
      particleLayerOutput.w = 1 - primaryAlpha;
    }

    particleLayerOutput = sanitize(particleLayerOutput, 0.0);

    RayReconstructionParticleOutput[thread_id] = particleLayerOutput;
  }
  else
  {
    // Output final composite result for NRD denoiser.
    finalOutput.xyz = attenuatedOutput * backgroundAlpha + alphaBlendOutput.xyz;
  }

  finalOutput = accumulate(thread_id.xy, finalOutput, cb.accumulationArgs, true, AccumulatedFinalOutput);
  
  imageStore(FinalOutput, thread_id.xy, finalOutput);

  postCompositeDebugView(thread_id, geometryFlags, particleLayerOutput);
}
