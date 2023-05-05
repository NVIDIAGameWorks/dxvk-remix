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

#include "rtx_utils.h"
#include "rtx_materials.h"
#include "rtx_hashing.h"
#include "vulkan/vulkan_core.h"

#include <inttypes.h>
#include <vector>
#include <future>

namespace dxvk 
{
class RtInstance;
struct D3D9FixedFunctionVS;

using RasterBuffer = GeometryBuffer<Raster>;
using RaytraceBuffer = GeometryBuffer<Raytrace>;

constexpr uint32_t MaxClipPlanes = 6;
constexpr uint32_t kInvalidFrameIndex = UINT32_MAX;

// NOTE: Needed to move this here in order to avoid
// circular includes.  This probably requires a 
// general cleanup.
struct SkinningData {
  std::vector<Matrix4> pBoneMatrices;
  uint32_t numBones = 0;
  uint32_t numBonesPerVertex = 0;
  XXH64_hash_t boneHash = 0;
  uint32_t minBoneIndex = 0; // This is the smallest index of all bones actually used by vertex data

  void computeHash() {
    if (numBones > 0) {
      assert(minBoneIndex >= 0);
      const Matrix4* firstBone = &pBoneMatrices[minBoneIndex];
      assert(numBones > minBoneIndex);
      boneHash = XXH3_64bits(firstBone, (numBones - minBoneIndex) * sizeof(Matrix4));
    } else {
      boneHash = 0;
    }
  }

  SkinningData& operator=(const SkinningData& skinningData) {
    if (this != &skinningData) {
      pBoneMatrices = skinningData.pBoneMatrices;
      numBones = skinningData.numBones;
      numBonesPerVertex = skinningData.numBonesPerVertex;
      boneHash = skinningData.boneHash;
      minBoneIndex = skinningData.minBoneIndex;
    }
    return *this;
  }
};


// Stores the geometry data representing a raytracable object
// Valid until the object is destroyed.
struct RaytraceGeometry {
  // Cached hashes from draw call on last update
  GeometryHashes hashes;

  XXH64_hash_t lastBoneHash = 0;

  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
  VkCullModeFlags cullMode = VkCullModeFlags(0);
  VkFrontFace frontFace = VkFrontFace(0);

  RaytraceBuffer positionBuffer;
  RaytraceBuffer previousPositionBuffer;
  RaytraceBuffer normalBuffer;
  RaytraceBuffer texcoordBuffer;
  RaytraceBuffer color0Buffer;
  RaytraceBuffer indexBuffer;

  uint32_t positionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t previousPositionBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t normalBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t texcoordBufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t color0BufferIndex = kSurfaceInvalidBufferIndex;
  uint32_t indexBufferIndex = kSurfaceInvalidBufferIndex;

  Rc<DxvkBuffer> historyBuffer[2] = {nullptr};
  Rc<DxvkBuffer> indexCacheBuffer = nullptr;

  bool usesIndices() const { 
    return indexBuffer.defined();
  }

  uint32_t calculatePrimitiveCount() const {
    return (usesIndices() ? indexCount : vertexCount) / 3;
  }
};

struct AxisAlignBoundingBox {
  Vector3 minPos = { FLT_MAX, FLT_MAX, FLT_MAX };
  Vector3 maxPos = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
};

// Stores a snapshot of the geometry state for a draw call.
// WARNING: Usage is undefined after the drawcall this was 
//          generated from has finished executing on the GPU
struct RasterGeometry {
  GeometryHashes hashes;
  std::shared_future<GeometryHashes> futureGeometryHashes;

  // Actual vertex/index count (when applicable) as calculated by geo-engine
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;

  // Hashed values
  VkPrimitiveTopology topology = VkPrimitiveTopology(0);
  VkCullModeFlags cullMode = VkCullModeFlags(0);
  VkFrontFace frontFace = VkFrontFace(0);

  // Used by replacements mostly, to force the cull bit to that set by the geometry data
  bool forceCullBit = false;

  RasterBuffer positionBuffer;
  RasterBuffer normalBuffer;
  RasterBuffer texcoordBuffer;
  RasterBuffer color0Buffer;
  RasterBuffer indexBuffer;
  RasterBuffer blendWeightBuffer;
  RasterBuffer blendIndicesBuffer;

  AxisAlignBoundingBox boundingBox;
  std::shared_future<AxisAlignBoundingBox> futureBoundingBox;

