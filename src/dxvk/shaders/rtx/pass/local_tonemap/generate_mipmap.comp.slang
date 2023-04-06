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
#include "rtx/pass/local_tonemap/local_tonemapping.slangh"

layout(binding = GENERATE_MIPMAP_INPUT)
Sampler2D InputTexture;

layout(binding = GENERATE_MIPMAP_OUTPUT)
RWTexture2D<vec4> OutputTexture;

layout(push_constant)
ConstantBuffer<GenerateMipmapArgs> cb;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  if (any(threadId >= cb.resolution))
  {
    return;
  }

  vec2 uv = (threadId + 0.5) / cb.resolution;

  if (cb.useGaussian)
  {
    // Use 3x3 gaussian kernel, which can be achieved by 4 bilinear interpolations
    vec2 dUv = 0.5 / cb.resolution;
    vec4 totalValue = 0;
    totalValue += InputTexture.SampleLevel(uv + vec2(-dUv.x, -dUv.y), 0);
    totalValue += InputTexture.SampleLevel(uv + vec2(-dUv.x, dUv.y), 0);
    totalValue += InputTexture.SampleLevel(uv + vec2(dUv.x, -dUv.y), 0);
    totalValue += InputTexture.SampleLevel(uv + vec2(dUv.x, dUv.y), 0);
    OutputTexture[threadId] = totalValue * 0.25;
  }
  else
  {
    OutputTexture[threadId] = InputTexture.SampleLevel(uv, 0);
  }
}
