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
#include "rtx_draw_call_tracker.h"
#include "rtx_options.h"
#include "rtx_instance_manager.h"
#include "rtx_ray_portal_manager.h"
#include "rtx_intersection_test.h"
#include "dxvk_device.h"
#include "../util/util_struct_hash.h"
#include "../util/log/log.h"
#include "../util/util_string.h"

#include <algorithm>

namespace dxvk {

  // Compute which lookup-key fields differ from the RI's cached values and write
  // the lookup-drift bits to ri->dirtyFlags (dynamic-feature bits are preserved).
  // Caller is expected to invoke this BEFORE overwriting the RI's cached fields.
  static void computeDirtyFlags(
      ReplacementInstance* ri, const ReplacementInstance::LookupKey& key) {
    ri->dirtyFlags.clr(ReplacementInstance::kLookupDriftMask);
    // This function is only called when the full identity hash doesn't match, so something must have changed.
    if (std::memcmp(&ri->objectToWorld, &key.transform, sizeof(Matrix4)) != 0) {
      ri->dirtyFlags.set(ReplacementInstance::DirtyFlag::Transform);
    }
    if (ri->vertexPositionHash != key.vertexPositionHash) {
      ri->dirtyFlags.set(ReplacementInstance::DirtyFlag::VertexPosHash);
    }
    if (ri->materialHash != key.materialHash) {
      ri->dirtyFlags.set(ReplacementInstance::DirtyFlag::MaterialHash);
    }
    // textureTransform/texgenMode aren't fed into a dedicated dirty bit yet —
    // surface state on the preserve path is reused as-is, so any drift here (terrain
    // baker rewriting view→cascade space, free-camera view-space texgen) needs to
    // force the dynamic path until the preserve path can refresh those fields. Bundle
    // into the catch-all Other bit so usePreservePath's isClear() check fails.
    if (std::memcmp(&ri->textureTransform, &key.textureTransform, sizeof(Matrix4)) != 0 ||
        ri->texgenMode != key.texgenMode) {
      ri->dirtyFlags.set(ReplacementInstance::DirtyFlag::Other);
    }
    if ((ri->dirtyFlags & ReplacementInstance::kLookupDriftMask).isClear()) {
      ri->dirtyFlags.set(ReplacementInstance::DirtyFlag::Other);
    }
  }

  // Frustum check for an AABB in the space defined by objectToWorld.
  // Uses SAT (high precision) or fast check based on the RtxOption.
  static bool aabbIntersectsFrustum(
      RtCamera& camera,
      const AxisAlignedBoundingBox& aabb,
      const Matrix4& objectToWorld) {
    const Matrix4 objectToView = camera.getWorldToView(false) * objectToWorld;
    if (RtxOptions::AntiCulling::Object::enableHighPrecisionAntiCulling()) {
      return boundingBoxIntersectsFrustumSAT(
          camera, aabb.minPos, aabb.maxPos, objectToView,
          RtxOptions::AntiCulling::Object::enableInfinityFarFrustum());
    }
    return boundingBoxIntersectsFrustum(camera.getFrustum(), aabb.minPos, aabb.maxPos, objectToView);
  }

  DrawCallTracker::DrawCallTracker(DxvkDevice* device)
    : m_device(device) {
  }

