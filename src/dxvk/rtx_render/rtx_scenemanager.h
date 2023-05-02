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
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <list>

#include "../dxvk_buffer.h"
#include "../dxvk_image.h"
#include "../dxvk_staging.h"
#include "../dxvk_bind_mask.h"
#include "../dxvk_cmdlist.h"
#include "../util/util_hashtable.h"

#include "rtx_types.h"
#include "rtx_cameramanager.h"
#include "rtx_drawcallcache.h"
#include "rtx_sparseuniquecache.h"
#include "rtx_sparserefcountcache.h"
#include "rtx_lightmanager.h"
#include "rtx_instancemanager.h"
#include "rtx_accelmanager.h"
#include "rtx_rayportalmanager.h"
#include "rtx_bindlessresourcemanager.h"
#include "rtx_volumemanager.h"
#include <d3d9types.h>

namespace dxvk 
{
class DxvkContext;
class DxvkDevice;
class GameCapturer;
struct AssetReplacement;
struct AssetReplacer;
class OpacityMicromapManager;

// The resource cache can be *searched* by other users
class ResourceCache {
public:
  bool find(const RaytraceBuffer& buf, uint32_t& outIdx) const { return m_bufferCache.find(buf, outIdx); }
  bool find(const TextureRef& tex, uint32_t& outIdx) const { return m_textureCache.find(tex, outIdx); }
  bool find(const RtSurfaceMaterial& surf, uint32_t& outIdx) const { return m_surfaceMaterialCache.find(surf, outIdx); }

protected:
  struct BufferHashFn {
    size_t operator() (const RaytraceBuffer& slice) const {
      return slice.getSliceHandle().hash();
    }
  };
  SparseRefCountCache<RaytraceBuffer, BufferHashFn> m_bufferCache;

  struct TextureHashFn {
    size_t operator() (const TextureRef& tex) const {
      return tex.getUniqueKey();
    }
  };
  struct TextureEquality {
    bool operator()(const TextureRef& lhs, const TextureRef& rhs) const {
      return lhs.getUniqueKey() == rhs.getUniqueKey();
    }
  };
  SparseUniqueCache<TextureRef, TextureHashFn, TextureEquality> m_textureCache;

  struct SurfaceMaterialHashFn {
    size_t operator() (const RtSurfaceMaterial& mat) const {
      return (size_t)mat.getHash();
    }
  };
  SparseUniqueCache<RtSurfaceMaterial, SurfaceMaterialHashFn> m_surfaceMaterialCache;

  struct VolumeMaterialHashFn {
    size_t operator() (const RtVolumeMaterial& mat) const {
      return (size_t)mat.getHash();
    }
  };
  SparseUniqueCache<RtVolumeMaterial, VolumeMaterialHashFn> m_volumeMaterialCache;
};

// Scene manager is a super manager, it's the interface between rendering and world state
// along with managing the operation of other caches, scene manager also manages the cache
// directly for "SceneObject"'s - which are "unique meshes/geometry", which map 1-to-1 with
// BLAS entries in raytracing terminology.
class SceneManager : public ResourceCache {
public:
  SceneManager(SceneManager const&) = delete;
  SceneManager& operator=(SceneManager const&) = delete;

  SceneManager(Rc<DxvkDevice> device);
  ~SceneManager();

  void initialize(Rc<DxvkContext> ctx);

  void destroy();

  void submitDrawState(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& input);
  
  bool areReplacementsLoaded() const;
  bool areReplacementsLoading() const;
  const std::string getReplacementStatus() const;

  uint32_t getGameTimeSinceStartMS();

  Rc<DxvkBuffer> getSurfaceMaterialBuffer() { return m_surfaceMaterialBuffer; }
  Rc<DxvkBuffer> getVolumeMaterialBuffer() { return m_volumeMaterialBuffer; }
  Rc<DxvkBuffer> getSurfaceBuffer() const { return m_accelManager.getSurfaceBuffer(); }
  Rc<DxvkBuffer> getSurfaceMappingBuffer() const { return m_accelManager.getSurfaceMappingBuffer(); }
  Rc<DxvkBuffer> getBillboardsBuffer() const { return m_accelManager.getBillboardsBuffer(); }
  bool isPreviousFrameSceneAvailable() const { return m_previousFrameSceneAvailable && getSurfaceMappingBuffer().ptr() != nullptr; }

  const std::vector<RaytraceBuffer>& getBufferTable() const { return m_bufferCache.getObjectTable(); }
  const std::vector<TextureRef>& getTextureTable() const { return m_textureCache.getObjectTable(); }
  const std::vector<RtSurfaceMaterial>& getSurfaceMaterialTable() const { return m_surfaceMaterialCache.getObjectTable(); }
  const std::vector<RtVolumeMaterial>& getVolumeMaterialTable() const { return m_volumeMaterialCache.getObjectTable(); }
  const DrawCallCache& getDrawCallCache() const { return m_drawCallCache; }
  const std::vector<RtInstance*>& getInstanceTable() const { return m_instanceManager.getInstanceTable(); }
  
