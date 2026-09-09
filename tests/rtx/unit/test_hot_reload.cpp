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

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>

#include "../../test_utils.h"
#include "rtx_render/rtx_asset_replacer.h"
#include "rtx_render/rtx_mod_usd.h"
#include "rtx_render/graph/rtx_graph_types.h"

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/notice.h>
#include "../../../src/lssusd/usd_include_end.h"

// Both defined in rtx_mod_usd.cpp (part of dxvk_lib).
namespace dxvk {
void classifyChangedPath(const pxr::SdfPath& path, bool isResync, const pxr::UsdStageWeakPtr& stage,
                         AssetReplacements& replacements,
                         std::unordered_set<XXH64_hash_t>& dirtyMeshHashes,
                         std::unordered_set<XXH64_hash_t>& dirtyMatHashes,
                         std::unordered_set<XXH64_hash_t>& dirtyLightHashes,
                         bool& rebuildAllMeshes, bool& rebuildAllMats, bool& rebuildAllLights);
bool usdShouldTriggerReload(const std::filesystem::path& changedPath,
                             const std::wstring& normalizedRootModPathKey,
                             bool watchDependencies,
                             const std::unordered_set<std::wstring>* usedLayerPaths = nullptr);
std::wstring normalizedPathKey(const std::filesystem::path& rawPath);
} // namespace dxvk

namespace dxvk {

Logger Logger::s_instance("test_hot_reload.log");

// ─── USD change listener ──────────────────────────────────────────────────────
// Thin wrapper that forwards UsdObjectsChanged notifications into classifyPath.
struct ChangeCapture : pxr::TfWeakBase {
  // Material-path index classifyChangedPath consults; tests populate it via registerMaterialPath.
  AssetReplacements replacements;
  std::unordered_set<XXH64_hash_t> dirtyMeshHashes;
  std::unordered_set<XXH64_hash_t> dirtyMatHashes;
  std::unordered_set<XXH64_hash_t> dirtyLightHashes;
  bool rebuildAllMeshes = false;
  bool rebuildAllMats   = false;
  bool rebuildAllLights = false;

  void reset() {
    dirtyMeshHashes.clear();
    dirtyMatHashes.clear();
    dirtyLightHashes.clear();
    rebuildAllMeshes = rebuildAllMats = rebuildAllLights = false;
  }

  void OnObjectsChanged(const pxr::UsdNotice::ObjectsChanged& notice,
                        const pxr::UsdStageWeakPtr& sender) {
    for (const auto& p : notice.GetResyncedPaths()) {
      classifyChangedPath(p, /*isResync=*/true, sender, replacements,
                          dirtyMeshHashes, dirtyMatHashes, dirtyLightHashes,
                          rebuildAllMeshes, rebuildAllMats, rebuildAllLights);
    }
    for (const auto& p : notice.GetChangedInfoOnlyPaths()) {
      classifyChangedPath(p, /*isResync=*/false, sender, replacements,
                          dirtyMeshHashes, dirtyMatHashes, dirtyLightHashes,
                          rebuildAllMeshes, rebuildAllMats, rebuildAllLights);
    }
  }
};

// ─── Helpers ──────────────────────────────────────────────────────────────────
// Returns the 64-bit hash encoded in a mesh_/mat_/light_ prim name.
static XXH64_hash_t hashFromHexName(const char* prefix, const char* primName) {
  const size_t prefixLen = strlen(prefix);
  return std::strtoull(primName + prefixLen, nullptr, 16);
}

// Write a minimal mod.usda to disk and return the path.
static std::string writeTempUsd(const std::string& content) {
  namespace fs = std::filesystem;
  const auto path = (fs::temp_directory_path() / "test_hot_reload.usda").string();
  std::ofstream f(path);
  f << content;
  return path;
}

// UsdStage::Reload() skips a layer whose on-disk mtime hasn't visibly changed; two writes
// issued back-to-back in a test can land within the filesystem's mtime granularity.
static void bumpMtimeForward(const std::string& path) {
  namespace fs = std::filesystem;
  fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(2));
}

// ─── mergeFrom helpers ───────────────────────────────────────────────────
static void setMeshBucket(AssetReplacements& table, XXH64_hash_t hash) {
  table.set<AssetReplacement::eMesh>(hash, { AssetReplacement("test") });
}
static bool hasMeshBucket(AssetReplacements& table, XXH64_hash_t hash) {
  return table.get<AssetReplacement::eMesh>(hash) != nullptr;
}

// ─── Material cache tests (mergeFrom) ────────────────────────────────────
// These cover the bug where sweepOrphans(m_materials) evicted standalone
// material replacements (sky, surface overrides via getReplacementMaterial)
// because they always have use_count==1 — only the cache holds them.

// Standalone materials survive a hot-reload that processes other things.
static void testStandaloneMatSurvivesHotReload() {
  AssetReplacements live, scratch;

  constexpr XXH64_hash_t kSkyMat  = 0x5AFEDEAD;
  constexpr XXH64_hash_t kMeshKey = 0x1111;

  live.storeMaterial(kSkyMat, MaterialData{});  // standalone — use_count==1 always
  setMeshBucket(live, kMeshKey);

  // A mesh changed; the standalone material was NOT in the dirty set.
  setMeshBucket(scratch, kMeshKey);

  AssetChanges info;
  info.dirtyMeshHashes = { kMeshKey };
  live.mergeFrom(std::move(scratch), info);

  std::shared_ptr<MaterialData> out;
  if (!live.getMaterial(kSkyMat, out)) {
    throw DxvkError("standalone material must survive a mesh-only reload");
  }
  if (!hasMeshBucket(live, kMeshKey)) {
    throw DxvkError("updated mesh must be present");
  }

  Logger::info("testStandaloneMatSurvivesHotReload passed");
}

