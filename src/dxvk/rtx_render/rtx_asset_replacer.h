/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "rtx_types.h"
#include "graph/rtx_graph_types.h"
#include "rtx_lights.h"
#include "rtx_mod_manager.h"
#include "rtx_utils.h"
#include "rtx_lights_data.h"

namespace dxvk {
  class DxvkContext;
  class DxvkDevice;
  class DxvkCommandList;

  struct Categorizer {
    CategoryFlags categoryFlags;
    CategoryFlags categoryExists;

    CategoryFlags applyCategoryFlags(const CategoryFlags& input) const {
      return (input.raw() & (~categoryExists.raw())) | categoryFlags.raw();
    }
  };

  struct MeshReplacement {
    RasterGeometry data;
  };

  constexpr uint32_t kInvalidPointInstanceIndex = std::numeric_limits<uint32_t>::max();

  struct AssetReplacement {
    enum Type {
      eMesh,
      eLight,
      eGraph,
      eNone,
    };
    Categorizer categories;
    // Shared pointers because the BlasEntry / GraphInstance can outlive the AssetReplacement
    std::shared_ptr<MeshReplacement> geometry;
    std::shared_ptr<RtGraphState> graphState;

    std::optional<RtxParticleSystemDesc> particleSystem;
    std::optional<LightData> lightData;
    // Shared pointer because multiple AssetReplacements can use the same material.
    // Set to null if the mesh should use the original material instead
    std::shared_ptr<MaterialData> materialData;
    // Stable identity hash of the bound material (same key as AssetReplacements::m_materials),
    // kEmptyHash if materialData is null. collectMeshesUsingMaterials matches on this rather
    // than materialData's pointer, which isn't a guaranteed-stable identity for a shared material.
    XXH64_hash_t materialPathHash = kEmptyHash;
    Matrix4 replacementToObject;
    // If this replacement represents multiple instances of an object, then this will contain a
    // list of transforms from the instance's space to Object space
    // (use the drawcall's objectToWorld * instancesToObject[n] to get instance n's world transform).
    // Stored as shared_ptr so downstream consumers (DrawCallTransforms, RtSurface,
    // PointInstancerBatch) can safely reference the data across frame boundaries.
    std::shared_ptr<std::vector<Matrix4>> instancesToObject;
    // If this replacement represents a single instance from a pointInstancer, then this will 
    // contain the index of the instance in the pointInstancer.
    uint32_t pointInstanceIndex = kInvalidPointInstanceIndex; 
    Type type;
    bool includeOriginal = false;

    std::string primPath;
    XXH64_hash_t usdPathHash = kEmptyHash;

    AssetReplacement(const std::string& primPath) :
      type(eNone),
      primPath(primPath),
      usdPathHash(XXH3_64bits(primPath.c_str(), primPath.size()))
    {}
    AssetReplacement(
        const std::string& primPath, 
        std::shared_ptr<MeshReplacement> geometryData,
        std::shared_ptr<MaterialData> materialData,
        Categorizer categoryFlags,
        const Matrix4& replacementToObject) :
      geometry(std::move(geometryData)),
      materialData(std::move(materialData)),
      categories(categoryFlags),
      replacementToObject(replacementToObject),
      type(eMesh),
      primPath(primPath),
      usdPathHash(XXH3_64bits(primPath.c_str(), primPath.size()))
    {}
    AssetReplacement(
        const std::string& primPath,
        const LightData& lightData,
        const Matrix4& replacementToObject) :
      lightData(lightData),
      replacementToObject(replacementToObject),
      type(eLight),
      primPath(primPath),
      usdPathHash(XXH3_64bits(primPath.c_str(), primPath.size()))
    {}
  };

  // The replacements a mesh or light hash resolves to. Wrapped in a struct and handed
  // out by shared_ptr so a ReplacementInstance owns what it points at:
  struct ReplacementBucket {
    std::vector<AssetReplacement> replacements;
    // Indicates anything using this ReplacementBucket should stop rendering and be cleaned up
    mutable std::atomic<bool> stale { false };
  };

  struct SecretReplacement {
    const std::string header;
    const std::string name;
    const std::string description;
    const XXH64_hash_t unlockHash;
    const XXH64_hash_t assetHash;
    const std::string replacementPath;
    const bool bDisplayBeforeUnlocked;
    // Instance tracking necessary to set this to false
    const bool bExclusiveReplacement = true; 
    const size_t variantId;
  };

  typedef fast_unordered_cache<std::vector<SecretReplacement>> SecretReplacements;

