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

#include "rtx_types.h"
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

  struct AssetReplacement {
    enum Type {
      eMesh,
      eLight
    };
    Categorizer categories;
    MeshReplacement* geometry;
    std::optional<LightData> lightData;
    // Note: This is the material to use for this replacement, if any. Set to null if should use
    // the original material instead, similar to how getReplacementMaterial works.
    MaterialData* materialData;
    Matrix4 replacementToObject;
    Type type;
    bool includeOriginal = false;

    AssetReplacement(MeshReplacement* geometryData, MaterialData* materialData, Categorizer categoryFlags, const Matrix4& replacementToObject)
        : geometry(geometryData), materialData(materialData), categories(categoryFlags), replacementToObject(replacementToObject), type(eMesh) {}
    AssetReplacement(const LightData& lightData) : lightData(lightData), type(eLight) {}
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

  // Asset replacements storage class.
  // Contains and owns the replacements, material and geometry objects.
  class AssetReplacements {
  public:
    // Returns a pointer to replacements of type T for a given hash value,
    // or a nullptr if no replacements found.
    template<AssetReplacement::Type T>
    std::vector<AssetReplacement>* get(XXH64_hash_t hash) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto& map = T == AssetReplacement::eMesh ? m_meshReplacers : m_lightReplacers;
      auto it = map.find(hash);
      if (it != map.end()) {
        return &it->second;
      }
      return nullptr;
    }

    // Stores replacements of type T for a hash value.
    template<AssetReplacement::Type T>
    void set(XXH64_hash_t hash, std::vector<AssetReplacement>&& v) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      auto& map = T == AssetReplacement::eMesh ? m_meshReplacers : m_lightReplacers;
      map.emplace(hash, std::move(v));
    }

    // Returns a pointer to the stored object of type T for a given hash value.
    // Return false if no object was found.
    template<typename T>
    bool getObject(XXH64_hash_t hash, T*& obj) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      if constexpr (std::is_same_v<T, MaterialData>) {
        auto it = m_materials.find(hash);
        if (it != m_materials.end()) {
          obj = &it->second;
          return true;
        }
        return false;
      } else if constexpr (std::is_same_v<T, MeshReplacement>) {
        auto it = m_geometries.find(hash);
        if (it != m_geometries.end()) {
          obj = &it->second;
          return true;
        }
        return false;
      }
      return false;
    }

    // Stores the object of type T for a hash value.
    template<typename T>
    T& storeObject(XXH64_hash_t hash, T&& obj) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      if constexpr (std::is_same_v<T, MaterialData>) {
        return m_materials.try_emplace(hash, std::move(obj)).first->second;
      } else if constexpr (std::is_same_v<T, MeshReplacement>) {
        return m_geometries.try_emplace(hash, std::move(obj)).first->second;
      } else {
        return m_secretReplacements[hash].emplace_back(obj);
      }
    }

    // Removes the object of type T for a hash value.
    template<typename T>
    void removeObject(XXH64_hash_t hash) {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      if constexpr (std::is_same_v<T, MaterialData>) {
        m_materials.erase(hash);
      } else if constexpr (std::is_same_v<T, MeshReplacement>) {
        m_geometries.erase(hash);
      } else {
        m_secretReplacements.erase(hash);
      }
    }

    // Destroys all replacements and stored objects.
    void clear() {
      std::lock_guard<sync::Spinlock> lock(m_spinlock);
      m_meshReplacers.clear();
      m_lightReplacers.clear();
      m_materials.clear();
      m_geometries.clear();
      m_secretReplacements.clear();
    }

    const SecretReplacements& secretReplacements() const {
      return m_secretReplacements;
    }

  private:
    mutable sync::Spinlock m_spinlock;

    // Replacements ready to be fed to the renderer
    fast_unordered_cache<std::vector<AssetReplacement>> m_meshReplacers;
    fast_unordered_cache<std::vector<AssetReplacement>> m_lightReplacers;

    // Replacement geometry storage
    fast_unordered_cache<MeshReplacement> m_geometries;

    // Replacement material storage
    fast_unordered_cache<MaterialData> m_materials;

    // Secret replacements if any
    SecretReplacements m_secretReplacements;
  };

  struct AssetReplacer {
    std::vector<AssetReplacement>* getReplacementsForMesh(XXH64_hash_t hash);
    std::vector<AssetReplacement>* getReplacementsForLight(XXH64_hash_t hash);
    MaterialData* getReplacementMaterial(XXH64_hash_t hash);

    // process the replacement USD and create all the m_replacements entries.
    void initialize(const Rc<DxvkContext>& context);

    // returns true if the state of replacements has changed.
    bool checkForChanges(const Rc<DxvkContext>& context);

    bool areReplacementsLoaded() const;
    bool areReplacementsLoading() const;
    const std::string& getReplacementStatus() const;

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
    [[nodiscard]] const std::vector<RasterGeometry>& accessExternalMesh(remixapi_MeshHandle handle) const;
    void destroyExternalMesh(remixapi_MeshHandle handle);

  private:
    void updateSecretReplacements();

    bool m_bSecretReplacementsUpdated = false;

    struct VariantInfo {
      static constexpr size_t kDefaultVariant = 0;
      size_t numVariants = 0;
      size_t selectedVariant = kDefaultVariant;
    };

    fast_unordered_cache<VariantInfo> m_variantInfos;
    SecretReplacements m_secretReplacements;

    ModManager m_modManager;

    std::unordered_map<remixapi_MaterialHandle, std::optional<MaterialData>> m_extMaterials {};
    std::unordered_map<remixapi_MeshHandle, std::vector<RasterGeometry>> m_extMeshes {};
  };
} // namespace dxvk

