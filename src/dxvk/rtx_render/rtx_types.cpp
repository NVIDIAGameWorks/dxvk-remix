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

#include "rtx_types.h"
#include "rtx_options.h"
#include "rtx_terrain_baker.h"
#include "rtx_instance_manager.h"
#include "rtx_light_manager.h"
#include "graph/rtx_graph_instance.h"
#include "dxvk_scoped_annotation.h"

namespace dxvk {

  // Instance constructor, getter, and assignment operator
  PrimInstance::PrimInstance(RtInstance* instance) : m_type(Type::Instance) {
    m_ptr.instance = instance;
  }
  RtInstance* PrimInstance::getInstance() const {
    if (m_type != Type::Instance) {
      return nullptr;
    }
    return m_ptr.instance;
  }

  // Light constructor, getter, and assignment operator
  PrimInstance::PrimInstance(RtLight* light) : m_type(Type::Light) {
    m_ptr.light = light;
  }
  RtLight* PrimInstance::getLight() const {
    if (m_type != Type::Light) {
      return nullptr;
    }
    return m_ptr.light;
  }

  // Graph constructor, getter, and assignment operator
  PrimInstance::PrimInstance(GraphInstance* graph) : m_type(Type::Graph) {
    m_ptr.graph = graph;
  }
  GraphInstance* PrimInstance::getGraph() const {
    if (m_type != Type::Graph) {
      return nullptr;
    }
    return m_ptr.graph;
  }

  PrimInstance::Type PrimInstance::getType() const {
    if (m_ptr.untyped == nullptr) {
      return Type::None;
    }
    return m_type;
  }

  PrimInstance::PrimInstance(void* owner, Type type) : m_type(type) {
    m_ptr.untyped = owner;
  }

  void* PrimInstance::getUntyped() const {
    return m_ptr.untyped;
  }

  void PrimInstance::setReplacementInstance(ReplacementInstance* replacementInstance, size_t replacementIndex) {
    PrimInstanceOwner* prim = nullptr;
    if (m_type == Type::Instance) {
      prim = &m_ptr.instance->getPrimInstanceOwner();
    } else if (m_type == Type::Light) {
      prim = &m_ptr.light->getPrimInstanceOwner();
    } else if (m_type == Type::Graph) {
      prim = &m_ptr.graph->getPrimInstanceOwner();
    }

    if (prim) {
      prim->setReplacementInstance(replacementInstance, replacementIndex, m_ptr.untyped, m_type);
    }
  }

  std::ostream& operator << (std::ostream& os, PrimInstance::Type type) {
    switch (type) {
      ENUM_NAME(PrimInstance::Type::Instance);
      ENUM_NAME(PrimInstance::Type::Light);
      ENUM_NAME(PrimInstance::Type::Graph);
      ENUM_NAME(PrimInstance::Type::None);
    }
    return os << static_cast<uint8_t>(type);
  }

  ReplacementInstance* PrimInstanceOwner::getOrCreateReplacementInstance(void* owner, PrimInstance::Type type, size_t index,size_t numPrims) {
    if (m_replacementInstance == nullptr) {
      ReplacementInstance* replacement = new ReplacementInstance();
      replacement->setup(PrimInstance(owner, type), numPrims);
      setReplacementInstance(replacement, index, owner, type);
    } else if (m_replacementInstance->prims.size() != numPrims) {
      // Number of prims changing generally means a new replacement asset has loaded in.
      // Need to unlink the old instances, and either re-link them (if they are returned as 
      // similar by findSimilarInstances) or create new ones.
      ReplacementInstance* replacement = m_replacementInstance;
      replacement->clear();
      replacement->setup(PrimInstance(owner, type), numPrims);
      setReplacementInstance(replacement, index, owner, type);
    }
    return m_replacementInstance;
  }

  ReplacementInstance::~ReplacementInstance() {
    clear();
  }

  void ReplacementInstance::clear() {
    // clear up all references to this ReplacementInstance.
    for (size_t i = 0; i < prims.size(); i++) {
      RtInstance* subInstance = prims[i].getInstance();
      if (subInstance) {
        subInstance->markForGarbageCollection();
      }
      GraphInstance* graphInstance = prims[i].getGraph();
      if (graphInstance) {
        graphInstance->removeInstance();
      }
      prims[i].setReplacementInstance(nullptr, kInvalidReplacementIndex);
    }
  }

  void ReplacementInstance::setup(PrimInstance newRoot, size_t numPrims) {
    prims.resize(numPrims);
    root = newRoot;
  }
  
