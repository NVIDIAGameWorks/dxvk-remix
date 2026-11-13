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
#define _CRT_SECURE_NO_WARNINGS

#pragma warning(disable : 4244 4305)

#include <shlwapi.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/base/plug/registry.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/sdf/attributeSpec.h>

#include <rtxio/rtxioCompression.h>

#include "usd_mesh_importer.h"
#include "util_filesys.h"
#include "util_string.h"
#include "util_error.h"

#include "rtx_asset_package_writer.h"
#include "rtx_asset_package_extractor.h"
#include "usdg.h"

#include <filesystem>
#include <chrono>
#include <set>
#include <cmath>
#include <algorithm>

using namespace dxvk;

constexpr uint32_t kMaxU16Indices = 64 * 1024;

static size_t g_totalLoaded = 0;
static uint64_t g_totalTimeUs = 0;
static uint64_t g_stageTimeUs = 0;

dxvk::Logger dxvk::Logger::s_instance("usdg-converter.log", LogLevel::None);

typedef AssetPackageWriter::PackageHandle (*PFN_UsdgGetPackage)(const char* packagePath);
static PFN_UsdgGetPackage UsdgGetPackage = nullptr;

namespace {
const pxr::TfToken kPoints("points");
const pxr::TfToken kNormals("normals");
const pxr::TfToken kColorAttribute("displayColor");
const pxr::TfToken kColorPrimVar("primvars:displayColor");
const pxr::TfToken kOpacityAttribute("displayOpacity");
const pxr::TfToken kOpacityPrimVar("primvars:displayOpacity");

const pxr::TfToken kFaceVertexCounts("faceVertexCounts");
const pxr::TfToken kFaceVertexIndices("faceVertexIndices");
const pxr::TfToken kIndices("triangleIndices");
const pxr::TfToken kIndices2("indices");

const pxr::TfToken kUvs[] = {
  pxr::TfToken("primvars:st"),
  pxr::TfToken("primvars:uv"),
  pxr::TfToken("primvars:st0"),
  pxr::TfToken("primvars:st1"),
  pxr::TfToken("primvars:st2"),
  pxr::TfToken("primvars:UVMap"),

  pxr::TfToken("primvars:st_1"),
  pxr::TfToken("primvars:st_2"),

  pxr::TfToken("primvars:st:indices"),
  pxr::TfToken("primvars:st_1:indices"),
  pxr::TfToken("primvars:st_2:indices"),

  pxr::TfToken("invertedUvs")
};
}

template<typename T>
inline T align(T a, T b) {
  return (a + b - 1) & ~(b - 1);
}

// Inverse of the unsigned-octahedral 32-bit normal encoding used when baking
// vertex data (see UsdMeshImporter's Attributes::Normals handling).
static inline pxr::GfVec3f decodeOctahedralNormal(uint32_t encoded) {
  constexpr float kUnorm16Scalar = (1 << 16) - 1;

  float x = static_cast<float>(encoded & 0xffffu) / kUnorm16Scalar;
  float y = static_cast<float>((encoded >> 16) & 0xffffu) / kUnorm16Scalar;

  // Unsigned -> signed octahedral
  x = x * 2.0f - 1.0f;
  y = y * 2.0f - 1.0f;

  pxr::GfVec3f v(x, y, 1.0f - std::abs(x) - std::abs(y));
  const float t = std::max(-v[2], 0.0f);

  v[0] += (v[0] >= 0.0f) ? -t : t;
  v[1] += (v[1] >= 0.0f) ? -t : t;

  return v.GetNormalized();
}

static inline uint64_t NowCountUs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::nanoseconds(now.time_since_epoch()).count() / 1000;
}

static inline std::string Canonize(const std::string& path) {
  std::string canonPath;
  for (auto it = path.begin(); it != path.end(); ++it) {
    auto c = *it;

    if (canonPath.empty() && (c == '/' || c == '\\' || c == '.')) {
      continue;
    }

    switch (c) {
    case '[':
      // Start over
      canonPath.clear();
      break;
    case ']':
      // Skip it
      break;
    case '/':
      c = '\\';
    default:
      canonPath.push_back(c);
    }
  }

  return canonPath;
}

namespace remix {

uint64_t getStrongestOpinionatedPathHash(const pxr::UsdPrim& prim) {
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

      uint64_t usdOriginHash = 0;
      usdOriginHash = std::hash<std::string> {}(originOfMeshFile);
      usdOriginHash ^= std::hash<std::string>{}(originPath);

      return usdOriginHash;
    }
  }

  Logger::err(str::format("Asset Replacement failed to find a source prim for ", prim.GetPath().GetString()));
  // fall back to using the prim's path in replacements.usda.  Potentially worse performance, since it may lead to duplicates.
  std::string name = prim.GetPath().GetString();
  return std::hash<std::string>{}(name);

}

uint64_t getNamedHash(const std::string& name, const char* prefix, const size_t len) {
  if (name.compare(0, len, prefix) == 0) {
    // is a mesh replacement.
    return std::strtoull(name.c_str() + len, nullptr, 16);
  } else {
    // Not a mesh replacements
    return 0;
  }
}

uint64_t getModelHash(const pxr::UsdPrim& prim) {
  static const char* prefix = "mesh_";
  static const size_t len = strlen(prefix);
  return getNamedHash(prim.GetName().GetString(), prefix, len);
}

uint64_t getMaterialHash(const pxr::UsdPrim& prim, const pxr::UsdPrim& shader) {
  static const pxr::TfToken kMaterialType("Material");
  static const char* prefix = "mat_";
  static const size_t len = strlen(prefix);
  std::string name = prim.GetName().GetString();
  uint64_t nameHash = getNamedHash(name, prefix, len);
  if (nameHash != 0) {
    return nameHash;
  }
  if (prim.GetTypeName() != kMaterialType) {
    return 0;
  }

  if (!shader.IsValid()) {
    return 0;
  }

  uint64_t usdOriginHash = getStrongestOpinionatedPathHash(shader);

  return usdOriginHash;
}

} // namespace remix

class UsdProcessor {
public:
  struct Config {
    union {
      int32_t imageCompressionLevel = 8;
      bool extractImages;
    };
    int32_t geometryCompressionLevel = 0;
    int32_t imagePkgSplitMB = 0;
    int32_t geometryPkgSplitMB = 0;
    int32_t maxBoneCount = 4;

    bool clearBakedData = false;
    bool convertToBinary = false;
    bool update = false;
  };

  virtual ~UsdProcessor() = default;
  virtual void process(const std::string& stagePath) = 0;
};

template<bool kConvertingToUsdg>
class UsdgConverter : public UsdProcessor {
  std::unique_ptr<AssetPackageWriter> m_writer;
  std::unique_ptr<AssetPackageExtractor> m_extractor;

