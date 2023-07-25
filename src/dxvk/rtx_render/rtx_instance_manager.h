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

#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "rtx_camera_manager.h"
#include "dxvk_cmdlist.h"

namespace dxvk 
{
class DxvkContext;
class DxvkDevice;
class ResourceCache;
class CameraManager;

// RtInstance defines a SceneObjects placement/parameterization within the current scene.
class RtInstance {
public:
  RtSurface surface;

  RtInstance() = delete;
  RtInstance(const uint64_t id, uint32_t instanceVectorId);
  RtInstance(const RtInstance& src, uint64_t id, uint32_t instanceVectorId);

  uint64_t getId() const { return m_id; }
  const VkAccelerationStructureInstanceKHR& getVkInstance() const { return m_vkInstance; }
  VkAccelerationStructureInstanceKHR& getVkInstance() { return m_vkInstance; }
  bool isObjectToWorldMirrored() const { return m_objectToWorldMirrored; }

  BlasEntry* getBlas() const { return m_linkedBlas; }
  const XXH64_hash_t& getMaterialHash() const { return m_materialHash; }
  const XXH64_hash_t& getMaterialDataHash() const { return m_materialDataHash; }
  const XXH64_hash_t& getTexcoordHash() const { return m_texcoordHash; }
  const XXH64_hash_t& getIndexHash() const { return m_indexHash; }
  Matrix4 getTransform() const { return transpose(dxvk::Matrix4(m_vkInstance.transform)); }
  const Matrix4& getPrevTransform() const { return surface.prevObjectToWorld; }
  Vector3 getWorldPosition() const { return { m_vkInstance.transform.matrix[0][3], m_vkInstance.transform.matrix[1][3], m_vkInstance.transform.matrix[2][3] }; }
  const Vector3& getPrevWorldPosition() const { return surface.prevObjectToWorld.data[3].xyz(); }

  bool isCreatedThisFrame(uint32_t frameIndex) const { return frameIndex == m_frameCreated; }

  // Bind a BLAS object to this instance
  void setBlas(BlasEntry& blas);
  // Set the transform for this instance, returns true if object has moved
  bool setTransform(const Matrix4& objectToWorld);
  // Set the transform for this instance for current frame only, returns true if object has moved
  bool setCurrentTransform(const Matrix4& objectToWorld);
  void setPrevTransform(const Matrix4& objectToWorld);
  void setFrameCreated(const uint32_t frameIndex);
  // Returns if this is the first occurence in a given frame
  bool setFrameLastUpdated(const uint32_t frameIndex);
  uint32_t getFrameLastUpdated() const { return m_frameLastUpdated; } 
  uint32_t getFrameAge() const { return m_frameLastUpdated - m_frameCreated; }
  // Signal this object should be collected on the next GC pass
  void markForGarbageCollection() const;
  void markAsInsideFrustum() const;
  void markAsOutsideFrustum() const;
  // Returns true if a new camera type was registered
  bool registerCamera(CameraType::Enum cameraType, uint32_t frameIndex);
  bool isCameraRegistered(CameraType::Enum cameraType) const;
  void setCustomIndexBit(uint32_t oneBitMask, bool value);
  bool getCustomIndexBit(uint32_t oneBitMask) const;
  bool isHidden() const { return m_isHidden; }
  void setHidden(bool value) { m_isHidden = value; }

  bool usesUnorderedApproximations() const { return m_isUnordered; }
  RtSurfaceMaterialType getMaterialType() const {
    return m_materialType;
  }
  uint32_t getAlbedoOpacityTextureIndex() const { return m_albedoOpacityTextureIndex; }
  uint32_t getSecondaryOpacityTextureIndex() const { return m_secondaryOpacityTextureIndex; }
  bool isAnimated() const {
    return m_isAnimated;
  }
  void setSurfaceIndex(uint32_t surfaceIndex) {
    m_surfaceIndex = surfaceIndex;
  }
  uint32_t getSurfaceIndex() const {
    return m_surfaceIndex;
  }
  void setPreviousSurfaceIndex(uint32_t surfaceIndex) {
    m_previousSurfaceIndex = surfaceIndex;
  }
  uint32_t getPreviousSurfaceIndex() const {
    return m_previousSurfaceIndex;
  }
  XXH64_hash_t getOpacityMicromapSourceHash() const { return m_opacityMicromapSourceHash; }
  void setOpacityMicromapSourceHash(XXH64_hash_t opacityMicromapSourceHash) { m_opacityMicromapSourceHash = opacityMicromapSourceHash; }

  uint32_t getFirstBillboardIndex() const { return m_firstBillboard; }
  uint32_t getBillboardCount() const { return m_billboardCount; }

  VkGeometryFlagsKHR getGeometryFlags() const { return m_geometryFlags; }

  bool isViewModel() const;
  bool isViewModelNonReference() const;
  bool isViewModelReference() const;
  bool isViewModelVirtual() const;
private:
  friend class InstanceManager;

