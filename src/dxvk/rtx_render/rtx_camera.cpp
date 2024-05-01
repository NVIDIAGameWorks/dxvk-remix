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
#include "../../util/util_math.h"
#include "../../util/util_vector.h"
#include "../../util/util_matrix.h"
#include "rtx_camera.h"
#include <windows.h>
#include "rtx_options.h"
#include "rtx_matrix_helpers.h"
#include "rtx_imgui.h"

/*
*             Free/Debug Camera
* 
* NOTE: Enable with the 'rtx.useFreeCamera = True' DXVK setting
* 
* W --------------------------- Move forward
* S --------------------------- Move backward
* A --------------------------- Move left
* D --------------------------- Move right
* Q --------------------------- Move down
* E --------------------------- Move up
* Left Shift ------------------ Move speed boost (hold)
* Left click (hold) + Mouse --- Look around
* Right click ----------------- Reset camera to default
* 
*/

namespace dxvk
{
  RtCameraSequence* RtCameraSequence::s_instance = nullptr;

  Vector3 RtCamera::getPosition(bool freecam) const {
    return Vector3{ getViewToWorld(freecam)[3].xyz() };
  }

  Vector3 RtCamera::getDirection(bool freecam) const {
    // Note: To get a "forward" direction in world space, the basis vector mapping world to view space's Z axis
    // is used, but unlike the up/right vectors we must consider which direction the projection matrix is treating as
    // forwards. With left handed projection matrices this is the +Z axis, but with right handed matrices this is -Z.
    if (m_context.isLHS) {
      return Vector3{ getViewToWorld(freecam)[2].xyz() };
    } else {
      return -Vector3{ getViewToWorld(freecam)[2].xyz() };
    }
  }

  Vector3 RtCamera::getUp(bool freecam) const {
    return Vector3{ getViewToWorld(freecam)[1].xyz() };
  }

  Vector3 RtCamera::getRight(bool freecam) const {
    return Vector3{ getViewToWorld(freecam)[0].xyz() };
  }

  bool RtCamera::isCameraCut() const {
    return lengthSqr(getViewToWorld()[3] - getPreviousViewToWorld()[3]) > RtxOptions::Get()->getUniqueObjectDistanceSqr();
  }

  bool RtCamera::isFreeCameraEnabled() {
    return enableFreeCamera();
  }

  Vector3 RtCamera::getHorizontalForwardDirection() const {
    const Vector3 forward = getDirection(false);
    const Vector3 up = getUp(false);
    const bool isZUp = RtxOptions::Get()->isZUp();

    Vector3 direction = forward;

    if (isZUp)
      direction.z = 0.f;
    else
      direction.y = 0.f;

    float len = length(direction);
    if (len == 0.f) {
      // Looking straight down or up
      if (isZUp && forward.z > 0 || !isZUp && forward.y > 0)
        direction = -up;
      else
        direction = up;

      if (isZUp)
        direction.z = 0.f;
      else
        direction.y = 0.f;

      len = length(direction);
    }

    direction /= len;
    return direction;
  }

  namespace
  {
    /** Returns elements of the Halton low-discrepancy sequence.
        \param[in] index Index of the queried element, starting from 0.
        \param[in] base Base for the digit inversion. Should be the next unused prime number.
    */
    float halton(uint32_t index, uint32_t base)
    {
      // Reversing digit order in the given base in floating point.
      float result = 0.0f;
      float factor = 1.0f;

      for (; index > 0; index /= base) {
        factor /= base;
        result += factor * (index % base);
      }

      return result;
    }
  }

  HaltonSamplePattern::HaltonSamplePattern(uint32_t sampleCount)
  {
    mSampleCount = sampleCount;
    mCurSample = 0;
  }

  glm::vec2 HaltonSamplePattern::next()
  {
    glm::vec2 value = { halton(mCurSample, 2), halton(mCurSample, 3) };

    // Modular increment.
    ++mCurSample;
    if (mSampleCount != 0) {
      mCurSample = mCurSample % mSampleCount;
    }

    // Map the result so that [0, 1) maps to [-0.5, 0.5) and 0 maps to the origin.
    return glm::fract(value + 0.5f) - 0.5f;
  }

  Vector2 getCurrentPixelOffset(int currentFrame) {
    // Halton jitter
    Vector2 result(0.0f, 0.0f);

    int frameIndex = (currentFrame) % 64;

    constexpr int baseX = 2;
    int Index = frameIndex + 1;
    float invBase = 1.0f / baseX;
    float fraction = invBase;
    while (Index > 0) {
      result.x += (Index % baseX) * fraction;
      Index /= baseX;
      fraction *= invBase;
    }

    constexpr int baseY = 3;
    Index = frameIndex + 1;
    invBase = 1.0f / baseY;
    fraction = invBase;
    while (Index > 0) {
      result.y += (Index % baseY) * fraction;
      Index /= baseY;
      fraction *= invBase;
    }

    result.x -= 0.5f;
    result.y -= 0.5f;
    return result;
  }

  void RtCamera::setResolution(const uint32_t renderResolution[2], const uint32_t finalResolution[2])
  {
    if (finalResolution[0] != m_finalResolution[0] ||
        finalResolution[1] != m_finalResolution[1] ||
        renderResolution[0] != m_renderResolution[0] ||
        renderResolution[1] != m_renderResolution[1]) {
      float resolutionRatio = float(finalResolution[1]) / float(renderResolution[1]);
      float basePhaseCount = 8;
      float totalPhases = basePhaseCount * resolutionRatio * resolutionRatio;
      m_halton = HaltonSamplePattern(uint32_t(totalPhases));

      m_renderResolution[0] = renderResolution[0];
      m_renderResolution[1] = renderResolution[1];
      m_finalResolution[0] = finalResolution[0];
      m_finalResolution[1] = finalResolution[1];
    }
  }

  const Matrix4d& RtCamera::getWorldToView(bool freecam) const { 
    return (freecam && isFreeCameraEnabled()) 
      ? m_matCache[MatrixType::FreeCamWorldToView] 
      : m_matCache[MatrixType::WorldToView]; 
  }