  // What changed in a rebuild. Passed from the mod to SceneManager to minimize scene invalidation.
  struct AssetChanges {
    // Mesh/light hashes whose replacement buckets changed (added, removed, or modified).
    std::unordered_set<XXH64_hash_t> dirtyMeshHashes;
    std::unordered_set<XXH64_hash_t> dirtyLightHashes;
    // Material hashes that changed.
    std::unordered_set<XXH64_hash_t> dirtyMatHashes;

    // When true, all existing assets of this category are dirty.
    bool fullMeshRebuild  = false;
    bool fullLightRebuild = false;
    bool fullMatRebuild   = false;

    bool empty() const {
      return dirtyMeshHashes.empty() && dirtyLightHashes.empty() && dirtyMatHashes.empty()
          && !fullMeshRebuild && !fullLightRebuild && !fullMatRebuild;
    }

    void merge(const AssetChanges& other) {
      dirtyMeshHashes.insert(other.dirtyMeshHashes.begin(), other.dirtyMeshHashes.end());
      dirtyLightHashes.insert(other.dirtyLightHashes.begin(), other.dirtyLightHashes.end());
      dirtyMatHashes.insert(other.dirtyMatHashes.begin(), other.dirtyMatHashes.end());
      fullMeshRebuild  = fullMeshRebuild  || other.fullMeshRebuild;
      fullLightRebuild = fullLightRebuild || other.fullLightRebuild;
      fullMatRebuild   = fullMatRebuild   || other.fullMatRebuild;
    }
  };

  // Asset replacements storage class.
  // Contains and owns the replacements, material and geometry objects.
  class AssetReplacements {
  public:
    // Returns the bucket of replacements of type T for a given hash value, or a nullptr
    // if no replacements were found. The strong ref is copied under the lock, so the
    // caller's bucket stays alive even if another thread replaces the map entry.
    template<AssetReplacement::Type T>
    std::shared_ptr<const ReplacementBucket> get(XXH64_hash_t hash) {
      static_assert(T == AssetReplacement::eMesh || T == AssetReplacement::eLight,
                    "Only mesh and light replacements are bucketed by hash.");
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto& map = T == AssetReplacement::eMesh ? m_meshReplacers : m_lightReplacers;
      auto it = map.find(hash);
      if (it != map.end()) {
        return it->second;
      }
      return nullptr;
    }

    // Stores replacements of type T for a hash value, replacing any existing bucket. If two
    // prims share a hash, the one processed last wins (storeCached keeps the first instead).
    template<AssetReplacement::Type T>
    void set(XXH64_hash_t hash, std::vector<AssetReplacement>&& v) {
      static_assert(T == AssetReplacement::eMesh || T == AssetReplacement::eLight,
                    "Only mesh and light replacements are bucketed by hash.");
      auto bucket = std::make_shared<ReplacementBucket>();
      bucket->replacements = std::move(v);

      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto& map = T == AssetReplacement::eMesh ? m_meshReplacers : m_lightReplacers;
      map.insert_or_assign(hash, std::move(bucket));
    }

    // Stores the object of type T for a hash value.
    template<typename T>
    T& storeObject(XXH64_hash_t hash, T&& obj) {
      static_assert(std::is_same_v<T, SecretReplacement>,
                    "Only secret replacements use the generic store; the cached per-prim "
                    "objects are shared_ptr-owned - use storeGeometry/storeMaterial/storeTopology.");
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      return m_secretReplacements[hash].emplace_back(obj);
    }

    bool getMaterial(XXH64_hash_t hash, std::shared_ptr<MaterialData>& obj) {
      return getCached(m_materials, hash, obj);
    }

    bool getGeometry(XXH64_hash_t hash, std::shared_ptr<MeshReplacement>& obj) {
      return getCached(m_geometries, hash, obj);
    }

    bool getTopology(XXH64_hash_t hash, std::shared_ptr<const RtGraphTopology>& obj) {
      return getCached(m_graphTopologies, hash, obj);
    }

    std::shared_ptr<MaterialData> storeMaterial(XXH64_hash_t hash, MaterialData&& obj) {
      return storeCached(m_materials, hash, std::move(obj));
    }

    // Records path->hash so a deleted prim (which leaves no primSpec to re-derive its hash
    // from) can still be classified. Call on every resolution, even a cache hit, so the
    // freshest path wins.
    void registerMaterialPath(const std::string& path, XXH64_hash_t hash) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      m_materialPathIndex.insert_or_assign(path, hash);
    }

