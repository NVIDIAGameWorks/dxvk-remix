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

#include <optional>

#include "rtx_types.h"
#include "rtx_option.h"
#include "rtx_common_object.h"

namespace dxvk {
  class RtCamera;
  class DxvkDevice;
  class RayPortalManager;
  class Config;

  namespace CameraType {
    enum Enum : uint32_t {
      Main = 0,     // Main camera
      ViewModel,    // Camera for view model rendering
      Portal0,      // Camera associated with rendering portal 0
      Portal1,      // Camera associated with rendering portal 1
      Unknown,      // Unset camera state, used mainly for state tracking. Its camera object is aliased 
                    // with the Main camera object, so on access it retrieves the Main camera

      Count
    };
  }

  class CameraManager : public CommonDeviceObject {
  public:
    explicit CameraManager(DxvkDevice* device);
    ~CameraManager() = default;

    const RtCamera& getCamera(CameraType::Enum cameraType) const { return **m_cameras[cameraType]; }
    RtCamera& getCamera(CameraType::Enum cameraType) { return **m_cameras[cameraType]; }

    const RtCamera& getMainCamera() const { return getCamera(CameraType::Main); }
    RtCamera& getMainCamera() { return getCamera(CameraType::Main); }

    const RtCamera& getLastSetCamera() const { return getCamera(m_lastSetCameraType); }
    RtCamera& getLastSetCamera() { return getCamera(m_lastSetCameraType); }
    CameraType::Enum getLastSetCameraType() const { return m_lastSetCameraType; }
    
    bool isCameraValid(CameraType::Enum cameraType) const;

    void initSettings(const dxvk::Config& config);
    void onFrameEnd();

    bool processCameraData(const DrawCallState& input);

    uint32_t getLastCameraCutFrameId() const { return m_lastCameraCutFrameId; }
    bool isCameraCutThisFrame() const;

  private:
    XXH64_hash_t calculateCameraHash(float fov, const Matrix4& worldToView, bool stencilEnabledState);

    std::array<std::optional<Rc<RtCamera>>, CameraType::Count> m_cameras;
    std::unordered_map<XXH64_hash_t, CameraType::Enum> m_cameraHashToType;
    CameraType::Enum m_lastSetCameraType = CameraType::Unknown;

    RTX_OPTION("rtx", bool, rayPortalEnabled, false, "Enables ray portal support. Note this requires portal texture hashes to be set for the ray portal geometries in rtx.rayPortalModelTextureHashes.");
    RTX_OPTION("rtx.camera", bool, trackCamerasSeenStats, false, "Enables tracking and reporting of statistics for Cameras seen within a frame.");

    std::string m_projParamsSeen;
    std::string m_lastProjParamsSeen;
    std::string m_viewTransformSeen;
    std::string m_lastViewTransformParamsSeen;
    uint32_t m_lastCameraCutFrameId = -1;
  };
}  // namespace dxvk

