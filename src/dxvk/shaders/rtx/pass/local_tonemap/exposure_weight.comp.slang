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

layout(binding = EXPOSURE_WEIGHT_INPUT)
Texture2D<vec4> LuminanceTexture;

layout(binding = EXPOSURE_WEIGHT_OUTPUT)
RWTexture2D<vec4> WeightTexture;

layout(binding = EXPOSURE_DEBUG_VIEW_OUTPUT)
RWTexture2D<float4> DebugView;

layout(push_constant)
ConstantBuffer<ExposureWeightArgs> cb;

#include "rtx/utility/debug_view_helpers.slangh"

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint2 threadId : SV_DispatchThreadID)
{
  // Compute the synthetic exposure weights.
  vec3 diff = LuminanceTexture[threadId].xyz - vec3(0.50 + cb.offset);
  vec3 weights = vec3(exp(-0.5 * diff * diff * cb.sigmaSq));
  weights /= dot(weights, vec3(1.0)) + 0.00001;
  WeightTexture[threadId] = vec4(weights, 1.0);
}
