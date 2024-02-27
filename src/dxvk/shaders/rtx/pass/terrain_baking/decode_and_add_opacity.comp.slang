/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "decode_and_add_opacity_binding_indices.h"
#include "rtx/utility/packing.slangh"

layout(push_constant)
ConstantBuffer<DecodeAndAddOpacityArgs> cb;

layout(rgba16f, binding = DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_INPUT)
Texture2D<float4> InputTexture;

layout(rgba16f, binding = DECODE_AND_ADD_OPACITY_BINDING_ALBEDO_OPACITY_TEXTURE_INPUT)
Texture2D<float4> InputAlbedoOpacityTexture;

layout(rgba16f, binding = DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_OUTPUT)
RWTexture2D<float4> OutputTexture;

layout(binding = DECODE_AND_ADD_OPACITY_BINDING_LINEAR_SAMPLER)
SamplerState LinearSampler;

[shader("compute")]
[numthreads(DECODE_AND_ADD_OPACITY_CS_DIMENSIONS)]
void main(uint2 thread_id : SV_DispatchThreadID)
{
  if (any(thread_id >= cb.resolution))
  {
    return;
  }

  // Get uv of a texel's center
  const float2 uv = cb.rcpResolution * (thread_id + 0.5f);

  // ToDo trilinear sampling
  float16_t opacity = InputAlbedoOpacityTexture.SampleLevel(LinearSampler, uv, 0.f).a;

  f16vec4 outSample = f16vec4(0, 0, 0, opacity);

  switch (cb.textureType) 
  {
  // ColorOpacity is not to be passed to be processed by this shader as it does not need to be converted
  case ReplacementMaterialTextureType::Enum::AlbedoOpacity:
  default:
    break;
  
  // Oct normal -> RGB16
  case ReplacementMaterialTextureType::Enum::Normal:
  case ReplacementMaterialTextureType::Enum::Tangent:
  // Note: Using f16 from texture to decode from is fine as the original texture is only 8 bits of precision.
    outSample.rgb = unsignedOctahedralToHemisphereDirection(
      InputTexture.SampleLevel(LinearSampler, uv, 0.f).rg, cb.normalIntensity);
    break;

  // R16
  case ReplacementMaterialTextureType::Enum::Roughness:
  case ReplacementMaterialTextureType::Enum::Metallic:
    outSample.r = InputTexture.SampleLevel(LinearSampler, uv, 0.f).r;
    break;
  case ReplacementMaterialTextureType::Enum::Height:
    outSample.r = InputTexture.SampleLevel(LinearSampler, uv, 0.f).r * cb.scale;
    break;

  // RGB16
  case ReplacementMaterialTextureType::Enum::Emissive:
    outSample.rgb = InputTexture.SampleLevel(LinearSampler, uv, 0.f).rgb;
    break;
  }

  OutputTexture[thread_id] = outSample;
}