  const uint64_t m_id;
  mutable uint32_t m_instanceVectorId; // Index within instance vector in instance manager

  mutable bool m_isMarkedForGC = false;
  mutable bool m_isInsideFrustum = true;
  mutable uint32_t m_frameLastUpdated = kInvalidFrameIndex;
  mutable uint32_t m_frameCreated = kInvalidFrameIndex;

  std::vector<CameraType::Enum> m_seenCameraTypes;  // Camera types with which the instance has been originally rendered with

  RtSurfaceMaterialType m_materialType = RtSurfaceMaterialType::Count;
  uint32_t m_albedoOpacityTextureIndex = kSurfaceMaterialInvalidTextureIndex;
  uint32_t m_secondaryOpacityTextureIndex = kSurfaceMaterialInvalidTextureIndex;

  // Extra instance meta data needed for Opacity Micromap Manager 
  bool m_isAnimated = false;
  XXH64_hash_t m_opacityMicromapSourceHash = kEmptyHash;   // Hash for the source data to Opacity Micromap

  uint32_t m_surfaceIndex;        // Material surface index for reordered surfaces by AccelManager
  uint32_t m_previousSurfaceIndex;

  bool m_isHidden = false;
  bool m_isPlayerModel = false;
  bool m_isWorldSpaceUI = false;
  bool m_isUnordered = false;
  bool m_objectToWorldMirrored = false;
  bool m_isCreatedByRenderer = false;
  BlasEntry* m_linkedBlas = nullptr;
  XXH64_hash_t m_materialHash = kEmptyHash;
  XXH64_hash_t m_materialDataHash = kEmptyHash;
  XXH64_hash_t m_texcoordHash = kEmptyHash;
  XXH64_hash_t m_indexHash = kEmptyHash;
  VkAccelerationStructureInstanceKHR m_vkInstance;
  VkGeometryFlagsKHR m_geometryFlags = 0;
  uint32_t m_firstBillboard = 0;
  uint32_t m_billboardCount = 0;

  // Used decal offsetting parameters
  XXH64_hash_t m_lastDecalOffsetVertexDataVersion = kEmptyHash;
  uint32_t m_currentDecalOffsetDifference = UINT32_MAX;

public:

  // Not really needed in this struct, just to store it somewhere for a batched build
  std::vector<VkAccelerationStructureGeometryKHR> buildGeometries;
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges;
  std::vector<uint32_t> billboardIndices;
  std::vector<uint32_t> indexOffsets;
};

// Optional notification callbacks that can be implemented to "opt-in" to InstanceManager events
struct InstanceEventHandler {
  void* eventHandlerOwnerAddress;

  // Callback triggered whenever a new instance has been added to the database
  std::function<void(const RtInstance&)> onInstanceAddedCallback;
  // Callback triggered whenever instance metadata is updated - the boolean flags 
  //   signal if the transform and/or vertex positions have changed (respectively)
  std::function<void(RtInstance&, const RtSurfaceMaterial&, bool, bool)> onInstanceUpdatedCallback;
  // Callback triggered whenever an instance has been removed from the database
  std::function<void(const RtInstance&)> onInstanceDestroyedCallback;

  InstanceEventHandler() = delete;
  InstanceEventHandler(void* _eventHandlerOwnerAddress) : eventHandlerOwnerAddress(_eventHandlerOwnerAddress) { }
};

struct IntersectionBillboard {
  Vector3 center;
  Vector3 xAxis;
  float width;
  Vector3 yAxis;
  float height;
  Vector2 xAxisUV;
  Vector2 yAxisUV;
  Vector2 centerUV;
  uint32_t vertexColor;
  uint32_t instanceMask;
  const RtInstance* instance;
  XXH64_hash_t texCoordHash;
  XXH64_hash_t vertexOpacityHash;
  bool allowAsIntersectionPrimitive;
  bool isBeam; // if true, the billboard's Y axis is fixed and the billboard is free to rotate around it
  bool isCameraFacing; // if true, the billboard should always orient the normal toward the camera, don't use the transform matrix
};

// InstanceManager is responsible for maintaining the active set of scene instances
//  and the GPU buffers which are required by VK for instancing.
class InstanceManager : public CommonDeviceObject {
public:
  InstanceManager(InstanceManager const&) = delete;
  InstanceManager& operator=(InstanceManager const&) = delete;

  InstanceManager(DxvkDevice* device, ResourceCache* pResourceCache);
  ~InstanceManager();

  // Return a list of instances currently active in the scene
  const std::vector<RtInstance*>& getInstanceTable() const { return m_instances; }

  // Returns the active number of instances in scene
  const uint32_t getActiveCount() const { return m_instances.size(); }
  
  void onFrameEnd();

  // Optional notification callbacks that can be implemented to "opt-in" to InstanceManager events
  void addEventHandler(const InstanceEventHandler& events) {
    m_eventHandlers.push_back(events);
  }
  
