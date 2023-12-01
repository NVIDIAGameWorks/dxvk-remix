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

#include "rtx_matrix_helpers.h"

#include "dxvk_device.h"

namespace {
  constexpr float kFovToleranceRadians = 0.001f;

  constexpr float kCameraSimilarityDistanceThreshold = 1.0f;

  auto areClose(const dxvk::Vector3& a, const dxvk::Vector3& b) {
    return lengthSqr(a - b) < kCameraSimilarityDistanceThreshold;
  }
}

namespace dxvk {

  CameraManager::CameraManager(DxvkDevice* device) : CommonDeviceObject(device) {
    for (int i = 0; i < CameraType::Count; i++) {
      m_cameras[i].setCameraType(CameraType::Enum(i));
    }
  }

  bool CameraManager::isCameraValid(CameraType::Enum cameraType) const {
    assert(cameraType < CameraType::Enum::Count);
    return accessCamera(*this, cameraType).isValid(m_device->getCurrentFrameId());
  }

  void CameraManager::onFrameEnd() {
    m_was3DSkyInPrevFrame = (m_camerasInfoAccum.uniquePositions >= 2);
    m_camerasInfoAccum.uniquePositions = 0;

    m_lastSetCameraType = CameraType::Unknown;
  }

  CameraType::Enum CameraManager::processCameraData(const DrawCallState& input) {
    // If theres no real camera data here - bail
    if (isIdentityExact(input.getTransformData().viewToProjection)) {
      return input.testCategoryFlags(InstanceCategories::Sky) ? CameraType::Sky : CameraType::Unknown;
    }

    switch (RtxOptions::Get()->fusedWorldViewMode()) {
    case FusedWorldViewMode::None:
      if (input.getTransformData().objectToView == input.getTransformData().objectToWorld && !isIdentityExact(input.getTransformData().objectToView)) {
        return input.testCategoryFlags(InstanceCategories::Sky) ? CameraType::Sky : CameraType::Unknown;
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

    // Filter invalid cameras, extreme shearing
    static auto isFovValid = [](float fovA) {
      return fovA >= kFovToleranceRadians;
    };
    static auto areFovsClose = [](float fovA, const RtCamera& cameraB) {
      return std::abs(fovA - cameraB.getFov()) < kFovToleranceRadians;
    };

    if (abs(shearX) > 0.01f || !isFovValid(fov)) {
      ONCE(Logger::warn("[RTX] CameraManager: rejected an invalid camera"));
      return input.getCategoryFlags().test(InstanceCategories::Sky) ? CameraType::Sky : CameraType::Unknown;
    }

    const uint32_t frameId = m_device->getCurrentFrameId();
    
    auto isViewModel = [this](float fov, float maxZ, uint32_t frameId) {
      if (RtxOptions::ViewModel::enable()) {
        // Note: max Z check is the top-priority
        if (maxZ <= RtxOptions::ViewModel::maxZThreshold()) {
          return true;
        }
        if (getCamera(CameraType::Main).isValid(frameId)) {
          // FOV is different from Main camera => assume that it's a ViewModel one
          if (!areFovsClose(fov, getCamera(CameraType::Main))) {
            return true;
          }
        }
      }
      return false;
    };

    auto isSky = [this](const DrawCallState& state, uint32_t frameId, bool zEnable, const auto& drawCallCameraPos) {
      if (state.testCategoryFlags(InstanceCategories::Sky)) {
        return true;
      }

      if (RtxOptions::skyAutoDetect() == SkyAutoDetectMode::None) {
        return false;
      }

      // if already done with sky, assume all consequent draw calls as non-sky
      if (getCamera(CameraType::Main).isValid(frameId) || getCamera(CameraType::ViewModel).isValid(frameId)) {
        return false;
      }

      const auto isFirstDrawCall = !getCamera(CameraType::Sky).isValid(frameId);

      if (RtxOptions::skyAutoDetect() == SkyAutoDetectMode::CameraPositionAndDepthFlags) {
        // if first processable draw call, or if there was no sky at all
        if (isFirstDrawCall || !m_was3DSkyInPrevFrame) {
          // z disabled: frame starts with a sky
          // z enabled: frame starts with a world, no sky
          return !zEnable;
        }
      } else if (RtxOptions::skyAutoDetect() == SkyAutoDetectMode::CameraPosition) {
        if (isFirstDrawCall) {
          // assume first camera to be sky
          return true;
        }
        if (!m_was3DSkyInPrevFrame) {
          // if there was no sky camera at all => assume no sky
          return false;
        }
      } else {
        ONCE(Logger::warn("[RTX] Found incorrect skyAutoDetect value"));
        return false;
      }

      assert(m_was3DSkyInPrevFrame);
      if (drawCallCameraPos) {
        // if new camera is far from existing sky camera => found a new camera that should not be sky
        if (!areClose(getCamera(CameraType::Sky).getPosition(false), *drawCallCameraPos)) {
          return false;
        }
      }

      return true;
    };

    static auto makeCameraPosition = [](const Matrix4& worldToView, bool zWrite, bool alphaBlend) -> std::optional<Vector3> {
      // particles
      if (!zWrite && alphaBlend) {
        return {};
      }
      // identity matrix
      if (isIdentityExact(worldToView)) {
        return {};
      }
      return (inverse(worldToView))[3].xyz();
    };

    // Note: don't calculate position, if sky detect is not automatic
    const auto drawCallCameraPos =
      RtxOptions::skyAutoDetect() != SkyAutoDetectMode::None
        ? makeCameraPosition(input.getTransformData().worldToView, input.zWriteEnable, input.alphaBlendEnable)
        : std::nullopt;
    
    if (drawCallCameraPos) {
      if (m_camerasInfoAccum.uniquePositions == 0 || !areClose(m_camerasInfoAccum.lastPosition, *drawCallCameraPos)) {
        m_camerasInfoAccum.uniquePositions++;
        m_camerasInfoAccum.lastPosition = *drawCallCameraPos;
      }
    }
    
    assert(isFovValid(fov));

    const auto cameraType =
      isSky(input, frameId, input.zEnable, drawCallCameraPos) ? CameraType::Sky
      : isViewModel(fov, input.maxZ, frameId)
        ? CameraType::ViewModel
        : CameraType::Main;
    
    // Check fov consistency across frames
    if (frameId > 0) {
      if (getCamera(cameraType).isValid(frameId - 1) && !areFovsClose(fov, getCamera(cameraType))) {
        ONCE(Logger::warn("[RTX] CameraManager: FOV of a camera changed between frames"));
      }
    }

    auto& camera = getCamera(cameraType);
    auto cameraSequence = RtCameraSequence::getInstance();
    bool shouldUpdateMainCamera = cameraType == CameraType::Main && camera.getLastUpdateFrame() != frameId;
    bool isPlaying = RtCameraSequence::mode() == RtCameraSequence::Mode::Playback;
    bool isBrowsing = RtCameraSequence::mode() == RtCameraSequence::Mode::Browse;
    bool isCameraCut = false;
    Matrix4 worldToView = input.getTransformData().worldToView;
    Matrix4 viewToProjection = input.getTransformData().viewToProjection;
    if (isPlaying || isBrowsing) {
      if (shouldUpdateMainCamera) {
        RtCamera::RtCameraSetting setting;
        cameraSequence->getRecord(cameraSequence->currentFrame(), setting);
        isCameraCut = camera.updateFromSetting(frameId, setting, 0);

        if (isPlaying) {
          cameraSequence->goToNextFrame();
        }
      }
    } else {
      isCameraCut = camera.update(frameId,
                                          worldToView,
                                          viewToProjection,
                                          fov,
                                          aspectRatio,
                                          nearPlane,
                                          farPlane,
                                          isLHS);
    }


    if (shouldUpdateMainCamera && RtCameraSequence::mode() == RtCameraSequence::Mode::Record) {
      auto& setting = camera.getSetting();
      cameraSequence->addRecord(setting);
    }

    // Register camera cut when there are significant interruptions to the view (like changing level, or opening a menu)
    if (isCameraCut && cameraType == CameraType::Main) {
      m_lastCameraCutFrameId = m_device->getCurrentFrameId();
    }
    m_lastSetCameraType = cameraType;

    return cameraType;
  }

  bool CameraManager::isCameraCutThisFrame() const {
    return m_lastCameraCutFrameId == m_device->getCurrentFrameId();
  }

  void CameraManager::processExternalCamera(CameraType::Enum type,
                                            const Matrix4& worldToView,
                                            const Matrix4& viewToProjection) {
    float fov, aspectRatio, nearPlane, farPlane, shearX, shearY;
    bool isLHS;
    bool isReverseZ;
    decomposeProjection(viewToProjection, aspectRatio, fov, nearPlane, farPlane, shearX, shearY, isLHS, isReverseZ);

    getCamera(type).update(
      m_device->getCurrentFrameId(),
      worldToView, viewToProjection, fov, aspectRatio, nearPlane, farPlane, isLHS);
  }
}  // namespace dxvk
