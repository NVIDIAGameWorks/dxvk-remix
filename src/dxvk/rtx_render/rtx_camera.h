/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_option.h"

#include "rtx/concept/camera/camera.h"

#include "../util/util_vector.h"
#include "../util/util_matrix.h"

#include <chrono>

namespace dxvk
{
  namespace CameraType {
    enum Enum : uint32_t {
      Main = 0,        // Main camera
      ViewModel,       // Camera for view model rendering
      Portal0,         // Camera associated with rendering portal 0
      Portal1,         // Camera associated with rendering portal 1
      Sky,             // Some renderers have separate world / sky cameras
      RenderToTexture, // Camera used to replace a render target texture that is being raytraced.
      Unknown,         // Unset camera state, used mainly for state tracking. Its camera object is aliased 
                       // with the Main camera object, so on access it retrieves the Main camera

      Count
    };
  }

  class HaltonSamplePattern
  {
  public:
    explicit HaltonSamplePattern(uint32_t sampleCount);

    HaltonSamplePattern() = default;
    ~HaltonSamplePattern() = default;

    uint32_t getSampleCount() const { return mSampleCount; }

    void reset() { mCurSample = 0; }

    glm::vec2 next();

  protected:
    uint32_t mCurSample = 0;
    uint32_t mSampleCount = 0;
  };


  // Returns a 2D <-0.5, 0.5> Halton jitter sample 
  Vector2 calculateHaltonJitter(uint32_t currentFrame, uint32_t jitterSequenceLength);

  class RtFrustum final : public cFrustum
  {
  public:
    void calculateFrustumGeometry(const float nearPlane, const float farPlane, const float fov, const float aspectRatio, const bool isLHS);

    inline const Vector3& getNearPlaneFrustumVertex(const uint32_t index) const {
      assert(index >= 0 && index <= 4);
      return nearPlaneFrustumVertices[index];
    }

    inline const Vector3& getFarPlaneFrustumVertex(const uint32_t index) const {
      assert(index >= 0 && index <= 4);
      return farPlaneFrustumVertices[index];
    }

    inline const Vector3& getFrustumEdgeVector(const uint32_t index) const {
      assert(index >= 0 && index <= 4);
      return frustumEdgeVectors[index];
    }

    inline const float getNearPlaneRightExtent() const { return nearPlaneRightExtent; }
    inline const float getNearPlaneUpExtent() const { return nearPlaneUpExtent; }
    inline const float getFarPlaneRightExtent() const { return farPlaneRightExtent; }
    inline const float getFarPlaneUpExtent() const { return farPlaneUpExtent; }

  private:
    // View Space Frustum data caches
    Vector3 nearPlaneFrustumVertices[4];
    Vector3 farPlaneFrustumVertices[4];
    Vector3 frustumEdgeVectors[4];
    float nearPlaneRightExtent = 0.0f;
    float nearPlaneUpExtent = 0.0f;
    float farPlaneRightExtent = 0.0f;
    float farPlaneUpExtent = 0.0f;
  };

  class RtCamera
  {
    RTX_OPTION_ENV("rtx.camera", bool, enableFreeCamera, false, "RTX_ENABLE_FREE_CAMERA", "Enables free camera.");
    RTX_OPTION_ENV("rtx.camera", Vector3, freeCameraPosition, Vector3(0.f, 0.f, 0.f), "RTX_FREE_CAMERA_POSITION", "Free camera's position.");
    RTX_OPTION_ENV("rtx.camera", float, freeCameraYaw, 0.f, "RTX_FREE_CAMERA_YAW", "Free camera's position.");
    RTX_OPTION_ENV("rtx.camera", float, freeCameraPitch, 0.f, "RTX_FREE_CAMERA_PITCH", "Free camera's pitch.");
    RTX_OPTION("rtx.camera", bool, lockFreeCamera, false, "Locks free camera.");
    RTX_OPTION("rtx.camera", bool, freeCameraViewRelative, true, "Free camera transform is relative to the view.");
    RTX_OPTION("rtx.camera", bool, useFreeCameraForComponents, true, "Use free camera for graph components when free camera is enabled.");
    RTX_OPTION("rtx", float, freeCameraSpeed, 200, "Free camera speed [GameUnits/s].");
    RTX_OPTION("rtx", float, freeCameraTurningSpeed, 1, "Free camera turning speed (applies to keyboard, not mouse) [radians/s].");
    RTX_OPTION("rtx", bool, freeCameraInvertY, false, "Invert free camera pitch direction.");

    uint32_t m_renderResolution[2] = { 0, 0 };
    uint32_t m_finalResolution[2] = { 0, 0 };
    float m_jitter[2] = { 0, 0 };
    HaltonSamplePattern m_halton = {};
    bool m_firstUpdate = true;
    CameraType::Enum m_type;

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

      ViewToWorldToFreeCamViewToWorld,

      Count
    };

    enum UpdateFlag {
      IncrementShakingFrame = 0x1,
      UpdateFreeCamera = 0x2,
      UpdateJitterFrame = 0x4,
      UpdateNormal = IncrementShakingFrame | UpdateFreeCamera | UpdateJitterFrame
    };