// A deleted material prim (dirty but absent from scratch) is erased.
static void testDeletedMatErasedFromCache() {
  AssetReplacements live, scratch;

  constexpr XXH64_hash_t kDeletedMat   = 0xDEAD1234;
  constexpr XXH64_hash_t kSurvivingMat = 0xBEEF5678;

  live.storeMaterial(kDeletedMat,   MaterialData{});
  live.storeMaterial(kSurvivingMat, MaterialData{});

  // kDeletedMat removed from stage: dirty but absent from scratch.
  // kSurvivingMat untouched: NOT dirty, must be left alone.
  AssetChanges info;
  info.dirtyMatHashes = { kDeletedMat };
  live.mergeFrom(std::move(scratch), info);

  std::shared_ptr<MaterialData> out;
  if (live.getMaterial(kDeletedMat, out)) {
    throw DxvkError("deleted material prim must be erased");
  }
  if (!live.getMaterial(kSurvivingMat, out)) {
    throw DxvkError("untouched material must survive");
  }

  Logger::info("testDeletedMatErasedFromCache passed");
}

// A pure-deletion reload (dirty mesh hash present, scratch empty - nothing reprocessed)
// must still sweep now-orphaned geometry/graph-topology cache entries.
static void testOrphanSweepOnPureDeletionReload() {
  AssetReplacements live, scratch;

  constexpr XXH64_hash_t kDeletedMesh = 0xDEAD0001;
  constexpr XXH64_hash_t kGeomHash    = 0xBEEF0001;

  setMeshBucket(live, kDeletedMesh);
  live.storeGeometry(kGeomHash, MeshReplacement{});  // only the cache holds it: use_count==1

  AssetChanges info;
  info.dirtyMeshHashes = { kDeletedMesh };
  live.mergeFrom(std::move(scratch), info);

  if (hasMeshBucket(live, kDeletedMesh)) {
    throw DxvkError("deleted mesh must be erased");
  }
  std::shared_ptr<MeshReplacement> out;
  if (live.getGeometry(kGeomHash, out)) {
    throw DxvkError("orphaned geometry must be swept on a pure-deletion reload");
  }

  Logger::info("testOrphanSweepOnPureDeletionReload passed");
}

// Simulates modify+undo: two hot-reloads where a standalone material was never
// dirty — it must survive both.
static void testModifyUndoCyclePreservesStandaloneMat() {
  AssetReplacements live, scratch1, scratch2;

  constexpr XXH64_hash_t kSkyMat  = 0x5CAFEBABE;
  constexpr XXH64_hash_t kRemovedMat = 0xDEAD;
  constexpr XXH64_hash_t kMesh    = 0x1111;

  live.storeMaterial(kSkyMat,     MaterialData{});
  live.storeMaterial(kRemovedMat, MaterialData{});
  setMeshBucket(live, kMesh);

  // Reload 1: kRemovedMat deleted, kSkyMat untouched.
  setMeshBucket(scratch1, kMesh);
  {
    AssetChanges info1;
    info1.dirtyMeshHashes = { kMesh };
    info1.dirtyMatHashes  = { kRemovedMat };
    live.mergeFrom(std::move(scratch1), info1);
  }

  std::shared_ptr<MaterialData> out;
  if (live.getMaterial(kRemovedMat, out)) {
    throw DxvkError("deleted mat must be gone after reload 1");
  }
  if (!live.getMaterial(kSkyMat, out)) {
    throw DxvkError("standalone mat must survive reload 1");
  }

  // Reload 2: kRemovedMat restored. kSkyMat still untouched.
  setMeshBucket(scratch2, kMesh);
  scratch2.storeMaterial(kRemovedMat, MaterialData{});
  {
    AssetChanges info2;
    info2.dirtyMeshHashes = { kMesh };
    info2.dirtyMatHashes  = { kRemovedMat };
    live.mergeFrom(std::move(scratch2), info2);
  }

  if (!live.getMaterial(kRemovedMat, out)) {
    throw DxvkError("restored mat must be back after reload 2");
  }
  if (!live.getMaterial(kSkyMat, out)) {
    throw DxvkError("standalone mat must survive reload 2");
  }

  Logger::info("testModifyUndoCyclePreservesStandaloneMat passed");
}

// ─── Test: seedMaterialsFrom reuses an unchanged material's exact object ──────
static void testSeedMaterialsFromReusesUnchangedMaterial() {
  AssetReplacements live, scratch;

  constexpr XXH64_hash_t kMat = 0xAAAA1111;
  auto original = live.storeMaterial(kMat, MaterialData{});

  std::unordered_set<XXH64_hash_t> dirtyMatHashes;  // kMat is unchanged.
  scratch.seedMaterialsFrom(live, dirtyMatHashes);

  std::shared_ptr<MaterialData> seeded;
  if (!scratch.getMaterial(kMat, seeded)) {
    throw DxvkError("seedMaterialsFrom should seed the unchanged material");
  }
  if (seeded != original) {
    throw DxvkError("seedMaterialsFrom must reuse the exact live object, not a duplicate");
  }

  Logger::info("testSeedMaterialsFromReusesUnchangedMaterial passed");
}

// ─── Test: seedMaterialsFrom skips dirty hashes, including deletions ──────────
static void testSeedMaterialsFromExcludesDirtyHashes() {
  AssetReplacements live, scratch;

  constexpr XXH64_hash_t kDeletedMat = 0xBBBB2222;
  live.storeMaterial(kDeletedMat, MaterialData{});

  // Simulates kDeletedMat's prim being classified dirty by deletion.
  std::unordered_set<XXH64_hash_t> dirtyMatHashes = { kDeletedMat };
  scratch.seedMaterialsFrom(live, dirtyMatHashes);

  std::shared_ptr<MaterialData> out;
  if (scratch.getMaterial(kDeletedMat, out)) {
    throw DxvkError("seedMaterialsFrom must not seed a hash in the dirty set");
  }

  AssetChanges changes;
  changes.dirtyMatHashes = dirtyMatHashes;
  live.mergeFrom(std::move(scratch), changes);

  if (live.getMaterial(kDeletedMat, out)) {
    throw DxvkError("a dirty material absent from scratch (deleted) must be erased after merge");
  }

  Logger::info("testSeedMaterialsFromExcludesDirtyHashes passed");
}

