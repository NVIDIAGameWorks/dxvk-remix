/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

#include "rtx/pass/nrc/nrc_resolve_binding_indices.h"

#define ENABLE_NRC 1  // Needed to access global values guarded by the define
#include "rtx/external/NRC.slangh"
#include "nrc/include/NRCHelpers.hlsli"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/concept/ray/ray_helper.slangh"
#include "rtx/pass/raytrace_args.h"
#include "rtx/utility/gpu_printing.slangh"
#include "rtx/pass/nrc/nrc_utilities.slangh"
#include "rtx/utility/debug_view_helpers.slangh"

// Inputs

layout(binding = NRC_RESOLVE_BINDING_NRC_QUERY_PATH_INFO_INPUT)
StructuredBuffer<NrcPackedQueryPathInfo> QueryPathInfo;

layout(binding = NRC_RESOLVE_BINDING_NRC_QUERY_RADIANCE_INPUT)
StructuredBuffer<vec3> QueryRadiance;

layout(binding = NRC_RESOLVE_BINDING_NRC_TRAINING_PATH_INFO_INPUT)
StructuredBuffer<NrcPackedTrainingPathInfo> TrainingPathInfo;

layout(binding = NRC_RESOLVE_BINDING_NRC_DEBUG_TRAINING_PATH_INFO_INPUT_OUTPUT)
RWStructuredBuffer<NrcDebugTrainingPathInfo> DebugTrainingPathInfo;

layout(r16ui, binding = NRC_RESOLVE_BINDING_SHARED_FLAGS_INPUT)
Texture2D<uint16_t> SharedFlags;


layout(binding = NRC_RESOLVE_BINDING_RAYTRACE_ARGS_INPUT)
ConstantBuffer<RaytraceArgs> cb;

// Inputs / Outputs

