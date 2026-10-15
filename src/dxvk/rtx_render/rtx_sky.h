/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_context.h"

#include "dxvk_device.h"
#include "rtx_resources.h"
#include "rtx_scene_manager.h"
#include "rtx_sky_utils.h"

namespace dxvk {

  static bool isSkyboxQuad(const DrawCallState& state) {
    if (state.getMaterialData().blendMode.enableBlending) {
      return false;
    }
    if (state.getGeometryData().indexCount == 0) {
      return state.getGeometryData().vertexCount <= 6;
    }
    return state.getGeometryData().indexCount <= 6;
  }

} // namespace dxvk

#include "MathLib/MathLib_f.h"

namespace dxvk {
  static Matrix4d overrideNearFarPlanes(const Matrix4d& modifiedViewToProj, float nearPlane, float farPlane) {
    // Note: Converted to floats to interface with MathLib. Ideally this should be a double still.
    Matrix4 floatModifiedViewToProj{ modifiedViewToProj };

    // Check size since struct padding can impact this memcpy
    static_assert(sizeof(float4x4) == sizeof(floatModifiedViewToProj));

    uint32_t flags;
    float    cameraParams[PROJ_NUM];
    DecomposeProjection(
      NDC_D3D,
      NDC_D3D,
      *reinterpret_cast<float4x4*>(&floatModifiedViewToProj),
      &flags,
      cameraParams,
      nullptr,
      nullptr,
      nullptr,
      nullptr);

    float4x4 newProjection;
    newProjection.SetupByAngles(
      cameraParams[PROJ_ANGLEMINX],
      cameraParams[PROJ_ANGLEMAXX],
      cameraParams[PROJ_ANGLEMINY],
      cameraParams[PROJ_ANGLEMAXY],
      nearPlane,
      farPlane,
      flags);
    memcpy(&floatModifiedViewToProj, &newProjection, sizeof(float4x4));

    return Matrix4d{ floatModifiedViewToProj };
  }
} // namespace dxvk

dxvk::RtxContext::TryHandleSkyResult dxvk::RtxContext::tryHandleSky(const DrawParameters* originalParams,
                                                                    DrawCallState* originalDrawCallState) {

  if (originalParams && originalDrawCallState && originalDrawCallState->cameraType == CameraType::Sky) {

    // Initialize the sky render targets
    {
      // Use game render target format for sky render target views whether it is linear, HDR or sRGB -- to render into the images
      m_skyRtColorFormat = m_state.om.renderTargets.color[0].view->image()->info().format;
      // Use sRGB (or linear for HDR formats) for image and sampling views -- to use in ray tracing
      m_skyColorFormat = TextureUtils::toSRGB(m_skyRtColorFormat);
      if (RtxOptions::skyForceHDR()) {
        m_skyRtColorFormat = m_skyColorFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
      }

      getResourceManager().getCompatibleViewForView(
        getResourceManager().getSkyMatte(this, m_skyColorFormat).view,
        m_skyRtColorFormat);
      initSkyProbe();
    }

    auto l_forceRaster = [&]() {
      if (!RtxOptions::skyReprojectToMainCameraSpace()) {
        return true;
      }
      // When skyForceAutoDetectedToReproject is enabled, draw calls classified as sky by
      // autoDetect are always reprojected instead of rasterized to the cubemap. This fixes
      // a class of bugs where autoDetect misclassifies world geometry as sky (due to shared
      // camera positions), causing that geometry to become invisible.
      if (RtxOptions::skyForceAutoDetectedToReproject() && originalDrawCallState->skyAutoDetected) {
        return false;
      }
      // Always rasterize sky planes
      if (isSkyboxQuad(*originalDrawCallState)) {
        return true;
      }
      return false;
    };

    if (l_forceRaster()) {
      rasterizeSky(*originalParams, *originalDrawCallState);
      return TryHandleSkyResult::Default;
    }

    // But for 3D skybox (i.e. the objects that are rendered in sky camera space),
    // we would need to know the main camera to be able to reproject from sky to main camera space,
    // so delay ray traced logic until then
    m_delayedRayTracedSky.push_back(std::move(*originalDrawCallState));
    return TryHandleSkyResult::SkipSubmit;
  }

  // Received a non-sky 'originalDrawCallState'
  assert(!originalDrawCallState || originalDrawCallState->cameraType != CameraType::Sky);

  if (m_delayedRayTracedSky.empty()) {
    return TryHandleSkyResult::Default;
  }

  // 2. Submit ray traced sky geometry as a part of the main scene by reprojecting its transform
  const RtCamera& mainCam = getSceneManager().getCameraManager().getCamera(CameraType::Main);

  if (mainCam.getLastUpdateFrame() != m_device->getCurrentFrameId()) {
    // Skip, if the main camera hasn't been updated yet
    return TryHandleSkyResult::Default;
  }

  // Note: getNearPlane() / getFarPlane() do not return actual values in case if overrideNearPlane is enabled
  const auto [mainCamNearPlane, mainCamFarPlane] = mainCam.calculateNearFarPlanes();

  const float scale = RtxOptions::skyReprojectScale();
  const Matrix4d scaleMatrix{
    scale, 0,     0,     0,
    0,     scale, 0,     0,
    0,     0,     scale, 0,
    0,     0,     0,     1,
  };

  for (DrawCallState& skyGeometry : m_delayedRayTracedSky) {
    // Swap camera
    skyGeometry.cameraType = CameraType::Main;
    skyGeometry.modifyCategoryFlags().clr(InstanceCategories::Sky);

    // And reproject
    DrawCallTransforms& skyTransform = skyGeometry.modifyTransformData();

    // Near / far planes must match to prevent problems related to the mismatching Z-space
    const Matrix4d skyViewToProjection = overrideNearFarPlanes(
      skyTransform.viewToProjection,
      mainCamNearPlane,
      mainCamFarPlane);

    const Matrix4d skyViewToMainWorld =
      mainCam.getViewToWorld(false) *
      (mainCam.getProjectionToView() * skyViewToProjection) *
      scaleMatrix;

    skyTransform.objectToWorld    = skyViewToMainWorld * skyTransform.worldToView * skyTransform.objectToWorld;
    skyTransform.worldToView      = mainCam.getWorldToView();
    skyTransform.viewToProjection = mainCam.getViewToProjection();
    skyTransform.sanitize();

    getSceneManager().submitDrawState(this, skyGeometry, nullptr);
  }
  m_delayedRayTracedSky.clear();

  // since 'originalDrawCallState' is not a sky (it just triggers delayed sky submit),
  // proceed with the default path
  assert(!originalDrawCallState || originalDrawCallState->cameraType != CameraType::Sky);
  return TryHandleSkyResult::Default;
}