    // Finds registered material hashes at or under `path` - covers a subtree deletion,
    // which resyncs an ancestor path rather than the material's own path.
    void collectMaterialHashesUnderPath(const std::string& path,
                                         std::unordered_set<XXH64_hash_t>& outHashes) {
      if (path.empty() || path == "/") {
        return;
      }
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      for (const auto& [matPath, hash] : m_materialPathIndex) {
        if (matPath == path ||
            (matPath.size() > path.size() &&
             matPath.compare(0, path.size(), path) == 0 &&
             matPath[path.size()] == '/')) {
          outHashes.insert(hash);
        }
      }
    }

    std::shared_ptr<MeshReplacement> storeGeometry(XXH64_hash_t hash, MeshReplacement&& obj) {
      return storeCached(m_geometries, hash, std::move(obj));
    }

    std::shared_ptr<const RtGraphTopology> storeTopology(XXH64_hash_t hash, RtGraphTopology&& obj) {
      return storeCached(m_graphTopologies, hash, std::move(obj));
    }

    // Finds meshes bound to any of the given materials, by identity hash - materialData's
    // pointer isn't a guaranteed-stable identity for a shared material.
    void collectMeshesUsingMaterials(const std::unordered_set<XXH64_hash_t>& dirtyMatHashes,
                                      std::unordered_set<XXH64_hash_t>& outMeshHashes) {
      if (dirtyMatHashes.empty()) {
        return;
      }
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      for (const auto& [meshHash, bucket] : m_meshReplacers) {
        if (!bucket) {
          continue;
        }
        for (const auto& replacement : bucket->replacements) {
          if (replacement.materialPathHash != kEmptyHash && dirtyMatHashes.count(replacement.materialPathHash)) {
            outMeshHashes.insert(meshHash);
            break;
          }
        }
      }
    }

    // Seeds the geometry cache from src. Called before a material-triggered
    // full mesh re-process so unchanged geometries hit the cache and are not
    // re-uploaded to the GPU.
    void seedGeometriesFrom(const AssetReplacements& src) {
      // Copy under src's lock, then move in under ours - avoids holding both at once.
      decltype(m_geometries) copy;
      {
        std::lock_guard<sync::Spinlock> lock(src.m_spinlock);
        copy = src.m_geometries;
      }
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      m_geometries = std::move(copy);
    }

    // Seeds the material cache from src, skipping dirtyHashes (includes deletions - a
    // dirty hash is never seeded, so mergeFrom's absent-from-src cleanup still erases it).
    // Called before processing so a mesh reprocessed for an unrelated reason (e.g. its own
    // geometry changed) reuses an unchanged material's existing object instead of
    // re-deserializing a content-identical duplicate.
    void seedMaterialsFrom(const AssetReplacements& src, const std::unordered_set<XXH64_hash_t>& dirtyHashes) {
      decltype(m_materials) copy;
      {
        std::lock_guard<sync::Spinlock> lock(src.m_spinlock);
        copy = src.m_materials;
      }
      for (const auto hash : dirtyHashes) {
        copy.erase(hash);
      }
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      m_materials = std::move(copy);
    }

