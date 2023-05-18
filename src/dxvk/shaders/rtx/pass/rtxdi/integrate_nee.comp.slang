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

bool evalNEESecondary(
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
  //f16vec2 diffuseSpecularMISWeight,
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
    return false;

  // Evaluate the material and accumulate throughput and hit distance

  const SurfaceMaterialInteractionSplitWeight splitWeight = opaqueSurfaceMaterialInteractionCalcProjectedWeight(
    opaqueSurfaceMaterialInteraction, minimalRayInteraction, visibility.rayDirection);

  const f16vec3 combinedWeight = splitWeight.diffuseReflectionWeight + splitWeight.specularReflectionWeight;

  specularPortion = calcBt709Luminance(splitWeight.specularReflectionWeight) / calcBt709Luminance(combinedWeight);

  const float lightSampleThroughput = invSelectionPdf / lightSample.solidAnglePdf;

  // Accumulate Radiance

  const vec3 lightThroughput = /*vec3(combinedWeight) */ lightSampleThroughput * vec3(visibility.attenuation);
  const vec3 attenuatedRadiance = lightSample.radiance * lightThroughput;

  diffuseLight = attenuatedRadiance * splitWeight.diffuseReflectionWeight;// *diffuseSpecularMISWeight.x;
  specularLight = attenuatedRadiance * splitWeight.specularReflectionWeight;// *diffuseSpecularMISWeight.y;
  //return attenuatedRadiance;
  //accumulatedRadiance += throughput * attenuatedRadiance;
  return true;
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
  RAB_RandomSamplerState rtxdiRNG = RAB_InitRandomSampler(threadIndex, cb.frameIdx, 7);
  RAB_Surface surface = RAB_GetGBufferSurface(threadIndex, false);
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

  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_Reservoir.createEmpty();

  if (cb.enableNeeCache && cb.enableNeeCacheInFirstBounce)
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
              if (cb.enableNeeCacheMIS)
              {
                misWeight.x = lightPdf / (lightPdf + diffuseSolidAnglePdf);
                misWeight.y = lightPdf / (lightPdf + specularSolidAnglePdf);
              }
              else
              {
                misWeight = 1.0;
              }

              float16_t specularPortion2;
              const bool isViewModel = false;// pathState.rayMask& OBJECT_MASK_ALL_VIEWMODEL;
              float emissiveInvSelectionPdf = 1.0;// / lightObjectPdf;
              vec3 diffuseLight, specularLight;
              bool isVisible = evalNEESecondary(
                emissiveLightSample, emissiveInvSelectionPdf, invalidRayPortalIndex, /*pathState.portalSpace*/PORTAL_SPACE_NONE, 
                OBJECT_MASK_ALL | (geometryFlags.objectMask & OBJECT_MASK_ALL_DYNAMIC) /*pathState.rayMask*/,
                isViewModel, /*ray*/ primaryRay, rayInteraction, /*surfaceInteraction*/minimalSurfaceInteraction,
                opaqueSurfaceMaterialInteraction, specularPortion2, diffuseLight, specularLight);


              //pathState.radiance += pathState.throughput * emissiveNEELight;
              //PrimaryDirectDiffuseLobeRadianceHitDistance[threadIndex].xyz += emissiveNEELight;
              //PrimaryDirectSpecularLobeRadianceHitDistance[threadIndex] = 0;// emissiveNEELight;
               PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex].xyz += diffuseLight * misWeight.x;
               PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex].xyz += specularLight * misWeight.y;

              if (cb.enableReSTIRGI && isVisible)
              {
                ReSTIRGI_Reservoir neeSample;
                neeSample.M = 1;
                neeSample.avgWeight = 1;
                neeSample.flagsAndVirtualFraction = 0;
                neeSample.radiance = vec3(0);
                neeSample.setPortalID(RESTIR_GI_INVALID_INDIRECT_LIGHT_PORTAL_ID);
                neeSample.position = emissiveLightSample.position;
                neeSample.normal = emissiveLightSample.normal;
                neeSample.radiance = emissiveLightSample.radiance / emissiveLightSample.solidAnglePdf;

                float wi = calcBt709Luminance(diffuseLight + specularLight);
                resultReservoir.update(wi, neeSample, 0.5);
              }
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

    float wiT = max(0.f, initialSample.avgWeight) * initialSample.M * RAB_GetGITargetPdfForSurface(initialSample.radiance, initialSample.position, surface);
    resultReservoir.update(wiT, initialSample, RAB_GetNextRandom(rtxdiRNG));

    float pNew = RAB_GetGITargetPdfForSurface(resultReservoir.radiance, resultReservoir.position, surface);
    resultReservoir.finalize(pNew, 1.0, resultReservoir.M);
    resultReservoir.M = 1;

    // Store complete initial sample
    RAB_StoreGIReservoir(resultReservoir, threadIndex, ReSTIRGI_GetInitSamplePage());
  }
}