// ─── Test: collectMeshesUsingMaterials finds meshes by material identity hash ──
static void testCollectMeshesUsingMaterials() {
  AssetReplacements table;

  constexpr XXH64_hash_t kDirtyMat = 0xAAAA;
  constexpr XXH64_hash_t kCleanMat = 0xBBBB;
  constexpr XXH64_hash_t kMeshUsingDirty = 0x1111;
  constexpr XXH64_hash_t kMeshUsingClean = 0x2222;
  constexpr XXH64_hash_t kMeshUsingNone  = 0x3333;

  auto dirtyMat = table.storeMaterial(kDirtyMat, MaterialData{});
  auto cleanMat = table.storeMaterial(kCleanMat, MaterialData{});

  AssetReplacement usingDirty("meshUsingDirty");
  usingDirty.type = AssetReplacement::eMesh;
  usingDirty.materialData = dirtyMat;
  usingDirty.materialPathHash = kDirtyMat;
  table.set<AssetReplacement::eMesh>(kMeshUsingDirty, { usingDirty });

  AssetReplacement usingClean("meshUsingClean");
  usingClean.type = AssetReplacement::eMesh;
  usingClean.materialData = cleanMat;
  usingClean.materialPathHash = kCleanMat;
  table.set<AssetReplacement::eMesh>(kMeshUsingClean, { usingClean });

  AssetReplacement usingNone("meshUsingNone");
  usingNone.type = AssetReplacement::eMesh;
  table.set<AssetReplacement::eMesh>(kMeshUsingNone, { usingNone });

  std::unordered_set<XXH64_hash_t> dirtyMatHashes = { kDirtyMat };
  std::unordered_set<XXH64_hash_t> outMeshHashes;
  table.collectMeshesUsingMaterials(dirtyMatHashes, outMeshHashes);

  if (!outMeshHashes.count(kMeshUsingDirty)) {
    throw DxvkError("mesh referencing the dirty material must be found");
  }
  if (outMeshHashes.count(kMeshUsingClean)) {
    throw DxvkError("mesh referencing an unrelated material must not be found");
  }
  if (outMeshHashes.count(kMeshUsingNone)) {
    throw DxvkError("mesh with no material must not be found");
  }

  Logger::info("testCollectMeshesUsingMaterials passed");
}

// ─── Test: every original path bound to a shared material is tracked independently ──
// processMaterial registers on every call, including cache hits, so multiple stage paths
// resolving to the same material (e.g. USD instance proxies) each get their own entry.
static void testMultiplePathsForSameMaterialAreAllTracked() {
  AssetReplacements table;
  constexpr XXH64_hash_t kMatHash = 0xABCDEF;

  table.registerMaterialPath("/Instance1/mat", kMatHash);
  table.registerMaterialPath("/Instance2/mat", kMatHash);

  std::unordered_set<XXH64_hash_t> outHashes;
  table.collectMaterialHashesUnderPath("/Instance1/mat", outHashes);
  if (!outHashes.count(kMatHash)) {
    throw DxvkError("deleting one registered path for a shared material must find its hash");
  }

  outHashes.clear();
  table.collectMaterialHashesUnderPath("/Instance2/mat", outHashes);
  if (!outHashes.count(kMatHash)) {
    throw DxvkError("deleting the other registered path for the same shared material must also find its hash");
  }

  Logger::info("testMultiplePathsForSameMaterialAreAllTracked passed");
}

// ─── Test: collectMaterialHashesUnderPath rejects the root and empty path ─────
// Every registered material path starts with '/', so a naive prefix match against "/" or
// "" matches everything - regression for a bug where a stage-level info-only notice
// (path.GetPrimPath() reaching here as the pseudo-root) dirtied every material in the mod.
static void testCollectMaterialHashesUnderPathRejectsRootAndEmpty() {
  AssetReplacements table;
  for (int i = 0; i < 50; i++) {
    table.registerMaterialPath("/Materials/mat" + std::to_string(i), 0x1000 + i);
  }

  std::unordered_set<XXH64_hash_t> outHashes;
  table.collectMaterialHashesUnderPath("/", outHashes);
  if (!outHashes.empty()) {
    throw DxvkError("collectMaterialHashesUnderPath(\"/\") must not match every registered material");
  }

  table.collectMaterialHashesUnderPath("", outHashes);
  if (!outHashes.empty()) {
    throw DxvkError("collectMaterialHashesUnderPath(\"\") must not match every registered material");
  }

  Logger::info("testCollectMaterialHashesUnderPathRejectsRootAndEmpty passed");
}

// ─── Test: an info-only pseudo-root notice must not dirty every material ──────
static void testRootInfoOnlyNoticeDoesNotDirtyAllMaterials() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  for (int i = 0; i < 50; i++) {
    cap.replacements.registerMaterialPath("/Materials/mat" + std::to_string(i), 0x1000 + i);
  }

  classifyChangedPath(pxr::SdfPath::AbsoluteRootPath(), /*isResync=*/false, stage, cap.replacements,
                      cap.dirtyMeshHashes, cap.dirtyMatHashes, cap.dirtyLightHashes,
                      cap.rebuildAllMeshes, cap.rebuildAllMats, cap.rebuildAllLights);

  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("an info-only notice for the pseudo-root must not dirty every registered material");
  }

  Logger::info("testRootInfoOnlyNoticeDoesNotDirtyAllMaterials passed");
}

// ─── Test: collectMeshesUsingMaterials matches by hash, not materialData pointer identity ──
// A partial reload can give one mesh a fresh MaterialData object while a sibling mesh
// keeps an older, content-identical one; both must still be found by hash.
static void testCollectMeshesUsingMaterialsAcrossDivergedMaterialPointers() {
  AssetReplacements table;

  constexpr XXH64_hash_t kMat   = 0xC0FFEE;
  constexpr XXH64_hash_t kMeshA = 0x1111;
  constexpr XXH64_hash_t kMeshB = 0x2222;

  // Same identity hash, different pointers - simulating a diverged MaterialData object.
  auto matPointerA = std::make_shared<MaterialData>();
  auto matPointerB = std::make_shared<MaterialData>();

  AssetReplacement usingA("meshA");
  usingA.type = AssetReplacement::eMesh;
  usingA.materialData = matPointerA;
  usingA.materialPathHash = kMat;
  table.set<AssetReplacement::eMesh>(kMeshA, { usingA });

  AssetReplacement usingB("meshB");
  usingB.type = AssetReplacement::eMesh;
  usingB.materialData = matPointerB;
  usingB.materialPathHash = kMat;
  table.set<AssetReplacement::eMesh>(kMeshB, { usingB });

  std::unordered_set<XXH64_hash_t> dirtyMatHashes = { kMat };
  std::unordered_set<XXH64_hash_t> outMeshHashes;
  table.collectMeshesUsingMaterials(dirtyMatHashes, outMeshHashes);

  if (!outMeshHashes.count(kMeshA)) {
    throw DxvkError("mesh referencing the dirty material via its own materialData object must be found");
  }
  if (!outMeshHashes.count(kMeshB)) {
    throw DxvkError("mesh referencing the same material via a diverged materialData pointer must still be found");
  }

  Logger::info("testCollectMeshesUsingMaterialsAcrossDivergedMaterialPointers passed");
}