  std::shared_ptr<dxvk::AssetPackage> m_package;

  std::list<std::shared_ptr<dxvk::AssetPackage>> m_auxPackages;
  std::shared_ptr<dxvk::AssetPackage> m_imagePackage;
  std::shared_ptr<dxvk::AssetPackage> m_geometryPackage;
  size_t m_imageDataSize = 0;
  size_t m_geometryDataSize = 0;
  int32_t m_auxPackageIndex = 0;

  std::string m_basePath;
  std::filesystem::path m_outputPackagePath;

  std::unordered_set<uint64_t> m_processedPrimSet;
  std::list<std::string> m_tmpFilesToRemove;

  int32_t m_imageCompressionLevel = 8;
  int32_t m_geometryCompressionLevel = 0;
  size_t m_imagePkgSplit = 0;
  size_t m_geometryPkgSplit = 0;

  uint32_t m_maxBones = 4;
  bool m_clearBakedData = false;
  bool m_useAnsiMainUsd = false;
  bool m_convertToBinary = false;
  bool m_extractImages = true;
  bool m_update = false;

  bool shouldProcessImages() const {
    return m_imageCompressionLevel >= 0;
  }

  bool shouldProcessGeometry() const {
    return m_geometryCompressionLevel >= 0;
  }

  std::string getNextPackageName() {
    if (m_outputPackagePath.empty()) {
      // Default to package path.
      m_outputPackagePath = std::filesystem::path(m_package->getBaseFilename());
      m_outputPackagePath.replace_extension();
    }

    char indexedPackageName[1024];
    sprintf(indexedPackageName, "_%02d.pkg", m_auxPackageIndex);

    auto packagePath = m_outputPackagePath;
    packagePath += indexedPackageName;

    m_auxPackageIndex++;

    return packagePath.string();
  }

  std::shared_ptr<dxvk::AssetPackage> getImagePackage() {
    if (m_imagePkgSplit == 0)
      return m_package;

    if (m_imagePackage && m_imageDataSize > m_imagePkgSplit) {
      m_writer->finalizePackage(m_imagePackage);

      m_auxPackages.emplace_back(std::move(m_imagePackage));
    }

    if (!m_imagePackage) {
      auto packageFilename = getNextPackageName();
      m_imagePackage = m_writer->createPackage(packageFilename);

      if (!m_imagePackage) {
        Logger::err(str::format("Fatal: unable to create image package at ", packageFilename));
      }

      m_imageDataSize = 0;
    }

    return m_imagePackage;
  }

  std::shared_ptr<dxvk::AssetPackage> getGeometryPackage() {
    if (m_geometryPkgSplit == 0)
      return m_package;

    if (m_geometryPackage && m_geometryDataSize > m_geometryPkgSplit) {
      m_writer->finalizePackage(m_geometryPackage);

      m_auxPackages.emplace_back(std::move(m_geometryPackage));
    }

    if (!m_geometryPackage) {
      auto packageFilename = getNextPackageName();
      m_geometryPackage = m_writer->createPackage(packageFilename);

      if (!m_geometryPackage) {
        Logger::err(str::format("Fatal: unable to create geometry package at ", packageFilename));
      }

      m_geometryDataSize = 0;
    }

    return m_geometryPackage;
  }

  template<typename T>
  bool isSame(std::string path, const std::vector<T>& buffer) {
    std::shared_ptr<dxvk::AssetPackage> package;
    const uint32_t assetIdx = findAsset(path, package);

    if (assetIdx != dxvk::AssetPackage::kNoAssetIdx) {
      auto desc = package->getAssetDesc(assetIdx);

      if (auto blobDesc = package->getDataBlobDesc(desc->baseBlobIdx)) {
        const uint32_t crc32 = rtxioCrc32(0, buffer.data(), buffer.size() * sizeof(T));
        return blobDesc->crc32 == crc32;
      }
    }

    return false;
  }

  template<typename T>
  void storeBuffer(std::string path, const std::vector<T>& buffer) {
    if (buffer.size() == 0) {
      return;
    }

    if (path[1] == ':' || (path[0] == '\\' && path[0] == path[1])) {
      // trim the full path to the base
      path = path.substr(m_basePath.length());
    }

    const auto name = Canonize(path);

    // Check if buffer data has been updated
    if (m_update && isSame(name, buffer)) {
      Logger::info(str::format("\tSkipping unchanged buffer: ", name));
      return;
    }

    if (auto package = getGeometryPackage()) {
      const uint32_t volume = package == m_package ? 1 : 0;
      m_writer->addBufferAsset(package, volume, name, m_geometryCompressionLevel,
          buffer.data(), buffer.size() * sizeof(T));
      m_geometryDataSize += buffer.size() * sizeof(T);
    } else {
      auto absPath = std::filesystem::current_path();
      absPath += path;
      absPath.make_preferred();

      auto parentPath = std::filesystem::path(absPath).parent_path();
      std::filesystem::create_directories(parentPath);

      if (FILE* f = fopen(absPath.string().c_str(), "wb")) {
        fwrite(buffer.data(), 1, buffer.size() * sizeof(T), f);
        fclose(f);
      }
    }
  }

  uint32_t findAsset(const std::string& path, std::shared_ptr<dxvk::AssetPackage>& outPackage) {
    auto cpath = Canonize(path);

    for (auto it = m_auxPackages.rbegin(); it != m_auxPackages.rend(); ++it) {
      const uint32_t assetIdx = (*it)->findAsset(cpath);
      if (assetIdx != dxvk::AssetPackage::kNoAssetIdx) {
        outPackage = *it;
        return assetIdx;
      }
    }

    if (m_package) {
      const uint32_t assetIdx = m_package->findAsset(cpath);
      if (assetIdx != dxvk::AssetPackage::kNoAssetIdx) {
        outPackage = m_package;
        return assetIdx;
      }
    }

    return dxvk::AssetPackage::kNoAssetIdx;
  }

  std::vector<uint8_t> loadBuffer(const std::string& path) {
    std::shared_ptr<dxvk::AssetPackage> package;
    const uint32_t assetIdx = findAsset(path, package);

    std::vector<uint8_t> buffer;

    // TODO: move to extractor
    if (assetIdx != dxvk::AssetPackage::kNoAssetIdx) {
      auto desc = package->getAssetDesc(assetIdx);

      if (auto blobDesc = package->getDataBlobDesc(desc->baseBlobIdx)) {
        buffer.resize(blobDesc->size);

        package->readDataBlob(desc->baseBlobIdx, buffer.data(), buffer.size());

        if (blobDesc->compression != RTXIO_COMPRESSION_NONE) {
          size_t uncompressedSize = desc->size;
          std::vector<uint8_t> uncompressed(uncompressedSize);

          if (!rtxioDecompress(uncompressed.data(), uncompressedSize, buffer.data(), buffer.size())) {
            throw std::runtime_error("Unable to decompress asset data.");
          }

          buffer = std::move(uncompressed);
        }
      }

      g_totalLoaded += buffer.size();
      return buffer;
    } else {
      Logger::warn(str::format("File ", path, " not found in packages."));
    }

    auto absPath = std::filesystem::current_path();
    absPath += path;
    absPath.make_preferred();

    if (FILE* f = fopen(absPath.string().c_str(), "rb")) {
      fseek(f, 0, SEEK_END);
      buffer.resize(ftell(f));
      fseek(f, 0, SEEK_SET);
      fread(buffer.data(), 1, buffer.size(), f);
      fclose(f);

      g_totalLoaded += buffer.size();
    } else {
      Logger::warn(str::format("file not found: ", path));
    }

    return buffer;
  }