  const XXH64_hash_t getHashForRule(const HashRule& rule) const {
    XXH64_hash_t hashResult = kEmptyHash;

    // TODO: Can be optimized to not iterate over all component indices
    for (uint32_t i = 0; i < (uint32_t)HashComponents::Count; i++) {
      const HashComponents component = (HashComponents) i;

      if (rule.test(component)) {
        if (hashResult == kEmptyHash)
          // For the first entry, we use the hash directly
          hashResult = hashes[component];
        else
          // For all other entries, we combine the hash via seeding
          hashResult = XXH64(&(hashes[component]), sizeof(XXH64_hash_t), hashResult);
      }
    }

    assert(hashResult != kEmptyHash);
    return hashResult;
  }


  const XXH64_hash_t getHashForRuleLegacy(const HashRule& rule) const {
    // Note: Only information relating to how the geometry is structured should be included here.
    XXH64_hash_t h = getHashForRule(rule);
    h = XXH64(&indexCount, sizeof(indexCount), h);
    h = XXH64(&vertexCount, sizeof(vertexCount), h);
    h = XXH64(&topology, sizeof(topology), h);
    const uint32_t vertexStride = positionBuffer.stride();
    h = XXH64(&vertexStride, sizeof(vertexStride), h);
    const VkIndexType indexType = indexBuffer.indexType();
    h = XXH64(&indexType, sizeof(indexType), h);
    return h;
  }

  bool usesIndices() const {
    return indexBuffer.defined();
  }

  bool isVertexDataInterleaved() const {
    if (normalBuffer.defined() && (!positionBuffer.matches(normalBuffer) || positionBuffer.stride() != normalBuffer.stride()))
      return false;

    if (texcoordBuffer.defined() && (!positionBuffer.matches(texcoordBuffer) || positionBuffer.stride() != texcoordBuffer.stride()))
      return false;

    if (color0Buffer.defined() && (!positionBuffer.matches(color0Buffer) || positionBuffer.stride() != color0Buffer.stride()))
      return false;

    return true;
  }

  bool areFormatsGpuFriendly() const {
    assert(positionBuffer.defined());

    if (positionBuffer.vertexFormat() != VK_FORMAT_R32G32B32_SFLOAT && positionBuffer.vertexFormat() != VK_FORMAT_R32G32B32A32_SFLOAT)
      return false;

    if (normalBuffer.defined() && (normalBuffer.vertexFormat() != VK_FORMAT_R32G32B32_SFLOAT && normalBuffer.vertexFormat() != VK_FORMAT_R32G32B32A32_SFLOAT))
      return false;

    if (texcoordBuffer.defined() && (texcoordBuffer.vertexFormat() != VK_FORMAT_R32G32_SFLOAT && texcoordBuffer.vertexFormat() != VK_FORMAT_R32G32B32_SFLOAT && texcoordBuffer.vertexFormat() != VK_FORMAT_R32G32B32A32_SFLOAT))
      return false;

    if (color0Buffer.defined() && (color0Buffer.vertexFormat() != VK_FORMAT_B8G8R8A8_UNORM))
      return false;

    return true;
  }

  bool isTopologyRaytraceReady() const {
    // Unsupported BVH builder topology
    if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP)
      return false;

    // Unsupported BVH builder topology
    if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
      return false;

    // No index buffer so must create one (BVH builder does support this mode, our RT code does not)
    if (indexCount == 0)
      return false;

    return true;
  }

  const void printDebugInfo(const char* name = "", uint32_t numTrisToPrint = 0) const {
    Logger::warn(str::format(
      "GeometryData ", name, " address: ", this,
      " vertexCount: ", vertexCount,
      " indexCount: ", indexCount,
      " topology: ", topology,
      " cullMode: ", cullMode,
      " frontFace: ", frontFace,
      " currentVertexHash: 0x", hashes[HashComponents::VertexPosition],
      " drawIndexHash: 0x", hashes[HashComponents::Indices], std::dec));

    // Print Triangles:
    if (numTrisToPrint > 0) {
      uint16_t* indexPtr = (uint16_t*) indexBuffer.mapPtr();
      for (uint32_t i = 0; i < indexCount && i < numTrisToPrint * 3; i += 3) {
        Vector3* position1 = (Vector3*) ((uint8_t*) positionBuffer.mapPtr() + positionBuffer.stride() * indexPtr[i]);
        Vector3* position2 = (Vector3*) ((uint8_t*) positionBuffer.mapPtr() + positionBuffer.stride() * indexPtr[i + 1]);
        Vector3* position3 = (Vector3*) ((uint8_t*) positionBuffer.mapPtr() + positionBuffer.stride() * indexPtr[i + 2]);
        Logger::warn(str::format(
          "[", std::setw(5), indexPtr[i], ", ", std::setw(5), indexPtr[i + 1], ", ", std::setw(5), indexPtr[i + 2], "] : ", std::setprecision(6),
          "(", std::setw(9), position1->x, ", ", std::setw(9), position1->y, ", ", std::setw(9), position1->z, "),   ",
          "(", std::setw(9), position2->x, ", ", std::setw(9), position2->y, ", ", std::setw(9), position2->z, "),   ",
          "(", std::setw(9), position3->x, ", ", std::setw(9), position3->y, ", ", std::setw(9), position3->z, "), "));
      }
    }
  }
};

