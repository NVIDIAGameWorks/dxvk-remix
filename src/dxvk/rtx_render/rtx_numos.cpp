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
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>

#include "numos/renderer.h"

#include "rtx_numos.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_composite.h"
#include "rtx_context.h"
#include "rtx_debug_view.h"
#include "rtx_global_volumetrics.h"
#include "rtx_light_manager.h"
#include "rtx_numos_gpu.h"
#include "rtx_numos_settings.h"
#include "rtx_numos_weather.h"
#include "rtx_options.h"
#include "rtx_scene_manager.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/composite/composite_args.h"
#include "rtx/pass/composite/composite_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx/utility/debug_view_indices.h"
#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_global_time.h"
#include "../../util/util_once.h"

namespace dxvk {

  namespace {
    numos::Vector3 toNumos(const Vector3& v) {
      return numos::Vector3(v.x, v.y, v.z);
    }

    Vector3 toRemix(const numos::Vector3& v) {
      return Vector3(v.x, v.y, v.z);
    }

    // These index the external views of NumosGpu.
    constexpr uint32_t kLinearViewZView = 0;
    constexpr uint32_t kBlueNoiseView = 1;

    constexpr float kMinDistantHalfAngle = 0.0005f;

    static_assert(sizeof(Camera) == sizeof(numos::Camera), "Remix's Camera and NUMOS's copy of it differ");
    static_assert(sizeof(numos::Camera::viewToWorld) == 16 * sizeof(float), "NUMOS mat4 is not 16 floats");

    std::vector<uint8_t> readFile(const std::string& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        return {};
      }
      return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // Matches the variant that NUMOS's dispatchCloudRender picks.
    numos::PassId getCloudRenderPass(const numos::Settings& settings) {
      if (settings.cloudProfilingMode == 9) {
        return numos::PassId::cloud_render_reference;
      }
      return settings.cloudPerformancePreset == 1 ? numos::PassId::cloud_render_fast : numos::PassId::cloud_render;
    }

    // Binds NUMOS's outputs.
    // Binds null resources and a valid sampler when there is no GPU adapter.
    class OutputBinder {
    public:
      OutputBinder(RtxContext& ctx, const NumosGpu* pGpu)
        : m_ctx(ctx)
        , m_pGpu(pGpu)
        , m_fallbackSampler(ctx.getResourceManager().getSampler(
            VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) { }

      void bindView(uint32_t slot, numos::GpuHandle handle) const {
        m_ctx.bindResourceView(slot, m_pGpu ? m_pGpu->getView(handle) : nullptr, nullptr);
      }

      void bindSampler(uint32_t slot, numos::GpuHandle handle) const {
        const Rc<DxvkSampler> sampler = m_pGpu ? m_pGpu->getSampler(handle) : nullptr;
        m_ctx.bindResourceSampler(slot, sampler != nullptr ? sampler : m_fallbackSampler);
      }

      void bindBuffer(uint32_t slot, numos::GpuHandle handle) const {
        const Rc<DxvkBuffer> buffer = m_pGpu ? m_pGpu->getBuffer(handle) : nullptr;
        m_ctx.bindResourceBuffer(slot, buffer != nullptr ? DxvkBufferSlice(buffer) : DxvkBufferSlice());
      }

    private:
      RtxContext& m_ctx;
      const NumosGpu* m_pGpu;
      Rc<DxvkSampler> m_fallbackSampler;
    };
  }

  struct RtxNumos::Impl {
    NumosGpu gpu;
    numos::Renderer renderer;
    std::vector<numos::LocalLight> localLights;
    numos::EmittedLight emittedLights[numos::kMaxMoons + 2];
    RtLight* lights[numos::kMaxMoons + 2] = {};
    float emittedRadianceScale = 1.0f;

    explicit Impl(DxvkDevice* pDevice)
      : gpu(pDevice, env::getDllDirectory() + "/numos/spirv") { }
  };

  RtxNumos::RtxNumos(DxvkDevice* device)
    : CommonDeviceObject(device) { }

  RtxNumos::~RtxNumos() {
    if (m_impl != nullptr) {
      m_impl->renderer.releaseGpuResources(m_impl->gpu);
    }
  }

