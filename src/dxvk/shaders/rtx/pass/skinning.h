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
#pragma once

// This function can be executed on the CPU or GPU!!
#ifdef __cplusplus
namespace dxvk {
typedef Vector4i uint4;
typedef Vector3 float3;
typedef Vector4 float4;

#define toMatrix4(m) (*reinterpret_cast<const Matrix4*>(&m))
#define mul(mat, vec) (mat * vec)

#define WriteBuffer(T) T*
#define ReadBuffer(T) const T*
#define ConstBuffer(T) const T&
#else
#define toMatrix4(x) x
#define Matrix4 float4x4

#define WriteBuffer(T) RWStructuredBuffer<T>
#define ReadBuffer(T) StructuredBuffer<T>
#define ConstBuffer(T) ConstantBuffer<T>
#endif

void skinning(const uint32_t idx,
              WriteBuffer(float) dstPosition,
              WriteBuffer(float) dstNormal,
              ReadBuffer(float) srcPosition,
              ReadBuffer(float) srcBlendWeight,
              ReadBuffer(uint) srcBlendIndices,
              ReadBuffer(float) srcNormal,
              ConstBuffer(SkinningArgs) cb) {
  const uint32_t baseWeightsOffset = (cb.blendWeightOffset + idx * cb.blendWeightStride) / 4;
  float4 blendWeights = float4(srcBlendWeight[baseWeightsOffset + 0],
                               srcBlendWeight[baseWeightsOffset + 1],
                               srcBlendWeight[baseWeightsOffset + 2],
                               srcBlendWeight[baseWeightsOffset + 3]);

  // When bone index buffer not used, indices default to the below
  uint4 blendIndices = uint4(0, 1, 2, 3);

  if (cb.useIndices) {
    const uint baseIndicesOffset = (cb.blendIndicesOffset + idx * cb.blendIndicesStride) / 4;
    const uint packedIndices = srcBlendIndices[baseIndicesOffset];
    blendIndices.x = (packedIndices >> 0) & 0xff;
    blendIndices.y = (packedIndices >> 8) & 0xff;
    blendIndices.z = (packedIndices >> 16) & 0xff;
    blendIndices.w = (packedIndices >> 24) & 0xff;
  }

  // Weights are normalized to 1, the last weight is equal to the remainder
  float totalWeight = 0;
  for (uint i = 0; i < cb.numBones - 1; i++) {
    totalWeight += blendWeights[i];
  }
  blendWeights[cb.numBones - 1] = 1.f - totalWeight;

  const uint baseSrcPositionOffset = (cb.srcPositionOffset + idx * cb.srcPositionStride) / 4;
  float4 position = float4(srcPosition[baseSrcPositionOffset + 0],
                           srcPosition[baseSrcPositionOffset + 1],
                           srcPosition[baseSrcPositionOffset + 2], 1.f);
  const uint baseSrcNormalOffset = (cb.srcNormalOffset + idx * cb.srcNormalStride) / 4;
  float4 normal = float4(srcNormal[baseSrcNormalOffset + 0],
                         srcNormal[baseSrcNormalOffset + 1],
                         srcNormal[baseSrcNormalOffset + 2], 0.f);

  // Do the skinning
  float4 positionOut = 0.f;
  float4 normalOut = 0.f;
  for (uint i = 0; i < cb.numBones; i++) {
    if (blendWeights[i] > 0) {
      Matrix4 bone = toMatrix4(cb.bones[blendIndices[i]]);
      positionOut += mul(bone, position) * blendWeights[i];
      normalOut += mul(bone, normal) * blendWeights[i];
    }
  }

  float3 newNormal = float3(normalOut.x, normalOut.y, normalOut.z);
  float normalLength = length(newNormal);
  if (normalLength > 0.f)
    newNormal /= normalLength;

  const uint baseDstPositionOffset = (cb.dstPositionOffset + idx * cb.dstPositionStride) / 4;
  dstPosition[baseDstPositionOffset + 0] = positionOut.x;
  dstPosition[baseDstPositionOffset + 1] = positionOut.y;
  dstPosition[baseDstPositionOffset + 2] = positionOut.z;

  const uint baseDstNormalOffset = (cb.dstNormalOffset + idx * cb.dstNormalStride) / 4;
  dstNormal[baseDstNormalOffset + 0] = newNormal.x;
  dstNormal[baseDstNormalOffset + 1] = newNormal.y;
  dstNormal[baseDstNormalOffset + 2] = newNormal.z;
}

#ifdef __cplusplus
#undef WriteBuffer
#undef ReadBuffer
#undef toMatrix4
#undef mul
} //dxvk
#endif