// ─── Test: adding a new mesh prim dirties exactly that mesh hash ──────────────
static void testAddMeshPrim() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode"));
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);

  // Author a new mesh prim — this fires UsdObjectsChanged synchronously.
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567"));

  const XXH64_hash_t expected = hashFromHexName("mesh_", "mesh_DEADBEEF01234567");
  if (!cap.dirtyMeshHashes.count(expected)) {
    throw DxvkError("new mesh prim should dirty its hash");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("no material should be dirtied");
  }
  if (!cap.dirtyLightHashes.empty()) {
    throw DxvkError("no light should be dirtied");
  }
  if (cap.rebuildAllMeshes) {
    throw DxvkError("targeted change should not force full rebuild");
  }

  Logger::info("testAddMeshPrim passed");
}

// ─── Test: editing a Shader input on a material dirties that material ─────────
// UsdObjectsChanged fires an info-only notice on the Shader prim, not on mat_.
// classifyPath must walk up to mat_ and dirty its hash rather than ignoring
// the deep path.
static void testEditShaderInputDirtiesMat() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/Looks/mat_AABBCCDDEEFF0011/Shader"));
  // Create the attribute before subscribing so the subscription-time resync
  // doesn't dirty the hash (we want to test the info-only path below).
  auto attr = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/Looks/mat_AABBCCDDEEFF0011/Shader"))
                   .CreateAttribute(pxr::TfToken("inputs:diffuse_texture"),
                                    pxr::SdfValueTypeNames->Asset);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();  // ignore anything from stage setup

  // Editing an attribute on Shader fires an info-only notice for that prim.
  attr.Set(pxr::SdfAssetPath("new_texture.dds"));

  const XXH64_hash_t expected = hashFromHexName("mat_", "mat_AABBCCDDEEFF0011");
  if (!cap.dirtyMatHashes.count(expected)) {
    throw DxvkError("shader input edit should dirty the mat_ ancestor");
  }
  if (!cap.dirtyMeshHashes.empty()) {
    throw DxvkError("no mesh should be dirtied");
  }
  if (cap.rebuildAllMats) {
    throw DxvkError("targeted change should not force section rebuild");
  }

  Logger::info("testEditShaderInputDirtiesMat passed");
}

// ─── Test: a Material prim living outside /RootNode is still classified ───────
static void testExternalMaterialIsClassified() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/Materials/woodMat"), pxr::TfToken("Material"));
  auto attr = stage->DefinePrim(pxr::SdfPath("/Materials/woodMat/Shader"))
                   .CreateAttribute(pxr::TfToken("inputs:diffuse_texture"),
                                    pxr::SdfValueTypeNames->Asset);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  attr.Set(pxr::SdfAssetPath("new_texture.dds"));

  if (cap.dirtyMatHashes.size() != 1) {
    throw DxvkError("external material edit should dirty exactly one hash");
  }
  if (!cap.dirtyMeshHashes.empty()) {
    throw DxvkError("no mesh should be dirtied directly");
  }
  if (cap.rebuildAllMats) {
    throw DxvkError("a targeted external material change should not force a full rebuild");
  }

  // The hash must be stable across edits.
  const XXH64_hash_t firstHash = *cap.dirtyMatHashes.begin();
  cap.reset();
  attr.Set(pxr::SdfAssetPath("another_texture.dds"));
  if (!cap.dirtyMatHashes.count(firstHash)) {
    throw DxvkError("the same external material must hash consistently across edits");
  }

  Logger::info("testExternalMaterialIsClassified passed");
}

// ─── Test: a material nested inside a mesh's own subtree is still classified ──
static void testMaterialNestedUnderMeshIsClassified() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567/localMat"),
                     pxr::TfToken("Material"));
  auto attr = stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567/localMat/Shader"))
                   .CreateAttribute(pxr::TfToken("inputs:diffuse_texture"),
                                    pxr::SdfValueTypeNames->Asset);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  attr.Set(pxr::SdfAssetPath("new_texture.dds"));

  const XXH64_hash_t expectedMesh = hashFromHexName("mesh_", "mesh_DEADBEEF01234567");
  if (!cap.dirtyMeshHashes.count(expectedMesh)) {
    throw DxvkError("owning mesh should still be dirtied as usual");
  }
  if (cap.dirtyMatHashes.size() != 1) {
    throw DxvkError("material nested under a mesh's own subtree should also be classified");
  }

  Logger::info("testMaterialNestedUnderMeshIsClassified passed");
}

// ─── Test: a deleted external material is still classified via the material path index ──
// GetPrimAtPath returns invalid for a deleted prim, so classification falls back to the
// path index (simulated here via registerMaterialPath, since there's no full USD walk).
static void testDeletedExternalMaterialIsClassified() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/Materials/woodMat"), pxr::TfToken("Material"));
  stage->DefinePrim(pxr::SdfPath("/Materials/woodMat/Shader"))
       .CreateAttribute(pxr::TfToken("inputs:diffuse_texture"), pxr::SdfValueTypeNames->Asset);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  constexpr XXH64_hash_t kMatHash = 0xC0FFEE;
  cap.replacements.registerMaterialPath("/Materials/woodMat", kMatHash);

  stage->RemovePrim(pxr::SdfPath("/Materials/woodMat"));

  if (!cap.dirtyMatHashes.count(kMatHash)) {
    throw DxvkError("deleting a registered external material should dirty its previously-known hash");
  }

  Logger::info("testDeletedExternalMaterialIsClassified passed");
}