  // Nothing is allocated until the sky mode first selects NUMOS.
  RtxNumos::Impl& RtxNumos::getImpl() {
    if (m_impl == nullptr) {
      NumosSettings::installLog();
      m_impl = std::make_unique<Impl>(device());

      DxvkBufferCreateInfo info;
      // The padding keeps writes partial, so DXVK updates the buffer in the command stream instead of renaming it.
      info.size = align(sizeof(numos::AtmosphereArgs) + 256, 256);
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT;
      m_argsBuffer = device()->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                            DxvkMemoryStats::Category::RTXBuffer, "NUMOS AtmosphereArgs");
    }
    return *m_impl;
  }

  const numos::FrameOutputs* RtxNumos::outputs() const {
    return m_rendering ? &m_impl->renderer.outputs() : nullptr;
  }

  numos::SimulationState RtxNumos::simulationState() const {
    return m_impl ? m_impl->renderer.simulationState() : numos::SimulationState {};
  }

  void RtxNumos::setSimulationState(const numos::SimulationState& state) {
    // Only the render thread creates the renderer, so updateLights can read it without the lock.
    if (m_impl != nullptr) {
      m_impl->renderer.setSimulationState(state);
    }
  }

  bool RtxNumos::writesUpscalerBiasMask() const {
    // Ray Reconstruction takes no mask.
    return m_rendering && !RtxOptions::isRayReconstructionEnabled() &&
           (RtxOptions::isDLSSEnabled() || RtxOptions::isXeSSEnabled());
  }

  void RtxNumos::prewarmShaders() {
    if (!enabled() || m_shadersPrewarmed) {
      return;
    }

    const auto stateLock = lockState();
    m_shadersPrewarmed = true;

    Impl& impl = getImpl();
    impl.gpu.prewarmShaders(getCloudRenderPass(NumosSettings::resolved()));
    m_shaderLoadFailed = !impl.gpu.areShadersValid();
    if (!m_shaderLoadFailed) {
      CompositePass::prewarmSkyProviderShader();
    }
  }

  void RtxNumos::loadNightSky() {
    if (m_nightSkyLoaded) {
      return;
    }
    m_nightSkyLoaded = true;

    const std::string directory = env::getDllDirectory() + "/numos/nightsky/";
    const std::vector<uint8_t> stars = readFile(directory + "stars.bin");
    const std::vector<uint8_t> milkyWay = readFile(directory + "milkyway.bin");
    const std::vector<uint8_t> starlightLow = readFile(directory + "starlight_low.bin");
    const std::vector<uint8_t> moon = readFile(directory + "moon.bin");
    if (stars.empty() || milkyWay.empty() || starlightLow.empty() || moon.empty()) {
      Logger::warn(str::format("[NUMOS] Night sky data is missing from ", directory,
                               ". The sky has no stars or a plain moon until the whole numos folder is installed."));
    }

    // NUMOS copies the data and streams it to the GPU over the next frames.
    numos::NightSkyAssets assets;
    assets.stars = stars.data();
    assets.starsSize = stars.size();
    assets.milkyWay = milkyWay.data();
    assets.milkyWaySize = milkyWay.size();
    assets.starlightLow = starlightLow.data();
    assets.starlightLowSize = starlightLow.size();
    assets.moon = moon.data();
    assets.moonSize = moon.size();
    m_impl->renderer.setNightSkyAssets(assets);
  }

