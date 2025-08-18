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

#include "rtx_mod_usd.h"
#include "rtx_asset_replacer.h"

#include "dxvk_device.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_utils.h"
#include "rtx_asset_data_manager.h"
#include "rtx_texture_manager.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/stage.h>
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
#include <src/usd-plugins/RemixParticleSystem/ParticleSystemAPI.h>
#include "../../lssusd/usd_include_end.h"
#include "../util/util_watchdog.h"

#include "../../lssusd/game_exporter_common.h"
#include "../../lssusd/game_exporter_paths.h"
#include "../../lssusd/usd_mesh_importer.h"
#include "../../lssusd/usd_common.h"
#include "graph/rtx_graph_usd_parser.h"

#include "rtx_lights_data.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace dxvk {
constexpr uint32_t kMaxU16Indices = 64 * 1024;
const char* const kStatusKey = "remix_replacement_status";


class UsdMod::Impl {
public:
  Impl(UsdMod& owner) 
    : m_owner{owner}
    , m_usdChangeWatchdog([this] { return this->haveFilesChanged(); }, "usd-mod-watchdog")
  {}

  void load(const Rc<DxvkContext>& context);
  void unload();
  bool checkForChanges(const Rc<DxvkContext>& context);

private:
  UsdMod& m_owner;

  struct Args {
    Rc<DxvkContext> context;
    pxr::UsdGeomXformCache& xformCache;

    pxr::UsdPrim& rootPrim;
    std::vector<AssetReplacement>& meshes;
    fast_unordered_cache<uint32_t> pathHashToIndexMap;
  };

  bool haveFilesChanged();

  void processUSD(const Rc<DxvkContext>& context);

  void TEMP_parseSecretReplacementVariants(const fast_unordered_cache<uint32_t>& variants);
  Rc<ManagedTexture> getTexture(const Args& args, const pxr::UsdPrim& shader, const pxr::TfToken& textureToken, bool forcePreload = false) const;
  MaterialData* processMaterial(Args& args, const pxr::UsdPrim& matPrim);
  MaterialData* processMaterialUser(Args& args, const pxr::UsdPrim& prim);
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
    static size_t id;
    ++id;
    return XXH64(&id, sizeof(id), kEmptyHash);
  }

  std::filesystem::file_time_type m_fileModificationTime;
  std::string m_openedFilePath;

  Watchdog<1000> m_usdChangeWatchdog;

  void addReplacementsSync(dxvk::Rc<dxvk::DxvkCommandList> cmdList, XXH64_hash_t hash, std::vector<AssetReplacement>& replacementVec);
  std::unordered_map<dxvk::DxvkCommandList*, std::thread> m_cmdListSyncThreads;
  // Asset replacement vector and hash to add when command list execution is complete
  std::unordered_map<dxvk::DxvkCommandList*, std::unordered_map<XXH64_hash_t, std::vector<AssetReplacement>>> m_meshReplacementsToAdd;
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
}  // namespace

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
  static pxr::SdfAssetPath path;
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