// ─── Test: deleting an ancestor of a registered material also classifies it ──────
// A subtree deletion resyncs the ancestor's path, not the nested material's own path, so
// the path-index fallback must match by prefix, not just exact path.
static void testDeletedMaterialAncestorIsClassified() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/Materials/group/woodMat"), pxr::TfToken("Material"));
  stage->DefinePrim(pxr::SdfPath("/Materials/group/woodMat/Shader"))
       .CreateAttribute(pxr::TfToken("inputs:diffuse_texture"), pxr::SdfValueTypeNames->Asset);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  constexpr XXH64_hash_t kMatHash = 0xBEEF00;
  cap.replacements.registerMaterialPath("/Materials/group/woodMat", kMatHash);

  stage->RemovePrim(pxr::SdfPath("/Materials/group"));

  if (!cap.dirtyMatHashes.count(kMatHash)) {
    throw DxvkError("deleting an ancestor of a registered material should still dirty its hash");
  }

  Logger::info("testDeletedMaterialAncestorIsClassified passed");
}

// ─── Test: when /RootNode/Looks is newly created, rebuildAllMats is set ───────
// USD reports the section folder itself rather than individual children when
// the section didn't exist before.
static void testNewLooksSectionTriggersRebuildAll() {
  // Start with a stage that has no /RootNode/Looks.
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // Creating /RootNode/Looks for the first time — USD reports /RootNode/Looks
  // as the resync root, not any individual mat_ child.
  stage->DefinePrim(pxr::SdfPath("/RootNode/Looks"));
  stage->DefinePrim(pxr::SdfPath("/RootNode/Looks/mat_1234567890ABCDEF"));

  if (!cap.rebuildAllMats) {
    throw DxvkError("creating the Looks section should trigger full mat rebuild");
  }
  if (cap.rebuildAllMeshes) {
    throw DxvkError("mesh section should be unaffected");
  }

  Logger::info("testNewLooksSectionTriggersRebuildAll passed");
}

// ─── Test: info-only change on the section prim itself does not rebuild ───────
// An attribute edit directly on /RootNode/Looks (unusual but possible) must not
// trigger a full material rebuild — only structural changes (isResync) should.
static void testAttributeOnSectionPrimIsIgnored() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  auto looksAttr = stage->DefinePrim(pxr::SdfPath("/RootNode/Looks"))
                        .CreateAttribute(pxr::TfToken("customData"),
                                         pxr::SdfValueTypeNames->String);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // Editing an attribute directly on /RootNode/Looks.
  looksAttr.Set(std::string("some_value"));

  if (cap.rebuildAllMats) {
    throw DxvkError("attribute on section prim must not trigger full rebuild");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("no mat_ hashes should be dirtied");
  }

  Logger::info("testAttributeOnSectionPrimIsIgnored passed");
}

// ─── Test: deleting a mesh prim dirties exactly that mesh hash ───────────────
static void testDeleteMeshPrimDirtiesHash() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  stage->RemovePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567"));

  const XXH64_hash_t expected = hashFromHexName("mesh_", "mesh_DEADBEEF01234567");
  if (!cap.dirtyMeshHashes.count(expected)) {
    throw DxvkError("deleted mesh prim should dirty its hash");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("no material should be dirtied");
  }

  Logger::info("testDeleteMeshPrimDirtiesHash passed");
}

// ─── Test: adding a light prim dirties its light hash ────────────────────────
static void testAddLightPrimDirtiesHash() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/lights"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);

  stage->DefinePrim(pxr::SdfPath("/RootNode/lights/light_AABB1122CCDD3344"));

  const XXH64_hash_t expected = hashFromHexName("light_", "light_AABB1122CCDD3344");
  if (!cap.dirtyLightHashes.count(expected)) {
    throw DxvkError("new light prim should dirty its hash");
  }
  if (!cap.dirtyMeshHashes.empty()) {
    throw DxvkError("no mesh should be dirtied");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("no material should be dirtied");
  }

  Logger::info("testAddLightPrimDirtiesHash passed");
}

// ─── Test: legacy sphereLight_ naming is recognised ──────────────────────────
static void testLegacySphereLightNaming() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/lights"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);

  stage->DefinePrim(pxr::SdfPath("/RootNode/lights/sphereLight_AABB1122CCDD3344"));

  const XXH64_hash_t expected = hashFromHexName("sphereLight_", "sphereLight_AABB1122CCDD3344");
  if (!cap.dirtyLightHashes.count(expected)) {
    throw DxvkError("legacy sphereLight_ name should dirty its hash");
  }

  Logger::info("testLegacySphereLightNaming passed");
}

// ─── Test: editing a GeomSubset sub-prim dirties the parent mesh ──────────────
// Verifies the walk-up in classifyPath reaches the mesh_ ancestor from a deep path.
static void testEditGeomSubsetDirtiesMesh() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567/SubMesh"));
  auto attr = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567/SubMesh"))
                   .CreateAttribute(pxr::TfToken("materialBinding"),
                                    pxr::SdfValueTypeNames->String);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  attr.Set(std::string("mat_ABC"));

  const XXH64_hash_t expected = hashFromHexName("mesh_", "mesh_DEADBEEF01234567");
  if (!cap.dirtyMeshHashes.count(expected)) {
    throw DxvkError("GeomSubset attribute edit should dirty parent mesh");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("no material should be dirtied");
  }

  Logger::info("testEditGeomSubsetDirtiesMesh passed");
}