  const Matrix4d& RtCamera::getPreviousWorldToView(bool freecam) const { 
    return (freecam && isFreeCameraEnabled()) 
      ? m_matCache[MatrixType::FreeCamPreviousWorldToView] 
      : m_matCache[MatrixType::PreviousWorldToView]; 
  }

  const Matrix4d& RtCamera::getPreviousPreviousWorldToView(bool freecam) const { 
    return (freecam && isFreeCameraEnabled()) 
      ? m_matCache[MatrixType::FreeCamPreviousPreviousViewToWorld]
      : m_matCache[MatrixType::PreviousPreviousWorldToView];
  }

  const Matrix4d& RtCamera::getViewToWorld(bool freecam) const { 
    return (freecam && isFreeCameraEnabled()) 
      ? m_matCache[MatrixType::FreeCamViewToWorld] 
      : m_matCache[MatrixType::ViewToWorld]; 
  }

  const Matrix4d& RtCamera::getPreviousViewToWorld(bool freecam) const { 
    return (freecam && isFreeCameraEnabled()) 
      ? m_matCache[MatrixType::FreeCamPreviousViewToWorld] 
      : m_matCache[MatrixType::PreviousViewToWorld]; 
  }
  
  const Matrix4d& RtCamera::getPreviousPreviousViewToWorld(bool freecam) const {
    return (freecam && isFreeCameraEnabled())
      ? m_matCache[MatrixType::FreeCamPreviousPreviousViewToWorld]
      : m_matCache[MatrixType::PreviousPreviousViewToWorld];
  }

  const Matrix4d& RtCamera::getTranslatedWorldToView(bool freecam) const {
    return (freecam && isFreeCameraEnabled())
      ? m_matCache[MatrixType::FreeCamTranslatedWorldToView]
      : m_matCache[MatrixType::TranslatedWorldToView];
  }

  const Matrix4d& RtCamera::getPreviousTranslatedWorldToView(bool freecam) const {
    return (freecam && isFreeCameraEnabled())
      ? m_matCache[MatrixType::FreeCamPreviousTranslatedWorldToView]
      : m_matCache[MatrixType::PreviousTranslatedWorldToView];
  }

  const Matrix4d& RtCamera::getViewToTranslatedWorld(bool freecam) const {
    return (freecam && isFreeCameraEnabled())
      ? m_matCache[MatrixType::FreeCamViewToTranslatedWorld]
      : m_matCache[MatrixType::ViewToTranslatedWorld];
  }

  const Matrix4d& RtCamera::getPreviousViewToTranslatedWorld(bool freecam) const {
    return (freecam && isFreeCameraEnabled())
      ? m_matCache[MatrixType::FreeCamPreviousViewToTranslatedWorld]
      : m_matCache[MatrixType::PreviousViewToTranslatedWorld];
  }

  void RtCamera::setPreviousWorldToView(const Matrix4d& worldToView, bool freecam) {
    dxvk::Matrix4d viewToWorld = inverse(worldToView);

    Matrix4d viewToTranslatedWorld = viewToWorld;
    viewToTranslatedWorld[3] = Vector4d(0.0, 0.0, 0.0, viewToTranslatedWorld[3].w);

    dxvk::Matrix4d translatedWorldToView = inverse(viewToTranslatedWorld);

    if (freecam && isFreeCameraEnabled()) {
      m_matCache[MatrixType::FreeCamPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::FreeCamPreviousPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::FreeCamPreviousWorldToView] = worldToView;
      m_matCache[MatrixType::FreeCamPreviousPreviousWorldToView] = worldToView;

      m_matCache[MatrixType::FreeCamPreviousViewToTranslatedWorld] = viewToTranslatedWorld;
      m_matCache[MatrixType::FreeCamPreviousTranslatedWorldToView] = translatedWorldToView;
    } else {
      m_matCache[MatrixType::PreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::PreviousPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::PreviousWorldToView] = worldToView;
      m_matCache[MatrixType::PreviousPreviousWorldToView] = worldToView;

      m_matCache[MatrixType::PreviousViewToTranslatedWorld] = viewToTranslatedWorld;
      m_matCache[MatrixType::PreviousTranslatedWorldToView] = translatedWorldToView;
    }

    m_previousArtificalWorldOffset = Vector3(0.f);
  }

  void RtCamera::setPreviousViewToWorld(const Matrix4d& viewToWorld, bool freecam) {
    Matrix4d viewToTranslatedWorld = viewToWorld;
    viewToTranslatedWorld[3] = Vector4d(0.0, 0.0, 0.0, viewToTranslatedWorld[3].w);

    dxvk::Matrix4d worldToView = inverse(viewToWorld);
    dxvk::Matrix4d translatedWorldToView = inverse(viewToTranslatedWorld);

    if (freecam && isFreeCameraEnabled()) {
      m_matCache[MatrixType::FreeCamPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::FreeCamPreviousPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::FreeCamPreviousWorldToView] = worldToView;
      m_matCache[MatrixType::FreeCamPreviousPreviousWorldToView] = worldToView;

      m_matCache[MatrixType::FreeCamPreviousViewToTranslatedWorld] = viewToTranslatedWorld;
      m_matCache[MatrixType::FreeCamPreviousTranslatedWorldToView] = translatedWorldToView;
    } else {
      m_matCache[MatrixType::PreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::PreviousPreviousViewToWorld] = viewToWorld;
      m_matCache[MatrixType::PreviousWorldToView] = worldToView;
      m_matCache[MatrixType::PreviousPreviousWorldToView] = worldToView;

      m_matCache[MatrixType::PreviousViewToTranslatedWorld] = viewToTranslatedWorld;
      m_matCache[MatrixType::PreviousTranslatedWorldToView] = translatedWorldToView;
    }

    m_previousArtificalWorldOffset = Vector3(0.f);
  }

  void RtCamera::applyArtificialWorldOffset(const Vector3& worldOffset) {
    m_matCache[MatrixType::ViewToWorld][3].xyz() += Vector3d{ worldOffset };
    m_matCache[MatrixType::WorldToView] = inverse(m_matCache[MatrixType::ViewToWorld]);
    // Note: Translated world space matrices do not get offset here as they do not need a world offset, only
    // the current to previous frame translated world space offset needs to be updated, but this is currently
    // automatically derived from the view to world and previous view to world matrices, so no work here is needed.

    m_artificalWorldOffset += worldOffset;
  }

