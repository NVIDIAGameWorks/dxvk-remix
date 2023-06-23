/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_camera_manager.h"
#include "dxvk_device.h"
#include "rtx_matrix_helpers.h"

namespace dxvk {

  CameraManager::CameraManager(DxvkDevice* device) : CommonDeviceObject(device) {
    initSettings(m_device->instance()->config());
  }

  bool CameraManager::isCameraValid(CameraType::Enum cameraType) const {
    return m_cameras[cameraType].has_value() &&
      (**m_cameras[cameraType]).isValid(m_device->getCurrentFrameId());
  }

  void CameraManager::initSettings(const dxvk::Config& config) {

    m_cameras[CameraType::Main] = new RtCamera();

    if (m_rayPortalEnabled.getValue()) {
      m_cameras[CameraType::Portal0] = new RtCamera();
      m_cameras[CameraType::Portal1] = new RtCamera();
    }

    // Default Uknown to Main camera object
    // since cameras can get rejected but rtx pipeline can 
    // still try to retrieve a camera corresponding to a DrawCall object.
    // In that case it will read from the Main camera.
    // This is OK as we never update Unknown camera directly.
    m_cameras[CameraType::Unknown] = m_cameras[CameraType::Main];

  }

  void CameraManager::onFrameEnd() {
    m_lastSetCameraType = CameraType::Unknown;
    m_cameraHashToType.clear();

    if (m_trackCamerasSeenStats.getValue()) {

      Logger::info("[RTX] CameraManager: view transforms seen this frame:");
      Logger::info(m_viewTransformSeen);

      Logger::info("[RTX] CameraManager: proj params seen this frame:");
      Logger::info(m_projParamsSeen);

      m_projParamsSeen = "";
      m_lastProjParamsSeen = "";
      m_viewTransformSeen = "";
      m_lastViewTransformParamsSeen = "";
    }
  }

