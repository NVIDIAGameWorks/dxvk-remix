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
#define OPAQUE_MATERIAL_USE_POM 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/integrate/integrate_nee_bindings.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/algorithm/integrator_helpers.slangh"
#include "rtxdi/DlssEnhancementFilterFunctions.slangh"
#include "rtx/algorithm/nee_cache.h"
#include "rtx/algorithm/integrator.slangh"

float16_t getLobeAngleFromMaterial(OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction)
{
  const float perceptualRoughnessSquared = opaqueSurfaceMaterialInteraction.isotropicRoughness;
  const float lobeAngleNorm = 2.0 * perceptualRoughnessSquared / (1.0 + perceptualRoughnessSquared);
  const float specularRatio = calcBt709Luminance(opaqueSurfaceMaterialInteraction.baseReflectivity)
                            / calcBt709Luminance(opaqueSurfaceMaterialInteraction.albedo + opaqueSurfaceMaterialInteraction.baseReflectivity);
  const float finalLobeAngleNorm = lerp(1.0, lobeAngleNorm, specularRatio);
  return pi * 0.25 * finalLobeAngleNorm;
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
    threadIndex, PrimaryWorldShadingNormal, PrimaryPerceptualRoughness, PrimaryAlbedo, PrimaryBaseReflectivity,
    SharedMaterialData0, SharedMaterialData1, SharedSurfaceIndex, SharedSubsurfaceData);

  // Perform next event estimation on cached emissive triangle list
  // Also add NEE sample to sample reservoir
  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_Reservoir.createEmpty();
  if (cb.neeCacheArgs.enable && cb.neeCacheArgs.enableOnFirstBounce &&
      polymorphicSurfaceMaterialInteractionGetTypeHelper(polymorphicSurfaceMaterialInteraction) == surfaceMaterialTypeOpaque)
  {
    uint jitter = sampleUniformIntRng(rtxdiRNG);
    NEECell cell = NEECache.getCell(NEECache.pointToOffset(minimalSurfaceInteraction.position, minimalSurfaceInteraction.triangleNormal, jitter));
    if (cell.isValid())
    {
      int candidateCount = cell.getCandidateCount();
      if (candidateCount > 0)
      {
        OpaqueSurfaceMaterialInteraction opaqueSurfaceMaterialInteraction = opaqueSurfaceMaterialInteractionCreate(polymorphicSurfaceMaterialInteraction);

        LightSample lightSample;
        float lightObjectPdf = 0;
        NEECandidate candidate = cell.sampleCandidate(RAB_GetNextRandom(rtxdiRNG), lightObjectPdf);
        int primitiveIndex = candidate.getPrimitiveID();
        vec2 uv = vec2(RAB_GetNextRandom(rtxdiRNG), RAB_GetNextRandom(rtxdiRNG));
        float triangleArea;
        float16_t spreadAngle = getLobeAngleFromMaterial(opaqueSurfaceMaterialInteraction);
        lightSample = NEECacheUtils.calculateLightSampleFromTriangle(
          candidate.getSurfaceID(), candidate.getPrimitiveID(), uv, lightObjectPdf, minimalSurfaceInteraction.position,
          minimalRayInteraction.coneRadius, spreadAngle, triangleArea, true, kFootprintFromRayOriginClamped);
          
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
          uint16_t surfaceIndex = cb.pomEnableNEECache && opaqueSurfaceMaterialInteractionHasHeightTexture(opaqueSurfaceMaterialInteraction) ? uint16_t(SharedSurfaceIndex[threadIndex]) : BINDING_INDEX_INVALID;
          isVisible = evalNEESecondary(
            lightSample, invalidRayPortalIndex, surface.portalSpace, rayMask,
            surface.isViewModel, primaryRay,
            minimalRayInteraction, minimalSurfaceInteraction, opaqueSurfaceMaterialInteraction,
            diffuseLight, specularLight, surfaceIndex, SharedTextureCoord[threadIndex]);

          // Skip some pixels to reduce memory traffic, especially when large screen area is occupied by a single cell.
          if (any(diffuseLight + specularLight) > 0 && (threadIndex.x + threadIndex.y) % 4 == 0)
          {
            float16_t accumulateValue = calcBt709Luminance(diffuseLight + specularLight);
            int prefixTask = NEECacheUtils.convertIDToPrefixSumID(candidate.getSurfaceID(), primitiveIndex, PrimitiveIDPrefixSum);
            float16_t randomOffset = RAB_GetNextRandom(rtxdiRNG);
            cell.insertSlotTask(prefixTask, accumulateValue, randomOffset, false);
          }

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
  RAB_StoreGIReservoir(resultReservoir, threadIndex, ReSTIRGI_GetInitSamplePage());

  PrimaryIndirectDiffuseLobeRadianceHitDistance[threadIndex] = indirectDiffuseOutput;
  PrimaryIndirectSpecularLobeRadianceHitDistance[threadIndex] = indirectSpecularOutput;
}