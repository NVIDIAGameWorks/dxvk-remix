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
#include "rtx_draw_call_cache.h"
#include "rtx_options.h"
#include "../d3d9/d3d9_state.h"

namespace dxvk 
{

namespace {
  bool exactMatch(const DrawCallState& drawCall, BlasEntry& blas) {
    auto isSky = [](CameraType::Enum t) {
      return t == CameraType::Sky;
    };

    if (isSky(drawCall.cameraType) != isSky(blas.input.cameraType)) {
      return false;
    }

    return drawCall.getMaterialData().getHash() == blas.input.getMaterialData().getHash()
        && drawCall.getGeometryData().getHashForRule<rules::FullGeometryHash>() == blas.input.getGeometryData().getHashForRule<rules::FullGeometryHash>()
        && drawCall.getSkinningState().boneHash == blas.input.getSkinningState().boneHash;
  }
}

DrawCallCache::DrawCallCache(DxvkDevice* device) : CommonDeviceObject(device) {
  m_entries.reserve(1024);
}
DrawCallCache::~DrawCallCache() {}

DrawCallCache::CacheState DrawCallCache::get(const DrawCallState& drawCall, BlasEntry** out) {
  // First, find the right bucket:
  const XXH64_hash_t hash = drawCall.getGeometryData().getHashForRule<rules::TopologicalHash>();
  auto range = m_entries.equal_range(hash);
  if (range.first == m_entries.end()) {
    // New bucket
    *out = allocateEntry(hash, drawCall);
    return CacheState::kNew;
  }
  // Handle buckets with 1 entry:
  auto iter = range.first;
  iter++;
  if (iter == range.second) {
    // Only 1 element
    BlasEntry& entry = range.first->second;

    const bool updatedThisFrame = entry.frameLastTouched == m_device->getCurrentFrameId();
    const bool vertexDataMatches = entry.input.getGeometryData().getHashForRule<rules::VertexDataHash>() == drawCall.getGeometryData().getHashForRule<rules::VertexDataHash>();
    const bool boneHashesMatch = entry.input.getSkinningState().boneHash == drawCall.getSkinningState().boneHash;
    const bool materialHashesMatch = entry.input.getMaterialData().getHash() == drawCall.getMaterialData().getHash();

    if (exactMatch(drawCall, entry) || !updatedThisFrame && (vertexDataMatches && boneHashesMatch || materialHashesMatch)) {
      // Exact vertex match that is reusable for the current draw call,
      // or something that hasn't been updated this frame and is similar enough.
      // Matching the logic in the multi-element loop below.
      *out = &entry;
      return CacheState::kExisted;
    } else {
      // First frame of having two mismatching instances, and the first instance has already 
      // been paired with the existing BlasEntry.
      *out = allocateEntry(hash, drawCall);
      return CacheState::kNew;
    }
  }

  // Bucket has multiple BlasEntries

  float bestScore = std::numeric_limits<float>::min();

  // Read once because RtxOption::operator() takes a global mutex per read.
  const bool worldSpaceBones = RtxOptions::assumeWorldSpaceSkinnedBones();

  // Pairs a draw with one of several candidates sharing a topological hash. World-placement bones
  // give every instance the same centroid, so a representative bone from the previous frame is used.
  auto pairingCentroid = [worldSpaceBones](const DrawCallState& dc) -> Vector3 {
    const SkinningData& sk = dc.getSkinningState();
    if (worldSpaceBones &&
        sk.numBones > sk.minBoneIndex && sk.minBoneIndex < sk.pBoneMatrices.size()) {
      // Pairing only needs a point near where the instance sat last frame, so the bone's
      // world position is enough. minBoneIndex stands in for a root bone, because rigging
      // places roots first and a fast-moving limb would destabilize pairing.
      // objectToWorld is identity for world-space bones, but external draws retain it.
      const Matrix4 bonePlacement = dc.getTransformData().objectToWorld * sk.pBoneMatrices[sk.minBoneIndex];
      return bonePlacement[3].xyz();
    }
    return dc.getGeometryData().boundingBox.getTransformedCentroid(dc.getTransformData().objectToWorld);
  };
  const Vector3 newWorldPosition = pairingCentroid(drawCall);

  for (auto bucketIter = range.first; bucketIter != range.second; bucketIter++) {
    BlasEntry& blas  = bucketIter->second;
    if (exactMatch(drawCall, blas)) {
      *out = &blas;
      return CacheState::kExisted;
    }
    if (blas.frameLastTouched == m_device->getCurrentFrameId()) {
      continue;
    }
    // TODO these heuristics could use more refinement.
    float score = 0;
    if (blas.modifiedGeometryData.hashes[HashComponents::VertexPosition] == drawCall.getGeometryData().hashes[HashComponents::VertexPosition] &&
        blas.input.getSkinningState().boneHash == drawCall.getSkinningState().boneHash) {
      score += 1000.f;
    }
    if (blas.modifiedGeometryData.hashes[HashComponents::VertexTexcoord] == drawCall.getGeometryData().hashes[HashComponents::VertexTexcoord]) {
      score += 1000.f;
    }
    if (blas.input.getMaterialData().getHash() == drawCall.getMaterialData().getHash()) {
      score += 1000.f;
    }
    // Portal logic is omitted. blas.input holds the previous-frame position, so a match is one frame away.
    const Vector3 worldPosition = pairingCentroid(blas.input);
    score -= lengthSqr(newWorldPosition - worldPosition);
    if (score > bestScore) {
      bestScore = score;
      *out = &blas;
    }
  }
  if (*out == nullptr) {
    // Failed to find similar blas, so allocate a new one
    *out = allocateEntry(hash, drawCall);
    return CacheState::kNew;
  }
  return CacheState::kExisted;

}

BlasEntry* DrawCallCache::allocateEntry(XXH64_hash_t hash, const DrawCallState& drawCall) {
  auto iter = m_entries.emplace(hash, drawCall);
  BlasEntry* result = &iter->second;
  result->frameCreated = m_device->getCurrentFrameId();
  return result;
}

}  // namespace nvvk
