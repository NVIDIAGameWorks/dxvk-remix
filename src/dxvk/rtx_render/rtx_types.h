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

#include "rtx_constants.h"
#include "rtx_utils.h"
#include "rtx_materials.h"
#include "rtx_hashing.h"
#include "rtx_camera.h"
#include "vulkan/vulkan_core.h"
#include "../../util/util_bounding_box.h"
#include "../../util/util_threadpool.h"
#include "../../util/util_spatial_map.h"

#include <inttypes.h>
#include <vector>
#include <future>

using remixapi_MaterialHandle = struct remixapi_MaterialHandle_T*;
using remixapi_MeshHandle = struct remixapi_MeshHandle_T*;

namespace dxvk 
{
class RtCamera;
class RtInstance;
struct RtLight;
class GraphInstance;
struct D3D9FixedFunctionVS;
struct D3D9FixedFunctionPS;
struct ReplacementInstance;

using RasterBuffer = GeometryBuffer<Raster>;
using RaytraceBuffer = GeometryBuffer<Raytrace>;

// DLFG async compute overlap: max of 2 frames in flight
// (set to 1 to serialize graphics and async compute queues)
constexpr uint32_t kDLFGMaxGPUFramesInFlight = 2;

// A container for the runtime instance that maps to a prim in a replacement heirarchy.
class PrimInstance {
public:
  enum class Type : uint8_t {
    Instance,
    Light,
    Graph,
    None
  };
  // Use `Entity()` to create a nullptr Entity.
  PrimInstance() {}

  // Default copy/move/destructors are fine - this just contains typed weak pointers.
  PrimInstance(const PrimInstance&) = default;
  PrimInstance(PrimInstance&&) noexcept = default;
  PrimInstance& operator=(const PrimInstance&) = default;
  PrimInstance& operator=(PrimInstance&&) noexcept = default;
  ~PrimInstance() = default;

  // Instance constructor, getter
  explicit PrimInstance(RtInstance* instance);
  RtInstance* getInstance() const;

  // Light constructor, getter
  explicit PrimInstance(RtLight* light);
  RtLight* getLight() const;

  // Graph constructor, getter
  explicit PrimInstance(GraphInstance* graph);
  GraphInstance* getGraph() const;

  // Untyped utilities.
  PrimInstance(void* owner, Type type);

  Type getType() const;
  void* getUntyped() const;
  void setReplacementInstance(ReplacementInstance* replacementInstance, size_t replacementIndex);

private:
  union EntityPtr {
    void* untyped = nullptr;
    RtInstance* instance;
    RtLight* light;
    GraphInstance* graph;
  } m_ptr;
  Type m_type = Type::None;
};
std::ostream& operator << (std::ostream& os, PrimInstance::Type type);

struct ReplacementInstance {
  // Lifecycle note:
  // Currently, ReplacementInstances are created the first time a given replaced draw call
  // is rendered.  A single entity (a light or instance) is designated as the 'root'.
  // When that entity is destroyed, the ReplacementInstance is destroyed.
  // Unfortunately, lights and instances aren't always destroyed at the same time, or
  // in the same order they were created.  To accomodate that, when non-root entities
  // are deleted, they remove themselves from the `entities` vector.  Similarly, when
  // the root is deleted, all entities remaining in the vector will have their pointer
  // to the ReplacementInstance set to nullptr.
  // TODO(REMIX-4226): In the future, draw calls should be tracked and destroyed based
  // on the pre-replacement draw call, so that everything in a ReplacementInstance gets
  // destroyed at the same time.  When that change is made, the original tracked draw
  // call should own this ReplacementInstance.

  static constexpr uint32_t kInvalidReplacementIndex = UINT32_MAX;

  ~ReplacementInstance();

  void clear();

  std::vector<PrimInstance> prims;
  PrimInstance root;

  void setup(PrimInstance newRoot, size_t numPrims);
};

// Wrapper utility to share the code for handling replacementInstance ownership.
class PrimInstanceOwner {
public:
  PrimInstanceOwner() = default;
  // NOTE: primInstanceOwner is not safe to copy - the RtInstance, RtLight, etc that holds the PrimInstanceOwner
  //       would have a different address after copying, so the PrimInstanceOwner would point to the wrong object.
  PrimInstanceOwner(const PrimInstanceOwner& other) = delete;
  PrimInstanceOwner& operator=(const PrimInstanceOwner& other) = delete;

