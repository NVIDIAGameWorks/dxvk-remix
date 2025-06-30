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
#define USE_32BIT_RAY_DIRECTION 1
#define RAB_HAS_RTXDI_RESERVOIRS 1
#define RAB_GRADIENTS_PASS 1

#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/rtxdi/rtxdi_compute_gradients_bindings.h"

#include "rtx/algorithm/rtxdi/rtxdi.slangh"

float getSurfaceIlluminance(
  LightSample lightSample, float invSelectionPdf, uint8_t sampledTransportPortalIndex,
  MinimalSurfaceInteraction minimalSurfaceInteraction, float coneRadiusAtSurface, 
  PortalSpace2BitsType portalSpace, uint8_t objectMask, bool isViewModel, bool usePreviousTLAS)
{
  // Setup and trace the visibility ray

  uint8_t rayMask = OBJECT_MASK_OPAQUE | (objectMask & OBJECT_MASK_ALL_DYNAMIC);
  rayMask |= (cb.enableDirectTranslucentShadows) ? OBJECT_MASK_TRANSLUCENT : 0;
  rayMask |= (cb.enableDirectAlphaBlendShadows) ? OBJECT_MASK_ALPHA_BLEND : 0;

  VisibilityResult visibility = traceVisibilityRay<visibilityModeEnableTranslucentMaterials | visibilityModeEnableSubsurfaceMaterials>(minimalSurfaceInteraction,
    lightSample.position, rayMask,
    sampledTransportPortalIndex, portalSpace, false,
    coneRadiusAtSurface, cb.screenSpacePixelSpreadHalfAngle, 
    1.0, isViewModel, usePreviousTLAS);

  if (visibility.hasOpaqueHit)
    return 0;

  const float lightSampleThroughput = invSelectionPdf / lightSample.solidAnglePdf;
  const vec3 commonLightThroughput = lightSampleThroughput * vec3(visibility.attenuation);
  return calcBt709Luminance(commonLightThroughput * lightSample.radiance);
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 GlobalIndex : SV_DispatchThreadID)
{
  // This shader runs one thread per image stratum, i.e. a square of pixels
  // (RTXDI_GRAD_FACTOR x RTXDI_GRAD_FACTOR) in size. One pixel is selected to
  // produce a gradient.

  RAB_RandomSamplerState rng = RAB_InitRandomSampler(GlobalIndex, cb.frameIdx, 5);

  bool usePrevSample;
  float selectedIlluminance;
  int2 selectedPixelPos;
  int2 selectedPrevPixelPos;
  int2 bestLightPixelPos;

  RTXDI_FindGradientSampleAndBestLight(rng, GlobalIndex, cb.camera.resolution, push.usePreviousIlluminance,
    usePrevSample, selectedIlluminance, selectedPixelPos, selectedPrevPixelPos, bestLightPixelPos);


  // Write out the best light information
  uint16_t bestLightIndex = RTXDI_INVALID_LIGHT_INDEX;
  uint16_t bestLightPortalIdx = RTXDI_INVALID_PORTAL_INDEX;
  if (bestLightPixelPos.x >= 0)
  {
    RTXDI_Reservoir selectedReservoir = RAB_LoadReservoir(bestLightPixelPos, RTXDI_GetSpatialOutputPage());
    bestLightIndex = selectedReservoir.lightIdx;
    bestLightPortalIdx = selectedReservoir.portalIdx;
  }
  RtxdiBestLights[GlobalIndex] = uint2(bestLightIndex, bestLightPortalIdx);

  if (!push.computeGradients)
  {
    return;
  }

  float2 gradient = 0;

  // If we have found at least one non-black pixel in the loop above...
  if (selectedIlluminance > 0)
  {
    int2 selectedCurrentOrPrevPixelPos = usePrevSample ? selectedPrevPixelPos : selectedPixelPos;

    // Load the reservoir that was selected for gradient evaluation, either from the current or the previous frame.
    RTXDI_Reservoir selectedReservoir = RAB_LoadReservoir(selectedCurrentOrPrevPixelPos, 
      usePrevSample ? RTXDI_GetTemporalInputPage() : RTXDI_GetSpatialOutputPage());

    // Map the reservoir's light index into the other frame (previous or current)
    uint16_t selectedMappedLightIndex = RAB_TranslateLightIndex(selectedReservoir.lightIdx, !usePrevSample);

    // Special handling for lights sampled through portals
    if (selectedReservoir.portalIdx != RTXDI_INVALID_PORTAL_INDEX)
    {
      // If we're using the current frame reservoir, we'll be shading it in the context of the previous frame,
      // so use previous frame's portals.
      if (!usePrevSample)
        selectedReservoir.portalIdx += uint8_t(maxRayPortalCount);

      // Check if the portal exists in the other frame
      if (!RAB_IsPortalActive(selectedReservoir.portalIdx))
        selectedMappedLightIndex = RTXDI_INVALID_LIGHT_INDEX;
    }
    
    if (selectedMappedLightIndex != RTXDI_INVALID_LIGHT_INDEX)
    {
      // If the mapping was successful, compare the lighting.

      // Load some parameters of the G-buffer surface
      const float3 worldMotion = PrimaryVirtualMotionVector[selectedPixelPos].xyz;
      const float coneRadiusAtSurface = abs(PrimaryConeRadius[selectedPixelPos]);
      const float positionError = PrimaryPositionError[selectedPixelPos];

      // Use the position error to see if the object is stationary with noisy motion vectors.
      const float maxStaticObjectMotion = positionError * kMaxStaticObjectMotionErrorScale;
      
      MinimalSurfaceInteraction minimalSurfaceInteraction;
      const bool isStaticObject = length(worldMotion) <= maxStaticObjectMotion;

      // Static objects (including surfaces with `isStatic == false` but with tiny motion vectors):
      // - Computing current sample: use the previous position to avoid false gradients from different jitter.
      // - Computing previous sample: use the current position, same reason.
      // Moving objects:
      // - Computing current sample: use the current position to avoid self-occlusion.
      // - Computing previous sample: use the previous position if the previous TLAS is available, 
      //   use the current position otherwise - all to avoid self-occlusion.
      if (usePrevSample && isStaticObject || !usePrevSample && cb.enablePreviousTLAS && !isStaticObject)
      {
        minimalSurfaceInteraction = minimalSurfaceInteractionReadFromGBuffer(
          selectedPrevPixelPos, PreviousWorldPosition_WorldTriangleNormal);
      }
      else
      {
        minimalSurfaceInteraction = minimalSurfaceInteractionReadFromGBuffer(
          selectedPixelPos, CurrentWorldPosition_WorldTriangleNormal);
      }
      
      if (minimalSurfaceInteraction.isValid)
      {
        minimalSurfaceInteraction.positionError = positionError;

        // Reconstruct the light sample
        RAB_LightSample lightSample = RAB_GetPortalSpaceLightSample(selectedMappedLightIndex,
        selectedReservoir.uv, selectedReservoir.portalIdx, minimalSurfaceInteraction, !usePrevSample);
  
        const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(selectedPixelPos, SharedFlags);

        // Calculate weighted illuminance for the other (previous or current) surface
        // using the other light sample.
        float newIlluminance = getSurfaceIlluminance(lightSample, selectedReservoir.weightSum, selectedReservoir.portalIdx,
        minimalSurfaceInteraction, coneRadiusAtSurface, geometryFlags.portalSpace, geometryFlags.objectMask, geometryFlags.isViewModel, !usePrevSample && cb.enablePreviousTLAS);

        // Convert to FP16 and back to avoid false-positive gradients due to precision loss in the 
        // CurrentRtxdiLuminance and PreviousRtxdiLuminance textures where selectedDiffSpecLum comes from.
        newIlluminance = f16tof32(f32tof16(newIlluminance));

        // Compute the gradient
        gradient.y = max(selectedIlluminance, newIlluminance);
        gradient.x = min(abs(selectedIlluminance - newIlluminance), gradient.y);
      }
      else
      {
        // Light index mapping was unsuccessful, which means the light has either appeared or disappeared.
        // Which means the gradient is 100% of the pixel's illuminance.
        gradient.xy = selectedIlluminance;
      }
    }
    else
    {
      // Light index mapping was unsuccessful, which means the light has either appeared or disappeared.
      // Which means the gradient is 100% of the pixel's illuminance.
      gradient.xy = selectedIlluminance;
    }
  }

  // Normalize the gradient.
  // Apply the "darkness bias" to avoid discarding history because of noise on very dark surfaces.
  const float normalizedGradient = saturate(gradient.x / (gradient.y + push.darknessBias));
  
  // Store the output
  const float referenceHitDistance = PrimaryHitDistance[selectedPixelPos];
  RtxdiGradients[uint3(GlobalIndex, 0)] = float2(normalizedGradient, referenceHitDistance);

  // Debug output
  if (cb.debugView == DEBUG_VIEW_RTXDI_GRADIENTS)
  {
    // Store the gradient debug output for all pixels in the stratum
    for (int yy = 0; yy < RTXDI_GRAD_FACTOR; yy++)
    for (int xx = 0; xx < RTXDI_GRAD_FACTOR; xx++)
    {
      int2 srcPixelPos = GlobalIndex * RTXDI_GRAD_FACTOR + int2(xx, yy);
      
      storeInDebugView(srcPixelPos, normalizedGradient.xxx);
    }
  }
  else if (cb.debugView == DEBUG_VIEW_NAN)
  {
    uint2 pixelIndex = GlobalIndex* RTXDI_GRAD_FACTOR;
    bool isValid = true;

    // DEBUG_VIEW_RTXDI_GRADIENTS
    isValid &= isValidValue(normalizedGradient);
    
    accumulateInDebugViewAnd(pixelIndex, isValid);
  }
}