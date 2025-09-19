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

#include <array>
#include <vector>
#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx/utility/shader_types.h"
#include "rtx/concept/light/light_types.h"
#include "rtx_lights.h"
#include "rtx_camera_manager.h"
#include "rtx_common_object.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"

using remixapi_LightHandle = struct remixapi_LightHandle_T*;

struct RaytraceArgs;

namespace dxvk {
class DxvkContext;
class DxvkDevice;

struct LightRange {
  uint32_t offset;
  uint32_t count;
};

struct LightManager : public CommonDeviceObject {
public:
  enum class FallbackLightMode : int {
    Never = 0,
    NoLightsPresent,
    Always,
  };

  enum class FallbackLightType : int {
    Distant = 0,
    Sphere,
  };

  LightManager(LightManager const&) = delete;
  LightManager& operator=(LightManager const&) = delete;

  explicit LightManager(DxvkDevice* device);
  ~LightManager();

  void showImguiSettings();
  void showImguiLightOverview();
  void showImguiDebugVisualization() const;

  const std::unordered_map<XXH64_hash_t, RtLight>& getLightTable() const { return m_lights; }
  const Rc<DxvkBuffer> getLightBuffer() const { return m_lightBuffer; }
  const Rc<DxvkBuffer> getPreviousLightBuffer() const { return m_previousLightBuffer.ptr() ? m_previousLightBuffer : m_lightBuffer; }
  const Rc<DxvkBuffer> getLightMappingBuffer() const { return m_lightMappingBuffer; }
  const uint32_t getActiveCount() const { return m_currentActiveLightCount; }
  const DomeLightArgs& getDomeLightArgs() const { return m_gpuDomeLightArgs; }

  void clear();
  void clearFromUIThread();

  void garbageCollection(RtCamera& camera);

  void dynamicLightMatching();

  void prepareSceneData(Rc<DxvkContext> ctx, CameraManager const& cameraManager);

  void addGameLight(D3DLIGHTTYPE type, const RtLight& light);
  RtLight* addLight(const RtLight& light, const RtLightAntiCullingType antiCullingType);
  RtLight* addLight(const RtLight& light, const DrawCallState& drawCallState, const RtLightAntiCullingType antiCullingType);

  // Externally tracked lights are lights whose lifecycle (creation, update, removal) is managed externally, rather than the
  // existing frame-to-frame tracking and anti-culling systems. These are kept separte to avoid any interference from anti culling
  // and light matching.
  RtLight* createExternallyTrackedLight(const RtLight& light);
  void updateExternallyTrackedLight(RtLight* light, const RtLight& newLight);
  void removeExternallyTrackedLight(RtLight* light);

  void addExternalLight(remixapi_LightHandle handle, const RtLight& rtlight);
  void addExternalDomeLight(remixapi_LightHandle handle, const DomeLight& domeLight);
  void removeExternalLight(remixapi_LightHandle handle);
  void addExternalLightInstance(remixapi_LightHandle enabledLight);

  void setRaytraceArgs(RaytraceArgs& raytraceArgs, uint32_t rtxdiInitialLightSamples, uint32_t volumeRISInitialLightSamples, uint32_t risLightSamples) const;
  
  uint getLightCount(uint type);


private:
  std::unordered_map<XXH64_hash_t, RtLight> m_lights;
  // Collection of lights whose lifecycle (creation, update, removal) is managed externally rather than by LightManager's
  // frame-to-frame tracking and anti-culling systems. These are kept separte to avoid any interference from anti culling
  // and light matching.
  // NOTE: this is an unordered_map rather than an unordered_set because we need the iteration
  // order to be deterministic in tests.
  std::unordered_map<uint64_t, RtLight> m_externallyTrackedLights;
  uint64_t m_nextExternallyTrackedLightId = 0;
  // Note: A fallback light tracked seperately and handled specially to not be mixed up with
  // lights provided from the application.
  std::optional<RtLight> m_fallbackLight{};
  std::unordered_map<remixapi_LightHandle, RtLight> m_externalLights;
  std::unordered_map<remixapi_LightHandle, DomeLight> m_externalDomeLights;
  std::unordered_set<remixapi_LightHandle> m_externalActiveLightList;
  remixapi_LightHandle m_externalActiveDomeLight = nullptr;
  DomeLightArgs m_gpuDomeLightArgs;

  Rc<DxvkBuffer> m_lightBuffer;
  Rc<DxvkBuffer> m_previousLightBuffer;
  Rc<DxvkBuffer> m_lightMappingBuffer;

  uint32_t m_currentActiveLightCount = 0;
  std::array<LightRange, lightTypeCount> m_lightTypeRanges;
  // Note: The following vectors are included as members rather as local variables in the
  // prepareSceneData function where they are primarily used to prevent redundant allocations/frees
  // of the memory behind these buffers between each call (at the cost of slightly more persistent
  // memory usage, but these buffers are fairly small at only 4 MiB or so max with 2^16 lights present).
  std::vector<RtLight*> m_linearizedLights{};
  std::vector<unsigned char> m_lightsGPUData{};
  std::vector<uint16_t> m_lightMappingData{};

  // Mutex to prevent the debugging UI from accessing the light data after it's been deleted.
  mutable std::mutex m_lightUIMutex;
  std::unique_lock<std::mutex> m_lightDebugUILock = std::unique_lock<std::mutex>(m_lightUIMutex, std::defer_lock);

  bool getActiveDomeLight(DomeLight& lightOut);

  void garbageCollectionInternal();

  // Similarity check.
  //  Returns -1 if not similar
  //  Returns 0~1 if similar, higher is more similar
  static float isSimilar(const RtLight& a, const RtLight& b, float distanceThreshold);
  static void updateLight(const RtLight& in, RtLight& out);