  XXH64_hash_t DrawCallTracker::computeIdentityHash(
      const DrawCallState& drawCallState,
      const MaterialData* overrideMaterialData) {
    struct IdentityHashData{
      XXH64_hash_t geoHash;
      XXH64_hash_t matHash;
      XXH64_hash_t boneHash;
      XXH64_hash_t overrideMaterialHash;
      Matrix4 xform;
      Matrix4 textureTransform;
      uint32_t categories;
      // Packed: cameraType (bits 0-7), texgenMode (bits 8-15), isUsingRaytracedRenderTarget (bit 16).
      uint32_t miscFlags;
    };

    // {} value-initializes the entire object.
    IdentityHashData data{};

    data.geoHash = drawCallState.getGeometryData().getHashForRule(rules::FullGeometryHash);
    data.matHash = drawCallState.getMaterialData().getHash();
    data.overrideMaterialHash = overrideMaterialData != nullptr
        ? overrideMaterialData->getHash()
        : kEmptyHash;
    data.xform = drawCallState.getTransformData().objectToWorld;
    data.categories = drawCallState.getCategoryFlags().raw();
    data.boneHash = drawCallState.getSkinningState().boneHash;
    data.textureTransform = drawCallState.getTransformData().textureTransform;

    // Note: these could be packed tighter, if we have more small values that need to be included.
    static_assert(CameraType::Count <= (1u << 8), "CameraType no longer fits in the 8 bits allocated to it in IdentityHashData::miscFlags");
    static_assert(static_cast<uint32_t>(TexGenMode::Count) <= (1u << 8), "TexGenMode no longer fits in the 8 bits allocated to it in IdentityHashData::miscFlags");
    data.miscFlags = static_cast<uint32_t>(drawCallState.cameraType)
        | (static_cast<uint32_t>(drawCallState.getTransformData().texgenMode) << 8)
        | (drawCallState.isUsingRaytracedRenderTarget ? (1u << 16) : 0u);

    return hashStructByMemory<IdentityHashData,
        &IdentityHashData::geoHash,
        &IdentityHashData::matHash,
        &IdentityHashData::boneHash,
        &IdentityHashData::overrideMaterialHash,
        &IdentityHashData::xform,
        &IdentityHashData::textureTransform,
        &IdentityHashData::categories,
        &IdentityHashData::miscFlags>(data);
  }

  void DrawCallTracker::eraseFromSpatialMap(
      std::unordered_map<XXH64_hash_t, ReplacementSpatialMap>& mapOfMaps,
      XXH64_hash_t bucketKey,
      XXH64_hash_t transformHash,
      const ReplacementInstance* data) {
    auto iter = mapOfMaps.find(bucketKey);
    if (iter != mapOfMaps.end()) {
      iter->second.erase(transformHash, data);
      if (iter->second.size() == 0) {
        mapOfMaps.erase(iter);
      }
    }
  }

  ReplacementInstance* DrawCallTracker::reassociateMatch(
      ReplacementInstance* match,
      const ReplacementInstance::LookupKey& key,
      ReplacementSpatialMap* moveInAssetMap) {
    m_identityHashMap.erase(match->identityHash);
    match->identityHash = key.identityHash;
    match->vertexPositionHash = key.vertexPositionHash;
    match->materialHash = key.materialHash;
    match->prevCentroid = match->centroid;
    match->centroid = key.worldPos;

    // Gather moved instances for history repair using spatial hash and source vertex buffer as keys
    if (match->prevCentroid != match->centroid) {
      m_movedInstanceClusters[match->spatialMapHash][key.sourceVertexBufferAddress].push_back(match);
    }

    if (moveInAssetMap) {
      match->spatialCacheTransformHash = moveInAssetMap->move(
          match->spatialCacheTransformHash, key.worldPos, key.transform, match);
    }

    m_identityHashMap[key.identityHash] = match;
    return match;
  }

  void DrawCallTracker::removeReplacementInstancesWithSpatialMapHash(XXH64_hash_t spatialMapKey) {
    for (size_t i = 0; i < m_replacementInstances.size();) {
      ReplacementInstance* replacementInstance = m_replacementInstances[i].get();
      if (replacementInstance->spatialMapHash == spatialMapKey) {
        destroyReplacementInstance(replacementInstance);
        std::swap(m_replacementInstances[i], m_replacementInstances.back());
        m_replacementInstances.pop_back();
        continue;
      }
      ++i;
    }
  }

  void DrawCallTracker::updateInstanceClusterCount(XXH64_hash_t spatialMapHash,
                                                   const void* sourceVertexBufferAddress,
                                                   bool isInstanceRemoved) {
    const XXH64_hash_t bucketKey = computeInstanceClusterBucketKey(spatialMapHash, sourceVertexBufferAddress);

    auto clusterStatsIter = m_instanceClusterBucketStats.find(bucketKey);
    if (clusterStatsIter == m_instanceClusterBucketStats.end()) {
      // A removal from a bucket that is not yet tracked is a no-op -- the bucket is already empty.
      if (isInstanceRemoved) {
        return;
      }
      clusterStatsIter = m_instanceClusterBucketStats.emplace(bucketKey, InstanceClusterBucketStats {}).first;
    }

    InstanceClusterBucketStats& stats = clusterStatsIter->second;

    // This is the first update for this bucket in the current frame, so reset the current-frame counters.
    const uint32_t currentFrameId = m_device->getCurrentFrameId();
    if (stats.frameLastUpdated != currentFrameId) {
      stats.previousCount = stats.currentCount;
      stats.currentCount = 0;
      stats.instanceDeleteCount = 0;
      stats.instanceDestroyedCurrentFrame = false;
      stats.frameLastUpdated = currentFrameId;
    }

    if (!isInstanceRemoved) {
      ++stats.currentCount;
      return;
    }

    stats.instanceDestroyedCurrentFrame = true;
    ++stats.instanceDeleteCount;
    // Drop the entry conservatively to prevent map from growing too large.
    if (std::max(stats.previousCount, stats.currentCount) <= stats.instanceDeleteCount) {
      m_instanceClusterBucketStats.erase(clusterStatsIter);
    }
  }