  // Packages a texture file assigned to a texture token in a shader.
  void processTexture(const pxr::UsdPrim& shader, const pxr::TfToken& textureToken) {
    if (!shouldProcessImages()) {
      return;
    }

    pxr::SdfAssetPath path;
    auto attr = shader.GetAttribute(textureToken);
    if (attr.Get(&path)) {
      const std::string& strPath = path.GetResolvedPath();
      if (!strPath.empty()) {
        auto name = Canonize(strPath.substr(m_basePath.length()));

        if (m_update) {
          std::shared_ptr<dxvk::AssetPackage> package;
          const uint32_t assetIdx = findAsset(name, package);
          if (AssetPackageWriter::isImageAssetChanged(strPath, package, assetIdx)) {
            Logger::info(str::format("\tSkipping unchanged image: ", name));
            return;
          }
        }

        Logger::info(str::format("\tProcessing image: ", name));

        const int volume = m_imagePkgSplit > 0 ? 0 : 1;
        m_writer->addImageAsset(getImagePackage(), volume, name, m_imageCompressionLevel, strPath);
        m_imageDataSize += std::filesystem::file_size(strPath);
      } else if (!path.GetAssetPath().empty()) {
        Logger::warn(str::format("rtx_asset_replacer found a texture with an invalid path: ", path.GetAssetPath()));
      }
    }
  }

  // Process a Remix material.
  void processMaterial(const pxr::UsdPrim& matPrim) {
    if (!kConvertingToUsdg) {
      // No action when not converting.
      return;
    }

    // Textures
    static const pxr::TfToken kShaderToken("Shader");
    static const pxr::TfToken kTextureTokens[] = {
      pxr::TfToken("inputs:diffuse_texture"),
      pxr::TfToken("inputs:normalmap_texture"),
      pxr::TfToken("inputs:tangent_texture"),
      pxr::TfToken("inputs:height_texture"),
      pxr::TfToken("inputs:reflectionroughness_texture"),
      pxr::TfToken("inputs:metallic_texture"),
      pxr::TfToken("inputs:emissive_mask_texture"),
      pxr::TfToken("inputs:transmittance_texture"),
      pxr::TfToken("inputs:subsurface_transmittance_texture"),
      pxr::TfToken("inputs:subsurface_thickness_texture"),
      pxr::TfToken("inputs:subsurface_single_scattering_texture"),
      pxr::TfToken("inputs:subsurface_radius_texture"),
      pxr::TfToken("inputs:secondary_texture"),
    };
    // Attributes
    static const pxr::TfToken kIgnore("inputs:ignore_material");  // Any draw call or replacement using a material with this flag will be skipped by the SceneManager

    pxr::UsdPrim shader = matPrim.GetChild(kShaderToken);
    if (!shader.IsValid() || !shader.IsA<pxr::UsdShadeShader>()) {
      auto children = matPrim.GetFilteredChildren(pxr::UsdPrimIsActive);
      for (auto child : children) {
        if (child.IsA<pxr::UsdShadeShader>()) {
          shader = child;
        }
      }
    }

    if (!shader.IsValid()) {
      Logger::warn(str::format("Material shader not found: ", matPrim.GetPath().GetString()));
      return;
    }

    const uint64_t materialHash = remix::getMaterialHash(matPrim, shader);
    if (materialHash == 0) {
      Logger::warn(str::format("Unable to calculate material hash: ", matPrim.GetPath().GetString()));
      return;
    }

    // Check if the material has already been processed
    if (m_processedPrimSet.count(materialHash) > 0) {
      return;
    }

    for (const auto& token : kTextureTokens) {
      processTexture(shader, token);
    }

    m_processedPrimSet.insert(materialHash);
  }

  // Process a material bound to a primitive.
  void processMaterialUser(const pxr::UsdPrim& prim) {
    auto bindAPI = pxr::UsdShadeMaterialBindingAPI(prim);
    auto boundMaterial = bindAPI.ComputeBoundMaterial();
    if (boundMaterial) {
      processMaterial(boundMaterial.GetPrim());
    }
  }

  bool isAnsiUSD(const std::string& path) {
    // Check if layer is ANSI. USD uses extension to determine file format so
    // it is safe to check just the last character.
    return path.back() == 'a' || path.back() == 'A';
  }

  void fixupUSDAPath(std::string& path) {
    if (!m_convertToBinary) {
      return;
    }

    size_t extPos = path.find(".usda");

    if (extPos == std::string::npos) {
      extPos = path.find(".USDA");
    }

    if (extPos != std::string::npos) {
      path.erase(extPos + 4, 1);
    }
  }