    struct RtCameraSetting {
    public:
      // Input matrix
      Matrix4 worldToView;
      Matrix4 viewToProjection;

      // Free camera parameters
      bool enableFreeCamera = false;
      Vector3 freeCameraPosition;
      float freeCameraYaw = 0.0f;
      float freeCameraPitch = 0.0f;
      bool freeCameraViewRelative = false;

      // Jitter
      float jitter[2];

      // Shaking parameters
      bool isCameraShaking = false;
      int cameraShakeFrameCount = 0;
      int cameraRotationFrameCount = 0;

      // input parameter
      uint32_t jitterFrameIdx = 0;
      float fov;
      float aspectRatio;
      float nearPlane;
      float farPlane;
      bool isLHS;
      uint32_t flags;
    };

    // Note: All camera matricies stored as double precision. While this does not do much for some matricies (which were provided
    // by the application in floating point precision), it does help for preserving matrix stability on those which have been inverted,
    // as well as in code using these matrices which may do further inversions or combination operations. If such precision is not needed
    // the matrices retreived from the various getter functions can be casted to float matrices for a minor upfront performance cost.
    Matrix4d m_matCache[MatrixType::Count] = {};

    RtFrustum m_frustum;
    cFrustum m_lightAntiCullingFrustum;

    // Captures any artificial offsets applied on top of the input transfrom 
    // from the game engine.
    Vector3 m_artificalWorldOffset = Vector3(0.f);
    Vector3 m_previousArtificalWorldOffset = Vector3(0.f);

    // Note: Start the camera off as invalid until it is set properly.
    uint32_t m_frameLastTouched = kInvalidFrameIndex;
    std::chrono::time_point<std::chrono::system_clock> m_prevRunningTime = {};

  public:
    RtCamera() = default;
    ~RtCamera() = default;
    // Note: Copying functionality required for DLFG implementation (to store a copy of a camera used to render a frame for DLFG evaluation later).
    // Avoid copies otherwise as this is a fairly large structure. If needed only the data required by DLFG could be copied and this copy functionality
    // could be removed for slightly better performance.
    RtCamera(const RtCamera& other) = default;
    RtCamera& operator=(const RtCamera& other) = default;

    // Gets the Y axis (vertical) FoV of the camera's projection matrix in radians. Note this value will be positive always (even with strange camera types).
    float getFov() const { return m_context.fov; }
    float getAspectRatio() const { return m_context.aspectRatio; }

    const Matrix4d& getWorldToView(bool freecam = true) const;
    const Matrix4d& getPreviousWorldToView(bool freecam = true) const;
    const Matrix4d& getPreviousPreviousWorldToView(bool freecam = true) const;
    const Matrix4d& getViewToWorld(bool freecam = true) const;
    const Matrix4d& getPreviousViewToWorld(bool freecam = true) const;
    const Matrix4d& getPreviousPreviousViewToWorld(bool freecam = true) const;

    const Matrix4d& getTranslatedWorldToView(bool freecam = true) const;
    const Matrix4d& getPreviousTranslatedWorldToView(bool freecam = true) const;
    const Matrix4d& getViewToTranslatedWorld(bool freecam = true) const;
    const Matrix4d& getPreviousViewToTranslatedWorld(bool freecam = true) const;

    const Matrix4d& getViewToProjection() const { return m_matCache[MatrixType::ViewToProjection]; }
    const Matrix4d& getPreviousViewToProjection() const { return m_matCache[MatrixType::PreviousViewToProjection]; }
    const Matrix4d& getProjectionToView() const { return m_matCache[MatrixType::ProjectionToView]; }
    const Matrix4d& getPreviousProjectionToView() const { return m_matCache[MatrixType::PreviousProjectionToView]; }

    const Matrix4d& getViewToWorldToFreeCamViewToWorld() const;

    const RtFrustum& getFrustum() const { return m_frustum; }
    RtFrustum& getFrustum() { return m_frustum; }

    inline const cFrustum& getLightAntiCullingFrustum() const { return m_lightAntiCullingFrustum; }
    inline cFrustum& getLightAntiCullingFrustum() { return m_lightAntiCullingFrustum; }

    void setPreviousWorldToView(const Matrix4d& worldToView, bool freecam = true);
    void setPreviousViewToWorld(const Matrix4d& viewToWorld, bool freecam = true);

    // Applies a world offset to the current frame on top of a transform set by the engine during update()
    // This must be called after any camera transform changes of the camera in a frame.
    // The transforms must not be externally updated further after applying this offset in the frame
    void applyArtificialWorldOffset(const Vector3& worldOffset);
    const Vector3& getArtificialWorldOffset() const { return m_artificalWorldOffset; }
    const Vector3& getPreviousArtificialWorldOffset() const { return m_previousArtificalWorldOffset; }

    bool isValid(const uint32_t frameIdx) const { return m_frameLastTouched == frameIdx; }
    uint32_t getLastUpdateFrame() const { return m_frameLastTouched; }