  ~PrimInstanceOwner() {
    // m_replacementInstance should always be properly cleaned up before the PrimInstanceOwner 
    // is destroyed. If this is hit, then whatever deleted the object holding the
    // primInstanceOwner needs to call setReplacementInstance(nullptr...) before doing that 
    // deletion.  If not, there will probably be use-after-free bugs later on.
    assert(m_replacementInstance == nullptr);
  }

  bool isRoot(const void* owner) const;
  void setReplacementInstance(ReplacementInstance* replacementInstance, size_t replacementIndex, void* owner, PrimInstance::Type type);
  ReplacementInstance* getOrCreateReplacementInstance(void* owner, PrimInstance::Type type, size_t index, size_t numPrims);
  ReplacementInstance* getReplacementInstance() const { return m_replacementInstance; }
  size_t getReplacementIndex() const { return m_replacementIndex; }
  bool isSubPrim() const {
    if (m_replacementInstance == nullptr) {
      return false;
    } else {
      return m_replacementIndex != ReplacementInstance::kInvalidReplacementIndex &&
        m_replacementInstance->root.getUntyped() != m_replacementInstance->prims[m_replacementIndex].getUntyped();
    }
  }
private:
  ReplacementInstance* m_replacementInstance = nullptr;
  size_t m_replacementIndex = ReplacementInstance::kInvalidReplacementIndex;
};

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

// Stores a snapshot of the geometry state for a draw call.
// WARNING: Usage is undefined after the drawcall this was 
//          generated from has finished executing on the GPU
struct RasterGeometry {
  GeometryHashes hashes;
  Future<GeometryHashes> futureGeometryHashes;

  // Actual vertex/index count (when applicable) as calculated by geo-engine
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;

  // Copy of the bones per vertex from SkinningState.
  // This allows replacements to have different values from the original.
  uint32_t numBonesPerVertex = 0;

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

  AxisAlignedBoundingBox boundingBox;
  Future<AxisAlignedBoundingBox> futureBoundingBox;

  remixapi_MaterialHandle externalMaterial = nullptr;

  template<uint32_t rule>
  const XXH64_hash_t getHashForRule() const {
    return hashes.getHashForRule<rule>();
  }

  const XXH64_hash_t getHashForRule(const HashRule& rule) const {
    return hashes.getHashForRule(rule);
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
  
  uint32_t calculatePrimitiveCount() const;

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

    if (normalBuffer.defined() && (normalBuffer.vertexFormat() != VK_FORMAT_R32G32B32_SFLOAT && normalBuffer.vertexFormat() != VK_FORMAT_R32G32B32A32_SFLOAT && normalBuffer.vertexFormat() != VK_FORMAT_R32_UINT))
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
      constexpr size_t normalSubElementSize = sizeof(std::uint32_t);
      normalStride = geometryData.normalBuffer.stride() / normalSubElementSize;
      normalData = (float*) geometryData.normalBuffer.mapPtr((size_t) geometryData.normalBuffer.offsetFromSlice());
    } else {
      normalStride = 0;
      normalData = nullptr;
    }