struct GeometryBufferData {
  uint16_t* indexData;
  size_t indexStride;

  float* positionData;
  size_t positionStride;

  float* texcoordData;
  size_t texcoordStride;

  float* normalData;
  size_t normalStride;

  uint32_t* vertexColorData;
  size_t vertexColorStride;

  GeometryBufferData(const RasterGeometry& geometryData) {
    if (geometryData.indexBuffer.defined()) {
      constexpr size_t indexSize = sizeof(uint16_t);
      indexStride = geometryData.indexBuffer.stride() / indexSize;
      indexData = (uint16_t*) geometryData.indexBuffer.mapPtr();
    } else {
      indexStride = 0;
      indexData = nullptr;
    }

    if (geometryData.positionBuffer.defined()) {
      constexpr size_t positionSubElementSize = sizeof(float);
      positionStride = geometryData.positionBuffer.stride() / positionSubElementSize;
      positionData = (float*) geometryData.positionBuffer.mapPtr((size_t) geometryData.positionBuffer.offsetFromSlice());
    } else {
      positionStride = 0;
      positionData = nullptr;
    }

    if (geometryData.texcoordBuffer.defined()) {
      constexpr size_t texcoordSubElementSize = sizeof(float);
      texcoordStride = geometryData.texcoordBuffer.stride() / texcoordSubElementSize;
      texcoordData = (float*) geometryData.texcoordBuffer.mapPtr((size_t) geometryData.texcoordBuffer.offsetFromSlice());
    } else {
      texcoordStride = 0;
      texcoordData = nullptr;
    }

    if (geometryData.normalBuffer.defined()) {
      constexpr size_t normalSubElementSize = sizeof(float);
      normalStride = geometryData.normalBuffer.stride() / normalSubElementSize;
      normalData = (float*) geometryData.normalBuffer.mapPtr((size_t) geometryData.normalBuffer.offsetFromSlice());
    } else {
      normalStride = 0;
      normalData = nullptr;
    }

    if (geometryData.color0Buffer.defined()) {
      constexpr size_t colorSubElementSize = sizeof(uint32_t);
      vertexColorStride = geometryData.color0Buffer.stride() / colorSubElementSize;
      vertexColorData = (uint32_t*) geometryData.color0Buffer.mapPtr((size_t) geometryData.color0Buffer.offsetFromSlice());
    } else {
      vertexColorStride = 0;
      vertexColorData = nullptr;
    }
  }

  uint16_t getIndex(uint32_t i) const {
    return indexData[i * indexStride];
  }

  Vector3& getPosition(uint32_t index) const {
    return *(Vector3*) (positionData + index * positionStride);
  }

  Vector2& getTexCoord(uint32_t index) const {
    return *(Vector2*) (texcoordData + index * texcoordStride);
  }

  Vector3& getNormal(uint32_t index) const {
    return *(Vector3*) (normalData + index * normalStride);
  }

  uint32_t& getVertexColor(uint32_t index) const {
    return vertexColorData[index * vertexColorStride];
  }
};


struct DrawCallTransforms {
  Matrix4 objectToWorld = Matrix4();
  Matrix4 objectToView = Matrix4();
  Matrix4 worldToView = Matrix4();
  Matrix4 viewToProjection = Matrix4();
  Matrix4 textureTransform = Matrix4();
  bool enableClipPlane = false;
  Vector4 clipPlane = { 0.f };
  TexGenMode texgenMode = TexGenMode::None;

  void sanitize() {
    if (objectToWorld[3][3] == 0.f) objectToWorld[3][3] = 1.f;
    if (objectToView[3][3] == 0.f) objectToView[3][3] = 1.f;
    if (worldToView[3][3] == 0.f) worldToView[3][3] = 1.f;
  }
};

struct FogState {
  uint32_t mode = D3DFOG_NONE;
  Vector3 color = Vector3();
  float scale = 0.f;
  float end = 0.f;
  float density = 0.f;
};


struct DrawCallState {
  DrawCallState()
    : m_stencilEnabled(false) { }

