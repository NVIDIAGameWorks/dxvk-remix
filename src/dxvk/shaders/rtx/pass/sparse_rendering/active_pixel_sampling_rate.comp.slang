/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/sparse_rendering/active_pixel_mask_binding_indices.h"
#include "rtx/pass/sparse_rendering/sparse_rendering.slangh"
#include "rtx/pass/sparse_rendering/per_pixel_rate.slangh"
#include "rtx/utility/geometry_flags.slangh"
#include "rtx/utility/gbuffer_helpers.slangh"
#include "rtx/utility/debug_view_helpers.slangh" // Resolves gbuffer_helpers.slangh's debug paths, unused here

// Returns the per-pixel sampling rate in [0,1] for the active sparse rendering mode.
float calculatePixelSamplingRate(uvec2 pixelCoordinate, SparseRenderingArgs args)
{
  switch (args.mode)
  {
    default:
    case SparseRenderingMode::Off:
      return 1.f;

    case SparseRenderingMode::Uniform:
      return args.pixelSamplingRate;
  }
}

// Inputs

layout(binding = ACTIVE_PIXEL_MASK_BINDING_SHARED_FLAGS_INPUT)
Texture2D<uint16_t> SharedFlags;

layout(binding = ACTIVE_PIXEL_MASK_BINDING_PRIMARY_CONE_RADIUS_INPUT)
Texture2D<float> PrimaryConeRadius;

layout(binding = ACTIVE_PIXEL_MASK_BINDING_SECONDARY_CONE_RADIUS_INPUT)
Texture2D<float> SecondaryConeRadius;

// Input-Outputs

layout(r32ui, binding = ACTIVE_PIXEL_MASK_BINDING_NRC_TRAINING_QUERY_KEY_RESERVOIR_INPUT_OUTPUT)
RWTexture2D<uint> NrcTrainingQueryKeyReservoir;

// Outputs

layout(r8, binding = ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_OUTPUT)
RWTexture2D<float> PixelSamplingRate;

[shader("compute")]
[numthreads(ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
  const uvec2 pixelCoordinate = dispatchThreadId.xy;
  if (any(pixelCoordinate >= cb.camera.resolution))
  {
    return;
  }

  float rate = 1.f;

  // When secondary surfaces are not sparsely lit, force pixels whose selected integration
  // surface is the secondary surface to full rate when enabled
  const GeometryFlags geometryFlags = geometryFlagsReadFromGBuffer(ivec2(pixelCoordinate), SharedFlags);
  const bool isSecondaryFullRate = !cb.sparseRenderingArgs.enableSparseSecondaryLighting &&
                                   geometryFlags.secondarySurfaceMask &&
                                   !geometryFlags.primarySelectedIntegrationSurface;

  // Calculate sampling rate for the inactive ones
  if (!isSecondaryFullRate)
  {
    rate = calculatePixelSamplingRate(pixelCoordinate, cb.sparseRenderingArgs);
  }

  // The rate is stored in an R8_UNORM texture (8-bit / 256 levels). Hardware UNORM
  // rounding is implementation-defined, so explicitly round-to-nearest here. This ensures the
  // value seen by every consumer is as intended, keeping the per-pixel sampling rate used for sample
  // drawing in sync with the scale used for radiance reconstruction.
  const uint quantizedRate255 = quantizeSamplingRate(rate);
  // Store the dequantized value so the mask pass reads back exactly what the activity test here used.
  const float quantizedRate = dequantizeSamplingRate(quantizedRate255);
  PixelSamplingRate[pixelCoordinate] = quantizedRate;

  // Accumulate this active candidate's key into its training cell; the mask pass picks the highest.
  // The quantized rate keeps the active test matched to the one the mask pass runs.
  // Disregard misses for integration selected surface to avoid wasting a training path activation there
  if (cb.sparseRenderingArgs.resampledNrcTrainingPaths &&
      isActivePixelFromSamplingRate(pixelCoordinate, quantizedRate, cb.sparseRenderingArgs, cb.frameIdx) &&
      !isSelectedIntegrationSurfaceGBufferMiss(ivec2(pixelCoordinate), geometryFlags, PrimaryConeRadius, SecondaryConeRadius))
  {
    const uvec2 trainingPixelCoordinate = Nrc::calculateTrainingPixelCoordinate(pixelCoordinate, cb.nrcArgs);
    if (all(trainingPixelCoordinate < uvec2(cb.nrcArgs.activeTrainingDimensions)))
    {
      const uint key = calculateNrcTrainingQueryKey(pixelCoordinate, cb.frameIdx, quantizedRate255);
      InterlockedMax(NrcTrainingQueryKeyReservoir[trainingPixelCoordinate], key);
    }
  }
}
