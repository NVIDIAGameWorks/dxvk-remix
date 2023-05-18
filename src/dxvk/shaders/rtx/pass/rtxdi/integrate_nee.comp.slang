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
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RESTIR_GI_RESERVOIRS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/rtxdi/integrate_nee_bindings.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtxdi/DlssEnhancementFilterFunctions.slangh"
#include "rtx/algorithm/neecache.h"

// #define RESTIR_GI_BOILING_FILTER_MIN_LANE_COUNT 32
// #define RESTIR_GI_BOILING_FILTER_GROUP_SIZE 8

// static const int misDisabledMode = 0;
// static const int misRoughnessMode = 1;
// static const int misParallaxMode = 2;

// groupshared vec2 s_weights[(RESTIR_GI_BOILING_FILTER_GROUP_SIZE * RESTIR_GI_BOILING_FILTER_GROUP_SIZE + RESTIR_GI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RESTIR_GI_BOILING_FILTER_MIN_LANE_COUNT];
// groupshared uint s_count[(RESTIR_GI_BOILING_FILTER_GROUP_SIZE * RESTIR_GI_BOILING_FILTER_GROUP_SIZE + RESTIR_GI_BOILING_FILTER_MIN_LANE_COUNT - 1) / RESTIR_GI_BOILING_FILTER_MIN_LANE_COUNT];

// Parallax MIS measures how good a neighbor sample can contribute to current pixel's specular lobe.
// Suppose the current pixel is borrowing a sample from a neighbor with distance neighborDistance, the sample is
// inside the neighbor's specular lobe. We calculate it's possible input direction to current pixel, corrected
// by parallax. Later we calculate original direction's BRDF value and parallax corrected direction's BRDF value,
// and use their ratio to determine MIS weight.
// f16vec3 getParallaxDirection(vec3 position, vec3 samplePosition, f16vec3 normal, f16vec3 viewDirection)
// {
//   float depth = length(cameraGetWorldPosition(cb.camera) - position);
//   float neighborDistance = depth * cb.reSTIRGIMISParallaxAmount;
//   f16vec3 incidentDirection = normalize(samplePosition - position);
//   f16vec3 bidirection = normalize(cross(normal, incidentDirection));
//   if (dot(bidirection, viewDirection) < float16_t(0))
//   {
//     bidirection *= float16_t(-1);
//   }
//   vec3 testPoint = samplePosition + bidirection * neighborDistance;
//   return normalize(testPoint - position);
// }