  DrawCallState(
    const RasterGeometry& geometryData, const LegacyMaterialData& materialData, const DrawCallTransforms& transformData,
    const SkinningData& skinningData, const FogState& fogState, bool stencilEnabled) :
    m_geometryData { geometryData },
    m_materialData { materialData },
    m_transformData { transformData },
    m_skinningData { skinningData },
    m_fogState { fogState },
    m_stencilEnabled { stencilEnabled }
  {}

  DrawCallState(const DrawCallState& _input)
    : m_geometryData(_input.m_geometryData)
    , m_materialData(_input.m_materialData)
    , m_transformData(_input.m_transformData)
    , m_skinningData(_input.m_skinningData)
    , m_fogState(_input.m_fogState)
    , m_isSky(_input.m_isSky) { }

  DrawCallState& operator=(const DrawCallState& drawCallState) {
    if (this != &drawCallState) {
      m_geometryData = drawCallState.m_geometryData;
      m_materialData = drawCallState.m_materialData;
      m_transformData = drawCallState.m_transformData;
      m_skinningData = drawCallState.m_skinningData;
      m_fogState = drawCallState.m_fogState;
      m_stencilEnabled = drawCallState.m_stencilEnabled;
    }

    return *this;
  }

  // Note: This uses the original material for the hash, not the replaced material
  const XXH64_hash_t getHash(const HashRule& rule) const {
    return m_geometryData.getHashForRule(rule) ^ m_materialData.getHash();
  }

  [[deprecated("(REMIX-656): Remove this once we can transition content to new hash")]]
  const XXH64_hash_t getHashLegacy(const HashRule& rule) const {
    return m_geometryData.getHashForRuleLegacy(rule) ^ m_materialData.getHash();
  }

  const RasterGeometry& getGeometryData() const {
    return m_geometryData;
  }

  const LegacyMaterialData& getMaterialData() const {
    return m_materialData;
  }

  const DrawCallTransforms& getTransformData() const {
    return m_transformData;
  }

  const SkinningData& getSkinningState() const {
    return m_skinningData;
  }

  const FogState& getFogState() const {
    return m_fogState;
  }

  bool getStencilEnabledState() const {
    return m_stencilEnabled;
  }

  bool getIsSky() const {
    return m_isSky;
  }

  bool finalizePendingFutures() {
    // Bounding boxes (if enabled) will be finalized here, default is FLT_MAX bounds
    finalizeGeometryBoundingBox();

    // Geometry hashes are vital, and cannot be disabled, so its important we get valid data (hence the return type)
    return finalizeGeometryHashes();
  }

  bool hasTextureCoordinates() const {
    return getGeometryData().texcoordBuffer.defined() || getTransformData().texgenMode != TexGenMode::None;
  }

private:
  friend class RtxContext;

  bool finalizeGeometryHashes() {
    if (!m_geometryData.futureGeometryHashes.valid())
      return false;

    m_geometryData.hashes = m_geometryData.futureGeometryHashes.get();

    if (m_geometryData.hashes[HashComponents::VertexPosition] == kEmptyHash)
      throw DxvkError("Position hash should never be empty");

    return true;
  }

  void finalizeGeometryBoundingBox() {
    if (m_geometryData.futureBoundingBox.valid()) {
      m_geometryData.boundingBox = m_geometryData.futureBoundingBox.get();
    }
  }

  RasterGeometry m_geometryData;

  // Note: This represents the original material from the D3D9 side, which will always be a LegacyMaterialData
  // whereas the replacement material data used for rendering will be a full MaterialData.
  LegacyMaterialData m_materialData;

  DrawCallTransforms m_transformData;

  // Note: Set these pointers to nullptr when not used
  SkinningData m_skinningData;

  FogState m_fogState;
  bool m_stencilEnabled;

  // Note: used for early sky detection and removing sky draws from raytracing scene and camera detection.
  bool m_isSky = false;
};

 // A BLAS and its data buffer that can be pooled and used for various geometries
struct PooledBlas : public RcObject {
  Rc<DxvkAccelStructure> accelStructure;
  uint64_t accelerationStructureReference = 0;

  // Frame when this BLAS was last used in a TLAS
  uint32_t frameLastTouched = kInvalidFrameIndex;

  // Hash of a bound opacity micromap
  // Note: only used for tracking of OMMs for static BLASes
  XXH64_hash_t opacityMicromapSourceHash = kEmptyHash;

  PooledBlas(Rc<DxvkDevice> device);
  ~PooledBlas();
};

// Information about a geometry, such as vertex buffers, and possibly a static BLAS for that geometry
struct BlasEntry {
  DrawCallState input; 
  RaytraceGeometry modifiedGeometryData;

