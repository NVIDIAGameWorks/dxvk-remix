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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cwctype>

#include "rtx_mod_usd.h"
#include "rtx_asset_replacer.h"
#include "../../lssusd/curve_utils.h"

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_initializer.h"
#include "rtx_options.h"
#include "rtx_utils.h"
#include "rtx_asset_data_manager.h"
#include "rtx_texture_manager.h"
#include "rtx_lights_data.h"
#include "rtx_file_watch.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/tf/notice.h>
#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/plug/plugin.h>
// ParticleSystemAPI accessed via codeless schema (string-based TfToken API)
#include "../../lssusd/usd_include_end.h"

#include "../../util/util_string.h"

#include "../../lssusd/particle_system_helpers_vec.h"
#include "../../lssusd/game_exporter_common.h"
#include "../../lssusd/game_exporter_paths.h"
#include "../../lssusd/usd_mesh_importer.h"
#include "../../lssusd/usd_common.h"
#include "graph/rtx_graph_usd_parser.h"

namespace fs = std::filesystem;

namespace dxvk {
const char* const kStatusKey = "remix_replacement_status";

// Canonicalizes (resolving symlinks/junctions to real on-disk case) and lowercases a
// path for case-insensitive, form-insensitive membership comparisons on Windows. Falls
// back to lexically-normal + absolute when canonical() fails (e.g. path no longer exists).
std::wstring normalizedPathKey(const std::filesystem::path& rawPath) {
  std::error_code ec;
  std::filesystem::path resolved = std::filesystem::canonical(rawPath, ec);
  if (ec) {
    resolved = std::filesystem::absolute(rawPath).lexically_normal();
  }
  std::wstring key = resolved.wstring();
  std::transform(key.begin(), key.end(), key.begin(),
                  [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
  return key;
}

class UsdMod::Impl {
public:
  Impl(UsdMod& owner)
    : m_owner{owner}
  {}

  ~Impl();

  void load(const Rc<DxvkContext>& context);
  void unload();
  bool checkForChanges(const Rc<DxvkContext>& context);
  bool applyPendingRebuild(const Rc<DxvkContext>& context, AssetChanges& changes);
  void onDestroy();

private:
  UsdMod& m_owner;

  struct Args {
    Rc<DxvkContext> context;
    pxr::UsdGeomXformCache& xformCache;

    pxr::UsdPrim& rootPrim;
    std::vector<AssetReplacement>& meshes;
    fast_unordered_cache<uint32_t> pathHashToIndexMap;
    AssetReplacements* target = nullptr;
  };


  // True once device teardown has begun (RtxInitializer::onDestroy raises the
  // flag before it joins the loader threads). Polled between prims by the walks
  // in processUSD.
  bool isShuttingDown() const;

  // Returns false if the walk did not run to completion — either it was
  // cancelled by shutdown or the stage was missing. Callers must not treat a
  // partially populated `target` as a finished load.
  // When filter sets are non-null, only prims whose hash is in the set are processed
  // (hot-reload path). Pass nullptr to process all prims (initial load path).
  bool processUSD(const Rc<DxvkContext>& context, AssetReplacements& target, std::string& outStatus,
                  const std::unordered_set<XXH64_hash_t>* pDirtyMeshHashes = nullptr,
                  const std::unordered_set<XXH64_hash_t>* pDirtyMatHashes  = nullptr,
                  const std::unordered_set<XXH64_hash_t>* pDirtyLightHashes = nullptr);

  void TEMP_parseSecretReplacementVariants(AssetReplacements& target, const fast_unordered_cache<uint32_t>& variants);
  Rc<ManagedTexture> getTexture(const Args& args, const pxr::UsdPrim& shader, const pxr::TfToken& textureToken, bool forcePreload = false) const;
  std::pair<XXH64_hash_t, std::shared_ptr<MaterialData>> processMaterial(Args& args, const pxr::UsdPrim& matPrim);
  std::pair<XXH64_hash_t, std::shared_ptr<MaterialData>> processMaterialUser(Args& args, const pxr::UsdPrim& prim);
  bool processMesh(const pxr::UsdPrim& prim, Args& args);
  void processPrim(Args& args, const pxr::UsdPrim& prim);
  void processPointInstancer(Args& args, const pxr::UsdPrim& prim);
  std::optional<RtxParticleSystemDesc> processParticleSystem(Args& args, const pxr::UsdPrim& prim);

  void processLight(Args& args, const pxr::UsdPrim& lightPrim, const bool isOverride);
  void processGraph(Args& args, const uint32_t index);
  bool processReplacement(Args& args);
  void processReplacementRecursive(Args& args, const pxr::UsdPrim& prim, bool isRoot = false);

  Categorizer processCategoryFlags(const pxr::UsdPrim& prim);

  // Returns next hash value compatible with geometry and drawcall hashing
  XXH64_hash_t getNextGeomHash() {
    // Needs to be atomic because hot-reload rebuilds call this from worker threads
    static std::atomic<size_t> id { 0 };
    const size_t next = ++id;
    return XXH64(&next, sizeof(next), kEmptyHash);
  }

  std::string m_openedFilePath;

  // Set by the FileWatch callback when a USD file changes in a watched directory.
  // Heap-allocated so the lambda can own a ref.
  std::shared_ptr<std::atomic<bool>> m_pUsdFileChanged =
    std::make_shared<std::atomic<bool>>(false);

  // Id returned by addFileChangedCallback, used to remove it. 0 means not registered.
  FileWatch::FileWatchCallbackId m_fileWatchCallbackId = 0;

  // Real paths of all USDs used by this mod's stage. Updated during load/reload, consulted
  // by the FileWatch callback to scope watchDependencies to this mod. Heap-allocated with
  // its own mutex so the callback can hold a reference independent of this Impl's lifetime.
  struct LayerPathSet {
    std::mutex mutex;
    std::unordered_set<std::wstring> paths;
  };
  std::shared_ptr<LayerPathSet> m_layerPaths = std::make_shared<LayerPathSet>();

  // Refreshes m_layerPaths from the stage's currently composed layers. Called on the
  // worker thread after a successful processUSD, where m_stage is safe to touch.
  void refreshLayerPaths() {
    if (!m_stage) {
      return;
    }
    std::unordered_set<std::wstring> updated;
    for (const auto& layer : m_stage->GetUsedLayers()) {
      if (!layer) {
        continue;
      }
      updated.insert(normalizedPathKey(layer->GetRealPath()));
    }
    std::lock_guard<std::mutex> lock(m_layerPaths->mutex);
    m_layerPaths->paths = std::move(updated);
  }

  void publishMeshReplacement(Args& args, XXH64_hash_t hash, std::vector<AssetReplacement>& replacementVec);

  // DxvkBarrierSet is per-context, so nothing else orders this copy against the render
  // thread's later use of the buffer; rebuildWorkerEntry waits on it before publishing.
  // Any one upload from the batch suffices: the worker's copies share one command list
  // and one submit, so one resource's fence covers the whole flush.
  Rc<DxvkBuffer> m_lastDeviceLocalUpload;

  // m_rebuildInFlight gates spawning a new worker; m_rebuildReady's release/acquire
  // pair is what publishes m_pending to the render thread.
  std::atomic<bool> m_rebuildInFlight { false };
  std::atomic<bool> m_rebuildReady    { false };
  // Set when a rebuild throws, so applyPendingRebuild reclaims the thread instead of
  // wedging hot-reload for the session.
  std::atomic<bool> m_rebuildAborted  { false };

  // Everything a rebuild produces, grouped so a new field can't be added without
  // deciding where it gets published and dropped. No m_published counterpart: the
  // live table and status belong to Mod, and promoting into them is what
  // applyPendingRebuild does.
  //
  // `replacements` is a scratch table containing only new/updated entries for dirty
  // prims — NOT the full replacement table. applyPendingRebuild merges its entries
  // into the live table in-place rather than swapping the whole table.
  struct RebuildData {
    // Set before the try block in rebuildWorkerEntry so abort-path handling can
    // distinguish an initial load from a hot-reload without payload fields being valid.
    bool isInitialLoad = false;
    // For initial loads: the path that was opened, written to m_openedFilePath by
    // applyPendingRebuild so FileWatch registration can happen on the render thread.
    std::string openedFilePath;
    std::unique_ptr<AssetReplacements> replacements;
    std::string status;
    // What changed, forwarded to SceneManager for selective scene invalidation.
    AssetChanges changes;

    void clear() {
      // isInitialLoad and openedFilePath are intentionally not reset: abort-path
      // handling in applyPendingRebuild reads them to pick the right state word.
      replacements.reset();
      status.clear();
      changes = AssetChanges{};
    }
  };
  RebuildData m_pending;
  dxvk::thread m_rebuildThread;

  void rebuildWorkerEntry(Rc<DxvkDevice> device, bool isInitialLoad, bool isManualReload = false);

  // Held across reloads, and touched only by the worker after the initial load -
  // which is what keeps Sdf layers single-threaded.
  pxr::UsdStageRefPtr m_stage;

  // Fires on whichever thread invoked the change, so normally the worker. The mutex
  // covers a notice arriving from elsewhere - two mods sharing an SdfLayer.
  class StageChangeListener;
  std::shared_ptr<StageChangeListener> m_changeListener;
  pxr::TfNotice::Key m_noticeKey;

  std::mutex m_changedPathsMutex;
  // Dirty hashes extracted from UsdObjectsChanged notices, accumulated since
  // the last rebuild started. Consumed by the worker via takeAndResetDirtyHashes().
  std::unordered_set<XXH64_hash_t> m_dirtyMeshHashes;
  std::unordered_set<XXH64_hash_t> m_dirtyMatHashes;
  std::unordered_set<XXH64_hash_t> m_dirtyLightHashes;
  // Set when a section folder itself is resynced (e.g. /RootNode/Looks newly created):
  // USD reports the folder rather than individual children, so specific hashes are unknown.
  bool m_rebuildAllMeshes = false;
  bool m_rebuildAllMats   = false;
  bool m_rebuildAllLights = false;

  // Open the long-lived stage for the first time. Subsequent rebuilds use
  // m_stage->Reload() instead. Returns false on parse failure.
  bool openStage(const std::string& replacementsUsdPath);
  // Reload the long-lived stage. Notices fire on this thread and populate
  // the dirty hash sets.
  void reloadStage();
  // Subscribe / unsubscribe TfNotice. Stage must be open before subscribe.
  void subscribeToStageChanges();
  void unsubscribeFromStageChanges();
  void takeAndResetDirtyHashes(std::unordered_set<XXH64_hash_t>& outMesh,
                               std::unordered_set<XXH64_hash_t>& outMat,
                               std::unordered_set<XXH64_hash_t>& outLight,
                               bool& outRebuildAllMeshes,
                               bool& outRebuildAllMats,
                               bool& outRebuildAllLights);
};

// context and member variable arguments to pass down to anonymous functions (to avoid having USD in the header)

namespace {
// Find the first prim in the layer stack that has a non-xform or material binding attribute
// return the hash of the filename and prim path.
XXH64_hash_t getStrongestOpinionatedPathHash(const pxr::UsdPrim& prim) {
  static const char* kXformPrefix = "xform";
  static const size_t kXformLen = strlen(kXformPrefix);
  static const pxr::TfToken kMaterialBinding("material:binding");
  auto stack = prim.GetPrimStack();
  for (auto spec : stack) {
    for (auto property : spec->GetProperties()) {
      if (property->GetName().compare(0, kXformLen, kXformPrefix) == 0) {
        // xform property
        continue;
      } else if (property->GetNameToken() == kMaterialBinding) {
        //material binding
        continue;
      }
      // This is the primSpec to use
      std::string originOfMeshFile = spec->GetLayer()->GetRealPath();
      std::string originPath = spec->GetPath().GetString();

      XXH64_hash_t usdOriginHash = 0;
      usdOriginHash = StringToXXH64(originOfMeshFile, usdOriginHash);
      usdOriginHash = StringToXXH64(originPath, usdOriginHash);

      return usdOriginHash;
    }
  }
  Logger::err(str::format("Asset Replacement failed to find a source prim for ", prim.GetPath().GetString()));
  // fall back to using the prim's path in replacements.usda.  Potentially worse performance, since it may lead to duplicates.
  std::string name = prim.GetPath().GetString();
  return XXH3_64bits(name.c_str(), name.size());
}

XXH64_hash_t getNamedHash(const std::string& name, const char* prefix, const size_t len) {
  if (name.compare(0, len, prefix) == 0) {
    // is a mesh replacement.
    return std::strtoull(name.c_str()+len, nullptr, 16);
  } else {
    // Not a mesh replacements
    return 0;
  }
}

XXH64_hash_t getModelHash(const pxr::UsdPrim& prim) {
  static const char* prefix = lss::prefix::mesh.c_str();
  static const size_t len = strlen(prefix);
  return getNamedHash(prim.GetName().GetString(), prefix, len);
}

XXH64_hash_t getLightHash(const pxr::UsdPrim& prim) {
  static const char* prefix = lss::prefix::light.c_str();
  static const size_t len = strlen(prefix);
  if (prim.GetName().GetText()[0] == 's') {
    // Handling for legacy `sphereLight_HASH` names.  TODO Remove once assets are updated
    static const char* legacyPrefix = "sphereLight_";
    static const size_t legacyLen = strlen(legacyPrefix);
    return getNamedHash(prim.GetName().GetString(), legacyPrefix, legacyLen);
  }
  return getNamedHash(prim.GetName().GetString(), prefix, len);
}

// Finds the Shader prim used by a Material prim: prefers a direct child named "Shader",
// falling back to the first UsdShadeShader-typed child.
pxr::UsdPrim findMaterialShader(const pxr::UsdPrim& matPrim) {
  static const pxr::TfToken kShaderToken("Shader");
  pxr::UsdPrim shader = matPrim.GetChild(kShaderToken);
  if (!shader.IsValid() || !shader.IsA<pxr::UsdShadeShader>()) {
    for (auto child : matPrim.GetFilteredChildren(pxr::UsdPrimIsActive)) {
      if (child.IsA<pxr::UsdShadeShader>()) {
        shader = child;
        break;
      }
    }
  }
  return shader;
}

XXH64_hash_t getMaterialHash(const pxr::UsdPrim& prim, const pxr::UsdPrim& shader) {
  static const pxr::TfToken kMaterialType("Material");
  static const char* prefix = lss::prefix::mat.c_str();
  static const size_t len = strlen(prefix);
  std::string name = prim.GetName().GetString();
  XXH64_hash_t nameHash = getNamedHash(name, prefix, len);
  if (nameHash != 0) {
    return nameHash;
  }
  if (prim.GetTypeName() != kMaterialType) {
    return 0;
  }

  if (!shader.IsValid()) {
    return 0;
  }

  XXH64_hash_t usdOriginHash = getStrongestOpinionatedPathHash(shader);

  return usdOriginHash;
}

// Walks up from primPath looking for the nearest Material-typed ancestor (an attribute
// edit usually lands on its child Shader prim, not the Material prim itself) and hashes
// it the same way processMaterial does. Returns true and dirties the hash if found.
//
// GetPrimAtPath returns invalid for a deleted path, so a deletion falls back to
// `replacements`' material path index, matching primPath itself or any material nested
// under it.
bool tryClassifyExternalMaterial(pxr::SdfPath primPath, const pxr::UsdStageWeakPtr& stage,
                                  AssetReplacements& replacements,
                                  std::unordered_set<XXH64_hash_t>& dirtyMatHashes) {
  if (!stage) {
    return false;
  }
  // "The whole stage" is handled elsewhere.
  if (primPath.IsEmpty() || primPath == pxr::SdfPath::AbsoluteRootPath()) {
    return false;
  }
  static const pxr::TfToken kMaterialType("Material");
  const pxr::SdfPath originalPath = primPath;
  while (!primPath.IsEmpty() && primPath != pxr::SdfPath::AbsoluteRootPath()) {
    pxr::UsdPrim prim = stage->GetPrimAtPath(primPath);
    if (prim.IsValid() && prim.GetTypeName() == kMaterialType) {
      const XXH64_hash_t h = getMaterialHash(prim, findMaterialShader(prim));
      if (h != 0) {
        dirtyMatHashes.insert(h);
        return true;
      }
      return false;
    }
    primPath = primPath.GetParentPath();
  }
  const size_t before = dirtyMatHashes.size();
  replacements.collectMaterialHashesUnderPath(originalPath.GetString(), dirtyMatHashes);
  return dirtyMatHashes.size() > before;
}
}  // namespace

// Returns true if a FileWatch-notified file change should trigger a USD reload.
// normalizedRootModPathKey and usedLayerPaths must already be normalizedPathKey()'d;
// changedPath is normalized internally. A null usedLayerPaths (no dependency data yet)
// falls back to triggering on any watched file.
bool usdShouldTriggerReload(const std::filesystem::path& changedPath,
                             const std::wstring& normalizedRootModPathKey,
                             bool watchDependencies,
                             const std::unordered_set<std::wstring>* usedLayerPaths = nullptr) {
  std::string ext = changedPath.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                  [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  if (ext != ".usda" && ext != ".usdc" && ext != ".usd" && ext != ".usdz") {
    return false;
  }
  // Computed once and reused below: canonical() is a filesystem call, made here on the
  // watcher thread while holding layerPaths->mutex.
  const std::wstring changedKey = normalizedPathKey(changedPath);
  // The mod's own file always triggers.
  if (changedKey == normalizedRootModPathKey) {
    return true;
  }
  if (!watchDependencies) {
    return false;
  }
  if (!usedLayerPaths) {
    return true;
  }
  return usedLayerPaths->count(changedKey) != 0;
}

void classifyChangedPath(const pxr::SdfPath& path, bool isResync, const pxr::UsdStageWeakPtr& stage,
                         AssetReplacements& replacements,
                         std::unordered_set<XXH64_hash_t>& dirtyMeshHashes,
                         std::unordered_set<XXH64_hash_t>& dirtyMatHashes,
                         std::unordered_set<XXH64_hash_t>& dirtyLightHashes,
                         bool& rebuildAllMeshes, bool& rebuildAllMats, bool& rebuildAllLights) {
  static const pxr::SdfPath kRootNode("/RootNode");
  static const pxr::SdfPath kMeshSection("/RootNode/meshes");
  static const pxr::SdfPath kLooksSection("/RootNode/Looks");
  static const pxr::SdfPath kLightsSection("/RootNode/lights");

  static const char* kMeshPrefix  = lss::prefix::mesh.c_str();
  static const size_t kMeshLen    = lss::prefix::mesh.size();
  static const char* kMatPrefix   = lss::prefix::mat.c_str();
  static const size_t kMatLen     = lss::prefix::mat.size();
  static const char* kLightPrefix = lss::prefix::light.c_str();
  static const size_t kLightLen   = lss::prefix::light.size();

  const pxr::SdfPath primPath = path.GetPrimPath();

  // "/" or "/RootNode" itself resyncing means the mod's whole tree was just added,
  // removed, or otherwise restructured (e.g. a sublayer add/remove) - unattributable to
  // any one section, so treat it as touching everything.
  if (isResync && (primPath == pxr::SdfPath::AbsoluteRootPath() || primPath == kRootNode)) {
    rebuildAllMeshes = rebuildAllMats = rebuildAllLights = true;
    return;
  }

  // material:binding is a relationship, not a composition arc, so USD never attributes a
  // material's change to whatever references it. Check unconditionally: cheap, and only
  // ever touches dirtyMatHashes when primPath or an ancestor is actually a Material.
  const bool classifiedAsMaterial = tryClassifyExternalMaterial(primPath, stage, replacements, dirtyMatHashes);

  // GetPrefixes() is ascending and excludes the absolute root: [0]=/RootNode, [1]=section,
  // [2]=replacement root.
  const pxr::SdfPathVector prefixes = primPath.GetPrefixes();
  const bool underRootNode = !prefixes.empty() && prefixes[0] == kRootNode;

  if (!underRootNode) {
    return;
  }

  if (prefixes.size() < 2) {
    return;
  }
  const pxr::SdfPath& sectionPath = prefixes[1];

  // size==2 means the section folder itself changed. For a resync (structural change)
  // we don't know which children were affected; for an info-only change the section
  // prim itself got an attribute edit, which doesn't affect any replacements.
  if (prefixes.size() == 2) {
    if (isResync) {
      if      (sectionPath == kMeshSection)   { rebuildAllMeshes = true; }
      else if (sectionPath == kLooksSection)  { rebuildAllMats   = true; }
      else if (sectionPath == kLightsSection) { rebuildAllLights = true; }
    }
    return;
  }

  const std::string name = prefixes[2].GetName();

  if (sectionPath == kMeshSection) {
    const XXH64_hash_t h = getNamedHash(name, kMeshPrefix, kMeshLen);
    if (h != 0) {
      dirtyMeshHashes.insert(h);
    } else {
      Logger::warn(str::format("USD hot-reload: unrecognized prim name under /RootNode/meshes: ", name));
    }
  } else if (sectionPath == kLooksSection) {
    const XXH64_hash_t h = getNamedHash(name, kMatPrefix, kMatLen);
    if (h != 0) {
      dirtyMatHashes.insert(h);
    } else if (!classifiedAsMaterial) {
      Logger::warn(str::format("USD hot-reload: unrecognized prim name under /RootNode/Looks: ", name));
    }
  } else if (sectionPath == kLightsSection) {
    XXH64_hash_t h = getNamedHash(name, kLightPrefix, kLightLen);
    if (h == 0) {
      // Legacy sphereLight_<HEX> naming.
      static const char* kLegacyPrefix = "sphereLight_";
      static const size_t kLegacyLen   = strlen(kLegacyPrefix);
      h = getNamedHash(name, kLegacyPrefix, kLegacyLen);
    }
    if (h != 0) {
      dirtyLightHashes.insert(h);
    } else {
      Logger::warn(str::format("USD hot-reload: unrecognized prim name under /RootNode/lights: ", name));
    }
  }
}

// Impl via shared_ptr. Subscribed once at stage open; unsubscribed at
// unload(). Notice handler buffers changed paths under the Impl mutex.
class UsdMod::Impl::StageChangeListener : public pxr::TfWeakBase {
public:
  StageChangeListener(Impl& impl) : m_impl(impl) {}

  void OnObjectsChanged(const pxr::UsdNotice::ObjectsChanged& notice,
                        const pxr::UsdStageWeakPtr& sender) {
    // Filter by stage — TfNotice::Register lets us scope to a sender, but be
    // defensive in case the registration scope is broader than expected.
    if (sender != m_impl.m_stage) {
      return;
    }
    std::lock_guard<std::mutex> lock(m_impl.m_changedPathsMutex);
    // Structural changes — may report a section folder or /RootNode when an
    // entire subtree is new or removed.
    for (const auto& path : notice.GetResyncedPaths()) {
      classifyChangedPath(path, /*isResync=*/true, sender, *m_impl.m_owner.m_replacements,
                          m_impl.m_dirtyMeshHashes, m_impl.m_dirtyMatHashes, m_impl.m_dirtyLightHashes,
                          m_impl.m_rebuildAllMeshes, m_impl.m_rebuildAllMats, m_impl.m_rebuildAllLights);
    }
    // Attribute-only edits. A shallow path means an attribute on that specific
    // prim changed; section-level paths here must not trigger full rebuilds.
    for (const auto& path : notice.GetChangedInfoOnlyPaths()) {
      classifyChangedPath(path, /*isResync=*/false, sender, *m_impl.m_owner.m_replacements,
                          m_impl.m_dirtyMeshHashes, m_impl.m_dirtyMatHashes, m_impl.m_dirtyLightHashes,
                          m_impl.m_rebuildAllMeshes, m_impl.m_rebuildAllMats, m_impl.m_rebuildAllLights);
    }
  }

private:
  Impl& m_impl;
};

// Resolves full path for a texture in a shader from texture USD asset path and source USD path.
// This method is used when real path to a texture asset was not resolved by USD, e.g. the asset
// is likely packaged and is not physically present on disk.
static std::string resolveTexturePath(
  const pxr::UsdPrim& shader,
  const pxr::TfToken& textureToken,
  const std::string& textureAssetPath) {
  for (auto spec : shader.GetPrimStack()) {
    auto attribs = spec->GetAttributes();
    if (attribs.find(textureToken) != attribs.end()) {
      std::filesystem::path sourcePath(spec->GetLayer()->GetRealPath());
      std::filesystem::path resolvedPath = sourcePath.parent_path();

      // Process special path symbols.
      // Note: we could use filesystem::weakly_canonical() to make the resulting path
      // canonical but unfortunately it is extremly expensive.
      size_t pathStartPos = 0;
      while (pathStartPos < textureAssetPath.size()) {
        // Check for current folder symbol
        if (textureAssetPath[pathStartPos] == '.') {
          // Skip it
          ++pathStartPos;
          // Check for parent folder symbol
          if (textureAssetPath[pathStartPos] == '.') {
            resolvedPath = resolvedPath.parent_path();
            ++pathStartPos;
          }
        } else if (textureAssetPath[pathStartPos] == '\\' || textureAssetPath[pathStartPos] == '/') {
          // Skip path separator
          ++pathStartPos;
        } else {
          break;
        }
      }

      resolvedPath /= textureAssetPath.data() + pathStartPos;
      resolvedPath.make_preferred();

      return resolvedPath.string();
    }
  }
  Logger::warn(str::format("Unable to resolve full path for ", textureAssetPath));
  return textureAssetPath;
}

Rc<ManagedTexture> UsdMod::Impl::getTexture(const Args& args, const pxr::UsdPrim& shader, const pxr::TfToken& textureToken, bool forcePreload) const {
  static const pxr::TfToken kSRGBColorSpace("sRGB");
  pxr::SdfAssetPath path;
  auto attr = shader.GetAttribute(textureToken);
  if (attr.Get(&path)) {
    const ColorSpace colorSpace = ColorSpace::AUTO; // Always do this, whether or not force SRGB is required or not is unclear at this time.

    std::string resolvedTexturePath;
    if (!path.GetResolvedPath().empty()) {
      // We have a resolved path - texture file exists on disk
      resolvedTexturePath = path.GetResolvedPath();
    } else if (!path.GetAssetPath().empty()) {
      // We do NOT have a resolved path - this could be a packaged texture
      // Resolve full path from the asset path and source USD path
      resolvedTexturePath = resolveTexturePath(shader, textureToken, path.GetAssetPath());
    } else {
      // No texture set
      return nullptr;
    }

    auto assetData = AssetDataManager::get().findAsset(resolvedTexturePath);
    if (assetData != nullptr) {
      auto device = args.context->getDevice();
      auto& textureManager = device->getCommon()->getTextureManager();
      return textureManager.preloadTextureAsset(assetData, colorSpace, forcePreload);
    } else if (RtxOptions::Automation::suppressAssetLoadingErrors()) {
      Logger::warn(str::format("Texture ", resolvedTexturePath, " asset data cannot be found or corrupted."));
    } else {
      Logger::err(str::format("Texture ", resolvedTexturePath, " asset data cannot be found or corrupted."));
    }
  }

  // Note: "Empty" texture returned on failure
  return nullptr;
}

// Returns the material's path-identity hash alongside its data, for stamping
// AssetReplacement::materialPathHash without re-deriving it.
std::pair<XXH64_hash_t, std::shared_ptr<MaterialData>> UsdMod::Impl::processMaterial(Args& args, const pxr::UsdPrim& matPrim) {
  ScopedCpuProfileZone();

  static const pxr::TfToken kIgnore("inputs:ignore_material");  // Any draw call or replacement using a material with this flag will be skipped by the SceneManager
  static const pxr::TfToken kPreloadTextures("inputs:preload_textures");  // Force textures to be loaded at highest mip
  static const pxr::TfToken kLegacyRayPortalIndexToken("rayPortalIndex");

  std::optional<RtxParticleSystemDesc> particleSystem = processParticleSystem(args, matPrim);

  pxr::UsdPrim shader = findMaterialShader(matPrim);

  XXH64_hash_t materialPathHash = getMaterialHash(matPrim, shader);
  if (materialPathHash == 0) {
    return { kEmptyHash, nullptr };
  }

  // tryClassifyExternalMaterial falls back to this for a deleted prim.
  args.target->registerMaterialPath(matPrim.GetPath().GetString(), materialPathHash);

  if (!shader.IsValid()) {
    // Special case to handle material overrides which have the particle system API, but no material parameter overrides.
    // This is the case when adding a particle system API to an existing legacy material in game.
    if (particleSystem.has_value()) {
      // In this case just return an empty opaque material.
      return { materialPathHash, args.target->storeMaterial(materialPathHash, MaterialData(OpaqueMaterialData::deserialize([](const pxr::UsdPrim& shader, const pxr::TfToken& name) { return TextureRef {}; }, shader), particleSystem)) };
    }
    return { kEmptyHash, nullptr };
  }

  // Check if the material has already been processed
  std::shared_ptr<MaterialData> materialData;
  if (args.target->getMaterial(materialPathHash, materialData)) {
    return { materialPathHash, materialData };
  }

  // Remix Flags:
  bool shouldIgnore = false;
  if (shader.HasAttribute(kIgnore)) {
    shader.GetAttribute(kIgnore).Get(&shouldIgnore);
  }
  bool preloadTextures = false;
  if (shader.HasAttribute(kPreloadTextures)) {
    shader.GetAttribute(kPreloadTextures).Get(&preloadTextures);
  }

  // Todo: Only Opaque materials are currently handled, in the future a Translucent path should also exist
  RtSurfaceMaterialType materialType = RtSurfaceMaterialType::Opaque;
  static const pxr::TfToken sourceAsset("info:mdl:sourceAsset");
  pxr::UsdAttribute sourceAssetAttr = shader.GetAttribute(sourceAsset);
  if (sourceAssetAttr.HasValue()) {
    pxr::SdfAssetPath assetPath;
    sourceAssetAttr.Get(&assetPath);
    std::string assetPathStr = assetPath.GetAssetPath();
    if (assetPathStr.find("AperturePBR_Portal.mdl") != std::string::npos) {
      materialType = RtSurfaceMaterialType::RayPortal;
    } else if (assetPathStr.find("AperturePBR_Translucent.mdl") != std::string::npos) {
      if (shader.HasAttribute(kLegacyRayPortalIndexToken)) {
        // TODO (TREX-1260) Remove legacy Translucent->RayPortal path.
        materialType = RtSurfaceMaterialType::RayPortal;
      } else {
        materialType = RtSurfaceMaterialType::Translucent;
      }
    }
  }

  auto getTextureFunctor = [&](const pxr::UsdPrim& shader, const pxr::TfToken& name) {
    return getTexture(args, shader, name, preloadTextures);
  };

  switch (materialType) {
  case RtSurfaceMaterialType::Opaque:
    return { materialPathHash, args.target->storeMaterial(materialPathHash, MaterialData(OpaqueMaterialData::deserialize(getTextureFunctor, shader), particleSystem, shouldIgnore)) };
  case RtSurfaceMaterialType::Translucent:
    return { materialPathHash, args.target->storeMaterial(materialPathHash, MaterialData(TranslucentMaterialData::deserialize(getTextureFunctor, shader), particleSystem, shouldIgnore)) };
  case RtSurfaceMaterialType::RayPortal:
    return { materialPathHash, args.target->storeMaterial(materialPathHash, MaterialData(RayPortalMaterialData::deserialize(getTextureFunctor, shader), particleSystem)) };
  default:
    assert(false && "Invalid materialType passed to getTextureFunctor");
  }

  return { kEmptyHash, nullptr };
}

std::pair<XXH64_hash_t, std::shared_ptr<MaterialData>> UsdMod::Impl::processMaterialUser(Args& args, const pxr::UsdPrim& prim) {
  auto bindAPI = pxr::UsdShadeMaterialBindingAPI(prim);
  auto boundMaterial = bindAPI.ComputeBoundMaterial();
  if (boundMaterial) {
    return processMaterial(args, boundMaterial.GetPrim());
  }
  return { kEmptyHash, nullptr };
}

void UsdMod::Impl::processPrim(Args& args, const pxr::UsdPrim& prim) {
  ScopedCpuProfileZone();

  const XXH64_hash_t usdOriginHash = getStrongestOpinionatedPathHash(prim);

  std::shared_ptr<MeshReplacement> pTemp;
  if (!args.target->getGeometry(usdOriginHash, pTemp)) {
    // First time seeing this mesh, then process it.
    if (!processMesh(prim, args)) {
      return;
    }
  }

  const auto [materialPathHash, materialData] = processMaterialUser(args, prim);

  bool unused = false;
  pxr::GfMatrix4f localToRoot = pxr::GfMatrix4f(args.xformCache.ComputeRelativeTransform(prim, args.rootPrim.GetParent(), &unused));
  const auto& replacementToObjectAsArray = reinterpret_cast<const float(&)[4][4]>(localToRoot);
  const Matrix4 replacementToObject(replacementToObjectAsArray);

  std::vector<pxr::UsdGeomSubset> geomSubsets;
  auto children = prim.GetFilteredChildren(pxr::UsdPrimIsActive);
  for (auto child : children) {
    if (child.IsA<pxr::UsdGeomSubset>()) {
      geomSubsets.emplace_back(child);
    }
  }

  Categorizer categoryFlags = processCategoryFlags(prim);

  std::optional<RtxParticleSystemDesc> particleSystem = processParticleSystem(args, prim);

  if (geomSubsets.empty()) {
    std::shared_ptr<MeshReplacement> pGeometryData;
    if (args.target->getGeometry(usdOriginHash, pGeometryData)) {
      AssetReplacement newReplacementMesh(prim.GetPrimPath().GetString(), std::move(pGeometryData), materialData, categoryFlags, replacementToObject);
      newReplacementMesh.materialPathHash = materialPathHash;
      newReplacementMesh.particleSystem = particleSystem;
      args.meshes.push_back(newReplacementMesh);
    }
  } else {
    for (auto subset : geomSubsets) {
      const XXH64_hash_t usdChildOriginHash = getStrongestOpinionatedPathHash(subset.GetPrim());
      std::shared_ptr<MeshReplacement> childGeometryData;
      if (args.target->getGeometry(usdChildOriginHash, childGeometryData)) {
        AssetReplacement newReplacementMesh(prim.GetPrimPath().GetString(), std::move(childGeometryData), materialData, categoryFlags, replacementToObject);
        newReplacementMesh.materialPathHash = materialPathHash;
        auto [subsetMaterialPathHash, mat] = processMaterialUser(args, subset.GetPrim());
        if (mat) {
          newReplacementMesh.materialData = std::move(mat);
          newReplacementMesh.materialPathHash = subsetMaterialPathHash;
        }
        newReplacementMesh.particleSystem = particleSystem;
        args.meshes.push_back(newReplacementMesh);
      }
    }
  }
}

bool hasExplicitTransform(const pxr::UsdPrim& prim) {
  return prim.HasAttribute(pxr::TfToken("xformOp:rotateZYX")) || prim.HasAttribute(pxr::TfToken("xformOp:scale")) || prim.HasAttribute(pxr::TfToken("xformOp:translate")) || prim.HasAttribute(pxr::TfToken("xformOpOrder"));
}

void UsdMod::Impl::processLight(Args& args, const pxr::UsdPrim& lightPrim, const bool isRoot) {
  if (args.rootPrim.IsA<pxr::UsdGeomMesh>() && lightPrim.IsA<pxr::UsdLuxDistantLight>()) {
    Logger::err(str::format(
      "A Distant Light detected under ", args.rootPrim.GetName(),
      " will be ignored.  Distant Lights are only supported as part of light replacements, not mesh replacements."
    ));
  }

  // Need to preserve the root's transform if it is a root light (with transform overrides), but ignore it if it's a mesh.
  // Lights being replaced are instances that need to exist in the same place as the drawcall they're replacing.
  // Meshes being replaced are assets that may have multiple instances, so any children need to be offset from the
  // asset root, instead of the world root.
  bool resetXformStack; // unused
  pxr::GfMatrix4d localToRoot = args.xformCache.ComputeRelativeTransform(lightPrim, args.rootPrim, &resetXformStack);

  // Because this may be an 'over' with no prim type, we must compute its transformation and include it in the localToRoot calculation
  const bool isTransformDefined = hasExplicitTransform(lightPrim);
  const bool isParentTransformDefined = hasExplicitTransform(args.rootPrim);
  if (LightData::isSupportedUsdLight(args.rootPrim) && isParentTransformDefined) {
    pxr::GfMatrix4d parentToWorld;
    pxr::UsdGeomXformable xform(args.rootPrim);
    xform.GetLocalTransformation(&parentToWorld, &resetXformStack);
    localToRoot *= parentToWorld;
  }

  const pxr::GfMatrix4f lightTransform = pxr::GfMatrix4f(localToRoot);
  const auto& replacementToObjectAsArray = reinterpret_cast<const float(&)[4][4]>(lightTransform);
  const Matrix4 replacementToObject(replacementToObjectAsArray);

  std::optional<LightData> lightData = LightData::tryCreate(lightPrim, isTransformDefined ? &lightTransform : nullptr, isRoot, isParentTransformDefined);
  if (lightData.has_value()) {
    if (isRoot && args.meshes.size() == 1) {
      // Already created an empty replacement to indicate the root needs to be kept,
      // but since we have actual override data for it, need to remove that placeholder.
      args.meshes.clear();
    }
    args.meshes.emplace_back(lightPrim.GetPrimPath().GetString(), lightData.value(), replacementToObject);
  }
}

void UsdMod::Impl::processGraph(Args& args, const uint32_t meshIndex) {
  pxr::UsdPrim graphPrim = args.rootPrim.GetStage()->GetPrimAtPath(pxr::SdfPath(args.meshes[meshIndex].primPath));
  args.meshes[meshIndex].graphState = std::make_shared<RtGraphState>(GraphUsdParser::parseGraph(*args.target, graphPrim, args.pathHashToIndexMap));
}

template<typename T>
static bool _SafeGetPrimvar(const pxr::UsdPrim& prim, const pxr::SdfPath& id, const pxr::TfToken& name, T& value) {
  using namespace pxr;
  UsdAttribute attribute = prim.GetAttribute(TfToken(std::string("primvars:") + name.GetString()));
  if (!attribute.IsDefined()) {
    return false;
  }

  VtValue v;
  if (!attribute.Get(&v)) {
    return false;
  }

  return lss::ConvertPrimvarValue(v, value);
}

std::optional<RtxParticleSystemDesc> UsdMod::Impl::processParticleSystem(Args& args, const pxr::UsdPrim& sceneDelegate) {
  using namespace pxr;
  using namespace CurveUtils;
  using ColorGradientData = ColorGradientDataT<vec4>;

  if (!sceneDelegate.HasAPI(TfToken("ParticleSystemAPI"))) {
    return std::nullopt;
  }

  RtxParticleSystemDesc particleInfo;

  bool anyExists = false;
  const pxr::SdfPath id = sceneDelegate.GetPath();
  uint32_t counter = 0;

  // Helper to read float array primvar
  auto readFloatArray = [&](const char* name, pxr::VtArray<float>& out) -> bool {
    return _SafeGetPrimvar(sceneDelegate, id, pxr::TfToken(std::string("particle:") + name), out) && out.size() > 0;
  };

  // Helper to read vec4 array primvar
  auto readVec4Array = [&](const char* name, pxr::VtArray<GfVec4f>& out) -> bool {
    return _SafeGetPrimvar(sceneDelegate, id, pxr::TfToken(std::string("particle:") + name), out) && out.size() > 0;
  };

  // Helper to read token array primvar
  auto readTokenArray = [&](const char* name, pxr::VtArray<TfToken>& out) -> bool {
    return _SafeGetPrimvar(sceneDelegate, id, pxr::TfToken(std::string("particle:") + name), out) && out.size() > 0;
  };

  // Helper to read bool array primvar
  auto readBoolArray = [&](const char* name, pxr::VtArray<bool>& out) -> bool {
    return _SafeGetPrimvar(sceneDelegate, id, pxr::TfToken(std::string("particle:") + name), out) && out.size() > 0;
  };

  // Read float curve data from USD primvars and populate a FloatCurveData struct
  auto readFloatCurveData = [&](const char* baseName) -> FloatCurveData {
    FloatCurveData curve;
    pxr::VtArray<float> times;
    pxr::VtArray<float> values, inTangentValues, outTangentValues, inTangentTimes, outTangentTimes;
    pxr::VtArray<TfToken> inTangentTypes, outTangentTypes;
    pxr::VtArray<bool> tangentBrokens;
    
    readFloatArray((std::string(baseName) + ":times").c_str(), times);
    readFloatArray((std::string(baseName) + ":values").c_str(), values);
    readTokenArray((std::string(baseName) + ":inTangentTypes").c_str(), inTangentTypes);
    readTokenArray((std::string(baseName) + ":outTangentTypes").c_str(), outTangentTypes);
    readFloatArray((std::string(baseName) + ":inTangentValues").c_str(), inTangentValues);
    readFloatArray((std::string(baseName) + ":outTangentValues").c_str(), outTangentValues);
    readFloatArray((std::string(baseName) + ":inTangentTimes").c_str(), inTangentTimes);
    readFloatArray((std::string(baseName) + ":outTangentTimes").c_str(), outTangentTimes);
    readBoolArray((std::string(baseName) + ":tangentBrokens").c_str(), tangentBrokens);

    // Copy to curve data struct
    curve.times.assign(times.begin(), times.end());
    curve.values.assign(values.begin(), values.end());
    curve.inTangentValues.assign(inTangentValues.begin(), inTangentValues.end());
    curve.outTangentValues.assign(outTangentValues.begin(), outTangentValues.end());
    curve.inTangentTimes.assign(inTangentTimes.begin(), inTangentTimes.end());
    curve.outTangentTimes.assign(outTangentTimes.begin(), outTangentTimes.end());
    curve.tangentBrokens.assign(tangentBrokens.begin(), tangentBrokens.end());
    
    // Convert token types
    curve.inTangentTypes.reserve(inTangentTypes.size());
    for (const auto& t : inTangentTypes) {
      curve.inTangentTypes.push_back(parseTangentType(t.GetText()));
    }
    curve.outTangentTypes.reserve(outTangentTypes.size());
    for (const auto& t : outTangentTypes) {
      curve.outTangentTypes.push_back(parseTangentType(t.GetText()));
    }
    
    return curve;
  };

  // Read color gradient data from USD primvars
  auto readColorGradientData = [&](const char* baseName) -> ColorGradientData {
    ColorGradientData gradient;
    pxr::VtArray<float> times;
    pxr::VtArray<GfVec4f> values;
    
    readFloatArray((std::string(baseName) + ":times").c_str(), times);
    readVec4Array((std::string(baseName) + ":values").c_str(), values);

    gradient.times.assign(times.begin(), times.end());
    gradient.values.reserve(values.size());
    for (const auto& v : values) {
      gradient.values.push_back(vec4(v[0], v[1], v[2], v[3]));
    }
    
    return gradient;
  };

  // Bake a single float channel using curve utilities
  auto bakeFloatChannel = [&](const char* baseName, std::vector<float>& out, float defaultValue) -> bool {
    FloatCurveData curve = readFloatCurveData(baseName);
    return bakeFloatCurve(curve, out, kDefaultAnimationResolution, defaultValue);
  };

  // Bake color channel using curve utilities
  auto bakeColorChannel = [&](const char* baseName, std::vector<vec4>& out, vec4 defaultValue) -> bool {
    ColorGradientData gradient = readColorGradientData(baseName);
    return bakeColorGradient(gradient, out, kDefaultAnimationResolution, defaultValue);
  };

  // Bake vec2 from two independent float channels
  auto bakeVec2Channels = [&](const char* baseNameX, const char* baseNameY, std::vector<vec2>& out, vec2 defaultValue) -> bool {
    std::vector<float> xChannel, yChannel;
    bool hasX = bakeFloatChannel(baseNameX, xChannel, defaultValue.x);
    bool hasY = bakeFloatChannel(baseNameY, yChannel, defaultValue.y);
    return combineToVec2(xChannel, hasX, yChannel, hasY, out, defaultValue, kDefaultAnimationResolution);
  };

  // Bake vec3 from three independent float channels
  auto bakeVec3Channels = [&](const char* baseNameX, const char* baseNameY, const char* baseNameZ, std::vector<vec3>& out, vec3 defaultValue) -> bool {
    std::vector<float> xChannel, yChannel, zChannel;
    bool hasX = bakeFloatChannel(baseNameX, xChannel, defaultValue.x);
    bool hasY = bakeFloatChannel(baseNameY, yChannel, defaultValue.y);
    bool hasZ = bakeFloatChannel(baseNameZ, zChannel, defaultValue.z);
    return combineToVec3(xChannel, hasX, yChannel, hasY, zChannel, hasZ, out, defaultValue, kDefaultAnimationResolution);
  };

  // The schema registry unit test validates exact property names. This counter also
  // catches additions or removals in the reader itself.

  // Color gradient animated properties: 2 channels (minColor, maxColor) x 2 attrs (times, values) = 4 schema attrs
  bool hasNewMinColor = bakeColorChannel("minColor", particleInfo.minColor, vec4(1.0f));
  bool hasNewMaxColor = bakeColorChannel("maxColor", particleInfo.maxColor, vec4(1.0f));
  counter += 4; // minColor:{times,values}, maxColor:{times,values}

  // Float curve animated properties: each channel has 9 attrs (times, values, in/outTangentTypes, in/outTangentValues, in/outTangentTimes, tangentBrokens)
  // Size: 4 channels (minSize:x, minSize:y, maxSize:x, maxSize:y) x 9 = 36 schema attrs
  bool hasNewMinSize = bakeVec2Channels("minSize:x", "minSize:y", particleInfo.minSize, vec2(10.0f, 10.0f));
  bool hasNewMaxSize = bakeVec2Channels("maxSize:x", "maxSize:y", particleInfo.maxSize, vec2(10.0f, 10.0f));
  counter += 36; // minSize:{x,y}, maxSize:{x,y} -- 4 channels x 9 attrs

  // Rotation speed: 2 channels (minRotationSpeed, maxRotationSpeed) x 9 = 18 schema attrs
  bool hasNewMinRotationSpeed = bakeFloatChannel("minRotationSpeed", particleInfo.minRotationSpeed, 0.0f);
  bool hasNewMaxRotationSpeed = bakeFloatChannel("maxRotationSpeed", particleInfo.maxRotationSpeed, 0.0f);
  counter += 18; // minRotationSpeed, maxRotationSpeed -- 2 channels x 9 attrs

  // Velocity: 3 channels (maxVelocity:x, maxVelocity:y, maxVelocity:z) x 9 = 27 schema attrs
  bool hasNewMaxVelocity = bakeVec3Channels("maxVelocity:x", "maxVelocity:y", "maxVelocity:z", particleInfo.maxVelocity, vec3(-1.0f, -1.0f, -1.0f));
  counter += 27; // maxVelocity:{x,y,z} -- 3 channels x 9 attrs

  // Legacy spawn/target fields - read them, and if new animated fields weren't found, create animation from spawn->target
  vec4 minSpawnColor(1.0f), maxSpawnColor(1.0f), minTargetColor(1.0f, 1.0f, 1.0f, 0.0f), maxTargetColor(1.0f, 1.0f, 1.0f, 0.0f);
  vec2 minSpawnSize(10.0f, 10.0f), maxSpawnSize(10.0f, 10.0f), minTargetSize(0.0f, 0.0f), maxTargetSize(0.0f, 0.0f);
  float minSpawnRotationSpeed = 0.0f, maxSpawnRotationSpeed = 0.0f, minTargetRotationSpeed = 0.0f, maxTargetRotationSpeed = 0.0f;
  float maxSpeed = 0.0f;

  // maxSpeed has been deprecated; apply it to both velocity limits if present. 

  _SafeGetParticlePrimvar(GfVec4f, id, minSpawnColor, );
  _SafeGetParticlePrimvar(GfVec4f, id, maxSpawnColor, );
  _SafeGetParticlePrimvar(GfVec4f, id, minTargetColor, );
  _SafeGetParticlePrimvar(GfVec4f, id, maxTargetColor, );
  _SafeGetParticlePrimvar(GfVec2f, id, minSpawnSize, );
  _SafeGetParticlePrimvar(GfVec2f, id, maxSpawnSize, );
  _SafeGetParticlePrimvar(GfVec2f, id, minTargetSize, );
  _SafeGetParticlePrimvar(GfVec2f, id, maxTargetSize, );
  _SafeGetParticlePrimvar(float, id, minSpawnRotationSpeed, );
  _SafeGetParticlePrimvar(float, id, maxSpawnRotationSpeed, );
  _SafeGetParticlePrimvar(float, id, minTargetRotationSpeed, );
  _SafeGetParticlePrimvar(float, id, maxTargetRotationSpeed, );

  // maxSpeed is deprecated (removed from schema); read if present for backward compatibility.
  {
    float temp {};
    if (_SafeGetPrimvar(sceneDelegate, id, pxr::TfToken("particle:maxSpeed"), temp)) {
      maxSpeed = temp;
      anyExists = true;
    }
  }

  // Fall back to legacy spawn/target animation if new fields weren't provided
  if (!hasNewMinColor) {
    particleInfo.minColor = { minSpawnColor, minTargetColor };
  }
  if (!hasNewMaxColor) {
    particleInfo.maxColor = { maxSpawnColor, maxTargetColor };
  }
  if (!hasNewMinSize) {
    particleInfo.minSize = { minSpawnSize, minTargetSize };
  }
  if (!hasNewMaxSize) {
    particleInfo.maxSize = { maxSpawnSize, maxTargetSize };
  }
  if (!hasNewMinRotationSpeed) {
    particleInfo.minRotationSpeed = { minSpawnRotationSpeed, minTargetRotationSpeed };
  }
  if (!hasNewMaxRotationSpeed) {
    particleInfo.maxRotationSpeed = { maxSpawnRotationSpeed, maxTargetRotationSpeed };
  }
  if (!hasNewMaxVelocity) {
    // maxSpeed has been deprecated; use it if maxVelocity isn't available
    particleInfo.maxVelocity = { {maxSpeed, maxSpeed, maxSpeed}, {maxSpeed, maxSpeed, maxSpeed} };
  }

  _SafeGetParticlePrimvar(GfVec3f, id, attractorPosition, particleInfo.);
  _SafeGetParticlePrimvar(float, id, attractorForce, particleInfo.);
  _SafeGetParticlePrimvar(float, id, minTimeToLive, particleInfo.);
  _SafeGetParticlePrimvar(float, id, maxTimeToLive, particleInfo.);
  _SafeGetParticlePrimvar(float, id, initialVelocityFromNormal, particleInfo.);
  _SafeGetParticlePrimvar(float, id, initialVelocityConeAngleDegrees, particleInfo.);
  _SafeGetParticlePrimvar(float, id, turbulenceFrequency, particleInfo.);
  _SafeGetParticlePrimvar(float, id, turbulenceForce, particleInfo.);
  _SafeGetParticlePrimvar(float, id, motionTrailMultiplier, particleInfo.);
  _SafeGetParticlePrimvar(float, id, spawnRatePerSecond, particleInfo.);
  _SafeGetParticlePrimvar(float, id, collisionThickness, particleInfo.);
  _SafeGetParticlePrimvar(float, id, collisionRestitution, particleInfo.);
  _SafeGetParticlePrimvar(float, id, initialRotationDeviationDegrees, particleInfo.);
  _SafeGetParticlePrimvar(float, id, spawnBurstDuration, particleInfo.);
  _SafeGetParticlePrimvar(float, id, dragCoefficient, particleInfo.);
  _SafeGetParticlePrimvar(float, id, attractorRadius, particleInfo.);
  _SafeGetParticlePrimvar(float, id, gravityForce, particleInfo.);
  _SafeGetParticlePrimvar(float, id, initialVelocityFromMotion, particleInfo.);
  _SafeGetParticlePrimvar(int, id, maxNumParticles, particleInfo.);
  _SafeGetParticlePrimvar(TfToken, id, billboardType, particleInfo.);
  _SafeGetParticlePrimvar(TfToken, id, spriteSheetMode, particleInfo.);
  _SafeGetParticlePrimvar(TfToken, id, collisionMode, particleInfo.);
  _SafeGetParticlePrimvar(TfToken, id, randomFlipAxis, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, hideEmitter, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, enableMotionTrail, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, useTurbulence, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, alignParticlesToVelocity, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, useSpawnTexcoords, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, enableCollisionDetection, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, restrictVelocityX, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, restrictVelocityY, particleInfo.);
  _SafeGetParticlePrimvar(bool, id, restrictVelocityZ, particleInfo.);

  assert(counter == lss::kParticleSystemSchemaPropertyCount);

  return particleInfo;
}

void UsdMod::Impl::processPointInstancer(Args& args, const pxr::UsdPrim& prim) {
  const pxr::UsdGeomPointInstancer instancer(prim);
  // caching rootPrim, since we need to treat each prototype as having a different rootprim.
  const pxr::UsdPrim rootPrim = args.rootPrim;
  const bool useFastPathForPointInstancerMeshes = RtxOptions::enableReplacementInstancerMeshRendering();
  // TODO: Implement support for `instancesToObject` when rendering lights.  For now, just make multiple copies of the lights in the AssetReplacement array.
  const bool useFastPathForPointInstancerLights = false;
  
  bool unused = false;
  const pxr::GfMatrix4f instancerToObject = pxr::GfMatrix4f(args.xformCache.ComputeRelativeTransform(prim, args.rootPrim, &unused));

  pxr::VtVec3fArray positions, scales;
  pxr::VtQuathArray orientations;
  pxr::VtIntArray protoIndices;
  instancer.GetPositionsAttr().Get(&positions);
  instancer.GetScalesAttr().Get(&scales);
  instancer.GetOrientationsAttr().Get(&orientations);
  instancer.GetProtoIndicesAttr().Get(&protoIndices);
  pxr::SdfPathVector protoTargets;
  if (!instancer.GetPrototypesRel().GetForwardedTargets(&protoTargets)) {
    Logger::err(str::format("Prototypes Rel on prim: ", prim.GetPath().GetString(), " failed to resolve."));
    return;
  }

  size_t numPrototypes = protoTargets.size();
  std::vector<Matrix4> instanceToObjectTransforms;
  instanceToObjectTransforms.reserve(positions.size());

  // For each prototype
  //   get the prototype
  //   make a list of transforms based on the protoIds
  //   record the starting meshes index
  //   invoke processReplacementRecursive on the prototype
  //   for each mesh from starting index to current
  //     combine the instanceToObject with the replacementToObject (which is really the replacementToPrototypeRoot)
  
  for (size_t protoIndex = 0; protoIndex < protoTargets.size(); ++protoIndex) {
    pxr::SdfPath protoPath = protoTargets[protoIndex];
    pxr::UsdPrim protoPrim = prim.GetStage()->GetPrimAtPath(protoPath);

    if (!protoPrim.IsValid()) {
      Logger::err(str::format("Prototype prim at : ", protoPath.GetString(), " was invalid."));
      continue;
    }
    
    for (size_t i = 0; i < positions.size(); ++i) {
      if (!protoIndices.empty() && protoIndices[i] != protoIndex) {
        // Transform is for a different prototype
        continue;
      }

      // Transform each instance according to the docs here: 
      // https://openusd.org/dev/api/class_usd_geom_point_instancer.html#UsdGeomPointInstancer_transform
      pxr::GfMatrix4f translate(1.f), rotate(1.f), scale(1.f);
      translate.SetTranslate(positions[i]);
      if (orientations.size() > 0) {
        rotate.SetRotate(orientations[i]);
      }
      if (scales.size() > 0) {
        scale.SetScale(scales[i]);
      }
      const pxr::GfMatrix4f instanceToObject = scale * rotate * translate * instancerToObject;

      const auto& instanceToObjectAsArray = reinterpret_cast<const float(&)[4][4]>(instanceToObject);
      instanceToObjectTransforms.emplace_back(instanceToObjectAsArray);
    }

    if (!instanceToObjectTransforms.empty()) {
      // A single prototype may contain multiple meshes and lights.  The transform list should apply
      // to all of them.
      
      // Before processing prim we have `start` meshes
      const size_t originalStart = args.meshes.size();
      args.rootPrim = protoPrim;
      processReplacementRecursive(args, protoPrim);
      args.rootPrim = rootPrim;

      const size_t originalEnd = args.meshes.size();
      size_t numNewMeshes = 0;
      for (size_t meshInd = originalStart; meshInd < originalEnd; ++meshInd) {
        // Note: -1 because `processReplacementRecursive` creates 1, which we re-use for the first instance
        if (!useFastPathForPointInstancerMeshes && args.meshes[meshInd].type == AssetReplacement::eMesh) {
          numNewMeshes += instanceToObjectTransforms.size() - 1;
        }
        if (!useFastPathForPointInstancerLights && args.meshes[meshInd].type == AssetReplacement::eLight) {
          numNewMeshes += instanceToObjectTransforms.size() - 1;
        }
      }

      // After processing prim we more meshes - create copies and apply the instance transforms to the copies
      args.meshes.reserve(args.meshes.size() + ( numNewMeshes));
      for (size_t meshInd = originalStart; meshInd < originalEnd; ++meshInd) {
        if (args.meshes[meshInd].type == AssetReplacement::eGraph) {
          // TODO[REMIX-4405]: graphs inside pointInstancers are not supported yet.
          // To support this, we need to make Prim Targets within the intial graphState
          // point at the correct pointInstancer instance.
          Logger::err(str::format("Graphs inside pointInstancers are not supported yet.  Prim: ", prim.GetPath().GetString()));
          continue;
        }
        if (
          (args.meshes[meshInd].type == AssetReplacement::eMesh && useFastPathForPointInstancerMeshes)
          || (args.meshes[meshInd].type == AssetReplacement::eLight && useFastPathForPointInstancerLights)
        ) {
          // Fast Path - this will attach a list of transforms to the replacement, which can be used later to render multiple copies of it.
          
          // Copy the transform vector to this mesh
          args.meshes[meshInd].instancesToObject = std::make_shared<std::vector<Matrix4>>(instanceToObjectTransforms);
          auto& instancesToObject = *args.meshes[meshInd].instancesToObject;
          // Append the meshToProtoRoot transform
          for (size_t instanceInd = 0; instanceInd < instancesToObject.size(); ++instanceInd) {
            instancesToObject[instanceInd] = instancesToObject[instanceInd] * args.meshes[meshInd].replacementToObject;
          }
          // Reset replacementToObject to be identity.
          args.meshes[meshInd].replacementToObject = Matrix4();
        } else {
          // Slow path - this will just create a copy of the replacement asset for each instance.
          args.meshes[meshInd].pointInstanceIndex = 0;

          Matrix4 replacementToInstance = args.meshes[meshInd].replacementToObject;

          // Update the transforms for the first instance separately, since it already exists in the meshes vector.
          {
            const Matrix4 composedReplacementToObject = instanceToObjectTransforms[0] * replacementToInstance;
            if (args.meshes[meshInd].type == AssetReplacement::eMesh) {
              args.meshes[meshInd].replacementToObject = composedReplacementToObject;
            } else if (args.meshes[meshInd].lightData.has_value()){
              const pxr::GfMatrix4f pxrReplacementToObject = pxr::GfMatrix4f(reinterpret_cast<const float(&)[4][4]>(composedReplacementToObject));
              args.meshes[meshInd].lightData.value().setTransform(pxrReplacementToObject);
            }
          }

          // Create and transform the rest of the instances.
          for (size_t instanceInd = 1; instanceInd < instanceToObjectTransforms.size(); ++instanceInd) {
            size_t index = args.meshes.size();
            // Create a new copy of the mesh for this instance.
            args.meshes.push_back(args.meshes[meshInd]);
            // PointInstancer copies would have the same hash as the original prim, but we need to be able
            // to uniquely address each instance. We'll assign a hash as:
            // original usdPathHash + instance index.
            args.meshes.back().usdPathHash = args.meshes[meshInd].usdPathHash + instanceInd;
            args.meshes.back().pointInstanceIndex = instanceInd;
            // Append the meshToProtoRoot transform
            const Matrix4 composedReplacementToObject = instanceToObjectTransforms[instanceInd] * replacementToInstance;
            if (args.meshes[index].type == AssetReplacement::eMesh) {
              args.meshes[index].replacementToObject = composedReplacementToObject;
            } else if (args.meshes[index].lightData.has_value()){
              const pxr::GfMatrix4f pxrReplacementToObject = pxr::GfMatrix4f(reinterpret_cast<const float(&)[4][4]>(composedReplacementToObject));
              args.meshes[index].lightData.value().setTransform(pxrReplacementToObject);
            }
          }
        }
      }
      instanceToObjectTransforms.clear();
    }
  }
}

bool preserveGameObject(const pxr::UsdPrim& prim) {
  // shortcut for legacy assets
  static const pxr::TfToken kPreserveOriginalToken("preserveOriginalDrawCall");
  if (prim.HasAttribute(kPreserveOriginalToken)) {
    int preserve = 0;
    prim.GetAttribute(kPreserveOriginalToken).Get(&preserve);
    return preserve;
  }

  auto strFindNoCase = [](const std::string& s1, const std::string& s2) {
    return std::search(s1.begin(), s1.end(), s2.begin(), s2.end(), [](const char a, const char b) { return (toupper(a) == toupper(b)); }) != s1.end();
  };

  auto legacyCaptureReferenceExists = [&](const std::string& referencePath) {
    std::filesystem::path asset(referencePath);
    return strFindNoCase(asset.replace_extension(std::filesystem::path()).string(), "/captures" + lss::commonDirName::meshDir + prim.GetName().GetString());
  };

  // determine draw call preservation by querying the references
  for (const pxr::SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
    if (primSpec->HasReferences()) {
      pxr::SdfReferenceListOp listOp;
      pxr::SdfReferencesProxy referencesProxy = primSpec->GetReferenceList();
      if (referencesProxy.IsExplicit()) {
        return false;
      }
      // Check if the capture object is present in deleted items
      for (const pxr::SdfReference& deletedItem : referencesProxy.GetDeletedItems()) {
        if (legacyCaptureReferenceExists(deletedItem.GetAssetPath())) {
          return false;
        }
      }
    }
  }

  if (const pxr::UsdPrim child = prim.GetStage()->GetPrimAtPath(prim.GetPath().AppendChild(lss::gTokMesh))) {
    if (child.HasAuthoredActive()) {
      return child.IsActive();
    }
  }

  return true;
}

bool explicitlyNoReferences(const pxr::UsdPrim& prim) {
  // does the references look something like: references = None or []
  for (const pxr::SdfPrimSpecHandle& primSpec : prim.GetPrimStack()) {
    if (primSpec->HasReferences()) {
      pxr::SdfReferenceListOp listOp;
      pxr::SdfReferencesProxy referencesProxy = primSpec->GetReferenceList();
      if (referencesProxy.IsExplicit() && referencesProxy.GetExplicitItems().size() == 0) {
        return true;
      }
    }
  }

  return false;
}

bool UsdMod::Impl::processReplacement(Args& args) {
  ScopedCpuProfileZone();
  
  // Want the original draw call to occupy the first index in the replacements vector, so that the indices of the
  // asset replacements line up with the indices of the Entities being drawn.
  if (preserveGameObject(args.rootPrim)) {
    // append the mesh token to the root path, to match with components that target the captured mesh.  Should match lss::gTokMesh.
    static const pxr::TfToken kMeshToken("mesh");
    args.meshes.push_back(AssetReplacement(args.rootPrim.GetPrimPath().AppendChild(kMeshToken).GetString()));
    args.meshes[0].includeOriginal = true;
    // The ParticleSystemAPI and category flags are authored on the mesh child prim, not the root hash prim.
    pxr::UsdPrim meshPrim = args.rootPrim.GetChild(kMeshToken);
    const pxr::UsdPrim& componentPrim = meshPrim.IsValid() ? meshPrim : args.rootPrim;
    args.meshes[0].categories = processCategoryFlags(componentPrim);
    std::optional<RtxParticleSystemDesc> particleSystem = processParticleSystem(args, componentPrim);
    args.meshes[0].particleSystem = particleSystem;
  }
  processReplacementRecursive(args, args.rootPrim, true);

  // construct a map of the usdpathHash to the index within the replacements vector.
  // This is used to let graphs reference the runtime instances that are created from specific prims.
  for (size_t i = 0; i < args.meshes.size(); ++i) {
    args.pathHashToIndexMap[args.meshes[i].usdPathHash] = i;
  }

  // Actually initialize the graphs - this must be done after the pathHashToIndexMap is populated.
  for (size_t i = 0; i < args.meshes.size(); ++i) {
    if (args.meshes[i].type == AssetReplacement::eGraph) {
      processGraph(args, i);
    }
  }

  // Only return false if no changes were made to the draw call, including the original draw being preserved.
  // A particle system or category override on the original draw still counts as a change.
  if (args.meshes.size() == 1 && args.meshes[0].includeOriginal
      && !args.meshes[0].particleSystem.has_value()
      && args.meshes[0].categories.categoryExists.raw() == 0) {
    return false;
  }
  return true;

}

void UsdMod::Impl::processReplacementRecursive(Args& args, const pxr::UsdPrim& prim, bool isRoot) {
  static const pxr::TfToken kGraphPrimType = pxr::TfToken("OmniGraph");
  if (prim.IsA<pxr::UsdGeomMesh>()) {
    processPrim(args, prim);
  } else if (prim.IsA<pxr::UsdGeomPointInstancer>()) {
    processPointInstancer(args, prim);
    return;  // meshes with pointInstancer parents don't render in USD Composer, so mimicing that behavior here.
  } else if (LightData::isSupportedUsdLight(prim) && (!isRoot || !explicitlyNoReferences(prim))) {
    processLight(args, prim, isRoot);
  } else if (prim.GetTypeName() == kGraphPrimType && !isRoot) {
    // Need to initialize the graphs after the pathHashToIndexMap is populated.
    // For now, just create empty placeholders that point to the source prim.
    args.meshes.push_back(AssetReplacement(prim.GetPath().GetString()));
    args.meshes.back().type = AssetReplacement::eGraph;
  }

  auto children = prim.GetFilteredChildren(pxr::UsdPrimIsActive);
  for (auto child : children) {
    processReplacementRecursive(args, child);
  }
}


void UsdMod::Impl::load(const Rc<DxvkContext>& context) {
  ScopedCpuProfileZone();
  if (m_owner.state().progressState != ProgressState::Unloaded) {
    return;
  }
  m_owner.setProgress(ProgressState::OpeningUSD);

  const std::string replacementsUsdPath(m_owner.m_filePath.string());
  if (!openStage(replacementsUsdPath)) {
    m_owner.setProgress(ProgressState::Unloaded);
    return;
  }

  m_rebuildInFlight.store(true, std::memory_order_release);
  m_rebuildReady.store(false, std::memory_order_release);
  m_rebuildAborted.store(false, std::memory_order_release);

  // Run inline: load() is already called on the right thread in both sync and async paths.
  rebuildWorkerEntry(context->getDevice(), /*isInitialLoad=*/true);

  if (!RtxOptions::asyncAssetLoading()) {
    AssetChanges changes;
    applyPendingRebuild(context, changes);
  }
}

void UsdMod::Impl::unload() {
  if (m_owner.state().progressState == ProgressState::Loaded) {
    FileWatch::get().removeFileChangedCallback(m_fileWatchCallbackId);
    m_fileWatchCallbackId = 0;

    // The worker writes to m_pending for both initial loads and rebuilds;
    // don't tear down replacement state until it's joined.
    if (m_rebuildThread.joinable()) {
      m_rebuildThread.join();
    }
    m_rebuildInFlight.store(false, std::memory_order_release);
    m_rebuildReady.store(false, std::memory_order_release);
    m_rebuildAborted.store(false, std::memory_order_release);
    m_pending.clear();

    // Drop the long-lived stage and unsubscribe before the listener is gone;
    // a TfNotice firing into a freed listener would crash.
    unsubscribeFromStageChanges();
    m_stage.Reset();
    {
      std::lock_guard<std::mutex> lock(m_changedPathsMutex);
      m_dirtyMeshHashes.clear();
      m_dirtyMatHashes.clear();
      m_dirtyLightHashes.clear();
      m_rebuildAllMeshes = m_rebuildAllMats = m_rebuildAllLights = false;
    }

    m_owner.m_replacements->clear();
    // Only this mod's paths: ModManager republishes the flattened list, so other loaded
    // mods keep their paths and mounted packages.
    m_owner.setSearchPaths({});

    m_owner.setProgress(ProgressState::Unloaded);
  }
}

bool UsdMod::Impl::openStage(const std::string& replacementsUsdPath) {
  // Cleanup any prior stage (generally a result of failure to load)
  unsubscribeFromStageChanges();
  m_stage = pxr::UsdStage::Open(replacementsUsdPath, pxr::UsdStage::LoadAll);
  if (!m_stage) {
    Logger::err(str::format("USD mod file failed parsing: ",
                            std::filesystem::weakly_canonical(replacementsUsdPath).string()));
    return false;
  }
  subscribeToStageChanges();
  return true;
}

void UsdMod::Impl::reloadStage() {
  ScopedCpuProfileZone();
  if (!m_stage) {
    return;
  }
  // Reload re-composes the stage from disk. Notices fire synchronously on
  // this thread before Reload returns, so by the time we walk prims, the
  // stage already reflects the saved file.
  m_stage->Reload();
}

void UsdMod::Impl::subscribeToStageChanges() {
  if (!m_stage || m_changeListener) {
    return;
  }
  m_changeListener = std::make_shared<StageChangeListener>(*this);
  m_noticeKey = pxr::TfNotice::Register(
      pxr::TfWeakPtr<StageChangeListener>(m_changeListener.get()),
      &StageChangeListener::OnObjectsChanged,
      m_stage);
}

void UsdMod::Impl::unsubscribeFromStageChanges() {
  if (m_changeListener) {
    pxr::TfNotice::Revoke(m_noticeKey);
    m_changeListener.reset();
  }
}

void UsdMod::Impl::takeAndResetDirtyHashes(std::unordered_set<XXH64_hash_t>& outMesh,
                                            std::unordered_set<XXH64_hash_t>& outMat,
                                            std::unordered_set<XXH64_hash_t>& outLight,
                                            bool& outRebuildAllMeshes,
                                            bool& outRebuildAllMats,
                                            bool& outRebuildAllLights) {
  std::lock_guard<std::mutex> lock(m_changedPathsMutex);
  outMesh            = std::move(m_dirtyMeshHashes);
  outMat             = std::move(m_dirtyMatHashes);
  outLight           = std::move(m_dirtyLightHashes);
  outRebuildAllMeshes = m_rebuildAllMeshes;
  outRebuildAllMats   = m_rebuildAllMats;
  outRebuildAllLights = m_rebuildAllLights;
  m_dirtyMeshHashes.clear();
  m_dirtyMatHashes.clear();
  m_dirtyLightHashes.clear();
  m_rebuildAllMeshes = m_rebuildAllMats = m_rebuildAllLights = false;
}

bool UsdMod::Impl::checkForChanges(const Rc<DxvkContext>& context) {
  // Never start new load work once teardown has begun — either path below would
  // spawn a rebuild the shutdown sequence then has to wait out. Drop any queued
  // request too, or it latches and leaves the UI button disabled for good.
  if (isShuttingDown()) {
    m_owner.consumeReloadRequest();
    return false;
  }

  const bool watchForChanges = UsdMod::reloadOnChanged();

  // Don't consume signals mid-load: they'd be silently dropped and cause a false-positive reload.
  const auto progress = m_owner.state().progressState;
  if (progress != ProgressState::Loaded && progress != ProgressState::Unloaded) {
    return false;
  }

  // The async path needs a stage for the worker to reload; with none it would walk
  // nothing and abort. Recovering a mod that failed to open is the manual reload's
  // main use, so send that case to the synchronous path along with the kill-switches.
  // asyncAssetLoading=false means reloads are also synchronous, so tests that turn
  // it off to get blocking loads also get blocking reloads.
  const bool canRebuildAsync =
    RtxOptions::asyncAssetLoading() && progress == ProgressState::Loaded;

  if (!canRebuildAsync) {
    if (m_rebuildInFlight.load(std::memory_order_acquire)) {
      return false;  // Let the in-flight rebuild land; signals stay latched for next frame.
    }
    // m_pUsdFileChanged->exchange needs to consume input even if watchForChanges is off
    const bool fileChanged = m_pUsdFileChanged->exchange(false, std::memory_order_acq_rel) && watchForChanges;
    const bool reloadRequested = m_owner.consumeReloadRequest();
    if (fileChanged || reloadRequested) {
      unload();
      load(context);
      return true;
    }
    return false;
  }

  // Neither signal may be consumed while a rebuild is busy — both are consuming
  // reads, so we would drop the save or the click that arrived during it. The call
  // after applyPendingRebuild picks it up.
  if (m_rebuildInFlight.load(std::memory_order_acquire)) {
    return false;
  }

  // m_pUsdFileChanged->exchange needs to consume input even if watchForChanges is off
  const bool fileChanged = m_pUsdFileChanged->exchange(false, std::memory_order_acq_rel) && watchForChanges;
  const bool reloadRequested = m_owner.consumeReloadRequest();
  if (!fileChanged && !reloadRequested) {
    return false;
  }
  // dxvk::thread's move-assign detaches rather than terminates, so spawning over a live
  // worker would leave it writing this Impl's state with no handle left to join it.
  if (m_rebuildThread.joinable()) {
    Logger::err("USD hot-reload: previous rebuild worker was never joined; skipping this reload.");
    assert(false && "checkForChanges: previous rebuild worker was never joined");
    // Re-latch the signals consumed above so the save/click isn't swallowed.
    if (fileChanged) {
      m_pUsdFileChanged->store(true, std::memory_order_release);
    }
    if (reloadRequested) {
      m_owner.requestReload();
    }
    return false;
  }

  m_rebuildInFlight.store(true, std::memory_order_release);
  m_rebuildReady.store(false, std::memory_order_release);
  m_rebuildAborted.store(false, std::memory_order_release);

  // Raise the reload progress indicator here rather than in the worker so the
  // HUD message appears on the same frame the rebuild is spawned, with no gap
  // before the worker gets scheduled. Cleared in applyPendingRebuild.
  m_owner.setProgress(ProgressState::OpeningUSD);

  // Hold the device alive via Rc<> for the worker's lifetime.
  Rc<DxvkDevice> device = context->getDevice();
  m_rebuildThread = dxvk::thread([this, device, reloadRequested]() {
    this->rebuildWorkerEntry(device, /*isInitialLoad=*/false, reloadRequested);
  });

  return false;
}

void UsdMod::Impl::rebuildWorkerEntry(Rc<DxvkDevice> device, bool isInitialLoad, bool isManualReload) {
  env::setThreadName(isInitialLoad ? "rtx-usd-mod-load" : "rtx-usd-mod-rebuild");
  ScopedCpuProfileZone();

  // Set before the try so abort-path handling in applyPendingRebuild can read it
  // even after m_pending.clear() discards the payload.
  m_pending.isInitialLoad = isInitialLoad;
  if (isInitialLoad) {
    m_pending.openedFilePath = m_owner.m_filePath.string();
  }

  // dxvk::thread swallows exceptions, and a throw here would leave
  // m_rebuildInFlight stuck true, wedging future loads for the session. Catch and
  // let applyPendingRebuild reclaim the worker instead.
  try {
    // Its own context and cmdlist, so uploads submit independently of the render
    // thread's stream.
    Rc<DxvkContext> ctx = device->createContext();
    ctx->beginRecording(device->createCommandList());

    auto scratch = std::make_unique<AssetReplacements>();
    std::string pendingStatus;
    AssetChanges pendingChanges;

    if (isInitialLoad) {
      // Stage was just opened by load(); walk all prims with no dirty-hash filtering.
      if (!processUSD(ctx, *scratch, pendingStatus)) {
        m_pending.clear();
        m_rebuildAborted.store(true, std::memory_order_release);
        return;
      }
    } else {
      // Hot-reload: reload the stage so ObjectsChanged notices fire synchronously,
      // then drain the dirty sets before walking only the changed prims.
      reloadStage();

      std::unordered_set<XXH64_hash_t> dirtyMeshHashes, dirtyMatHashes, dirtyLightHashes;
      bool rebuildAllMeshes = false, rebuildAllMats = false, rebuildAllLights = false;
      takeAndResetDirtyHashes(dirtyMeshHashes, dirtyMatHashes, dirtyLightHashes,
                              rebuildAllMeshes, rebuildAllMats, rebuildAllLights);

      // A manually requested reload that found nothing dirty (e.g. clicked without
      // saving, or the change was in a dependency FileWatch wasn't watching) would
      // otherwise walk zero prims; force a full rebuild so the button still does something.
      const bool nothingDirty = dirtyMeshHashes.empty() && dirtyMatHashes.empty() && dirtyLightHashes.empty()
                              && !rebuildAllMeshes && !rebuildAllMats && !rebuildAllLights;
      if (isManualReload && nothingDirty) {
        rebuildAllMeshes = rebuildAllMats = rebuildAllLights = true;
      }

      // Captured before the material sweep below adds mesh hashes that are dirty only by
      // material dependency, not because their own geometry changed.
      const bool anyMeshGeometryDirty = rebuildAllMeshes || !dirtyMeshHashes.empty();

      // material:binding is a relationship, so USD's notices never name the mesh that
      // references a changed material. Find those meshes with a materialPathHash-matching
      // sweep over the live table — cheaper than walking every mesh in the scene.
      if (!dirtyMatHashes.empty() && !rebuildAllMeshes) {
        m_owner.m_replacements->collectMeshesUsingMaterials(dirtyMatHashes, dirtyMeshHashes);
      }

      const bool needFullMeshProcess = rebuildAllMeshes || rebuildAllMats;
      // Seed the geometry cache so re-walking meshes doesn't re-upload unchanged geometry.
      // Unsafe when a mesh's own geometry is dirty: the cache would return stale geometry
      // for that edited mesh.
      if (!anyMeshGeometryDirty) {
        scratch->seedGeometriesFrom(*m_owner.m_replacements);
      }
      // Seed unchanged materials too, so a mesh reprocessed for an unrelated reason (its own
      // geometry, say) reuses the existing object instead of deserializing a duplicate.
      // Skipped on a full mat rebuild: dirtyMatHashes doesn't list every live material yet,
      // so seeding would let stale content through as a false cache hit.
      if (!rebuildAllMats) {
        scratch->seedMaterialsFrom(*m_owner.m_replacements, dirtyMatHashes);
      }
      const std::unordered_set<XXH64_hash_t>* pMeshFilter  = needFullMeshProcess  ? nullptr : &dirtyMeshHashes;
      const std::unordered_set<XXH64_hash_t>* pMatFilter   = rebuildAllMats       ? nullptr : &dirtyMatHashes;
      const std::unordered_set<XXH64_hash_t>* pLightFilter = rebuildAllLights     ? nullptr : &dirtyLightHashes;

      if (!processUSD(ctx, *scratch, pendingStatus,
                      pMeshFilter, pMatFilter, pLightFilter)) {
        m_pending.clear();
        m_rebuildAborted.store(true, std::memory_order_release);
        return;
      }

      pendingChanges.dirtyMeshHashes  = std::move(dirtyMeshHashes);
      pendingChanges.dirtyLightHashes = std::move(dirtyLightHashes);
      pendingChanges.dirtyMatHashes   = std::move(dirtyMatHashes);
      pendingChanges.fullMeshRebuild  = rebuildAllMeshes;
      pendingChanges.fullLightRebuild = rebuildAllLights;
      pendingChanges.fullMatRebuild   = rebuildAllMats;
    }

    // Composed layers can change between reloads, so refresh after every successful walk.
    refreshLayerPaths();

    // The worker's own cmdlist — nothing else ever submits it.
    ctx->flushCommandList();

    // Flushing only submits; block here (worker thread, not render thread) until the GPU
    // copy actually completes.
    if (m_lastDeviceLocalUpload.ptr() != nullptr) {
      device->waitForResource(m_lastDeviceLocalUpload, DxvkAccess::Write);
      m_lastDeviceLocalUpload = nullptr;
    }

    // Release-store on m_rebuildReady pairs with the acquire-load in applyPendingRebuild.
    m_pending.status        = std::move(pendingStatus);
    m_pending.replacements  = std::move(scratch);
    m_pending.changes = std::move(pendingChanges);
    m_rebuildReady.store(true, std::memory_order_release);
  } catch (const std::exception& e) {
    Logger::err(str::format("USD mod worker failed; keeping current scene: ", e.what()));
    m_pending.clear();
    m_rebuildAborted.store(true, std::memory_order_release);
  } catch (...) {
    Logger::err("USD mod worker failed with an unknown exception; keeping current scene.");
    m_pending.clear();
    m_rebuildAborted.store(true, std::memory_order_release);
  }
}

bool UsdMod::Impl::applyPendingRebuild(const Rc<DxvkContext>& context, AssetChanges& changes) {
  ScopedCpuProfileZone();

  // Worker threw (see rebuildWorkerEntry's catch): join it, drop any partial
  // state, and re-arm so a later save can retry — without touching the live table.
  if (m_rebuildAborted.load(std::memory_order_acquire)) {
    if (m_rebuildThread.joinable()) {
      m_rebuildThread.join();
    }
    m_pending.clear();
    // m_pending.isInitialLoad is preserved across clear() so we know which state to fix.
    // Initial load: no content ever installed — stay Unloaded.
    // Hot-reload abort: previous content still valid — return to Loaded.
    m_owner.setProgress(m_pending.isInitialLoad ? ProgressState::Unloaded : ProgressState::Loaded);
    m_rebuildAborted.store(false, std::memory_order_release);
    m_rebuildInFlight.store(false, std::memory_order_release);
    return false;
  }

  if (!m_rebuildReady.load(std::memory_order_acquire)) {
    return false;
  }
  if (!m_pending.replacements) {
    Logger::err("USD mod: rebuild signalled ready but scratch table is missing; keeping current scene.");
    assert(false && "applyPendingRebuild: m_rebuildReady set without a scratch table");
    if (m_rebuildThread.joinable()) {
      m_rebuildThread.join();
    }
    m_pending.clear();
    m_owner.setProgress(m_pending.isInitialLoad ? ProgressState::Unloaded : ProgressState::Loaded);
    m_rebuildReady.store(false, std::memory_order_release);
    m_rebuildInFlight.store(false, std::memory_order_release);
    return false;
  }

  assert(m_rebuildInFlight.load(std::memory_order_relaxed));

  // Apply the scratch table into the live table in-place. For initial loads the
  // live table is empty, so this is a pure insert. For hot-reloads, entries for
  // dirty hashes not in the scratch (deleted prims) are erased from the live table,
  // and unchanged entries are untouched so RIs that hold a shared_ptr<ReplacementBucket>
  // remain valid. GPU resources (Rc<DxvkBuffer>) survive until in-flight commands complete.
  m_owner.m_replacements->mergeFrom(
      std::move(*m_pending.replacements),
      m_pending.changes);

  m_owner.m_status = std::move(m_pending.status);

  // Forward what changed so SceneManager can selectively invalidate the scene.
  changes.merge(m_pending.changes);

  if (m_pending.isInitialLoad) {
    m_openedFilePath = m_pending.openedFilePath;

    // Register file-change notifications now that the initial walk is complete.
    // reloadOnChanged is read live in checkForChanges, so toggling it at runtime
    // works without a reload; the callback just becomes a no-op when the flag is off.
    // FileWatch only lexically normalizes reported paths; normalizedPathKey() below adds
    // the canonical()+lowercase resolution needed to match m_openedFilePath. Computed once
    // since the root mod path is fixed for the mod's lifetime.
    const std::wstring rootPathKey = normalizedPathKey(m_openedFilePath);
    // Captured by shared_ptr rather than `this`: removeFileChangedCallback() only queues
    // a removal request, so a callback can still fire after this Impl starts tearing down.
    m_fileWatchCallbackId = FileWatch::get().addFileChangedCallback(
      [flag = m_pUsdFileChanged, rootPathKey, layerPaths = m_layerPaths](const std::filesystem::path& p) {
        std::lock_guard<std::mutex> lock(layerPaths->mutex);
        if (usdShouldTriggerReload(p, rootPathKey, UsdMod::watchDependencies(), &layerPaths->paths)) {
          flag->store(true, std::memory_order_release);
        }
      });

    // The listener was active during the walk (subscribeToStageChanges was called
    // in openStage). Drain any notices that arrived during the walk so the reload
    // system starts from a known-empty state.
    {
      std::lock_guard<std::mutex> lock(m_changedPathsMutex);
      m_dirtyMeshHashes.clear();
      m_dirtyMatHashes.clear();
      m_dirtyLightHashes.clear();
      m_rebuildAllMeshes = m_rebuildAllMats = m_rebuildAllLights = false;
    }

    m_owner.setProgress(ProgressState::Loaded);
  } else {
    // Cleared only now: the reload isn't done from the user's perspective until
    // the new data is actually in the live table.
    m_owner.setProgress(ProgressState::Loaded);
  }

  // Worker may have run inline (sync load path) — only join if a thread was spawned.
  if (m_rebuildThread.joinable()) {
    m_rebuildThread.join();
  }
  m_rebuildReady.store(false, std::memory_order_release);
  m_rebuildInFlight.store(false, std::memory_order_release);

  return true;
}

void UsdMod::Impl::onDestroy() {
  // RtxInitializer::onDestroy has already run, so the cancellation flag is up and an
  // in-progress walk bails within a prim or two. Joining here rather than in ~Impl is
  // what makes it safe: DxvkObjects destroys the initializer and texture manager
  // first, and a worker still running by then would be reading through both.
  FileWatch::get().removeFileChangedCallback(m_fileWatchCallbackId);
  m_fileWatchCallbackId = 0;

  if (m_rebuildThread.joinable()) {
    m_rebuildThread.join();
  }
  m_rebuildInFlight.store(false, std::memory_order_release);
  m_rebuildReady.store(false, std::memory_order_release);
  m_rebuildAborted.store(false, std::memory_order_release);
  m_pending.clear();
}

UsdMod::Impl::~Impl() {
  // Backstop for teardown paths that never ran onDestroy().
  FileWatch::get().removeFileChangedCallback(m_fileWatchCallbackId);
  if (m_rebuildThread.joinable()) {
    m_rebuildThread.join();
  }
  // Must unsubscribe before Impl is freed: onDestroy() only does this when the
  // mod was in Loaded state, but teardown paths skip unload(). A notice firing
  // into a dangling m_impl reference after this would crash.
  unsubscribeFromStageChanges();
}

bool UsdMod::Impl::isShuttingDown() const {
  return m_owner.isLoadingCancelled();
}

bool UsdMod::Impl::processUSD(const Rc<DxvkContext>& context, AssetReplacements& target, std::string& outStatus,
                              const std::unordered_set<XXH64_hash_t>* pDirtyMeshHashes,
                              const std::unordered_set<XXH64_hash_t>* pDirtyMatHashes,
                              const std::unordered_set<XXH64_hash_t>* pDirtyLightHashes) {
  ScopedCpuProfileZone();
  std::string replacementsUsdPath(m_owner.m_filePath.string());

  // Stage open / reload is the caller's responsibility: load() calls openStage();
  // rebuildWorkerEntry() calls reloadStage() so that notices have fired and dirty
  // hashes are set before processUSD walks the prims.
  if (!m_stage) {
    Logger::err(str::format("processUSD called with no open stage for: ",
                            std::filesystem::weakly_canonical(replacementsUsdPath).string()));
    return false;
  }

  pxr::UsdStageRefPtr stage = m_stage;

  std::filesystem::path modBaseDirectory = std::filesystem::path(replacementsUsdPath).remove_filename();

  // Sublayer base paths, in strength order. Registered with AssetDataManager immediately,
  // before the material/texture walk that needs them.
  // Collected in ascending precedence and published as one list, so precedence within
  // a mod is just position here and precedence between mods is the mod order.
  std::vector<std::filesystem::path> collectedSearchPaths;
  auto collectSearchPath = [&](const std::filesystem::path& searchPath) {
    collectedSearchPaths.push_back(searchPath);
  };
  auto sublayers = stage->GetRootLayer()->GetSubLayerPaths();
  for (size_t i = 0, s = sublayers.size(); i < s; i++) {
    const std::string& identifier = sublayers[i];
    auto layerBasePath = std::filesystem::path(identifier).remove_filename();
    auto fullLayerBasePath = modBaseDirectory / layerBasePath;
    collectSearchPath(fullLayerBasePath);
  }

  // Add stage's base path last - highest precedence within this mod.
  collectSearchPath(modBaseDirectory);

  // Publish immediately: the material/texture walk below resolves textures through
  // AssetDataManager, which needs these search paths registered before it runs, not
  // after processUSD returns.
  m_owner.setSearchPaths(std::move(collectedSearchPaths));

  pxr::UsdGeomXformCache xformCache;

  pxr::VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
  if (layerData.empty()) {
    outStatus = "Layer Data Missing";
  } else {
    const PXR_NS::VtValue* vtExportStatus = layerData.GetValueAtPath(kStatusKey);
    if (vtExportStatus && !vtExportStatus->IsEmpty()) {
      outStatus = vtExportStatus->Get<std::string>();
    } else {
      outStatus = "Status Missing";
    }
  }

  auto setProgress = [this](ProgressState progressState, std::uint32_t progressCount) {
    m_owner.setProgressWithCount(progressState, progressCount);
  };

  // Process Materials

  setProgress(ProgressState::ProcessingMaterials, 0);

  pxr::UsdPrim materialRoot = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/Looks"));
  if (materialRoot.IsValid()) {
    const auto children = materialRoot.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentMaterialCount{ 0U };
    std::vector<AssetReplacement> placeholder;

    Args args = {context, xformCache, materialRoot, placeholder};
    args.target = &target;

    for (pxr::UsdPrim materialPrim : children) {
      // Cancellation is checked per prim rather than per stage: that is fine
      // enough granularity to keep teardown prompt without adding an atomic read
      // to the inner per-mesh work.
      if (isShuttingDown()) {
        return false;
      }
      // Hot-reload: skip prims whose material hash is confirmed-clean.
      if (pDirtyMatHashes != nullptr) {
        const XXH64_hash_t h = getMaterialHash(materialPrim, findMaterialShader(materialPrim));
        if (h != 0 && pDirtyMatHashes->count(h) == 0) {
          ++currentMaterialCount;
          continue;
        }
      }
      processMaterial(args, materialPrim);

      // Note: Update the state progress only every 16 materials to reduce the number of atomic writes.
      if ((++currentMaterialCount & 0b1111u) == 0u) {
        setProgress(ProgressState::ProcessingMaterials, currentMaterialCount);
      }
    }
  }

  // Process Meshes

  setProgress(ProgressState::ProcessingMeshes, 0);

  fast_unordered_cache<uint32_t> variantCounts;
  pxr::UsdPrim meshes = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/meshes"));
  if (meshes.IsValid()) {
    const auto children = meshes.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentMeshCount{ 0U };

    for (pxr::UsdPrim child : children) {
      if (isShuttingDown()) {
        return false;
      }
      const auto hash = getModelHash(child);

      // Hot-reload: skip mesh prims that are not in the dirty set.
      if (pDirtyMeshHashes != nullptr && hash != 0 && pDirtyMeshHashes->count(hash) == 0) {
        ++currentMeshCount;
        continue;
      }

      if (hash != 0) {
        std::vector<AssetReplacement> replacementVec;
        Args args = {context, xformCache, child, replacementVec};
        args.target = &target;

        if (processReplacement(args)) {
          variantCounts[hash]++;

          publishMeshReplacement(args, hash, replacementVec);
        }
      }

      // Note: Update the state progress only every 16 meshes to reduce the number of atomic writes.
      if ((++currentMeshCount & 0b1111u) == 0u) {
        setProgress(ProgressState::ProcessingMeshes, currentMeshCount);
      }
    }
  }

  // Process Secret Meshes
  // Skipped on hot-reload: secret variants live in separate USD stages that never
  // emit TfNotices to our listener, so they can't be filtered by dirty hashes. A
  // hot-reload would inject new bucket pointers for all variants without any
  // corresponding RI scrub, leaving anti-culled RIs rendering stale geometry.
  // Initial loads (pDirtyMeshHashes == nullptr) pick them up normally.
  if (pDirtyMeshHashes == nullptr) {
    // TODO: enter "secrets" section of USD as exported by Kit app
    TEMP_parseSecretReplacementVariants(target, variantCounts);
    for (auto& [hash, secretReplacements] : target.secretReplacements()) {
      for (auto& secretReplacement : secretReplacements) {
        // Each variant opens its own stage, so this loop is as worth cancelling as
        // the main walks above.
        if (isShuttingDown()) {
          return false;
        }
        const std::string variantStage(modBaseDirectory.string() + secretReplacement.replacementPath);
        double dummy;
        if (!pxr::ArchGetModificationTime(variantStage.c_str(),&dummy)) {
          Logger::warn(
            std::string("[SecretReplacement] Could not find stage: ") + variantStage);
          continue;
        }
        auto pStage = pxr::UsdStage::Open(variantStage, pxr::UsdStage::LoadAll);
        if (!pStage) {
          Logger::err(
            std::string("[SecretReplacement] Failed to open stage: ") + variantStage);
          continue;
        }
        auto rootPrim = pStage->GetDefaultPrim();
        auto variantHash = hash + secretReplacement.variantId;
        std::vector<AssetReplacement> replacementVec;

        Args args = {context, xformCache, rootPrim, replacementVec};
        args.target = &target;

        if (processReplacement(args)) {
          publishMeshReplacement(args, variantHash, replacementVec);
        }
      }
    }
  }

  // Process Lights

  setProgress(ProgressState::ProcessingLights, 0);

  pxr::UsdPrim lights = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/lights"));
  if (lights.IsValid()) {
    const auto children = lights.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentLightCount{ 0U };

    for (pxr::UsdPrim child : children) {
      if (isShuttingDown()) {
        return false;
      }
      const auto hash = getLightHash(child);

      // Hot-reload: skip light prims that are not in the dirty set.
      if (pDirtyLightHashes != nullptr && hash != 0 && pDirtyLightHashes->count(hash) == 0) {
        ++currentLightCount;
        continue;
      }

      if (hash != 0) {
        std::vector<AssetReplacement> replacementVec;
        Args args = {context, xformCache, child, replacementVec};
        args.target = &target;

        if (processReplacement(args)) {
          target.set<AssetReplacement::eLight>(hash, std::move(replacementVec));
        }
      }

      // Note: Update the state progress only every 16 lights to reduce the number of atomic writes.
      if ((++currentLightCount & 0b1111u) == 0u) {
        setProgress(ProgressState::ProcessingLights, currentLightCount);
      }
    }
  }

  // flush entire cache, kinda a sledgehammer
  context->emitMemoryBarrier(0,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

  return true;
}

void UsdMod::Impl::publishMeshReplacement(Args& args, XXH64_hash_t hash, std::vector<AssetReplacement>& replacementVec) {
  // Both initial load and hot-reload write to a scratch table that gets swapped
  // into the live table by applyPendingRebuild on the render thread, so there is
  // no command-list synchronisation needed here.
  args.target->set<AssetReplacement::eMesh>(hash, std::move(replacementVec));
}

void UsdMod::Impl::TEMP_parseSecretReplacementVariants(AssetReplacements& target, const fast_unordered_cache<uint32_t>& variantCounts) {
  auto lookupCount = [&variantCounts](XXH64_hash_t hash) -> auto {
    // NOTE: If there's no default replacement make sure secret variants are not default.
    return variantCounts.count(hash) ? variantCounts.at(hash) : 1u;
  };

  static constexpr XXH64_hash_t kStorageCubeHash = 0xc728cfe75526c741;
  uint32_t numVariants = lookupCount(kStorageCubeHash);
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Ice","",
    0x60ead40e2269b3c5,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Ice.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Lens","",
    0xa8e871f4ebc52eab,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Lens.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Camera","",
    0xd150bdeff3f0299a,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeCamera_A01_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Digital Skull","",
    0xb26578451f75c11a,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeDigital_A02_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Wheatly","",
    0xc270f63a956c0c71,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A01_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Voyager","",
    0xaaaf0cbd8c8204cd,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A02_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Black-Mesa","",
    0x2f9fe4ce23a83bc2,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A03_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","RTX","",
    0xe361f386c03400f3,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_RTX_CompanionCube_A1_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Roll Cage","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_RollCage.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Health Pack","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Healthpack.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Space","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Space.usd",
    true,
    true,
    numVariants++});

  static constexpr XXH64_hash_t kCompanionCubeHash = 0x6ef165bb7e0b8512;
  numVariants = lookupCount(kCompanionCubeHash);
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Pillow","",
    0xc901411d90916a58,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Pillow_A.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Ceramic","",
    0x3495c5b9d210daa1,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Ceramic.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Wood","",
    0x5e50cb7c64375acc,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Wood.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Digital","",
    0xf2bda31c09fc42f6,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeDigital_A01_01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Steampunk","",
    0x0,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_SteamPunk_A01.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Arts and Crafts","",
    0x0,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_ArtsAndCrafts.usd",
    true,
    true,
    numVariants++});
  target.storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Cubus","",
    0x0,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Cubus.usd",
    true,
    true,
    numVariants++});
}