    // Merges src into this table. Dirty hashes absent from changes (deleted prims) are erased;
    // present hashes overwrite the live entry. fullMatRebuild pre-populates changes.dirtyMatHashes.
    void mergeFrom(AssetReplacements&& src, AssetChanges& changes) {
      // std::scoped_lock over one non-recursive Spinlock held twice would spin forever.
      if (this == &src) {
        assert(false && "mergeFrom: cannot merge a table into itself");
        return;
      }

      // Replaced/erased entries are destroyed after the lock below is released, not while
      // other threads are busy-spinning on it.
      std::vector<std::shared_ptr<void>> doomed;
      {
        std::scoped_lock lock(m_spinlock, src.m_spinlock);

        const auto staleAndReplace = [&doomed](auto& map, auto& srcMap,
                                        const std::unordered_set<XXH64_hash_t>& dirty,
                                        bool fullRebuild) {
          for (auto& [hash, newBucket] : srcMap) {
            auto it = map.find(hash);
            if (it != map.end()) {
              it->second->stale.store(true, std::memory_order_release);
              doomed.push_back(std::move(it->second));
            }
            map.insert_or_assign(hash, std::move(newBucket));
          }
          if (fullRebuild) {
            // In a full rebuild for this type, mark all old entries as stale to clear live instances.
            for (auto it = map.begin(); it != map.end(); ) {
              if (srcMap.find(it->first) == srcMap.end()) {
                it->second->stale.store(true, std::memory_order_release);
                doomed.push_back(std::move(it->second));
                it = map.erase(it);
              } else {
                ++it;
              }
            }
          } else {
            for (const auto hash : dirty) {
              if (srcMap.find(hash) == srcMap.end()) {
                auto it = map.find(hash);
                if (it != map.end()) {
                  it->second->stale.store(true, std::memory_order_release);
                  doomed.push_back(std::move(it->second));
                  map.erase(it);
                }
              }
            }
          }
        };
        staleAndReplace(m_meshReplacers,  src.m_meshReplacers,  changes.dirtyMeshHashes,  changes.fullMeshRebuild);
        staleAndReplace(m_lightReplacers, src.m_lightReplacers, changes.dirtyLightHashes, changes.fullLightRebuild);

        if (changes.fullMatRebuild) {
          for (const auto& [hash, _] : m_materials) {
            changes.dirtyMatHashes.insert(hash);
          }
        }

        for (auto& [hash, mat] : src.m_materials) {
          auto it = m_materials.find(hash);
          // Only a genuine content change should invalidate RIs referencing this material;
          // a reprocessed-but-identical material (e.g. swept up by a full mat rebuild) must
          // not force every matching instance back onto the dynamic path.
          if (it == m_materials.end() || !it->second || !mat ||
              it->second->getHash() != mat->getHash()) {
            changes.dirtyMatHashes.insert(hash);
          }
          if (it != m_materials.end()) {
            doomed.push_back(std::move(it->second));
          }
          // Always replace with src's object: mesh replacements merged in above reference
          // it, and collectMeshesUsingMaterials classifies by materialPathHash.
          m_materials.insert_or_assign(hash, std::move(mat));
        }
        // Keyed by path, not hash: a re-registered path overwrites in place, and an entry
        // absent from src (unprocessed this reload) is left alone, not treated as deleted.
        for (auto& [path, hash] : src.m_materialPathIndex) {
          m_materialPathIndex.insert_or_assign(path, hash);
        }
        for (auto& [hash, geom] : src.m_geometries) {
          auto it = m_geometries.find(hash);
          if (it != m_geometries.end()) {
            // Same object seedGeometriesFrom already installed here; retiring it into
            // doomed would hide it from sweepOrphans's use_count()==1 check below.
            if (it->second == geom) {
              continue;
            }
            doomed.push_back(std::move(it->second));
          }
          m_geometries.insert_or_assign(hash, std::move(geom));
        }
        for (auto& [hash, topo] : src.m_graphTopologies) {
          auto it = m_graphTopologies.find(hash);
          if (it != m_graphTopologies.end()) {
            doomed.push_back(std::move(it->second));
          }
          m_graphTopologies.insert_or_assign(hash, std::move(topo));
        }

        for (const auto hash : changes.dirtyMatHashes) {
          if (src.m_materials.find(hash) == src.m_materials.end()) {
            auto it = m_materials.find(hash);
            if (it != m_materials.end()) {
              doomed.push_back(std::move(it->second));
              m_materials.erase(it);
            }
            // Drop this hash's path-index entries too, so repeated delete/recreate cycles
            // at different paths don't grow the index unboundedly.
            for (auto pit = m_materialPathIndex.begin(); pit != m_materialPathIndex.end(); ) {
              if (pit->second == hash) {
                pit = m_materialPathIndex.erase(pit);
              } else {
                ++pit;
              }
            }
          }
        }

        if (!changes.dirtyMeshHashes.empty() || changes.fullMeshRebuild) {
          // Use shared pointer ref counts to clean up cached mesh data that is no longer used.
          const auto sweepOrphans = [&doomed](auto& cache) {
            for (auto it = cache.begin(); it != cache.end(); ) {
              if (it->second.use_count() == 1) {
                doomed.push_back(std::move(it->second));
                it = cache.erase(it);
              } else {
                ++it;
              }
            }
          };
          sweepOrphans(m_geometries);
          sweepOrphans(m_graphTopologies);
        }
      }
    }

    // Destroys all replacements and stored objects.
    void clear() {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      m_meshReplacers.clear();
      m_lightReplacers.clear();
      m_materials.clear();
      m_geometries.clear();
      m_graphTopologies.clear();
      m_materialPathIndex.clear();
      m_secretReplacements.clear();
    }

    const SecretReplacements& secretReplacements() const {
      return m_secretReplacements;
    }

