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

#include <memory>
#include <unordered_map>
#include <vector>

#include "rtx_types.h"
#include "../util/util_spatial_map.h"

namespace dxvk {

class DxvkDevice;
class RtCamera;
class RayPortalManager;
struct MaterialData;

// Bundled hash/position/transform parameters used to look up or create a ReplacementInstance.
// Constructed by callers from whatever source they have (DrawCallState, D3DLIGHT9, ExternalDrawState).
struct InstanceLookupKey {
  XXH64_hash_t identityHash;
  // The hash to identify which spatial map to search for the replacementInstance.
  XXH64_hash_t spatialMapHash;
  XXH64_hash_t materialHash;
  XXH64_hash_t vertexPositionHash;
  Vector3 worldPos;
  Matrix4 transform;
};

// Tracks draw calls across frames by mapping each draw call to a ReplacementInstance.
// Uses a two-level hash lookup (identity hash for L1, tracking hash + spatial proximity
// for L2) to match the current frame's draw calls against previous frames.
class DrawCallTracker {
public:
  DrawCallTracker(DxvkDevice* device);
  ~DrawCallTracker() = default;

  DrawCallTracker(const DrawCallTracker&) = delete;
  DrawCallTracker& operator=(const DrawCallTracker&) = delete;

  // Find or create a ReplacementInstance using precomputed lookup key.
  ReplacementInstance* findOrCreateReplacementInstance(const InstanceLookupKey& key);

  // Removes replacement instances whose spatialMapHash matches (same bucket key as
  // m_assetSpatialMaps). Used when a draw source tied to that bucket is invalidated.
  void removeReplacementInstancesWithSpatialMapHash(XXH64_hash_t spatialMapHash);

  // Find or create a ReplacementInstance from a DrawCallState and its material.
  // Portal-aware matching for ViewModel draw calls is performed using the RayPortalManager.
  ReplacementInstance* findOrCreateReplacementInstance(
      const DrawCallState& drawCallState,
      const MaterialData& materialData,
      const RayPortalManager& rayPortalManager);

  // Garbage collect expired ReplacementInstances. Object anti-culling keeps mesh RIs alive
  // if their bounding box intersects the camera frustum. Light content uses the camera's
  // light anti-culling frustum (wider FOV) and extended lifetime.
  void garbageCollectReplacementInstances(RtCamera& camera, bool isAntiCullingSupported);

  void clear();

  // Rebuild all spatial maps with a new cell size.
  // Call when uniqueObjectDistance changes.
  void rebuildSpatialMaps(float cellSize);

  const std::vector<std::unique_ptr<ReplacementInstance>>& getReplacementInstances() const {
    return m_replacementInstances;
  }

  // Compute a hash that uniquely identifies a draw call for L1 matching.
  static XXH64_hash_t computeIdentityHash(const DrawCallState& drawCallState);

private:
  // Cleans up a ReplacementInstance's map/index entries and prims. Does NOT remove
  // the entry from m_replacementInstances -- callers must handle that separately.
  void destroyReplacementInstance(ReplacementInstance* replacementInstance);

  DxvkDevice* m_device;

  std::unordered_map<XXH64_hash_t, ReplacementInstance*> m_identityHashMap;

  using ReplacementSpatialMap = SpatialMap<ReplacementInstance>;
  std::unordered_map<XXH64_hash_t, ReplacementSpatialMap> m_assetSpatialMaps;

  // Secondary spatial index keyed by material hash for cross-topology matching.
  // When animated geometry uses different index buffers per frame (changing the
  // tracking hash), this index allows O(1) lookup by material + spatial proximity
  // instead of scanning all tracking hash buckets.
  std::unordered_map<XXH64_hash_t, ReplacementSpatialMap> m_materialSpatialMaps;

  // Attempts to match a newly created ReplacementInstance through ray portals.
  // If a match is found, destroys the new RI and returns the reassociated match.
  // Returns nullptr if no portal match is found.
  ReplacementInstance* tryPortalMatch(
      ReplacementInstance* newInstance,
      const InstanceLookupKey& key,
      const RayPortalManager& rayPortalManager);

  // Erases an entry from a spatial map bucket and removes the bucket if empty.
  void eraseFromSpatialMap(
      std::unordered_map<XXH64_hash_t, ReplacementSpatialMap>& mapOfMaps,
      XXH64_hash_t bucketKey,
      XXH64_hash_t transformHash,
      const ReplacementInstance* data);

  // Reassociates an existing ReplacementInstance with a new draw call identity.
  // Updates position fields and optionally moves within spatial maps.
  // moveInAssetMap: if non-null, the RI is moved within this asset spatial map.
  // moveInMaterialMap: if true, the RI is moved within its material spatial map.
  ReplacementInstance* reassociateMatch(
      ReplacementInstance* match,
      const InstanceLookupKey& key,
      ReplacementSpatialMap* moveInAssetMap,
      bool moveInMaterialMap);

  std::vector<std::unique_ptr<ReplacementInstance>> m_replacementInstances;
  uint32_t m_nextReplacementInstanceId = 0;

};

}  // namespace dxvk