  void serializeMesh(const pxr::UsdPrim& prim) {
    std::unique_ptr<lss::UsdMeshImporter> processedMesh;

    try {
      processedMesh = std::make_unique<lss::UsdMeshImporter>(prim, m_maxBones);
    }
    catch (dxvk::DxvkError e) {
      Logger::err(e.message());
      return;
    }

    // Push the AABB
    if (shouldProcessGeometry()) {
      auto aabb = processedMesh->GetBoundingBox();

      usdg::AABB usdgAabb;
      usdgAabb.value() = { {aabb.minPos.x, aabb.minPos.y, aabb.minPos.z},
          {aabb.maxPos.x, aabb.maxPos.y, aabb.maxPos.z} };

      usdgAabb.serialize(prim);
    }

    // Process vertices
    if (shouldProcessGeometry()) {
      usdg::GpuDataDecl vertexDecl(usdg::kVerticesName);

      for (const auto& element : processedMesh->GetVertexDecl()) {
        switch (element.attribute) {
        case lss::UsdMeshImporter::VertexPositions:
          vertexDecl.addElement(usdg::Attribute::Position, usdg::Type::Float3, element.offset);
          if (m_clearBakedData) {
            usdg::ClearAttribute(prim, kPoints);
          }
          break;
        case lss::UsdMeshImporter::Normals:
          vertexDecl.addElement(usdg::Attribute::Normal, usdg::Type::UInt, element.offset);
          if (m_clearBakedData) {
            usdg::ClearAttribute(prim, kNormals);
          }
          break;
        case lss::UsdMeshImporter::Texcoords:
          vertexDecl.addElement(usdg::Attribute::Uv, usdg::Type::Float2, element.offset);
          if (m_clearBakedData) {
            for (const pxr::TfToken& uvName : kUvs) {
              if (prim.HasAttribute(uvName)) {
                usdg::ClearAttribute(prim, uvName);
              }
            }
          }
          break;
        case lss::UsdMeshImporter::Colors:
          vertexDecl.addElement(usdg::Attribute::Color, usdg::Type::UChar, element.offset, 4);

          if (m_clearBakedData) {
            usdg::ClearAttribute(prim, kColorAttribute);
            usdg::ClearAttribute(prim, kColorPrimVar);
            usdg::ClearAttribute(prim, kOpacityAttribute);
            usdg::ClearAttribute(prim, kOpacityPrimVar);
          }
          break;
        case lss::UsdMeshImporter::BlendWeights:
          // The last joint's weight is implicit, so a single-bone mesh has zero stored weights.
          vertexDecl.addElement(usdg::Attribute::BlendWeight, usdg::Type::Float, element.offset,
              element.size / sizeof(float));

          if (m_clearBakedData) {
            usdg::ClearAttribute(prim, pxr::UsdSkelTokens->primvarsSkelJointWeights);
          }
          break;
        case lss::UsdMeshImporter::BlendIndices:
          vertexDecl.addElement(usdg::Attribute::BlendIndices, usdg::Type::UChar, element.offset,
              std::max<size_t>(1, processedMesh->GetNumBonesPerVertex()));

          if (m_clearBakedData) {
            usdg::ClearAttribute(prim, pxr::UsdSkelTokens->primvarsSkelJointIndices);
          }
          break;
        }
      }

      // Find prim spec for vertices
      auto primSpec = usdg::GetPrimSpecForAttrib(prim, kPoints);

      // Serialize vertex declaration in the prim spec
      vertexDecl.serialize(primSpec);

      // Push num bones
      if (processedMesh->GetNumBonesPerVertex() > 0) {
        usdg::SetCustomMetadata(primSpec, usdg::kNumBones,
          static_cast<uint32_t>(processedMesh->GetNumBonesPerVertex()));
      }

      usdg::SetCustomMetadata(primSpec, usdg::kBaked, true);

      auto path = usdg::ResolvePath(prim, kPoints);
      fixupUSDAPath(path);

      storeBuffer(path + "/" + usdg::kVerticesName, processedMesh->GetVertexData());

      if (m_clearBakedData) {
        usdg::ClearAttribute(prim, kFaceVertexIndices);
        usdg::ClearAttribute(prim, kFaceVertexCounts);
      }
    }

    processMaterialUser(prim);

    for (const lss::UsdMeshImporter::SubMesh& submesh : processedMesh->GetSubMeshes()) {
      if (submesh.GetNumIndices() == 0) {
        Logger::err(str::format("Fatal: prim ", submesh.prim.GetPath().GetString(),
                                ", does not have indices, this is currently a requirement."));
        continue;
      }

      processMaterialUser(submesh.prim);

      if (!shouldProcessGeometry()) {
        continue;
      }

      usdg::GpuDataDecl indexDecl(usdg::kIndicesName);
      indexDecl.addElement(usdg::Attribute::Index, usdg::Type::UInt, 0);

      // Find the source prim spec of indices
      auto primSpec = usdg::GetPrimSpecForAttrib(submesh.prim, kIndices);
      if (primSpec == nullptr) {
        primSpec = usdg::GetPrimSpecForAttrib(submesh.prim, kIndices2);
        if (primSpec == nullptr) {
          primSpec = usdg::GetPrimSpecForAttrib(submesh.prim, kFaceVertexIndices);
        }
      }

      // Serialize index declaration in the prim spec
      indexDecl.serialize(primSpec);
      usdg::SetCustomMetadata(primSpec, usdg::kBaked, true);

      auto path = primSpec->GetLayer()->GetRealPath() +
        primSpec->GetPath().GetString(); //usdg::ResolvePath(submesh.prim, kIndices);
      fixupUSDAPath(path);

      storeBuffer(path + "/" + usdg::kIndicesName, submesh.indexBuffer);

      if (m_clearBakedData) {
        usdg::ClearAttribute(submesh.prim, kIndices);
        usdg::ClearAttribute(submesh.prim, kIndices2);
      }
    }
  }

  static inline pxr::SdfAttributeSpecHandle FindSourceSpec(const pxr::UsdPrim& prim, const pxr::TfToken& attrib) {
    for (auto spec : prim.GetPrimStack()) {
      auto attribs = spec->GetAttributes();
      auto ait = attribs.find(attrib);
      if (ait != attribs.end()) {
        return (*ait);
      }
    }
    return {};
  }

  static inline pxr::SdfAttributeSpecHandle GetSourceSpec(const pxr::UsdAttribute& attr) {
    auto propStack = attr.GetPropertyStack(pxr::UsdTimeCode::Default());
    if (propStack.empty()) return pxr::SdfAttributeSpecHandle();
    return pxr::TfDynamic_cast<pxr::SdfAttributeSpecHandle>(propStack[0]);
  }