  ReplacementInstance* DrawCallTracker::findOrCreateReplacementInstance(
      const ReplacementInstance::LookupKey& key) {
    ScopedCpuProfileZone();
    const uint32_t currentFrameId = m_device->getCurrentFrameId();

    // Level 1: exact identity match.
    // Draw calls with the same identity hash (same geometry, material, transform) return
    // the same ReplacementInstance even if already seen this frame. This handles two-pass rendering where
    // the game draws the same mesh twice (e.g., base pass + overlay). The second pass
    // merges into the same instance via mergeInstanceHeuristics in updateInstance.
    auto exactMatchIter = m_identityHashMap.find(key.identityHash);
    if (exactMatchIter != m_identityHashMap.end()) {
      // identityHash includes transform/material/vertex hashes, so all key fields
      // match this RI's cached values by construction. Nothing changed since last
      // submission of this identity.
      //
      // Only clear lookup-drift bits on the first lookup of a new frame. A second lookup
      // within the same frame (two-pass rendering) must not clobber flags the L2
      // path set when first matching this RI for the current frame. Dynamic-feature
      // bits persist until the dynamic path runs.
      ReplacementInstance* match = exactMatchIter->second;
      if (match->frameLastSeen != currentFrameId) {
        match->dirtyFlags.clr(ReplacementInstance::kLookupDriftMask);
        match->prevCentroid = match->centroid;
        updateInstanceClusterCount(key.spatialMapHash, key.sourceVertexBufferAddress,
                                   false);
      }
      return match;
    }

    // Level 2: tracking hash (topological, stable for animated geometry) + spatial proximity
    const float uniqueObjectDistanceSqr = RtxOptions::getUniqueObjectDistanceSqr();
    const float spatialMapCellSize = RtxOptions::uniqueObjectDistance() * 2.f;

    auto l2Filter = [&](const ReplacementInstance* candidate) {
      return candidate->frameLastSeen != currentFrameId &&
             candidate->materialHash == key.materialHash &&
             candidate->sourceVertexBufferAddress == key.sourceVertexBufferAddress;
    };

    auto spatialMapIter = m_assetSpatialMaps.find(key.spatialMapHash);
    if (spatialMapIter != m_assetSpatialMaps.end()) {
      // Try exact transform + vertex position hash match first
      ReplacementInstance* exactTransformMatch = nullptr;
      spatialMapIter->second.forEachAtTransform(key.transform, [&](const ReplacementInstance* candidate) {
        if (candidate->vertexPositionHash == key.vertexPositionHash && l2Filter(candidate)) {
          exactTransformMatch = const_cast<ReplacementInstance*>(candidate);
          return true;
        }
        return false;
      });
      if (exactTransformMatch != nullptr) {
        // Compute diff before reassociation. Transform/vertex/spatialMap match
        // by construction here; only material can diverge (l2Filter requires
        // material match, so usually nothing differs at this point).
        computeDirtyFlags(exactTransformMatch, key);
        m_identityHashMap.erase(exactTransformMatch->identityHash);
        exactTransformMatch->identityHash = key.identityHash;
        m_identityHashMap[key.identityHash] = exactTransformMatch;
        exactTransformMatch->prevCentroid = exactTransformMatch->centroid;
        updateInstanceClusterCount(key.spatialMapHash, key.sourceVertexBufferAddress,
                                   false);
        return exactTransformMatch;
      }

      // Spatial nearest-neighbor search
      float nearestDistSqr = FLT_MAX;
      const ReplacementInstance* nearestMatch = spatialMapIter->second.getNearestData(
        key.worldPos, uniqueObjectDistanceSqr, nearestDistSqr, l2Filter);

      if (nearestMatch != nullptr) {
        // Compute diff before reassociation overwrites the cached fields. The
        // transform differs (otherwise we would have hit the exact-transform
        // branch above); other fields may also have changed.
        ReplacementInstance* match = const_cast<ReplacementInstance*>(nearestMatch);
        computeDirtyFlags(match, key);
        updateInstanceClusterCount(key.spatialMapHash, key.sourceVertexBufferAddress,
                                   false);
        return reassociateMatch(match, key, &spatialMapIter->second);
      }
    }

    // Level 3: no match — create a new ReplacementInstance.
    auto newReplacementInstance = std::make_unique<ReplacementInstance>(
        key, m_nextReplacementInstanceId++, currentFrameId);
    ReplacementInstance* replacementInstance = newReplacementInstance.get();

    m_identityHashMap[key.identityHash] = replacementInstance;

    auto [mapIter, inserted] = m_assetSpatialMaps.try_emplace(key.spatialMapHash, spatialMapCellSize);
    replacementInstance->spatialCacheTransformHash =
        mapIter->second.insert(key.worldPos, key.transform, replacementInstance);

    m_replacementInstances.push_back(std::move(newReplacementInstance));

    // A new RI is created when L1/L2 finds no continuation -- but a draw reorder can also make an
    // older instance miss and look new. Add it to the moved cluster so the end-of-frame repair can
    // recover its previous-frame state if one exists.
    m_movedInstanceClusters[replacementInstance->spatialMapHash][key.sourceVertexBufferAddress]
        .push_back(replacementInstance);

    updateInstanceClusterCount(key.spatialMapHash, key.sourceVertexBufferAddress,
                               false);

    return replacementInstance;
  }

