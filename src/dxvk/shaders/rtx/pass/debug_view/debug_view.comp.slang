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
#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx/pass/debug_view/debug_view_binding_indices.h"
#include "rtx/utility/geometry_flags.slangh"

layout(rgba32f, binding = DEBUG_VIEW_BINDING_INPUT_OUTPUT)
RWTexture2D<float4> DebugView;

layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedPrimaryDirectDiffuseRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedPrimaryDirectSpecularRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedSecondaryCombinedDiffuseRadianceHitT;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT)
Texture2D<float4> DenoisedSecondaryCombinedSpecularRadianceHitT;
layout(r16ui, binding = DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT)
Texture2D<uint> SharedFlags;
layout(r32f, binding = DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
Texture2D<float> PrimaryLinearViewZ;
layout(rgba16, binding = DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT)
Texture2D<float4> PrimaryVirtualWorldNormalPerceptualRoughness;
layout(rgba16, binding = DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT)
Texture2D<float4> PrimaryVirtualMotionVector;
layout(rg16, binding = DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT)
Texture2D<float2> PrimaryScreenSpaceMotionVector;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT)
Texture2D<float4> RtxdiConfidence;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT)
Texture2D<float4> FinalShading;
layout(r32ui, binding = DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT)
Texture2D<uint> Instrumentation;
layout(rgba16f, binding = DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT)
Texture2D<float4> CompositeOutput;

layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformRedInputOutput;
layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformGreenInputOutput;
layout(r32ui, binding = DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT)
RWTexture2D<uint> HDRWaveformBlueInputOutput;

layout(binding = DEBUG_VIEW_BINDING_CONSTANTS_INPUT)
ConstantBuffer<DebugViewArgs> cb;

#include "rtx/utility/common.slangh"
#include "rtx/utility/color.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/debug_view_helpers.slangh"
//#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/external/NRD.slangh"

float nrdGetHitT(float normalizedHitT, float primaryHitPerceptualRoughness, float linearViewZ)
{
  if (cb.nrd.isReblurEnabled > 0)
  {
    return REBLUR_GetHitDist(normalizedHitT, linearViewZ, cb.nrd.hitDistanceParams, primaryHitPerceptualRoughness);
  }
  else
  {
    return normalizedHitT;
  }
}

vec4 nrdDenoisedHitTtoColor(ivec2 threadId)
{
  float normalizedHitT;
  float primaryHitPerceptualRoughness;

  switch (cb.debugViewIdx) {
  
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
    normalizedHitT = DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].w;
    primaryHitPerceptualRoughness = 1.0;
    break;
  
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
    normalizedHitT = DenoisedPrimaryDirectSpecularRadianceHitT[threadId].w;
    primaryHitPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
    break;

  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
    normalizedHitT = DebugView[threadId].w;
    primaryHitPerceptualRoughness = 1.0;
    break;

  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    normalizedHitT = DebugView[threadId].w;
    primaryHitPerceptualRoughness = PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w;
    break;
  }

  if (cb.nrd.isReblurEnabled > 0)
  {
    float linearViewZ = PrimaryLinearViewZ[threadId].x;
    return vec4(vec3(REBLUR_GetHitDist(normalizedHitT, linearViewZ, cb.nrd.hitDistanceParams, primaryHitPerceptualRoughness)), 1.0);
  }
  else
  {
    return vec4(vec3(normalizedHitT), 1.0);
  }
}

vec4 unormVectorToColor(vec3 unormVector)
{
  return vec4(unormVector * 2.0 - 1.0, 1.0); 
}

