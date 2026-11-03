// Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
// SPDX-License-Identifier: MIT
#include "rtx/utility/camera_transport.slangh"
#include "rtx/utility/packing.slangh"

RWStructuredBuffer<float4> results;
StructuredBuffer<float> gains;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID)
{
  f16vec3 attenuation = f16vec3(1.0f);
  attenuation = accumulateCameraAttenuation(attenuation, gains[0]);
  attenuation = accumulateCameraAttenuation(attenuation, gains[1]);
  const float3 radiance = float3(0.0f, 0.25f, 0.0f) * float3(attenuation);
  results[0] = float4(attenuation, float(all(isfinite(attenuation))));
  results[1] = float4(radiance, float(all(isfinite(radiance))));
  const float3 storedAttenuation = unpackAttenuation(packAttenuation(float3(attenuation)));
  results[4] = float4(storedAttenuation, float(all(isfinite(storedAttenuation))));

  // Two mirror samples preserve emission-off visibility. Either diffuse lobe revokes it.
  bool mirrors = cameraVisibilityAfterSample(true, surfaceMaterialTypeOpaque, opaqueLobeTypeSpecularReflection);
  mirrors = cameraVisibilityAfterSample(mirrors, surfaceMaterialTypeOpaque, opaqueLobeTypeSpecularReflection);
  bool mixed = cameraVisibilityAfterSample(mirrors, surfaceMaterialTypeOpaque, opaqueLobeTypeDiffuseReflection);
  mixed = cameraVisibilityAfterSample(mixed, surfaceMaterialTypeOpaque, opaqueLobeTypeSpecularReflection);
  bool transmittedDiffuse = cameraVisibilityAfterSample(mirrors, surfaceMaterialTypeOpaque, opaqueLobeTypeDiffuseTransmission);
  bool diffuseFirst = cameraVisibilityAfterSample(false, surfaceMaterialTypeOpaque, opaqueLobeTypeSpecularReflection);
  results[2] = float4(float(mirrors), float(mixed), float(transmittedDiffuse), float(diffuseFirst));

  bool glass = cameraVisibilityAfterSample(mirrors, surfaceMaterialTypeTranslucent, translucentLobeTypeSpecularReflection);
  glass = cameraVisibilityAfterSample(glass, surfaceMaterialTypeTranslucent, translucentLobeTypeSpecularTransmission);
  bool cutout = cameraVisibilityAfterSample(mirrors, surfaceMaterialTypeOpaque, opaqueLobeTypeOpacityTransmission);
  results[3] = float4(float(glass), float(cutout), 0, 0);

  // The first diffuse-transmission sample is classified as specular for denoising only.
  const bool firstDiffuse = cameraVisibilityAfterSample(true, surfaceMaterialTypeOpaque, opaqueLobeTypeDiffuseTransmission);
  const float packedDiffuseRadius = float(float16_t(packCameraVisibilityConeRadius(0.25f, firstDiffuse)));
  const float packedMirrorRadius = float(float16_t(packCameraVisibilityConeRadius(0.0f, mirrors)));
  results[5] = float4(float(unpackCameraVisibility(packedDiffuseRadius)), abs(packedDiffuseRadius),
    float(unpackCameraVisibility(packedMirrorRadius)), abs(packedMirrorRadius));
}
