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
  // A draw that matched an entry exactly before finds it without the bucket walk.
  // The key hashes everything exactMatch compares.
  const XXH64_hash_t fields[] = {
    drawCall.cameraType == CameraType::Sky ? 1ull : 0ull,
    drawCall.getMaterialData().getHash(),
    drawCall.getGeometryData().getHashForRule<rules::FullGeometryHash>(),
    drawCall.getSkinningState().boneHash
  };
  const XXH64_hash_t exactKey = XXH3_64bits(fields, sizeof(fields));
  const auto indexed = m_exactMatchIndex.find(exactKey);
  if (indexed != m_exactMatchIndex.end() && exactMatch(drawCall, *indexed->second)) {
    *out = indexed->second;
    return CacheState::kExisted;
  }
  const CacheState state = findInBucket(drawCall, out);
  // New entries and exact matches go to the index, similar matches do not
  if (exactMatch(drawCall, **out)) {
    unindex(*out);
    m_exactMatchIndex[exactKey] = *out;
    m_exactMatchKeys[*out] = exactKey;
  }
  return state;
}

DrawCallCache::MultimapType::iterator DrawCallCache::erase(MultimapType::iterator iter) {
  unindex(&iter->second);
  return m_entries.erase(iter);
}

void DrawCallCache::unindex(const BlasEntry* entry) {
  const auto key = m_exactMatchKeys.find(entry);
  if (key == m_exactMatchKeys.end()) {
    return;
  }
  // The slot may hold a later entry with the same key, which keeps it
  const auto slot = m_exactMatchIndex.find(key->second);
  if (slot != m_exactMatchIndex.end() && slot->second == entry) {
    m_exactMatchIndex.erase(slot);
  }
  m_exactMatchKeys.erase(key);
}

DrawCallCache::CacheState DrawCallCache::findInBucket(const DrawCallState& drawCall, BlasEntry** out) {
  // First, find the right bucket:
  const XXH64_hash_t hash = drawCall.getGeometryData().getHashForRule<rules::TopologicalHash>();
  // Not equal_range: it walks the whole bucket before returning.
  // Entries with equal keys are adjacent, so the walks below start at find() and stop at the bucket end.
  const auto first = m_entries.find(hash);
  if (first == m_entries.end()) {
    // New bucket
    *out = allocateEntry(hash, drawCall);
    return CacheState::kNew;
  }
  auto isInBucket = [this, hash](const MultimapType::iterator& iter) {
    return iter != m_entries.end() && iter->first == hash;
  };
  // Handle buckets with 1 entry:
  if (!isInBucket(std::next(first))) {
    // Only 1 element
    BlasEntry& entry = first->second;

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

  for (auto bucketIter = first; isInBucket(bucketIter); bucketIter++) {
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