  void deserializeFaces(const pxr::UsdPrim& prim) {
    pxr::UsdGeomMesh mesh(prim);
    if (mesh.GetFaceVertexIndicesAttr().HasValue()) {
      // Mesh attributes were not cleared - no need to repopulate them. Bail.
      return;
    }

    auto loadSubsetIndices = [this](const pxr::UsdPrim& prim) {
      usdg::GpuDataDecl indexDecl(usdg::kIndicesName);
      indexDecl.deserialize(prim);

      // Find the source prim spec of face indices or submesh indices
      auto primSpec = usdg::GetPrimSpecForAttrib(prim, kFaceVertexIndices);
      if (primSpec == nullptr) {
        primSpec = usdg::GetPrimSpecForAttrib(prim, kIndices);
        if (primSpec == nullptr) {
          primSpec = usdg::GetPrimSpecForAttrib(prim, kIndices2);
        }
      }

      auto path = primSpec->GetLayer()->GetRealPath() + primSpec->GetPath().GetString();

      return loadBuffer(Canonize(path + "/" + usdg::kIndicesName));
    };

    auto children = prim.GetFilteredChildren(pxr::UsdPrimIsActive);
    std::list<std::pair<pxr::UsdPrim, std::vector<uint8_t>>> subsets;
    for (const auto& child : children) {
      if (child.IsA<pxr::UsdGeomSubset>()) {
        subsets.emplace_back(child, loadSubsetIndices(child));
      }
    }

    if (subsets.empty()) {
      subsets.emplace_back(prim, loadSubsetIndices(prim));
    }

    // Each subset's data buffer is a flat array of triangle vertex indices (3 per
    // triangle) into the mesh's points array. Rebuild the mesh's faceVertexCounts /
    // faceVertexIndices from the concatenation of all subsets, and, for real
    // UsdGeomSubset prims, rebuild their face-index "indices" attribute to reference
    // the faces they contributed.
    pxr::VtIntArray faceVertexCounts;
    pxr::VtIntArray faceVertexIndices;

    for (auto& [subsetPrim, buffer] : subsets) {
      const uint32_t* triangleIndices = reinterpret_cast<const uint32_t*>(buffer.data());
      const size_t numIndices = buffer.size() / sizeof(uint32_t);
      const size_t numTriangles = numIndices / 3;

      pxr::VtIntArray subsetFaceIndices;
      subsetFaceIndices.reserve(numTriangles);

      for (size_t tri = 0; tri < numTriangles; tri++) {
        subsetFaceIndices.push_back(static_cast<int>(faceVertexCounts.size()));

        faceVertexCounts.push_back(3);
        faceVertexIndices.push_back(static_cast<int>(triangleIndices[tri * 3 + 0]));
        faceVertexIndices.push_back(static_cast<int>(triangleIndices[tri * 3 + 1]));
        faceVertexIndices.push_back(static_cast<int>(triangleIndices[tri * 3 + 2]));
      }

      if (subsetPrim != prim) {
        pxr::UsdGeomSubset subset(subsetPrim);
        GetSourceSpec(subset.GetIndicesAttr())->SetDefaultValue(pxr::VtValue(subsetFaceIndices));
      }
    }

    GetSourceSpec(mesh.GetFaceVertexIndicesAttr())->SetDefaultValue(pxr::VtValue(faceVertexIndices));
    GetSourceSpec(mesh.GetFaceVertexCountsAttr())->SetDefaultValue(pxr::VtValue(faceVertexCounts));
  }

  void deserializeVertices(const pxr::UsdPrim& prim) {
    pxr::UsdGeomMesh mesh(prim);
    if (mesh.GetPointsAttr().HasValue()) {
      // Mesh attributes were not cleared - no need to repopulate them. Bail.
      return;
    }

    usdg::GpuDataDecl vertexDecl(usdg::kVerticesName);
    vertexDecl.deserialize(prim);

    auto path = usdg::ResolvePath(prim, kPoints);
    auto vertices = loadBuffer(path + "/" + usdg::kVerticesName);

    const size_t stride = vertexDecl.size();
    if (vertices.empty() || stride == 0) {
      return;
    }

    const size_t numVertices = vertices.size() / stride;
    const uint8_t* data = vertices.data();

    const bool hasSkel = prim.HasAPI<pxr::UsdSkelBindingAPI>();
    uint32_t numBones = 0;
    usdg::GetCustomMetadata(prim, usdg::kNumBones, numBones);

    const bool restoreBlendData = hasSkel && numBones > 0;
    // Only the first numBones - 1 weights are stored explicitly; the
    // last joint's weight is implicitly 1 - sum(others) (see BlendWeights
    // handling in UsdMeshImporter).
    const uint32_t numStoredWeights = numBones > 0 ? numBones - 1 : 0;

    pxr::VtVec3fArray points(numVertices);
    pxr::VtVec3fArray normals;
    pxr::VtVec2fArray uvs;
    pxr::VtVec3fArray colors;
    pxr::VtFloatArray opacities;
    pxr::VtIntArray jointIndices;
    pxr::VtFloatArray jointWeights;

    for (size_t v = 0; v < numVertices; v++) {
      const uint8_t* vertex = data + v * stride;

      for (const auto& element : vertexDecl.elements()) {
        const uint8_t* field = vertex + element.offset;

        switch (element.attribute) {
        case usdg::Attribute::Position:
        {
          const float* pos = reinterpret_cast<const float*>(field);
          points[v] = pxr::GfVec3f(pos[0], pos[1], pos[2]);
          break;
        }
        case usdg::Attribute::Normal:
        {
          if (normals.empty()) {
            normals.resize(numVertices);
          }
          const uint32_t encoded = *reinterpret_cast<const uint32_t*>(field);
          normals[v] = decodeOctahedralNormal(encoded);
          break;
        }
        case usdg::Attribute::Uv:
        {
          if (uvs.empty()) {
            uvs.resize(numVertices);
          }
          const float* uv = reinterpret_cast<const float*>(field);
          // Undo the v-flip applied when baking texcoords.
          uvs[v] = pxr::GfVec2f(uv[0], 1.0f - uv[1]);
          break;
        }
        case usdg::Attribute::Color:
        {
          if (colors.empty()) {
            colors.resize(numVertices);
            opacities.resize(numVertices);
          }
          const uint32_t argb = *reinterpret_cast<const uint32_t*>(field);
          opacities[v] = static_cast<float>((argb >> 24) & 0xff) / 255.0f;
          colors[v] = pxr::GfVec3f(
              static_cast<float>((argb >> 16) & 0xff) / 255.0f,
              static_cast<float>((argb >> 8) & 0xff) / 255.0f,
              static_cast<float>(argb & 0xff) / 255.0f);
          break;
        }
        case usdg::Attribute::BlendIndices:
        {
          if (jointIndices.empty()) {
            jointIndices.assign(numVertices * numBones, 0);
          }

          for (uint32_t b = 0; b < numBones; b++) {
            jointIndices[v * numBones + b] = static_cast<int>(field[b]);
          }
          break;
        }
        case usdg::Attribute::BlendWeight:
        {
          if (jointWeights.empty()) {
            jointWeights.assign(numVertices * numBones, 0.f);
          }

          const float* w = reinterpret_cast<const float*>(field);
          float sum = 0.0f;

          for (uint32_t b = 0; b < numStoredWeights && b < numBones; b++) {
            jointWeights[v * numBones + b] = w[b];
            sum += w[b];
          }

          if (numBones > numStoredWeights) {
            jointWeights[v * numBones + numStoredWeights] = std::max(0.0f, 1.0f - sum);
          }
          break;
        }
        default:
          break;
        }
      }
    }

    GetSourceSpec(mesh.GetPointsAttr())->SetDefaultValue(pxr::VtValue(points));
    GetSourceSpec(mesh.GetNormalsAttr())->SetDefaultValue(pxr::VtValue(normals));

    if (auto spec = GetSourceSpec(mesh.GetDisplayColorAttr())) {
      spec->SetDefaultValue(pxr::VtValue(colors));
    } else if (auto spec = GetSourceSpec(prim.GetAttribute(kColorAttribute))) {
      spec->SetDefaultValue(pxr::VtValue(colors));
    }

    if (auto spec = GetSourceSpec(mesh.GetDisplayOpacityAttr())) {
      spec->SetDefaultValue(pxr::VtValue(opacities));
    } else if (auto spec = GetSourceSpec(prim.GetAttribute(kOpacityAttribute))) {
      spec->SetDefaultValue(pxr::VtValue(opacities));
    }

    for (const pxr::TfToken& uvName : kUvs) {
      if (prim.HasAttribute(uvName)) {
        GetSourceSpec(prim.GetAttribute(uvName))->SetDefaultValue(pxr::VtValue(uvs));
        break;
      }
    }

    if (restoreBlendData) {
      pxr::UsdSkelBindingAPI skelBinding(prim);
      GetSourceSpec(skelBinding.GetJointIndicesPrimvar().GetAttr())->SetDefaultValue(pxr::VtValue(jointIndices));
      GetSourceSpec(skelBinding.GetJointWeightsPrimvar().GetAttr())->SetDefaultValue(pxr::VtValue(jointWeights));
    }
  }