  // Returns true on camera cut
  bool CameraManager::processCameraData(const DrawCallState& input) {
    // Skip camera processing for the sky to prevent camera manager from latching to incorrect main camera.
    if (input.isSky)
      return false;

    // If theres no real camera data here - bail
    if (isIdentityExact(input.getTransformData().viewToProjection))
      return false;

    switch (RtxOptions::Get()->fusedWorldViewMode()) {
    case FusedWorldViewMode::None:
      if (input.getTransformData().objectToView == input.getTransformData().objectToWorld &&
          !isIdentityExact(input.getTransformData().objectToView)) {
        return false;
      }
      break;
    case FusedWorldViewMode::View:
      if (Logger::logLevel() >= LogLevel::Warn) {
        // Check if World is identity
        ONCE_IF_FALSE(isIdentityExact(input.getTransformData().objectToWorld),
                      Logger::warn("[RTX-Compatibility] Fused world-view tranform set to View but World transform is not identity!"));
      }
      break;
    case FusedWorldViewMode::World:
      if (Logger::logLevel() >= LogLevel::Warn) {
        // Check if View is identity
        ONCE_IF_FALSE(isIdentityExact(input.getTransformData().objectToView),
                      Logger::warn("[RTX-Compatibility] Fused world-view tranform set to World but View transform is not identity!"));
      }
      break;
    }

    // Get camera params
    float fov, aspectRatio, nearPlane, farPlane, shearX, shearY;
    bool isLHS;
    bool isReverseZ;
    decomposeProjection(input.getTransformData().viewToProjection, aspectRatio, fov, nearPlane, farPlane, shearX, shearY, isLHS, isReverseZ);

    if (m_trackCamerasSeenStats.getValue()) {
      std::string projParamsDesc = str::format("fv:", fov, ", np : ", nearPlane, ", fp : ", farPlane, ", lhs : ", isLHS, ", reverseZ : ", isReverseZ, " }");

      if (projParamsDesc != m_lastProjParamsSeen) {
        m_projParamsSeen += "\n" + projParamsDesc;
        m_lastProjParamsSeen = projParamsDesc;
      }

      auto& v0 = input.getTransformData().worldToView.data[0];
      auto& v1 = input.getTransformData().worldToView.data[1];
      auto& v2 = input.getTransformData().worldToView.data[2];
      auto& v3 = input.getTransformData().worldToView.data[3];

      auto printVector = [=](Vector4 v) {
        return str::format("{", v.x, ",", v.y, ",", v.z, ",", v.w, "}");
      };

      std::string viewTransformDesc = str::format("{", 
        printVector(v0), ",", printVector(v1), ",", printVector(v2), ",", printVector(v3), "}");

      if (viewTransformDesc != m_lastViewTransformParamsSeen) {
        m_viewTransformSeen += "\n" + viewTransformDesc;
        m_lastViewTransformParamsSeen = viewTransformDesc;
      }
    }

    // Filter invalid cameras, extreme shearing
    constexpr float kFovToleranceRadians = 0.001f;
    if (abs(shearX) > 0.01f || fov < kFovToleranceRadians) {
      ONCE(Logger::warn("[RTX] Camera Manager: rejected an invalid camera"));
      return false;
    }

    const uint32_t frameId = m_device->getCurrentFrameId();

    // Classify the camera
    // ToDo: we should default to unknown camera and discard renders that don't match with cameras
    CameraType::Enum cameraType = CameraType::Main;  

    XXH64_hash_t cameraHash = calculateCameraHash(fov, input.getTransformData().worldToView, input.stencilEnabled);
    {
      auto isTheCameraTypeKnownAlready = [&](CameraType::Enum type) {
        for (auto& it : m_cameraHashToType) {
          if (it.second == type) {
            return true;
          }
        }
        return false;
      };

      // First valid camera in a frame, we currently expect it to be the main camera
      // If FOV is 0, just default to the main camera
      if (m_lastSetCameraType == CameraType::Unknown || fov < kFovToleranceRadians) {
        cameraType = CameraType::Main;
      } 
      else {
        // See if the camera is already registered
        auto cameraTypeIter = m_cameraHashToType.find(cameraHash);
        if (cameraTypeIter != m_cameraHashToType.end()) {
          cameraType = cameraTypeIter->second;
        } else {
          // FOV is different from Main camera => consider it View Model
          if (abs(fov - getCamera(CameraType::Main).getFov()) > kFovToleranceRadians) {
            if (RtxOptions::Get()->isViewModelEnabled()) {
              if (!m_cameras[CameraType::ViewModel].has_value())
                m_cameras[CameraType::ViewModel] = new RtCamera();

              if (getCamera(CameraType::ViewModel).isValid(frameId) && (fov - getCamera(CameraType::ViewModel).getFov() > kFovToleranceRadians)) {
                ONCE(Logger::warn("[RTX] Camera Manager: Multiple ViewModel camera candidates found. Secondary candidates are defaulted to Main camera. "));
                cameraType = CameraType::Main;
              } else {
                cameraType = CameraType::ViewModel;
              }
            }
          }

          if (isTheCameraTypeKnownAlready(cameraType))
            ONCE(Logger::warn("[RTX] CameraManager: Multiple different cameras were matched against a same camera type"));
        } 
      }
    }

    // Check fov consistency accross frames
    if (frameId > 0 && getCamera(cameraType).isValid(frameId - 1) && (fov - getCamera(cameraType).getFov() > kFovToleranceRadians))
      ONCE(Logger::warn("[RTX] Camera Manager: FOV of a camera changed between frames"));

    m_cameraHashToType[cameraHash] = cameraType;
    m_lastSetCameraType = cameraType;

    // Don't update unknown camera, it's only used for last set camera state tracking
    if (cameraType == CameraType::Unknown) {
      ONCE(Logger::warn("[RTX] Camera Manager: failed to match a camera"));
      return false;
    }

    const bool isCameraCut = getLastSetCamera().update(
      frameId, input.getTransformData().worldToView, input.getTransformData().viewToProjection,
      fov, aspectRatio, nearPlane, farPlane, isLHS);

    // Register camera cut when there are significant interruptions to the view (like changing level, or opening a menu)
    if (isCameraCut && getLastSetCameraType() == CameraType::Main) {
      m_lastCameraCutFrameId = m_device->getCurrentFrameId();
    }

    return isCameraCut;
  }

  XXH64_hash_t CameraManager::calculateCameraHash(float fov, const Matrix4& worldToView, bool stencilEnabledState) {
    XXH64_hash_t h = 0;
    h = XXH64(&fov, sizeof(fov), h);
    h = XXH64(&worldToView, sizeof(worldToView), h);
    h = XXH64(&stencilEnabledState, sizeof(stencilEnabledState), h);

    return h;
  }

  bool CameraManager::isCameraCutThisFrame() const {
    return m_lastCameraCutFrameId == m_device->getCurrentFrameId();
  }

}  // namespace dxvk
