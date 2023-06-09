/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/integrate/integrate_nee_bindings.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtxdi/DlssEnhancementFilterFunctions.slangh"
#include "rtx/algorithm/nee_cache.h"
#include "rtx/algorithm/integrator.slangh"


void showDebugViews(uint2 threadIndex, GeometryFlags geometryFlags, MinimalSurfaceInteraction minimalSurfaceInteraction, RAB_Surface surface)
{
  // Debug views
  switch(cb.debugView)
  {
  case DEBUG_VIEW_NEE_CACHE_CANDIDATE_ID:
    if (geometryFlags.primarySelectedIntegrationSurface)
    {
      ivec3 cellID = NEECache.pointToCell(minimalSurfaceInteraction.position, false);
      vec3 color = 0;
      if (all(cellID != -1))
      {
        color = vec3(cellID + 1) / NEE_CACHE_PROBE_RESOLUTION;
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
        color = lerp(color, (dot(cellID, 1) & 0x1) ? vec3(1) : vec3(0), 0.3);
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
            vec3 lightIntensity;
            NEECacheUtils.calculateTriangleLightIntensity(candidiate.getSurfaceID(), candidiate.getPrimitiveID(), triangleCenter, triangleNormal, lightIntensity);
            color = lightIntensity / calcBt709Luminance(lightIntensity) * 0.7;
          }
          else if(binPixel.y < legendHeight)
          {
            // Padding line
            color *= 0.8;
          }
          else if (pdfPixel.y < binHeight * probability)
          {
            // Histogram
            color = (dot(cellID, 1) & 0x1) ? vec3(1,0.5,0) : vec3(0,0,1);
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

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadIndex : SV_DispatchThreadID, uint2 LocalIndex : SV_GroupThreadID)
{
  if (any(threadIndex >= cb.camera.resolution))
  {
    return;
  }

  const bool gBufferMiss = isGBufferMiss(threadIndex, PrimaryConeRadius);

  // Early out if the primary surface was a miss or if the indirect integrator was not selected
  // to output indirect lighting information
  if (gBufferMiss)
  {
    return;
  }

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadIndex, SharedFlags);

  vec4 indirectDiffuseOutput = vec4(0);
  vec4 indirectSpecularOutput = vec4(0);
  if (geometryFlags.primarySelectedIntegrationSurface)
  {
    const vec4 indirectRadianceHitDistance = IndirectRadianceHitDistance[threadIndex];   
    if (geometryFlags.firstSampledLobeIsSpecular) 
    {
      indirectSpecularOutput = indirectRadianceHitDistance;
      indirectSpecularOutput.xyz = clamp(indirectSpecularOutput.xyz, 0, cb.reSTIRGIFireflyThreshold);
    }
    else
    {
      indirectDiffuseOutput = indirectRadianceHitDistance;
    }
  }

  // Early out in case when NEE cache is disabled.
  if(!cb.neeCacheArgs.enable)
  {
    PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = indirectDiffuseOutput;
    PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = indirectSpecularOutput;
    return;
  }

  // Note: Re-derive the original primary ray direction.
  const Ray originalPrimaryRay = rayCreatePrimaryFromPixel(cb.camera, threadIndex);

  RNG rng = createRNG(uvec2(threadIndex), cb.frameIdx);
  RAB_RandomSamplerState rtxdiRNG = RAB_InitRandomSampler(threadIndex, cb.frameIdx, 7);
  RAB_Surface surface = RAB_GetGBufferSurface(threadIndex, false);

  // Deserialize the Ray
  GBufferMemoryMinimalRay gBufferMemoryMinimalRay;
  gBufferMemoryMinimalRay.spreadAngle = float16_t(cb.screenSpacePixelSpreadHalfAngle);
  const MinimalRay minimalRay = minimalRayCreate(gBufferMemoryMinimalRay);
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

  // Perform next event estimation on cached emissive triangle list
  // Also add NEE sample to sample reservoir
  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_Reservoir.createEmpty();
  if (cb.neeCacheArgs.enable && cb.neeCacheArgs.enableOnFirstBounce &&
      polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) == surfaceMaterialTypeOpaque)
  {
    NEECell cell = NEECache.findCell(minimalSurfaceInteraction.position, true);
    if (cell.isValid())
    {
      int candidateCount = cell.getCandidateCount();
      if (candidateCount > 0)
      {
        OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction = opaqueSurfaceMaterialInteractionCreate(polymorphicSurfaceMaterialInteraction);
        float lightObjectPdf = 0;
        NEECandidate candidate = cell.sampleCandidate(getNextSampleBlueNoise(rng), lightObjectPdf);
        int surfaceIndex = candidate.getSurfaceID();
        int primitiveIndex = candidate.getPrimitiveID();
        if (candidate.isValid())
        {
          vec2 uv = vec2(getNextSampleBlueNoise(rng), getNextSampleBlueNoise(rng));
          LightSample lightSample = NEECacheUtils.calculateLightSampleFromTriangle(
            candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, minimalSurfaceInteraction.position, minimalRayInteraction.coneRadius, primaryRay.spreadAngle);

          bool isVisible = false;
          vec3 diffuseLight = 0;
          vec3 specularLight = 0;
          if (any(lightSample.radiance > 0))
          {
            vec3 inputDirection = normalize(lightSample.position - minimalSurfaceInteraction.position);
            float brdfSampleSolidAnglePdf = opaqueSurfaceMaterialInteractionCalcSolidAnglePdf(opaqueSurfaceMaterialInteraction, minimalRayInteraction, inputDirection);
            float misWeight = lightSample.solidAnglePdf / (brdfSampleSolidAnglePdf + lightSample.solidAnglePdf);
            lightSample.radiance *= misWeight;

            evaluateUnshadowedLight(lightSample, opaqueSurfaceMaterialInteraction, minimalRayInteraction, inputDirection, diffuseLight, specularLight);

            uint8_t rayMask = OBJECT_MASK_OPAQUE | (geometryFlags.objectMask & OBJECT_MASK_ALL_DYNAMIC);
            isVisible = evalNEESecondary(
              lightSample, invalidRayPortalIndex, surface.portalSpace, rayMask,
              surface.isViewModel, primaryRay, minimalRayInteraction, minimalSurfaceInteraction,
              diffuseLight, specularLight);

            indirectDiffuseOutput.xyz += diffuseLight;
            indirectSpecularOutput.xyz += specularLight;
          }

          if (cb.enableReSTIRGI && isVisible)
          {
            ReSTIRGI_Reservoir neeSample = ReSTIRGI_Reservoir.createEmpty();
            neeSample.M = 1;
            neeSample.avgWeight = 1;
            neeSample.flagsAndVirtualFraction = 0;
            neeSample.radiance = vec3(0);
            neeSample.setPortalID(RESTIR_GI_INVALID_INDIRECT_LIGHT_PORTAL_ID);
            neeSample.position = lightSample.position;
            neeSample.normal = lightSample.normal;
            neeSample.radiance = lightSample.radiance / lightSample.solidAnglePdf;
            neeSample.setVirtualFraction(0.0);

            float wi = RAB_GetGITargetPdfForSurface(neeSample.radiance, neeSample.position, surface);
            resultReservoir.update(wi, neeSample, 0.5);
          }
        }
      }
    }
  }
  RAB_StoreGIReservoir(resultReservoir, threadIndex, ReSTIRGI_GetInitSamplePage());

  PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = indirectDiffuseOutput;
  PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = indirectSpecularOutput;

  showDebugViews(threadIndex, geometryFlags, minimalSurfaceInteraction, surface);
}