  void deserializeMesh(const pxr::UsdPrim& prim) {
    if (kConvertingToUsdg) {
      return;
    } else {
      // Logger::info(str::format("Deserializing: ", prim.GetPath().GetText()));
    }

    usdg::AABB aabb;
    aabb.deserialize(prim);

    deserializeVertices(prim);
    deserializeFaces(prim);
  }

  // Process a mesh.
  void processMesh(const pxr::UsdPrim& prim) {
    const uint64_t usdOriginHash = remix::getStrongestOpinionatedPathHash(prim);

    if (m_processedPrimSet.count(usdOriginHash) != 0) {
      return;
    }

    m_processedPrimSet.insert(usdOriginHash);

    if (kConvertingToUsdg) {
      serializeMesh(prim);
    } else {
      if (usdg::IsBaked(prim)) {
        deserializeMesh(prim);
        usdg::SetCustomMetadata(prim, usdg::kBaked, false);
        return;
      } else {
        // Logger::warn(str::format("Discovered a non-baked mesh: ", prim.GetPath().GetText()));
      }
    }
  }

  void processRootPrim(pxr::UsdPrim& rootPrim) {
    if (rootPrim.IsA<pxr::UsdGeomMesh>()) {
      processMesh(rootPrim);
    }

    // Always processing all possible material users to capture 
    // particle system materials and other unordinary cases
    processMaterialUser(rootPrim);

    auto descendents = rootPrim.GetFilteredDescendants(pxr::UsdPrimIsActive);
    for (auto desc : descendents) {
      processRootPrim(desc);
    }
  }

  void processStageLayers(const pxr::UsdStageRefPtr& stage) {
    auto rootLayer = stage->GetRootLayer();

    // Build a lookup from real path to layer so recursive calls can resolve
    // a referenced path back to its in-memory SdfLayer.
    std::unordered_map<std::string, pxr::SdfLayerRefPtr> layersByPath;
    for (const auto& layer : stage->GetUsedLayers()) {
      const std::string realPath = layer->GetRealPath();
      if (!realPath.empty() && realPath != rootLayer->GetRealPath()) {
        layersByPath[realPath] = layer;
      }
    }

    // usda_real_path -> binary_real_path, populated as layers are exported.
    std::unordered_map<std::string, std::string> remapping;
    // Tracks layers that have been fully processed to avoid revisiting them.
    std::unordered_set<std::string> visited;

    // Recursively process a single layer:
    //   1. recurse into its dependencies first
    //   2. fix in-memory references that now have binary counterparts
    //   3. export this layer to binary (if ASCII), with correct references baked in
    std::function<void(const pxr::SdfLayerRefPtr&)> processLayer =
      [&](const pxr::SdfLayerRefPtr& layer) {
      std::string realPath = layer->GetRealPath();
      if (realPath.empty() || !visited.insert(realPath).second) {
        return;
      }

      const auto layerDir = std::filesystem::path(realPath).parent_path();

      // Snapshot external refs before any modifications so we hold the
      // original path strings needed by UpdateExternalReference below.
      const std::set<std::string> externalRefs = layer->GetExternalReferences();

      // Step 1: depth-first - convert dependencies before this layer.
      for (const auto& ref : externalRefs) {
        const std::filesystem::path refPath(ref);
        const std::string resolvedStr =
          (refPath.is_absolute() ? refPath : (layerDir / refPath).lexically_normal())
          .string();

        const auto it = layersByPath.find(resolvedStr);
        if (it != layersByPath.end()) {
          processLayer(it->second);
        }
      }

      // Step 2: fix references whose targets were just converted.
      if (m_convertToBinary) {
        for (const auto& ref : externalRefs) {
          const std::filesystem::path refPath(ref);
          const std::string resolvedStr =
            (refPath.is_absolute() ? refPath : (layerDir / refPath).lexically_normal()).string();

          const auto it = remapping.find(resolvedStr);
          if (it == remapping.end()) {
            Logger::info(str::format("Layer not found: ", resolvedStr));
            continue;
          }

          const std::filesystem::path newBinaryPath(it->second);
          std::string newRef;
          if (refPath.is_absolute()) {
            newRef = newBinaryPath.string();
          } else {
            newRef = std::filesystem::relative(newBinaryPath, layerDir).string();
          }

          layer->UpdateExternalReference(ref, newRef);
        }
      }

      // Step 3: export this layer.

      // Trim path to base path to get relative name
      auto name = realPath.substr(m_basePath.length());
      const bool isAnsi = isAnsiUSD(realPath);

      if (shouldProcessGeometry() || (m_convertToBinary && isAnsi) || m_clearBakedData || layer == rootLayer) {
        auto origPath = realPath;

        if (m_convertToBinary && isAnsi) {
          Logger::info(str::format("Converting layer to binary: ", realPath));

          // Drop the last character in the extension
          realPath.resize(realPath.length() - 1);
          name.resize(name.length() - 1);
        }

        if (layer == rootLayer) {
          realPath = name = m_useAnsiMainUsd ? "main.usda" : "main.usd";
        }

        char tmp[L_tmpnam];
        std::tmpnam(tmp);
        std::strcat(tmp, realPath.data() + realPath.rfind('.'));

        if (layer->Export(tmp)) {
          remapping[origPath] = realPath;
          realPath = tmp;

          m_tmpFilesToRemove.push_back(tmp);
        } else {
          Logger::err(str::format("Fatal: unable to export layer: ", realPath, " to ", tmp));
        }
      }

      // Put layer to the package
      m_writer->addUsdAsset(m_package, name, realPath);
    };

    processLayer(rootLayer);
  }