Categorizer UsdMod::Impl::processCategoryFlags(const pxr::UsdPrim& prim) {
  Categorizer categoryFlags;
  for (uint32_t i = 0; i < (uint32_t) InstanceCategories::Count; i++) {
    const char* categoryName = getInstanceCategorySubKey((InstanceCategories) i);
    pxr::TfToken token = pxr::TfToken(categoryName);
    if (!prim.HasAttribute(token)) {
      continue;
    }

    pxr::VtValue value;
    if (!prim.GetAttribute(token).Get(&value)) {
      continue;
    }

    categoryFlags.categoryExists.set((InstanceCategories) i);
    if (value.Get<bool>()) {
      categoryFlags.categoryFlags.set((InstanceCategories) i);
    }
  }

  return categoryFlags;
}

bool UsdMod::Impl::processMesh(const pxr::UsdPrim& prim, Args& args) {
  MeshReplacement replacement;
  RasterGeometry& geometryData = replacement.data;

  std::unique_ptr<lss::UsdMeshImporter> processedMesh;

  try {
    processedMesh = std::make_unique<lss::UsdMeshImporter>(prim, RtxOptions::limitedBonesPerVertex());
  }
  catch (DxvkError e) {
    Logger::err(e.message());
    return false;
  }

  geometryData.vertexCount = processedMesh->GetNumVertices();

  if (processedMesh->GetNumVertices() == 0) {
    throw DxvkError(str::format("Warning: No vertices on this mesh after processing, id=.", prim.GetName()));
  }

  const size_t vertexDataSize = processedMesh->GetNumVertices() * processedMesh->GetVertexStride();

  // Allocate the instance buffer and copy its contents from host to device memory
  DxvkBufferCreateInfo info;
  info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
  info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
  info.size = dxvk::align(vertexDataSize, CACHE_LINE_SIZE);

  // Check if the mesh has weights
  bool isDynamicMesh = false;
  for (const auto& element : processedMesh->GetVertexDecl()) {
    isDynamicMesh |= element.attribute == lss::UsdMeshImporter::BlendWeights;
    if (isDynamicMesh)
      break;
  }

  // Buffer contains:
  // |---POSITIONS---|---NORMALS---|---UVS---| ... (VERTEX DATA INTERLEAVED)
  Rc<DxvkBuffer> vertexBuffer_staging = args.context->getDevice()->createBuffer(info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, DxvkMemoryStats::Category::RTXReplacementGeometry, "Mesh Staging Buffer");
  memcpy(vertexBuffer_staging->mapPtr(0), processedMesh->GetVertexData().data(), vertexDataSize);

  // Dynamic meshes should have their vertex data in device memory, static meshes should reside in host memory and allow geometry streaming to handle host/device memory management
  Rc<DxvkBuffer> vertexBuffer;
  if (isDynamicMesh) {
    vertexBuffer = args.context->getDevice()->createBuffer(
        info,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXReplacementGeometry, prim.GetName().GetString().c_str());
    args.context->copyBuffer(vertexBuffer, 0, vertexBuffer_staging, 0, vertexDataSize);
    m_lastDeviceLocalUpload = vertexBuffer;
  } else {
    vertexBuffer = vertexBuffer_staging;
  }

  const DxvkBufferSlice& vertexSlice = DxvkBufferSlice(vertexBuffer);

  for (const auto& element : processedMesh->GetVertexDecl()) {
    switch (element.attribute) {
    case lss::UsdMeshImporter::VertexPositions:
      geometryData.positionBuffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_R32G32B32_SFLOAT);
      break;
    case lss::UsdMeshImporter::Normals:
      geometryData.normalBuffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_R32_UINT);
      break;
    case lss::UsdMeshImporter::Texcoords:
      geometryData.texcoordBuffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_R32G32_SFLOAT);
      geometryData.hashes[HashComponents::VertexTexcoord] = getNextGeomHash();
      break;
    case lss::UsdMeshImporter::Colors:
      geometryData.color0Buffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_B8G8R8A8_UNORM);
      break;
    case lss::UsdMeshImporter::BlendWeights:
      geometryData.blendWeightBuffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_R32_SFLOAT);
      // Note: only want to set this when there are actually weights, as it triggers the replacement to be skinned.
      geometryData.numBonesPerVertex = processedMesh->GetNumBonesPerVertex(); // TODO: Implement this in UsdMesh
      break;
    case lss::UsdMeshImporter::BlendIndices:
      geometryData.blendIndicesBuffer = RasterBuffer(vertexSlice, element.offset, processedMesh->GetVertexStride(), VK_FORMAT_R8G8B8A8_USCALED);
      break;
    default:
      assert(false && "Invalid vertex attribute in UsdMod::Impl::processMesh");
      break;
    }
  }

  geometryData.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if (processedMesh->GetDoubleSidedState() != lss::UsdMeshImporter::Inherit) {
    const VkCullModeFlagBits singleSidedCullMode = processedMesh->IsRightHanded() ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
    geometryData.cullMode = processedMesh->GetDoubleSidedState() == lss::UsdMeshImporter::IsDoubleSided ? VK_CULL_MODE_NONE : singleSidedCullMode;
    geometryData.forceCullBit = true; // Overrule the instance face culling rules
  } else {
    // In this case we use the face culling set from the application for this mesh
    geometryData.cullMode = VK_CULL_MODE_NONE;
  }

  geometryData.frontFace = processedMesh->IsRightHanded() ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;

  // Get bounding box from the USD mesh importer
  geometryData.boundingBox = processedMesh->GetBoundingBox();

  for (const lss::UsdMeshImporter::SubMesh& submesh : processedMesh->GetSubMeshes()) {
    if (submesh.GetNumIndices() == 0) {
      Logger::err(str::format("Prim: ", submesh.prim.GetPath().GetString(), ", does not have indices, this is currently a requirement."));
      continue;
    }

    XXH64_hash_t usdOriginHash = getStrongestOpinionatedPathHash(submesh.prim);
    std::shared_ptr<MeshReplacement> childGeometryData;
    if (!args.target->getGeometry(usdOriginHash, childGeometryData)) {
      const auto newReplacement = args.target->storeGeometry(usdOriginHash, MeshReplacement(replacement));
      RasterGeometry& newGeomData = newReplacement->data;

      const size_t indexDataSize = submesh.GetNumIndices() * sizeof(uint32_t);
      info.size = dxvk::align(indexDataSize, CACHE_LINE_SIZE);

      // Buffer contains: indices
      Rc<DxvkBuffer> indexBuffer_staging = args.context->getDevice()->createBuffer(info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, DxvkMemoryStats::Category::RTXReplacementGeometry, "Mesh index buffer staging");
      memcpy(indexBuffer_staging->mapPtr(0), submesh.indexBuffer.data(), indexDataSize);
      Rc<DxvkBuffer> indexBuffer;
      if (isDynamicMesh) {
        indexBuffer = args.context->getDevice()->createBuffer(
          info,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          DxvkMemoryStats::Category::RTXReplacementGeometry, submesh.prim.GetName().GetString().c_str());
        args.context->copyBuffer(indexBuffer, 0, indexBuffer_staging, 0, indexDataSize);
        m_lastDeviceLocalUpload = indexBuffer;
      } else {
        indexBuffer = indexBuffer_staging;
      }

      const DxvkBufferSlice& indexSlice = DxvkBufferSlice(indexBuffer);
      newGeomData.indexBuffer = RasterBuffer(indexSlice, 0, sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
      newGeomData.indexCount = submesh.GetNumIndices();
      // Set these as hashed so that the geometryData acts like it's static.
      newGeomData.hashes[HashComponents::Indices] = newGeomData.hashes[HashComponents::VertexPosition] = getNextGeomHash();
      newGeomData.hashes.precombine();
    }
  }

  return true;
}