  // Frame when this geometry was seen for the first time
  uint32_t frameCreated = kInvalidFrameIndex;

  // Frame when this geometry was last used in a TLAS
  uint32_t frameLastTouched = kInvalidFrameIndex;

  // Frame when the vertex data of this geometry was last updated, used to detect static geometries
  uint32_t frameLastUpdated = kInvalidFrameIndex;

  Rc<PooledBlas> staticBlas;

  BlasEntry() = default;

  BlasEntry(const DrawCallState& input_)
    : input(input_) { }

  void cacheMaterial(const LegacyMaterialData& newMaterial) {
    if (input.getMaterialData().getHash() != newMaterial.getHash()) {
      m_materials.emplace(newMaterial.getHash(), newMaterial);
    }
  }

  const LegacyMaterialData& getMaterialData(XXH64_hash_t matHash) const {
    if (input.getMaterialData().getHash() == matHash) {
      return input.getMaterialData();
    }
    auto iter = m_materials.find(matHash);
    if (iter != m_materials.end()) {
      return iter->second;
    }
    assert(false); // tried to get a material that the BlasEntry doesn't know about.
    return input.getMaterialData();
  }

  void clearMaterialCache() {
    m_materials.clear();
  }

  void linkInstance(const RtInstance* instance) {
    m_linkedInstances.push_back(instance);
  }

  void unlinkInstance(const RtInstance* instance) {
    auto& it = std::find(m_linkedInstances.begin(), m_linkedInstances.end(), instance);
    if (it != m_linkedInstances.end()) {
      // Swap & pop - faster than "erase", but doesn't preserve order, which is fine here.
      std::swap(*it, m_linkedInstances.back());
      m_linkedInstances.pop_back();
    } else {
      Logger::err("Tried to unlink an instance, which was never linked!");
    }
  }

  const std::vector<const RtInstance*>& getLinkedInstances() const { return m_linkedInstances; }

private:
  std::vector<const RtInstance*> m_linkedInstances;
  std::unordered_map<XXH64_hash_t, LegacyMaterialData> m_materials;
};

// Top-level acceleration structure
struct Tlas {
  enum Type : size_t {
    Opaque,
    Unordered,

    Count
  };

  VkBuildAccelerationStructureFlagsKHR flags = 0;
  Rc<DxvkAccelStructure> accelStructure = nullptr;
  Rc<DxvkAccelStructure> previousAccelStructure = nullptr;
};



struct DxvkRtxLegacyState {
  bool alphaTestEnabled = false;
  uint8_t alphaTestReferenceValue = 0;
  VkCompareOp alphaTestCompareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS;
  // Material color source
  DxvkRtColorSource diffuseColorSource = DxvkRtColorSource::None;
  DxvkRtColorSource specularColorSource = DxvkRtColorSource::None;
  uint32_t tFactor = 0xffffffff; // Value for D3DRS_TEXTUREFACTOR, default value of is opaque white
};

struct DxvkRtxTextureStageState {
  DxvkRtTextureArgSource colorArg1Source = DxvkRtTextureArgSource::Texture;
  DxvkRtTextureArgSource colorArg2Source = DxvkRtTextureArgSource::Diffuse;
  DxvkRtTextureOperation colorOperation = DxvkRtTextureOperation::Modulate;
  DxvkRtTextureArgSource alphaArg1Source = DxvkRtTextureArgSource::Texture;
  DxvkRtTextureArgSource alphaArg2Source = DxvkRtTextureArgSource::Diffuse;
  DxvkRtTextureOperation alphaOperation = DxvkRtTextureOperation::Modulate;
  uint32_t texcoordIndex = 0;
  uint32_t transformFlags = 0;
  Matrix4 transform;
};

enum class RtxGeometryStatus {
  Ignored,
  RayTraced,
  Rasterized
};

struct DxvkRaytracingInstanceState {
  RasterGeometry geometry;
  RtxGeometryStatus geometryStatus;
  std::shared_future<SkinningData> futureSkinningData;
  bool useProgrammableVS;
  bool useProgrammablePS;
  Matrix4 world;
  Matrix4 view;
  Matrix4 projection;
  Rc<DxvkBuffer> vsFixedFunctionCB;
  uint32_t colorTextureSlot = UINT32_MAX;
  uint32_t colorTextureSlot2 = UINT32_MAX;
  DxvkRtxLegacyState legacyState;
  DxvkRtxTextureStageState texStage;
  uint32_t clipPlaneMask = 0;
  Vector4 clipPlanes[MaxClipPlanes] = { 0.f };
};

} // namespace dxvk
