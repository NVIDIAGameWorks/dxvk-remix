/*
* Copyright (c) 2022-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/common_bindings.slangh"
#include "rtx/pass/volumetrics/volume_preintegrate_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/concept/camera/camera.slangh"

float3 froxelIndexToTranslatedWorldPosition(uint3 voxelIndex, VolumeArgs volumeArgs, VolumeDefinitionCamera camera)
{
  const float3 froxelCoordinate = froxelIndexToFroxelCoordinate(voxelIndex);
  return froxelCoordinateToTranslatedWorldPosition(
    camera.projectionToTranslatedWorld, camera.viewToProjection, packedFlagGet(camera.flags, rightHandedFlag),
    volumeArgs.inverseFroxelGridDimensions, volumeArgs.froxelDepthSlices,
    volumeArgs.froxelDepthSliceDistributionExponent, volumeArgs.froxelMaxDistance, camera.nearPlane,
    froxelCoordinate);
}

// Returns the T value of a ray intersecting a portal, or a negative value if there is no valid intersection.
float getRayPortalIntersection(float3 origin, float3 direction, RayPortalHitInfo portal)
{
  const float NdotD = dot(direction, portal.normal);
  float t = dot(portal.centroid - origin, portal.normal) / NdotD;
  if (isinf(t) || isnan(t))
  {
    return -1.0;
  }

  if (t > 0 && NdotD < 0)
  {
    // Compute the UV coordinates of the ray-portal intersection
    vec3 p = origin + direction * t;
    vec3 cToP = p - portal.centroid.xyz;
    vec2 uv;
    uv.x = dot(cToP, portal.xAxis.xyz) * portal.inverseHalfWidth;
    uv.y = dot(cToP, portal.yAxis.xyz) * portal.inverseHalfHeight;

    // Consider only rays that fall inside the portal oval to be intersecting with the portal.
    // This minimizes interpolation artifacts when the preintegrated volumetric texture is sampled
    // near the edge of the portal, or rather, hides them under the burning portal edge mask.
    if (length(uv) < 0.95)
    {
      return t;
    }
  }

  return -1.0;
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint3 ipos : SV_DispatchThreadID)
{
  ivec2 threadIndex = ipos.xy;
  const VolumeArgs volumeArgs = cb.volumeArgs;
  const uvec3 froxelDimensions = uvec3(volumeArgs.froxelGridDimensions, volumeArgs.froxelDepthSlices);

  if (any(threadIndex.xy >= froxelDimensions.xy))
  {
    return;
  }
  
  const VolumeDefinitionCamera camera = volumeArgs.cameras[froxelVolumeMain];

  // Set up for a portal transition
  float portalIntersectionT = -1.0;
  uint froxelVolumeAfterPortal = 0;
  int currentFroxelVolume = froxelVolumeMain;

  // Evaluate parameters for a portal transition if there are multiple volumes
  if (volumeArgs.numActiveFroxelVolumes > 1)
  {
    // Construct a ray that represents the current column of froxels
    const float3 lastFroxelTranslatedWorldPosition = froxelIndexToTranslatedWorldPosition(
      uint3(threadIndex, froxelDimensions.z - 1), volumeArgs, camera);

    const float3 rayOrigin = camera.translatedWorldOffset;
    const float3 rayDirection = normalize(lastFroxelTranslatedWorldPosition);

    // Find a portal that is intersected by the ray
    for (uint portalIndex = 0; portalIndex < cb.numActiveRayPortals; ++portalIndex)
    {
      portalIntersectionT = getRayPortalIntersection(rayOrigin, rayDirection, cb.rayPortalHitInfos[portalIndex]);
      if (portalIntersectionT > 0)
      {
        froxelVolumeAfterPortal = portalIndex + froxelVolumePortal0;
        break;
      }
    }
  }


  // Raymarch over the froxel grid

  float3 preintegratedRadiance = float3(0.0f, 0.0f, 0.0f);

  for (uint z = 0; z < froxelDimensions.z; z++)
  {
    const uint3 currentVoxelIdx = uint3(threadIndex, z);
    const float3 currentTranslatedWorldPosition = froxelIndexToTranslatedWorldPosition(currentVoxelIdx, volumeArgs, camera);

    // Note: While the froxel position is in translated world space, this is simply the length of the vector (implicitly from the origin).
    const float currentFroxelDistance = length(currentTranslatedWorldPosition);

    // See if the ray has crossed a portal
    if (currentFroxelDistance > portalIntersectionT && portalIntersectionT > 0)
    {
      // Switch to the volume that represents this portal.
      // This is sufficient to get correct lighting sampled from that volume, as long as
      // the per-portal volumes have the same projection parameters as the main volume.
      // No froxel reprojection is necessary because the volume is constructed using this portal.
      currentFroxelVolume = froxelVolumeAfterPortal;
      // Prevent further switches
      portalIntersectionT = -1;
    }

    // Calculate new radiance for this marching step

    // Note: No accumulation of the attenuation coefficient along the path needed right now as with homogenous
    // mediums it is not needed (with heterogenous it'd have to be accumulated over the raymarching process).
    const float3 attenuation = evalBeerLambertAttenuation(volumeArgs.attenuationCoefficient, currentFroxelDistance);
    // Note: Currently the 1/4pi phase function of isotropic scattering is already baked into the light contribution, in the
    // future though we'll need an actual phase function here to convolve with a spherical harmonic distribution. Additionally
    // convert from gamma back to linear before accumulating.
    const uint3 froxelVolumeOffset = uint3(currentFroxelVolume * volumeArgs.froxelGridDimensions.x, 0, 0);
    const float3 inscattering = volumeArgs.scatteringCoefficient * (gammaToLinearFast(
      FilteredAccumulatedRadiance[currentVoxelIdx + froxelVolumeOffset]) + volumeArgs.multiScatteringEstimate);
    
    preintegratedRadiance += (inscattering - inscattering * attenuation) / volumeArgs.attenuationCoefficient;

    // Set into the preintegrated radiance output

    // Note: Encode to gamma for better perceptual filtering when reading in compositing.
    RWPreintegratedRadiance[currentVoxelIdx] = linearToGammaFast(preintegratedRadiance);
  }
}
