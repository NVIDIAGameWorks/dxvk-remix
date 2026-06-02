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

layout(binding = ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_INPUT)
Texture2D<float> DirectPixelSamplingRate;

layout(binding = ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_INPUT)
Texture2D<float> IndirectPixelSamplingRate;

// Outputs

layout(binding = ACTIVE_PIXEL_MASK_BINDING_DIRECT_ACTIVE_PIXEL_MASK_OUTPUT)
RWTexture2D<uint> DirectActivePixelMask;

layout(binding = ACTIVE_PIXEL_MASK_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_OUTPUT)
RWTexture2D<uint> IndirectActivePixelMask;

layout(binding = ACTIVE_PIXEL_MASK_BINDING_UNION_ACTIVE_PIXEL_MASK_OUTPUT)
RWTexture2D<uint> UnionActivePixelMask;

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

  uint directMask = 0;
  uint indirectMask = 0;
  uint unionMask = 0;

  for (uint y = 0; y < ACTIVE_PIXEL_MASK_BLOCK_HEIGHT; ++y)
  {
    for (uint x = 0; x < ACTIVE_PIXEL_MASK_BLOCK_WIDTH; ++x)
    {
      const uvec2 pixelCoordinate = basePixelCoordinate + uvec2(x, y);
      const bool inBounds = all(pixelCoordinate < cb.camera.resolution);

      bool directActive = false;
      bool indirectActive = false;
      if (inBounds)
      {
        float unusedScale = 1.f;
        directActive = isActivePixelForDirect(pixelCoordinate, DirectPixelSamplingRate[pixelCoordinate], cb.sparseRenderingArgs, cb.frameIdx, unusedScale);
        indirectActive = isActivePixelForIndirect(pixelCoordinate, IndirectPixelSamplingRate[pixelCoordinate], cb.sparseRenderingArgs, cb.frameIdx, unusedScale);
      }

      const uint bitIndex = y * ACTIVE_PIXEL_MASK_BLOCK_WIDTH + x;
      directMask |= directActive << bitIndex;
      indirectMask |= indirectActive << bitIndex;
      unionMask |= (directActive || indirectActive) << bitIndex;
    }
  }

  DirectActivePixelMask[maskCoordinate] = directMask;
  IndirectActivePixelMask[maskCoordinate] = indirectMask;
  UnionActivePixelMask[maskCoordinate] = unionMask;
}
