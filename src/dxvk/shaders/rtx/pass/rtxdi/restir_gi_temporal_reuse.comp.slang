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
#define RAB_HAS_PREVIOUS_GBUFFER 1
#define RAB_HAS_CURRENT_GBUFFER 1
#define RAB_HAS_RESTIR_GI_RESERVOIRS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/rtxdi/restir_gi_reuse_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/texture.slangh"
#include "rtx/utility/sampling.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/buffer_helpers.slangh"
#include "rtx/concept/ray/ray.slangh"
#include "rtx/concept/surface/surface.slangh"
#include "rtx/concept/surface_material/surface_material.slangh"
#include "rtx/concept/light/light.slangh"
#include "rtx/algorithm/resolve.slangh"
#include "rtx/algorithm/rtxdi/rtxdi.slangh"

void validateSample(ReSTIRGI_Reservoir inputReservoir, inout ReSTIRGI_Reservoir resultReservoir, inout bool isGBufferSimilar)
{
  // Calculate screen space position of the sample point
  // Note: NDC x/y coordinates are valid from [-1, 1], whereas the NDC z (depth) is valid from [0, 1]
  vec4 ndc = mul(cb.camera.worldToProjectionJittered, vec4(resultReservoir.position, 1.0f));
  ndc.xyz /= ndc.w;
  bool isInsideScreen = all((ndc.xyz <= vec3(1.0f, 1.0f, 1.0f)) && (ndc.xyz >= vec3(-1.0f, -1.0f, 0.0f)));

  if (isInsideScreen)
  {
    // Check the gradient value
    ivec2 confidencePixel = cameraNDCToScreenUV(cb.camera, ndc.xy) * vec2(cb.camera.resolution);
    vec2 gradient = RtxdiGradients[uint3(confidencePixel / RTXDI_GRAD_FACTOR, 0)];
    if (gradient.x > cb.reSTIRGISampleValidationThreshold)
    {
      // Check if depth value is in the given tolerance
      float sampleSurfaceDepth = gradient.y;
      float reservoirDepth = length(resultReservoir.position - cameraGetWorldPosition(cb.camera));
      const float depthTolerance = 0.05;

      // Discard the sample if depth matches, and gradient is above given threshold
      if (abs(sampleSurfaceDepth - reservoirDepth) < max(sampleSurfaceDepth, reservoirDepth) * depthTolerance)
      {
        // Recover input reservoir's weight, see the TODO above.
        inputReservoir.avgWeight = inputReservoir.avgWeight > 0 ? 1.0 / inputReservoir.avgWeight : 0.0;
        resultReservoir = inputReservoir;
        isGBufferSimilar = false;
      }
    }
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(int2 thread_id : SV_DispatchThreadID)
{
  Camera camera = cb.camera;
  if (thread_id.x >= camera.resolution.x || thread_id.y >= camera.resolution.y)
  {
    return;
  }

  RAB_Surface surface = RAB_GetGBufferSurface(thread_id, false);
  if (!RAB_IsSurfaceValid(surface))
  {
    return;
  }


  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(thread_id, SharedFlags);

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
    vec4 radianceAndDistance = RestirGIRadiance[thread_id];
    initialSample.radiance = sanitize(radianceAndDistance.rgb, vec3(0));
    indirectPathLength = radianceAndDistance.w; 
  }

  // The restir algorithm can accept larger fireflies while producing stable result, so set the threshold higher to avoid darkening.
  const float fireflyFilteringFactor = 30;
  initialSample.radiance = fireflyFiltering(initialSample.radiance, cb.fireflyFilteringLuminanceThreshold * fireflyFilteringFactor);

  uint8_t portalID;
  ReSTIRGI_LoadHitGeometry(RestirGIHitGeometry, thread_id, initialSample.position, initialSample.normal, portalID);
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
  if (cb.enableReSTIRGIVirtualSample > 0 && surface.opaqueSurfaceMaterialInteraction.isotropicRoughness > RAB_RESTIR_GI_DELTA_ROUGHNESS)
  {
    vec3 direction = initialSample.position - surface.minimalSurfaceInteraction.position;
    if (any(direction != 0) && indirectPathLength > length(direction) * 1.01)
    {
      // Extend virtual fraction length by 20% to prevent occlusion when the sample is used by neighbor pixels
      initialSample.setVirtualFraction((indirectPathLength - length(direction)) * 1.2);
      initialSample.position = surface.minimalSurfaceInteraction.position + normalize(direction) * indirectPathLength;
      initialSample.normal = -normalize(direction);
    }
  }

  ReSTIRGI_Reservoir inputReservoir = ReSTIRGI_Reservoir.createEmpty();
  RAB_RandomSamplerState rng = RAB_InitRandomSampler(thread_id, cb.frameIdx, 2);
  if (cb.neeCacheArgs.enable && cb.neeCacheArgs.enableOnFirstBounce)
  {
    inputReservoir = RAB_LoadGIReservoir(thread_id, ReSTIRGI_GetInitSamplePage());
  }

  float wiT = max(0.f, initialSample.avgWeight) * initialSample.M * RAB_GetGITargetPdfForSurface(initialSample.radiance, initialSample.position, surface);
  inputReservoir.update(wiT, initialSample, RAB_GetNextRandom(rng));

  float pNew = RAB_GetGITargetPdfForSurface(inputReservoir.radiance, inputReservoir.position, surface);
  // Both inputs samples use MIS, no need to divide the result by M
  inputReservoir.M = 1;
  inputReservoir.finalize(pNew, 1.0, inputReservoir.M);

  // Store complete initial sample
  RAB_StoreGIReservoir(inputReservoir, thread_id, ReSTIRGI_GetInitSamplePage());

  // Reprojection
  // Note: Camera jittering not used for RTXDI. Also not using Ray and rayEvaluate here due to Ray using 16 bit
  // directions currently until we have a way to differentiate that on a per-instance basis (via generics).
  const float3 virtualMotionVector = PrimaryVirtualMotionVector[thread_id].xyz;
  const float3 prevVirtualWorldPosition = surface.virtualWorldPosition + virtualMotionVector;

  vec4 prevNDC = mul(camera.prevWorldToProjection, vec4(prevVirtualWorldPosition, 1.0f));
  prevNDC.xyz /= prevNDC.w;
  float2 prevPixelCenter = cameraNDCToScreenUV(camera, prevNDC.xy) * vec2(camera.resolution);
  float expectedPrevHitDistance = length(prevVirtualWorldPosition - cameraGetPreviousWorldPosition(camera).xyz);
  float viewDirectionDotTriangleNormal = abs(dot(surface.minimalRayInteraction.viewDirection, surface.minimalSurfaceInteraction.triangleNormal));
  float2 depthThreshold = 0.01 / max(viewDirectionDotTriangleNormal, 0.01);
  float normalThreshold = lerp(0.995, 0.5, surface.opaqueSurfaceMaterialInteraction.isotropicRoughness);

  // Calculate reflection sample weight
  float reflectionWeight = 0;
  if (cb.enableReSTIRGIReflectionReprojection > 0)
  {
    float roughWeight = 1.0 - surface.opaqueSurfaceMaterialInteraction.isotropicRoughness;
    const float16_t normalDotOutputDirection = dot(surface.opaqueSurfaceMaterialInteraction.shadingNormal, surface.minimalRayInteraction.viewDirection);
    const float16_t albedoLuminance = calcBt709Luminance(surface.opaqueSurfaceMaterialInteraction.albedo);
    const f16vec3 fresnel = evalOpaqueSchlickFresnel(surface.opaqueSurfaceMaterialInteraction.baseReflectivity, normalDotOutputDirection);
    const float16_t fresnelLuminance = calcBt709Luminance(fresnel);
    float specularWeight = fresnelLuminance / (fresnelLuminance + albedoLuminance);
    reflectionWeight = specularWeight * roughWeight;
  }

  // Reflection reprojection, only enabled when reflection and parallax are strong enough
  float2 prevBackupPixelCenter = -1;
  float2 currentPixelCenter = thread_id + 0.5;
  bool discardEnlargedPixels = cb.enableReSTIRGIDiscardEnlargedPixels;
  float reflectionReprojectionWeight = 0;
  if (cb.enableReSTIRGIReflectionReprojection > 0 && reflectionWeight > 0.05 && length(prevPixelCenter - currentPixelCenter) > 1)
  {
    float3 worldPos = surface.minimalSurfaceInteraction.position;
    float3 viewVector = -surface.minimalRayInteraction.viewDirection;
    float3 reflectionVector = reflect(viewVector, surface.opaqueSurfaceMaterialInteraction.shadingNormal);

    // Calculate reflection hit T
    uint8_t backupPortalID = RTXDI_INVALID_PORTAL_INDEX;
    uint8_t rayMask = OBJECT_MASK_OPAQUE | (surface.objectMask & OBJECT_MASK_ALL_DYNAMIC);
    const float infiniteHitT = 1e5;
    float3 dstPosition = worldPos + reflectionVector * infiniteHitT;
    VisibilityResult visibility = traceVisibilityRay(surface.minimalSurfaceInteraction,
      dstPosition, rayMask, visibilityModeAccurateHitDistance, backupPortalID, surface.portalSpace,
      0, 0, RAB_RESTIR_GI_VISIBILITY_SHORTENING, surface.isViewModel);
    float reflectionHitT = visibility.hasOpaqueHit ? visibility.hitDistance : infiniteHitT;

    // Calculate reflection reprojected position 
    float3 reflectPosition = worldPos + viewVector * reflectionHitT;
    vec4 reflectNDC = mul(camera.prevWorldToProjection, vec4(reflectPosition, 1.0f));
    reflectNDC.xyz /= reflectNDC.w;
    float2 prevReflectionCenter = cameraNDCToScreenUV(camera, reflectNDC.xy) * vec2(camera.resolution);

    // Intepolate reprojection position, or randomly choose one type of reprojection
    const float interpolateDistance = cb.restirGIReflectionMinParallax;
    float reprojectionDistance = length(prevReflectionCenter - prevPixelCenter);
    reflectionReprojectionWeight = reflectionWeight;
    prevBackupPixelCenter = prevPixelCenter; // Set original reprojection position as a backup position
    if (reprojectionDistance < interpolateDistance)
    {
      float distanceWeight = saturate(reprojectionDistance / interpolateDistance);
      reflectionReprojectionWeight *= distanceWeight;
      prevPixelCenter = lerp(prevPixelCenter, prevReflectionCenter, reflectionReprojectionWeight);
    }
    else if (RAB_GetNextRandom(rng) < reflectionReprojectionWeight)
    {
      // To get significant parallax, the camera must be close to reflective surface.
      // Losen depth check and don't discard enlarged pixels to get stable reprojected result.
      prevPixelCenter = prevReflectionCenter;
      depthThreshold.x = 0.25;
      discardEnlargedPixels = false;
    }
  }

  ReSTIRGI_TemporalResamplingParameters tparams = {};
  tparams.temporalHistoryLength = cb.temporalHistoryLength;
  tparams.enableJacobian = cb.enableReSTIRGITemporalJacobian;
  tparams.enableBiasCorrection = cb.enableReSTIRGITemporalBiasCorrection;
  tparams.prevPixelCenter = prevPixelCenter;
  tparams.prevBackupPixelCenter = prevBackupPixelCenter;
  tparams.resolution = cb.camera.resolution;
  tparams.expectedPrevHitDistance = vec2(expectedPrevHitDistance);
  tparams.normalThreshold = normalThreshold;
  tparams.depthThreshold = depthThreshold;
  tparams.virtualMotionVector = virtualMotionVector;
  tparams.uniformRandomNumber = cb.uniformRandomNumber;
  tparams.enablePermutationSampling = cb.enableReSTIRGIPermutationSampling;
  tparams.permutationSamplingSize = cb.permutationSamplingSize;
  tparams.teleportationPortalIndex = cb.teleportationPortalIndex;
  tparams.discardEnlargedPixels = discardEnlargedPixels;
  tparams.sourceBufferIndex = ReSTIRGI_GetTemporalInputPage();
  tparams.initialSearchRadius = 0;

  bool isGBufferSimilar;
  bool isInitialSample;
  // TODO: The temporal reuse function considers the average weight as pdf, instead of W.
  // According to the GRIS paper: Generalized Resampled Importance Sampling: Foundations of ReSTIR, E[W] = 1 / pdf
  // So a quick fix here is to use the reciprocal of average weight.
  // Should fix the temporal reuse function in the future
  inputReservoir.avgWeight = inputReservoir.avgWeight > 0 ? 1.0 / inputReservoir.avgWeight : 0.0;
  ReSTIRGI_Reservoir resultReservoir = ReSTIRGI_TemporalResampling(inputReservoir, thread_id, surface, rng, tparams, isGBufferSimilar, isInitialSample);

  // Update last frame's visibility mask when raytraced pairwise MIS is used. 
  if (cb.reSTIRGIBiasCorrectionMode == RTXDI_BIAS_CORRECTION_PAIRWISE_RAY_TRACED &&
     isGBufferSimilar && !isInitialSample)
  {
    bool isVisible = RAB_TraceGISampleVisibility(surface, surface, resultReservoir, cb.numActiveRayPortals);
    resultReservoir.clearFlag(RESTIR_GI_FLAG_OCCLUDED_SAMPLE);
    resultReservoir.setFlag(isVisible ? 0 : RESTIR_GI_FLAG_OCCLUDED_SAMPLE);
  }

  // Sample validation use rtxdi gradient to discard stale samples.
  // Although it can only verify samples inside the screen, it still helps in many cases.
  if(cb.enableReSTIRGISampleValidation > 0 && !isInitialSample)
  {
    validateSample(inputReservoir, resultReservoir, isGBufferSimilar);
  }

  if (!cb.enableReSTIRGITemporalReuse)
  {
    resultReservoir = inputReservoir;
  }
  RAB_StoreGIReservoir(resultReservoir, thread_id, ReSTIRGI_GetTemporalOutputPage());

  if (cb.debugView == DEBUG_VIEW_RESTIR_GI_INITIAL_SAMPLE)
  {
    storeInDebugView(thread_id, inputReservoir.radiance);
  }
  else if (cb.debugView == DEBUG_VIEW_RESTIR_GI_TEMPORAL_REUSE)
  {
    storeInDebugView(thread_id, isGBufferSimilar ? lerp(vec3(1, 0, 0), vec3(0, 1, 0), reflectionReprojectionWeight) : vec3(0));
  }
  else if(cb.debugView == DEBUG_VIEW_NAN)
  {
    bool isValid = true;
    // DEBUG_VIEW_RESTIR_GI_INITIAL_SAMPLE
    isValid &= isValidValue(inputReservoir.radiance);
    accumulateInDebugViewAnd(thread_id, isValid);
  }
}