  void RtxNumos::gatherLocalLights(RtxContext& ctx) {
    Impl& impl = *m_impl;
    impl.localLights.clear();
    if (!m_hazeActive) {
      return;
    }

    for (const RtLight* pLight : ctx.getSceneManager().getLightManager().getLinearizedLights()) {
      // NUMOS's own lightning light already glows in the haze.
      if (std::find(std::begin(impl.lights), std::end(impl.lights), pLight) != std::end(impl.lights)) {
        continue;
      }

      numos::LocalLight local;
      const RtLightShaping* pShaping = nullptr;
      switch (pLight->getType()) {
      case RtLightType::Sphere: {
        const RtSphereLight& sphere = pLight->getSphereLight();
        local.type = numos::LocalLight::Type::Sphere;
        local.position = toNumos(sphere.getPosition());
        local.radiance = toNumos(sphere.getRadiance() * sphere.getVolumetricRadianceScale());
        local.radius = sphere.getRadius();
        pShaping = &sphere.getShaping();
        break;
      }
      case RtLightType::Rect: {
        const RtRectLight& rect = pLight->getRectLight();
        local.type = numos::LocalLight::Type::Rect;
        local.position = toNumos(rect.getPosition());
        local.radiance = toNumos(rect.getRadiance() * rect.getVolumetricRadianceScale());
        const Vector2 dimensions = rect.getDimensions();
        local.dimensions = numos::Vector2(dimensions.x, dimensions.y);
        pShaping = &rect.getShaping();
        break;
      }
      case RtLightType::Disk: {
        const RtDiskLight& disk = pLight->getDiskLight();
        local.type = numos::LocalLight::Type::Disk;
        local.position = toNumos(disk.getPosition());
        local.radiance = toNumos(disk.getRadiance() * disk.getVolumetricRadianceScale());
        const Vector2 halfDimensions = disk.getHalfDimensions();
        local.dimensions = numos::Vector2(halfDimensions.x, halfDimensions.y);
        pShaping = &disk.getShaping();
        break;
      }
      case RtLightType::Cylinder: {
        const RtCylinderLight& cylinder = pLight->getCylinderLight();
        local.type = numos::LocalLight::Type::Cylinder;
        local.position = toNumos(cylinder.getPosition());
        local.radiance = toNumos(cylinder.getRadiance() * cylinder.getVolumetricRadianceScale());
        local.radius = cylinder.getRadius();
        local.axisLength = cylinder.getAxisLength();
        break;
      }
      default:
        // NUMOS lights the air with its own sun and sky, so distant lights are left out.
        continue;
      }

      if (pShaping != nullptr && pShaping->getEnabled()) {
        local.shaped = true;
        local.shapingDirection = toNumos(pShaping->getDirection());
        local.cosConeAngle = pShaping->getCosConeAngle();
        local.coneSoftness = pShaping->getConeSoftness();
        local.focusExponent = pShaping->getFocusExponent();
      }
      impl.localLights.push_back(local);
    }
  }

  void RtxNumos::updateLights(RtxContext& ctx) {
    if (m_impl == nullptr) {
      return;
    }

    Impl& impl = *m_impl;
    SceneManager& sceneManager = ctx.getSceneManager();
    LightManager& lightManager = sceneManager.getLightManager();
    const bool hasSky = m_rendering && replacesGameSky();
    const float inverseScale = 1.0f / impl.emittedRadianceScale;
    bool lightsChanged = false;

    for (uint32_t i = 0; i < std::size(impl.lights); ++i) {
      const numos::EmittedLight& emitted = impl.emittedLights[i];
      RtLight*& pSlot = impl.lights[i];

      std::optional<RtLight> light;
      if (hasSky && emitted.active) {
        const Vector3 radiance(std::max(emitted.radiance.x * inverseScale, 0.0f),
                               std::max(emitted.radiance.y * inverseScale, 0.0f),
                               std::max(emitted.radiance.z * inverseScale, 0.0f));
        if (emitted.type == numos::EmittedLight::Type::Distant) {
          const float halfAngle = std::max(emitted.halfAngle, kMinDistantHalfAngle);
          if (auto distant = RtDistantLight::tryCreate(toRemix(emitted.direction), halfAngle, radiance)) {
            light.emplace(*distant);
          }
        } else if (auto sphere = RtSphereLight::tryCreate(toRemix(emitted.position), radiance, emitted.radius, RtLightShaping())) {
          light.emplace(*sphere);
        }
      }

      if (!light) {
        if (pSlot != nullptr) {
          pSlot->markForGarbageCollection();
          pSlot = nullptr;
          lightsChanged = true;
        }
        continue;
      }

      light->isDynamic = true;
      light->isCloudShadowed = emitted.cloudShadowed;
      if (pSlot == nullptr) {
        pSlot = lightManager.createExternallyTrackedLight(*light);
      } else {
        lightManager.updateExternallyTrackedLight(pSlot, *light);
      }
      lightsChanged = true;
    }

    // The GPU resident scene skips the scene's garbage collection.
    if (lightsChanged && SceneManager::gpuResidentScene()) {
      lightManager.garbageCollection(sceneManager.getCamera());
    }
  }

