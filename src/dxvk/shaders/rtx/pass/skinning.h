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

#define abs(x) std::abs(x)
#define max(x, y) std::max(x, y)

#define WriteBuffer(T) T*
#define ReadBuffer(T) const T*
#define ReadByteBuffer const uint8_t*
#define ConstBuffer(T) const T&

uint4 get(ReadByteBuffer buf, uint index) {
  return uint4(buf[index], buf[index+1], buf[index+2], buf[index+3]);
}

uint asuint(float f) {
  return *reinterpret_cast<const uint*>(&f);
}

float asfloat(uint u) {
  return *reinterpret_cast<const float*>(&u);
}
#else
#define toMatrix4(x) x
#define Matrix4 float4x4

#define WriteBuffer(T) RWStructuredBuffer<T>
#define ReadBuffer(T) StructuredBuffer<T>
#define ReadByteBuffer ByteAddressBuffer
#define ConstBuffer(T) ConstantBuffer<T>

uint4 get(ReadByteBuffer buf, uint index) {
  uint packedIndices = buf.Load(index);
  uint4 blendIndices;
  blendIndices.x = (packedIndices >> 0) & 0xff;
  blendIndices.y = (packedIndices >> 8) & 0xff;
  blendIndices.z = (packedIndices >> 16) & 0xff;
  blendIndices.w = (packedIndices >> 24) & 0xff;
  return blendIndices;
  // return buf.Load4(index);
}
#endif
// Todo: Share with GPU implementation by including here
uint f32ToUnorm16(float x) {
  const float scalar = (1 << 16) - 1;
  const float conv = x * scalar + 0.5f;

  return uint(conv);
}
float unorm16ToF32(uint x) {
  const float scalar = (1 << 16) - 1;
  const float conv = float(x & ((1 << 16) - 1)) / scalar;

  return conv;
}
uint encodeNormal(float3 n) {
  const float maxMag = abs(n.x) + abs(n.y) + abs(n.z);
  const float inverseMag = maxMag == 0.0f ? 0.0f : (1.0f / maxMag);
  float x = n.x * inverseMag;
  float y = n.y * inverseMag;

  if (n.z < 0.0f) {
    const float originalXSign = x < 0.0f ? -1.0f : 1.0f;
    const float originalYSign = y < 0.0f ? -1.0f : 1.0f;
    const float inverseAbsX = 1.0f - abs(x);
    const float inverseAbsY = 1.0f - abs(y);

    x = inverseAbsY * originalXSign;
    y = inverseAbsX * originalYSign;
  }

  // Signed->Unsigned octahedral
  x = x * 0.5f + 0.5f;
  y = y * 0.5f + 0.5f;

  return f32ToUnorm16(x) | (f32ToUnorm16(y) << 16);
}
float3 decodeNormal(uint e) {
  float x = unorm16ToF32(e);
  float y = unorm16ToF32(e >> 16);

  // Unsigned->Signed octahedral
  x = x * 2.0f - 1.0f;
  y = y * 2.0f - 1.0f;

  float3 v = float3(x, y, 1.0f - abs(x) - abs(y));
  const float t = max(-v.z, 0.0f);

  v.x += (v.x >= 0.0f) ? -t : t;
  v.y += (v.y >= 0.0f) ? -t : t;

  return normalize(v);
}

void skinning(const uint32_t idx,
              WriteBuffer(float) dstPosition,
              WriteBuffer(float) dstNormal,
              ReadBuffer(float) srcPosition,
              ReadBuffer(float) srcBlendWeight,
              ReadByteBuffer srcBlendIndices,
              ReadBuffer(float) srcNormal,
              ConstBuffer(SkinningArgs) cb) {
  const uint32_t baseWeightsOffset = (cb.blendWeightOffset + idx * cb.blendWeightStride) / 4;

  // Weights are normalized to 1, the last weight is equal to the remainder
  float lastWeight = 1.f;
  for (uint i = 0; i < cb.numBones - 1; i++) {
    lastWeight -= srcBlendWeight[baseWeightsOffset + i];
  }

  // Read position
  const uint baseSrcPositionOffset = (cb.srcPositionOffset + idx * cb.srcPositionStride) / 4;
  float4 position = float4(srcPosition[baseSrcPositionOffset + 0],
                           srcPosition[baseSrcPositionOffset + 1],
                           srcPosition[baseSrcPositionOffset + 2], 1.f);

  // Read normal
  const uint baseSrcNormalOffset = (cb.srcNormalOffset + idx * cb.srcNormalStride) / 4;
  float4 normal;
  if (cb.useOctahedralNormals) {
    const uint encodedNormal = asuint(srcNormal[baseSrcNormalOffset]);
    const float3 decodedNormal = decodeNormal(encodedNormal);

    normal = float4(decodedNormal.x, decodedNormal.y, decodedNormal.z, 0.f);
  } else {
    normal = float4(srcNormal[baseSrcNormalOffset + 0],
                    srcNormal[baseSrcNormalOffset + 1],
                    srcNormal[baseSrcNormalOffset + 2], 0.f);
  }

  // Do the skinning
  float4 positionOut = float4(0.f);
  float4 normalOut = float4(0.f);
  if (cb.useIndices) {
    const uint baseIndicesOffset = cb.blendIndicesOffset + idx * cb.blendIndicesStride;
    for (uint j = 0; j < cb.numBones; j+=4) {
      uint4 blendIndices = get(srcBlendIndices, baseIndicesOffset + j);
      for (uint i = 0; i < 4 && i + j < cb.numBones; ++i) {
        float blendWeight = i + j == cb.numBones - 1 ? lastWeight : srcBlendWeight[baseWeightsOffset + i + j];
        if (blendWeight > 0) {
          Matrix4 bone = toMatrix4(cb.bones[blendIndices[i]]);
          positionOut += mul(bone, position) * blendWeight;
          normalOut += mul(bone, normal) * blendWeight;
        }
      }
    }
  } else {
    for (uint i = 0; i < cb.numBones - 1; ++i) {
      float blendWeight = srcBlendWeight[baseWeightsOffset + i];
      if (blendWeight > 0.f) {
        Matrix4 bone = toMatrix4(cb.bones[i]);
        positionOut += mul(bone, position) * blendWeight;
        normalOut += mul(bone, normal) * blendWeight;
      }
    }
    // Unwrap the last bone, since blendWeights only contains numBones - 1 weights
    if (lastWeight > 0.f) {
      Matrix4 bone = toMatrix4(cb.bones[cb.numBones - 1]);
      positionOut += mul(bone, position) * lastWeight;
      normalOut += mul(bone, normal) * lastWeight;
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
  if (cb.useOctahedralNormals) {
    const uint encodedNormal = encodeNormal(newNormal);

    dstNormal[baseDstNormalOffset] = asfloat(encodedNormal);
  } else {
    dstNormal[baseDstNormalOffset + 0] = newNormal.x;
    dstNormal[baseDstNormalOffset + 1] = newNormal.y;
    dstNormal[baseDstNormalOffset + 2] = newNormal.z;
  }
}

#ifdef __cplusplus
#undef WriteBuffer
#undef ReadBuffer
#undef toMatrix4
#undef mul
#undef max
#undef abs
} //dxvk
#endif