    if (geometryData.color0Buffer.defined()) {
      constexpr size_t colorSubElementSize = sizeof(std::uint32_t);
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

  uint32_t getIndex32(uint32_t i) const {
    return (uint32_t)indexData[i * indexStride];
  }

  Vector3& getPosition(uint32_t index) const {
    return *(Vector3*) (positionData + index * positionStride);
  }

  Vector2& getTexCoord(uint32_t index) const {
    return *(Vector2*) (texcoordData + index * texcoordStride);
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
  Vector4 clipPlane{ 0.f };
  TexGenMode texgenMode = TexGenMode::None;
  const std::vector<Matrix4>* instancesToObject = nullptr;

  void sanitize() {
    if (objectToWorld[3][3] == 0.f) objectToWorld[3][3] = 1.f;
    if (objectToView[3][3] == 0.f) objectToView[3][3] = 1.f;
    if (worldToView[3][3] == 0.f) worldToView[3][3] = 1.f;
  }

  Matrix4 calcFirstInstanceObjectToWorld() const {
    if (instancesToObject && !instancesToObject->empty()) {
      return objectToWorld * (*instancesToObject)[0];
    } else {
      return objectToWorld;
    }
  }
};

struct FogState {
  uint32_t mode = D3DFOG_NONE;
  Vector3 color = Vector3();
  float scale = 0.f;
  float end = 0.f;
  float density = 0.f;

  XXH64_hash_t getHash() const {
    return XXH3_64bits(this, sizeof(FogState));
  }
};

enum class InstanceCategories : uint32_t {
  WorldUI,
  WorldMatte,
  Sky,
  Ignore,
  IgnoreLights,
  IgnoreAntiCulling,
  IgnoreMotionBlur,
  IgnoreOpacityMicromap,
  IgnoreAlphaChannel,
  Hidden,
  Particle,
  Beam,
  DecalStatic,
  DecalDynamic,
  DecalSingleOffset,
  DecalNoOffset,
  AlphaBlendToCutout,
  Terrain,
  AnimatedWater,
  ThirdPersonPlayerModel,
  ThirdPersonPlayerBody,
  IgnoreBakedLighting,
  IgnoreTransparencyLayer,
  ParticleEmitter,

  Count,
};

using CategoryFlags = Flags<InstanceCategories>;

#define DECAL_CATEGORY_FLAGS InstanceCategories::DecalStatic, InstanceCategories::DecalDynamic, InstanceCategories::DecalSingleOffset, InstanceCategories::DecalNoOffset

struct DrawCallState {
  DrawCallState() = default;
  DrawCallState(const DrawCallState& _input) = default;
  DrawCallState& operator=(const DrawCallState& drawCallState) = default;

  // Note: This uses the original material for the hash, not the replaced material
  const XXH64_hash_t getHash(const HashRule& rule) const {
    return geometryData.getHashForRule(rule) ^ materialData.getHash();
  }

  [[deprecated("(REMIX-656): Remove this once we can transition content to new hash")]]
  const XXH64_hash_t getHashLegacy(const HashRule& rule) const {
    return geometryData.getHashForRuleLegacy(rule) ^ materialData.getHash();
  }

  const RasterGeometry& getGeometryData() const {
    return geometryData;
  }

  const LegacyMaterialData& getMaterialData() const {
    return materialData;
  }

  const DrawCallTransforms& getTransformData() const {
    return transformData;
  }

  const SkinningData& getSkinningState() const {
    return skinningData;
  }

  const FogState& getFogState() const {
    return fogState;
  }

  const CategoryFlags getCategoryFlags() const {
    return categories;
  }

  bool finalizePendingFutures(const RtCamera* pLastCamera);

  bool hasTextureCoordinates() const {
    return getGeometryData().texcoordBuffer.defined() || getTransformData().texgenMode != TexGenMode::None;
  }

  bool stencilEnabled = false;

  // Camera type associated with the draw call
  CameraType::Enum cameraType = CameraType::Unknown;

  // Uses programmamble VS/PS
  bool usesVertexShader = false, usesPixelShader = false;

  // Contains valid values only if usesVertex/PixelShader is set
  DxsoProgramInfo programmableVertexShaderInfo;
  DxsoProgramInfo programmablePixelShaderInfo;

  float minZ = 0.0f;
  float maxZ = 1.0f;

  bool zWriteEnable = false;
  bool zEnable = false;

  uint32_t drawCallID = 0;

  bool isDrawingToRaytracedRenderTarget = false;
  bool isUsingRaytracedRenderTarget = false;

  void setupCategoriesForTexture();
  void setupCategoriesForGeometry();
  void setupCategoriesForHeuristics(uint32_t prevFrameSeenCamerasCount,
                                    std::vector<Vector3>& seenCameraPositions);

  template<typename... InstanceCategories>
  bool testCategoryFlags(InstanceCategories... cat) const { return categories.any(cat...); }

  void printDebugInfo(const char* name = "") const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "DrawCallState ", name, "\n",
      "  address: ", this, "\n",
      "  drawCallID: ", drawCallID, "\n",
      "  cameraType: ", static_cast<int>(cameraType), "\n",
      "  usesVertexShader: ", usesVertexShader, "\n",
      "  usesPixelShader: ", usesPixelShader, "\n",
      "  stencilEnabled: ", stencilEnabled, "\n",
      "  zWriteEnable: ", zWriteEnable, "\n",
      "  zEnable: ", zEnable, "\n",
      "  minZ: ", minZ, "\n",
      "  maxZ: ", maxZ, "\n",
      "  isDrawingToRaytracedRenderTarget: ", isDrawingToRaytracedRenderTarget, "\n",
      "  isUsingRaytracedRenderTarget: ", isUsingRaytracedRenderTarget, "\n",
      "  categoryFlags: ", categories.raw(), "\n",
      "  hasTextureCoordinates: ", hasTextureCoordinates(), "\n",
      "  materialHash: 0x", std::hex, materialData.getHash(), std::dec));
    