  void RtxNumos::beginFrame(RtxContext& ctx, RaytraceArgs& constants) {
    constants.skyProviderActive = false;
    constants.skyRadianceUnscale = 1.0f;

    if (!enabled() && !m_rendering) {
      return;
    }

    const auto stateLock = lockState();
    m_rendering = false;
    m_hazeActive = false;
    m_postDepthDone = false;
    if (!enabled()) {
      m_impl->gpu.endFrame();
      return;
    }

    prewarmShaders();
    Impl& impl = getImpl();
    m_shaderLoadFailed = !impl.gpu.areShadersValid();
    if (m_shaderLoadFailed) {
      ONCE(Logger::err("[NUMOS] The shaders did not load, so the sky falls back to the game's sky."));
      return;
    }

    Rc<DxvkContext> dxvkCtx = &ctx;
    ScopedGpuProfileZone(&ctx, "NUMOS Sky");

    loadNightSky();

    // The weather reaches NUMOS inside the settings, so FrameInputs::weather stays null.
    numos::Settings settings = ctx.getCommonObjects()->metaNumosWeather().update(NumosSettings::resolved(),
                                                                                 GlobalTime::get().deltaTime());

    SceneManager& sceneManager = ctx.getSceneManager();
    m_hostSkyOverride = sceneManager.getLightManager().getDomeLightArgs().active;

    // Remix volumetrics and the NUMOS haze are exclusive, see "Haze and volumetrics" in documentation/NumosSky.md.
    if (RtxGlobalVolumetrics::enable()) {
      settings.aerialPerspective = false;
      // The cloud composite applies the height fog by itself, so the fog has to be cleared separately.
      settings.fogVisibilityMeters = 0.0f;
    }
    // The haze assumes air between the camera and the surfaces, so a camera inside a medium gets none.
    m_hazeActive = settings.aerialPerspective && !m_hostSkyOverride &&
                   constants.startInMediumMaterialIndex == SURFACE_INDEX_INVALID;
    impl.renderer.setSettings(settings);

    impl.gpu.beginFrame(dxvkCtx);

    const uint32_t deviceFrame = ctx.getDevice()->getCurrentFrameId();
    const CameraManager& cameraManager = sceneManager.getCameraManager();
    const RtCamera& camera = sceneManager.getCamera();
    Resources& resources = ctx.getResourceManager();

    numos::FrameInputs inputs;
    inputs.frameIndex = m_frameIndex;
    inputs.deltaSeconds = GlobalTime::get().deltaTime();
    inputs.cameraCut = cameraManager.getLastCameraCutFrameId() + 1u >= deviceFrame;
    if (inputs.cameraCut) {
      m_lastCameraCutFrame = m_frameIndex;
    }
    inputs.lastCameraCutFrame = m_lastCameraCutFrame;
    inputs.worldChanged = worldChangeCount() != m_lastWorldChangeCount;
    m_lastWorldChangeCount = worldChangeCount();

    Camera shaderCamera = camera.getShaderConstants(/*freecam=*/true);
    {
      // NUMOS's temporal filters need the previous projection with this frame's jitter, as numos::makeCamera builds it.
      // Remix's constants hold the previous frame's jitter there, which would shift the history every frame.
      const Matrix4d jitter = camera.getViewToProjectionJittered() - camera.getViewToProjection();
      const Matrix4d prevViewToProjectionJittered = camera.getPreviousViewToProjection() + jitter;
      const Matrix4d& prevWorldToView = camera.getPreviousWorldToView(/*freecam=*/true);
      shaderCamera.prevWorldToProjectionJittered = prevViewToProjectionJittered * prevWorldToView;
      shaderCamera.prevProjectionToViewJittered = inverse(prevViewToProjectionJittered);
      shaderCamera.projectionToPrevProjectionJittered = prevViewToProjectionJittered * prevWorldToView
        * camera.getViewToWorld(/*freecam=*/true) * camera.getProjectionToViewJittered();
    }
    std::memcpy(&inputs.camera, &shaderCamera, sizeof(Camera));
    std::memcpy(inputs.viewToWorld, &inputs.camera.viewToWorld, sizeof(inputs.viewToWorld));
    std::memcpy(inputs.projectionToView, &inputs.camera.projectionToView, sizeof(inputs.projectionToView));
    inputs.cameraPosition = toNumos(camera.getPosition(/*freecam=*/true));
    inputs.cameraPositionGame = toNumos(camera.getPosition(/*freecam=*/false));
    inputs.cameraForward = toNumos(normalize(camera.getDirection(/*freecam=*/true)));

    inputs.zUp = RtxOptions::zUp();
    inputs.hostUnitsPerMeter = RtxOptions::getMeterToWorldUnitScale();
    inputs.hasWorldOrigin = false;

    const VkExtent3D& renderExtent = resources.getDownscaleDimensions();
    inputs.renderWidth = renderExtent.width;
    inputs.renderHeight = renderExtent.height;
    const VkExtent3D& outputExtent = resources.getTargetDimensions();
    inputs.outputWidth = outputExtent.width;
    inputs.outputHeight = outputExtent.height;

    inputs.opaqueTlas = impl.gpu.setExternalAccel(resources.getTLAS(Tlas::Opaque).accelStructure);
    inputs.blueNoise = impl.gpu.setExternalView(kBlueNoiseView, resources.getBlueNoiseTexture(dxvkCtx));

    gatherLocalLights(ctx);
    inputs.localLights = impl.localLights.data();
    inputs.localLightCount = static_cast<uint32_t>(impl.localLights.size());
    inputs.hostSkyOverride = m_hostSkyOverride;
    inputs.volumetricsHandoffWorldUnits = 0.0f;
    inputs.hostLocalLightInjection = false;
    inputs.debugViewActive = ctx.getCommonObjects()->metaDebugView().getDebugViewIndex() != DEBUG_VIEW_DISABLED;

    impl.renderer.beginFrame(impl.gpu, inputs);
    ++m_frameIndex;

    const numos::FrameOutputs& out = impl.renderer.outputs();
    ctx.writeToBuffer(m_argsBuffer, 0, sizeof(numos::AtmosphereArgs), &out.args);

    impl.emittedRadianceScale = out.radianceScale > 0.0f ? out.radianceScale : 1.0f;
    std::copy(std::begin(out.lights), std::end(out.lights), std::begin(impl.emittedLights));

    constants.skyRadianceUnscale = 1.0f / impl.emittedRadianceScale;
    constants.skyProviderActive = true;
    m_rendering = true;
  }

