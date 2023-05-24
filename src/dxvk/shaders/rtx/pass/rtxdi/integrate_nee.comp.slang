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
#include "rtx/algorithm/integrator.slangh"

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

  vec4 initialDiffuseOutput = vec4(0);
  vec4 initialSpecularOutput = vec4(0);
  const vec4 indirectRadianceHitDistance = IndirectRadianceHitDistance[threadIndex];
  if (!gBufferMiss)
  {    
    if (geometryFlags.firstSampledLobeIsSpecular) 
    {
      initialSpecularOutput = indirectRadianceHitDistance;
      initialSpecularOutput.xyz = clamp(initialSpecularOutput.xyz, 0, cb.reSTIRGIFireflyThreshold);
    }
    else
      initialDiffuseOutput = indirectRadianceHitDistance;
  }

  PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = initialDiffuseOutput;
  PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = initialSpecularOutput;

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

  if (polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) != surfaceMaterialTypeOpaque)
  {
    return;
  }

  OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction = opaqueSurfaceMaterialInteractionCreate(polymorphicSurfaceMaterialInteraction);

  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_Reservoir.createEmpty();

  if (cb.enableNeeCache && cb.enableNeeCacheInFirstBounce)
  {
    NEECell cell = NEECache.findCell(minimalSurfaceInteraction.position, true);
    if (cell.isValid())
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
          vec2 uv = vec2(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng));
          LightSample lightSample = getLightSampleFromTriangle(candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, minimalSurfaceInteraction.position);

          if (any(lightSample.radiance > 0))
          {
            RayInteraction rayInteraction = {};
            rayInteraction.coneRadius = minimalRayInteraction.coneRadius;
            rayInteraction.viewDirection = minimalRayInteraction.viewDirection;
            f16vec3 inputDirection = normalize(lightSample.position - minimalSurfaceInteraction.position);
            float16_t diffuseSolidAnglePdf = opaqueSurfaceMaterialInteractionCalcDiffuseReflectionSolidAnglePdf(
              opaqueSurfaceMaterialInteraction, rayInteraction, inputDirection);
            float16_t specularSolidAnglePdf = opaqueSurfaceMaterialInteractionCalcSpecularReflectionSolidAnglePdf(
              opaqueSurfaceMaterialInteraction, rayInteraction, inputDirection);
            f16vec2 misWeight;
            if (cb.enableNeeCacheMIS)
            {
              misWeight.x = lightSample.solidAnglePdf / (lightSample.solidAnglePdf + diffuseSolidAnglePdf);
              misWeight.y = lightSample.solidAnglePdf / (lightSample.solidAnglePdf + specularSolidAnglePdf);
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
              lightSample, emissiveInvSelectionPdf, invalidRayPortalIndex, /*pathState.portalSpace*/PORTAL_SPACE_NONE, 
              OBJECT_MASK_ALL | (geometryFlags.objectMask & OBJECT_MASK_ALL_DYNAMIC) /*pathState.rayMask*/,
              isViewModel, /*ray*/ primaryRay, rayInteraction, /*surfaceInteraction*/minimalSurfaceInteraction,
              opaqueSurfaceMaterialInteraction, diffuseLight, specularLight);

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
              neeSample.position = lightSample.position;
              neeSample.normal = lightSample.normal;
              neeSample.radiance = lightSample.radiance / lightSample.solidAnglePdf;

              float wi = calcBt709Luminance(diffuseLight + specularLight);
              resultReservoir.update(wi, neeSample, 0.5);
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
    // Both inputs samples use MIS, no need to divide the result by M
    resultReservoir.M = 1;
    resultReservoir.finalize(pNew, 1.0, resultReservoir.M);

    // Store complete initial sample
    RAB_StoreGIReservoir(resultReservoir, threadIndex, ReSTIRGI_GetInitSamplePage());
  }

  switch(cb.debugView)
  {
  case DEBUG_VIEW_NEE_CACHE_CANDIDATE_ID:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      ivec3 cellID = NEECache.pointToCell(minimalSurfaceInteraction.position, false);
      vec3 color = 0;
      if (all(cellID != -1))
      {
        color = vec3(cellID + 1) / RADIANCE_CACHE_PROBE_RESOLUTION;
      }
      storeInDebugView(ivec2(threadIndex), color);
    }
    break;
  case DEBUG_VIEW_NEE_CACHE_HISTOGRAM:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      NEECell cell = NEECache.findCell(minimalSurfaceInteraction.position, false);
      ivec3 cellID = NEECache.pointToCell(minimalSurfaceInteraction.position, false);
      vec3 color = (surface.opaqueSurfaceMaterialInteraction.albedo + surface.opaqueSurfaceMaterialInteraction.baseReflectivity);
      if (all(cellID != -1))
      {
        color = lerp(color, vec3(cellID + 1) / RADIANCE_CACHE_PROBE_RESOLUTION, 0.9);
      }
      if (cell.isValid())
      {
        const ivec2 histogramSize = ivec2(100);
        const ivec2 histogramPixel = vec2(threadIndex.x, cb.camera.resolution.y - threadIndex.y) % histogramSize;
        const int padding = 2;
        if (all(histogramPixel >= ivec2(padding)) && all(histogramPixel < ivec2(histogramSize - padding)))
        {
          // Get bin index
          int legendHeight = 24;
          int maxBinCount = NEECell.getMaxCandidateCount();
          const ivec2 binPixel = histogramPixel - padding;
          const float binWidth = float(histogramSize.x - padding * 2) / maxBinCount;
          const int binHeight = histogramSize.y - padding * 2 - legendHeight;
          int binIndex = binPixel.x / binWidth;

          // Get bin threshold
          NEECandidate candidiate = cell.getCandidate(binIndex);
          float sampleThreshold = candidiate.getSampleThreshold();
          float lastSampleThreshold = 0;
          if (binIndex > 0)
          {
            NEECandidate candidiate = cell.getCandidate(binIndex - 1);
            lastSampleThreshold = candidiate.getSampleThreshold();
          }
          float probability = sampleThreshold - lastSampleThreshold;

          // Calculate histogram          
          const ivec2 pdfPixel = binPixel - ivec2(0, legendHeight);
          const int legendContentHeight = legendHeight - padding * 2;
          if (binPixel.y < legendContentHeight / 2)
          {
            int age = candidiate.getAge();
            color = lerp(vec3(0,1,0), vec3(1,0,0), saturate(age / 255.0));
          }
          else if (binPixel.y < legendContentHeight)
          {
            // Triangle color
            vec3 triangleCenter;
            f16vec3 triangleNormal;
            vec3 lightFlux;
            getTriangleLightFlux(candidiate.getSurfaceID(), candidiate.getPrimitiveID(), triangleCenter, triangleNormal, lightFlux);
            color = lightFlux / calcBt709Luminance(lightFlux) * 0.7;
          }
          else if(binPixel.y < legendHeight)
          {
            // Padding line
            color *= 0.8;
          }
          else if (pdfPixel.y < binHeight * probability)
          {
            // Histogram
            color = (dot(cellID, 1) & 0x1) ? vec3(0.8) : vec3(0.3);
          }
        }
        else
        {
          color *= 0.8;
        }
      }
      storeInDebugView(ivec2(threadIndex), color);
    }
    break;
  }
}