  bool PrimInstanceOwner::isRoot(void* owner) const {
    return m_replacementInstance != nullptr
      && m_replacementIndex != ReplacementInstance::kInvalidReplacementIndex
      && m_replacementInstance->root.getUntyped() == owner;
  }

  void PrimInstanceOwner::setReplacementInstance(ReplacementInstance* replacementInstance, size_t replacementIndex, void* owner, PrimInstance::Type type) {
    // Early out if this is just re-applying the same values.
    if (m_replacementInstance == replacementInstance) {
      assert(m_replacementIndex == replacementIndex && "single prim is being set to multiple replacement indices.");
      return;
    }

    // Then check if the owner is already in a replacement:
    if (m_replacementInstance && m_replacementIndex != ReplacementInstance::kInvalidReplacementIndex) {
      
      // Inside a replacement, check if it's the root:
      if (isRoot(owner)) {
        // This is the root of a replacement being deleted.
        // Clear the root, and delete the replacementInstance.
        // the ReplacementInstance destructor will call this function again, which will
        // actually clear m_replacementInstance and m_replacementIndex.
        m_replacementInstance->root = PrimInstance();
        delete m_replacementInstance;
        return;
      }

      // Next, remove the prim from the replacementInstance.
      PrimInstance& prim = m_replacementInstance->prims[m_replacementIndex];
      if (prim.getType() == type && prim.getUntyped() == owner) {
        // clear up the old reference to this owner
        prim = PrimInstance();
      } else {
        // The prim believed it was in a slot, but something else was actually there.
        // This is a sign that something went wrong earlier, but shouldn't cause problems itself.
        assert(false && "PrimInstance was not properly removed from its replacementInstance before something else took its place.");
      }
    }

    // Set this owner to the new replacementInstance.
    m_replacementInstance = replacementInstance;
    m_replacementIndex = replacementIndex;

    // Inform the replacementInstance that this owner is now in it.
    if (m_replacementInstance && replacementIndex != ReplacementInstance::kInvalidReplacementIndex) {
      PrimInstance& prim = m_replacementInstance->prims[replacementIndex];
      if (prim.getType() != type && prim.getType() != PrimInstance::Type::None) {
        // While specific pointers may change, the type of a slot should never change.
        assert(false && "Trying to assign a primInstance to a replacementInstance slot that was not the same type.");
        m_replacementInstance = nullptr;
        m_replacementIndex = ReplacementInstance::kInvalidReplacementIndex;
        return;
      } else if (prim.getUntyped() != nullptr && prim.getUntyped() != owner) {
        // Another owner is already in this spot.  Clean that up properly before overriding it.
        if (m_replacementInstance->root.getUntyped() == prim.getUntyped()) {
          // Replacing the old root.  Shouldn't happen, but if it does we would want to
          // update the root before clearing the old root, to avoid triggering garbage collection.
          m_replacementInstance->root = PrimInstance(owner, type);
        }
        prim.setReplacementInstance(nullptr, ReplacementInstance::kInvalidReplacementIndex);
        assert(false && "PrimInstance was not properly cleaned up before being replaced.");
      }
      prim = PrimInstance(owner, type);
    }
  }

