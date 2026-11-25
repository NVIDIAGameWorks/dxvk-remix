/*
* Copyright (c) 2026, Kim2091
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

#include <memory>
#include <mutex>

#include "rtx_common_object.h"
#include "rtx_option.h"
#include "rtx_resources.h"

namespace numos {
  struct FrameOutputs;
  struct SimulationState;
}

// These structs are shared with the shaders, so they live in the global namespace.
struct CompositeArgs;
struct RaytraceArgs;

namespace dxvk {

  class RtxContext;

  enum class SkyMode : int {
    SkyboxRasterization = 0,
    Numos = 1
  };

  // Hosts the NUMOS sky, see documentation/NumosSky.md.
  class RtxNumos : public CommonDeviceObject {
  public:
    RTX_OPTION("rtx", SkyMode, skyMode, SkyMode::SkyboxRasterization,
               "The source of the sky.\n"
               "0: Original Game Sky, rasterizes the game's own sky.\n"
               "1: NUMOS Atmosphere, renders a physically based sky with clouds, haze and weather.");
    RTX_OPTION_FLAG("rtx.skyProvider", uint32_t, worldChangeCount, 0, RtxOptionFlags::NoSave,
                    "A signal for game integrations. Changing it tells the sky that the world under the camera may be a different one, "
                    "for example after loading a save, so the sky discards the ground level it measured.\n"
                    "A teleport farther than rtx.atmosphere.cloudAnchorCutKm is detected without it.");
    RTX_OPTION("rtx.numos", bool, cloudMotionVectors, true,
               "Enables depth and screen space motion vectors of the clouds in pixels where they hide the surface behind them. "
               "Upscalers, Ray Reconstruction, motion blur and frame generation then move the clouds separately from that surface.\n"
               "The clouds are reprojected as static, without their drift in the wind.");
    RTX_OPTION_ARGS("rtx.numos", float, cloudMotionVectorCoverage, 0.5f,
                    "The cloud opacity from which a pixel takes the cloud's depth. The motion vector blends to the cloud's within 0.15 of this opacity.",
                    args.minValue = 0.0f, args.maxValue = 1.0f);

    explicit RtxNumos(DxvkDevice* device);
    ~RtxNumos();

    static bool enabled() {
      return skyMode() == SkyMode::Numos;
    }

    // Returns true while the game's sky gives way, which needs NUMOS's shaders to have loaded.
    bool replacesGameSky() const {
      return enabled() && !m_shaderLoadFailed;
    }

    bool hasShaderLoadFailed() const {
      return m_shaderLoadFailed;
    }

    // Registers the shaders of NUMOS and of the composite that draws it, once the sky mode selects NUMOS.
    void prewarmShaders();

    // Applies the previous frame's sun, moon and lightning lights.
    // It has to run before SceneManager::prepareSceneData, whose garbage collection releases the light lock.
    void updateLights(RtxContext& ctx);

    void beginFrame(RtxContext& ctx, RaytraceArgs& constants);
    void dispatchPostDepth(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void bindCommonResources(RtxContext& ctx);
    void bindCompositeResources(RtxContext& ctx, CompositeArgs& args);

    // Returns whether the composite writes the clouds and haze into the mask that asks DLSS and XeSS to favor the current frame.
    bool writesUpscalerBiasMask() const;

    // Returns this frame's outputs.
    // Returns null while NUMOS is not rendering.
    const numos::FrameOutputs* outputs() const;

    numos::SimulationState simulationState() const;
    void setSimulationState(const numos::SimulationState& state);

    // The UI runs on the present thread and shares the renderer and the weather presets with the render thread.
    std::unique_lock<std::recursive_mutex> lockState() const {
      return std::unique_lock<std::recursive_mutex>(m_stateMutex);
    }

  private:
    // NUMOS types stay out of this header, because dxvk_objects.h includes it.
    struct Impl;

    Impl& getImpl();
    void loadNightSky();
    void gatherLocalLights(RtxContext& ctx);

    std::unique_ptr<Impl> m_impl;
    Rc<DxvkBuffer> m_argsBuffer;
    mutable std::recursive_mutex m_stateMutex;

    uint32_t m_frameIndex = 0;
    uint32_t m_lastCameraCutFrame = 0;
    uint32_t m_lastWorldChangeCount = 0;
    bool m_rendering = false;
    bool m_hazeActive = false;
    bool m_hostSkyOverride = false;
    bool m_postDepthDone = false;
    bool m_nightSkyLoaded = false;
    bool m_shadersPrewarmed = false;
    bool m_shaderLoadFailed = false;
    bool m_commonResourcesBound = false;
  };

}
