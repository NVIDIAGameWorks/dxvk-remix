/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx/pass/particles/dust_particles_bindings.slangh"

#include "rtx/utility/common.slangh"
#include "rtx/utility/noise.slangh"
#include "rtx/utility/brdf.slangh"
#include "rtx/utility/froxel.slangh"
#include "rtx/utility/packing.slangh"
#include "rtx/concept/camera/camera.slangh"

float softEdge(float size, float2 centerUv, float2 screenUv)
{
  float2 uvSize = size.xx * 0.5f / particleCb.renderResolution;
  return pow(1.f - saturate(length((screenUv - centerUv) / uvSize)), 1.f/3);
}

[shader("fragment")]
float4 main(float4 screenPos : SV_Position, float4 radiance : COLOR, nointerpolation float3 centerPosSize : CENTER) : SV_Target
{
  // depth test
  const float2 screenUV = screenPos.xy / particleCb.renderResolution;
  
  const float depth = PrimaryDepth.SampleLevel(screenUV, 0);
  if(depth <= screenPos.z && depth > 0.f)
    discard;
  
  const float blend_range = 0.0005f;
  return radiance * softEdge(centerPosSize.z, centerPosSize.xy, screenUV) * saturate(abs(screenPos.z-depth) / blend_range);
}