void evalNEESecondary(
  //vec3 throughput, 
  //inout vec3 accumulatedRadiance,
  LightSample lightSample,
  float invSelectionPdf,
  uint8_t sampledTransportPortalIndex,
  PortalSpace2BitsType portalSpace,
  uint8_t objectMask,
  bool isViewModelSurface,
  Ray viewRay,
  MinimalRayInteraction minimalRayInteraction,
  MinimalSurfaceInteraction minimalSurfaceInteraction,
  OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction,
  out float16_t specularPortion,
  f16vec2 diffuseSpecularMISWeight,
  out vec3 diffuseLight,
  out vec3 specularLight)
{
  // Setup and trace the visibility ray

  uint8_t rayMask = OBJECT_MASK_OPAQUE | (objectMask & OBJECT_MASK_ALL_DYNAMIC);
  if (cb.enableIndirectTranslucentShadows) rayMask |= OBJECT_MASK_TRANSLUCENT;

  VisibilityResult visibility = traceVisibilityRay(minimalSurfaceInteraction,
    lightSample.position, false, rayMask,
    visibilityModeEnableTranslucentMaterials,
    sampledTransportPortalIndex, portalSpace,
    minimalRayInteraction.coneRadius, viewRay.spreadAngle, 1.0, isViewModelSurface);

  specularPortion = float16_t(0);
  diffuseLight = specularLight = 0;

  if (visibility.hasOpaqueHit)
    return;

  // Evaluate the material and accumulate throughput and hit distance

  const SurfaceMaterialInteractionSplitWeight splitWeight = opaqueSurfaceMaterialInteractionCalcProjectedWeight(
    opaqueSurfaceMaterialInteraction, minimalRayInteraction, visibility.rayDirection);

  const f16vec3 combinedWeight = splitWeight.diffuseReflectionWeight * diffuseSpecularMISWeight.x + splitWeight.specularReflectionWeight * diffuseSpecularMISWeight.y;

  specularPortion = calcBt709Luminance(splitWeight.specularReflectionWeight) / calcBt709Luminance(combinedWeight);

  const float lightSampleThroughput = invSelectionPdf / lightSample.solidAnglePdf;

  // Accumulate Radiance

  const vec3 lightThroughput = /*vec3(combinedWeight) */ lightSampleThroughput * vec3(visibility.attenuation);
  const vec3 attenuatedRadiance = lightSample.radiance * lightThroughput;

  diffuseLight = attenuatedRadiance * splitWeight.diffuseReflectionWeight * diffuseSpecularMISWeight.x;
  specularLight = attenuatedRadiance * splitWeight.specularReflectionWeight * diffuseSpecularMISWeight.y;
  //return attenuatedRadiance;
  //accumulatedRadiance += throughput * attenuatedRadiance;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint2 threadIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
{
  if (any(threadIndex >= cb.camera.resolution))
    return;

  const Camera camera = cb.camera;

  // Note: Re-derive the original primary ray direction.
  const Ray originalPrimaryRay = rayCreatePrimaryFromPixel(camera, threadIndex);

  RNG rng = createRNG(uvec2(threadIndex), cb.frameIdx);

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadIndex, SharedFlags);
  const bool gBufferMiss = isGBufferMiss(threadIndex, PrimaryConeRadius);

  // Early out if the primary surface was a miss or if the indirect integrator was not selected
  // to output indirect lighting information
  if (gBufferMiss)
  {
    return;
  }

  // Deserialize the Ray
  GBufferMemoryMinimalRay gBufferMemoryMinimalRay;
  gBufferMemoryMinimalRay.spreadAngle = float16_t(cb.screenSpacePixelSpreadHalfAngle);
  const MinimalRay minimalRay = minimalRayCreate(gBufferMemoryMinimalRay);
  Ray currentRay;
  currentRay.spreadAngle = minimalRay.spreadAngle;

  Ray primaryRay;
  primaryRay.spreadAngle = minimalRay.spreadAngle;

  // Deserialize the Ray Interaction
  MinimalRayInteraction minimalRayInteraction = minimalRayInteractionReadFromGBuffer(
    originalPrimaryRay, threadIndex, PrimaryViewDirection, PrimaryConeRadius);

  // Deserialize the Surface Interaction
  MinimalSurfaceInteraction minimalSurfaceInteraction = minimalSurfaceInteractionReadFromGBufferWithError(
    threadIndex, PrimaryWorldPositionWorldTriangleNormal, PrimaryPositionError);

  // Deserialize the Surface Material Interaction
  PolymorphicSurfaceMaterialInteraction polymorphicSurfaceMaterialInteraction = polymorphicSurfaceMaterialInteractionReadFromGBuffer(
    threadIndex, PrimaryWorldShadingNormal, PrimaryPerceptualRoughness, PrimaryAlbedo, PrimaryBaseReflectivity, SharedMaterialData0, SharedMaterialData1);

  ReSTIRGI_Reservoir spatialReservoir = RAB_LoadGIReservoir(threadIndex, ReSTIRGI_GetSpatialOutputPage());

  vec4 initialDiffuseOutput = vec4(0);
  vec4 initialSpecularOutput = vec4(0);
  const vec4 indirectRadianceHitDistance = IndirectRadianceHitDistance[threadIndex];
  if (geometryFlags.firstSampledLobeIsSpecular) 
  {
    initialSpecularOutput = indirectRadianceHitDistance;
    initialSpecularOutput.xyz = clamp(initialSpecularOutput.xyz, 0, cb.reSTIRGIFireflyThreshold);
  }
  else
    initialDiffuseOutput = indirectRadianceHitDistance;

  PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = initialDiffuseOutput;
  PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = initialSpecularOutput;

  if (polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) != surfaceMaterialTypeOpaque)
  {
    return;
  }

  OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction = opaqueSurfaceMaterialInteractionCreate(polymorphicSurfaceMaterialInteraction);
  if (cb.enableNeeCache > 0)
  {
    NEECell cell = NEECache.findCell(minimalSurfaceInteraction.position, true);
    if (cell.isValid())// && polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) == surfaceMaterialTypeOpaque)
    {
      int candidateCount = cell.getCandidateCount();
      if (candidateCount > 0)
      {
        float lightObjectPdf = 0;
        NEECandidate candidate = cell.sampleCandidate(getNextSampleBlueNoise(rng), lightObjectPdf);
        int surfaceIndex = candidate.getSurfaceID();
        int primitiveIndex = candidate.getPrimitiveID();
        if (candidate.isValid())
        {
          SURFACE_CREATE_READ(emissiveSurface, surfaceIndex, surfaces);
          RayInteraction emissiveRayInteracton = {};
          vec2 uv = vec2(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng));
          emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(uvToBary(uv));
          //emissiveRayInteracton.barycentricCoordinates = barycentricsToUint(vec2(0.9999999));
          emissiveRayInteracton.primitiveIndex = primitiveIndex;
          //emissiveRayInteracton.viewDirection = -minimalRayInteraction.viewDirection;
          SurfaceInteraction emissiveSurfaceInteraction = surfaceInteractionCreate(emissiveSurface, emissiveRayInteracton, primaryRay);

          const MemoryPolymorphicSurfaceMaterial memoryPolymorphicSurfaceMaterial = surfaceMaterials[emissiveSurface.surfaceMaterialIndex];
          const uint8_t polymorphicType = memoryPolymorphicSurfaceMaterialGetTypeHelper(memoryPolymorphicSurfaceMaterial);

          if (polymorphicType == surfaceMaterialTypeOpaque)
          {
            const OpaqueSurfaceMaterial emissiveOpaqueSurfaceMaterial = opaqueSurfaceMaterialCreate(emissiveSurface.surfaceMaterialIndex, memoryPolymorphicSurfaceMaterial);
            const OpaqueSurfaceMaterialInteraction emissiveOpaqueSurfaceMaterialInteraction =
              opaqueSurfaceMaterialInteractionCreate(emissiveOpaqueSurfaceMaterial, emissiveSurface, emissiveSurfaceInteraction, emissiveRayInteracton);

            if (any(emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance > f16vec3(0)))
            {
              LightSample emissiveLightSample;
              emissiveLightSample.position = emissiveSurfaceInteraction.position;
              emissiveLightSample.normal = emissiveSurfaceInteraction.interpolatedNormal;
              emissiveLightSample.radiance = emissiveOpaqueSurfaceMaterialInteraction.emissiveRadiance;
              float16_t lightPdf = lightObjectPdf * getLightSamplingSolidAnglePDF(
                emissiveSurfaceInteraction.triangleArea, emissiveSurfaceInteraction.position, emissiveSurfaceInteraction.triangleNormal, minimalSurfaceInteraction.position);
              emissiveLightSample.solidAnglePdf = lightPdf;

              RayInteraction rayInteraction = {};
              rayInteraction.coneRadius = minimalRayInteraction.coneRadius;
              rayInteraction.viewDirection = minimalRayInteraction.viewDirection;
              f16vec3 inputDirection = normalize(emissiveSurfaceInteraction.position - minimalSurfaceInteraction.position);
              float16_t diffuseSolidAnglePdf = opaqueSurfaceMaterialInteractionCalcDiffuseReflectionSolidAnglePdf(
                opaqueSurfaceMaterialInteraction, rayInteraction, inputDirection);
              float16_t specularSolidAnglePdf = opaqueSurfaceMaterialInteractionCalcSpecularReflectionSolidAnglePdf(
                opaqueSurfaceMaterialInteraction, rayInteraction, inputDirection);
              f16vec2 misWeight;
              misWeight.x = lightPdf / (lightPdf + diffuseSolidAnglePdf);
              misWeight.y = lightPdf / (lightPdf + specularSolidAnglePdf);

              float16_t specularPortion2;
              const bool isViewModel = false;// pathState.rayMask& OBJECT_MASK_ALL_VIEWMODEL;
              float emissiveInvSelectionPdf = 1.0;// / lightObjectPdf;
              vec3 diffuseLight, specularLight;
              evalNEESecondary(
                emissiveLightSample, emissiveInvSelectionPdf, invalidRayPortalIndex, /*pathState.portalSpace*/PORTAL_SPACE_NONE, 
                OBJECT_MASK_ALL | (geometryFlags.objectMask & OBJECT_MASK_ALL_DYNAMIC) /*pathState.rayMask*/,
                isViewModel, /*ray*/ primaryRay, rayInteraction, /*surfaceInteraction*/minimalSurfaceInteraction,
                opaqueSurfaceMaterialInteraction, specularPortion2, misWeight, diffuseLight, specularLight);


              //pathState.radiance += pathState.throughput * emissiveNEELight;
              //PrimaryDirectDiffuseLobeRadianceHitDistance[threadIndex].xyz += emissiveNEELight;
              //PrimaryDirectSpecularLobeRadianceHitDistance[threadIndex] = 0;// emissiveNEELight;
               PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex].xyz += diffuseLight;
               PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex].xyz += specularLight;
            }
          }
        }
      }
    }
  }

  if (cb.enableReSTIRGI)
  {
    // Create the initial reservoir.
    // We don't store the actual reservoir in the integrator because a) many parameters are constant anyway,
    // b) hit geometry is written into a texture separate from the reservoir buffer to avoid partial writes.
    ReSTIRGI_Reservoir initialSample;
    initialSample.M = 1;
    initialSample.avgWeight = 1;
    initialSample.flagsAndVirtualFraction = 0;
    initialSample.radiance = vec3(0);
    float indirectPathLength = 0;

    // Load the radiance
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      vec4 radianceAndDistance = RestirGIRadiance[threadIndex];
      initialSample.radiance = sanitize(radianceAndDistance.rgb, vec3(0));
      indirectPathLength = radianceAndDistance.w; 
    }

    // The restir algorithm can accept larger fireflies while producing stable result, so set the threshold higher to avoid darkening.
    const float fireflyFilteringFactor = 30;
    initialSample.radiance = fireflyFiltering(initialSample.radiance, cb.fireflyFilteringLuminanceThreshold * fireflyFilteringFactor);

    uint8_t portalID;
    ReSTIRGI_LoadHitGeometry(RestirGIHitGeometry, threadIndex, initialSample.position, initialSample.normal, portalID);
    initialSample.setPortalID(portalID);

    // Transform hit point when the indirect ray crosses a portal
    if (portalID != RESTIR_GI_INVALID_INDIRECT_LIGHT_PORTAL_ID)
    {
      RayPortalHitInfo rayPortalHitInfo = cb.rayPortalHitInfos[(uint)getOpposingRayPortalIndex(portalID)];
      if (rayPortalHitInfoIsActive(rayPortalHitInfo))
      {
        mat4 teleportMatrix = rayPortalHitInfo.encodedPortalToOpposingPortalDirection.unpack();
        initialSample.position = (mul(teleportMatrix, vec4(initialSample.position, 1.0f))).xyz;
        initialSample.normal = mul(mat3(teleportMatrix), initialSample.normal);
      }
    }

    // Calculate position and normal for a virtual sample
    initialSample.setVirtualFraction(0.0);
    if (cb.enableReSTIRGIVirtualSample > 0
      //&& surface.opaqueSurfaceMaterialInteraction.isotropicRoughness > RAB_RESTIR_GI_DELTA_ROUGHNESS
      )
    {
      vec3 direction = initialSample.position - minimalSurfaceInteraction.position;
      if (any(direction != 0) && indirectPathLength > length(direction) * 1.01)
      {
        // Extend virtual fraction length by 20% to prevent occlusion when the sample is used by neighbor pixels
        initialSample.setVirtualFraction((indirectPathLength - length(direction)) * 1.2);
        initialSample.position = minimalSurfaceInteraction.position + normalize(direction) * indirectPathLength;
        initialSample.normal = -normalize(direction);
      }
    }

    // Store complete initial sample
    RAB_StoreGIReservoir(initialSample, threadIndex, ReSTIRGI_GetInitSamplePage());
  }

  // f16vec3 filterColor = f16vec3(1);
  // if (all(minimalSurfaceInteraction.position == spatialReservoir.position))
  // {
  //   filterColor = f16vec3(0);
  // }
  // else if(cb.enableReSTIRGIFinalVisibility || cb.reSTIRGIBiasCorrectionMode == RTXDI_BIAS_CORRECTION_PAIRWISE_RAY_TRACED)
  // {
  //   uint8_t portalID = ReSTIRGI_PortalID2BitTo8Bit(spatialReservoir.getPortalID());
  //   if(portalID == invalidRayPortalIndex || portalID < cb.numActiveRayPortals)
  //   {
  //     uint8_t rayMask = OBJECT_MASK_OPAQUE | (geometryFlags.objectMask & OBJECT_MASK_ALL_DYNAMIC);

  //     vec3 dstPosition = spatialReservoir.getVisibilityPoint(minimalSurfaceInteraction.position);
  //     VisibilityResult visibility = traceVisibilityRay(minimalSurfaceInteraction,
  //       dstPosition, false, rayMask, visibilityModeNone, portalID,
  //       geometryFlags.portalSpace, 0, 0, RAB_RESTIR_GI_VISIBILITY_SHORTENING, geometryFlags.isViewModel);
  //     filterColor = visibility.attenuation;
  //   }
  //   else
  //   {
  //     filterColor = f16vec3(0);
  //   }
  // }

  // vec3 diffuseLight = vec3(0);
  // vec3 specularLight = vec3(0);
  // float initialWeight = 1;
  // float restirWeight = 0;
  // float VdotN = dot(minimalRayInteraction.viewDirection, minimalSurfaceInteraction.triangleNormal);
  // if (polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) == surfaceMaterialTypeOpaque)
  // {
  //   OpaqueSurfaceMaterialInteraction material = opaqueSurfaceMaterialInteractionCreate(polymorphicSurfaceMaterialInteraction);

  //   // Adjust primary base reflectivity based on opacity. For more details, see integrate.slangh, but do note ReSTIR GI needs to read from an un-adjusted
  //   // primary base reflectivity, so it cannot be adjusted in the typical direct integration pass if ReSTIR GI is enabled, and thus has to happen here. When
  //   // ReSTIR GI is disabled this code will not run and thus double-adjustment will not happen either.
  //   const f16vec3 adjustedBaseReflectivity = opaqueSurfaceMaterialGetAdjustedBaseReflectivity(material);
  //   if (geometryFlags.primarySelectedIntegrationSurface)
  //   {
  //     imageStore(PrimaryBaseReflectivity, threadIndex, vec4(adjustedBaseReflectivity, 0.0f));
  //   }

  //   // Calulate MIS weight
  //   if (any(filterColor > f16vec3(0)))
  //   {
  //     vec3 L = normalize(spatialReservoir.position - minimalSurfaceInteraction.position);
  //     SurfaceMaterialInteractionSplitWeight brdf = opaqueSurfaceMaterialInteractionCalcProjectedWeight(material, minimalRayInteraction, L);

  //     diffuseLight  = spatialReservoir.radiance * brdf.diffuseReflectionWeight  * spatialReservoir.avgWeight;
  //     specularLight = spatialReservoir.radiance * brdf.specularReflectionWeight * spatialReservoir.avgWeight;

  //     // Calculate MIS weight
  //     if (geometryFlags.primarySelectedIntegrationSurface && cb.reSTIRGIMISMode != misDisabledMode)
  //     {
  //       ReSTIRGI_Reservoir initialReservoir = RAB_LoadGIReservoir(threadIndex, ReSTIRGI_GetInitSamplePage());

  //       vec3 L0 = normalize(initialReservoir.position - minimalSurfaceInteraction.position);


  //       // Calculate MIS weight for restir output
  //       SurfaceMaterialInteractionSplitWeight brdf0 = opaqueSurfaceMaterialInteractionCalcProjectedWeight(material, minimalRayInteraction, L0);

  //       bool useRoughnessMIS = cb.reSTIRGIMISMode == misRoughnessMode;
  //       vec3 parallaxL, parallaxL0;
  //       if (useRoughnessMIS)
  //       {
  //         material.isotropicRoughness = max(material.isotropicRoughness, float16_t(cb.reSTIRGIMISRoughness));
  //         material.anisotropicRoughness = max(material.anisotropicRoughness, f16vec2(cb.reSTIRGIMISRoughness));
  //         parallaxL = L;
  //         parallaxL0 = L0;
  //       }
  //       else
  //       {
  //         parallaxL  = getParallaxDirection(minimalSurfaceInteraction.position, spatialReservoir.position, material.shadingNormal, minimalRayInteraction.viewDirection);
  //         parallaxL0 = getParallaxDirection(minimalSurfaceInteraction.position, initialReservoir.position, material.shadingNormal, minimalRayInteraction.viewDirection);
  //       }

  //       SurfaceMaterialInteractionSplitWeight roughBrdf = opaqueSurfaceMaterialInteractionCalcProjectedWeight(material, minimalRayInteraction, parallaxL);
  //       restirWeight = 1.0 - RAB_GetInitialSampleMISWeight(roughBrdf.specularReflectionWeight, brdf.specularReflectionWeight);

  //       // Calculate MIS weight for initial sample.
  //       // ReSTIR GI cannot handle transmissive rays. In that case, the initialWeight is 1.
  //       if (dot(parallaxL0, material.shadingNormal) > 0)
  //       {
  //         SurfaceMaterialInteractionSplitWeight roughBrdf0 = opaqueSurfaceMaterialInteractionCalcProjectedWeight(material, minimalRayInteraction, parallaxL0);
  //         initialWeight = RAB_GetInitialSampleMISWeight(roughBrdf0.specularReflectionWeight, brdf0.specularReflectionWeight);
  //       }

  //       // The above MIS cannot work well in very low roughness case because of floating point precision,
  //       // transition to initial samples in that case.
  //       const float fullMISRoughness = RAB_RESTIR_GI_DELTA_ROUGHNESS;
  //       const float noMISRoughness = 0.01;
  //       const float misWeight = saturate((material.isotropicRoughness - noMISRoughness) / (fullMISRoughness - noMISRoughness));
  //       initialWeight = lerp(1.0, initialWeight, misWeight);
  //       restirWeight = lerp(0.0, restirWeight, misWeight);
  //     }
  //   }
  // }


  // // Boiling filter
  // float boilingFilterMultiplier = lerp(cb.boilingFilterLowerThreshold, cb.boilingFilterHigherThreshold, VdotN);
  // if(cb.enableReSTIRGIBoilingFilter)
  // {
  //   // The following code is borrowed from RTXDI SDK. Unlike RTXDI, we use boiling filter to
  //   // filter fireflies in the ReSTIR output data. Those fireflies may not be filtered by denoisers
  //   // due to small normal details. We calculate the output samples' local average and use that
  //   // to remove outliers.

  //   // Start with average nonzero weight within the wavefront
  //   vec2 luminance = vec2(calcBt709Luminance(diffuseLight), calcBt709Luminance(specularLight));
  //   vec2 waveWeight = WaveActiveSum(luminance);
  //   uint waveCount = WaveActiveCountBits(spatialReservoir.avgWeight > 0);

  //   // Store the results of each wavefront into shared memory
  //   uint linearThreadIndex = LocalIndex.x + LocalIndex.y * RESTIR_GI_BOILING_FILTER_GROUP_SIZE;
  //   uint waveIndex = linearThreadIndex / WaveGetLaneCount();

  //   if (WaveIsFirstLane())
  //   {
  //       s_weights[waveIndex] = waveWeight;
  //       s_count[waveIndex] = waveCount;
  //   }

  //   GroupMemoryBarrierWithGroupSync();

  //   // Reduce the per-wavefront averages into a global average using one wavefront
  //   if (linearThreadIndex < (RESTIR_GI_BOILING_FILTER_GROUP_SIZE * RESTIR_GI_BOILING_FILTER_GROUP_SIZE + WaveGetLaneCount() - 1) / WaveGetLaneCount())
  //   {
  //       waveWeight = s_weights[linearThreadIndex];
  //       waveCount = s_count[linearThreadIndex];

  //       waveWeight = WaveActiveSum(waveWeight);
  //       waveCount = WaveActiveSum(waveCount);

  //       if (linearThreadIndex == 0)
  //       {
  //           s_weights[0] = (waveCount > 0) ? (waveWeight / float(waveCount)) : 0.0;
  //       }
  //   }

  //   GroupMemoryBarrierWithGroupSync();

  //   // Read the per-group average and apply the threshold
  //   vec2 averageNonzeroWeight = s_weights[0];
  //   if (spatialReservoir.M >= cb.temporalHistoryLength)
  //   {
  //     if (calcBt709Luminance(specularLight) > averageNonzeroWeight.y * cb.boilingFilterRemoveReservoirThreshold)
  //     {
  //       spatialReservoir.M = 0;
  //       RAB_StoreGIReservoir(spatialReservoir, threadIndex, ReSTIRGI_GetSpatialOutputPage());
  //     }

  //     if (calcBt709Luminance(diffuseLight) > averageNonzeroWeight.x * boilingFilterMultiplier)
  //     {
  //       diffuseLight *= averageNonzeroWeight.x / calcBt709Luminance(diffuseLight);
  //     }
  //     if (calcBt709Luminance(specularLight) > averageNonzeroWeight.y * boilingFilterMultiplier)
  //     {
  //       specularLight *= averageNonzeroWeight.y / calcBt709Luminance(specularLight);
  //     }
  //   }
  // }

  // if (all(filterColor == f16vec3(0)))
  // {
  //   spatialReservoir.setFlag(RESTIR_GI_FLAG_OCCLUDED_SAMPLE);

  //   const bool removeOccludedSamples = false;
  //   if(removeOccludedSamples)
  //     spatialReservoir.M = 0;
  //   RAB_StoreGIReservoir(spatialReservoir, threadIndex, ReSTIRGI_GetSpatialOutputPage());
  // }
  
  // // Restir GI processes only the primary surface data, the pixels for secondary surfaces are skipped above 
  // // Note: IndirectRadianceHitDistance is resource shared by primary and secondary surfaces
  // if (geometryFlags.primarySelectedIntegrationSurface)
  // {
  //   float hitDistance = length(spatialReservoir.position - minimalSurfaceInteraction.position);
  //   vec4 diffuseOutput = sanitizeRadianceHitDistance(diffuseLight, hitDistance);
  //   vec4 specularOutput = sanitizeRadianceHitDistance(specularLight, hitDistance);

  //   const bool useDiffuseMIS = false;
  //   if(cb.reSTIRGIMISMode != misDisabledMode)
  //   {
  //     vec4 initialDiffuseOutput = vec4(0);
  //     vec4 initialSpecularOutput = vec4(0);
        
  //     const vec4 indirectRadianceHitDistance = IndirectRadianceHitDistance[threadIndex];

  //     if (geometryFlags.firstSampledLobeIsSpecular) 
  //     {
  //       initialSpecularOutput = indirectRadianceHitDistance;
  //       initialSpecularOutput.xyz = clamp(initialSpecularOutput.xyz, 0, cb.reSTIRGIFireflyThreshold);
  //     }
  //     else
  //       initialDiffuseOutput = indirectRadianceHitDistance;

  //     if (useDiffuseMIS)
  //     {
  //       diffuseOutput = initialDiffuseOutput * initialWeight + diffuseOutput * restirWeight;
  //     }
  //     specularOutput = initialSpecularOutput * initialWeight + specularOutput * restirWeight;

  //     specularOutput.a = initialSpecularOutput.a;
  //     diffuseOutput.a = initialDiffuseOutput.a; 
  //   }

  //   // Note: These outputs include radiance often even if the Hit T value is 0 due to not being selected for integration. This indicates NRD
  //   // should fill in the Hit T value but also use the radiance as-is. This works because the diffuse/specular data is not stochastically selected
  //   // unlike we do in indirect integration typically, but rather is a more "solid" signal.
  //   PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = diffuseOutput;
  //   PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = specularOutput;
  // }

  // // DLSS enhancement
  // vec2 factor = vec2(1);
  // if(cb.enableEnhanceBSDFDetail && all(minimalSurfaceInteraction.position != spatialReservoir.position))
  // {
  //   if (cb.enhanceBSDFIndirectMode == 0)
  //   {
  //     const float3 cameraPosition = cameraGetWorldPosition(camera);
  //     ReSTIRGI_Reservoir temporalReservoir = RAB_LoadGIReservoir(threadIndex, ReSTIRGI_GetTemporalOutputPage());

  //     // NOTE: The offset pattern must match the pattern in ReSTIRGI_GetDlssEnhancementIndirectFactorLaplacian in RTXDI
  //     uint16_t opacityMask = 0;
  //     [unroll]
  //     for (int i = 0; i < 5; ++i)
  //     {
  //       int2 offset;
  //       switch (i)
  //       {
  //         case 0: offset = int2( 0,  0); break;
  //         case 1: offset = int2( 1,  0); break;
  //         case 2: offset = int2( 0,  1); break;
  //         case 3: offset = int2(-1,  0); break;
  //         case 4: offset = int2( 0, -1); break;
  //       }
  //       const uint opacityData = SharedMaterialData0[threadIndex + offset];
  //       const float16_t opacity = unorm8ToF16(uint8_t(opacityData >> 8));
  //       opacityMask |= uint16_t(opacity < 1.0f ? 0 : (1 << i));
  //     }

  //     factor = ReSTIRGI_GetDlssEnhancementIndirectFactorLaplacian(
  //       (int2)threadIndex, cameraPosition, minimalSurfaceInteraction.position, opacityMask,
  //       spatialReservoir, temporalReservoir,
  //       cb.enhanceBSDFIndirectLightMinRoughness, vec2(0.01f, 0.01f), vec2(10000.0f));

  //     factor = pow(factor, f16vec2(cb.enhanceBSDFIndirectLightPower));
  //     factor = clamp(factor, f16vec2(0.0), f16vec2(cb.enhanceBSDFIndirectLightMaxValue));
  //   }
  //   else if(cb.enhanceBSDFIndirectMode == 1)
  //   {
  //     const float3 interpolatedNormal = signedOctahedralToSphereDirection(snorm2x16ToFloat2x32(PrimaryWorldInterpolatedNormal[threadIndex]));
  //     factor = ReSTIRGI_GetDlssEnhancementIndirectFactorNormalDifference(
  //       (int2)threadIndex, minimalSurfaceInteraction.position, interpolatedNormal, spatialReservoir,
  //       cb.enhanceBSDFIndirectLightMinRoughness, vec2(0.05f, 0.01f), vec2(1.0f, 100.0f));

  //     // Scale and clamp factor
  //     factor = pow(factor, cb.enhanceBSDFIndirectLightPower * f16vec2(1.5f, 0.5f));
  //     factor = clamp(factor, f16vec2(0.0), f16vec2(cb.enhanceBSDFIndirectLightMaxValue));
  //   }
  // }
  // BSDFFactor2[threadIndex] = vec2(factor);

  // if (cb.debugView == DEBUG_VIEW_RESTIR_GI_FINAL_SHADING)
  // {
  //   storeInDebugView(threadIndex, vec3(initialWeight, restirWeight, 0));
  // }
  // else if (cb.debugView == DEBUG_VIEW_RESTIR_GI_VIRTUAL_HIT_T)
  // {
  //   float totalLength = length(spatialReservoir.position - minimalSurfaceInteraction.position);
  //   float virtualFraction = spatialReservoir.getVirtualFraction();
  //   storeInDebugView(threadIndex, virtualFraction / (totalLength + 0.001));
  // }
  // else if(cb.debugView == DEBUG_VIEW_NAN)
  // {
  //   bool isValid = true;
  //   isValid &= isValidValue(vec3(initialWeight, restirWeight, 0));
  //   isValid &= isValidValue(spatialReservoir.getVirtualFraction());
  //   accumulateInDebugViewAnd(threadIndex, isValid);
  // }
}