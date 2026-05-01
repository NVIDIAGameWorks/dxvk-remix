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
#include "rtx_ray_portal_manager.h"
#include "rtx_intersection_test.h"
#include "dxvk_device.h"


namespace dxvk {

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

  XXH64_hash_t DrawCallTracker::computeIdentityHash(const DrawCallState& drawCallState) {
    struct IdentityHashData{
      XXH64_hash_t geoHash;
      XXH64_hash_t matHash;
      XXH64_hash_t boneHash;
      CameraType::Enum cameraType;
      uint32_t categories;
      Matrix4 xform;
    };

    // 0 initialize to avoid any problems caused by padding
    IdentityHashData data{};

    data.geoHash = drawCallState.getGeometryData().getHashForRule(rules::FullGeometryHash);
    data.matHash = drawCallState.getMaterialData().getHash();
    data.xform = drawCallState.getTransformData().objectToWorld;
    data.categories = drawCallState.getCategoryFlags().raw();
    data.boneHash = drawCallState.getSkinningState().boneHash;
    data.cameraType = drawCallState.cameraType;

    return XXH3_64bits(&data, sizeof(data));
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
      const InstanceLookupKey& key,
      ReplacementSpatialMap* moveInAssetMap,
      bool moveInMaterialMap) {
    m_identityHashMap.erase(match->identityHash);
    match->identityHash = key.identityHash;
    match->vertexPositionHash = key.vertexPositionHash;
    match->centroid = key.worldPos;

    if (moveInAssetMap) {
      match->spatialCacheTransformHash = moveInAssetMap->move(
          match->spatialCacheTransformHash, key.worldPos, key.transform, match);
    }

    if (moveInMaterialMap) {
      auto matMapIter = m_materialSpatialMaps.find(match->materialHash);
      if (matMapIter != m_materialSpatialMaps.end()) {
        match->materialSpatialCacheTransformHash = matMapIter->second.move(
            match->materialSpatialCacheTransformHash, key.worldPos, key.transform, match);
      }
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

  ReplacementInstance* DrawCallTracker::findOrCreateReplacementInstance(
      const InstanceLookupKey& key) {
    const uint32_t currentFrameId = m_device->getCurrentFrameId();

    // Level 1: exact identity match.
    // Draw calls with the same identity hash (same geometry, material, transform) return
    // the same ReplacementInstance even if already seen this frame. This handles two-pass rendering where
    // the game draws the same mesh twice (e.g., base pass + overlay). The second pass
    // merges into the same instance via mergeInstanceHeuristics in updateInstance.
    auto exactMatchIter = m_identityHashMap.find(key.identityHash);
    if (exactMatchIter != m_identityHashMap.end()) {
      return exactMatchIter->second;
    }

    // Level 2: tracking hash (topological, stable for animated geometry) + spatial proximity
    const float uniqueObjectDistanceSqr = RtxOptions::getUniqueObjectDistanceSqr();
    const float spatialMapCellSize = RtxOptions::uniqueObjectDistance() * 2.f;

    auto l2Filter = [&](const ReplacementInstance* candidate) {
      return candidate->frameLastSeen != currentFrameId &&
             candidate->materialHash == key.materialHash;
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
        m_identityHashMap.erase(exactTransformMatch->identityHash);
        exactTransformMatch->identityHash = key.identityHash;
        m_identityHashMap[key.identityHash] = exactTransformMatch;
        return exactTransformMatch;
      }

      // Spatial nearest-neighbor search
      float nearestDistSqr = FLT_MAX;
      const ReplacementInstance* nearestMatch = spatialMapIter->second.getNearestData(
        key.worldPos, uniqueObjectDistanceSqr, nearestDistSqr, l2Filter);

      if (nearestMatch != nullptr) {
        return reassociateMatch(
            const_cast<ReplacementInstance*>(nearestMatch),
            key, &spatialMapIter->second, true);
      }
    }

    // Level 2.5: cross-topology fallback via material spatial index.
    // When animated geometry uses different index buffers per frame, both components of
    // the tracking hash (Indices + GeometryDescriptor) change, so the RI lives in a
    // different asset spatial map bucket. The material spatial map indexes all RIs by
    // material hash regardless of topology, allowing O(1) bucket lookup + spatial search.
    {
      auto frameFilter = [&](const ReplacementInstance* candidate) {
        return candidate->frameLastSeen != currentFrameId;
      };

      auto matMapIter = m_materialSpatialMaps.find(key.materialHash);
      if (matMapIter != m_materialSpatialMaps.end()) {
        float nearestDistSqr = FLT_MAX;
        const ReplacementInstance* crossMatch = matMapIter->second.getNearestData(
          key.worldPos, uniqueObjectDistanceSqr, nearestDistSqr, frameFilter);

        if (crossMatch != nullptr) {
          ReplacementInstance* bestMatch = const_cast<ReplacementInstance*>(crossMatch);

          // Migrate the RI from the old spatial map to the new one.
          eraseFromSpatialMap(m_assetSpatialMaps, bestMatch->spatialMapHash,
              bestMatch->spatialCacheTransformHash, bestMatch);

          bestMatch->spatialMapHash = key.spatialMapHash;
          auto [newMapIter, inserted] = m_assetSpatialMaps.try_emplace(key.spatialMapHash, spatialMapCellSize);
          bestMatch->spatialCacheTransformHash =
              newMapIter->second.insert(key.worldPos, key.transform, bestMatch);

          return reassociateMatch(bestMatch, key, nullptr, true);
        }
      }
    }

    // Level 3: no match — create a new ReplacementInstance
    auto newReplacementInstance = std::make_unique<ReplacementInstance>();
    ReplacementInstance* replacementInstance = newReplacementInstance.get();
    replacementInstance->id = m_nextReplacementInstanceId++;
    replacementInstance->identityHash = key.identityHash;
    replacementInstance->spatialMapHash = key.spatialMapHash;
    replacementInstance->materialHash = key.materialHash;
    replacementInstance->vertexPositionHash = key.vertexPositionHash;
    replacementInstance->centroid = key.worldPos;
    replacementInstance->frameCreated = currentFrameId;

    m_identityHashMap[key.identityHash] = replacementInstance;

    auto [mapIter, inserted] = m_assetSpatialMaps.try_emplace(key.spatialMapHash, spatialMapCellSize);
    replacementInstance->spatialCacheTransformHash =
        mapIter->second.insert(key.worldPos, key.transform, replacementInstance);

    auto [matMapIter, matInserted] = m_materialSpatialMaps.try_emplace(key.materialHash, spatialMapCellSize);
    replacementInstance->materialSpatialCacheTransformHash =
        matMapIter->second.insert(key.worldPos, key.transform, replacementInstance);

    m_replacementInstances.push_back(std::move(newReplacementInstance));

    return replacementInstance;
  }

  ReplacementInstance* DrawCallTracker::findOrCreateReplacementInstance(
      const DrawCallState& drawCallState,
      const MaterialData& materialData,
      const RayPortalManager& rayPortalManager) {
    // Tracking hash uses topology only (indices + geometry descriptor), matching how the
    // baseline grouped instances by BlasEntry (topological hash). The material hash is used
    // as a filter in the spatial search, not as part of the key — this allows matching
    // even when the material changes between frames (LOD, texture animation).
    const auto& hashes = drawCallState.getGeometryData().hashes;
    const Matrix4& objectToWorld = drawCallState.getTransformData().objectToWorld;

    const InstanceLookupKey key {
      computeIdentityHash(drawCallState),
      hashes.getHashForRule<rules::TopologicalHash>(),
      materialData.getHash(),
      hashes[HashComponents::VertexPosition],
      drawCallState.getGeometryData().boundingBox.getTransformedCentroid(objectToWorld),
      objectToWorld
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

  ReplacementInstance* DrawCallTracker::tryPortalMatch(
      ReplacementInstance* newInstance,
      const InstanceLookupKey& key,
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

          return reassociateMatch(
              const_cast<ReplacementInstance*>(portalMatch),
              key, &spatialMapIter->second, false);
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

    eraseFromSpatialMap(m_assetSpatialMaps, replacementInstance->spatialMapHash,
        replacementInstance->spatialCacheTransformHash, replacementInstance);
    eraseFromSpatialMap(m_materialSpatialMaps, replacementInstance->materialHash,
        replacementInstance->materialSpatialCacheTransformHash, replacementInstance);

    replacementInstance->clear();
  }

  void DrawCallTracker::garbageCollectReplacementInstances(RtCamera& camera, bool isAntiCullingSupported) {
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
          // Note: the old system also exempted spritesheet-animated objects, but that
          // was likely a conservative safeguard — spritesheet state isn't broken by
          // anti-culling the way skeletal pose is.
          const CategoryFlags categories(replacementInstance->categoryFlags);
          const bool exemptFromAntiCulling =
              replacementInstance->isSkinned ||
              categories.test(InstanceCategories::ThirdPersonPlayerModel) ||
              categories.test(InstanceCategories::IgnoreAntiCulling);

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
    m_materialSpatialMaps.clear();
    m_replacementInstances.clear();
  }

  void DrawCallTracker::rebuildSpatialMaps(float cellSize) {
    for (auto& [hash, spatialMap] : m_assetSpatialMaps) {
      spatialMap.rebuild(cellSize);
    }
    for (auto& [hash, spatialMap] : m_materialSpatialMaps) {
      spatialMap.rebuild(cellSize);
    }
  }

}  // namespace dxvk
