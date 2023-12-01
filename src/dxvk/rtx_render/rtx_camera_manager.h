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
#pragma once

#include "rtx_camera.h"
#include "rtx_option.h"
#include "rtx_types.h"
#include "rtx_common_object.h"

namespace dxvk {
  class DxvkDevice;
  class RayPortalManager;
  class Config;

  class CameraManager : public CommonDeviceObject {
  public:
    explicit CameraManager(DxvkDevice* device);
    ~CameraManager() override = default;

    CameraManager(const CameraManager& other) = delete;
    CameraManager(CameraManager&& other) noexcept = delete;
    CameraManager& operator=(const CameraManager& other) = delete;
    CameraManager& operator=(CameraManager&& other) noexcept = delete;

    const RtCamera& getCamera(CameraType::Enum cameraType) const { return accessCamera(*this, cameraType); }
    RtCamera& getCamera(CameraType::Enum cameraType) { return accessCamera(*this, cameraType); }

    const RtCamera& getMainCamera() const { return getCamera(CameraType::Main); }
    RtCamera& getMainCamera() { return getCamera(CameraType::Main); }

    CameraType::Enum getLastSetCameraType() const { return m_lastSetCameraType; }
    
    bool isCameraValid(CameraType::Enum cameraType) const;

    void onFrameEnd();

    // Calculates a camera type for the specified draw call.
    CameraType::Enum processCameraData(const DrawCallState& input);
    void processExternalCamera(CameraType::Enum type, const Matrix4& worldToView, const Matrix4& viewToProjection);

    uint32_t getLastCameraCutFrameId() const { return m_lastCameraCutFrameId; }
    bool isCameraCutThisFrame() const;

    bool was3DSkyInPrevFrame() const { return m_was3DSkyInPrevFrame; }

  private:
    template<
      typename T,
      std::enable_if_t<std::is_same_v<T, CameraManager> || std::is_same_v<T, const CameraManager>, bool> = true>
    static auto& accessCamera(T& cameraManager, CameraType::Enum cameraType) {
      assert(cameraType < CameraType::Count);
      // Default Unknown to Main camera object
      // since cameras can get rejected but rtx pipeline can
      // still try to retrieve a camera corresponding to a DrawCall object.
      // In that case it will read from the Main camera.
      // This is OK as we never update Unknown camera directly.
      if (cameraType == CameraType::Unknown) {
        return cameraManager.m_cameras[CameraType::Main];
      }
      return cameraManager.m_cameras[cameraType];
    }

  private:
    std::array<RtCamera, CameraType::Count> m_cameras;
    CameraType::Enum m_lastSetCameraType = CameraType::Unknown;
    uint32_t m_lastCameraCutFrameId = -1;

    bool m_was3DSkyInPrevFrame = false;

    struct CameraInfoAccum {
      Vector3 lastPosition;
      uint32_t uniquePositions;
    } m_camerasInfoAccum = {};

    RTX_OPTION("rtx", bool, rayPortalEnabled, false, "Enables ray portal support. Note this requires portal texture hashes to be set for the ray portal geometries in rtx.rayPortalModelTextureHashes.");
  };
}  // namespace dxvk