  ReplacementInstance* DrawCallTracker::findOrCreateReplacementInstance(
      const DrawCallState& drawCallState,
      const RayPortalManager& rayPortalManager,
      const MaterialData* overrideMaterialData) {
    // Tracking hash uses topology only (indices + geometry descriptor), matching how the
    // baseline grouped instances by BlasEntry (topological hash). The material hash is used
    // as a filter in the spatial search, not as part of the key — this allows matching
    // even when the material changes between frames (LOD, texture animation).
    const auto& hashes = drawCallState.getGeometryData().hashes;
    const Matrix4& objectToWorld = drawCallState.getTransformData().objectToWorld;

    const ReplacementInstance::LookupKey key {
      computeIdentityHash(drawCallState, overrideMaterialData),
      hashes.getHashForRule<rules::TopologicalHash>(),
      drawCallState.getMaterialData().getHash(),
      hashes[HashComponents::VertexPosition],
      drawCallState.getGeometryData().boundingBox.getTransformedCentroid(objectToWorld),
      objectToWorld,
      drawCallState.getTransformData().textureTransform,
      drawCallState.getTransformData().texgenMode,
      drawCallState.getGeometryData().sourceVertexBufferAddress
    };

    ReplacementInstance* result = findOrCreateReplacementInstance(key);

    // Portal-aware matching for ViewModel draw calls: if the normal lookup created a
    // brand-new ReplacementInstance (no existing prims), try matching through portals.
    if (result->prims.empty() &&
        drawCallState.cameraType == CameraType::ViewModel &&
        RtxOptions::useRayPortalVirtualInstanceMatching()) {
      ReplacementInstance* portalResult = tryPortalMatch(result, key, rayPortalManager);
      if (portalResult != nullptr) {
        return portalResult;
      }
    }

    return result;
  }

  ReplacementInstance* DrawCallTracker::findReplacementInstanceByIdentity(XXH64_hash_t identityHash) {
    auto it = m_identityHashMap.find(identityHash);
    return (it != m_identityHashMap.end()) ? it->second : nullptr;
  }