    // Print geometry info
    Logger::warn("=== Geometry Info ===");
    Logger::warn(str::format(
      "  vertexCount: ", geometryData.vertexCount, "\n",
      "  indexCount: ", geometryData.indexCount, "\n",
      "  numBonesPerVertex: ", geometryData.numBonesPerVertex, "\n",
      "  topology: ", static_cast<int>(geometryData.topology), "\n",
      "  cullMode: ", static_cast<int>(geometryData.cullMode), "\n",
      "  frontFace: ", static_cast<int>(geometryData.frontFace), "\n",
      "  forceCullBit: ", geometryData.forceCullBit, "\n",
      "  externalMaterial: ", (geometryData.externalMaterial != nullptr ? "valid" : "null")));
    
    // Print transform info
    Logger::warn("=== Transform Info ===");
    Logger::warn(str::format(
      "  enableClipPlane: ", transformData.enableClipPlane, "\n",
      "  clipPlane: (", transformData.clipPlane.x, ", ", transformData.clipPlane.y, ", ", transformData.clipPlane.z, ", ", transformData.clipPlane.w, ")"));
    
    // Print skinning info
    Logger::warn("=== Skinning Info ===");
    Logger::warn(str::format(
      "  numBones: ", skinningData.numBones, "\n",
      "  numBonesPerVertex: ", skinningData.numBonesPerVertex, "\n",
      "  minBoneIndex: ", skinningData.minBoneIndex, "\n",
      "  boneHash: 0x", std::hex, skinningData.boneHash, std::dec));
    
    // Print fog info
    Logger::warn("=== Fog Info ===");
    Logger::warn(str::format(
      "  fogMode: ", fogState.mode, "\n",
      "  fogColor: (", fogState.color.x, ", ", fogState.color.y, ", ", fogState.color.z, ")\n",
      "  fogScale: ", fogState.scale, "\n",
      "  fogEnd: ", fogState.end, "\n",
      "  fogDensity: ", fogState.density));
    
    // Print material info
    Logger::warn("=== Material Info ===");
    materialData.printDebugInfo("(from DrawCallState)");
#endif
  }

private:
  friend class RtxContext;
  friend class SceneManager;
  friend struct D3D9Rtx;
  friend class TerrainBaker;
  friend struct RemixAPIPrivateAccessor;
  friend class RtxParticleSystemManager;

  bool finalizeGeometryHashes();
  void finalizeGeometryBoundingBox();
  void finalizeSkinningData(const RtCamera* pLastCamera);

  // NOTE: 'setCategory' can only add a category, it will not unset a bit
  void setCategory(InstanceCategories category, bool set);
  void removeCategory(InstanceCategories category);

  RasterGeometry geometryData;

  // Note: This represents the original material from the D3D9 side, which will always be a LegacyMaterialData
  // whereas the replacement material data used for rendering will be a full MaterialData.
  LegacyMaterialData materialData;

  DrawCallTransforms transformData;

  // Note: Set these pointers to nullptr when not used
  SkinningData skinningData;
  Future<SkinningData> futureSkinningData;

  FogState fogState;

  CategoryFlags categories = 0;
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

  // Keep a copy of the build info so we can validate BLAS update compatibility
  VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {};
  std::vector<uint32_t> primitiveCounts {};

  explicit PooledBlas();
  ~PooledBlas();
};

// Information about a geometry, such as vertex buffers, and possibly a static BLAS for that geometry
struct BlasEntry {
  // input contains legacy or replacements (the data can be on CPU or GPU)
  //  - Data on CPU is guaranteed to be alive during draw call's submission.
  //  - Data can be made alive on CPU for longer with an explicit ref hold on it
  //  - For shader based games the data may contain various unsupported formats a game might deliver the data in. 
  //    That is converted and optimized in RtxGeometryUtils::interleaveGeometry. 
  //    Fixed function games always use supported buffer formats/encodings etc...
  DrawCallState input; 
  // modifiedGeometryData contains the same geometry as "input" but it (may) have been transformed (i.e.interleaved vertex data, 
  // converted to optimal vertex formats [we prefer float32], will always be a triangle list and could be skinned)
  // - Data is on GPU 
  // - Data is not directly mappable on CPU
  RaytraceGeometry modifiedGeometryData;

  // Frame when this geometry was seen for the first time
  uint32_t frameCreated = kInvalidFrameIndex;

  // Frame when this geometry was last used in a TLAS
  uint32_t frameLastTouched = kInvalidFrameIndex;

  // Frame when the vertex data of this geometry was last updated, used to detect static geometries
  uint32_t frameLastUpdated = kInvalidFrameIndex;