MaterialData* UsdMod::Impl::processMaterial(Args& args, const pxr::UsdPrim& matPrim) {
  ScopedCpuProfileZone();

  static const pxr::TfToken kShaderToken("Shader");
  static const pxr::TfToken kIgnore("inputs:ignore_material");  // Any draw call or replacement using a material with this flag will be skipped by the SceneManager
  static const pxr::TfToken kPreloadTextures("inputs:preload_textures");  // Force textures to be loaded at highest mip
  static const pxr::TfToken kLegacyRayPortalIndexToken("rayPortalIndex");

  std::optional<RtxParticleSystemDesc> particleSystem = processParticleSystem(args, matPrim);

  pxr::UsdPrim shader = matPrim.GetChild(kShaderToken);
  if (!shader.IsValid() || !shader.IsA<pxr::UsdShadeShader>()) {
    auto children = matPrim.GetFilteredChildren(pxr::UsdPrimIsActive);
    for (auto child : children) {
      if (child.IsA<pxr::UsdShadeShader>()) {
        shader = child;
      }
    }
  }

  XXH64_hash_t materialHash = getMaterialHash(matPrim, shader);
  if (materialHash == 0) {
    return nullptr;
  }

  if (!shader.IsValid()) {
    // Special case to handle material overrides which have the particle system API, but no material parameter overrides.
    // This is the case when adding a particle system API to an existing legacy material in game.
    if (particleSystem.has_value()) {
      // In this case just return an empty opaque material.
      return &m_owner.m_replacements->storeObject(materialHash, MaterialData(OpaqueMaterialData::deserialize([](const pxr::UsdPrim& shader, const pxr::TfToken& name) { return TextureRef {}; }, shader), particleSystem));
    }
    return nullptr;
  }

  // Check if the material has already been processed
  MaterialData* materialData;
  if (m_owner.m_replacements->getObject(materialHash, materialData)) {
    return materialData;
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
    static pxr::SdfAssetPath assetPath;
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
    return &m_owner.m_replacements->storeObject(materialHash, MaterialData(OpaqueMaterialData::deserialize(getTextureFunctor, shader), particleSystem, shouldIgnore));
  case RtSurfaceMaterialType::Translucent:
    return &m_owner.m_replacements->storeObject(materialHash, MaterialData(TranslucentMaterialData::deserialize(getTextureFunctor, shader), particleSystem, shouldIgnore));
  case RtSurfaceMaterialType::RayPortal:
    return &m_owner.m_replacements->storeObject(materialHash, MaterialData(RayPortalMaterialData::deserialize(getTextureFunctor, shader), particleSystem));
  default:
    assert(false && "Invalid materialType passed to getTextureFunctor");
  }

  return nullptr;
}

MaterialData* UsdMod::Impl::processMaterialUser(Args& args, const pxr::UsdPrim& prim) {
  auto bindAPI = pxr::UsdShadeMaterialBindingAPI(prim);
  auto boundMaterial = bindAPI.ComputeBoundMaterial();
  if (boundMaterial) {
    return processMaterial(args, boundMaterial.GetPrim());
  }
  return nullptr;
}

