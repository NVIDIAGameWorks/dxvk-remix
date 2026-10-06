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

// Input-Outputs

layout(r32ui, binding = ACTIVE_PIXEL_MASK_BINDING_NRC_TRAINING_QUERY_RESERVOIR_INPUT_OUTPUT)
RWTexture2D<uint> NrcTrainingQueryReservoir;

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

  const float rate = calculatePixelSamplingRate(pixelCoordinate, cb.sparseRenderingArgs);

  // The rate is stored in an R8_UNORM texture (8-bit / 256 levels). Hardware UNORM
  // rounding is implementation-defined, so explicitly round-to-nearest here. This ensures the
  // value seen by every consumer is as intended, keeping the per-pixel sampling rate used for sample
  // drawing in sync with the scale used for radiance reconstruction.
  // Store the dequantized value so the mask pass reads back exactly what the activity test here used.
  const float quantizedRate = dequantizeSamplingRate(quantizeSamplingRate(rate));
  PixelSamplingRate[pixelCoordinate] = quantizedRate;

  // Enter this active candidate into its training cell's reservoir, carrying the pixel that drew the key.
  // The quantized rate keeps the active test matched to the one the mask pass runs.
  if (cb.sparseRenderingArgs.resampledNrcTrainingPaths &&
      isActivePixelFromSamplingRate(pixelCoordinate, quantizedRate, cb.sparseRenderingArgs, cb.frameIdx))
  {
    const uvec2 trainingPixelCoordinate = Nrc::calculateTrainingPixelCoordinate(pixelCoordinate, cb.nrcArgs);
    if (all(trainingPixelCoordinate < uvec2(cb.nrcArgs.activeTrainingDimensions)))
    {
      const uvec2 cellOrigin =
        Nrc::calculateTrainingCellOriginInQuerySpace(trainingPixelCoordinate, cb.nrcArgs);
      const uvec2 queryPixelOffsetInCell = pixelCoordinate - cellOrigin;

      // Only enter candidates the offset field can address. Storing a wrapped offset would hand the cell's training
      // path to a different pixel, which may not even be active this frame.
      if (Nrc::isNrcTrainingQueryPixelOffsetRepresentable(queryPixelOffsetInCell))
      {
        const uint key = calculateNrcTrainingQueryKey(pixelCoordinate, cb.frameIdx, quantizedRate);

        InterlockedMax(NrcTrainingQueryReservoir[trainingPixelCoordinate],
          Nrc::packNrcTrainingQueryReservoirEntry(key, queryPixelOffsetInCell));
      }
    }
  }
}
