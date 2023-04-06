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
#include "rtx/pass/post_fx/post_fx.h"
#include "rtx/pass/post_fx/post_fx_motion_blur_geometry_flags.slangh"
#include "rtx/utility/math.slangh"
#include "rtx/utility/packing.slangh"

layout(r8ui, binding = POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_INPUT)
Texture2D<uint> PrimarySurfaceFlags;

layout(r8ui, binding = POST_FX_MOTION_BLUR_PREFILTER_PRIMARY_SURFACE_FLAGS_FILTERED_OUTPUT)
RWTexture2D<uint> PrimarySurfaceFlagsFiltered;

layout(push_constant)
ConstantBuffer<PostFxMotionBlurPrefilterArgs> cb;

[shader("compute")]
[numthreads(POST_FX_TILE_SIZE, POST_FX_TILE_SIZE, 1)]
void main(in uint2 pixelPos : SV_DispatchThreadID)
{
  if (any(pixelPos >= cb.imageSize))
  {
    return;
  }

  // Filter motion blur surface flags
  MotionBlurSurfaceFlags centerMotionBlurSurfaceFlags = motionBlurSurfaceFlagsReadFromGBuffer(PrimarySurfaceFlags[pixelPos]);
  [unroll]
  for (int i = -1; i < 1; ++i)
  {
    const int2 samplePos = int2(pixelPos) + i * cb.pixelStep;
    const uint encodedFlags = PrimarySurfaceFlags[samplePos];
    MotionBlurSurfaceFlags motionBlurSurfaceFlags = motionBlurSurfaceFlagsReadFromGBuffer(encodedFlags);

    // Don't do 3x3 region check for static, because dynamic objects have mild motion blur compare to the statics

    centerMotionBlurSurfaceFlags.isEmissive = motionBlurSurfaceFlags.isEmissive ? true : centerMotionBlurSurfaceFlags.isEmissive;
    centerMotionBlurSurfaceFlags.isViewModel = motionBlurSurfaceFlags.isViewModel ? true : centerMotionBlurSurfaceFlags.isViewModel;
  }

  motionBlurSurfaceFlagsWriteToGBuffer(centerMotionBlurSurfaceFlags, ivec2(pixelPos), PrimarySurfaceFlagsFiltered);
}
