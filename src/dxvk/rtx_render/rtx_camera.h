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

#include "rtx_option.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "../util/rc/util_rc.h"
#include <chrono>
#include "glm/glm.hpp"
#include "rtx/concept/camera/camera.h"
#include "rtx_types.h"

namespace dxvk
{

  class HaltonSamplePattern
  {
  public:
    HaltonSamplePattern(uint32_t sampleCount);

    HaltonSamplePattern() = default;
    ~HaltonSamplePattern() = default;

    uint32_t getSampleCount() const { return mSampleCount; }

    void reset(uint32_t startID = 0) { mCurSample = 0; }

    glm::vec2 next();

  protected:
    uint32_t mCurSample = 0;
    uint32_t mSampleCount;
  };

  class RtCamera : public RcObject
  {
    RTX_OPTION_ENV("rtx.camera", bool, enableFreeCamera, false, "RTX_ENABLE_FREE_CAMERA", "Enable free camera.");
    RW_RTX_OPTION_ENV("rtx.camera", Vector3, freeCameraPosition, Vector3(0.f, 0.f, 0.f), "RTX_FREE_CAMERA_POSITION", "Free camera's position.");
    RW_RTX_OPTION_ENV("rtx.camera", float, freeCameraYaw, 0.f, "RTX_FREE_CAMERA_YAW", "Free camera's position.");
    RW_RTX_OPTION_ENV("rtx.camera", float, freeCameraPitch, 0.f, "RTX_FREE_CAMERA_PITCH", "Free camera's pitch.");
    RW_RTX_OPTION("rtx.camera", bool, lockFreeCamera, false, "Locks free camera.");
    RW_RTX_OPTION("rtx.camera", bool, freeCameraViewRelative, true, "Free camera transform is relative to the view.");
    RW_RTX_OPTION("rtx", float, freeCameraSpeed, 200, "Free camera speed [GameUnits/s].");

    long m_mouseX = 0, m_mouseY = 0;
    uint32_t m_renderResolution[2] = { 0 };
    uint32_t m_finalResolution[2] = { 0 };
    float m_jitter[2] = { 0 };
    bool m_isLHS = false;
    float m_nearPlane = 0.0f;
    float m_farPlane = 0.0f;
    HaltonSamplePattern m_halton;
    bool m_firstUpdate = true;
    int m_cameraShakeFrameCount = 0;
    int m_cameraRotationFrameCount = 0;
    Vector4 m_shakeOffset = { 0.0f,0.0f,0.0f,0.0f };
    float m_shakeYaw = 0.0f, m_shakePitch = 0.0f;
    float m_fov = 0.f;
    float m_aspectRatio = 0.f;

    enum MatrixType {
      WorldToView = 0,
      PreviousWorldToView,
      PreviousPreviousWorldToView,
      ViewToWorld,
      PreviousViewToWorld,
      PreviousPreviousViewToWorld,
      UncorrectedPreviousViewToWorld,

      TranslatedWorldToView,
      PreviousTranslatedWorldToView,
      UncorrectedPreviousTranslatedWorldToView,
      ViewToTranslatedWorld,
      PreviousViewToTranslatedWorld,

      ViewToProjection,
      PreviousViewToProjection,
      ProjectionToView,
      PreviousProjectionToView,

      ViewToProjectionJittered,
      PreviousViewToProjectionJittered,
      ProjectionToViewJittered,
      PreviousProjectionToViewJittered,

      FreeCamViewToWorld,
      FreeCamPreviousViewToWorld,
      FreeCamPreviousPreviousViewToWorld,
      FreeCamWorldToView,
      FreeCamPreviousWorldToView,
      FreeCamPreviousPreviousWorldToView,

      FreeCamViewToTranslatedWorld,
      FreeCamPreviousViewToTranslatedWorld,
      FreeCamTranslatedWorldToView,
      FreeCamPreviousTranslatedWorldToView,

      Count
    };

    Matrix4 m_matCache[MatrixType::Count];

    cFrustum m_frustum;

    // Captures any artificial offsets applied on top of the input transfrom 
    // from the game engine.
    Vector3 m_artificalWorldOffset = Vector3(0.f);
    Vector3 m_previousArtificalWorldOffset = Vector3(0.f);

