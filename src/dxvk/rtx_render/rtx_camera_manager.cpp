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

    if (std::abs(shearX) > 0.01f || !isFovValid(fov)) {
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

    auto& skyCamera = getCamera(CameraType::Sky);
    auto& mainCamera = getCamera(CameraType::Main);

    // Firstly, don't try skyscale if we're not pathtracing
    // Additionally, don't try sky scale if we have no sky camera, or the main camera is invalid
    // Finally, don't do scale calculations if the current camera isn't the main camera
    auto shouldCalculateSkyScale = RtxOptions::Get()->skyBoxPathTracing()
      && skyCamera.isValid(frameId)
      && mainCamera.isValid(frameId)
      && cameraType == CameraType::Main;

    if (shouldCalculateSkyScale) {

      // Fallback to the default scale if the camera is odd, but let the formula work after
      if (isCameraCut)
        skyCamera.setSkyScale(RtxOptions::Get()->skyDefaultScale());

      auto curCamPos = (Vector3) mainCamera.getPosition(false);
      auto curSkyPos = (Vector3) skyCamera.getPosition(false);
      auto lastCamPos = (Vector3) inverse(mainCamera.getPreviousWorldToView(false))[3].xyz();;
      auto lastSkyPos = (Vector3) inverse(skyCamera.getPreviousWorldToView(false))[3].xyz();

      bool uniqueCamerasFound =
        !areClose(curCamPos, curSkyPos) &&
        !areClose(curSkyPos, lastCamPos);

      /*
        Do not do any transitioning if the main and skycams are equal
        or have been equal last frame! Fixes occasions where first-view
        gets mistaken as the main view instead of sky view
      */
      if (uniqueCamerasFound) {

        switch (RtxOptions::Get()->skyScaleCalibrationMode()) {

        case SkyScaleCalibrationMode::Fixed:
          skyCamera.setSkyScale(RtxOptions::Get()->skyDefaultScale());
          break;
        case SkyScaleCalibrationMode::DeltaAutomatic:
          if (!areClose(curSkyPos, lastSkyPos) && !areClose(curCamPos, lastCamPos)) {

            float mdiff = lengthSqr(curCamPos - lastCamPos);
            float sdiff = lengthSqr(curSkyPos - lastSkyPos);

            if (sdiff != 0 && mdiff != 0) {
              float ratio_scale = mdiff / sdiff;
              int new_scale = ratio_scale >= 1 ? static_cast<int>(std::sqrt(ratio_scale)) : skyCamera.m_lastSkyScale;
              skyCamera.setSkyScale(new_scale);
            }
          }
          break;
        case SkyScaleCalibrationMode::SourceEngineAutomatic:
          static auto getDecimal = [](double val) {
            double workable = std::abs(val);
            return workable - std::floor(workable);
          };

          static auto huntForScale = [](double skyPos, double mainPos) {
            double error = 0.0001;
            double mainDecimal = getDecimal(mainPos);
            double skyDecimal = getDecimal(skyPos);
            if (std::abs(mainDecimal - skyDecimal) < error)
              return -1;

            bool shouldInvert = std::signbit(mainPos) != std::signbit(skyPos);

            float max_scale = 64; // Source typically uses power of 2, and must be an even integer
            double max_square = std::sqrt(max_scale);
            for (int i = 0; i < max_square - 1; i++) {
              int predicted_scale = static_cast<int>(std::pow(2, i));
              double predicted_skypos = mainPos / predicted_scale;
              double predicted_skydec = getDecimal(predicted_skypos);

              if (shouldInvert)
                predicted_skydec = std::abs(-1 * predicted_skydec + 1); //Produces the opposite during offset

              if (std::abs(predicted_skydec - skyDecimal) < error)
                return predicted_scale;

            };

            return -1;
          };

          auto corroborateScale = [skyCamera](int scalex, int scaley, int scalez) {
            if (scalex == scaley && scaley == scalez && scalex > 0)
              return scalex;

            if (scalex == scalez && scalex > 0) {
              if (scaley < 0)
                return scalex;
            };

            if (scalex == scaley && scaley > 0) {
              if (scalez < 0)
                return scalex;
            };

            if (scaley == scalez && scalez > 0) {
              if (scalex < 0)
                return scaley;
            };

            if (scalex < 0 && scaley < 0 && scalez > 0)
              return scalez;

            if (scalex < 0 && scalez < 0 && scaley > 0)
              return scaley;

            if (scaley < 0 && scalez < 0 && scalex > 0)
              return scalex;

            return -1;
          };


          int scalex = huntForScale(curSkyPos.x, curCamPos.x);
          int scaley = huntForScale(curSkyPos.y, curCamPos.y);
          int scalez = huntForScale(curSkyPos.z, curCamPos.z);
          int scale = corroborateScale(scalex, scaley, scalez);
          //  float sky_denom = getDenominator(skyref);
          //  float ply_denom = getDenominator(plyref);

          //float scale = 1;
          //if (skyref != plyref) {
          //  float sky_denom = getDenominator(skyref);
          //  float ply_denom = getDenominator(plyref);
          //  scale = sky_denom > ply_denom ? sky_denom / ply_denom : ply_denom / sky_denom;
          //}
          if (scale < 0)
            scale = skyCamera.m_lastSkyScale;
          skyCamera.setSkyScale(scale);
          break;
        }
      }

      float skyScale = static_cast<float>(skyCamera.m_skyScale);
      switch (RtxOptions::Get()->skyScaleOffsetFormula()) {
      case SkyScaleOffsetFormula::Origin:
        skyCamera.setSkyOffset(Vector3(0, 0, 0));
        break;

      case SkyScaleOffsetFormula::SourceEngine:

        // Shift by sky scale to get the post scaled value
        curCamPos.x *= 1 / skyScale;
        curCamPos.y *= 1 / skyScale;
        curCamPos.z *= 1 / skyScale;

        // Everything after is linear so don't break out of this one
      case SkyScaleOffsetFormula::Linear:
        auto offset = curCamPos - curSkyPos;
        if (!areClose(skyCamera.m_skyOffset, offset))
          skyCamera.setSkyOffset(offset);
        break;
      }


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
