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
#include "rtx/pass/dlss/dlss.h"
#include "rtx/utility/packing.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/concept/camera/camera.slangh"
#include "rtx/utility/debug_view_indices.h"

layout(binding = DLSS_CONSTANTS)
ConstantBuffer<DLSSArgs> cb;

layout(r32ui, binding = DLSS_NORMALS_INPUT)
Texture2D<uint> InNormals;

layout(rgba16f, binding = DLSS_VIRTUAL_NORMALS_INPUT)
Texture2D<float4> InVirtualNormals;

layout(rgba16f, binding = DLSS_NORMALS_OUTPUT)
RWTexture2D<float4> OutNormals;

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

  const vec2 octNormal = snorm2x16ToFloat2x32(texelFetch(InNormals, threadId, 0));
  const float3 normal = signedOctahedralToSphereDirection(octNormal);
  imageStore(OutNormals, threadId, vec4(normal, 0.0));
}