  void exportStageLayers(const pxr::UsdStageRefPtr& stage) {
    // Go over each used layer and export them
    auto layerStack = stage->GetUsedLayers();
    for (auto layer : layerStack) {
      auto path = layer->GetRealPath();

      if (path.empty()) {
        continue;
      }

      usdg::MakeFullPathFromPackageRelativePath(path);

      if (path.find(".usdg") != std::string::npos || path.find(".USDG") != std::string::npos) {
        path.back() = 'a';
      }

      if (!layer->Export(path)) {
        Logger::err(str::format("Fatal: unable to export layer to ", path));
      }
    }
  }

  // TODO: unify with Remix runtime
  bool mountStagePackages(const pxr::UsdStageRefPtr& stage, const std::string& stageFile) {
    if (kConvertingToUsdg && !m_update) return true;

    // check if our stage is a package.
    if (!kConvertingToUsdg && stage->GetRootLayer()->GetFileFormat()->IsPackage()) {
      m_package = UsdgGetPackage(stageFile.c_str());

      if (m_package) {
        Logger::info(str::format("USDG package mounted: ", m_package->getBaseFilename()));
      } else {
        Logger::err(str::format("Fatal: unable to mount a USDG package at ", stageFile));
        return false;
      }
    }

    auto workPath = m_basePath;

    if (m_update) {
      auto outputPackage = m_outputPackagePath;
      workPath = outputPackage.remove_filename().string();
    }

    // Find other packages
    std::vector<std::filesystem::directory_entry> packages;
    for (const auto& entry : std::filesystem::directory_iterator(workPath)) {
      if (entry.path().extension() == ".pkg" || entry.path().extension() == ".rtxio") {
        packages.push_back(entry);
      }
    }

    // Sort packages in natural order
    std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
      return ::StrCmpLogicalW(a.path().filename().wstring().c_str(), b.path().filename().wstring().c_str()) < 0;
    });

    for (auto entry : packages) {
      auto entryString = entry.path().string();

      if (auto package = m_extractor->openPackage(entryString)) {
        Logger::info(str::format("Package mounted: ", package->getBaseFilename()));
        m_auxPackages.emplace_back(std::move(package));
      }

      // If updating - find the index of last package
      if (m_update) {
        if (auto updatePtr = strstr(entryString.c_str(), "update_")) {
          m_auxPackageIndex = atoi(updatePtr + strlen("update_"));
        }
      }
    }

    if (m_update) {
      Logger::info(str::format("Update package index starts at ", m_auxPackageIndex));
    }

    return true;
  }

public:
  UsdgConverter() = delete;
  UsdgConverter(const std::string& dstUsdgFilename, const Config& config);
  ~UsdgConverter() override = default;

  void process(const std::string& stageFile) override {
    uint64_t startUs = NowCountUs();

    // Load stage
    pxr::ArResolverScopedCache scopedCache;
    auto stage = pxr::UsdStage::Open(stageFile, pxr::UsdStage::LoadNone);

    if (!stage) {
      Logger::err(str::format("Fatal: unable to open a ", kConvertingToUsdg ? "USD" : "USDG",
                              " stage at ", stageFile));
      return;
    }

    g_stageTimeUs = NowCountUs() - startUs;

    {
      auto rootLayerPath = std::filesystem::path(stage->GetRootLayer()->GetRealPath());
      rootLayerPath.remove_filename();
      m_basePath = rootLayerPath.string();
      // Add the backslash so we can trim it from the fullpaths later in one step
      if (m_basePath.back() != '\\' && m_basePath.back() != '/') {
        m_basePath += '\\';
      }
    }

    // Mount packages
    if (!mountStagePackages(stage, stageFile)) {
      return;
    }

    // Process geometry
    auto meshes = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/meshes"));
    if (meshes.IsValid()) {
      auto children = meshes.GetFilteredChildren(pxr::UsdPrimIsActive);
      for (auto child : children) {
        uint64_t hash = remix::getModelHash(child);
        if (hash != 0) {
          processRootPrim(child);
        }
      }
    }

    // Process materials
    pxr::UsdPrim materialRoot = stage->GetPrimAtPath(pxr::SdfPath("/RootNode/Looks"));
    if (materialRoot.IsValid()) {
      auto children = materialRoot.GetFilteredChildren(pxr::UsdPrimIsActive);
      for (pxr::UsdPrim materialPrim : children) {
        processMaterial(materialPrim);
      }
    }

    // Process layers and finalize the package
    if (kConvertingToUsdg) {
      processStageLayers(stage);

      // Finalize package
      m_writer->finalizePackage(m_package);

      // Finalize image package if any
      if (m_imagePackage) {
        m_writer->finalizePackage(m_imagePackage);
        m_auxPackages.emplace_back(std::move(m_imagePackage));
      }

      // Finalize geometry package if any
      if (m_geometryPackage) {
        m_writer->finalizePackage(m_geometryPackage);
        m_auxPackages.emplace_back(std::move(m_geometryPackage));
      }

      for (auto& package : m_auxPackages) {
        m_writer->waitPackageCompletion(package);
      }

      m_writer->waitPackageCompletion(m_package);

      for (auto file : m_tmpFilesToRemove) {
        std::filesystem::remove(file);
      }
    } else {
      if (m_extractImages) {
        for (auto& package : m_auxPackages) {
          m_extractor->extractPackage(package, m_basePath, 8);
        }

        m_extractor->extractPackage(m_package, m_basePath, 8);
      }

      exportStageLayers(stage);
    }

    g_totalTimeUs = NowCountUs() - startUs;

    if (!kConvertingToUsdg) {
      Logger::info(str::format("\n", m_processedPrimSet.size(), " replacement meshes processed"));
      Logger::info(str::format("geometry bytes: ", g_totalLoaded));
      Logger::info(str::format("USD stage parse time: ", g_stageTimeUs, " us"));
      Logger::info(str::format("total time: ", g_totalTimeUs, " us"));
    }
  }
};