// ─── Test: simultaneous mesh and material edits dirty both independently ──────
static void testMultipleChangesInOneNotice() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  auto meshAttr = stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_1111111111111111"))
                       .CreateAttribute(pxr::TfToken("prop"), pxr::SdfValueTypeNames->Int);
  auto matAttr  = stage->DefinePrim(pxr::SdfPath("/RootNode/Looks/mat_2222222222222222"))
                       .CreateAttribute(pxr::TfToken("prop"), pxr::SdfValueTypeNames->Int);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // Author both changes; USD coalesces them into one notice delivery.
  {
    pxr::SdfChangeBlock block;
    meshAttr.Set(1);
    matAttr.Set(2);
  }

  const XXH64_hash_t meshHash = hashFromHexName("mesh_", "mesh_1111111111111111");
  const XXH64_hash_t matHash  = hashFromHexName("mat_",  "mat_2222222222222222");
  if (!cap.dirtyMeshHashes.count(meshHash)) {
    throw DxvkError("mesh change should be captured");
  }
  if (!cap.dirtyMatHashes.count(matHash)) {
    throw DxvkError("material change should be captured");
  }

  Logger::info("testMultipleChangesInOneNotice passed");
}

// ─── Test: /RootNode resync triggers full rebuild of all sections ─────────────
static void testRootNodeResyncTriggersFullRebuild() {
  // Stage starts completely empty — no /RootNode yet.
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);

  // Adding /RootNode from scratch — USD reports /RootNode as the resync root.
  stage->DefinePrim(pxr::SdfPath("/RootNode"));
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567"));
  stage->DefinePrim(pxr::SdfPath("/RootNode/Looks/mat_AABBCCDDEEFF0011"));

  // /RootNode itself or its direct section children should trigger full rebuild.
  const bool fullRebuild = cap.rebuildAllMeshes || cap.rebuildAllMats || cap.rebuildAllLights;
  if (!fullRebuild) {
    throw DxvkError("/RootNode resync should trigger at least one full section rebuild");
  }

  Logger::info("testRootNodeResyncTriggersFullRebuild passed");
}

// ─── Test: deleting a mat_-named material still dirties its hash ──────────────
// tryClassifyExternalMaterial needs a live prim to check its type, so it can't identify
// a deleted material by path alone - only the name-based /RootNode/Looks fast path can.
static void testDeleteMatPrimDirtiesHash() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/Looks/mat_1122334455667788"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  stage->RemovePrim(pxr::SdfPath("/RootNode/Looks/mat_1122334455667788"));

  const XXH64_hash_t expected = hashFromHexName("mat_", "mat_1122334455667788");
  if (!cap.dirtyMatHashes.count(expected)) {
    throw DxvkError("deleting a mat_-named material should still dirty its hash");
  }

  Logger::info("testDeleteMatPrimDirtiesHash passed");
}

// ─── Test: bare /RootNode resync (no section children yet) forces a full rebuild ──
static void testBareRootNodeResyncTriggersFullRebuild() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);

  stage->DefinePrim(pxr::SdfPath("/RootNode"));

  if (!(cap.rebuildAllMeshes && cap.rebuildAllMats && cap.rebuildAllLights)) {
    throw DxvkError("/RootNode resync with no children yet should force a full rebuild of all sections");
  }

  Logger::info("testBareRootNodeResyncTriggersFullRebuild passed");
}

// ─── Test: a resync entirely outside /RootNode is ignored, not a full rebuild ─
static void testUnrelatedResyncOutsideRootNodeIsIgnored() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/mesh_DEADBEEF01234567"));

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // A resync of a prim with no relation to /RootNode - e.g. an editor camera.
  stage->DefinePrim(pxr::SdfPath("/camera1"));

  if (!(cap.dirtyMeshHashes.empty() && cap.dirtyMatHashes.empty() && cap.dirtyLightHashes.empty())) {
    throw DxvkError("unrelated prim outside /RootNode must not dirty anything");
  }
  if (cap.rebuildAllMeshes || cap.rebuildAllMats || cap.rebuildAllLights) {
    throw DxvkError("unrelated resync outside /RootNode must not force a full rebuild");
  }

  Logger::info("testUnrelatedResyncOutsideRootNodeIsIgnored passed");
}

// ─── Test: paths outside known sections are silently ignored ─────────────────
static void testPathOutsideKnownSectionsIsIgnored() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  auto attr = stage->DefinePrim(pxr::SdfPath("/RootNode/SomeOtherSection/prim_ABCD"))
                   .CreateAttribute(pxr::TfToken("prop"), pxr::SdfValueTypeNames->Int);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  attr.Set(42);

  if (!cap.dirtyMeshHashes.empty()) {
    throw DxvkError("unknown section should not dirty any mesh");
  }
  if (!cap.dirtyMatHashes.empty()) {
    throw DxvkError("unknown section should not dirty any material");
  }
  if (!cap.dirtyLightHashes.empty()) {
    throw DxvkError("unknown section should not dirty any light");
  }
  if (cap.rebuildAllMeshes || cap.rebuildAllMats || cap.rebuildAllLights) {
    throw DxvkError("unknown section should not trigger any full rebuild");
  }

  Logger::info("testPathOutsideKnownSectionsIsIgnored passed");
}

// ─── Test: a mesh_-section prim with an unrecognized name is safely ignored ───
// A prim under /RootNode/meshes that doesn't match mesh_<HEX> naming and isn't a
// Material must not dirty anything (only log a warning).
static void testUnrecognizedPrimNameIsIgnored() {
  auto stage = pxr::UsdStage::CreateInMemory("test.usda");
  auto attr = stage->DefinePrim(pxr::SdfPath("/RootNode/meshes/notAMesh"))
                   .CreateAttribute(pxr::TfToken("prop"), pxr::SdfValueTypeNames->Int);

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  attr.Set(42);

  if (!cap.dirtyMeshHashes.empty() || cap.rebuildAllMeshes) {
    throw DxvkError("unrecognized prim name under meshes must not dirty anything");
  }

  Logger::info("testUnrecognizedPrimNameIsIgnored passed");
}