  void removeEventHandler(void* eventHandlerOwnerAddress);

  // Clear all instances currently tracked by manager
  void clear();

  // Clean up instances which are deemed as no longer required
  void garbageCollection();
  
  // Takes a scene object entry (blas + drawcall) and generates/finds the instance data internally
  RtInstance* processSceneObject(
    const CameraManager& cameraManager, const RayPortalManager& rayPortalManager,
    BlasEntry& blas, const DrawCallState& drawCall, const MaterialData& materialData, const RtSurfaceMaterial& material);

  // Creates a copy of a reference instance and adds it to the instance pool
  // Temporary single frame instances generated every frame should disable valid id generation to avoid overflowing it
  RtInstance* createInstanceCopy(const RtInstance& reference, bool generateValidID = true);

  // Creates a view model instance from the reference and adds it to the instance pool
  RtInstance* createViewModelInstance(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, const RtInstance& reference, const Matrix4& perspectiveCorrection, const Matrix4& prevPerspectiveCorrection);

  // Creates view model instances and their virtual counterparts
  void createViewModelInstances(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, const CameraManager& cameraManager, const RayPortalManager& rayPortalManager);

  void createPlayerModelVirtualInstances(Rc<DxvkContext> ctx, const CameraManager& cameraManager, const RayPortalManager& rayPortalManager);

  void findPortalForVirtualInstances(const CameraManager& cameraManager, const RayPortalManager& rayPortalManager);

  int getVirtualInstancePortalIndex() const { return m_virtualInstancePortalIndex; }
    
  // Creates ray portal virtual instances for viewModel instances for a closest portal within range
  void createRayPortalVirtualViewModelInstances(const std::vector<RtInstance*>& viewModelInstances, const CameraManager& cameraManager, const RayPortalManager& rayPortalManager);

  void resetSurfaceIndices();

  const std::vector<IntersectionBillboard>& getBillboards() const { return m_billboards; }
  
private:
  ResourceCache* m_pResourceCache;

  uint64_t m_nextInstanceId = 0;

  std::vector<RtInstance*> m_instances; 
  std::vector<RtInstance*> m_viewModelCandidates;
  std::vector<RtInstance*> m_playerModelInstances;
  std::vector<IntersectionBillboard> m_billboards;

  bool m_previousViewModelState = false;
  RtInstance* targetInstance = nullptr;

  uint32_t m_currentDecalOffsetIndex;
    
  // Controls active portal space for which virtual view model or player model instances have been generated for.
  // Negative values mean there is no portal that's close enough to the camera.
  int m_virtualInstancePortalIndex = 0;    

  std::vector<InstanceEventHandler> m_eventHandlers;

  // Handles the case of when two (or more) identical geometries+textures draw calls have been submitted in a single frame (typically used for two-pass rendering in FF)
  void mergeInstanceHeuristics(RtInstance& instanceToModify, const DrawCallState& drawCall, const RtSurfaceMaterial& material, const RtSurface::AlphaState& alphaState) const;

  // Finds the "closest" matching instance to a set of inputs, returns a pointer (can be null if not found) to closest instance
  RtInstance* findSimilarInstance(const BlasEntry& blas, const RtSurfaceMaterial& material, const Matrix4& transform, CameraType::Enum cameraType, const RayPortalManager& rayPortalManager);

  RtInstance* addInstance(BlasEntry& blas, const DrawCallState& drawCall, const RtSurfaceMaterial& material, const Matrix4& transform);
  void processInstanceBuffers(const BlasEntry& blas, RtInstance& currentInstance) const;

  void updateInstance(
    RtInstance& currentInstance, const CameraManager& cameraManager,
    const BlasEntry& blas, const DrawCallState& drawCall, const MaterialData& materialData, const RtSurfaceMaterial& material,
    const Matrix4& transform, const Matrix4& worldToProjection);

  void removeInstance(RtInstance* instance);

  static RtSurface::AlphaState calculateAlphaState(const DrawCallState& drawCall, const MaterialData& materialData, const RtSurfaceMaterial& material);

  // Modifies an instance given active developer options. Returns true if the instance was modified
  bool applyDeveloperOptions(RtInstance& currentInstance, const DrawCallState& drawCall);

  void applyDecalOffsets(RtInstance& instance, const RasterGeometry& geometryData);

  void createBillboards(RtInstance& instance, const Vector3& cameraViewDirection);

  void createBeams(RtInstance& instance);

  void filterPlayerModelInstances(const Vector3& playerModelPosition, const RtInstance* bodyInstance);

  void detectIfPlayerModelIsVirtual(
    const CameraManager& cameraManager,
    const RayPortalManager& rayPortalManager,
    const Vector3& playerModelPosition,
    bool* out_PlayerModelIsVirtual,
    const struct SingleRayPortalDirectionInfo** out_NearPortalInfo,
    const struct SingleRayPortalDirectionInfo** out_FarPortalInfo) const;
};

}  // namespace dxvk

