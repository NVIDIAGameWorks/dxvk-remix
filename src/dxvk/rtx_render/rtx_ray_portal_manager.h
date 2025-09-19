/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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

#include <array>
#include <optional>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx/concept/ray_portal/ray_portal.h"
#include "rtx_camera.h"
#include "rtx_common_object.h"

namespace dxvk {

class DxvkContext;
class DxvkDevice;
class CameraManager;
class ResourceCache;

struct RayPortalInfo {
  Matrix4 worldToModelTransform;

  Vector3 centroid;
  Vector3 planeBasis[2];
  Vector3 planeNormal;
  Vector2 planeHalfExtents;
  Vector3 rayOffset;

  uint32_t portalIndex; // Unique index consistent across frames
  bool isCreatedThisFrame;

  uint32_t materialIndex;

  Matrix4 textureTransform;

  uint8_t spriteSheetRows;
  uint8_t spriteSheetCols;
  uint8_t spriteSheetFPS;
};

struct SingleRayPortalDirectionInfo {
  RayPortalInfo entryPortalInfo;
  Matrix4 portalToOpposingPortalDirectionWithoutRayOffset;  // W/o ray offset
  Matrix4 portalToOpposingPortalDirection;      // Includes ray offset
};

struct RayPortalPairInfo {
  SingleRayPortalDirectionInfo pairInfos[2];   // infos for {P0->P1, P1->P0}
};

class RayPortalManager : public CommonDeviceObject {
public:
  using RayPortalInfosType = std::array<std::optional<RayPortalInfo>, maxRayPortalCount>;
  // Portals get chain paired => max pairs == numPortals - 1
  using RayPortalPairInfosType = std::array<std::optional<RayPortalPairInfo>, maxRayPortalCount - 1>;

  // Final portal state used for raytracing
  struct SceneData {
    // Note: Not tightly packed, meaning these indices will align with the Ray Portal Index in the
    // Surface Material. Do note however due to elements being potentially "empty" each Ray Portal Hit Info
    // must be checked to be empty or not before usage. Additionally both Ray Portals in a pair will match
    // in state, either being present or not.
    RayPortalHitInfo rayPortalHitInfos[uint(maxRayPortalCount)];

    // rayPortalHitInfos from the previous frame
    RayPortalHitInfo previousRayPortalHitInfos[uint(maxRayPortalCount)];

    uint32_t numActiveRayPortals = 0;
  };

  RayPortalManager(RayPortalManager const&) = delete;
  RayPortalManager& operator=(RayPortalManager const&) = delete;

  RayPortalManager(DxvkDevice* device, ResourceCache* pResourceCache);
  ~RayPortalManager();

  // Called whenever an instance is updated, used to set new Ray Portal information each frame
  void processRayPortalData(RtInstance& instance, const RtSurfaceMaterial& material);

  // Updates the camera state due to any teleportation frame to frame
  // Returns true if teleportation occured and was handled
  bool detectTeleportationAndCorrectCameraHistory(RtCamera& camera, RtCamera* viewmodelCamera);

  // Fixes camera in-between portals by pushing it out to closest portal plane
  void fixCameraInBetweenPortals(RtCamera& camera) const;

  bool tryMatchCameraToPortal(const CameraManager& cameraManager, const Matrix4& worldToView, uint8_t& portalIndex) const;

  // Get the Ray Portal information to use for drawing
  const SceneData& getRayPortalInfoSceneData() const { return m_sceneData; }
  const RayPortalInfosType& getRayPortalInfos() const { return m_rayPortalInfos; }
  const RayPortalPairInfosType& getRayPortalPairInfos() const { return m_rayPortalPairInfos; }
  bool areAnyRayPortalPairsActive() const;
  static uint32_t getRayPortalPairPortalBaseIndex(uint32_t pairIndex) { return pairIndex * 2; }

  const SingleRayPortalDirectionInfo* getCameraTeleportationRayPortalDirectionInfo() const { return m_cameraTeleportationRayPortalDirectionInfo; }
  
  void clear();
  void garbageCollection();
  void prepareSceneData(Rc<DxvkContext> ctx);
  void createVirtualCameras(CameraManager& cameraManager) const;

  // Helpers
  static float calculateDistanceAlongNormalToPortal(const Vector3& p, const RayPortalInfo& portal);
  static bool isInFrontOfPortal(const Vector3& p, const RayPortalInfo& portal, const float distanceThreshold = 0.f);
  static Vector3 getVirtualPosition(const Vector3& p, const Matrix4& portalToOpposingPortal);
private:

  static Vector3 calculateRayOriginOffset(const Vector3& centroid, const Vector3& planeNormal);

  SceneData m_sceneData;

  ResourceCache* m_pResourceCache;

  // Active portal state during frame recording
  RayPortalInfosType m_rayPortalInfos{};

  // Portal pair infos persist until to their point of recreation every frame
  // so that the previous frame versions can be used during frame recording
  RayPortalPairInfosType m_rayPortalPairInfos{};

  uint32_t m_numFramesSinceTeleportationWasDetected = 0;
  const float kCameraDepthPenetrationThreshold;

  // Valid pointer points to direction info used for camera teleportation in the frame
  SingleRayPortalDirectionInfo* m_cameraTeleportationRayPortalDirectionInfo = nullptr;
};

}  // namespace dxvk