  private:
    template<typename Cache, typename Ptr>
    bool getCached(Cache& cache, XXH64_hash_t hash, Ptr& obj) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto it = cache.find(hash);
      if (it != cache.end() && it->second) {
        obj = it->second;
        return true;
      }
      return false;
    }

    template<typename Cache, typename T>
    auto storeCached(Cache& cache, XXH64_hash_t hash, T&& obj) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto [it, inserted] = cache.try_emplace(hash);
      if (inserted || !it->second) {
        it->second = std::make_shared<std::decay_t<T>>(std::forward<T>(obj));
      }
      return it->second;
    }

    mutable sync::Spinlock m_spinlock;

    // Replacements ready to be fed to the renderer
    fast_unordered_cache<std::shared_ptr<ReplacementBucket>> m_meshReplacers;
    fast_unordered_cache<std::shared_ptr<ReplacementBucket>> m_lightReplacers;

    // Per-prim cached objects: geometry, materials, and graph topologies.
    // Handed out by shared_ptr so consumers keep them alive past a hot-reload swap.
    fast_unordered_cache<std::shared_ptr<MeshReplacement>> m_geometries;
    fast_unordered_cache<std::shared_ptr<MaterialData>> m_materials;
    fast_unordered_cache<std::shared_ptr<RtGraphTopology>> m_graphTopologies;

    // Material USD path -> identity hash, kept for deletion classification. See
    // registerMaterialPath / collectMaterialHashesUnderPath.
    std::unordered_map<std::string, XXH64_hash_t> m_materialPathIndex;

    // Secret replacements if any
    SecretReplacements m_secretReplacements;
  };

  struct AssetReplacer {
    std::shared_ptr<const ReplacementBucket> getReplacementsForMesh(XXH64_hash_t hash);
    std::shared_ptr<const ReplacementBucket> getReplacementsForLight(XXH64_hash_t hash);
    std::shared_ptr<MaterialData> getReplacementMaterial(XXH64_hash_t hash);

    // process the replacement USD and create all the m_replacements entries.
    void initialize(const Rc<DxvkContext>& context);

    // returns true if the state of replacements has changed.
    bool checkForChanges(const Rc<DxvkContext>& context);

    // Applies any ready background hot-reloads. Returns true iff at least one mod was
    // updated this call, and merges each mod's AssetChanges into changes.
    bool applyPendingRebuilds(const Rc<DxvkContext>& context, AssetChanges& changes);

    // Device teardown hook: lets every mod join its background workers while the
    // rest of DxvkObjects is still alive.
    void onDestroy();

    // Abandon any in-progress mod load. Called by RtxInitializer::onDestroy.
    void cancelLoading() {
      m_modManager.cancelLoading();
    }


    // Returns true if at least one mod has been discovered (regardless of load state).
    bool hasAnyMods() const;
    std::vector<Mod::State> getReplacementStates() const;

    // Queues a manual reload of every mod, picked up on the next checkForChanges.
    void requestReload();
    // True while any mod has a reload queued or in flight, so the UI can disable the
    // button rather than letting clicks pile up.
    bool isReloadPending() const;

    const bool hasNewSecretReplacementInfo() const {
      return m_bSecretReplacementsUpdated;
    }

    const SecretReplacements& getSecretReplacementInfo() {
      assert(m_bSecretReplacementsUpdated);
      m_bSecretReplacementsUpdated = false;
      return m_secretReplacements;
    }

    void markVariantStatus(const XXH64_hash_t assetHash,
                           const size_t variantId,
                           const bool bEnabled) {
      m_variantInfos[assetHash].selectedVariant =
        (bEnabled) ? variantId : VariantInfo::kDefaultVariant;
    }

    void makeMaterialWithTexturePreload(DxvkContext& ctx, remixapi_MaterialHandle handle, MaterialData&& data);
    [[nodiscard]] const MaterialData* accessExternalMaterial(remixapi_MaterialHandle handle) const;
    void destroyExternalMaterial(remixapi_MaterialHandle handle);

    void registerExternalMesh(remixapi_MeshHandle handle, std::vector<RasterGeometry>&& submeshes);
    // Shared pointer because destroyExternalMesh can erase the entry while
    // cached draw calls still point into it.
    [[nodiscard]] std::shared_ptr<const std::vector<RasterGeometry>> accessExternalMesh(remixapi_MeshHandle handle) const;
    void destroyExternalMesh(remixapi_MeshHandle handle);

  private:
    void updateSecretReplacements();

    bool m_bSecretReplacementsUpdated = false;

    struct VariantInfo {
      static constexpr size_t kDefaultVariant = 0;
      size_t selectedVariant = kDefaultVariant;
    };

    fast_unordered_cache<VariantInfo> m_variantInfos;
    SecretReplacements m_secretReplacements;

    ModManager m_modManager;

    std::unordered_map<remixapi_MaterialHandle, std::optional<MaterialData>> m_extMaterials {};
    std::unordered_map<remixapi_MeshHandle, std::shared_ptr<std::vector<RasterGeometry>>> m_extMeshes {};
  };
} // namespace dxvk