  const InstanceManager& getInstanceManager() const { return m_instanceManager; }
  const AccelManager& getAccelManager() const { return m_accelManager; }
  const LightManager& getLightManager() const { return m_lightManager; }
  const RayPortalManager& getRayPortalManager() const { return m_rayPortalManager; }
  const BindlessResourceManager& getBindlessResourceManager() const { return m_bindlessResourceManager; }
  const OpacityMicromapManager* getOpacityMicromapManager() const { return m_opacityMicromapManager.get(); }
  const VolumeManager& getVolumeManager() const { return m_volumeManager; }
  LightManager& getLightManager() { return m_lightManager; }
  std::unique_ptr<AssetReplacer>& getAssetReplacer() { return m_pReplacer; }

  void addLight(const D3DLIGHT9& light);
  
  bool processCameraData(const DrawCallState& input) {
    return m_cameraManager.processCameraData(input);
  }

  const CameraManager& getCameraManager() const { return m_cameraManager; }
  const RtCamera& getCamera() const { return m_cameraManager.getMainCamera(); }
  RtCamera& getCamera() { return m_cameraManager.getMainCamera(); }

  FogState& getFogState() { return m_fog; }
  void clearFogState();

  // ISceneManager but not really
  void clear(Rc<DxvkContext> ctx, bool needWfi);
  void garbageCollection();
  void prepareSceneData(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, class DxvkBarrierSet& execBarriers, const float frameTimeSecs);

  void onFrameEnd(Rc<DxvkContext> ctx);

  // GameCapturer
  void triggerUsdCapture() const;
  bool isGameCapturerIdle() const;

  void finalizeAllPendingTexturePromotions();

  void trackTexture(Rc<DxvkContext> ctx, TextureRef inputTexture, uint32_t& textureIndex, bool hasTexcoords, bool patchSampler = true, bool allowAsync = true);

private:
  enum class ObjectCacheState
  {
    kUpdateInstance = 0,
    kUpdateBVH = 1,
    KBuildBVH = 2,
    kInvalid = -1
  };
  // Handles conversion of geometry data coming from a draw call, to the data used by the raytracing backend
  template<bool isNew>
  ObjectCacheState processGeometryInfo(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, RaytraceGeometry& modifiedGeometryData);

  // Consumes a draw call state and updates the scene state accordingly
  uint64_t processDrawCallState(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& blasInput, const MaterialData* replacementMaterialData);
  // Updates ref counts for old and new buffers
  void updateBufferCache(const RaytraceGeometry& oldGeoData, RaytraceGeometry& newGeoData);
  // Decrements the ref count for buffers associated with the specified geometry data
  void freeBufferCache(const RaytraceGeometry& geoData);

  // Called whenever a new BLAS scene object is added to the cache
  ObjectCacheState onSceneObjectAdded(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, BlasEntry* pBlas);
  // Called whenever a BLAS scene object is updated
  ObjectCacheState onSceneObjectUpdated(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState& drawCallState, BlasEntry* pBlas);
  // Called whenever a BLAS scene object is destroyed
  void onSceneObjectDestroyed(const BlasEntry& pBlas, const XXH64_hash_t& hash);

  // Called whenever a new instance has been added to the database
  void onInstanceAdded(const RtInstance& instance);
  // Called whenever instance metadata is updated
  void onInstanceUpdated(RtInstance& instance, const RtSurfaceMaterial& material, const bool hasTransformChanged, const bool hasVerticesdChanged);
  // Called whenever an instance has been removed from the database
  void onInstanceDestroyed(const RtInstance& instance);

  uint64_t drawReplacements(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmd, const DrawCallState* input, const std::vector<AssetReplacement>* pReplacements, const MaterialData* overrideMaterialData);

  void createEffectLight(Rc<DxvkContext> ctx, const DrawCallState& input, const RtInstance* instance);

  Rc<GameCapturer> m_gameCapturer;
  uint32_t m_beginUsdExportFrameNum = -1;
  bool m_enqueueDelayedClear = false;
  bool m_previousFrameSceneAvailable = false;

  // Hash/Cache's
  InstanceManager m_instanceManager;
  AccelManager m_accelManager;
  LightManager m_lightManager;
  RayPortalManager m_rayPortalManager;
  BindlessResourceManager m_bindlessResourceManager;
  std::unique_ptr<OpacityMicromapManager> m_opacityMicromapManager;
  VolumeManager m_volumeManager;

  DrawCallCache m_drawCallCache;

  CameraManager m_cameraManager;

  std::unique_ptr<AssetReplacer> m_pReplacer;

  FogState m_fog;

  // TODO: Move the following resources and getters to RtResources class
  Rc<DxvkBuffer> m_surfaceMaterialBuffer;
  Rc<DxvkBuffer> m_volumeMaterialBuffer;

  Rc<DxvkDevice> m_device;

  Rc<DxvkSampler> m_materialTextureSampler;

  uint32_t m_currentFrameIdx = -1;
  bool m_useFixedFrameTime = false;
  std::chrono::time_point<std::chrono::system_clock> m_startTime;
};

}  // namespace nvvk