  Matrix4 getMatrixFromEulerAngles(float pitch, float yaw) {
    float cosPitch = cos(pitch);
    float sinPitch = sin(pitch);
    float cosYaw = cos(yaw);
    float sinYaw = sin(yaw);
    Matrix4 customTransform;
    customTransform[0] = Vector4{ cosYaw, 0, -sinYaw, 0.0 };
    customTransform[1] = Vector4{ sinYaw * sinPitch, cosPitch, cosYaw * sinPitch, 0.0 };
    customTransform[2] = Vector4{ sinYaw * cosPitch, -sinPitch, cosPitch * cosYaw, 0.0 };
    return customTransform;
  }

  const RtCamera::RtCameraSetting& RtCamera::getSetting() {
    return m_context;
  }

  Matrix4d RtCamera::updateFreeCamera(uint32_t flags) {
    auto currTime = std::chrono::system_clock::now();

    std::chrono::duration<float> elapsedSec = currTime - m_prevRunningTime;
    m_prevRunningTime = currTime;

    // Perform custom camera controls logic
    float speed = elapsedSec.count() * RtxOptions::Get()->getSceneScale() * freeCameraSpeed();

    float moveLeftRight = 0;
    float moveBackForward = 0;
    float moveDownUp = 0;
    HWND fgWin = GetForegroundWindow();
    DWORD processId = 0;
    if (fgWin) {
      GetWindowThreadProcessId(fgWin, &processId);
    }

    if (!ImGui::GetIO().WantCaptureMouse && (flags & (int)RtCamera::UpdateFlag::UpdateFreeCamera) != 0) {
      // Speed booster
      if (ImGui::IsKeyDown(ImGuiKey_LeftShift)) {
        speed *= 4;
      }

      // Typical WASD controls with EQ up-down
      bool isKeyAvailable =
        !ImGui::IsKeyDown(ImGuiKey_LeftCtrl) &&
        !ImGui::IsKeyDown(ImGuiKey_RightCtrl) &&
        !ImGui::IsKeyDown(ImGuiKey_LeftAlt) &&
        !ImGui::IsKeyDown(ImGuiKey_RightAlt) && !lockFreeCamera();

      if (!isKeyAvailable) {
        speed = 0;
      }

      float coordSystemScale = m_context.isLHS ? -1.f : 1.f;

      if (ImGui::IsKeyDown(ImGuiKey_A)) {
        moveLeftRight -= speed;
      }
      if (ImGui::IsKeyDown(ImGuiKey_D)) {
        moveLeftRight += speed;
      }
      if (ImGui::IsKeyDown(ImGuiKey_W)) {
        moveBackForward += coordSystemScale * speed;
      }
      if (ImGui::IsKeyDown(ImGuiKey_S)) {
        moveBackForward -= coordSystemScale * speed;
      }
      if (ImGui::IsKeyDown(ImGuiKey_E)) {
        moveDownUp += speed;
      }
      if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        moveDownUp -= speed;
      }

      POINT p;
      if (GetCursorPos(&p)) {
        if (!lockFreeCamera() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ((m_mouseX != p.x) || (m_mouseY != p.y))) {
          freeCameraYawRef() += coordSystemScale * (m_mouseX - p.x) * 0.1f * elapsedSec.count();
          freeCameraPitchRef() += coordSystemScale * (m_mouseY - p.y) * 0.2f * elapsedSec.count();
        }

        m_mouseX = p.x;
        m_mouseY = p.y;
      }

      // Reset
      if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        freeCameraPositionRef() = Vector3(0.f);
        moveLeftRight = 0;
        moveBackForward = 0;
        moveDownUp = 0;
        freeCameraYawRef() = 0.0f;
        freeCameraPitchRef() = 0.0f;
      }
    } else {
      // track mouse position when out of focus to avoid uncontrollable camera flips when we're back in focus
      POINT p;
      if (GetCursorPos(&p)) {
        m_mouseX = p.x;
        m_mouseY = p.y;
      }
    }

    // Calculate Free Camera matrix information

    auto freeCamViewToWorld(m_matCache[MatrixType::ViewToWorld]);

    freeCamViewToWorld[3] = Vector4d(0.0);
    freeCamViewToWorld *= getMatrixFromEulerAngles(freeCameraPitch(), freeCameraYaw());

    if (m_type == CameraType::Main && (flags & (uint32_t)UpdateFlag::UpdateFreeCamera)) {
      freeCameraPositionRef() += moveLeftRight * Vector3(freeCamViewToWorld.data[0].xyz());
      freeCameraPositionRef() += moveDownUp * Vector3(freeCamViewToWorld.data[1].xyz());
      freeCameraPositionRef() -= moveBackForward * Vector3(freeCamViewToWorld.data[2].xyz());

      // save free camera context
      m_context.enableFreeCamera = enableFreeCamera();
      m_context.freeCameraPosition = freeCameraPosition();
      m_context.freeCameraYaw = freeCameraYaw();
      m_context.freeCameraPitch = freeCameraPitch();
      m_context.freeCameraViewRelative = freeCameraViewRelative();
    }

    freeCamViewToWorld[3] = m_matCache[MatrixType::ViewToWorld][3] + Vector4d{ Vector3d{ freeCameraPosition() }, 0.0 };
    return freeCamViewToWorld;
  }

  void RtCamera::updateAntiCulling(float fov, float aspectRatio, float nearPlane, float farPlane, bool isLHS) {
    // Create Anti-Culling frustum
    if (RtxOptions::AntiCulling::Object::enable()) {
      const float fovScale = RtxOptions::AntiCulling::Object::fovScale();
      const float farPlaneScale = RtxOptions::AntiCulling::Object::farPlaneScale();
      float4x4 frustumMatrix;
      if (RtxOptions::AntiCulling::Object::enableInfinityFarFrustum()) {
        frustumMatrix.SetupByHalfFovyInf((float) (fov * fovScale * 0.5), aspectRatio, nearPlane, (isLHS ? PROJ_LEFT_HANDED : 0));
      } else {
        frustumMatrix.SetupByHalfFovy((float) (fov * fovScale * 0.5), aspectRatio, nearPlane, farPlane * farPlaneScale, (isLHS ? PROJ_LEFT_HANDED : 0));
      }
      m_frustum.Setup(NDC_OGL, frustumMatrix);

      m_frustum.calculateFrustumGeometry(nearPlane, farPlane, fov, aspectRatio, isLHS);
    }

    if (RtxOptions::AntiCulling::Light::enable()) {
      const float fovScale = RtxOptions::AntiCulling::Light::fovScale();
      const float scaledHalfFov = std::min(fov * fovScale * 0.5f, 1.55f); // Clamp to half fov to 89 degrees
      const float projectionMatrixFovScale = std::tan(fov * 0.5f) / std::tan(scaledHalfFov);
      Matrix4 viewToProjection = getViewToProjection();
      viewToProjection[0][0] *= projectionMatrixFovScale;
      viewToProjection[1][1] *= projectionMatrixFovScale;

      const Matrix4 worldToProj = viewToProjection * getWorldToView(false);
      m_lightAntiCullingFrustum.Setup(NDC_OGL, *reinterpret_cast<const float4x4*>(&worldToProj));
    }
  }

  Matrix4d RtCamera::overrideNearPlane(const Matrix4d& modifiedViewToProj) {
    // Note: Converted to floats to interface with MathLib. Ideally this should be a double still.
    Matrix4 floatModifiedViewToProj { modifiedViewToProj };

    // Check size since struct padding can impact this memcpy
    static_assert(sizeof(float4x4) == sizeof(floatModifiedViewToProj));

    uint32_t flags;
    float cameraParams[PROJ_NUM];
    DecomposeProjection(NDC_D3D, NDC_D3D, *reinterpret_cast<float4x4*>(&floatModifiedViewToProj), &flags, cameraParams, nullptr, nullptr, nullptr, nullptr);

    // Prevent user controls exceeding the near plane distance from original projection
    const float minNearPlane = std::min(RtxOptions::Get()->nearPlaneOverride(), cameraParams[PROJ_ZNEAR]);

    float4x4 newProjection;
    newProjection.SetupByAngles(cameraParams[PROJ_ANGLEMINX], cameraParams[PROJ_ANGLEMAXX], cameraParams[PROJ_ANGLEMINY], cameraParams[PROJ_ANGLEMAXY], minNearPlane, cameraParams[PROJ_ZFAR], flags);
    memcpy(&floatModifiedViewToProj, &newProjection, sizeof(float4x4));

    return Matrix4d { floatModifiedViewToProj };
  }


  bool RtCamera::updateFromSetting(uint32_t frameIdx, const RtCameraSetting& setting, uint32_t flags) {
    enableFreeCameraRef() = setting.enableFreeCamera;
    freeCameraPositionRef() = setting.freeCameraPosition;
    freeCameraYawRef() = setting.freeCameraYaw;
    freeCameraPitchRef() = setting.freeCameraPitch;
    freeCameraViewRelativeRef() = setting.freeCameraViewRelative;

    RtxOptions::Get()->shakeCameraRef() = setting.isCameraShaking;

    m_context = setting;

    return update(frameIdx, setting.worldToView, setting.viewToProjection, setting.fov, setting.aspectRatio, setting.nearPlane, setting.farPlane, setting.isLHS, flags);
  }

  bool RtCamera::update(
    uint32_t frameIdx, const Matrix4& newWorldToView, const Matrix4& newViewToProjection,
    float fov, float aspectRatio, float nearPlane, float farPlane, bool isLHS, uint32_t flags
  ) {
    if (m_frameLastTouched == frameIdx)
      return false;

    m_context.worldToView = newWorldToView;
    m_context.viewToProjection = newViewToProjection;
    m_context.fov = fov;
    m_context.aspectRatio = aspectRatio;
    m_context.nearPlane = nearPlane;
    m_context.farPlane = farPlane;
    m_context.isLHS = isLHS;
    m_context.flags = flags;

    m_previousArtificalWorldOffset = m_artificalWorldOffset;
    m_artificalWorldOffset = Vector3(0.f);

    if (!RtxOptions::Get()->isCameraShaking()) {
      m_context.cameraShakeFrameCount = 0;
      m_context.cameraRotationFrameCount = 0;
    }

    // Setup World/View Matrix Data

    m_matCache[MatrixType::PreviousPreviousWorldToView] = m_matCache[MatrixType::PreviousWorldToView];
    m_matCache[MatrixType::PreviousPreviousViewToWorld] = m_matCache[MatrixType::PreviousViewToWorld];
    m_matCache[MatrixType::PreviousWorldToView] = m_matCache[MatrixType::WorldToView];
    m_matCache[MatrixType::PreviousViewToWorld] = m_matCache[MatrixType::ViewToWorld];
    m_matCache[MatrixType::UncorrectedPreviousViewToWorld] = m_matCache[MatrixType::ViewToWorld];
    m_matCache[MatrixType::WorldToView] = freeCameraViewRelative() ? Matrix4d{ newWorldToView } : Matrix4d();
    m_matCache[MatrixType::ViewToWorld] = inverse(m_matCache[MatrixType::WorldToView]);

    // Setup Translated World/View Matrix Data

    m_matCache[MatrixType::PreviousTranslatedWorldToView] = m_matCache[MatrixType::TranslatedWorldToView];
    m_matCache[MatrixType::PreviousViewToTranslatedWorld] = m_matCache[MatrixType::ViewToTranslatedWorld];
    m_matCache[MatrixType::UncorrectedPreviousTranslatedWorldToView] = m_matCache[MatrixType::TranslatedWorldToView];

    auto viewToTranslatedWorld = m_matCache[MatrixType::ViewToWorld];
    viewToTranslatedWorld[3] = Vector4d(0.0f, 0.0f, 0.0f, viewToTranslatedWorld[3].w);

    m_matCache[MatrixType::ViewToTranslatedWorld] = freeCameraViewRelative() ? viewToTranslatedWorld : Matrix4d();
    // Note: Slightly non-ideal to have to inverse an already inverted matrix when we have the original world to view matrix,
    // but this is the safest way to ensure a proper inversion when modifying the view to world transform manually.
    m_matCache[MatrixType::TranslatedWorldToView] = inverse(m_matCache[MatrixType::ViewToTranslatedWorld]);

    // Setup View/Projection Matrix Data

    m_matCache[MatrixType::PreviousViewToProjection] = m_matCache[MatrixType::ViewToProjection];
    m_matCache[MatrixType::PreviousProjectionToView] = m_matCache[MatrixType::ProjectionToView];

    auto modifiedViewToProj = Matrix4d{ newViewToProjection };

    updateAntiCulling(fov, aspectRatio, nearPlane, farPlane, isLHS);

    // Sometimes we want to modify the near plane for RT.  See DevSettings->Camera->Advanced
    if(RtxOptions::Get()->enableNearPlaneOverride()) {
      modifiedViewToProj = overrideNearPlane(modifiedViewToProj);
    }
    
    m_matCache[MatrixType::ViewToProjection] = modifiedViewToProj;
    m_matCache[MatrixType::ProjectionToView] = inverse(modifiedViewToProj);

    // Apply free camera shaking

    if (!enableFreeCamera() && RtxOptions::Get()->isCameraShaking()) {
      auto newViewToWorld = getShakenViewToWorldMatrix(m_matCache[MatrixType::ViewToWorld], flags);
      auto newViewToTranslatedWorld = newViewToWorld;
      newViewToTranslatedWorld[3] = Vector4d(0.0, 0.0, 0.0, newViewToTranslatedWorld[3].w);

      // Note: Error added here from an extra inverse operation, but should be fine as camera shaking is only used as a debugging path.
      m_matCache[MatrixType::WorldToView] = inverse(newViewToWorld);
      m_matCache[MatrixType::ViewToWorld] = newViewToWorld;
      m_matCache[MatrixType::TranslatedWorldToView] = inverse(newViewToTranslatedWorld);
      m_matCache[MatrixType::ViewToTranslatedWorld] = newViewToTranslatedWorld;
    }

    // Apply jittering

    if (flags & (uint32_t) UpdateFlag::UpdateJitterFrame) {
      m_context.jitterFrameIdx = frameIdx;
    }

    auto newViewToProjectionJittered = modifiedViewToProj;
    applyAndGetJitter(
      newViewToProjectionJittered,
      m_jitter,
      m_context.jitterFrameIdx,
      m_renderResolution[0],
      m_renderResolution[1]);

    m_context.jitter[0] = m_jitter[0];
    m_context.jitter[1] = m_jitter[1];
    m_context.isCameraShaking = RtxOptions::Get()->isCameraShaking();

    m_matCache[MatrixType::PreviousViewToProjectionJittered] = m_matCache[MatrixType::ViewToProjectionJittered];
    m_matCache[MatrixType::PreviousProjectionToViewJittered] = m_matCache[MatrixType::ProjectionToViewJittered];
    m_matCache[MatrixType::ViewToProjectionJittered] = newViewToProjectionJittered;
    m_matCache[MatrixType::ProjectionToViewJittered] = inverse(newViewToProjectionJittered);

    m_frameLastTouched = frameIdx;

    // For our first update, we should init both previous and current to the same value
    if (m_firstUpdate) {
      m_matCache[MatrixType::PreviousWorldToView] = m_matCache[MatrixType::WorldToView];
      m_matCache[MatrixType::PreviousViewToWorld] = m_matCache[MatrixType::ViewToWorld];
      m_matCache[MatrixType::PreviousPreviousWorldToView] = m_matCache[MatrixType::WorldToView];
      m_matCache[MatrixType::PreviousPreviousViewToWorld] = m_matCache[MatrixType::ViewToWorld];

      m_matCache[MatrixType::PreviousTranslatedWorldToView] = m_matCache[MatrixType::TranslatedWorldToView];
      m_matCache[MatrixType::PreviousViewToTranslatedWorld] = m_matCache[MatrixType::ViewToTranslatedWorld];

      m_matCache[MatrixType::PreviousViewToProjection] = m_matCache[MatrixType::ViewToProjection];
      m_matCache[MatrixType::PreviousProjectionToView] = m_matCache[MatrixType::ProjectionToView];

      m_matCache[MatrixType::PreviousViewToProjectionJittered] = m_matCache[MatrixType::ViewToProjectionJittered];
      m_matCache[MatrixType::PreviousProjectionToViewJittered] = m_matCache[MatrixType::ProjectionToViewJittered];

      // Never do this again
      m_firstUpdate = false;
    }

    // Only calculate free camera matrices for main camera
    if (!enableFreeCamera() || m_type != CameraType::Main)
      return isCameraCut();

    auto freeCamViewToWorld = updateFreeCamera(flags);

    if (RtxOptions::Get()->isCameraShaking()) {
      freeCamViewToWorld = getShakenViewToWorldMatrix(freeCamViewToWorld, flags);
    }

    auto freeCamViewToTranslatedWorld = freeCamViewToWorld;
    freeCamViewToTranslatedWorld[3] = Vector4d(0.0f, 0.0f, 0.0f, freeCamViewToTranslatedWorld[3].w);

    m_matCache[MatrixType::FreeCamPreviousPreviousWorldToView] = m_matCache[MatrixType::FreeCamPreviousWorldToView];
    m_matCache[MatrixType::FreeCamPreviousPreviousViewToWorld] = m_matCache[MatrixType::FreeCamPreviousViewToWorld];
    m_matCache[MatrixType::FreeCamPreviousWorldToView] = m_matCache[MatrixType::FreeCamWorldToView];
    m_matCache[MatrixType::FreeCamPreviousViewToWorld] = m_matCache[MatrixType::FreeCamViewToWorld];
    m_matCache[MatrixType::FreeCamWorldToView] = inverse(freeCamViewToWorld);
    m_matCache[MatrixType::FreeCamViewToWorld] = freeCamViewToWorld;

    m_matCache[MatrixType::FreeCamPreviousTranslatedWorldToView] = m_matCache[MatrixType::FreeCamTranslatedWorldToView];
    m_matCache[MatrixType::FreeCamPreviousViewToTranslatedWorld] = m_matCache[MatrixType::FreeCamViewToTranslatedWorld];
    m_matCache[MatrixType::FreeCamTranslatedWorldToView] = inverse(freeCamViewToTranslatedWorld);
    m_matCache[MatrixType::FreeCamViewToTranslatedWorld] = freeCamViewToTranslatedWorld;

    return false; // If we are using the debug/free camera, never do camera cuts
  }

  void RtCamera::getJittering(float jitter[2]) const {
    jitter[0] = m_jitter[0];
    jitter[1] = m_jitter[1];
  }

  Vector2 RtCamera::calcPixelJitter(uint32_t jitterFrameIdx) {
    // Only apply jittering when DLSS/TAA is enabled, or if forced by settings
    if (!RtxOptions::Get()->isDLSSOrRayReconstructionEnabled() &&
        !RtxOptions::Get()->isTAAEnabled() &&
        !RtxOptions::Get()->forceCameraJitter()) {
      return Vector2{ 0, 0 };
    }

#define USE_DLSS_DEMO_JITTER_PATTERN 1
#if USE_DLSS_DEMO_JITTER_PATTERN
    return getCurrentPixelOffset(jitterFrameIdx);
#else
    return m_halton.next();
#endif
  }

  Vector2 RtCamera::calcClipSpaceJitter(Vector2 pixelJitter, 
                                           uint32_t renderResolutionX, uint32_t renderResolutionY,
                                           float ratioX, float ratioY) {
    if (renderResolutionX == 0 || renderResolutionY == 0) {
      return Vector2{ 0, 0 };
    }
    return Vector2{
      pixelJitter[0] / float(renderResolutionX) * ratioX * 2.f,
      pixelJitter[1] / float(renderResolutionY) * ratioY * 2.f,
    };
  }

  void RtCamera::applyJitterTo(Matrix4& inoutProjection, uint32_t jitterFrameIdx, uint32_t renderResolutionX, uint32_t renderResolutionY) {
    Vector2 pixelJitter = calcPixelJitter(jitterFrameIdx);
    float ratioX = Sign(inoutProjection[2][3]);
    float ratioY = -Sign(inoutProjection[2][3]);
    Vector2 clipSpaceJitter = calcClipSpaceJitter(pixelJitter,
                                                     renderResolutionX, renderResolutionY,
                                                     ratioX, ratioY);
    if (std::abs(clipSpaceJitter[0]) < std::numeric_limits<float>::min() &&
        std::abs(clipSpaceJitter[1]) < std::numeric_limits<float>::min()) {
      return;
    }
    inoutProjection[2][0] += clipSpaceJitter[0];
    inoutProjection[2][1] += clipSpaceJitter[1];
  }

  void RtCamera::applyAndGetJitter(Matrix4d& inoutProjection, float (&outPixelJitter)[2], uint32_t jitterFrameIdx, uint32_t renderResolutionX, uint32_t renderResolutionY) {
    Vector2 pixelJitter = calcPixelJitter(jitterFrameIdx);
    float ratioX = Sign(inoutProjection[2][3]);
    float ratioY = -Sign(inoutProjection[2][3]);
    Vector2 clipSpaceJitter = calcClipSpaceJitter(pixelJitter,
                                                  renderResolutionX, renderResolutionY,
                                                  ratioX, ratioY);
    {
      outPixelJitter[0] = pixelJitter[0];
      outPixelJitter[1] = pixelJitter[1];
    }
    if (std::abs(clipSpaceJitter[0]) < std::numeric_limits<float>::min() &&
        std::abs(clipSpaceJitter[1]) < std::numeric_limits<float>::min()) {
      return;
    }
    inoutProjection[2][0] += clipSpaceJitter[0];
    inoutProjection[2][1] += clipSpaceJitter[1];
  }

  Camera RtCamera::getShaderConstants() const {
    auto& worldToView = getWorldToView();
    auto& translatedWorldToView = getTranslatedWorldToView();
    auto& viewToWorld = getViewToWorld();
    auto& viewToTranslatedWorld = getViewToTranslatedWorld();
    auto& viewToProjection = getViewToProjection();
    auto& projectionToView = getProjectionToView();
    auto& prevWorldToView = getPreviousWorldToView();
    auto& prevTranslatedWorldToView = getPreviousTranslatedWorldToView();
    auto& prevViewToWorld = getPreviousViewToWorld();
    auto& viewToProjectionJittered = getViewToProjectionJittered();
    auto& projectionToViewJittered = getProjectionToViewJittered();
    auto& prevViewToProjection = getPreviousViewToProjection();
    auto& prevViewToProjectionJittered = getPreviousViewToProjectionJittered();
    auto& prevProjectionToView = getPreviousProjectionToView();
    auto& prevProjectionToViewJittered = getPreviousProjectionToViewJittered();
    auto viewToPrevView = prevWorldToView * viewToWorld;

    Camera camera;
    camera.worldToView = worldToView;
    camera.viewToWorld = viewToWorld;
    camera.viewToProjection = viewToProjection;
    camera.projectionToView = projectionToView;
    camera.viewToProjectionJittered = viewToProjectionJittered;
    camera.projectionToViewJittered = projectionToViewJittered;
    camera.worldToProjectionJittered = viewToProjectionJittered * worldToView;
    camera.projectionToWorldJittered = viewToWorld * projectionToViewJittered;
    camera.translatedWorldToView = translatedWorldToView;
    camera.translatedWorldToProjectionJittered = viewToProjectionJittered * translatedWorldToView;
    camera.projectionToTranslatedWorld = viewToTranslatedWorld * projectionToView;

    camera.prevWorldToView = prevWorldToView;
    camera.prevViewToWorld = prevViewToWorld;
    camera.prevProjectionToView = prevProjectionToView;
    camera.prevProjectionToViewJittered = prevProjectionToViewJittered;
    camera.prevWorldToProjection = prevViewToProjection * prevWorldToView;
    camera.prevWorldToProjectionJittered = prevViewToProjectionJittered * prevWorldToView;
    camera.prevTranslatedWorldToView = prevTranslatedWorldToView;
    camera.prevTranslatedWorldToProjection = prevViewToProjection * prevTranslatedWorldToView;

    camera.projectionToPrevProjectionJittered = prevViewToProjectionJittered * viewToPrevView * projectionToViewJittered;
    
    camera.resolution = uvec2 { m_renderResolution[0], m_renderResolution[1] };
    camera.nearPlane = m_context.nearPlane;

    camera.flags = ((!m_context.isLHS) ? rightHandedFlag : 0);

    return camera;
  }

  VolumeDefinitionCamera RtCamera::getVolumeShaderConstants() const {
    auto& translatedWorldToView = getTranslatedWorldToView();
    auto& viewToTranslatedWorld = getViewToTranslatedWorld();
    auto& viewToProjection = getViewToProjection();
    auto& viewToWorld = getViewToWorld();
    auto& projectionToView = getProjectionToView();
    auto& viewToProjectionJittered = getViewToProjectionJittered();
    auto& prevViewToProjection = getPreviousViewToProjection();

    bool isFreeCamera = isFreeCameraEnabled();
    auto& prevViewToWorld = isFreeCamera ? m_matCache[FreeCamPreviousViewToWorld] : m_matCache[UncorrectedPreviousViewToWorld];
    auto& prevTranslatedWorldToView = isFreeCamera ? m_matCache[FreeCamPreviousTranslatedWorldToView] : m_matCache[UncorrectedPreviousTranslatedWorldToView];
    
    VolumeDefinitionCamera camera;

    camera.viewToProjection = viewToProjection;
    camera.translatedWorldToView = translatedWorldToView;
    camera.translatedWorldToProjectionJittered = viewToProjectionJittered * translatedWorldToView;
    camera.projectionToTranslatedWorld = viewToTranslatedWorld * projectionToView;
    camera.prevTranslatedWorldToView = prevTranslatedWorldToView;
    camera.prevTranslatedWorldToProjection = prevViewToProjection * prevTranslatedWorldToView;
    
    camera.translatedWorldOffset = Vector3{ viewToWorld[3].xyz() };
    camera.previousTranslatedWorldOffset = Vector3{ prevViewToWorld[3].xyz() };
    camera.nearPlane = m_context.nearPlane;
    camera.flags = ((!m_context.isLHS) ? rightHandedFlag : 0);

    return camera;
  }

  void RtCamera::showImguiSettings() {
    const static ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    const static ImGuiTreeNodeFlags collapsingHeaderFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_CollapsingHeader;
    const static ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;

    if (ImGui::CollapsingHeader("Free Camera", collapsingHeaderFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Enable Free Camera", &enableFreeCameraObject());
      ImGui::Checkbox("Lock Free Camera", &lockFreeCameraObject());
      ImGui::DragFloat3("Position", &freeCameraPositionObject(), 0.1f, -1e5, -1e5, "%.3f", sliderFlags);
      ImGui::DragFloat("Yaw", &freeCameraYawObject(), 0.1f, -Pi<float>(2), Pi<float>(2), "%.3f", sliderFlags);
      ImGui::DragFloat("Pitch", &freeCameraPitchObject(), 0.1f, -Pi<float>(2), Pi<float>(2), "%.3f", sliderFlags);
      ImGui::DragFloat("Speed", &freeCameraSpeedObject(), 0.1f, 0.f, 5000.0f, "%.3f");
      ImGui::Checkbox("View Relative", &freeCameraViewRelativeObject());

      ImGui::Unindent();
    }
  }

  Matrix4d RtCamera::getShakenViewToWorldMatrix(Matrix4d& viewToWorld, uint32_t flags) {
    float moveLeftRight = 0;
    float moveBackForward = 0;
    float shakeYaw = 0;
    float shakePitch = 0;

    int period = RtxOptions::Get()->getCameraShakePeriod();
    float sceneScale = RtxOptions::Get()->getSceneScale();
    CameraAnimationMode animationMode = RtxOptions::Get()->getCameraAnimationMode();
    float amplitude = RtxOptions::Get()->getCameraAnimationAmplitude();

    float offset = sin(float(m_context.cameraShakeFrameCount) / (2 * period) * 2.0f * M_PI);
    switch (animationMode) {
    case CameraAnimationMode::CameraShake_LeftRight:
      moveLeftRight += amplitude * sceneScale * offset;
      break;
    case CameraAnimationMode::CameraShake_FrontBack:
      moveBackForward += amplitude * sceneScale * offset;
      break;
    case CameraAnimationMode::CameraShake_Yaw:
      shakeYaw = amplitude * 0.05 * offset;
      break;
    case CameraAnimationMode::CameraShake_Pitch:
      shakePitch = amplitude * 0.05 * offset;
      break;
    case CameraAnimationMode::YawRotation:
      const float constSpeed = float(m_context.cameraRotationFrameCount) / (2 * period) * 2.0f * M_PI;
      shakeYaw = amplitude * 0.05 * constSpeed;
      break;
    }

    if (flags & (uint32_t)UpdateFlag::IncrementShakingFrame) {
      ++m_context.cameraRotationFrameCount;
      m_context.cameraShakeFrameCount = (m_context.cameraShakeFrameCount + 1) % (2 * period);
    }

    Matrix4d viewRot = viewToWorld;
    viewRot[3] = Vector4d(0.0);
    viewRot *= getMatrixFromEulerAngles(shakePitch, shakeYaw);

    Vector4d shakeOffset = static_cast<double>(moveLeftRight) * viewRot.data[0];
    shakeOffset += static_cast<double>(moveBackForward) * viewRot.data[2];

    viewRot[3] = viewToWorld[3] + shakeOffset;

    return viewRot;
  }

  void RtFrustum::calculateFrustumGeometry(const float nearPlane, const float farPlane, const float fov, const float aspectRatio, const bool isLHS) {
    // Calculate frustum near and far plane extents
    const float tanHalfFov = std::tan(fov * 0.5f);
    nearPlaneUpExtent = nearPlane * tanHalfFov;
    nearPlaneRightExtent = nearPlaneUpExtent * aspectRatio;
    farPlaneUpExtent = farPlane * tanHalfFov;
    farPlaneRightExtent = farPlaneUpExtent * aspectRatio;

    const float N = isLHS ? nearPlane : -nearPlane;
    const float F = isLHS ? farPlane : -farPlane;

    // Near Plane Vertices
    nearPlaneFrustumVertices[0] = Vector3(-nearPlaneRightExtent, -nearPlaneUpExtent, N);
    nearPlaneFrustumVertices[1] = Vector3(-nearPlaneRightExtent,  nearPlaneUpExtent, N);
    nearPlaneFrustumVertices[2] = Vector3( nearPlaneRightExtent,  nearPlaneUpExtent, N);
    nearPlaneFrustumVertices[3] = Vector3( nearPlaneRightExtent, -nearPlaneUpExtent, N);

    // Far Plane Vertices
    farPlaneFrustumVertices[0] = Vector3(-farPlaneRightExtent, -farPlaneUpExtent, F);
    farPlaneFrustumVertices[1] = Vector3(-farPlaneRightExtent,  farPlaneUpExtent, F);
    farPlaneFrustumVertices[2] = Vector3( farPlaneRightExtent,  farPlaneUpExtent, F);
    farPlaneFrustumVertices[3] = Vector3( farPlaneRightExtent, -farPlaneUpExtent, F);

    // Edge Vectors (Normalized)
    for (int i = 0; i < 4; ++i) {
      frustumEdgeVectors[i] = normalize(farPlaneFrustumVertices[i] - nearPlaneFrustumVertices[i]);
    }
  }

  void RtCameraSequence::startPlay() {
    modeRef() = Mode::Playback;
    currentFrameRef() = 0;
  }

  void RtCameraSequence::startRecord() {
    modeRef() = Mode::Record;
    currentFrameRef() = 0;
    m_settings.clear();
  }

  void RtCameraSequence::addRecord(const RtCamera::RtCameraSetting& setting) {
    m_settings.push_back(setting);
    currentFrameRef() = m_settings.size() - 1;
  }

  void RtCameraSequence::save() {
    std::ofstream file(filePath(), std::ios::out | std::ios::binary);

    if (file.is_open()) {
      int nElements = m_settings.size();
      Header header;
      memset(&header, 0, sizeof(header));
      header.nElements = nElements;
      file.write((char*)&header, sizeof(header));

      for (int i = 0; i < nElements; i++) {
        FrameData frameData = {};
        memset(&frameData, 0, sizeof(frameData));
        frameData.setting = m_settings[i];
        file.write((char*)&frameData, sizeof(frameData));
      }

      file.close();
    }
  }

  void RtCameraSequence::load() {
    std::ifstream file(filePath(), std::ios::in | std::ios::binary);

    if (file.is_open()) {
      Header header;
      file.read((char*) &header, sizeof(header));
      int nElements = header.nElements;

      m_settings.resize(nElements);
      for (int i = 0; i < nElements; i++) {
        FrameData frameData = {};
        file.read((char*)&frameData, sizeof(frameData));
        m_settings[i] = frameData.setting;
      }
    }
  }

  bool RtCameraSequence::getRecord(int frame, RtCamera::RtCameraSetting& setting) {
    if (autoLoad() && m_settings.size() == 0 && filePath().size() > 0) {
      load();
    }
    if (frame < 0 || frame >= m_settings.size()) {
      return false;
    }
    setting = m_settings[frame];
    return true;
  }

  void RtCameraSequence::goToNextFrame() {
    if (m_settings.size() == 0) {
      currentFrameRef() = 0;
      return;
    }
    currentFrameRef() = (currentFrame() + 1) % (int) m_settings.size();
  }

  void RtCameraSequence::showImguiSettings() {
    const int maxLength = 1024;
    static char codewordBuf[maxLength] = "";
    std::string path = filePath();
    memcpy(codewordBuf, path.data(), path.size() + 1);
    ImGui::InputText("File Path", codewordBuf, IM_ARRAYSIZE(codewordBuf) - 1, ImGuiInputTextFlags_EnterReturnsTrue);
    codewordBuf[maxLength-1] = '\0';
    std::string newPath(codewordBuf);
    filePathRef() = newPath;

    if (ImGui::Button("Load Sequence")) {
      load();
    }

    ImGui::SameLine();

    if (ImGui::Button("Save Sequence")) {
      save();
    }

    int oldFrame = currentFrame();
    ImGui::SliderInt("Current Frame", &currentFrameObject(), 0, m_settings.size() -1, "%d", ImGuiSliderFlags_AlwaysClamp);
    currentFrameRef() = std::min(currentFrame(), (int)m_settings.size());

    if (oldFrame != currentFrame() && mode() == Mode::None) {
      modeRef() = Mode::Browse;
    }

    bool isRecording = mode() == Mode::Record;
    bool isPlaying = mode() == Mode::Playback;
    bool isBrowsing = mode() == Mode::Browse;
    bool isStopping = mode() == Mode::None;

    // Record Button
    {
      ImGui::BeginDisabled(isPlaying);

      if (isRecording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5, 0, 0, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 0, 0, 1));
      }

      if (ImGui::Button("Record")) {
        startRecord();
      }

      if (isRecording) {
        ImGui::PopStyleColor(2);
      }
      ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // Play Button
    {
      ImGui::BeginDisabled(isRecording);

      if (isPlaying) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0.5, 0, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 1.0, 0, 1));
      }

      if (ImGui::Button("Play")) {
        startPlay();
      }

      if (isPlaying) {
        ImGui::PopStyleColor(2);
      }

      ImGui::EndDisabled();
    }

    ImGui::SameLine();

    // Stop Button
    {
      if (ImGui::Button("Stop")) {
        modeRef() = Mode::None;
        RtxOptions::Get()->shakeCameraRef() = false;
      }
    }

    ImGui::SameLine();

    // Browse Button
    {
      ImGui::BeginDisabled(isRecording || isPlaying);

      if (isBrowsing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5, 0.5, 0, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0, 1.0, 0, 1));
      }

      if (ImGui::Button("Browse")) {
        modeRef() = Mode::Browse;
        currentFrameRef() = 0;
      }

      if (isBrowsing) {
        ImGui::PopStyleColor(2);
      }

      ImGui::EndDisabled();
    }

    ImGui::Text("Total Frames: %d", m_settings.size());
  }
}