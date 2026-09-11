/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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

// Inputs

layout(binding = ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_INPUT)
Texture2D<float> PixelSamplingRate;

// Input-Outputs

layout(r32ui, binding = ACTIVE_PIXEL_MASK_BINDING_NRC_TRAINING_QUERY_KEY_RESERVOIR_INPUT_OUTPUT)
RWTexture2D<uint> NrcTrainingQueryKeyReservoir;

layout(r32ui, binding = ACTIVE_PIXEL_MASK_BINDING_NRC_TRAINING_QUERY_PIXEL_INPUT_OUTPUT)
RWTexture2D<uint> NrcTrainingQueryPixel;

// Outputs

layout(binding = ACTIVE_PIXEL_MASK_BINDING_ACTIVE_PIXEL_MASK_OUTPUT)
RWTexture2D<uint> ActivePixelMask;

[shader("compute")]
[numthreads(ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
  const uvec2 maskCoordinate = dispatchThreadId.xy;

  // The dispatch is rounded up to the threadgroup size, but the mask buffer is rounded up to the
  // block size. Early-exit threads on the margin to avoid out-of-bounds writes.
  if (any(maskCoordinate >= cb.sparseRenderingArgs.activePixelMaskExtent))
  {
    return;
  }

  const uvec2 basePixelCoordinate = maskCoordinate * uvec2(ACTIVE_PIXEL_MASK_BLOCK_WIDTH, ACTIVE_PIXEL_MASK_BLOCK_HEIGHT);

  uint activeMask = 0;

  [unroll]
  for (uint y = 0; y < ACTIVE_PIXEL_MASK_BLOCK_HEIGHT; ++y)
  {
    [unroll]
    for (uint x = 0; x < ACTIVE_PIXEL_MASK_BLOCK_WIDTH; ++x)
    {
      const uvec2 pixelCoordinate = basePixelCoordinate + uvec2(x, y);
      const bool inBounds = all(pixelCoordinate < cb.camera.resolution);

      bool active = false;
      if (inBounds)
      {
        const float rate = PixelSamplingRate[pixelCoordinate];
        active = isActivePixelFromSamplingRate(pixelCoordinate, rate, cb.sparseRenderingArgs, cb.frameIdx);

        // Record the cell's selected query pixel: active, and the winner of the cell's key reservoir.
        // The stored rate is deliberately left alone, so it stays the 1/rate divisor demodulate and composite use.
        // Candidacy is not re-tested here: only candidates insert a key, and the cleared reservoir is 0 while keys
        // start at 1. A non-candidate can still collide with the winning key, which costs the cell one frame of
        // training once Integrate Direct sees its selected surface missed and invalidates the path.
        if (cb.sparseRenderingArgs.resampledNrcTrainingPaths && active)
        {
          const uvec2 trainingPixelCoordinate = Nrc::calculateTrainingPixelCoordinate(pixelCoordinate, cb.nrcArgs);
          // Recompute the sampling-rate pass' key to find the cell's winner, from the quantized rate so the two keys are bit-identical.
          const uint quantizedRate255 = quantizeSamplingRate(rate);
          if (all(trainingPixelCoordinate < uvec2(cb.nrcArgs.activeTrainingDimensions)) &&
              calculateNrcTrainingQueryKey(pixelCoordinate, cb.frameIdx, quantizedRate255) == NrcTrainingQueryKeyReservoir[trainingPixelCoordinate])
          {
            NrcTrainingQueryPixel[trainingPixelCoordinate] = Nrc::packNrcTrainingQueryPixel(pixelCoordinate);
          }
        }
      }

      const uint bitIndex = y * ACTIVE_PIXEL_MASK_BLOCK_WIDTH + x;
      activeMask |= active << bitIndex;
    }
  }

  ActivePixelMask[maskCoordinate] = activeMask;
}