  RTX_OPTION("rtx", bool, suppressLightKeeping, false, 
             "If true, Remix doesn't keep game's original light sources for many frames. "
             "For example, if a game switches a point light off, then, in Remix, the light might still be rendered as if it's enabled: "
             "because the light would be cached (kept) for many consecutive frames. (So to solve this, set this option to True).");

  RTX_OPTION("rtx", bool, ignoreGameDirectionalLights, false, "Ignores any directional lights coming from the original game (lights added via toolkit still work).");
  RTX_OPTION("rtx", bool, ignoreGamePointLights, false, "Ignores any point lights coming from the original game (lights added via toolkit still work).");
  RTX_OPTION("rtx", bool, ignoreGameSpotLights, false, "Ignores any spot lights coming from the original game (lights added via toolkit still work).");

  // Legacy light translation Options
  // The mode to determine when to create a fallback light. Never (0) never creates the light, NoLightsPresent (1) creates the fallback light only when no lights are provided to Remix, and Always (2)
  // always creates the fallback light. Primarily a debugging feature, users should create their own lights via the Remix workflow rather than relying on this feature to provide lighting.
  // As such, this option should be set to Never for "production" builds of Remix creations to avoid the fallback light from appearing in games unintentionally in cases where no lights exist (which is
  // the default behavior when set to NoLightsPresent).
  RTX_OPTION("rtx", FallbackLightMode, fallbackLightMode, FallbackLightMode::NoLightsPresent,
             "The mode to determine when to create a fallback light.\n"
             "Never (0) never creates the light, NoLightsPresent (1) creates the fallback light only when no lights are provided to Remix, and Always (2) always creates the fallback light.\n"
             "Primarily a debugging feature, users should create their own lights via the Remix workflow rather than relying on this feature to provide lighting.\n"
             "As such, this option should be set to Never for \"production\" builds of Remix creations to avoid the fallback light from appearing in games unintentionally in cases where no lights exist (which is the default behavior when set to NoLightsPresent).");
  RTX_OPTION("rtx", FallbackLightType, fallbackLightType, FallbackLightType::Distant, "The light type to use for the fallback light. Determines which other fallback light options are used.");
  RTX_OPTION("rtx", Vector3, fallbackLightRadiance, Vector3(1.6f, 1.8f, 2.0f), "The radiance to use for the fallback light (used across all light types).");
  RTX_OPTION("rtx", Vector3, fallbackLightDirection, Vector3(-0.2f, -1.0f, 0.4f), "The direction to use for the fallback light (used only for Distant light types)");
  RTX_OPTION("rtx", float, fallbackLightAngle, 5.0f, "The angular size in degrees to use for the fallback light (used only for Distant light types). Should only be within the range [0, 180].");
  RTX_OPTION("rtx", float, fallbackLightRadius, 5.0f, "The radius to use for the fallback light (used only for Sphere light types).");
  RTX_OPTION("rtx", Vector3, fallbackLightPositionOffset, Vector3(0.0f, 0.0f, 0.0f), "The position offset from the camera origin to use for the fallback light (used only for non-Distant light types).");
  RTX_OPTION("rtx", bool, enableFallbackLightShaping, false, "Enables light shaping on the fallback light (only used for non-Distant light types).");
  RTX_OPTION("rtx", bool, enableFallbackLightViewPrimaryAxis, false,
             R"(Enables usage of the camera's view axis as the primary axis for the fallback light's shaping (only used for non - Distant light types). Typically the shaping primary axis may be specified directly, but if desired it may be set to the camera's view axis for a "flashlight" effect.)");
  RTX_OPTION("rtx", Vector3, fallbackLightPrimaryAxis, Vector3(0.0f, 0.0f, -1.0f), "The primary axis to use for the fallback light shaping (used only for non-Distant light types).");
  RTX_OPTION("rtx", float, fallbackLightConeAngle, 25.0f, "The cone angle in degrees to use for the fallback light shaping (used only for non-Distant light types with shaping enabled). Should only be within the range [0, 180].");
  RTX_OPTION("rtx", float, fallbackLightConeSoftness, 0.1f, "The cone softness to use for the fallback light shaping (used only for non-Distant light types with shaping enabled).");
  RTX_OPTION("rtx", float, fallbackLightFocusExponent, 2.0f, "The focus exponent to use for the fallback light shaping (used only for non-Distant light types with shaping enabled).");
  RTX_OPTION("rtx", bool, calculateLightIntensityUsingLeastSquares, true, "Enable usage of least squares for approximating a light's falloff curve rather than a more basic single point approach. This will generally result in more accurate matching of the original application's custom light attenuation curves, especially with non physically based linear-style attenuation.");
  RTX_OPTION("rtx", float, lightConversionSphereLightFixedRadius, 4.f, "The fixed radius in world units to use for legacy lights converted to sphere lights (currently point and spot lights will convert to sphere lights). Use caution with large light radii as many legacy lights will be placed close to geometry and intersect it, causing suboptimal light sampling performance or other visual artifacts (lights clipping through walls, etc).");
  RTX_OPTION("rtx", float, lightConversionDistantLightFixedIntensity, 1.0f, "The fixed intensity (in W/sr) to use for legacy lights converted to distant lights (currently directional lights will convert to distant lights).");
  RTX_OPTION("rtx", float, lightConversionDistantLightFixedAngle, 0.0349f, "The angular size in radians of the distant light source for legacy lights converted to distant lights. Set to ~2 degrees in radians by default. Should only be within the range [0, pi].");
  RTX_OPTION("rtx", float, lightConversionMaxIntensity, FLT_MAX, "The highest intensity value a converted light can have.");
  RTX_OPTION("rtx", float, lightConversionIntensityFactor, 1.f, "Scales the converted light intensities.");
};

}  // namespace dxvk