template<>
UsdgConverter<true>::UsdgConverter(const std::string& dstUsdgFilename, const Config& config) {
  m_clearBakedData = config.clearBakedData;
  m_convertToBinary = config.convertToBinary;
  m_imageCompressionLevel = std::clamp(config.imageCompressionLevel, -1, 12);
  m_geometryCompressionLevel = std::clamp(config.geometryCompressionLevel, -1, 12);
  m_imagePkgSplit = size_t(config.imagePkgSplitMB) * 1024 * 1024;
  m_geometryPkgSplit = size_t(config.geometryPkgSplitMB) * 1024 * 1024;
  m_maxBones = std::clamp(config.maxBoneCount, 2, 16);
  m_update = config.update;

  if (m_update) {
    m_extractor = std::make_unique<AssetPackageExtractor>();

    if (auto inPackage = m_extractor->openPackage(dstUsdgFilename)) {
      m_auxPackages.push_back(std::move(inPackage));

      std::filesystem::path path = dstUsdgFilename;
      m_outputPackagePath = std::filesystem::absolute(path).remove_filename();
      m_outputPackagePath /= "update";

      // Rename original usdg to tmp
      m_tmpFilesToRemove.push_back(std::filesystem::path(dstUsdgFilename).replace_extension("tmp").string());
      std::filesystem::rename(dstUsdgFilename, m_tmpFilesToRemove.back());

      // Force package splits for update
      if (m_imagePkgSplit == 0) m_imagePkgSplit = 2048llu * 1024 * 1024;
      if (m_geometryPkgSplit == 0) m_geometryPkgSplit = 1024llu * 1024 * 1024;
    } else {
      Logger::warn(str::format("Warning: unable to open USDG package for update at ",
        dstUsdgFilename, ". New USDG package will be created."));
      m_update = false;
    }
  }

  m_writer = std::make_unique<AssetPackageWriter>();
  m_package = m_writer->createPackage(dstUsdgFilename);

  if (!m_package) {
    throw std::runtime_error("Unable to create USDG package at " + dstUsdgFilename);
  }
}

template<>
UsdgConverter<false>::UsdgConverter(const std::string& dstUsdgFilename, const Config& config) {
  HMODULE h = LoadLibraryA("usdg.dll");
  UsdgGetPackage = (PFN_UsdgGetPackage) GetProcAddress(h,
    "?UsdgGetPackage@@YA?AV?$shared_ptr@VAssetPackage@dxvk@@@std@@PEBD@Z");

  char modulePath[1024];
  GetModuleFileNameA(NULL, modulePath, sizeof(modulePath));
  std::filesystem::path pluginsPath(modulePath);

  pluginsPath.remove_filename();
  pluginsPath /= "usd\\plugins";

  pxr::PlugRegistry::GetInstance().RegisterPlugins(pluginsPath.string().c_str());

  m_extractor = std::make_unique<AssetPackageExtractor>();

  m_extractImages = config.extractImages;
}

int main(int argc, char* argv[]) {
  auto showUsage = []() {
    printf("usage: usdg-converter.exe <command> <input.usd> [output.usdg]\n"
        "commands:\n"
        "    pack    package a USD stage into USDG.\n"
        "    unpack  unpackage a USDG stage back into USD.\n"
        "    update  make an incremental update for a USDG stage.\n"
        "    prune   prune packaged mod assets. \n"
        "\n"
        "pack command options:\n"
        "    -c      : Wipe the original USD attributes that have been baked for GPU.\n"
        "    -i N    : Image compression level N - [-1..12]. -1 to disable image processing.\n"
        "    -g N    : Geometry compression level N - [-1..12]. -1 to disable geometry processing.\n"
        "    -ip MB  : Place images into separate packages with approximately [MB] uncompressed size.\n"
        "    -gp MB  : Place geometry into separate packages with approximately [MB] uncompressed size.\n"
        "    -b N    : Maximum bone influence count per vertex. Default - 4.\n"
        "    -binary : Convert USDA into binary USD. Default - false.\n"
        "\n"
        "unpack command options:\n"
        "    -ni     : Do not extract images.\n"
        "\n"
        "update command options:\n"
        "    same as for pack command\n"
    );
  };

  if (argc < 3) {
    showUsage();
    return -1;
  }

  dxvk::util::RtxFileSys::init(".");
  Logger::initRtxLog();

  int mode = 0;
  UsdProcessor::Config config;

  int arg = 1;
  // command parse
  if (0 == strcmp(argv[arg], "pack")) {
    mode = 1;
  } else if (0 == strcmp(argv[arg], "unpack")) {
    mode = 2;
  } else if (0 == strcmp(argv[arg], "update")) {
    mode = 3;
  } else if (0 == strcmp(argv[arg], "prune")) {
    Logger::err("The update command is not implemented yet.");
    return -1;
    mode = 4;
  } else {
    Logger::err(str::format("Unknown command: ", argv[arg]));
    showUsage();
    return -1;
  }

  arg++;

  // Consumes the option's value (argv[arg + 1]), setting missingValue if it's absent.
  bool missingValue = false;
  auto nextInt = [&](int32_t& out) {
    ++arg;
    if (arg >= argc) {
      missingValue = true;
      return;
    }
    out = atoi(argv[arg]);
    ++arg;
  };

  // pack and update options parse
  while (mode != 2 && arg < argc) {
    if (0 == strcmp(argv[arg], "-i")) {
      nextInt(config.imageCompressionLevel);
    } else if (0 == strcmp(argv[arg], "-g")) {
      nextInt(config.geometryCompressionLevel);
    } else if (0 == strcmp(argv[arg], "-c")) {
      config.clearBakedData = true;
      ++arg;
    } else if (0 == strcmp(argv[arg], "-ip")) {
      nextInt(config.imagePkgSplitMB);
    } else if (0 == strcmp(argv[arg], "-gp")) {
      nextInt(config.geometryPkgSplitMB);
    } else if (0 == strcmp(argv[arg], "-b")) {
      nextInt(config.maxBoneCount);
    } else if (0 == strcmp(argv[arg], "-binary")) {
      config.convertToBinary = true;
      ++arg;
    } else {
      break;
    }

    if (missingValue) {
      Logger::err(str::format("Missing value for option: ", argv[arg - 1]));
      showUsage();
      return -1;
    }
  }

  // unpack options parse
  while (mode == 2 && arg < argc) {
    if (0 == strcmp(argv[arg], "-ni")) {
      config.extractImages = false;
      ++arg;
      continue;
    }

    break;
  }

  if (mode == 3) config.update = true;

  const std::string srcFilename = argv[arg];
  std::string dstFilename = arg + 1 < argc ? argv[arg + 1] : "";

  if (dstFilename.empty()) {
    std::filesystem::path path(srcFilename);
    path.replace_extension(mode != 2 ? ".usdg" : ".usd");

    dstFilename = path.string();
  }

  if (config.update) {
    Logger::info(str::format("Updating ", dstFilename, " from ", srcFilename));
  } else {
    Logger::info(str::format("Converting ", srcFilename, " to ", dstFilename));
  }

  std::unique_ptr<UsdProcessor> processor;
  try {
    if (mode != 2) {
      processor = std::make_unique<UsdgConverter<true>>(dstFilename, config);
    } else {
      processor = std::make_unique<UsdgConverter<false>>(dstFilename, config);
    }

    processor->process(srcFilename);
  } catch (const dxvk::DxvkError& e) {
    Logger::err(str::format("Fatal exception: ", e.message()));
    return -1;
  } catch (const std::runtime_error& e) {
    Logger::err(str::format("Fatal exception: ", e.what()));
    return -1;
  } catch (...) {
    Logger::err("Got an unknown fatal exception.");
    return -1;
  }

  return 0;
}