  ReplacementInstance* DrawCallTracker::tryPortalMatch(
      ReplacementInstance* newInstance,
      const ReplacementInstance::LookupKey& key,
      const RayPortalManager& rayPortalManager) {
    const uint32_t currentFrameId = m_device->getCurrentFrameId();
    const float uniqueObjectDistanceSqr = RtxOptions::getUniqueObjectDistanceSqr();

    auto spatialMapIter = m_assetSpatialMaps.find(key.spatialMapHash);
    if (spatialMapIter == m_assetSpatialMaps.end()) {
      return nullptr;
    }

    auto portalFilter = [&](const ReplacementInstance* candidate) {
      return candidate != newInstance &&
             candidate->frameLastSeen != currentFrameId &&
             candidate->materialHash == key.materialHash;
    };

    for (auto& rayPortalPair : rayPortalManager.getRayPortalPairInfos()) {
      if (!rayPortalPair.has_value()) {
        continue;
      }
      for (uint32_t portalIdx = 0; portalIdx < 2; portalIdx++) {
        const auto& rayPortal = rayPortalPair->pairInfos[portalIdx];
        const Vector3 virtualPos = rayPortalManager.getVirtualPosition(
            key.worldPos, rayPortal.portalToOpposingPortalDirection);

        float nearestDistSqr = FLT_MAX;
        const ReplacementInstance* portalMatch = spatialMapIter->second.getNearestData(
            virtualPos, uniqueObjectDistanceSqr, nearestDistSqr, portalFilter);

        if (portalMatch != nullptr) {
          destroyReplacementInstance(newInstance);
          // When this code was written, newInstance would always be the last element
          // of m_replacementInstances. If this assert fires, that assumption is no
          // longer true. Either restore that assumption, or change this code to search
          // for the correct ReplacementInstance to remove.
          assert(m_replacementInstances.back().get() == newInstance &&
                 "tryPortalMatch: newInstance must be the last element");
          m_replacementInstances.pop_back();

          ReplacementInstance* match = const_cast<ReplacementInstance*>(portalMatch);
          computeDirtyFlags(match, key);
          return reassociateMatch( match, key, &spatialMapIter->second);
        }
      }
    }

    return nullptr;
  }

  void DrawCallTracker::destroyReplacementInstance(ReplacementInstance* replacementInstance) {
    if (!replacementInstance) {
      return;
    }

    m_identityHashMap.erase(replacementInstance->identityHash);
    updateInstanceClusterCount(replacementInstance->spatialMapHash,
                               replacementInstance->sourceVertexBufferAddress,
                               true);
    eraseFromSpatialMap(m_assetSpatialMaps, replacementInstance->spatialMapHash,
        replacementInstance->spatialCacheTransformHash, replacementInstance);

    auto movedClusterIter = m_movedInstanceClusters.find(replacementInstance->spatialMapHash);
    if (movedClusterIter != m_movedInstanceClusters.end()) {
      auto& bufferBuckets = movedClusterIter->second;
      // The instance may live under any source-buffer bucket so we remove it from each.
      for (auto bucketIter = bufferBuckets.begin(); bucketIter != bufferBuckets.end();) {
        auto& movedCluster = bucketIter->second;
        movedCluster.erase(
            std::remove(movedCluster.begin(), movedCluster.end(), replacementInstance),
            movedCluster.end());
        if (movedCluster.empty()) {
          bucketIter = bufferBuckets.erase(bucketIter);
        } else {
          ++bucketIter;
        }
      }
      if (bufferBuckets.empty()) {
        m_movedInstanceClusters.erase(movedClusterIter);
      }
    }

    replacementInstance->clear();
  }