UsdMod::UsdMod(const Mod::Path& usdFilePath)
: Mod(usdFilePath) {
  m_impl = std::make_unique<Impl>(*this);
  loadUsdPlugins();
}

void UsdMod::loadUsdPlugins() {
  // Load plugins
  static std::string pluginsDir;
  auto dir = env::getDllDirectory();
  if (!dir.empty()) {
    std::filesystem::path p(dir);
    p /= "usd\\plugins";
    pluginsDir = p.string();
    const auto& plugins = pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginsDir);

    if (plugins.empty()) {
      Logger::warn("usd plugins were not loaded");
      return;
    }

    for (auto const& notice : plugins) {
      if(!notice->IsLoaded() && !notice->Load()) {
        Logger::warn(str::format("USD plugin, ", notice->GetName(), " failed to load!"));
      } else {
        Logger::info(str::format("USD plugin, ", notice->GetName(), " loaded!"));
      }

      const std::string& name = notice->GetName();
      const std::string& libPath = notice->GetPath();
      const std::string& resourcePath = notice->GetResourcePath();

      Logger::debug(str::format("Plugin Info: ", name, "\n"
                                , "\tLibrary:       ", libPath, "\n"
                                , "\tResourcePath:  ", resourcePath));
    }
  } else {
    Logger::warn("usd plugins were not loaded");
  }
}

