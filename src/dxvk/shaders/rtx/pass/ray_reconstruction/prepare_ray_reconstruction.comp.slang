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
#include "rtx/pass/ray_reconstruction/ray_reconstruction.h"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
#include "rtx/utility/geometry_flags.slangh"

// Inputs
layout(binding = RAY_RECONSTRUCTION_CONSTANTS_INPUT)
ConstantBuffer<RayReconstructionArgs> cb;

layout(r32ui, binding = RAY_RECONSTRUCTION_NORMALS_INPUT)
Texture2D<uint> InNormals;

layout(rgba16f, binding = RAY_RECONSTRUCTION_VIRTUAL_NORMALS_INPUT)
Texture2D<float4> InVirtualNormals;

layout(rgba16f, binding = RAY_RECONSTRUCTION_PRIMARY_INDIRECT_SPECULAR_INPUT)
Texture2D<float4> InPrimaryIndirectSpecular;

layout(binding = RAY_RECONSTRUCTION_PRIMARY_ATTENUATION_INPUT)
Texture2D<uint> PrimaryAttenuation;

layout(rgba16, binding = RAY_RECONSTRUCTION_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
Texture2D<float4> PrimaryVirtualWorldNormalPerceptualRoughness;

layout(binding = RAY_RECONSTRUCTION_SECONDARY_ALBEDO_INPUT)
Texture2D<float4> SecondaryAlbedo;

layout(rgb10_a2, binding = RAY_RECONSTRUCTION_SECONDARY_SPECULAR_ALBEDO_INPUT)
Texture2D<float4> SecondarySpecularAlbedo;

layout(binding = RAY_RECONSTRUCTION_SECONDARY_ATTENUATION_INPUT)
Texture2D<uint> SecondaryAttenuation;

layout(rgba16, binding = RAY_RECONSTRUCTION_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
Texture2D<float4> SecondaryVirtualWorldNormalPerceptualRoughness;

layout(r16ui, binding = RAY_RECONSTRUCTION_SHARED_FLAGS_INPUT)
Texture2D<uint> SharedFlags;

layout(binding = RAY_RECONSTRUCTION_COMBINED_INPUT)
Texture2D<float4> CombinedInput;

layout(rgba16f, binding = RAY_RECONSTRUCTION_NORMALS_DLSSRR_INPUT)
Texture2D<float4> InNormalsDLSSRR;

layout(r32f, binding = RAY_RECONSTRUCTION_DEPTHS_INPUT)
Texture2D<float4> InDepth;

layout(rgba16, binding = RAY_RECONSTRUCTION_MOTION_VECTOR_INPUT)
Texture2D<float4> InMotionVector;

// Outputs
layout(rgba16f, binding = RAY_RECONSTRUCTION_NORMALS_OUTPUT)
RWTexture2D<float4> OutNormals;

layout(r16f, binding = RAY_RECONSTRUCTION_HIT_DISTANCE_OUTPUT)
RWTexture2D<float> OutHitDistance;

layout(binding = RAY_RECONSTRUCTION_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> DebugView;

// Input/Outputs
layout(binding = RAY_RECONSTRUCTION_PRIMARY_ALBEDO_INPUT_OUTPUT)
RWTexture2D<float4> PrimaryAlbedo;

layout(rgb10_a2, binding = RAY_RECONSTRUCTION_PRIMARY_SPECULAR_ALBEDO_INPUT_OUTPUT)
RWTexture2D<float4> PrimarySpecularAlbedo;


float getRoughnessFactor(float roughness)
{
  roughness = roughness + 0.1;
  float factor = cb.enableDemodulateRoughness ? cb.upscalerRoughnessDemodulationMultiplier * (pow(1.0 / roughness, cb.upscalerRoughnessDemodulationOffset)) : 1.0;
  return factor;
}

// Computes the histogram of the current image and stores the result.
[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  const Camera camera = cb.camera;

  // Early out for pixels outside the camera

  if (any(threadId >= camera.resolution))
  {
    return;
  }

  float3 normal;

  // Choose normal inputs for DLSS-RR
  if (cb.enableDLSSRRInputs) {
    normal = InNormalsDLSSRR[threadId].xyz;
    imageStore(OutNormals, threadId, float4(normal, 0.0));
  } else if (cb.rayReconstructionUseVirtualNormals) {
    normal = texelFetch(InVirtualNormals, threadId, 0).xyz * 2.0 - 1.0;
    imageStore(OutNormals, threadId, float4(normal, 0.0));
  } else {
    vec2 octNormal = snorm2x16ToFloat2x32(texelFetch(InNormals, threadId, 0));
    normal = signedOctahedralToSphereDirection(octNormal);
    imageStore(OutNormals, threadId, vec4(normal, 0.0));
  }

  const float primaryPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
  const float secondaryPerceptualRoughness = SecondaryVirtualWorldNormalPerceptualRoughness[threadId].w;

  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);
  if (cb.combineSpecularAlbedo)
  {
    bool hasPSR = geometryFlags.performPSTR || geometryFlags.performPSRR;

    vec4 primaryAttenuation = vec4(r11g11b10ToColor(PrimaryAttenuation[threadId]), 1.0);
    vec4 secondaryAttenuation = hasPSR ? vec4(r11g11b10ToColor(SecondaryAttenuation[threadId]), 1.0) : vec4(0,0,0,1);

    vec4 primaryAlbedo          = PrimaryAlbedo[threadId] * primaryAttenuation;
    vec4 primarySpecularAlbedo  = PrimarySpecularAlbedo[threadId] * primaryAttenuation;

    vec4 secondaryAlbedo         = vec4(SecondaryAlbedo[threadId].xyz, 0.0) * secondaryAttenuation;
    vec4 secondarySpecularAlbedo = vec4(SecondarySpecularAlbedo[threadId].xyz, 0.0) * secondaryAttenuation;

    primarySpecularAlbedo *= getRoughnessFactor(primaryPerceptualRoughness);
    
    // Combine secondary albedo to improve reflection quality.
    if (hasPSR)
    {
      primaryAlbedo += secondaryAlbedo;
      primarySpecularAlbedo += secondarySpecularAlbedo * getRoughnessFactor(secondaryPerceptualRoughness);
    }

    // Clamp minimum albedo to improve stability
    PrimaryAlbedo[threadId] = lerp(primaryAlbedo, 1, 0.03);
    PrimarySpecularAlbedo[threadId] = primarySpecularAlbedo;
  }

  // DLSS-RR doesn't expect hitT with holes, here a simple box filter is used to provide a dense hitT input.
  // The filter calculates average hitT in 3x3 area, excluding the minimum and the maximum ones.
  float hitDistance = InPrimaryIndirectSpecular[threadId].a;
  if (cb.filterHitT)
  {
    const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);
    // Calculate average specular hitT
    float totalHitT = 0;
    float totalWeight = 0;
    float minHitT = 1e10;
    float maxHitT = 0;
    for (int i = 0; i < 9; ++i)
    {
      int2 offset = int2(i % 3, i / 3) - 1;
      const GeometryFlags neighborFlags = geometryFlagsReadFromGBuffer(threadId + offset, SharedFlags);
      if (neighborFlags.firstSampledLobeIsSpecular)
      {
        float hitT = InPrimaryIndirectSpecular[threadId + offset].a;
        totalHitT += hitT;
        totalWeight += 1;
        minHitT = min(minHitT, hitT);
        maxHitT = max(maxHitT, hitT);
      }
    }

    float hitT = hitDistance;
    if (!geometryFlags.firstSampledLobeIsSpecular && totalWeight > 0)
    {
      // If this pixel doesn't have specular hitT, use average specular hitT from neighbors
      hitT = totalHitT / totalWeight;
    }
    else if ((hitDistance == minHitT || hitDistance == maxHitT) && totalWeight > 2)
    {
      // Remove minimum or maximum hitT and calculate average hitT
      totalHitT -= minHitT + maxHitT;
      totalWeight -= 2;
      hitT = totalHitT / totalWeight;
    }
    hitDistance = hitT;
  }
  OutHitDistance[threadId] = hitDistance;

  switch(cb.debugViewIdx)
  {
  case DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO:
    storeInDebugView(ivec2(threadId), PrimaryAlbedo[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO:
    storeInDebugView(ivec2(threadId), PrimarySpecularAlbedo[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_HIT_DISTANCE:
    storeInDebugView(ivec2(threadId), hitDistance / 1000.0);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL:
    storeInDebugView(ivec2(threadId), OutNormals[threadId]);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH:
    storeInDebugView(ivec2(threadId), InDepth[threadId].x);
    break;
  case DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR:
    storeInDebugView(ivec2(threadId), InMotionVector[threadId].xyz);
    break;
  }
}