  void RtxNumos::dispatchPostDepth(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    if (!m_rendering) {
      return;
    }

    ScopedGpuProfileZone(&ctx, "NUMOS Clouds");

    const auto stateLock = lockState();
    Impl& impl = *m_impl;
    const numos::GpuHandle linearViewZ = impl.gpu.setExternalView(kLinearViewZView, rtOutput.m_primaryLinearViewZ.view);
    impl.renderer.dispatchPostDepth(impl.gpu, linearViewZ, rtOutput.m_raytraceArgs.primaryDirectMissLinearViewZ);
    m_postDepthDone = true;

    // NUMOS updates its args after the cloud pass, and the composite reads those.
    ctx.writeToBuffer(m_argsBuffer, 0, sizeof(numos::AtmosphereArgs), &impl.renderer.outputs().args);
    impl.gpu.endFrame();
  }

  void RtxNumos::bindCommonResources(RtxContext& ctx) {
    if (!m_rendering && !m_commonResourcesBound) {
      return;
    }
    m_commonResourcesBound = m_rendering;

    static const numos::FrameOutputs kNoOutputs {};
    const numos::FrameOutputs& out = m_rendering ? m_impl->renderer.outputs() : kNoOutputs;
    const OutputBinder binder(ctx, m_rendering ? &m_impl->gpu : nullptr);

    ctx.bindResourceBuffer(BINDING_SKY_ARGS,
                           m_rendering ? DxvkBufferSlice(m_argsBuffer, 0, sizeof(numos::AtmosphereArgs)) : DxvkBufferSlice());
    binder.bindView(BINDING_SKY_TRANSMITTANCE_LUT, out.transmittanceLut);
    binder.bindView(BINDING_SKY_MULTISCATTERING_LUT, out.multiscatteringLut);
    binder.bindView(BINDING_SKY_VIEW_LUT, out.skyViewLut);
    binder.bindSampler(BINDING_SKY_VIEW_SAMPLER, out.skyViewSampler);
    binder.bindView(BINDING_SKY_CLOUD_TRANSMITTANCE_LUT, out.cloudSkyTransmittanceLut);
    binder.bindView(BINDING_SKY_CLOUD_SHADOW_MAP, out.cloudShadowMap);
    binder.bindSampler(BINDING_SKY_CLOUD_NOISE_SAMPLER, out.cloudNoiseSampler);
    binder.bindView(BINDING_SKY_CLOUD_SECONDARY_LUT, out.cloudSecondaryLut);
    binder.bindBuffer(BINDING_SKY_STAR_CELLS, out.nightSkyStarCells);
    binder.bindBuffer(BINDING_SKY_STARS, out.nightSkyStars);
    binder.bindBuffer(BINDING_SKY_MILKY_WAY, out.nightSkyMilkyWay);
    binder.bindView(BINDING_SKY_STARLIGHT_LOW, out.nightSkyStarlightLow);
    binder.bindView(BINDING_SKY_MOON, out.nightSkyMoon);
  }