  uint32_t RasterGeometry::calculatePrimitiveCount() const {
    const uint32_t elementCount = usesIndices() ? indexCount : vertexCount;
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return elementCount / 3;

    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return elementCount >= 3
        ? elementCount - 2
        : 0;

    default:
      assert(!"Unsupported primitive topology");
      return UINT32_MAX;
    }
  }

  bool DrawCallState::finalizePendingFutures(const RtCamera* pLastCamera) {
    ScopedCpuProfileZone();
    // Geometry hashes are vital, and cannot be disabled, so its important we get valid data (hence the return type)
    const bool valid = finalizeGeometryHashes();
    if (valid) {
      // Bounding boxes (if enabled) will be finalized here, default is FLT_MAX bounds
      finalizeGeometryBoundingBox();

      // Skinning processing will be finalized here, if object requires skinning
      finalizeSkinningData(pLastCamera);

      // Update any categories that require geometry hash
      setupCategoriesForGeometry();

      return true;
    }

    return false;
  }

  bool DrawCallState::finalizeGeometryHashes() {
    if (!geometryData.futureGeometryHashes.valid()) {
      return false;
    }

    geometryData.hashes = geometryData.futureGeometryHashes.get();

    if (geometryData.hashes[HashComponents::VertexPosition] == kEmptyHash) {
      throw DxvkError("Position hash should never be empty");
    }

    return true;
  }

  void DrawCallState::finalizeGeometryBoundingBox() {
    if (geometryData.futureBoundingBox.valid())
      geometryData.boundingBox = geometryData.futureBoundingBox.get();
  }

  void DrawCallState::finalizeSkinningData(const RtCamera* pLastCamera) {
    if (futureSkinningData.valid()) {
      skinningData = futureSkinningData.get();

      assert(geometryData.blendWeightBuffer.defined());
      assert(skinningData.numBonesPerVertex <= 4);

      if (pLastCamera != nullptr) {
        const auto fusedMode = RtxOptions::fusedWorldViewMode();
        if (likely(fusedMode == FusedWorldViewMode::None)) {
          transformData.objectToView = transformData.worldToView;
          // Do not bother when transform is fused. Camera matrices are identity and so is worldToView.
        }
        transformData.objectToWorld = pLastCamera->getViewToWorld(false) * transformData.objectToView;
        transformData.worldToView = pLastCamera->getWorldToView(false);
      } else {
        ONCE(Logger::warn("[RTX-Compatibility-Warn] Cannot decompose the matrices for a skinned mesh because the camera is not set."));
      }

      // In rare cases when the mesh is skinned but has only one active bone, skip the skinning pass
      // and bake that single bone into the objectToWorld/View matrices.
      if (skinningData.minBoneIndex + 1 == skinningData.numBones) {
        const Matrix4& skinningMatrix = skinningData.pBoneMatrices[skinningData.minBoneIndex];

        transformData.objectToWorld = transformData.objectToWorld * skinningMatrix;
        transformData.objectToView = transformData.objectToView * skinningMatrix;

        skinningData.boneHash = 0;
        skinningData.numBones = 0;
        skinningData.numBonesPerVertex = 0;
      }

      // Store the numBonesPerVertex in the RasterGeometry as well to allow it to be overridden
      geometryData.numBonesPerVertex = skinningData.numBonesPerVertex;
    }
  }

  void DrawCallState::setCategory(InstanceCategories category, bool doSet) {
    if (doSet) {
      categories.set(category);
    }
  }

  void DrawCallState::removeCategory(InstanceCategories category) {
    categories.clr(category);
  }

  void DrawCallState::setupCategoriesForTexture() {
    // TODO (REMIX-231): It would probably be much more efficient to use a map of texture hash to category flags, rather
    //                   than doing N lookups per texture hash for each category.
    const XXH64_hash_t& textureHash = materialData.getColorTexture().getImageHash();

    setCategory(InstanceCategories::WorldUI, lookupHash(RtxOptions::worldSpaceUiTextures(), textureHash));
    setCategory(InstanceCategories::WorldMatte, lookupHash(RtxOptions::worldSpaceUiBackgroundTextures(), textureHash));

    setCategory(InstanceCategories::Ignore, lookupHash(RtxOptions::ignoreTextures(), textureHash));
    setCategory(InstanceCategories::IgnoreLights, lookupHash(RtxOptions::ignoreLights(), textureHash));
    setCategory(InstanceCategories::IgnoreAntiCulling, lookupHash(RtxOptions::antiCullingTextures(), textureHash));
    setCategory(InstanceCategories::IgnoreMotionBlur, lookupHash(RtxOptions::motionBlurMaskOutTextures(), textureHash));
    setCategory(InstanceCategories::IgnoreOpacityMicromap, lookupHash(RtxOptions::opacityMicromapIgnoreTextures(), textureHash) || isUsingRaytracedRenderTarget);
    setCategory(InstanceCategories::IgnoreAlphaChannel, lookupHash(RtxOptions::ignoreAlphaOnTextures(), textureHash));
    setCategory(InstanceCategories::IgnoreBakedLighting, lookupHash(RtxOptions::ignoreBakedLightingTextures(), textureHash));

    setCategory(InstanceCategories::Hidden, lookupHash(RtxOptions::hideInstanceTextures(), textureHash));

    setCategory(InstanceCategories::Particle, lookupHash(RtxOptions::particleTextures(), textureHash));
    setCategory(InstanceCategories::Beam, lookupHash(RtxOptions::beamTextures(), textureHash));
    setCategory(InstanceCategories::IgnoreTransparencyLayer, lookupHash(RtxOptions::ignoreTransparencyLayerTextures(), textureHash));

    setCategory(InstanceCategories::DecalStatic, lookupHash(RtxOptions::decalTextures(), textureHash));
    setCategory(InstanceCategories::DecalDynamic, lookupHash(RtxOptions::dynamicDecalTextures(), textureHash));
    setCategory(InstanceCategories::DecalSingleOffset, lookupHash(RtxOptions::singleOffsetDecalTextures(), textureHash));
    setCategory(InstanceCategories::DecalNoOffset, lookupHash(RtxOptions::nonOffsetDecalTextures(), textureHash));

    setCategory(InstanceCategories::AnimatedWater, lookupHash(RtxOptions::animatedWaterTextures(), textureHash));

    setCategory(InstanceCategories::ThirdPersonPlayerModel, lookupHash(RtxOptions::playerModelTextures(), textureHash));
    setCategory(InstanceCategories::ThirdPersonPlayerBody, lookupHash(RtxOptions::playerModelBodyTextures(), textureHash));

    setCategory(InstanceCategories::Terrain, lookupHash(RtxOptions::terrainTextures(), textureHash));
    setCategory(InstanceCategories::Sky, lookupHash(RtxOptions::skyBoxTextures(), textureHash));

    setCategory(InstanceCategories::ParticleEmitter, lookupHash(RtxOptions::particleEmitterTextures(), textureHash));
  }

  void DrawCallState::setupCategoriesForGeometry() {
    const XXH64_hash_t assetReplacementHash = getHash(RtxOptions::geometryAssetHashRule());
    setCategory(InstanceCategories::Sky, lookupHash(RtxOptions::skyBoxGeometries(), assetReplacementHash));
  }

  static std::optional<Vector3> makeCameraPosition(const Matrix4& worldToView,
                                                   bool zWrite,
                                                   bool alphaBlend,
                                                   bool hasSkinning) {
    if (hasSkinning) {
      return std::nullopt;
    }
    // particles
    if (!zWrite && alphaBlend) {
      return std::nullopt;
    }
    // identity matrix
    if (isIdentityExact(worldToView)) {
      return std::nullopt;
    }

#define USE_TRUE_CAMERA_POSITION_FOR_COMPARISON 0

#if USE_TRUE_CAMERA_POSITION_FOR_COMPARISON
    return (inverse(worldToView))[3].xyz();
#else
    // as we compare the cameras relatively and don't need precise camera position:
    // just return a position-like vector, to avoid calculating heavy matrix inverse operation
    return worldToView[3].xyz();
#endif
  }

  static bool areCamerasClose(const Vector3& a, const Vector3& b) {
    const float distanceThreshold = RtxOptions::skyAutoDetectUniqueCameraDistance();
    return lengthSqr(a - b) < distanceThreshold * distanceThreshold;
  }

  bool checkSkyAutoDetect(bool depthTestEnable,
                          const std::optional<Vector3>& newCameraPos,
                          uint32_t prevFrameSeenCamerasCount,
                          const std::vector<Vector3>& seenCameraPositions) {

    if (RtxOptions::skyAutoDetect() != SkyAutoDetectMode::CameraPositionAndDepthFlags &&
        RtxOptions::skyAutoDetect() != SkyAutoDetectMode::CameraPosition) {
      return false;
    }
    const bool withDepthFlags = (RtxOptions::skyAutoDetect() == SkyAutoDetectMode::CameraPositionAndDepthFlags);


    const bool searchingForSkyCamera             = (seenCameraPositions.size() == 0);
    const bool skyFoundAndSearchingForMainCamera = (seenCameraPositions.size() == 1);
    const bool skyAndMainCameraFound             = (seenCameraPositions.size() >= 2);

    if (skyAndMainCameraFound) {
      // assume that subsequent draw calls can not be sky
      return false;
    }

    if (searchingForSkyCamera) {
      if (withDepthFlags) {
        // no depth test: frame starts with a sky
        // depth test: frame starts with a world, not a sky
        return !depthTestEnable;
      }
      // assume the first camera to be sky
      return true;
    }

    {
      // corner case: if there was no sky camera at all, fallback, but this would also
      // involve a one-frame (preceding to the current one) being rasterized (like a flicker)
      if (prevFrameSeenCamerasCount < 2) {
        if (withDepthFlags) {
          // no depth test: sky
          // depth test: world
          return !depthTestEnable;
        }
        // assume no sky
        return false;
      }
    }

    if (skyFoundAndSearchingForMainCamera) {
      // if draw call doesn't have a camera position
      if (!newCameraPos) {
        // it can't contain main camera, so assume that it's still a sky
        return true;
      }

      // if same as the existing sky camera
      if (areCamerasClose(seenCameraPositions[0], *newCameraPos)) {
        // still sky
        return true;
      }

      // found a new unique camera, which should be a main camera
      return false;
    }

    assert(0);
    return false;
  }

  bool shouldBakeSky(const DrawCallState& drawCallState,
                     bool hasSkinning,
                     uint32_t prevFrameSeenCamerasCount,
                     std::vector<Vector3>& seenCameraPositions) {           
    const auto drawCallCameraPos =
      drawCallState.isDrawingToRaytracedRenderTarget
        ? std::optional<Vector3>{}
        : makeCameraPosition(
            drawCallState.getTransformData().worldToView,
            drawCallState.zWriteEnable,
            drawCallState.getMaterialData().blendMode.enableBlending,
            hasSkinning);

    auto l_addIfUnique = [&seenCameraPositions](const std::optional<Vector3>& newCameraPos) {
      if (!newCameraPos) {
        return;
      }
      for (const Vector3& seen : seenCameraPositions) {
        if (areCamerasClose(seen, *newCameraPos)) {
          return;
        }
      }
      seenCameraPositions.push_back(*newCameraPos);
    };
    l_addIfUnique(drawCallCameraPos);


    if (drawCallState.minZ >= RtxOptions::skyMinZThreshold()) {
      return true;
    }

    // NOTE: we use color texture hash for sky detection, however the replacement is hashed with
    // the whole legacy material hash (which, as of 12/9/2022, equals to color texture hash). Adding a check just in case.
    assert(drawCallState.getMaterialData().getColorTexture().getImageHash() == drawCallState.getMaterialData().getHash() && "Texture or material hash method changed!");

    if (drawCallState.getMaterialData().usesTexture()) {
      if (lookupHash(RtxOptions::skyBoxTextures(), drawCallState.getMaterialData().getHash())) {
        return true;
      }
    } else {
      if (drawCallState.drawCallID < RtxOptions::skyDrawcallIdThreshold()) {
        return true;
      }
    }

    // don't track camera positions for Raytraced Render Targets, as they are a different camera position from main view
    const static auto renderTargetCameraPositions = std::vector<Vector3>{};

    if (checkSkyAutoDetect(drawCallState.zEnable,
                           drawCallCameraPos,
                           prevFrameSeenCamerasCount,
                           drawCallState.isDrawingToRaytracedRenderTarget ? renderTargetCameraPositions : seenCameraPositions)) {
      return true;
    }

    return false;
  }

  bool shouldBakeTerrain(const DrawCallState& drawCallState) {
    if (!TerrainBaker::needsTerrainBaking())
      return false;

    return lookupHash(RtxOptions::terrainTextures(), drawCallState.getMaterialData().getHash());
  }

  void DrawCallState::setupCategoriesForHeuristics(uint32_t prevFrameSeenCamerasCount,
                                                   std::vector<Vector3>& seenCameraPositions) {
    setCategory(InstanceCategories::Sky, shouldBakeSky(*this,
                                                       futureSkinningData.valid(),
                                                       prevFrameSeenCamerasCount,
                                                       seenCameraPositions));
    setCategory(InstanceCategories::Terrain, shouldBakeTerrain(*this));
  }

  BlasEntry::BlasEntry(const DrawCallState& input_)
    : input(input_), m_spatialMap(RtxOptions::uniqueObjectDistance() * 2.f) {
      if (RtxOptions::uniqueObjectDistance() <= 0.f) {
        ONCE(Logger::err("rtx.uniqueObjectDistance must be greater than 0."));
      }
    }

  void BlasEntry::unlinkInstance(RtInstance* instance) {
    instance->removeFromSpatialCache();
    auto it = std::find(m_linkedInstances.begin(), m_linkedInstances.end(), instance);
    if (it != m_linkedInstances.end()) {
      // Swap & pop - faster than "erase", but doesn't preserve order, which is fine here.
      std::swap(*it, m_linkedInstances.back());
      m_linkedInstances.pop_back();
    } else {
      ONCE(Logger::err("Tried to unlink an instance, which was never linked!"));
    }
  }

  void BlasEntry::rebuildSpatialMap() {
    m_spatialMap.rebuild(RtxOptions::uniqueObjectDistance() * 2.f);
  }

} // namespace dxvk