// ─── Test: changing a prim in a sublayer dirties the correct hash ─────────────
// Sublayers are common in RTX Remix mods (e.g. a shared materials.usda).
// USD fires UsdObjectsChanged with stage-level prim paths regardless of which
// layer the opinion lives in, so classifyChangedPath should handle this
// transparently.
static void testSublayerChangeDirtiesCorrectHash() {
  namespace fs = std::filesystem;
  const auto tempDir      = fs::temp_directory_path();
  const std::string rootPath = (tempDir / "test_hot_reload_root.usda").string();
  const std::string subPath  = (tempDir / "test_hot_reload_sub.usda").string();

  // Root file references the sublayer by relative name (both in the same dir).
  {
    std::ofstream f(rootPath);
    f << "#usda 1.0\n(\n    subLayers = [\n        @test_hot_reload_sub.usda@\n    ]\n)\n";
  }
  // Initial sublayer: one existing material.
  {
    std::ofstream f(subPath);
    f << R"(#usda 1.0
def "RootNode" {
    def "Looks" {
        def Material "mat_AABBCCDDEEFF0011" {
        }
    }
}
)";
  }

  auto stage = pxr::UsdStage::Open(rootPath, pxr::UsdStage::LoadAll);
  if (!stage) {
    throw DxvkError("stage should open with sublayer");
  }

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // Updated sublayer: edit the existing material and add a new one.
  {
    std::ofstream f(subPath);
    f << R"(#usda 1.0
def "RootNode" {
    def "Looks" {
        def Material "mat_AABBCCDDEEFF0011" {
            string diffuse = "new_texture.dds"
        }
        def Material "mat_1122334455667788" {
        }
    }
}
)";
  }
  bumpMtimeForward(subPath);
  stage->Reload();

  const XXH64_hash_t existing = hashFromHexName("mat_", "mat_AABBCCDDEEFF0011");
  const XXH64_hash_t added    = hashFromHexName("mat_", "mat_1122334455667788");

  // /RootNode/Looks already existed before the reload, so both changes must classify to
  // their specific hashes rather than degrading to a full-section rebuild.
  if (!cap.dirtyMatHashes.count(existing)) {
    throw DxvkError("modified material in sublayer should be dirtied");
  }
  if (!cap.dirtyMatHashes.count(added)) {
    throw DxvkError("new material in sublayer should be dirtied");
  }
  if (cap.rebuildAllMats) {
    throw DxvkError("sublayer change with an already-existing Looks section must not force a full rebuild");
  }
  if (!cap.dirtyMeshHashes.empty() || cap.rebuildAllMeshes) {
    throw DxvkError("mesh section should be unaffected by material-only sublayer change");
  }

  fs::remove(rootPath);
  fs::remove(subPath);
  Logger::info("testSublayerChangeDirtiesCorrectHash passed");
}

// ─── Test: watchDependencies=false only triggers on the root mod file ─────────
// When watchDependencies is off, sublayer .usda files and non-USD files in the
// same watched directory must not trigger a reload.
static void testWatchDependenciesOff() {
  namespace fs = std::filesystem;
  const fs::path root    = fs::temp_directory_path() / "mod.usda";
  const fs::path sublayer = fs::temp_directory_path() / "materials.usda";
  const fs::path texture  = fs::temp_directory_path() / "diffuse.png";
  const std::wstring rootKey = normalizedPathKey(root);

  if (!usdShouldTriggerReload(root, rootKey, /*watchDeps=*/false)) {
    throw DxvkError("root must trigger");
  }
  if (usdShouldTriggerReload(sublayer, rootKey, /*watchDeps=*/false)) {
    throw DxvkError("sublayer must NOT trigger");
  }
  if (usdShouldTriggerReload(texture, rootKey, /*watchDeps=*/false)) {
    throw DxvkError("non-USD must NOT trigger");
  }

  // Must still match with differing case or path separators (e.g. an env var vs.
  // FileWatch's canonicalized report).
  std::wstring upperStr = root.wstring();
  std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), ::towupper);
  const fs::path differentCase(upperStr);
  if (differentCase.native() == root.native()) {
    throw DxvkError("test setup: case must actually differ");
  }
  if (!usdShouldTriggerReload(differentCase, rootKey, /*watchDeps=*/false)) {
    throw DxvkError("differing case must still match the mod's own root file");
  }

  std::wstring slashStr = root.wstring();
  std::replace(slashStr.begin(), slashStr.end(), L'\\', L'/');
  const fs::path differentSeparators(slashStr);
  if (!usdShouldTriggerReload(root, normalizedPathKey(differentSeparators), /*watchDeps=*/false)) {
    throw DxvkError("differing path separators must still match the mod's own root file");
  }

  Logger::info("testWatchDependenciesOff passed");
}

// ─── Test: watchDependencies=true triggers on root and sublayer USD files ─────
// With watchDependencies on, any .usda/.usdc/.usd change in a watched directory
// (not just the root file) should trigger a reload. Non-USD files are still ignored.
static void testWatchDependenciesOn() {
  namespace fs = std::filesystem;
  const fs::path root     = fs::temp_directory_path() / "mod.usda";
  const fs::path sublayer = fs::temp_directory_path() / "materials.usda";
  const fs::path other    = fs::temp_directory_path() / "unrelated.usdc";
  const fs::path texture  = fs::temp_directory_path() / "diffuse.png";
  const std::wstring rootKey = normalizedPathKey(root);

  if (!usdShouldTriggerReload(root, rootKey, /*watchDeps=*/true)) {
    throw DxvkError("root must trigger");
  }
  if (!usdShouldTriggerReload(sublayer, rootKey, /*watchDeps=*/true)) {
    throw DxvkError("sublayer must trigger");
  }
  if (!usdShouldTriggerReload(other, rootKey, /*watchDeps=*/true)) {
    throw DxvkError("other .usdc must trigger");
  }
  if (usdShouldTriggerReload(texture, rootKey, /*watchDeps=*/true)) {
    throw DxvkError("non-USD must NOT trigger");
  }

  Logger::info("testWatchDependenciesOn passed");
}