  void RtxNumos::bindCompositeResources(RtxContext& ctx, CompositeArgs& args) {
    if (!m_rendering) {
      return;
    }

    const numos::FrameOutputs& out = m_impl->renderer.outputs();
    const NumosGpu& gpu = m_impl->gpu;

    args.skyProviderActive = true;
    args.skyRadianceScale = out.radianceScale > 0.0f ? out.radianceScale : 1.0f;
    args.skyHostOverride = m_hostSkyOverride;
    args.skyHazeActive = m_hazeActive;
    args.skyCloudsValid = m_postDepthDone && out.args.cloudRenderRTEnable &&
                          gpu.getView(out.cloudRender) != nullptr && gpu.getView(out.cloudDepth) != nullptr;
    args.cloudMotionVectors = cloudMotionVectors();
    args.cloudMotionVectorCoverage = cloudMotionVectorCoverage();
    args.skyWriteBiasMask = writesUpscalerBiasMask();

    const OutputBinder binder(ctx, &gpu);
    ctx.bindResourceBuffer(COMPOSITE_SKY_ARGS_INPUT, DxvkBufferSlice(m_argsBuffer, 0, sizeof(numos::AtmosphereArgs)));
    binder.bindView(COMPOSITE_SKY_CLOUD_RENDER_INPUT, out.cloudRender);
    binder.bindView(COMPOSITE_SKY_CLOUD_DEPTH_INPUT, out.cloudDepth);
    binder.bindView(COMPOSITE_SKY_HAZE_NEAR_SCATTER_INPUT, out.apNearScatter);
    binder.bindView(COMPOSITE_SKY_HAZE_FAR_SCATTER_INPUT, out.apFarScatter);
    binder.bindView(COMPOSITE_SKY_HAZE_FAR_TRANSMITTANCE_INPUT, out.apFarTransmittance);
    binder.bindView(COMPOSITE_SKY_HAZE_FROXEL_AUX_INPUT, out.apFroxelAux);
    binder.bindBuffer(COMPOSITE_SKY_HAZE_LIGHTS_INPUT, out.apLights);
    binder.bindBuffer(COMPOSITE_SKY_HAZE_LIGHT_CLUSTERS_INPUT, out.apLightClusters);
    binder.bindSampler(COMPOSITE_SKY_HAZE_SAMPLER_INPUT, out.linearClampSampler);
    binder.bindView(COMPOSITE_SKY_TRANSMITTANCE_LUT_INPUT, out.transmittanceLut);
    binder.bindView(COMPOSITE_SKY_MULTISCATTERING_LUT_INPUT, out.multiscatteringLut);
    binder.bindView(COMPOSITE_SKY_VIEW_LUT_INPUT, out.skyViewLut);
    binder.bindSampler(COMPOSITE_SKY_VIEW_SAMPLER_INPUT, out.skyViewSampler);
    binder.bindView(COMPOSITE_SKY_CLOUD_TRANSMITTANCE_LUT_INPUT, out.cloudSkyTransmittanceLut);
    binder.bindView(COMPOSITE_SKY_CLOUD_SHADOW_MAP_INPUT, out.cloudShadowMap);
    binder.bindSampler(COMPOSITE_SKY_CLOUD_NOISE_SAMPLER_INPUT, out.cloudNoiseSampler);
    binder.bindView(COMPOSITE_SKY_CLOUD_SECONDARY_LUT_INPUT, out.cloudSecondaryLut);
    binder.bindBuffer(COMPOSITE_SKY_STAR_CELLS_INPUT, out.nightSkyStarCells);
    binder.bindBuffer(COMPOSITE_SKY_STARS_INPUT, out.nightSkyStars);
    binder.bindBuffer(COMPOSITE_SKY_MILKY_WAY_INPUT, out.nightSkyMilkyWay);
    binder.bindView(COMPOSITE_SKY_STARLIGHT_LOW_INPUT, out.nightSkyStarlightLow);
    binder.bindView(COMPOSITE_SKY_MOON_INPUT, out.nightSkyMoon);
  }

}
