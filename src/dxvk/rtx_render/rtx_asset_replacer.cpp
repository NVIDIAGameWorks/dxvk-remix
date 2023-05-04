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

#include "rtx_asset_replacer.h"

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_game_capturer_paths.h"
#include "rtx_utils.h"
#include "rtx_asset_datamanager.h"

namespace dxvk {

std::vector<AssetReplacement>* AssetReplacer::getReplacementsForMesh(XXH64_hash_t hash) {
  if (!RtxOptions::Get()->getEnableReplacementMeshes())
    return nullptr;

  const auto variantHash = hash + m_variantInfos[hash].selectedVariant;

  for (auto& mod : m_modManager.mods()) {
    if (auto replacement = mod->replacements().get<AssetReplacement::eMesh>(hash)) {
      return replacement;
    }
  }

  return nullptr;
}

std::vector<AssetReplacement>* AssetReplacer::getReplacementsForLight(XXH64_hash_t hash) {
  if (!RtxOptions::Get()->getEnableReplacementLights())
    return nullptr;

  for (auto& mod : m_modManager.mods()) {
    if (auto replacement = mod->replacements().get<AssetReplacement::eLight>(hash)) {
      return replacement;
    }
  }

  return nullptr;
}

MaterialData* AssetReplacer::getReplacementMaterial(XXH64_hash_t hash) {
  if (!RtxOptions::Get()->getEnableReplacementMaterials())
    return nullptr;

  for (auto& mod : m_modManager.mods()) {
    MaterialData* material;
    if (mod->replacements().getObject(hash, material)) {
      return material;
    }
  }

  return nullptr;
}

void AssetReplacer::initialize(const Rc<DxvkContext>& context) {
  for (auto& mod : m_modManager.mods()) {
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

bool AssetReplacer::areReplacementsLoaded() const {
  bool loaded = false;
  for (auto& mod : m_modManager.mods()) {
    loaded |= mod->state() == Mod::State::Loaded;
  }
  return loaded;
}

bool AssetReplacer::areReplacementsLoading() const {
  bool loading = false;
  for (auto& mod : m_modManager.mods()) {
    loading |= mod->state() == Mod::State::Loading;
  }
  return loading;
}

const std::string& AssetReplacer::getReplacementStatus() const {
  // TODO: make an array?
  for (auto& mod : m_modManager.mods()) {
    return mod->status();
  }
  static const std::string noReplacements("no replacements");
  return noReplacements;
}

void AssetReplacer::updateSecretReplacements() {
  bool updated = false;

  m_variantInfos.clear();
  m_secretReplacements.clear();

  for (auto& mod : m_modManager.mods()) {
    if (mod->state() != Mod::State::Loaded) {
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

        auto& numVariants = m_variantInfos[secrets.first].numVariants;
        numVariants = std::max(secret.variantId, numVariants);

        updated = true;
      }
    }
  }

  m_bSecretReplacementsUpdated = updated;
}

} // namespace dxvk