  void DrawCallTracker::garbageCollectReplacementInstances(
      RtCamera& camera,
      bool isAntiCullingSupported) {

    const uint32_t currentFrame = m_device->getCurrentFrameId();
    const uint32_t numFramesToKeepObjects = RtxOptions::numFramesToKeepInstances();
    const uint32_t numFramesToKeepLights = RtxOptions::AntiCulling::Light::numFramesToExtendLightLifetime();

    const bool objectAntiCullingEnabled = RtxOptions::AntiCulling::isObjectAntiCullingEnabled();
    const bool lightAntiCullingEnabled = RtxOptions::AntiCulling::isLightAntiCullingEnabled();
    const bool isCameraCut = camera.isCameraCut();
    const bool forceGC = (m_replacementInstances.size() >=
        RtxOptions::AntiCulling::Object::numObjectsToKeep());

    for (size_t i = 0; i < m_replacementInstances.size();) {
      ReplacementInstance* replacementInstance = m_replacementInstances[i].get();

      const bool hasLights = replacementInstance->lightBoundingBox.isValid();
      const bool hasMeshes = replacementInstance->geometryBoundingBox.isValid();

      if (replacementInstance->frameLastSeen + numFramesToKeepObjects <= currentFrame) {
        bool keepAlive = false;

        // Only anti-cull RIs that have been matched at least once after creation.
        const bool isStable = (replacementInstance->frameLastSeen > replacementInstance->frameCreated);

        if (!isCameraCut && isStable) {
          // Skinned and player model objects should always be GC'd when stale —
          // anti-culling them produces frozen poses or wrong positions.
          // Objects tagged IgnoreAntiCulling by the game config are also exempt.
          // Translation-animated objects (transform changed in their last update)
          // are also exempt: anti-culling them freezes them mid-motion at the
          // last-seen position, which is rarely the right answer for a moving
          // entity that the game has finished drawing.
          // Note: the old system also exempted spritesheet-animated objects, but that
          // was likely a conservative safeguard — spritesheet state isn't broken by
          // anti-culling the way skeletal pose is.
          const CategoryFlags categories(replacementInstance->categoryFlags);
          const bool wasMovingWhenLastSeen =
              replacementInstance->dirtyFlags.test(ReplacementInstance::DirtyFlag::Transform);
          const bool exemptFromAntiCulling =
              replacementInstance->isSkinned ||
              categories.test(InstanceCategories::ThirdPersonPlayerModel) ||
              categories.test(InstanceCategories::IgnoreAntiCulling) ||
              wasMovingWhenLastSeen;

          // Object anti-culling: keep objects that are OUTSIDE the camera frustum.
          // If the game stopped submitting an object that's clearly visible, it
          // likely had a valid reason (destruction, LOD swap, etc.) — allow GC.
          // Objects outside the frustum may have been wrongly culled by the game's
          // own frustum (which doesn't match ours) and should be preserved.
          if (!keepAlive && objectAntiCullingEnabled && !forceGC
              && isAntiCullingSupported && hasMeshes && !exemptFromAntiCulling) {
            keepAlive = !aabbIntersectsFrustum(
                camera,
                replacementInstance->geometryBoundingBox,
                replacementInstance->objectToWorld);
          }

          // Light anti-culling for mesh replacement lights. Mirrors the old
          // LightManager MeshReplacement behavior: isDynamic lights inside
          // the frustum were GC'd immediately — only lights OUTSIDE the
          // camera frustum were protected, and only within the extended
          // lifetime window. Uses the camera frustum (not the wider light
          // anti-culling frustum) since the check is position-based like
          // object anti-culling.
          if (!keepAlive && lightAntiCullingEnabled && hasLights
              && isAntiCullingSupported && !exemptFromAntiCulling && !forceGC) {
            const bool withinExtendedLifetime =
                replacementInstance->frameLastSeen + numFramesToKeepLights > currentFrame;
            if (withinExtendedLifetime) {
              keepAlive = !aabbIntersectsFrustum(
                  camera,
                  replacementInstance->lightBoundingBox,
                  replacementInstance->objectToWorld);
            }
          }
        }

        if (!keepAlive) {
          destroyReplacementInstance(replacementInstance);
          std::swap(m_replacementInstances[i], m_replacementInstances.back());
          m_replacementInstances.pop_back();
          continue;
        }
      }
      ++i;
    }
  }

  void DrawCallTracker::clear() {
    m_identityHashMap.clear();
    m_assetSpatialMaps.clear();
    m_movedInstanceClusters.clear();
    m_instanceClusterBucketStats.clear();
    m_replacementInstances.clear();
  }

  void DrawCallTracker::rebuildSpatialMaps(float cellSize) {
    for (auto& [hash, spatialMap] : m_assetSpatialMaps) {
      spatialMap.rebuild(cellSize);
    }
  }

