/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

// Shared smooth normals implementation — compiles as both C++ and Slang/HLSL.
// Follows the same dual-compile pattern as skinning.h.

#ifdef __cplusplus
#include <cstring>
#include <algorithm>
namespace dxvk {
typedef Vector3 float3;

#define ReadBuffer(T)   const T*
#define WriteBuffer(T)  T*
#define RWIntBuffer      int32_t*
#define ConstBuffer(T)  const T&

// CPU: reinterpret float bits via memcpy (strict-aliasing safe)
inline uint32_t asuint_sn(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  return u;
}

#else // GPU (Slang/HLSL)

#define ReadBuffer(T)   StructuredBuffer<T>
#define WriteBuffer(T)  RWStructuredBuffer<T>
#define RWIntBuffer      RWStructuredBuffer<int>
// Use plain struct (not ConstantBuffer<T>) so callers can pass push constants
// or regular constant buffers alike — Slang handles the implicit conversion.
#define ConstBuffer(T)  T

uint asuint_sn(float f) {
  return asuint(f);
}

#endif

// Scale factor for fixed-point integer accumulation.
// Face normals are normalized to unit length before scaling, so each triangle
// contributes at most FIXED_POINT_SCALE per component.  With 10000, a vertex
// can safely accumulate normals from up to ~200 000 triangles before int32
// overflow.  The angular precision is ~0.006 degrees.
static const float FIXED_POINT_SCALE = 10000.0f;

// Max linear-probe distance before giving up.  With a load factor of ~0.25
// the expected probe length is ~1.17; 128 is extremely generous.
static const uint MAX_PROBES = 128u;

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

// Load a vertex position from the position buffer.
float3 smoothNormalsLoadPosition(uint vertexIndex,
                                 ReadBuffer(float) positionData,
                                 ConstBuffer(SmoothNormalsArgs) cb)
{
  uint baseOffset = cb.positionOffset / 4 + vertexIndex * (cb.positionStride / 4);
  return float3(
    positionData[baseOffset + 0],
    positionData[baseOffset + 1],
    positionData[baseOffset + 2]
  );
}

// Load an index from the index buffer.
uint smoothNormalsLoadIndex(uint idx,
                            ReadBuffer(uint) srcIndex,
                            ConstBuffer(SmoothNormalsArgs) cb)
{
  if (cb.useShortIndices != 0) {
    uint wordIndex = idx / 2;
    uint wordOffset = idx % 2;
    uint packed = srcIndex[cb.indexOffset / 4 + wordIndex];
    return (packed >> (wordOffset * 16)) & 0xFFFF;
  } else {
    return srcIndex[cb.indexOffset / 4 + idx];
  }
}

// Canonicalize float bits: flush -0.0 to +0.0 so that vertices at the
// same spatial position always produce identical bit patterns.
uint canonicalizeFloatBits(float v)
{
  if (v == 0.0f) return 0u;
  return asuint_sn(v);
}

// Compute the hash-table slot index from a position.
uint hashPositionSlot(float3 pos, uint hashTableSize)
{
  uint hx = canonicalizeFloatBits(pos.x);
  uint hy = canonicalizeFloatBits(pos.y);
  uint hz = canonicalizeFloatBits(pos.z);
  uint h = (hx * 73856093u) ^ (hy * 19349663u) ^ (hz * 83492791u);
  return h & (hashTableSize - 1);
}

// Compute a 32-bit position tag for collision detection.
// Uses different primes from the slot hash. Guaranteed non-zero (0 = empty).
uint computePositionTag(float3 pos)
{
  uint hx = canonicalizeFloatBits(pos.x);
  uint hy = canonicalizeFloatBits(pos.y);
  uint hz = canonicalizeFloatBits(pos.z);
  uint h = hx ^ (hy * 2654435761u) ^ (hz * 2246822519u);
  return (h == 0u) ? 1u : h;
}

// ---------------------------------------------------------------------------
// Hash table accumulate — GPU uses atomics, CPU uses direct writes
// ---------------------------------------------------------------------------

#ifdef __cplusplus
// CPU path: single-threaded, no atomics needed
void accumulateAtPosition(float3 pos, float3 faceNormal,
                          RWIntBuffer hashTableData, uint hashTableSize)
{
  uint tag  = computePositionTag(pos);
  uint slot = hashPositionSlot(pos, hashTableSize);

  int ix = int(faceNormal.x * FIXED_POINT_SCALE);
  int iy = int(faceNormal.y * FIXED_POINT_SCALE);
  int iz = int(faceNormal.z * FIXED_POINT_SCALE);

  uint probes = std::min(MAX_PROBES, hashTableSize);
  for (uint p = 0; p < probes; p++) {
    uint entrySlot = (slot + p) & (hashTableSize - 1);
    uint base = entrySlot * 4;

    int32_t storedTag = hashTableData[base + 0];
    if (storedTag == 0) {
      // Empty slot — claim it
      hashTableData[base + 0] = int32_t(tag);
      hashTableData[base + 1] = ix;
      hashTableData[base + 2] = iy;
      hashTableData[base + 3] = iz;
      return;
    }
    if (uint32_t(storedTag) == tag) {
      // Our slot — accumulate
      hashTableData[base + 1] += ix;
      hashTableData[base + 2] += iy;
      hashTableData[base + 3] += iz;
      return;
    }
    // Slot owned by different position — keep probing
  }
}

#else // GPU

void accumulateAtPosition(float3 pos, float3 faceNormal,
                          RWIntBuffer hashTableData, uint hashTableSize)
{
  uint tag  = computePositionTag(pos);
  uint slot = hashPositionSlot(pos, hashTableSize);

  int ix = int(faceNormal.x * FIXED_POINT_SCALE);
  int iy = int(faceNormal.y * FIXED_POINT_SCALE);
  int iz = int(faceNormal.z * FIXED_POINT_SCALE);

  uint probes = min(MAX_PROBES, hashTableSize);
  for (uint p = 0; p < probes; p++) {
    uint entrySlot = (slot + p) & (hashTableSize - 1);
    uint base = entrySlot * 4;

    int oldTag;
    InterlockedCompareExchange(hashTableData[base + 0], 0, int(tag), oldTag);

    if (oldTag == 0 || uint(oldTag) == tag) {
      InterlockedAdd(hashTableData[base + 1], ix);
      InterlockedAdd(hashTableData[base + 2], iy);
      InterlockedAdd(hashTableData[base + 3], iz);
      return;
    }
  }
}

#endif

// ---------------------------------------------------------------------------
// Hash table lookup — identical on CPU and GPU (read-only)
// ---------------------------------------------------------------------------

float3 lookupSmoothedNormal(float3 pos,
#ifdef __cplusplus
                            const int32_t* hashTableData,
#else
                            RWIntBuffer hashTableData,
#endif
                            uint hashTableSize)
{
#ifdef __cplusplus
  uint probes = std::min(MAX_PROBES, hashTableSize);
#else
  uint probes = min(MAX_PROBES, hashTableSize);
#endif

  uint tag  = computePositionTag(pos);
  uint slot = hashPositionSlot(pos, hashTableSize);

  for (uint p = 0; p < probes; p++) {
    uint entrySlot = (slot + p) & (hashTableSize - 1);
    uint base = entrySlot * 4;

    int storedTag = hashTableData[base + 0];

    if (storedTag == 0) {
      break; // empty slot — not found
    }

    if (uint(storedTag) == tag) {
      return float3(
        float(hashTableData[base + 1]),
        float(hashTableData[base + 2]),
        float(hashTableData[base + 3])
      );
    }
  }
  return float3(0.0f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Phase 1: Per-triangle accumulation
// ---------------------------------------------------------------------------

void smoothNormalsAccumulate(uint triIdx,
                             ReadBuffer(float) positionData,
                             ReadBuffer(uint) srcIndex,
                             RWIntBuffer hashTableData,
                             ConstBuffer(SmoothNormalsArgs) cb)
{
  uint i0 = smoothNormalsLoadIndex(triIdx * 3 + 0, srcIndex, cb);
  uint i1 = smoothNormalsLoadIndex(triIdx * 3 + 1, srcIndex, cb);
  uint i2 = smoothNormalsLoadIndex(triIdx * 3 + 2, srcIndex, cb);

  float3 p0 = smoothNormalsLoadPosition(i0, positionData, cb);
  float3 p1 = smoothNormalsLoadPosition(i1, positionData, cb);
  float3 p2 = smoothNormalsLoadPosition(i2, positionData, cb);

  float3 edge1 = p1 - p0;
  float3 edge2 = p2 - p0;
  float3 faceNormal = cross(edge1, edge2);

  float faceLen = length(faceNormal);
  if (faceLen <= 1e-20f) return; // degenerate triangle
  faceNormal /= faceLen;

  accumulateAtPosition(p0, faceNormal, hashTableData, cb.hashTableSize);
  accumulateAtPosition(p1, faceNormal, hashTableData, cb.hashTableSize);
  accumulateAtPosition(p2, faceNormal, hashTableData, cb.hashTableSize);
}

// ---------------------------------------------------------------------------
// Phase 2: Per-vertex scatter and normalize
// ---------------------------------------------------------------------------

void smoothNormalsScatter(uint vertIdx,
                          ReadBuffer(float) positionData,
                          WriteBuffer(float) normalData,
#ifdef __cplusplus
                          const int32_t* hashTableData,
#else
                          RWIntBuffer hashTableData,
#endif
                          ConstBuffer(SmoothNormalsArgs) cb)
{
  float3 pos = smoothNormalsLoadPosition(vertIdx, positionData, cb);
  float3 n = lookupSmoothedNormal(pos, hashTableData, cb.hashTableSize);

  float len = length(n);
  if (len > 1e-7f) {
    n /= len;
  } else {
    n = float3(0.0f, 1.0f, 0.0f); // Default up normal for degenerate cases
  }

  uint normalBase = cb.normalOffset / 4 + vertIdx * (cb.normalStride / 4);
  normalData[normalBase + 0] = n.x;
  normalData[normalBase + 1] = n.y;
  normalData[normalBase + 2] = n.z;
}

#ifdef __cplusplus
#undef ReadBuffer
#undef WriteBuffer
#undef RWIntBuffer
#undef ConstBuffer
} // namespace dxvk
#endif
