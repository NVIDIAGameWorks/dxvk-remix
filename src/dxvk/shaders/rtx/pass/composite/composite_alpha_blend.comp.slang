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
#include "rtx/pass/composite/composite_bindings.slangh"

#include "rtx/pass/composite/composite_args.h"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/demodulate_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/surface/alpha_blend_surface.slangh"
#include "rtx/external/NRD.slangh"
#include "rtx/utility/geometry_flags.slangh"

groupshared uint samplePixel[16 * 8];

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

  samplePixel[localIndex.y * 16 + localIndex.x] = 0xffffffff;
  GroupMemoryBarrierWithGroupSync();

  // Early out for pixels outside the camera
  if (any(thread_id >= fakeCamera.resolution))
  {
    return;
  }

  const float primaryLinearViewZ = texelFetch(PrimaryLinearViewZ, thread_id.xy, 0).x;
  const bool primaryMiss = primaryLinearViewZ == cb.primaryDirectMissLinearViewZ;
  const CameraDirections primaryRayDirections = cameraPixelCoordinateToDirection(fakeCamera, thread_id.xy);
  const vec3 viewPosition = cameraReconstructViewPosition(fakeCamera, primaryRayDirections.viewDirection, primaryLinearViewZ);
  const float viewDistance = length(viewPosition);
  vec3 cameraPosition = cameraGetWorldPosition(fakeCamera);

  AlphaBlendSurface surface = AlphaBlendSurface.createFromPacked(AlphaBlendGBuffer[thread_id]);
  vec3 surfaceLight = 0;
  vec4 debugValue = 0;
  vec3 surfacePosition = cameraPosition + surface.hitT * primaryRayDirections.worldDirection;
  if (surface.isValid())
  {
    vec3 particleNormal = surface.normal;

    RNG randomState = createRNG(thread_id, cb.frameIdx, 4);

    int2 centerPixel = thread_id;
    int2 neighborPixel = centerPixel;
    float radius = cb.stochasticAlphaBlendInitialSearchRadius;
    float closestDepth = 1e10;
    bool found = false;

    if (cb.stochasticAlphaBlendUseNeighborSearch)
    {
      for (int i = 0; i < cb.stochasticAlphaBlendSearchIteration; ++i)
      {
        if (!found)
        {
          float neighborViewZ = texelFetch(PrimaryLinearViewZ, neighborPixel, 0).x;
          const vec4 neighborNormalData = PrimaryVirtualWorldNormalPerceptualRoughness[neighborPixel.xy];
          const vec3 neighborNormal = neighborNormalData.xyz * 2-1;
          const CameraDirections neighborRayDirections = cameraPixelCoordinateToDirection(fakeCamera, neighborPixel.xy);
          float neighborDistance = abs(neighborViewZ / neighborRayDirections.viewDirection.z);
          vec3 neighborPosition = cameraPosition + neighborDistance * neighborRayDirections.worldDirection;
          float depthDiff = abs(neighborDistance - surface.hitT);
          float3 positionDiff = neighborPosition-surfacePosition;

          bool isMiss = neighborViewZ == cb.primaryDirectMissLinearViewZ;
          bool isCoplanar = (abs(dot(normalize(positionDiff), particleNormal)) < cb.stochasticAlphaBlendPlanarDifference && depthDiff < surface.hitT * 0.2);
          bool isDepthSimilar = length(positionDiff) < cb.stochasticAlphaBlendDepthDifference;
          bool isNormalSimilar = dot(neighborNormal, particleNormal) > cb.stochasticAlphaBlendNormalSimilarity;
          bool isSameGeometry = true;
          if (cb.stochasticAlphaBlendSearchTheSameObject)
          {
            uint16_t neighborGeometryHash = AlphaBlendSurface.createFromPacked(AlphaBlendGBuffer[neighborPixel]).geometryHash;
            isSameGeometry = (i == 0) || surface.geometryHash == neighborGeometryHash;
          }
          if (!isMiss && (isCoplanar || isDepthSimilar) && isNormalSimilar && isSameGeometry)
          {
            found = true;
            samplePixel[localIndex.y * 16 + localIndex.x] = (neighborPixel.x << 16) | neighborPixel.y;
          }
          else
          {
            radius *= cb.stochasticAlphaBlendRadiusExpandFactor;
          }
        }

        GroupMemoryBarrierWithGroupSync();

        if (!found)
        {
          vec2 offset = vec2(getNextSampleBlueNoise(randomState), getNextSampleBlueNoise(randomState)) * 2 - 1;

          uint neighborResult = -1;
          for (int i = 0; i < 4; ++i)
          {
            int2 idx = int2(i&1, i>>1) * 2 - 1;
            int2 neighborLocalIndex = clamp(localIndex, 1, int2(16 ,8) - 2) + idx;
            neighborResult = samplePixel[neighborLocalIndex.y * 16 + neighborLocalIndex.x];
            if (neighborResult != -1)
            {
              break;
            }
          }

          if (neighborResult != -1 && cb.stochasticAlphaBlendShareNeighbors)
          {
            neighborPixel = int2((neighborResult >> 16) & 0xffff, neighborResult & 0xffff);
            vec2 offset2 = vec2(getNextSampleBlueNoise(randomState), getNextSampleBlueNoise(randomState)) * 2 - 1;
            neighborPixel += int2(offset2 * min(60, radius * 0.15));
          }
          else
          {
            neighborPixel = clamp(centerPixel.xy + offset * radius,0,cb.resolution-1);
          }
          radius = clamp(radius, 2, 200);
        }
      }
      centerPixel = neighborPixel;

      if (found)
      {
        int2 neighborPixel = centerPixel;
        vec3 diffuseLight = 0;
        diffuseLight += PrimaryDirectDiffuseRadianceHitDistance[neighborPixel].xyz * surface.color.xyz;
        if (cb.enableSeparatedDenoisers)
        {
          diffuseLight += PrimaryIndirectDiffuseRadianceHitDistance[neighborPixel].xyz * surface.color.xyz;
        }

        float perceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[neighborPixel].a;

        float roughnessFactor = 1.0;
        if (cb.demodulateRoughness) {
          roughnessFactor = getRoughnessDemodulationFactor(perceptualRoughness, cb.roughnessDemodulationOffset);
        }

        const vec3 primarySpecularAlbedo = texelFetch(PrimarySpecularAlbedo, neighborPixel.xy, 0).xyz;
        vec3 specularLight = PrimaryDirectSpecularRadianceHitDistance[neighborPixel].xyz;
        if (cb.enableSeparatedDenoisers)
        {
          specularLight += PrimaryIndirectSpecularRadianceHitDistance[neighborPixel].xyz;
        }
        specularLight = primarySpecularAlbedo * specularLight / roughnessFactor;

        
        surfaceLight += vec3(diffuseLight + specularLight);
      }
    }

    if (cb.stochasticAlphaBlendUseRadianceVolume && !found)
    {
      uint froxelVolumeHint = froxelVolumeMain;

      bool volumeFound = false;
      vec3 physicalFroxelUVW;

      for (uint i = 0; i < volumeArgs.numActiveFroxelVolumes; ++i)
      {
        const uint froxelVolume = (i + froxelVolumeHint) % volumeArgs.numActiveFroxelVolumes;

        // Note: use the CB directly because indexing the camera array that is passed through parameters is extremely slow.
        const VolumeDefinitionCamera camera = cb.volumeArgs.cameras[froxelVolume];

        const vec3 translatedWorldPosition = worldToTranslatedWorld(camera, surfacePosition);
        // Note: Lookup lighting at the specified position in the volume radiance cache by calculating its UVW coordinates. Jittered matrix used
        // here due to Surface Interaction assumptions stated in function documentation.
        vec3 virtualFroxelUVW = translatedWorldPositionToFroxelUVW(
          camera.translatedWorldToView, camera.translatedWorldToProjectionJittered, packedFlagGet(camera.flags, rightHandedFlag),
          volumeArgs.froxelDepthSlices, volumeArgs.froxelDepthSliceDistributionExponent, volumeArgs.froxelMaxDistance, camera.nearPlane,
          translatedWorldPosition);

        virtualFroxelUVW = saturate(virtualFroxelUVW);
        if (all(virtualFroxelUVW >= 0.0f) && all(virtualFroxelUVW <= 1.0f))
        {
          physicalFroxelUVW = virtualFroxelUVWToPhysicalFroxelUVW(
            virtualFroxelUVW, froxelVolume,
            volumeArgs.minFilteredRadianceU, volumeArgs.maxFilteredRadianceU, volumeArgs.inverseNumFroxelVolumes);
          volumeFound = true;

          break;
        }
      }

      if (volumeFound)
      {
        const vec3 radiance = gammaToLinearFast(VolumeFilteredRadiance.SampleLevel(physicalFroxelUVW, 0));
        surfaceLight = radiance* surface.color.xyz * fourPi * cb.stochasticAlphaBlendRadianceVolumeMultiplier;
        found= true;
      }
    }
    if (!found)
    {
       surfaceLight = 0;
    }
  }

  AlphaBlendRadiance[thread_id.xy] = vec4(surfaceLight * surface.color.a,1);
}