// ─── Test: watchDependencies scopes reloads to this mod's actual dependencies ─
// A change to a file that isn't in the mod's usedLayerPaths set (e.g. another
// mod's sublayer, sharing the same watched directory) must not trigger a reload
// once dependency data is available. Before any dependency data exists (nullptr),
// behavior falls back to the old blanket trigger rather than missing a real one.
static void testWatchDependenciesScopedToUsedLayers() {
  namespace fs = std::filesystem;
  const fs::path root         = fs::temp_directory_path() / "mod_scoped.usda";
  const fs::path ownDep       = fs::temp_directory_path() / "own_sublayer.usda";
  const fs::path otherModDep  = fs::temp_directory_path() / "other_mod_sublayer.usda";

  // normalizedPathKey() canonicalizes, which requires the file to exist.
  for (const auto& p : { root, ownDep, otherModDep }) {
    std::ofstream(p) << "#usda 1.0\n";
  }

  const std::unordered_set<std::wstring> usedLayers = {
    normalizedPathKey(root), normalizedPathKey(ownDep)
  };
  const std::wstring rootKey = normalizedPathKey(root);

  if (!usdShouldTriggerReload(root, rootKey, /*watchDeps=*/true, &usedLayers)) {
    throw DxvkError("mod's own root file always triggers");
  }
  if (!usdShouldTriggerReload(ownDep, rootKey, /*watchDeps=*/true, &usedLayers)) {
    throw DxvkError("this mod's own dependency must trigger");
  }
  if (usdShouldTriggerReload(otherModDep, rootKey, /*watchDeps=*/true, &usedLayers)) {
    throw DxvkError("another mod's sublayer must NOT trigger once scoped");
  }
  if (!usdShouldTriggerReload(otherModDep, rootKey, /*watchDeps=*/true, nullptr)) {
    throw DxvkError("no dependency data yet must fall back to the broad trigger");
  }

  fs::remove(root);
  fs::remove(ownDep);
  fs::remove(otherModDep);
  Logger::info("testWatchDependenciesScopedToUsedLayers passed");
}

// ─── Test: file-based reload with real file change ───────────────────────────
// Writes a USD file, opens a stage, modifies the file, calls Reload(),
// and verifies that the updated mat_ hash is dirtied.
static void testFileReloadDirtiesChangedPrims() {
  const std::string primName = "mat_CAFEBABE12345678";
  const std::string initialContent = R"(#usda 1.0
def "RootNode" {
    def "Looks" {
    }
}
)";
  const std::string updatedContent = R"(#usda 1.0
def "RootNode" {
    def "Looks" {
        def Material ")" + primName + R"(" {
        }
    }
}
)";

  const std::string filePath = writeTempUsd(initialContent);

  auto stage = pxr::UsdStage::Open(filePath, pxr::UsdStage::LoadAll);
  if (!stage) {
    throw DxvkError("stage should open cleanly");
  }

  ChangeCapture cap;
  pxr::TfNotice::Register(pxr::TfWeakPtr<ChangeCapture>(&cap),
                           &ChangeCapture::OnObjectsChanged, stage);
  cap.reset();

  // Modify the file on disk and reload.
  writeTempUsd(updatedContent);  // overwrites the temp file
  bumpMtimeForward(filePath);
  stage->Reload();               // UsdObjectsChanged fires synchronously

  // /RootNode/Looks already existed (empty) before the reload, so the new material must
  // classify to its specific hash rather than degrading to a full-section rebuild.
  const XXH64_hash_t expected = hashFromHexName("mat_", primName.c_str());
  if (!cap.dirtyMatHashes.count(expected)) {
    throw DxvkError("new material on disk should be dirtied");
  }
  if (cap.rebuildAllMats) {
    throw DxvkError("reload with an already-existing Looks section must not force a full rebuild");
  }

  std::filesystem::remove(filePath);
  Logger::info("testFileReloadDirtiesChangedPrims passed");
}

} // namespace dxvk

int main() {
  try {
    dxvk::UsdMod::loadUsdPlugins();

    dxvk::testStandaloneMatSurvivesHotReload();
    dxvk::testDeletedMatErasedFromCache();
    dxvk::testOrphanSweepOnPureDeletionReload();
    dxvk::testModifyUndoCyclePreservesStandaloneMat();
    dxvk::testSeedMaterialsFromReusesUnchangedMaterial();
    dxvk::testSeedMaterialsFromExcludesDirtyHashes();
    dxvk::testCollectMeshesUsingMaterials();
    dxvk::testMultiplePathsForSameMaterialAreAllTracked();
    dxvk::testCollectMaterialHashesUnderPathRejectsRootAndEmpty();
    dxvk::testRootInfoOnlyNoticeDoesNotDirtyAllMaterials();
    dxvk::testCollectMeshesUsingMaterialsAcrossDivergedMaterialPointers();
    dxvk::testAddMeshPrim();
    dxvk::testDeleteMeshPrimDirtiesHash();
    dxvk::testEditShaderInputDirtiesMat();
    dxvk::testExternalMaterialIsClassified();
    dxvk::testMaterialNestedUnderMeshIsClassified();
    dxvk::testDeletedExternalMaterialIsClassified();
    dxvk::testDeletedMaterialAncestorIsClassified();
    dxvk::testAddLightPrimDirtiesHash();
    dxvk::testLegacySphereLightNaming();
    dxvk::testEditGeomSubsetDirtiesMesh();
    dxvk::testMultipleChangesInOneNotice();
    dxvk::testNewLooksSectionTriggersRebuildAll();
    dxvk::testAttributeOnSectionPrimIsIgnored();
    dxvk::testRootNodeResyncTriggersFullRebuild();
    dxvk::testDeleteMatPrimDirtiesHash();
    dxvk::testBareRootNodeResyncTriggersFullRebuild();
    dxvk::testUnrelatedResyncOutsideRootNodeIsIgnored();
    dxvk::testPathOutsideKnownSectionsIsIgnored();
    dxvk::testUnrecognizedPrimNameIsIgnored();
    dxvk::testSublayerChangeDirtiesCorrectHash();
    dxvk::testWatchDependenciesOff();
    dxvk::testWatchDependenciesOn();
    dxvk::testWatchDependenciesScopedToUsedLayers();
    dxvk::testFileReloadDirtiesChangedPrims();

    dxvk::Logger::info("All hot-reload tests passed.");
    return 0;
  } catch (const dxvk::DxvkError& e) {
    dxvk::Logger::err(dxvk::str::format("Test failed: ", e.message()));
    return 1;
  } catch (const std::exception& e) {
    dxvk::Logger::err(dxvk::str::format("Test failed: ", e.what()));
    return 1;
  } catch (...) {
    dxvk::Logger::err("Test failed with unknown exception");
    return 1;
  }
}