UsdMod::~UsdMod() {
}

void UsdMod::load(const Rc<DxvkContext>& context) {
  m_impl->load(context);
}

void UsdMod::unload() {
  m_impl->unload();
}

void UsdMod::onDestroy() {
  m_impl->onDestroy();
}

bool UsdMod::checkForChanges(const Rc<DxvkContext>& context) {
  return m_impl->checkForChanges(context);
}

bool UsdMod::applyPendingRebuild(const Rc<DxvkContext>& context, AssetChanges& changes) {
  return m_impl->applyPendingRebuild(context, changes);
}

struct UsdModTypeInfo final : public ModTypeInfo {
  std::unique_ptr<Mod> construct(const Mod::Path& modFilePath) const {
    return std::unique_ptr<UsdMod>(new UsdMod(modFilePath));
  }

  bool isValidMod(const Mod::Path& modFilePath) const {
    const auto ext = modFilePath.extension().string();
    for (auto& usdExt : lss::usdExts) {
      if (ext == usdExt.str) {
        return true;
      }
    }
    return false;
  }
};

const ModTypeInfo& UsdMod::getTypeInfo() {
  static UsdModTypeInfo s_typeInfo;
  return s_typeInfo;
}

static std::string getRemixCategoriesSchemaUsda() {
  std::stringstream ss;
  ss << "#usda 1.0\n";
  ss << "(\n";
  ss << "    \"\"\"Generated from RTX_OPTION category descriptions. Do not edit directly.\"\"\"\n";
  ss << ")\n";
  ss << "\n";
  ss << "class \"RemixInstanceCategoryAPI\" (\n";
  ss << "    customData = {\n";
  ss << "        string userDocBrief = \"Adds Remix instance category flags to a prim.\"\n";
  ss << "    }\n";
  ss << ")\n";
  ss << "{\n";
  for (const RemixCategoryEntry& entry : kRemixCategoryEntries) {
    const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(entry.optionName);
    if (option == nullptr || option->getDescription() == nullptr || option->getDescription()[0] == '\0') {
      return {};
    }

    ss << "    bool " << entry.attr << " = 0 (\n";
    ss << "        doc = \"" << str::escapeCStyle(option->getDescription()) << "\"\n";
    ss << "        displayGroup = \"Remix Categories\"\n";
    ss << "        displayName = \"" << entry.displayName << "\"\n";
    ss << "    )\n";
  }
  ss << "}\n";
  return ss.str();
}

} // namespace dxvk

#ifdef _WIN32
extern "C" __declspec(dllexport)
#else
extern "C" __attribute__((visibility("default")))
#endif
bool writeRemixCategoriesSchemaUsda(const char* outputFilePath) {
  const std::string schema = dxvk::getRemixCategoriesSchemaUsda();
  if (outputFilePath == nullptr || schema.empty()) {
    return false;
  }

  std::ofstream file(outputFilePath);
  file << schema;
  file.close();
  return !file.fail();
}