    Vector3 getPosition(bool freecam = true) const;
    Vector3 getDirection(bool freecam = true) const;
    Vector3 getUp(bool freecam = true) const;
    Vector3 getRight(bool freecam = true) const;

    Vector3 getPreviousPosition(bool freecam = true) const;

    // Note: getNearPlane() / getFarPlane() return values
    // corresponding to the viewToProjection matrix passed into update(..),
    // and NOT to the viewToProjection in the m_matCache, because of 'enableNearPlaneOverride' option.
    // If need actual near/far planes corresponding to the current matrix, use calculateNearFarPlanes().
    float getNearPlane() const { return m_context.nearPlane; }
    float getFarPlane() const { return m_context.farPlane; }
    std::pair<float, float> calculateNearFarPlanes() const;

    void setCameraType(CameraType::Enum type) {
      m_type = type;
    }

    bool isCameraCut() const;
    static bool isFreeCameraEnabled();
    Vector3 getHorizontalForwardDirection() const;

    const Matrix4d& getViewToProjectionJittered() const { return m_matCache[MatrixType::ViewToProjectionJittered]; }
    const Matrix4d& getPreviousViewToProjectionJittered() const { return m_matCache[MatrixType::PreviousViewToProjectionJittered]; }
    const Matrix4d& getProjectionToViewJittered() const { return m_matCache[MatrixType::ProjectionToViewJittered]; }
    const Matrix4d& getPreviousProjectionToViewJittered() const { return m_matCache[MatrixType::PreviousProjectionToViewJittered]; }

    void setResolution(const uint32_t renderResolution[2], const uint32_t finalResolution[2]);
    bool update(
      uint32_t frameIdx, const Matrix4& newWorldToView, const Matrix4& newViewToProjection,
      float fov, float aspectRatio, float nearPlane, float farPlane, bool isLHS, uint32_t flags = (uint32_t)UpdateFlag::UpdateNormal
    );
    bool updateFromSetting(uint32_t frameIdx, const RtCameraSetting& setting, uint32_t flags = (uint32_t) UpdateFlag::UpdateNormal);
    void getJittering(float jitter[2]) const;
    bool isLHS() const { return m_context.isLHS; }

    Vector2 calcPixelJitter(uint32_t jitterFrameIdx) const;
    Vector2 calcClipSpaceJitter(const Vector2& pixelJitter, float ratioX, float ratioY) const;
    void applyJitterTo(Matrix4& inoutProjection, uint32_t jitterFrameIdx) const;
    void applyAndGetJitter(Matrix4d& inoutProjection, float (&outPixelJitter)[2], uint32_t jitterFrameIdx) const;

    Camera getShaderConstants(bool freecam = true) const;
    VolumeDefinitionCamera getVolumeShaderConstants(const float maxDistance, const float guardBand = 1.f) const;

    static void showImguiSettings();

    const RtCameraSetting& getSetting();

  private:
    Matrix4d getShakenViewToWorldMatrix(Matrix4d& viewToWorld, uint32_t flags);
    Matrix4d updateFreeCamera(uint32_t flags);
    void updateAntiCulling(float fov, float aspectRatio, float nearPlane, float farPlane, bool isLHS);
    Matrix4d overrideNearPlane(const Matrix4d& modifiedViewToProj);

    RtCameraSetting m_context;
  };

  class RtCameraSequence {
  public:
    enum class Mode {
      None,
      Record,
      Playback,
      Browse,
    };

    static RtCameraSequence* getInstance() {
      if (s_instance == nullptr) {
        s_instance = new RtCameraSequence();
      }
      return s_instance;
    }

    void reset() {
      m_currentFrame = 0;
    }

    void startRecord();
    void addRecord(const RtCamera::RtCameraSetting& setting);
    bool getRecord(int frame, RtCamera::RtCameraSetting& setting);

    void goToNextFrame();

    void showImguiSettings();

    void load();
    void save();

    void startPlay();

  private:
    // File Blocks
    // Allocate more space for future data
    union Header {
      int nElements;
      char padding[256];
    };

    union FrameData {
      RtCamera::RtCameraSetting setting;
      char padding[1024];
    };

    RtCameraSequence() { }
    ~RtCameraSequence() { }

    RTX_OPTION_ENV("rtx.cameraSequence", std::string, filePath, "", "DXVK_CAMERA_SEQUENCE_PATH", "File path.");
    RTX_OPTION_ENV("rtx.cameraSequence", bool, autoLoad, false, "DXVK_CAMERA_SEQUENCE_AUTO_LOAD", "Load camera sequence automatically.");
    RTX_OPTION_ENV("rtx.cameraSequence", Mode, mode, Mode::None, "DXVK_CAMERA_SEQUENCE_MODE", "Current mode.");
    private: static inline int m_currentFrame = 0;
    public: static int currentFrame() { return m_currentFrame; }
    
    std::vector<RtCamera::RtCameraSetting> m_settings;
    static RtCameraSequence* s_instance;
  };
}