void UsdMod::Impl::processPrim(Args& args, const pxr::UsdPrim& prim) {
  ScopedCpuProfileZone();

  const XXH64_hash_t usdOriginHash = getStrongestOpinionatedPathHash(prim);

  MeshReplacement* pTemp;
  if (!m_owner.m_replacements->getObject(usdOriginHash, pTemp)) {
    // First time seeing this mesh, then process it.
    if (!processMesh(prim, args)) {
      return;
    }
  }

  MaterialData* materialData = processMaterialUser(args, prim);

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
    MeshReplacement* pGeometryData;
    if (m_owner.m_replacements->getObject(usdOriginHash, pGeometryData)) {
      AssetReplacement newReplacementMesh(prim.GetPrimPath().GetString(), pGeometryData, materialData, categoryFlags, replacementToObject);
      newReplacementMesh.particleSystem = particleSystem;
      args.meshes.push_back(newReplacementMesh);
    }
  } else {
    for (auto subset : geomSubsets) {
      const XXH64_hash_t usdChildOriginHash = getStrongestOpinionatedPathHash(subset.GetPrim());
      MeshReplacement* childGeometryData;
      if (m_owner.m_replacements->getObject(usdChildOriginHash, childGeometryData)) {
        AssetReplacement newReplacementMesh(prim.GetPrimPath().GetString(), childGeometryData, materialData, categoryFlags, replacementToObject);
        MaterialData* mat = processMaterialUser(args, subset.GetPrim());
        if (mat) {
          newReplacementMesh.materialData = mat;
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
  args.meshes[meshIndex].graphState.emplace(GraphUsdParser::parseGraph(*m_owner.m_replacements, graphPrim, args.pathHashToIndexMap));
}

inline Vector4 toFloat4(const pxr::GfVec4f& v) {
  Vector4 f;
  f.x = v[0];
  f.y = v[1];
  f.z = v[2];
  f.w = v[3];
  return f;
}

inline ParticleBillboardType toBillboardType(const pxr::TfToken& token) {
  static pxr::RemixTokensType tokens;
  if (token == tokens.faceCamera_UpAxisLocked) {
    return FaceCamera_UpAxisLocked;
  }
  if (token == tokens.faceCamera_Position) {
    return FaceCamera_Position;
  }
  if (token == tokens.faceWorldUp) {
    return FaceWorldUp;
  }
  return FaceCamera_Spherical;
}

std::optional<RtxParticleSystemDesc> UsdMod::Impl::processParticleSystem(Args& args, const pxr::UsdPrim& prim) {
  using namespace pxr;

  if (!prim.HasAPI<RemixParticleSystemAPI>()) {
    return std::nullopt;
  }
  RemixParticleSystemAPI particleSystem(prim);

  RtxParticleSystemDesc particleDesc;
  // Helper macro to read an attribute into a float or int field
#define READ_ATTR(Type, attrName, field) \
            if (UsdAttribute attr = particleSystem.GetPrimvarsParticle##attrName##Attr()) { \
                Type result; \
                attr.Get<Type>(&result); \
                particleDesc.field = result; \
            } 
#define READ_ATTR_CONV(Type, attrName, field, conversionFunc) \
            if (UsdAttribute attr = particleSystem.GetPrimvarsParticle##attrName##Attr()) { \
                Type result; \
                attr.Get<Type>(&result); \
                particleDesc.field = conversionFunc(result); \
            } 

    // Read all simulation parameters
  READ_ATTR(float, MinTimeToLive, minTtl);
  READ_ATTR(float, MaxTimeToLive, maxTtl);
  READ_ATTR(float, InitialVelocityFromMotion, initialVelocityFromMotion);
  READ_ATTR(float, InitialVelocityFromNormal, initialVelocityFromNormal);
  READ_ATTR(float, InitialVelocityConeAngleDegrees, initialVelocityConeAngleDegrees);
  READ_ATTR(float, MinSpawnSize, minSpawnSize);
  READ_ATTR(float, MaxSpawnSize, maxSpawnSize);
  READ_ATTR(float, MinSpawnRotationSpeed, minSpawnRotationSpeed);
  READ_ATTR(float, MaxSpawnRotationSpeed, maxSpawnRotationSpeed);
  READ_ATTR(float, MinTargetSize, minTargetSize);
  READ_ATTR(float, MaxTargetSize, maxTargetSize);
  READ_ATTR(float, MinTargetRotationSpeed, minTargetRotationSpeed);
  READ_ATTR(float, MaxTargetRotationSpeed, maxTargetRotationSpeed);
  READ_ATTR(float, GravityForce, gravityForce);
  READ_ATTR(float, MaxSpeed, maxSpeed);
  READ_ATTR(float, TurbulenceFrequency, turbulenceFrequency);
  READ_ATTR(float, TurbulenceForce, turbulenceForce);
  READ_ATTR(float, CollisionThickness, collisionThickness);
  READ_ATTR(float, CollisionRestitution, collisionRestitution);
  READ_ATTR(float, MotionTrailMultiplier, motionTrailMultiplier);
  READ_ATTR(int, MaxNumParticles, maxNumParticles);
  READ_ATTR(bool, UseTurbulence, useTurbulence);
  READ_ATTR(bool, UseSpawnTexcoords, useSpawnTexcoords);
  READ_ATTR(bool, EnableCollisionDetection, enableCollisionDetection);
  READ_ATTR(bool, AlignParticlesToVelocity, alignParticlesToVelocity);
  READ_ATTR(bool, EnableMotionTrail, enableMotionTrail);
  READ_ATTR(bool, HideEmitter, hideEmitter);
  READ_ATTR(float, SpawnRatePerSecond, spawnRate);
  READ_ATTR_CONV(TfToken, BillboardType, billboardType, toBillboardType);
  READ_ATTR_CONV(GfVec4f, MaxSpawnColor, maxSpawnColor, toFloat4);
  READ_ATTR_CONV(GfVec4f, MinSpawnColor, minSpawnColor, toFloat4);
  READ_ATTR_CONV(GfVec4f, MaxTargetColor, maxTargetColor, toFloat4);
  READ_ATTR_CONV(GfVec4f, MinTargetColor, minTargetColor, toFloat4);
  // If this assert fails a new particle system parameter added, please update here.
  assert(RemixParticleSystemAPI::GetSchemaAttributeNames(false).size() == 33);

#undef READ_ATTR
#undef READ_ATTR_CONV

  return particleDesc;
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
          args.meshes[meshInd].instancesToObject = instanceToObjectTransforms;
          auto& instancesToObject = args.meshes[meshInd].instancesToObject;
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
    args.meshes.push_back(AssetReplacement(args.rootPrim.GetPrimPath().GetString()));
    args.meshes[0].includeOriginal = true;
    args.meshes[0].categories = processCategoryFlags(args.rootPrim);
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
  if (args.meshes.size() == 1 && args.meshes[0].includeOriginal) {
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
  if (m_owner.state().progressState == ProgressState::Unloaded) {
    processUSD(context);

    m_usdChangeWatchdog.start();
  }
}

void UsdMod::Impl::unload() {
  if (m_owner.state().progressState == ProgressState::Loaded) {
    m_usdChangeWatchdog.stop();

    m_owner.m_replacements->clear();
    AssetDataManager::get().clearSearchPaths();

    m_owner.setState(ProgressState::Unloaded);
  }
}

bool UsdMod::Impl::haveFilesChanged() {
  if (m_openedFilePath.empty())
    return false;

  fs::file_time_type newModTime;
  if (m_owner.state().progressState == ProgressState::Loaded) {
    newModTime = fs::last_write_time(fs::path(m_openedFilePath));
  } else {
    bool fileFound = false;
    const auto replacementsUsdPath = fs::path(m_openedFilePath);
    fileFound = fs::exists(replacementsUsdPath);
    if (fs::exists(replacementsUsdPath)) {
      newModTime = fs::last_write_time(replacementsUsdPath);
    } else {
      m_owner.setState(ProgressState::Unloaded);
      return false;
    }
  }
  return (newModTime > m_fileModificationTime);
}

bool UsdMod::Impl::checkForChanges(const Rc<DxvkContext>& context) {
  if (m_usdChangeWatchdog.hasSignaled()) {
    unload();
    load(context);
    return true;
  }

  return false;
}

void UsdMod::Impl::processUSD(const Rc<DxvkContext>& context) {
  ScopedCpuProfileZone();
  std::string replacementsUsdPath(m_owner.m_filePath.string());

  // Open the USD

  m_owner.setState(ProgressState::OpeningUSD);

  pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(replacementsUsdPath, pxr::UsdStage::LoadAll);

  if (!stage) {
    Logger::err(str::format("USD mod file failed parsing: ", std::filesystem::weakly_canonical(replacementsUsdPath).string()));
    m_openedFilePath.clear();
    m_fileModificationTime = fs::file_time_type();
    m_owner.setState(ProgressState::Unloaded);
    return;
  }

  std::filesystem::path modBaseDirectory = std::filesystem::path(replacementsUsdPath).remove_filename();
  m_openedFilePath = replacementsUsdPath;

  // Iterate sublayers in the strength order, resolve the base paths and
  // populate asset manager search paths.
  auto sublayers = stage->GetRootLayer()->GetSubLayerPaths();
  for (size_t i = 0, s = sublayers.size(); i < s; i++) {
    const std::string& identifier = sublayers[i];
    auto layerBasePath = std::filesystem::path(identifier).remove_filename();
    auto fullLayerBasePath = modBaseDirectory / layerBasePath;
    AssetDataManager::get().addSearchPath(i, fullLayerBasePath);
  }

  // Add stage's base path last.
  AssetDataManager::get().addSearchPath(sublayers.size(), modBaseDirectory);

  m_fileModificationTime = fs::last_write_time(fs::path(m_openedFilePath));
  pxr::UsdGeomXformCache xformCache;

  pxr::VtDictionary layerData = stage->GetRootLayer()->GetCustomLayerData();
  if (layerData.empty()) {
    m_owner.m_status = "Layer Data Missing";
  } else {
    const PXR_NS::VtValue* vtExportStatus = layerData.GetValueAtPath(kStatusKey);
    if (vtExportStatus && !vtExportStatus->IsEmpty()) {
      m_owner.m_status = vtExportStatus->Get<std::string>();
    } else {
      m_owner.m_status = "Status Missing";
    }
  }

  // Process Materials

  m_owner.setStateWithCount(ProgressState::ProcessingMaterials, 0);

  pxr::UsdPrim materialRoot = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/Looks"));
  if (materialRoot.IsValid()) {
    const auto children = materialRoot.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentMaterialCount{ 0U };
    std::vector<AssetReplacement> placeholder;

    Args args = {context, xformCache, materialRoot, placeholder};

    for (pxr::UsdPrim materialPrim : children) {
      processMaterial(args, materialPrim);

      // Note: Update the state progress only every 16 materials to reduce the number of atomic writes.
      if ((++currentMaterialCount & 0b1111u) == 0u) {
        m_owner.setStateWithCount(ProgressState::ProcessingMaterials, currentMaterialCount);
      }
    }
  }

  // Process Meshes

  m_owner.setStateWithCount(ProgressState::ProcessingMeshes, 0);

  fast_unordered_cache<uint32_t> variantCounts;
  pxr::UsdPrim meshes = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/meshes"));
  if (meshes.IsValid()) {
    const auto children = meshes.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentMeshCount{ 0U };

    for (pxr::UsdPrim child : children) {
      const auto hash = getModelHash(child);

      if (hash != 0) {
        std::vector<AssetReplacement> replacementVec;
        Args args = {context, xformCache, child, replacementVec};

        if (processReplacement(args)) {
          variantCounts[hash]++;

          addReplacementsSync(args.context->getCommandList(), hash, replacementVec);
        }
      }

      // Note: Update the state progress only every 16 meshes to reduce the number of atomic writes.
      if ((++currentMeshCount & 0b1111u) == 0u) {
        m_owner.setStateWithCount(ProgressState::ProcessingMeshes, currentMeshCount);
      }
    }
  }

  // Process Secret Meshes

  // TODO: enter "secrets" section of USD as exported by Kit app
  TEMP_parseSecretReplacementVariants(variantCounts);
  for (auto& [hash, secretReplacements] : m_owner.m_replacements->secretReplacements()) {
    for (auto& secretReplacement : secretReplacements) {
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

      if (processReplacement(args)) {
        addReplacementsSync(args.context->getCommandList(), variantHash, replacementVec);
      }
    }
  }

  // Process Lights

  m_owner.setStateWithCount(ProgressState::ProcessingLights, 0);

  pxr::UsdPrim lights = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/lights"));
  if (lights.IsValid()) {
    const auto children = lights.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::uint32_t currentLightCount{ 0U };

    for (pxr::UsdPrim child : children) {
      const auto hash = getLightHash(child);

      if (hash != 0) {
        std::vector<AssetReplacement> replacementVec;
        Args args = {context, xformCache, child, replacementVec};

        if (processReplacement(args)) {
          m_owner.m_replacements->set<AssetReplacement::eLight>(hash, std::move(replacementVec));
        }
      }

      // Note: Update the state progress only every 16 lights to reduce the number of atomic writes.
      if ((++currentLightCount & 0b1111u) == 0u) {
        m_owner.setStateWithCount(ProgressState::ProcessingLights, currentLightCount);
      }
    }
  }

  // flush entire cache, kinda a sledgehammer
  context->emitMemoryBarrier(0,
    VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR);

  m_owner.setState(ProgressState::Loaded);
}

void UsdMod::Impl::TEMP_parseSecretReplacementVariants(const fast_unordered_cache<uint32_t>& variantCounts) {
  auto lookupCount = [&variantCounts](XXH64_hash_t hash) -> auto {
    // NOTE: If there's no default replacement make sure secret variants are not default.
    return variantCounts.count(hash) ? variantCounts.at(hash) : 1u;
  };

  static constexpr XXH64_hash_t kStorageCubeHash = 0xc728cfe75526c741;
  uint32_t numVariants = lookupCount(kStorageCubeHash);
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Ice","",
    0x60ead40e2269b3c5,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Ice.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Lens","",
    0xa8e871f4ebc52eab,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Lens.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Camera","",
    0xd150bdeff3f0299a,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeCamera_A01_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Digital Skull","",
    0xb26578451f75c11a,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeDigital_A02_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Wheatly","",
    0xc270f63a956c0c71,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A01_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Voyager","",
    0xaaaf0cbd8c8204cd,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A02_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Iso-Black-Mesa","",
    0x2f9fe4ce23a83bc2,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeIsogrid_A03_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","RTX","",
    0xe361f386c03400f3,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_RTX_CompanionCube_A1_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Roll Cage","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_RollCage.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Health Pack","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Healthpack.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kStorageCubeHash, SecretReplacement{
    "Storage Cubes","Space","",
    0x0,
    kStorageCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Space.usd",
    true,
    true,
    numVariants++});

  static constexpr XXH64_hash_t kCompanionCubeHash = 0x6ef165bb7e0b8512;
  numVariants = lookupCount(kCompanionCubeHash);
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Pillow","",
    0xc901411d90916a58,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Pillow_A.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Ceramic","",
    0x3495c5b9d210daa1,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Ceramic.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Wood","",
    0x5e50cb7c64375acc,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_Wood.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Digital","",
    0xf2bda31c09fc42f6,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCubeDigital_A01_01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Steampunk","",
    0x0,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_SteamPunk_A01.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
    "Companion Cubes","Arts and Crafts","",
    0x0,
    kCompanionCubeHash,
    "./SubUSDs/SM_Prop_CompanionCube_ArtsAndCrafts.usd",
    true,
    true,
    numVariants++});
  m_owner.m_replacements->storeObject(kCompanionCubeHash, SecretReplacement{
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

void UsdMod::Impl::addReplacementsSync(dxvk::Rc<dxvk::DxvkCommandList> cmdList, XXH64_hash_t hash, std::vector<AssetReplacement>& replacementVec) {
  m_meshReplacementsToAdd[cmdList.ptr()][hash] = std::move(replacementVec);

  // If the sync thread for this command list hasn't been created then create it now
  if (!m_cmdListSyncThreads[cmdList.ptr()].joinable()) {
    m_cmdListSyncThreads[cmdList.ptr()] = std::thread([this, cmdList]() {
      // Base on Vulkan Document: https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-fences
      // Host access to each member of pFences must be externally synchronized
      // So, we must wait for the VkFence synchronization finished in queue submission thread. If we do synchronize here, it will cause VkFence multiple thread error.
      {
        constexpr uint64_t initialSignalValue = 0;
        constexpr uint64_t waitSignalValue = 1;
        Rc<sync::Fence> replacementSyncSignal = new sync::Fence(initialSignalValue);

        cmdList->queueSignal(replacementSyncSignal, waitSignalValue);
        // Note: May be possible that the command list's signal tracker can be reset before this wait call or before the signal is actually signaled, which may cause this
        // wait to never complete. Unsure if this happens in practice, but previously a bug existed where a ref-counted pointer to the replacement signal wasn't used
        // which resulted in a crash due to the object being freed before getting to this wait call, and the only way it would've been freed is if the command list's signal
        // tracker was reset. it is possible that most/all the times this reset happens the signal has been properly signaled though and this may not be a concern, but
        // something to watch out for regardless.
        replacementSyncSignal->wait(waitSignalValue);
      }

      // Add the replacements vector to the collection of replacements for this hash
      for (auto it : m_meshReplacementsToAdd[cmdList.ptr()]) {
        m_owner.m_replacements->set<AssetReplacement::eMesh>(it.first, std::move(it.second));
      }
      m_meshReplacementsToAdd[cmdList.ptr()].clear();
    });
  }
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
  DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
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

  for (const lss::UsdMeshImporter::SubMesh& submesh : processedMesh->GetSubMeshes()) {
    if (submesh.GetNumIndices() == 0) {
      Logger::err(str::format("Prim: ", submesh.prim.GetPath().GetString(), ", does not have indices, this is currently a requirement."));
      continue;
    }

    XXH64_hash_t usdOriginHash = getStrongestOpinionatedPathHash(submesh.prim);
    MeshReplacement* childGeometryData;
    if (!m_owner.m_replacements->getObject(usdOriginHash, childGeometryData)) {
      MeshReplacement& newReplacement = m_owner.m_replacements->storeObject(usdOriginHash, MeshReplacement(replacement));
      RasterGeometry& newGeomData = newReplacement.data;

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

bool UsdMod::checkForChanges(const Rc<DxvkContext>& context) {
  return m_impl->checkForChanges(context);
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

} // namespace dxvk