  void DrawCallTracker::repairCluster(std::vector<ReplacementInstance*>& cluster,
      uint32_t currentFrameId) {
    const uint32_t n = static_cast<uint32_t>(cluster.size());

    // Sort by stable RI id so the assignment depends only on positions, not submission order.
    std::sort(cluster.begin(), cluster.end(),
        [](const ReplacementInstance* a, const ReplacementInstance* b) { return a->id < b->id; });

    // Pair each current position (centroid) with the previous position (prevCentroid) it continues
    // from. reEvaluatedIndexArray[i] == i means "new". Only instances that existed last frame are
    // valid previous slots.
    struct Candidate {
      float distSqr;
      uint32_t previousIdx;
    };

    // Step 1: rank valid previous slots per current instance by distance.
    std::vector<std::vector<Candidate>> candidates(n);
    for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
      std::vector<Candidate>& currentCandidates = candidates[currentIdx];
      currentCandidates.reserve(n);
      for (uint32_t previousIdx = 0; previousIdx < n; ++previousIdx) {
        // Skip slots created this frame (no previous-frame history).
        if (cluster[previousIdx]->frameCreated == currentFrameId) {
          continue;
        }
        currentCandidates.push_back(
            { lengthSqr(cluster[currentIdx]->centroid - cluster[previousIdx]->prevCentroid), previousIdx });
      }
      std::sort(currentCandidates.begin(), currentCandidates.end(),
          [](const Candidate& a, const Candidate& b) {
            if (a.distSqr != b.distSqr) {
              return a.distSqr < b.distSqr;
            }
            return a.previousIdx < b.previousIdx;   // deterministic tie-break
          });
    }