layout(rgba16f, binding = NRC_RESOLVE_BINDING_PRIMARY_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
RWTexture2D<vec4> PrimaryDiffuseRadianceHitDistance;

layout(rgba16f, binding = NRC_RESOLVE_BINDING_PRIMARY_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
RWTexture2D<vec4> PrimarySpecularRadianceHitDistance;

layout(rgba16f, binding = NRC_RESOLVE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
RWTexture2D<vec4> IndirectRadianceHitDistance;

// Outputs

layout(rgba32f, binding = NRC_RESOLVE_BINDING_DEBUG_VIEW_TEXTURE_OUTPUT)
RWTexture2D<vec4> DebugView;

layout(binding = NRC_RESOLVE_BINDING_GPU_PRINT_BUFFER_OUTPUT) 
RWStructuredBuffer<GpuPrintBufferElement> GpuPrintBuffer;

layout(push_constant)
ConstantBuffer<NrcResolvePushConstants> push;

enum class OutputMode
{
  Add,
  Replace,
};

void writeToDebugView(
  OutputMode outputMode,
  uint2 pixelIndex,
  vec3 color)
{
  // Prevent NaNs and Infs leaking into final image
  if (NrcIsNan(color) || NrcIsInf(color))
  {
    return;
  }

  const uint samplesPerPixel = push.samplesPerPixel;

  if (samplesPerPixel > 1)
  {
    // When more samples per pixel are used, we need to average their contribution.
    color /= float(samplesPerPixel);
  }
  vec4 outputValue = vec4(color, 0);

  switch (outputMode)
  {
  case OutputMode::Add:
    outputValue += DebugView[pixelIndex];
    break;

  case OutputMode::Replace:
    break;
  }

  DebugView[pixelIndex] = outputValue;
}

// Heat map where 0->1 maps to
//   0   0.25   0.5   0.75   1
// Black->Red->Green->Blue->White
vec3 HeatMap(float val)
{
  val *= 4;
  float a = saturate(val - 0);
  float b = saturate(val - 1);
  float c = saturate(val - 2);
  float d = saturate(val - 3);
  vec3 color;
  color.r = saturate(a - b + d); //   /\_/
  color.g = saturate(b - c + d); //   _/\/
  color.b = c;                   //   __/-
  return color;
}

vec3 IndexToColor(uint x)
{
  x += x << 10;
  x ^= x >> 6;
  x += x << 3;
  x ^= x >> 11;
  x += x << 15;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;

  vec3 color;
  color.r = float(x >> 21) * (float)1.f / (float)0x7ff;
  color.g = float((x >> 10) & 0x7ff) * (float)1.f / (float)0x7ff;
  color.b = float(x & 0x7ff) * (float)1.f / (float)0x7ff;
  color *= color;
  return color;
}

void debugResolve(uint2 pixelIndex)
{
  // Early out on out-of-frame pixels
  const uint2 frameDimensions = cb.camera.resolution;
  if (any(pixelIndex >= frameDimensions))
  {
    return;
  }

  const uint samplesPerPixel = push.samplesPerPixel;

  const uint queryPathIndexBase = NrcCalculateQueryPathIndex(frameDimensions, pixelIndex, 0, samplesPerPixel);

  // Load corresponding training path info for debug visualizations
  uint2 trainingPixelIndex = Nrc::calculateTrainingPixelCoordinate(pixelIndex, cb.nrcArgs);
  const uint trainingPathIndex = NrcCalculateTrainingPathIndex(cb.nrcArgs.nrcConstants.trainingDimensions, trainingPixelIndex);
  NrcTrainingPathInfo trainingPath = NrcUnpackTrainingPathInfo(TrainingPathInfo[trainingPathIndex]);

  // Resolve output color according to set NrcResolveMode

  vec3 outputColor = 0..xxx;
  for (uint sampleIndex = 0; sampleIndex < samplesPerPixel; ++sampleIndex)
  {
    // Load path info for this pixel
    const NrcQueryPathInfo queryPath = NrcUnpackQueryPathInfo(QueryPathInfo[queryPathIndexBase + sampleIndex]);

    switch (push.resolveMode)
    {
    case NrcResolveMode::AddQueryResultToOutput:
    case NrcResolveMode::DirectCacheView:
    case NrcResolveMode::ReplaceOutputWithQueryResult:
      if (queryPath.queryBufferIndex != Nrc::kInvalidQueryBufferIndex)
      {
        outputColor += 
          NrcUnpackQueryRadiance(cb.nrcArgs.nrcConstants, QueryRadiance[queryPath.queryBufferIndex]) 
          * queryPath.prefixThroughput;
      }
      break;

    case NrcResolveMode::TrainingBounceHeatMap:
    case NrcResolveMode::TrainingBounceHeatMapSmoothed:
      // Vertex count heat map
      outputColor += HeatMap(float(trainingPath.packedData & nrcVertexCountMask) / 8.f);
      break;

    case NrcResolveMode::PrimaryVertexTrainingRadiance:
    case NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed:
    case NrcResolveMode::SecondaryVertexTrainingRadiance:
    case NrcResolveMode::SecondaryVertexTrainingRadianceSmoothed:
      if (push.debugBuffersAreEnabled)
      {
        //  Radiance passed to training at the primary/secoundary vertex
      outputColor += DebugTrainingPathInfo[trainingPathIndex].debugRadiance;
      }
      break;

    case NrcResolveMode::QueryIndex:
      outputColor += IndexToColor(queryPath.queryBufferIndex);
      break;

    case NrcResolveMode::TrainingQueryIndex:
      outputColor += IndexToColor(trainingPath.queryBufferIndex);
      break;
    }
  }

  // Determine accumulation mode 

  OutputMode outputMode;
  outputMode = OutputMode::Replace;

  switch (push.resolveMode)
  {
  case NrcResolveMode::AddQueryResultToOutput:
    outputMode = OutputMode::Add;
    break;

  case NrcResolveMode::TrainingBounceHeatMapSmoothed:
  case NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed:
  case NrcResolveMode::SecondaryVertexTrainingRadianceSmoothed:
  {
    if (push.debugBuffersAreEnabled)
    {
      // We store smoothed color data in one of our debug buffers
      vec3 previous = DebugTrainingPathInfo[trainingPathIndex].accumulation;
      outputColor = lerp(previous, outputColor, push.resolveModeAccumulationWeight);
      DebugTrainingPathInfo[trainingPathIndex].accumulation = outputColor;
      }
    }
    break;

  default:
    break;
  }

  writeToDebugView(outputMode, pixelIndex, outputColor);
}

vec3 loadIndirectRadiance(
  uint2 launchIndex,
  GeometryFlags geometryFlags)
{
  // Indirect NEE shader already split radiance into diffuse and specular resources for the primary surface
  if (geometryFlags.primarySelectedIntegrationSurface) 
  {
    if (geometryFlags.firstSampledLobeIsSpecular)
    {
      return PrimarySpecularRadianceHitDistance[launchIndex].xyz;
    }
    else
    {
      return PrimaryDiffuseRadianceHitDistance[launchIndex].xyz;
    }
  }
  else
  {
    return IndirectRadianceHitDistance[launchIndex].xyz;
  }
}

void storeIndirectRadiance(
  uint2 launchIndex,
  GeometryFlags geometryFlags,
  vec3 indirectRadiance)
{
  if (geometryFlags.primarySelectedIntegrationSurface) 
  {
    if (geometryFlags.firstSampledLobeIsSpecular)
    {
      PrimarySpecularRadianceHitDistance[launchIndex].xyz = indirectRadiance;
    }
    else
    {
      PrimaryDiffuseRadianceHitDistance[launchIndex].xyz = indirectRadiance;
    }
  }
  else
  {
    IndirectRadianceHitDistance[launchIndex].xyz = indirectRadiance;
  }

  if (cb.debugView == DEBUG_VIEW_NRC_RESOLVED_RADIANCE)
  {
    storeInDebugView(launchIndex, indirectRadiance);
  }
}

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uint2 launchIndex: SV_DispatchThreadID)
{
  if (any(launchIndex >= push.resolution))
  {
    return;
  }

  if (push.resolveMode != NrcResolveMode::AddQueryResultToOutput) 
  {
    debugResolve(launchIndex);
  }
    
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(launchIndex, SharedFlags);

  const uint sampleIndex = 0;
  const uint samplesPerPixel = push.samplesPerPixel;

  const uint pathIndex = NrcCalculateQueryPathIndex(push.resolution, launchIndex, sampleIndex, samplesPerPixel);
  const NrcQueryPathInfo queryPathInfo = NrcUnpackQueryPathInfo(QueryPathInfo[pathIndex]);

  vec3 indirectRadiance = vec3(0);

  if (push.addPathtracedRadiance)
  {
    indirectRadiance = loadIndirectRadiance(launchIndex, geometryFlags);
  }

  // Resolve NRC radiance
  if (queryPathInfo.queryBufferIndex != Nrc::kInvalidQueryBufferIndex)
  {
    const vec3 nrcRadiance = queryPathInfo.prefixThroughput * NrcUnpackQueryRadiance(
      cb.nrcArgs.nrcConstants, QueryRadiance[queryPathInfo.queryBufferIndex]);

    if (push.addNrcRadiance &&
        !NrcIsNan(nrcRadiance) && !NrcIsInf(nrcRadiance))
    {
      indirectRadiance += nrcRadiance;
    }
  }  


  storeIndirectRadiance(launchIndex, geometryFlags, indirectRadiance);
}