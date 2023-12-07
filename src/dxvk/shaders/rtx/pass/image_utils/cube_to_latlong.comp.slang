/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include "cube_to_latlong.h"

layout(binding = CUBE_TO_LATLONG_INPUT)
SamplerCube<float3> Cube;

layout(rgba16f, binding = CUBE_TO_LATLONG_OUTPUT)
RWTexture2D<float3> LatLong;

layout(push_constant)
ConstantBuffer<CubeToLatLongArgs> cb;

[shader("compute")]
[numthreads(32, 32, 1)]
void main(uint2 idx : SV_DispatchThreadID) {
  if (any(idx >= cb.extent))
    return;

  float2 latlong = (float2(idx) - cb.extent / 2.0) * cb.scale;

  latlong.y = -latlong.y;

  if (cb.transform == LatLongTransform::ZUpToOpenEXR) {
    latlong.x = -latlong.x + 3.141592653;
  }

  float3 dir = float3(cos(latlong.y) * cos(latlong.x),
                      cos(latlong.y) * sin(latlong.x),
                      sin(latlong.y));

  LatLong[idx] = Cube.Sample(dir);
}