    // Note: Start the camera off as invalid until it is set properly.
    uint32_t m_frameLastTouched = kInvalidFrameIndex;
    std::chrono::time_point<std::chrono::system_clock> m_prevRunningTime;

  public:

    RtCamera();

    // Gets the Y axis (vertical) FoV of the camera's projection matrix in radians. Note this value will be positive always (even with strange camera types).
    inline const float getFov() const { return m_fov; }
    inline const float getAspectRatio() const { return m_aspectRatio; }

    const Matrix4& getWorldToView(bool freecam = true) const;
    const Matrix4& getPreviousWorldToView(bool freecam = true) const;
    const Matrix4& getPreviousPreviousWorldToView(bool freecam = true) const;
    const Matrix4& getViewToWorld(bool freecam = true) const;
    const Matrix4& getPreviousViewToWorld(bool freecam = true) const;
    const Matrix4& getPreviousPreviousViewToWorld(bool freecam = true) const;

    const Matrix4& getTranslatedWorldToView(bool freecam = true) const;
    const Matrix4& getPreviousTranslatedWorldToView(bool freecam = true) const;
    const Matrix4& getViewToTranslatedWorld(bool freecam = true) const;
    const Matrix4& getPreviousViewToTranslatedWorld(bool freecam = true) const;

    const Matrix4& getViewToProjection() const { return m_matCache[MatrixType::ViewToProjection]; }
    const Matrix4& getPreviousViewToProjection() const { return m_matCache[MatrixType::PreviousViewToProjection]; }
    const Matrix4& getProjectionToView() const { return m_matCache[MatrixType::ProjectionToView]; }
    const Matrix4& getPreviousProjectionToView() const { return m_matCache[MatrixType::PreviousProjectionToView]; }

    inline const cFrustum& getFrustum() const { return m_frustum; }
    inline cFrustum& getFrustum() { return m_frustum; }

    void setPreviousWorldToView(const Matrix4& worldToView, bool freecam = true);
    void setPreviousViewToWorld(const Matrix4& viewToWorld, bool freecam = true);

    // Applies a world offset to the current frame on top of a transform set by the engine during update()
    // This must be called after any camera transform changes of the camera in a frame.
    // The transforms must not be externally updated further after applying this offset in the frame
    void applyArtificialWorldOffset(const Vector3& worldOffset);
    const Vector3& getArtificialWorldOffset() const { return m_artificalWorldOffset; }
    const Vector3& getPreviousArtificialWorldOffset() const { return m_previousArtificalWorldOffset; }

    bool isValid(const uint32_t frameIdx) const { return m_frameLastTouched == frameIdx; }

    const Vector3& getPosition(bool freecam = true) const;
    Vector3 getDirection(bool freecam = true) const;
    Vector3 getUp(bool freecam = true) const;
    Vector3 getRight(bool freecam = true) const;
    float getNearPlane() const { return m_nearPlane; }
    float getFarPlane() const { return m_farPlane; }

    bool isCameraCut() const;
    static bool isFreeCameraEnabled();
    Vector3 getHorizontalForwardDirection() const;

    const Matrix4& getViewToProjectionJittered() const { return m_matCache[MatrixType::ViewToProjectionJittered]; }
    const Matrix4& getPreviousViewToProjectionJittered() const { return m_matCache[MatrixType::PreviousViewToProjectionJittered]; }
    const Matrix4& getProjectionToViewJittered() const { return m_matCache[MatrixType::ProjectionToViewJittered]; }
    const Matrix4& getPreviousProjectionToViewJittered() const { return m_matCache[MatrixType::PreviousProjectionToViewJittered]; }

    void setResolution(const uint32_t renderResolution[2], const uint32_t finalResolution[2]);
    bool update(
      uint32_t frameIdx, const Matrix4& newWorldToView, const Matrix4& newViewToProjection,
      float fov, float aspectRatio, float nearPlane, float farPlane, bool isLHS
    );
    void getJittering(float jitter[2]) const;
    bool isLHS() const { return m_isLHS; }

    Camera getShaderConstants() const;
    VolumeDefinitionCamera getVolumeShaderConstants() const;

    static void showImguiSettings();
  private:
    Matrix4 getShakenViewToWorldMatrix(Matrix4& viewToWorld);
  };
}