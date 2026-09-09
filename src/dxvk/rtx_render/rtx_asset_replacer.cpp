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

#include "rtx_asset_replacer.h"

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_initializer.h"
#include "rtx_options.h"
#include "rtx_utils.h"
#include "rtx_asset_data_manager.h"

namespace dxvk {

std::shared_ptr<const ReplacementBucket> AssetReplacer::getReplacementsForMesh(XXH64_hash_t hash) {
  if (!RtxOptions::getEnableReplacementMeshes())
    return nullptr;

  auto variantInfo = m_variantInfos.find(hash);

  if (variantInfo != m_variantInfos.end()) {
    hash += variantInfo->second.selectedVariant;
  }

  for (auto& mod : m_modManager.mods()) {
    if (auto replacement = mod->replacements().get<AssetReplacement::eMesh>(hash)) {
      return replacement;
    }
  }

  return nullptr;
}

std::shared_ptr<const ReplacementBucket> AssetReplacer::getReplacementsForLight(XXH64_hash_t hash) {
  if (!RtxOptions::getEnableReplacementLights())
    return nullptr;

  for (auto& mod : m_modManager.mods()) {
    if (auto replacement = mod->replacements().get<AssetReplacement::eLight>(hash)) {
      return replacement;
    }
  }

  return nullptr;
}

std::shared_ptr<MaterialData> AssetReplacer::getReplacementMaterial(XXH64_hash_t hash) {
  if (!RtxOptions::getEnableReplacementMaterials())
    return nullptr;

  for (auto& mod : m_modManager.mods()) {
    std::shared_ptr<MaterialData> material;
    if (mod->replacements().getMaterial(hash, material)) {
      return material;
    }
  }

  return nullptr;
}

void AssetReplacer::initialize(const Rc<DxvkContext>& context) {
  for (auto& mod : m_modManager.mods()) {
    // Each mod cancels internally too; this stops us starting the next one. Returning
    // rather than breaking skips the secret-replacement pass, which has nothing to do
    // over a mod set we deliberately stopped populating.
    if (m_modManager.isLoadingCancelled()) {
      return;
    }
    mod->load(context);
  }
  updateSecretReplacements();
}

bool AssetReplacer::checkForChanges(const Rc<DxvkContext>& context) {
  ScopedCpuProfileZone();

  bool changed = false;
  for (auto& mod : m_modManager.mods()) {
    changed |= mod->checkForChanges(context);
  }
  if (changed) {
    updateSecretReplacements();
  }
  return changed;
}

bool AssetReplacer::applyPendingRebuilds(const Rc<DxvkContext>& context, AssetChanges& changes) {
  ScopedCpuProfileZone();

  bool applied = false;
  for (auto& mod : m_modManager.mods()) {
    applied |= mod->applyPendingRebuild(context, changes);
  }
  if (applied) {
    updateSecretReplacements();
  }
  return applied;
}

void AssetReplacer::onDestroy() {
  for (auto& mod : m_modManager.mods()) {
    mod->onDestroy();
  }
}

bool AssetReplacer::hasAnyMods() const {
  return !m_modManager.mods().empty();
}

std::vector<Mod::State> AssetReplacer::getReplacementStates() const {
  const auto& mods = m_modManager.mods();
  std::vector<Mod::State> modStates;
  modStates.reserve(mods.size());

  for (auto& mod : mods) {
    modStates.emplace_back(mod->state());
  }

  return modStates;
}

void AssetReplacer::requestReload() {
  for (auto& mod : m_modManager.mods()) {
    mod->requestReload();
  }
}

bool AssetReplacer::isReloadPending() const {
  for (auto& mod : m_modManager.mods()) {
    const auto p = mod->state();
    if (mod->isReloadRequested() || (p.progressState != Mod::ProgressState::Unloaded && p.progressState != Mod::ProgressState::Loaded)) {
      return true;
    }
  }

  return false;
}

void AssetReplacer::updateSecretReplacements() {
  bool updated = false;

  m_secretReplacements.clear();

  for (auto& mod : m_modManager.mods()) {
    if (mod->state().progressState != Mod::ProgressState::Loaded) {
      continue;
    }

    // Pull secret replacement info
    for (auto& secrets : mod->replacements().secretReplacements()) {
      for (auto& secret : secrets.second) {
        m_secretReplacements[secrets.first].emplace_back(
          SecretReplacement{secret.header,
                            secret.name,
                            secret.description,
                            secret.unlockHash,
                            secret.assetHash,
                            secret.replacementPath,
                            secret.bDisplayBeforeUnlocked,
                            secret.bExclusiveReplacement,
                            secret.variantId
          }
        );

        updated = true;
      }
    }
  }

  // Selected variants are user state keyed by asset hash, not data derived from the mod
  // tables, so they outlive a reload. Drop only selections whose variant is gone.
  m_variantInfos.erase_if([this](const auto& it) {
    if (it->second.selectedVariant == VariantInfo::kDefaultVariant) {
      return false;
    }
    const auto secrets = m_secretReplacements.find(it->first);
    if (secrets == m_secretReplacements.end()) {
      return true;
    }
    for (const auto& secret : secrets->second) {
      if (secret.variantId == it->second.selectedVariant) {
        return false;
      }
    }
    return true;
  });

  m_bSecretReplacementsUpdated = updated;
}

namespace {
  std::string tostr(const remixapi_MaterialHandle& h) {
    static_assert(sizeof h == sizeof uint64_t);
    return std::to_string(reinterpret_cast<uint64_t>(h));
  }
  std::string tostr(const remixapi_MeshHandle& h) {
    static_assert(sizeof h == sizeof uint64_t);
    return std::to_string(reinterpret_cast<uint64_t>(h));
  }
}

void AssetReplacer::makeMaterialWithTexturePreload(DxvkContext& ctx, remixapi_MaterialHandle handle, MaterialData&& data) {
  auto [iter, isNew] = m_extMaterials.emplace(handle, std::move(data));

  if (!isNew) {
    Logger::info("Ignoring repeated material registration (handle=" + tostr(handle) + ") ");
    return;
  }
}

const MaterialData* AssetReplacer::accessExternalMaterial(remixapi_MaterialHandle handle) const {
  auto found = m_extMaterials.find(handle);
  if (found == m_extMaterials.end()) {
    return nullptr;
  }
  return found->second ? &found->second.value() : nullptr;
}

void AssetReplacer::destroyExternalMaterial(remixapi_MaterialHandle handle) {
  m_extMaterials.erase(handle);
}

void AssetReplacer::registerExternalMesh(remixapi_MeshHandle handle, std::vector<RasterGeometry>&& submeshes) {
  if (m_extMeshes.count(handle) > 0) {
    Logger::info("Ignoring repeated mesh registration (handle=" + tostr(handle) + ") ");
    return;
  }

  m_extMeshes.emplace(handle, std::make_shared<std::vector<RasterGeometry>>(std::move(submeshes)));
}

std::shared_ptr<const std::vector<RasterGeometry>> AssetReplacer::accessExternalMesh(remixapi_MeshHandle handle) const {
  auto found = m_extMeshes.find(handle);
  if (found == m_extMeshes.end()) {
    static const auto s_empty = std::make_shared<const std::vector<RasterGeometry>>();
    return s_empty;
  }
  return found->second;
}

void AssetReplacer::destroyExternalMesh(remixapi_MeshHandle handle) {
  m_extMeshes.erase(handle);
}

} // namespace dxvk

