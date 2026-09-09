/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../../util/rc/util_rc_ptr.h"
#include "../../lssusd/game_exporter_paths.h"

#include <algorithm>
#include <string>
#include <filesystem>
#include <set>
#include <cassert>
#include <atomic>
#include <vector>
#include <mutex>

namespace dxvk {

class ModManager;

class DxvkContext;
class AssetReplacements;
// Defined in rtx_asset_replacer.h, which includes this header. Only passed by reference
// here, so the declaration is enough.
struct AssetChanges;

/**
 * \brief Mod base class
 *
 * Base class and interface for all mod types. Owns common Mod data
 * and state, as well as the replacements.
 */
class Mod {
public:
  typedef std::filesystem::path Path;

  enum ProgressState : std::uint8_t {
    // Set when the stage has yet to be loaded.
    Unloaded = 0,

    // Set during the first phase of stage loading when opening the USD file.
    OpeningUSD,
    // Set during the next phases of stage loading when material, mesh or light processing is currently underway.
    // When these are set the progress count may be read from for progress information.
    ProcessingMaterials,
    ProcessingMeshes,
    ProcessingLights,

    // Set when the stage is completely loaded without errors.
    Loaded,
  };

  struct State {
    ProgressState progressState;
    // Only valid during Processing* states.
    std::uint32_t progressCount;
  };

  virtual ~Mod();

  // Loads the mod and initializes the replacements.
  virtual void load(const Rc<DxvkContext>& context) = 0;
  // Unloads the mod and destroys the replacements.
  virtual void unload() = 0;
  // Consumes any pending file-change signal and may kick off a background rebuild.
  // Returns true only if state changed synchronously. Background reloads land in
  // applyPendingRebuild.
  virtual bool checkForChanges(const Rc<DxvkContext>& context) = 0;
  // Render-thread apply step. Returns true iff a pending rebuild was applied.
  virtual bool applyPendingRebuild(const Rc<DxvkContext>& context, AssetChanges& changes) { return false; }
  // Teardown hook: joins any background worker while what it reads is still alive.
  virtual void onDestroy() {}

  // Queues a manual reload, honoured by the next checkForChanges on the render thread.
  // Safe to call from the UI, which draws on a different thread. Repeated calls before
  // the render thread takes it collapse into one reload.
  void requestReload() {
    m_reloadRequested.store(true, std::memory_order_release);
  }

  bool isReloadRequested() const {
    return m_reloadRequested.load(std::memory_order_acquire);
  }

  // True once device teardown has begun, so a load in progress can bail out promptly.
  // Answered by the ModManager - a mod with no manager was never published and cannot
  // be loading.
  bool isLoadingCancelled() const;

  // Replaces this mod's paths and republishes the flattened list. Call after a load,
  // a reload, or an unload.
  void setSearchPaths(std::vector<Path> paths);

  State state() const {
    return {
      m_progressState.load(std::memory_order_acquire),
      m_progressCount.load(std::memory_order_acquire),
    };
  }

  AssetReplacements& replacements() {
    return *m_replacements.get();
  }

  struct ComparePtrs {
    bool operator()(const std::unique_ptr<Mod>& lhs,
                    const std::unique_ptr<Mod>& rhs) const {
      if (lhs->m_filePath == rhs->m_filePath) {
        return false;
      } else if (lhs->m_priority != rhs->m_priority) {
        return lhs->m_priority < rhs->m_priority;
      }
      return lhs->m_filePath < rhs->m_filePath;
    }
  };

protected:
  explicit Mod(const Path& filePath);

  void setProgress(ProgressState progressState) {
    assert(
      progressState == ProgressState::Unloaded ||
      progressState == ProgressState::OpeningUSD ||
      progressState == ProgressState::Loaded
    );
    m_progressState.store(progressState, std::memory_order_release);
  }

  void setProgressWithCount(ProgressState progressState, std::uint32_t progressCount) {
    assert(
      progressState == ProgressState::ProcessingMaterials ||
      progressState == ProgressState::ProcessingMeshes ||
      progressState == ProgressState::ProcessingLights
    );
    m_progressCount.store(progressCount, std::memory_order_relaxed);
    m_progressState.store(progressState, std::memory_order_release);
  }

  // Takes a queued manual reload request, clearing it. Render thread only.
  bool consumeReloadRequest() {
    return m_reloadRequested.exchange(false, std::memory_order_acq_rel);
  }

  // This mod's asset search paths, in ascending precedence order. Precedence between
  // mods is decided by the mod order when ModManager flattens these, so nothing here
  // carries a priority number. Caller must hold ModManager::m_searchPathMutex - the only
  // caller is ModManager::publishSearchPaths, via friend access.
  const std::vector<Path>& searchPaths() const {
    return m_searchPaths;
  }

  const Path m_filePath;
  size_t m_priority;

  std::atomic<ProgressState> m_progressState{ ProgressState::Unloaded };
  std::atomic<std::uint32_t> m_progressCount{ 0 };
  // Set by the UI thread, consumed by the render thread in checkForChanges.
  std::atomic<bool> m_reloadRequested{ false };
  // Ascending precedence. Owned per-mod so a reload replaces only this mod's paths.
  std::vector<Path> m_searchPaths;
  // Set by ModManager::refreshMods so a mod can ask for a republish after it reloads.
  ModManager* m_pManager = nullptr;
  friend class ModManager;
  std::string m_status = "Unloaded";

  static_assert(decltype(m_progressState)::is_always_lock_free, "Mod progress state atomic should be lock-free for performance.");

  std::unique_ptr<AssetReplacements> m_replacements;
};

/**
 * \brief Mod type information base class
 *
 * Contains Mod type information, implements path validation query
 * and Mod object factory.
 */
struct ModTypeInfo {
  virtual std::unique_ptr<Mod> construct(const Mod::Path& modFilePath) const = 0;
  virtual bool isValidMod(const Mod::Path& modFilePath) const = 0;
};

/**
 * \brief Mod Manager
 *
 * Discovers and manages the Mods.
 */
class ModManager {
public:
  typedef std::set<std::unique_ptr<Mod>, Mod::ComparePtrs> Mods;

  ModManager();

  // Refresh mods, create the newly discovered mods and
  // destroy the removed mods.
  void refreshMods();

  // Concatenate every mod's search paths in mod order and hand the flattened list to
  // the AssetDataManager. Mod order is the precedence order, so this is the only place
  // that needs to know how mods rank against each other.
  void publishSearchPaths() const;

  // Guards m_searchPaths on each Mod against concurrent reads in publishSearchPaths
  // (render thread) vs writes in setSearchPaths (asset-load thread during initial load,
  // or a mod's own rebuild worker thread during a hot-reload - both publish inline so
  // the same walk that needs the search paths already sees them).
  mutable std::mutex m_searchPathMutex;

  // Abandon any in-progress load. Raised once, at device teardown, by
  // RtxInitializer::onDestroy before it joins the asset loading thread: a USD walk
  // that runs to completion there blocks the quit for the whole remaining load.
  void cancelLoading() {
    m_loadingCancelled.store(true, std::memory_order_release);
  }

  bool isLoadingCancelled() const {
    return m_loadingCancelled.load(std::memory_order_acquire);
  }

  const Mods& mods() const {
    return m_mods;
  }

  static std::string getBaseGameModPath(std::string baseGameModRegexStr, std::string baseGameModPathRegexStr);
private:
  using Path = Mod::Path;

  Mods enumerateAllMods();
  Mods enumerateModsInDir(const Path& modsDirPath);

  Mods m_mods;
  std::atomic<bool> m_loadingCancelled = false;
};

}