    // Step 2: greedily assign the globally closest current<->previous pair, remove the consumed
    // previous from remaining lists, and repeat. Currents with an empty list stay new.
    std::vector<uint32_t> reEvaluatedIndexArray(n);
    for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
      reEvaluatedIndexArray[currentIdx] = currentIdx;   // default: new (self-mapping)
    }
    std::vector<bool> currentAssigned(n, false);
    for (uint32_t round = 0; round < n; ++round) {
      // Pick the unassigned current whose closest available previous is globally smallest.
      uint32_t bestCurrentIdx = UINT32_MAX;
      float bestDistSqr = FLT_MAX;
      for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
        if (currentAssigned[currentIdx] || candidates[currentIdx].empty()) {
          continue;
        }
        const float frontDistSqr = candidates[currentIdx][0].distSqr;
        if (bestCurrentIdx == UINT32_MAX || frontDistSqr < bestDistSqr) {
          bestDistSqr = frontDistSqr;
          bestCurrentIdx = currentIdx;
        }
      }

      // No more assignable currents -- the rest stay new.
      if (bestCurrentIdx == UINT32_MAX) {
        break;
      }

      const uint32_t chosenPreviousIdx = candidates[bestCurrentIdx][0].previousIdx;
      reEvaluatedIndexArray[bestCurrentIdx] = chosenPreviousIdx;
      currentAssigned[bestCurrentIdx] = true;

      // Remove the consumed previous from every still-unassigned current's list.
      for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
        if (currentAssigned[currentIdx]) {
          continue;
        }
        std::vector<Candidate>& currentCandidates = candidates[currentIdx];
        for (size_t pos = 0; pos < currentCandidates.size(); ++pos) {
          if (currentCandidates[pos].previousIdx == chosenPreviousIdx) {
            currentCandidates.erase(currentCandidates.begin() + pos);
            break;
          }
        }
      }
    }

    // Work is needed if any pairing changed or any instance was left new (it may hold stale previous
    // state to reset below).
    bool needsWork = false;
    for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
      if (reEvaluatedIndexArray[currentIdx] != currentIdx || !currentAssigned[currentIdx]) {
        needsWork = true;
        break;
      }
    }
    if (!needsWork) {
      return;
    }

    // Snapshot per-prim previous-frame state before mutating, since the permutation reads values
    // other iterations overwrite.
    struct PrevPrimState {
      bool isInstance = false;
      Matrix4 prevObjectToWorld;
      uint32_t prevSurfaceIndex = 0;
    };
    std::vector<std::vector<PrevPrimState>> snapshot(n);
    for (uint32_t instanceIdx = 0; instanceIdx < n; ++instanceIdx) {
      const std::vector<PrimInstance>& prims = cluster[instanceIdx]->prims;
      snapshot[instanceIdx].resize(prims.size());
      for (size_t primIdx = 0; primIdx < prims.size(); ++primIdx) {
        RtInstance* inst = prims[primIdx].getInstance();
        if (inst != nullptr) {
          snapshot[instanceIdx][primIdx] = { true, inst->surface.prevObjectToWorld, inst->getPreviousSurfaceIndex() };
        }
      }
    }

    for (uint32_t currentIdx = 0; currentIdx < n; ++currentIdx) {
      const uint32_t reEvaluatedIdx = reEvaluatedIndexArray[currentIdx];

      // Leftover-new instance: reset previous-frame state to current (zero motion) so it is new.
      if (!currentAssigned[currentIdx]) {
        ReplacementInstance* ri = cluster[currentIdx];
        ri->prevCentroid = ri->centroid;
        for (PrimInstance& prim : ri->prims) {
          RtInstance* inst = prim.getInstance();
          if (inst != nullptr) {
            // prevObjectToWorld = current => zero motion; prev surface = current surface.
            inst->reassignPreviousFrameState(inst->surface.objectToWorld, inst->getSurfaceIndex());
          }
        }
        continue;
      }

      if (reEvaluatedIdx == currentIdx) {
        continue;
      }

      // Same spatialMapHash => same prim layout, so prim i maps to prim i. Guard against structural
      // mismatch.
      std::vector<PrimInstance>& prims = cluster[currentIdx]->prims;
      const std::vector<PrevPrimState>& reEvaluatedPrims = snapshot[reEvaluatedIdx];
      const size_t primCount = std::min(prims.size(), reEvaluatedPrims.size());
      for (size_t primIdx = 0; primIdx < primCount; ++primIdx) {
        RtInstance* inst = prims[primIdx].getInstance();
        const PrevPrimState& primState = reEvaluatedPrims[primIdx];
        if (inst != nullptr && primState.isInstance) {
          inst->reassignPreviousFrameState(primState.prevObjectToWorld, primState.prevSurfaceIndex);
        }
      }
    }
  }

  void DrawCallTracker::repairClusteredInstanceHistory(uint32_t currentFrameId) {
    ScopedCpuProfileZone();

    // repairCluster scales cubically with cluster size, so skip clusters larger than this to
    // bound the per-frame cost. A cap below 2 disables the repair pass entirely.
    const uint32_t maxClusterSize = RtxOptions::maxInstanceHistoryRepairClusterSize();
    if (maxClusterSize < 2) {
      m_movedInstanceClusters.clear();
      return;
    }

    const bool requireClusterChange = RtxOptions::repairInstanceMatchOnlyWhenClusterChanges();

    for (auto& [spatialMapHash, bufferBuckets] : m_movedInstanceClusters) {
      // Each source-buffer bucket is repaired independently -- instances only continue from a
      // previous sharing the same mesh and source vertex buffer.
      for (auto& [srcVtxBuffer, movedCluster] : bufferBuckets) {
        // Need at least 2 instances for a mix-up to be possible.
        if (movedCluster.size() < 2) {
          continue;
        }

        // Optionally restrict repair to clusters that have been altered since last frame. A cluster is considered altered if
        // either the instance count differs from last frame, or the count matches but an instance
        // was destroyed this frame (a destroy masked by an add).
        if (requireClusterChange) {
          auto clusterStatsIter =
              m_instanceClusterBucketStats.find(computeInstanceClusterBucketKey(spatialMapHash, srcVtxBuffer));
          if (clusterStatsIter != m_instanceClusterBucketStats.end()) {
            const InstanceClusterBucketStats& stats = clusterStatsIter->second;
            const bool clusterAltered = stats.currentCount != stats.previousCount ||
              stats.instanceDestroyedCurrentFrame;
            if (!clusterAltered) {
              continue;
            }
          } 
        }

        std::vector<ReplacementInstance*> cluster;
        cluster.reserve(movedCluster.size());
        for (ReplacementInstance* ri : movedCluster) {
          // Skip prim-less instances.
          if (ri->prims.empty()) {
            continue;
          }
          if (std::find(cluster.begin(), cluster.end(), ri) == cluster.end()) {
            cluster.push_back(ri);
            if (cluster.size() > maxClusterSize)
              break;
          }
        }

        // Repair clusters of 2+ instances, up to the configured size cap.
        if (cluster.size() >= 2 && cluster.size() <= maxClusterSize) {
          repairCluster(cluster, currentFrameId);
        }
      }
    }
    m_movedInstanceClusters.clear();
  }

}  // namespace dxvk