  using InstanceMap = SpatialMap<RtInstance>;

  Rc<PooledBlas> dynamicBlas = nullptr;

  std::vector<VkAccelerationStructureGeometryKHR> buildGeometries;
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges;

  BlasEntry() = default;

  BlasEntry(const DrawCallState& input_);

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

  void linkInstance(RtInstance* instance) {
    m_linkedInstances.push_back(instance);
  }

  void unlinkInstance(RtInstance* instance);

  const std::vector<RtInstance*>& getLinkedInstances() const { return m_linkedInstances; }
  InstanceMap& getSpatialMap() { return m_spatialMap; }
  const InstanceMap& getSpatialMap() const { return m_spatialMap; }

  void rebuildSpatialMap();

  void printDebugInfo(const char* name = "") const {
#ifdef REMIX_DEVELOPMENT
    Logger::warn(str::format(
      "BlasEntry ", name, "\n",
      "  address: ", this, "\n",
      "  frameCreated: ", frameCreated, "\n",
      "  frameLastTouched: ", frameLastTouched, "\n",
      "  frameLastUpdated: ", frameLastUpdated, "\n",
      "  vertexCount: ", modifiedGeometryData.vertexCount, "\n",
      "  indexCount: ", modifiedGeometryData.indexCount, "\n",
      "  linkedInstances: ", m_linkedInstances.size(), "\n",
      "  cachedMaterials: ", m_materials.size(), "\n",
      "  buildGeometries: ", buildGeometries.size(), "\n",
      "  buildRanges: ", buildRanges.size(), "\n",
      "  dynamicBlas: ", (dynamicBlas != nullptr ? "valid" : "null")));
    
    // Print main material info
    Logger::warn("=== Main Material Info ===");
    input.getMaterialData().printDebugInfo("(main)");
    
    // Print cached materials info
    if (!m_materials.empty()) {
      Logger::warn("=== Cached Materials Info ===");
      for (const auto& [hash, material] : m_materials) {
        Logger::warn(str::format("Cached Material Hash: 0x", std::hex, hash, std::dec));
        material.printDebugInfo("(cached)");
      }
    }
#endif
  }

private:
  std::vector<RtInstance*> m_linkedInstances;
  InstanceMap m_spatialMap;
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

enum class RtxGeometryStatus {
  Ignored,
  RayTraced,
  Rasterized
};

struct DxvkRaytracingInstanceState {
  Rc<DxvkBuffer> vsFixedFunctionCB;
  Rc<DxvkBuffer> psSharedStateCB;
  Rc<DxvkBuffer> vertexCaptureCB;
};

enum class RtxFramePassStage {
  FrameBegin,
  Volumetrics,
  VolumeIntegrateRestirInitial,
  VolumeIntegrateRestirVisible,
  VolumeIntegrateRestirTemporal,
  VolumeIntegrateRestirSpatialResampling,
  VolumeIntegrateRaytracing,
  GBufferPrimaryRays,
  ReflectionPSR,
  TransmissionPSR,
  RTXDI_InitialTemporalReuse,
  RTXDI_SpatialReuse,
  NEE_Cache,
  DirectIntegration,
  RTXDI_ComputeGradients,
  IndirectIntegration,
  NEE_Integration,
  NRC,
  RTXDI_FilterGradients,
  RTXDI_ComputeConfidence,
  ReSTIR_GI_TemporalReuse,
  ReSTIR_GI_SpatialReuse,
  ReSTIR_GI_FinalShading,
  Demodulate,
  NRD,
  CompositionAlphaBlend,
  Composition,
  DLSS,
  DLSSRR,
  NIS,
  XeSS,
  FSR,
  TAA,
  DustParticles,
  Bloom,
  PostFX,
  AutoExposure_Histogram,
  AutoExposure_Exposure,
  ToneMapping,
  FrameEnd
};

enum class RtxTextureExtentType {
  DownScaledExtent,
  TargetExtent,
  Custom
};

// Category of texture format base on the doc: https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap46.html#formats-compatibility-classes
// Note: We currently only categorize the uncompressed color textures
enum class RtxTextureFormatCompatibilityCategory : uint32_t {
  Color_Format_8_Bits,
  Color_Format_16_Bits,
  Color_Format_32_Bits,
  Color_Format_64_Bits,
  Color_Format_128_Bits,
  Color_Format_256_Bits,

  Count,
  InvalidFormatCompatibilityCategory = UINT32_MAX
};

} // namespace dxvk
