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
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "rtx_generation_array.h"

struct remixapi_InstanceInfoBlendEXT;

namespace dxvk {

  template<typename T>
  class Rc;
  class DxvkBarrierSet;
  class DxvkBuffer;
  class DxvkContext;
  class DxvkDevice;
  class RtxContext;
  class SceneManager;
  struct ExternalDrawState;
  struct MaterialData;
  struct RasterGeometry;

  // Remix API materials, meshes and instances whose tables, BLASes and TLAS instances persist on the GPU.
  // SceneManager creates it only with rtx.gpuResidentScene, see documentation/GpuResidentScene.md.
  struct GpuResidentScene {
    explicit GpuResidentScene(DxvkDevice* device);
    ~GpuResidentScene();

    struct Data;
    std::unique_ptr<Data> m_data;
  };


  MaterialId gpuResidentSceneAllocMaterialId(GpuResidentScene& scene);
  bool gpuResidentSceneFreeMaterialId(GpuResidentScene& scene, MaterialId id);
  bool gpuResidentSceneSetMaterial(
      GpuResidentScene& scene,
      SceneManager& sceneManager,
      MaterialId id,
      MaterialData&& material
  );
  bool gpuResidentSceneUpdateMaterial(
      GpuResidentScene& scene,
      MaterialId id,
      MaterialData&& material,
      bool& isCameraMediumMaterial
  );
  bool gpuResidentSceneReleaseMaterial(
      GpuResidentScene& scene,
      MaterialId id,
      bool& wasCameraMediumMaterial
  );


  BlasId gpuResidentSceneAllocMeshId(GpuResidentScene& scene);
  bool gpuResidentSceneFreeMeshId(GpuResidentScene& scene, BlasId id);
  bool gpuResidentSceneSetMesh(
      GpuResidentScene& scene,
      SceneManager& sceneManager,
      DxvkContext* ctx,
      BlasId id,
      std::vector<RasterGeometry>&& surfaces
  );
  bool gpuResidentSceneReleaseMesh(GpuResidentScene& scene, BlasId id);


  SurfaceId gpuResidentSceneAllocInstanceId(GpuResidentScene& scene);
  bool gpuResidentSceneFreeInstanceId(GpuResidentScene& scene, SurfaceId id);
  bool gpuResidentSceneSetInstance(
      GpuResidentScene& scene,
      SurfaceId id,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  );
  // A frame-local instance is released at the end of the frame that added it.
  void gpuResidentSceneAddFrameLocalInstance(
      GpuResidentScene& scene,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  );
  bool gpuResidentSceneUpdateInstance(
      GpuResidentScene& scene,
      SurfaceId id,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  );
  bool gpuResidentSceneReleaseInstance(GpuResidentScene& scene, SurfaceId id);
  void gpuResidentSceneEndFrame(GpuResidentScene& scene, SceneManager& sceneManager);
  // Waits for the GPU through synchronize, then finishes every pending free.
  // Frames without a scene call it, because their frees never get the GPU event that the per-frame prepare records.
  void gpuResidentSceneResolveDeferredFrees(
      GpuResidentScene& scene,
      SceneManager& sceneManager,
      const std::function<void()>& synchronize
  );
  // Drops the materials' texture references before the texture cache is cleared, and marks the materials for re-creation.
  void gpuResidentScenePrepareTextureCacheClear(GpuResidentScene& scene);


  bool gpuResidentSceneSetCameraMediumMaterial(GpuResidentScene& scene, MaterialId id);
  uint32_t gpuResidentSceneGetCameraMediumMaterialIndex(const GpuResidentScene& scene);


  uint32_t gpuResidentSceneGetSurfaceCount(const GpuResidentScene& scene);
  bool gpuResidentSceneHasRenderableSurfaces(const GpuResidentScene& scene);
  bool gpuResidentSceneHasPreviousScene(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetSurfaceBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetMaterialBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetMaterialExtensionBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetSurfaceMappingBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetEmptyBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetCurrentPrimitiveIDPrefixSumBuffer(const GpuResidentScene& scene);
  const Rc<DxvkBuffer>& gpuResidentSceneGetPreviousPrimitiveIDPrefixSumBuffer(const GpuResidentScene& scene);


  // Creates the GPU materials of dirty material records.
  // Runs before texture upload, so that the textures the materials track are uploaded this frame.
  void gpuResidentScenePrepareResources(
      GpuResidentScene& scene,
      SceneManager& sceneManager
  );


  void gpuResidentSceneOnDestroy(GpuResidentScene& scene);


  // Uploads the dirty table pages, builds dirty BLASes and TLASes and publishes the TLASes to the resource manager.
  void gpuResidentScenePrepare(
      GpuResidentScene& scene,
      const Rc<RtxContext>& ctx,
      DxvkBarrierSet& execBarriers
  );

}  // namespace dxvk