vec4 loadInput(ivec2 threadId)
{
  vec4 value;
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(threadId, SharedFlags);

  // Compute the coordinate with respect to the upscaled output
  // Note: This is used so textures (primarily just the final output) using the upscaled extent
  // rather than the downscaled extent can be displayed through the current debug view system.
  // This does mean that the image will be downscaled in a fairly poor nearest neighbor
  // way into the debug view texture, meaning when viewing the final output through the debug
  // view it is recommended to disable DLSS (or whatever other upscaler) until this is changed
  // to something better (the debug view texture ideally should be at the full output resolution).

  uvec2 upscaledResolution;
  FinalShading.GetDimensions(upscaledResolution.x, upscaledResolution.y);

  const ivec2 upscaledPixelCoordinate = (threadId * upscaledResolution) / cb.debugViewResolution;

  // Get the respective input based on the debug index

  switch (cb.debugViewIdx) {
  case DEBUG_VIEW_VIRTUAL_SHADING_NORMAL:
    value = unormVectorToColor(PrimaryVirtualWorldNormalPerceptualRoughness[threadId].xyz);
    break;
  case DEBUG_VIEW_VIRTUAL_MOTION_VECTOR:
    value = vec4(abs(PrimaryVirtualMotionVector[threadId].xyz), 1);
    break;
  case DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR:
    value = vec4(abs(PrimaryScreenSpaceMotionVector[threadId].xy), 0, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
    value = vec4(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE:
    value = vec4(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
    value = vec4(DebugView[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
    value = vec4(DebugView[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE:
    value = vec4(DenoisedSecondaryCombinedDiffuseRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE:
    value = vec4(DenoisedSecondaryCombinedSpecularRadianceHitT[threadId].xyz, 1);
    break;
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
  case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
    value = nrdDenoisedHitTtoColor(threadId);
    break;
  case DEBUG_VIEW_PRE_TONEMAP_OUTPUT:
  case DEBUG_VIEW_POST_TONEMAP_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT:
  case DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT:
    value = FinalShading[upscaledPixelCoordinate];
    break;
  case DEBUG_VIEW_COMPOSITE_OUTPUT:
    value = CompositeOutput[threadId];
    break;
  case DEBUG_VIEW_VIEW_MODEL:
    if (geometryFlags.isViewModel)
    {
      value = FinalShading[upscaledPixelCoordinate];
    }
    else
    {
      // Checkerboard background
      const bool state1 = ((threadId.x >> 3) & 1) ^ ((threadId.y >> 3) & 1);
      value = vec4(0.25, 0.25, 0.25, 1) + state1 * vec4(0.25, 0.25, 0.25, 0);
    }
    break;
  case DEBUG_VIEW_PSR_PRIMARY_SECONDARY_SURFACE_MASK:
    value = geometryFlags.primarySelectedIntegrationSurface ? vec4(1, 1, 1, 1) : vec4(0, 0, 0, 1);
    break;
  case DEBUG_VIEW_EXPOSURE_HISTOGRAM:
    // Show histogram or weight as a bar across the top on the screen, technically not safe, but the first handful of threads should be fine.
    if (threadId.y < 32 && threadId.x < cb.debugViewResolution.x * 0.5)
    {
      const uint histogramBucket = (float(threadId.x) / (cb.debugViewResolution.x * 0.5)) * 256;
      vec4 histogramOrWeight = DebugView[uint2(histogramBucket, 0)];
      if (threadId.y < 16)
      {
        // Show weight
        value = float4(0,histogramOrWeight.yz,0);
      }
      else
      {
        // Show weighted histogram
        value = histogramOrWeight.y > 0 ? float4(0,1,0,0) : histogramOrWeight.xxxx;
      }
      
      if (histogramBucket == 0 && value.x > 0)
      {
        value = float4(1, 0, 0, 0); // Red, we have pixels that are in the ignore bucket.
      }
    }
    else
        value = FinalShading[upscaledPixelCoordinate];
    break;
  case DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE:
    value = float4(Instrumentation[threadId], 0, 0, 0);
    break;
  case DEBUG_VIEW_RTXDI_CONFIDENCE:
    if (cb.isRTXDIConfidenceValid) 
    {
      const float confidence = saturate(1.0 - RtxdiConfidence[threadId].x);
      const float confidenceAlpha = pow(confidence, 0.5);
      const float3 confidenceColor = turboColormap(confidence);
      const float3 sceneColor = FinalShading[upscaledPixelCoordinate].rgb;
      value.rgb = lerp(sceneColor, confidenceColor, confidenceAlpha);
      value.a = 1.0;
    }
    break;
  case DEBUG_VIEW_SCROLLING_LINE:
    value = FinalShading[upscaledPixelCoordinate];
    if (threadId.x == (cb.frameIdx + 10) % cb.debugViewResolution.x)
      value = float4(0, 1, 1, 1);
    break;
  case DEBUG_VIEW_NAN:
    bool isValid = readInDebugView(threadId).x != 0.0;
    // DEBUG_VIEW_VIRTUAL_SHADING_NORMAL
    isValid &= isValidValue(unormVectorToColor(PrimaryVirtualWorldNormalPerceptualRoughness[threadId].xyz));
    // DEBUG_VIEW_VIRTUAL_MOTION_VECTOR
    isValid &= isValidValue(PrimaryVirtualMotionVector[threadId].xy);
    // DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR
    isValid &= isValidValue(PrimaryScreenSpaceMotionVector[threadId].xy);
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE
    isValid &= isValidValue(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE
    isValid &= isValidValue(DebugView[threadId].xyz);
    // DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE
    isValid &= isValidValue(DenoisedSecondaryCombinedDiffuseRadianceHitT[threadId].xyz);
    // DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE
    isValid &= isValidValue(DenoisedSecondaryCombinedSpecularRadianceHitT[threadId].xyz);

    float linearViewZ = PrimaryLinearViewZ[threadId].x;
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T
    isValid &= isValidValue(nrdGetHitT(DenoisedPrimaryDirectDiffuseRadianceHitT[threadId].w, 1.0, linearViewZ));
    // DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(nrdGetHitT(DenoisedPrimaryDirectSpecularRadianceHitT[threadId].w, PrimaryVirtualWorldNormalPerceptualRoughness[threadId].w, linearViewZ));
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T
    // DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T
    isValid &= isValidValue(nrdGetHitT(DebugView[threadId].w, 1.0, linearViewZ));
    // DEBUG_VIEW_PRE_TONEMAP_OUTPUT
    // DEBUG_VIEW_POST_TONEMAP_OUTPUT
    // DEBUG_VIEW_VIEW_MODEL
    // DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT
    // DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT
    isValid &= isValidValue(FinalShading[upscaledPixelCoordinate]);
    // DEBUG_VIEW_COMPOSITE_OUTPUT
    isValid &= isValidValue(CompositeOutput[threadId]);
    // DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE
    isValid &= isValidValue(Instrumentation[threadId]);
    
    if (cb.isRTXDIConfidenceValid) 
    {
      // DEBUG_VIEW_RTXDI_CONFIDENCE
      isValid &= isValidValue(RtxdiConfidence[threadId].x);
    }

    // If there is invalid number, output NaN to trigger NaN highlight effect 
    value = isValid ? 1.0 : 0.0 / 0.0;
    break;
  default:
    value = DebugView[threadId];
  };

  return value;
}

void hdrWaveformOutput(ivec2 threadId, RWTexture2D<uint> HDRWaveformInputOutput, float normalizedOutCoordinate)
{
  // Early out if the coordinate is out of range
  // Note: Using >= 1 as the coordinate value is floored, meaning a value of 1 would be one pixel outside the output texture.

  if (normalizedOutCoordinate >= 1.0f || normalizedOutCoordinate < 0.0f)
  {
    return;
  }

  const uvec2 outCoordinate = uvec2(
    uint(threadId.x / cb.hdrWaveformResolutionScaleFactor),
    uint(normalizedOutCoordinate * cb.hdrWaveformResolution.y));

  uint dummyOldValue;
  InterlockedAdd(HDRWaveformInputOutput[outCoordinate], 1, dummyOldValue);
}

void postprocess(ivec2 threadId, inout vec4 outValue)
{
  // Color code inf, nan values
  if (cb.enableInfNanViewFlag)
  { 
    bool nanDetected = false;
    bool infDetected = false;

    // Detect nans and infs in pixel neighborhood
    if (cb.colorCodeRadius > 0) 
    {
      int iRadius = cb.colorCodeRadius;
      for (int y = threadId.y - iRadius; y <= threadId.y + iRadius; y++ ) {
        for (int x = threadId.x - iRadius; x <= threadId.x + iRadius; x++ ) {
          if (any(ivec2(x, y) >= cb.debugViewResolution) || any(ivec2(x, y) < 0)) continue;
          const vec4 temp = loadInput(ivec2(x, y));

          nanDetected |= any(isnan(temp));
          infDetected |= any(isinf(temp));
        }
      }
    }

    if (nanDetected || infDetected)
    {
      const bool usePseudoColorWarningColors = (cb.displayType == displayTypeStandard) && cb.enablePseudoColorFlag;
      const vec3 kNanWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(1, 0, 0);
      const vec3 kInfWarningColor = usePseudoColorWarningColors ? vec3(0, 0, 0) : vec3(0, 0, 1);
      const vec3 kCombinedWarningColor =
        nanDetected ? kNanWarningColor : vec3(0, 0, 0) +
        infDetected ? kInfWarningColor : vec3(0, 0, 0);

      outValue = cb.animationTimeSec > 0 
                ? vec4(sin(cb.animationTimeSec * 8) * kCombinedWarningColor, 1) // flash warning color
                : vec4(kCombinedWarningColor, 1);
      return;
    }
  }

  // Switch between Display mode

  if (cb.displayType == displayTypeStandard)
  {
    outValue *= cb.scale;
    outValue = (outValue - vec4(cb.minValue)) / (vec4(cb.maxValue - cb.minValue));

    if (!cb.enableAlphaChannelFlag)
      outValue.a = 1;

    if (cb.enablePseudoColorFlag)
      outValue.rgb = turboColormap(calcBt709Luminance(outValue.rgb));
  }
  else if (cb.displayType == displayTypeBGRExclusiveColor)
  {
    outValue.rgb = colorCodeIntoBGRexclusive(outValue.r, cb.maxValue);
  }
  else if (cb.displayType == displayTypeEV100)
  {
    const float luminance = calcBt709Luminance(outValue.rgb);
    const float ev100 = calcLuminanceEV100(luminance);
    const float normalizedEV = float(ev100 - cb.evMinValue) / cb.evRange;

    outValue = vec4(turboColormap(normalizedEV), 1.0f);
  }
  else if (cb.displayType == displayTypeHDRWaveform)
  {
    vec3 log10OutValue;

    if (cb.enableLuminanceModeFlag)
    {
      log10OutValue = log10(calcBt709Luminance(outValue.rgb));
    }
    else
    {
      log10OutValue = log10(outValue.rgb);
    }

    const vec3 normalizedOutCoordinate = (log10OutValue - cb.log10MinValue) / cb.log10Range;

    hdrWaveformOutput(threadId, HDRWaveformRedInputOutput, normalizedOutCoordinate.r);
    hdrWaveformOutput(threadId, HDRWaveformGreenInputOutput, normalizedOutCoordinate.g);
    hdrWaveformOutput(threadId, HDRWaveformBlueInputOutput, normalizedOutCoordinate.b);
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.debugViewResolution))
    return;

  vec4 outValue = loadInput(threadId);

  // Post-processing and overrides if applicable
  postprocess(threadId, outValue);

  imageStore(DebugView, threadId, outValue);
}
