/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_particle_system.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h" 
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

#include "../util/util_globaltime.h"

#include <rtx_shaders/particle_system_evolve.h>
#include "math.h"

namespace dxvk { 

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class ParticleSystemEvolve : public ManagedShader {
      SHADER_SOURCE(ParticleSystemEvolve, VK_SHADER_STAGE_COMPUTE_BIT, particle_system_evolve)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        CONSTANT_BUFFER(PARTICLE_SYSTEM_BINDING_CONSTANTS)

        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT)
        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT)

        TEXTURE2D(PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT)

        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT)
      END_PARAMETER()
    };
  }

  RtxParticleSystemManager::RtxParticleSystemManager(DxvkDevice* device) 
    : CommonDeviceObject(device) {
  }

  void RtxParticleSystemManager::showImguiSettings() {
    if (ImGui::CollapsingHeader("Particle System", ImGuiTreeNodeFlags_CollapsingHeader)) {
      ImGui::PushID("rtx_particles");
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      ImGui::Checkbox("Enable", &enableObject());
      ImGui::BeginDisabled(!enable());
      ImGui::DragFloat("Time Scale", &timeScaleObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

      if (ImGui::CollapsingHeader("Global Preset", ImGuiTreeNodeFlags_CollapsingHeader)) {
        ImGui::TextWrapped("The following settings will be applied to all particle systems created using the texture tagging mechanism.  Particle systems created via USD assets are not affected by these.");
        ImGui::Separator();

        ImGui::DragInt("Number of Particles Per Material", &numberOfParticlesPerMaterialObject(), 0.1f, 1, 10000000, "%d", ImGuiSliderFlags_AlwaysClamp);

        const auto colourPickerOpts = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_DisplayRGB;
        if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::DragInt("Spawn Rate Per Second", &spawnRatePerSecondObject(), 0.1f, 1, 10000, "%d", ImGuiSliderFlags_AlwaysClamp);
          ImGui::Separator();
          ImGui::Checkbox("Use Spawn Texture Coordinates", &useSpawnTexcoordsObject());
          ImGui::Separator();
          ImGui::DragFloat("Initial Velocity From Normal", &initialVelocityFromNormalObject(), 0.01f, -500.f, 500.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloat("Initial Velocity Cone Angle", &initialVelocityConeAngleDegreesObject(), 0.01f, -500.f, 500.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::Separator();
          ImGui::DragFloatRange("Time to Live Range", { &minParticleLifeObject(), &maxParticleLifeObject() }, 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::Separator();
          ImGui::DragFloatRange("Particle Size Range", { &minSpawnSizeObject(), &maxSpawnSizeObject() }, 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloatRange("Particle Rotation Speed Range", { &minSpawnRotationSpeedObject(), &maxSpawnRotationSpeedObject() }, 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::ColorPicker4("Minimum Color Tint", &minSpawnColorObject(), colourPickerOpts);
          ImGui::ColorPicker4("Maximum Color Tint", &maxSpawnColorObject(), colourPickerOpts);
        }

        if (ImGui::CollapsingHeader("Target", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::DragFloatRange("Particle Size Range", { &minTargetSizeObject(), &maxTargetSizeObject() }, 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloatRange("Particle Rotation Speed Range", { &minTargetRotationSpeedObject(), &maxTargetRotationSpeedObject() }, 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::ColorPicker4("Minimum Color Tint", &minTargetColorObject(), colourPickerOpts);
          ImGui::ColorPicker4("Maximum Color Tint", &maxTargetColorObject(), colourPickerOpts);
        }

        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::DragFloat("Gravity Force", &gravityForceObject(), 0.01f, -100.f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloat("Max Speed", &maxSpeedObject(), 0.01f, 0.f, 100000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

          ImGui::Checkbox("Align Particles with Velocity", &alignParticlesToVelocityObject());
          ImGui::Checkbox("Enable Motion Trail", &enableMotionTrailObject());
          ImGui::BeginDisabled(!enableMotionTrail());
          ImGui::DragFloat("Motion Trail Length Multiplier", &motionTrailMultiplierObject(), 0.01f, 0.001f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::EndDisabled();

          ImGui::Checkbox("Enable Particle World Collisions", &enableCollisionDetectionObject());
          ImGui::BeginDisabled(!enableCollisionDetection());
          ImGui::DragFloat("Collision Restitution", &collisionRestitutionObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloat("Collision Thickness", &collisionThicknessObject(), 0.01f, 0.f, 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::EndDisabled();

          ImGui::Checkbox("Simulate Turbulence", &useTurbulenceObject());
          ImGui::BeginDisabled(!useTurbulence());
          ImGui::DragFloat("Turbulence Amplitude", &turbulenceAmplitudeObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::DragFloat("Turbulence Frequency", &turbulenceFrequencyObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::EndDisabled();
        }
      }
      ImGui::Unindent();
      ImGui::EndDisabled();
      ImGui::PopID();
    }
  }

  void RtxParticleSystemManager::setupConstants(RtxContext* ctx, ParticleSystemConstants& constants) {
    ScopedCpuProfileZone();
    const RtCamera& camera = ctx->getSceneManager().getCamera();
    constants.worldToView = camera.getWorldToView();
    constants.viewToWorld = camera.getViewToWorld();
    constants.prevWorldToProjection = camera.getPreviousViewToProjection() * camera.getPreviousWorldToView();

    constants.renderingWidth = ctx->getSceneManager().getCamera().m_renderResolution[0];
    constants.renderingHeight = ctx->getSceneManager().getCamera().m_renderResolution[1];

    constants.frameIdx = device()->getCurrentFrameId();

    constants.upDirection.x = ctx->getSceneManager().getSceneUp().x;
    constants.upDirection.y = ctx->getSceneManager().getSceneUp().y;
    constants.upDirection.z = ctx->getSceneManager().getSceneUp().z;
    constants.deltaTimeSecs = GlobalTime::get().deltaTime() * timeScale();
    constants.invDeltaTimeSecs = 1.f / constants.deltaTimeSecs;
    constants.absoluteTimeSecs = GlobalTime::get().absoluteTimeMs() * 0.001f * timeScale();
  }

  // Please re-profile performance if any of these structures change in size.  As a minimum performance requirement, always preserve a 16 byte alignment.
  static_assert(sizeof(GpuParticle) == 12 * 4, "Unexpected, please check perf");
  static_assert(sizeof(RtxParticleSystemDesc) % (4 * 4) == 0, "Unexpected, please check perf");

  RtxParticleSystemDesc RtxParticleSystemManager::createGlobalParticleSystemDesc() {
    RtxParticleSystemDesc desc;
    desc.initialVelocityFromNormal = RtxParticleSystemManager::initialVelocityFromNormal();
    desc.initialVelocityConeAngleDegrees = RtxParticleSystemManager::initialVelocityConeAngleDegrees();
    desc.alignParticlesToVelocity = RtxParticleSystemManager::alignParticlesToVelocity();
    desc.gravityForce = RtxParticleSystemManager::gravityForce();
    desc.maxSpeed = RtxParticleSystemManager::maxSpeed();
    desc.useTurbulence = RtxParticleSystemManager::useTurbulence() ? 1 : 0;
    desc.turbulenceFrequency = RtxParticleSystemManager::turbulenceFrequency();
    desc.turbulenceAmplitude = RtxParticleSystemManager::turbulenceAmplitude();
    desc.minTtl = RtxParticleSystemManager::minParticleLife();
    desc.maxTtl = RtxParticleSystemManager::maxParticleLife();
    desc.minSpawnSize = RtxParticleSystemManager::minSpawnSize();
    desc.maxSpawnSize = RtxParticleSystemManager::maxSpawnSize();
    desc.maxNumParticles = RtxParticleSystemManager::numberOfParticlesPerMaterial();
    desc.minSpawnColor = RtxParticleSystemManager::minSpawnColor();
    desc.maxSpawnColor = RtxParticleSystemManager::maxSpawnColor();
    desc.minSpawnRotationSpeed = RtxParticleSystemManager::minSpawnRotationSpeed();
    desc.maxSpawnRotationSpeed = RtxParticleSystemManager::maxSpawnRotationSpeed();
    desc.useSpawnTexcoords = RtxParticleSystemManager::useSpawnTexcoords() ? 1 : 0;
    desc.enableCollisionDetection = RtxParticleSystemManager::enableCollisionDetection() ? 1 : 0;
    desc.alignParticlesToVelocity = RtxParticleSystemManager::alignParticlesToVelocity() ? 1 : 0;
    desc.collisionRestitution = RtxParticleSystemManager::collisionRestitution();
    desc.collisionThickness = RtxParticleSystemManager::collisionThickness();
    desc.enableMotionTrail = RtxParticleSystemManager::enableMotionTrail() ? 1 : 0;
    desc.motionTrailMultiplier = RtxParticleSystemManager::motionTrailMultiplier();
    desc.spawnRate = (float)RtxParticleSystemManager::spawnRatePerSecond();
    desc.minTargetSize = RtxParticleSystemManager::minTargetSize();
    desc.maxTargetSize = RtxParticleSystemManager::maxTargetSize();
    desc.minTargetRotationSpeed = RtxParticleSystemManager::minTargetRotationSpeed();
    desc.maxTargetRotationSpeed = RtxParticleSystemManager::maxTargetRotationSpeed();
    desc.minTargetColor = RtxParticleSystemManager::minTargetColor();
    desc.maxTargetColor = RtxParticleSystemManager::maxTargetColor();
    desc.hideEmitter = 0;
    return desc;
  }

  bool RtxParticleSystemManager::fetchParticleSystem(DxvkContext* ctx, const DrawCallState& drawCallState, const RtxParticleSystemDesc& desc, const MaterialData& renderMaterialData, ParticleSystem** materialSystem) {
    ScopedCpuProfileZone();
    if(desc.maxNumParticles == 0) {
      return false;
    }
    
    const XXH64_hash_t particleSystemHash = drawCallState.getMaterialData().getHash() ^ desc.calcHash();

    auto& materialSystemIt = m_particleSystems.find(particleSystemHash);
    if (materialSystemIt == m_particleSystems.end()) {
      // Strip out any custom particle defined in the target material to avoid creating duplicated, nested systems.
      MaterialData particleRenderMaterial(renderMaterialData);
      particleRenderMaterial.m_particleSystem = std::nullopt;
      auto pNewParticleSystem = std::make_shared<ParticleSystem>(desc, particleRenderMaterial, drawCallState.getMaterialData(), drawCallState.getCategoryFlags(), m_particleSystemCounter++);
      pNewParticleSystem->allocStaticBuffers(ctx);
      auto insertResult = m_particleSystems.insert({ particleSystemHash, pNewParticleSystem });
      materialSystemIt = insertResult.first;
    }

    (*materialSystem) = materialSystemIt->second.get();

    return true;
  }

  uint32_t RtxParticleSystemManager::getNumberOfParticlesToSpawn(ParticleSystem* particleSystem, const DrawCallState& drawCallState) {
    ScopedCpuProfileZone();

    float lambda = particleSystem->context.desc.spawnRate * GlobalTime::get().deltaTime();

    // poisson dist wont work well with these values (inf loop)
    if (isnan(lambda) || lambda < 0.0f) {
      return 0;
    }

    std::poisson_distribution<uint32_t> distribution(lambda);
    uint32_t numParticles = std::min(distribution(particleSystem->generator), particleSystem->context.desc.maxNumParticles);

    if (particleSystem->context.spawnParticleCount + numParticles >= particleSystem->context.desc.maxNumParticles) {
      return 0; 
    }

    return numParticles;
  }

  void RtxParticleSystemManager::spawnParticles(DxvkContext* ctx, const RtxParticleSystemDesc& desc, const uint32_t instanceId, const DrawCallState& drawCallState, const MaterialData& renderMaterialData) {
    ScopedCpuProfileZone();
    if (!enable()) {
      return;
    }

    m_initialized = true;

    ParticleSystem* particleSystem = nullptr;
    if (!fetchParticleSystem(ctx, drawCallState, desc, renderMaterialData, &particleSystem)) {
      return;
    }

    const uint32_t numParticles = getNumberOfParticlesToSpawn(particleSystem, drawCallState);
    if (numParticles == 0) {
      return;
    }

    // First check for wrap around
    if ((particleSystem->particleWriteOffset + numParticles) >= particleSystem->context.desc.maxNumParticles) {
      particleSystem->particleWriteOffset = 0;
    }

    // Register the spawn context data
    SpawnContext spawnCtx;
    spawnCtx.numberOfParticles = numParticles;
    spawnCtx.particleOffset = particleSystem->particleWriteOffset;
    spawnCtx.instanceId = instanceId;
    spawnCtx.particleSystemHash = particleSystem->getHash();

    // Update material specific counters
    particleSystem->particleWriteOffset += numParticles;
    particleSystem->context.spawnParticleCount += numParticles;

    // Map the particles to a context for spawn
    particleSystem->spawnContextParticleMap.insert(particleSystem->spawnContextParticleMap.end(), spawnCtx.numberOfParticles, m_spawnContexts.size());

    // Mark the time 
    particleSystem->lastSpawnTimeMs = GlobalTime::get().absoluteTimeMs();

    // Track this spawn context by copying off
    m_spawnContexts.emplace_back(std::move(spawnCtx));
  }

  void RtxParticleSystemManager::simulate(RtxContext* ctx) {
    if (!enable() || !m_initialized) {
      m_spawnContexts.clear();
      return;
    }

    ScopedGpuProfileZone(ctx, "Rtx Particle Simulation");

    allocStaticBuffers(ctx);

    // If we have particles to simulate...
    if (m_particleSystems.size()) {
      writeSpawnContextsToGpu(ctx);

      ParticleSystemConstants constants;
      setupConstants(ctx, constants);

      ctx->bindResourceView(BINDING_VALUE_NOISE_SAMPLER, ctx->getResourceManager().getValueNoiseLut(ctx), nullptr);
      Rc<DxvkSampler> valueNoiseSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
      ctx->bindResourceSampler(BINDING_VALUE_NOISE_SAMPLER, valueNoiseSampler);


      ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT, DxvkBufferSlice(m_spawnContextsBuffer));


      const auto& rtOutput = ctx->getResourceManager().getRaytracingOutput();
      ctx->bindResourceView(PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT, 
                            rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read, 
                                                                                               rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().matchesWriteFrameIdx(constants.frameIdx - 1)), nullptr);

      for (auto& system : m_particleSystems) {
        // Update CB
        constants.particleSystem = system.second->context;
        const DxvkBufferSliceHandle cSlice = m_cb->allocSlice(); 
        ctx->invalidateBuffer(m_cb, cSlice);
        ctx->writeToBuffer(m_cb, 0, sizeof(ParticleSystemConstants), &constants);
        ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_CONSTANTS, DxvkBufferSlice(m_cb));

        ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT, DxvkBufferSlice(system.second->getSpawnContextMappingBuffer()));
        ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT, DxvkBufferSlice(system.second->getParticlesBuffer()));
        ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT, DxvkBufferSlice(system.second->getVertexBuffer()));

        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ParticleSystemEvolve::getShader());

        const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { (uint32_t) system.second->context.desc.maxNumParticles, 1, 1 }, VkExtent3D { 128, 1, 1 });
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }
    }

    prepareForNextFrame();
  }

  void RtxParticleSystemManager::writeSpawnContextsToGpu(RtxContext* ctx) {
    if (!m_spawnContexts.size()) {
      return;
    }

    // Align the data
    std::vector<GpuSpawnContext> gpuSpawnContexts(m_spawnContexts.size());
    for (auto& spawnCtxIt = m_spawnContexts.begin(); spawnCtxIt != m_spawnContexts.end(); spawnCtxIt++) {
      const SpawnContext& spawnCtx = *spawnCtxIt;
      const uint32_t contextIdx = spawnCtxIt - m_spawnContexts.begin();
      GpuSpawnContext& gpuCtx = gpuSpawnContexts[contextIdx];

      const RtInstance* pTargetInstance = spawnCtx.instanceId < ctx->getSceneManager().getInstanceTable().size() ? ctx->getSceneManager().getInstanceTable()[spawnCtx.instanceId] : nullptr;

      if (pTargetInstance == nullptr) {
        // I dont see this case being hit, but in theory it could happen since we track the 
        //   instance ID at draw time, and the instance list can change over the course of a frame.
        //   In the event it does happen, handle gracefully...dw
        memset(&gpuCtx, 0, sizeof(GpuSpawnContext));
        // zero out the spawn count, so we dont try to create any new particles here
        auto& particleSystemIt = m_particleSystems.find(spawnCtx.particleSystemHash);
        if (particleSystemIt != m_particleSystems.end()) {
          particleSystemIt->second->context.spawnParticleCount = 0;
        }
        continue;
      }

      gpuCtx.spawnObjectToWorld = pTargetInstance->getTransform();

      gpuCtx.indices32bit = pTargetInstance->getBlas()->modifiedGeometryData.indexBuffer.indexType() == VK_INDEX_TYPE_UINT32 ? 1 : 0;
      gpuCtx.numTriangles = pTargetInstance->getBlas()->modifiedGeometryData.indexCount / 3;
      gpuCtx.spawnMeshIndexIdx = pTargetInstance->surface.indexBufferIndex;
      gpuCtx.spawnMeshPositionsIdx = pTargetInstance->surface.positionBufferIndex;

      gpuCtx.spawnMeshColorsIdx = pTargetInstance->surface.color0BufferIndex;
      gpuCtx.spawnMeshTexcoordsIdx = pTargetInstance->surface.texcoordBufferIndex;
      gpuCtx.spawnMeshPositionsOffset = pTargetInstance->surface.positionOffset;
      gpuCtx.spawnMeshPositionsStride = pTargetInstance->surface.positionStride;

      gpuCtx.spawnMeshColorsOffset = pTargetInstance->surface.color0Offset;
      gpuCtx.spawnMeshColorsStride = pTargetInstance->surface.color0Stride;
      gpuCtx.spawnMeshTexcoordsOffset = pTargetInstance->surface.texcoordOffset;
      gpuCtx.spawnMeshTexcoordsStride = pTargetInstance->surface.texcoordStride;
    }

    // Send data to GPU

    ctx->writeToBuffer(m_spawnContextsBuffer, 0, gpuSpawnContexts.size() * sizeof(GpuSpawnContext), gpuSpawnContexts.data());

    for (const auto& keyPair : m_particleSystems) {
      const ParticleSystem& particleSystem = *(keyPair.second.get());
      if (particleSystem.spawnContextParticleMap.size() == 0) {
        assert(particleSystem.context.spawnParticleCount == 0);
        continue;
      }

      const std::vector<uint16_t>& particleSpawnMap = particleSystem.spawnContextParticleMap;
      ctx->writeToBuffer(particleSystem.getSpawnContextMappingBuffer(), 0, particleSpawnMap.size() * sizeof(uint16_t), particleSpawnMap.data());
    }
  }

  void RtxParticleSystemManager::submitDrawState(RtxContext* ctx) const {
    ScopedCpuProfileZone();
    if (!enable() || !m_initialized) {
      return;
    }

    for (const auto& keyPair : m_particleSystems) {
      const ParticleSystem& particleSystem = *(keyPair.second.get());

      // Here we create a fake draw call, and send it through the regular scene manager pipeline
      //   which has the advantage of supporting replacement materials.

      const DxvkBufferSlice& vertexSlice = DxvkBufferSlice(particleSystem.getVertexBuffer());
      const DxvkBufferSlice& indexSlice = DxvkBufferSlice(particleSystem.getIndexBuffer());

      // This is used to uniquely hash particle system geometry data - we do this because the particle data is hashed differently from regular D3D9 geometry.
      constexpr XXH64_hash_t particleHashConstant = 0xba171c1e;

      RasterGeometry particleGeometry;
      particleGeometry.indexBuffer = RasterBuffer(indexSlice, 0, sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
      particleGeometry.indexCount = particleSystem.getIndexCount();
      particleGeometry.vertexCount = particleSystem.getVertexCount();
      particleGeometry.positionBuffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, position), sizeof(ParticleVertex), VK_FORMAT_R32G32B32_SFLOAT);
      particleGeometry.color0Buffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, color), sizeof(ParticleVertex), VK_FORMAT_B8G8R8A8_UNORM);
      particleGeometry.texcoordBuffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, texcoord), sizeof(ParticleVertex), VK_FORMAT_R32G32_SFLOAT);
      particleGeometry.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      particleGeometry.cullMode = VK_CULL_MODE_NONE;
      particleGeometry.frontFace = VK_FRONT_FACE_CLOCKWISE;
      particleGeometry.hashes[HashComponents::Indices] = particleHashConstant ^ particleSystem.getHash();
      particleGeometry.hashes[HashComponents::VertexPosition] = particleHashConstant ^ particleSystem.getGeneration() ^ particleSystem.getHash();
      particleGeometry.hashes.precombine();

      const RtCamera& camera = ctx->getSceneManager().getCamera();

      DrawCallState newDrawCallState;
      newDrawCallState.geometryData = particleGeometry; // Note: Geometry Data replaced
      newDrawCallState.categories = particleSystem.categories;
      newDrawCallState.categories.set(InstanceCategories::Particle); // ?
      newDrawCallState.categories.clr(InstanceCategories::ParticleEmitter);
      newDrawCallState.categories.clr(InstanceCategories::Hidden);
      newDrawCallState.transformData.viewToProjection = camera.getViewToProjection();
      newDrawCallState.transformData.worldToView = camera.getWorldToView();
      newDrawCallState.materialData = particleSystem.legacyMaterialData;

      // We want to always have particles support vertex colour for now.
      newDrawCallState.materialData.textureColorArg2Source = RtTextureArgSource::VertexColor0;

      ctx->getSceneManager().submitDrawState(ctx, newDrawCallState, &particleSystem.materialData);
    }
  }

  RtxParticleSystemManager::ParticleSystem::ParticleSystem(const RtxParticleSystemDesc& desc, const MaterialData& matData, const LegacyMaterialData& legacyMatData, const CategoryFlags& cats, const uint32_t seed) : context(desc)
    , materialData(matData)
    , legacyMaterialData(legacyMatData)
    , categories(cats) {
    // Seed the RNG with a parameter from the manager, so we get unique random values for each particle system
    generator = std::default_random_engine(seed);
    // Store this hash since it cannot change now.
    // NOTE: This material data hash is stable within a run, but since hash depends on VK handles, it is not reliable across runs.
    m_cachedHash = materialData.getHash() ^ context.desc.calcHash();
    context.numVerticesPerParticle = getVerticesPerParticle();

    // classic square billboard
    static const float2 offsets[4] = {
        float2(-0.5f,  0.5f),
        float2(0.5f,  0.5f),
        float2(-0.5f, -0.5f),
        float2(0.5f, -0.5f)
    };

    // motion trail - first 4 are "head", last 4 are "tail"
    static const float2 offsetsMotionTrail[8] = {
      // TAIL quad (fixed)
      float2(-0.5f, -0.5f),
      float2(-0.5f,  0.0f),
      float2( 0.5f, -0.5f),
      float2( 0.5f,  0.0f),

      // HEAD quad (stretched)
      float2(-0.5f, 0.0f),
      float2(-0.5f, 0.5f),
      float2( 0.5f, 0.0f),
      float2( 0.5f, 0.5f)
    };

    if (desc.enableMotionTrail) {
      memcpy(&context.particleVertexOffsets[0], &offsetsMotionTrail[0], sizeof(offsetsMotionTrail));
    } else {
      memcpy(&context.particleVertexOffsets[0], &offsets[0], sizeof(offsets));
    }
  }

  void RtxParticleSystemManager::ParticleSystem::allocStaticBuffers(DxvkContext* ctx) {
    ScopedCpuProfileZone();

    // Handle the reallocation of all GPU and CPU data structures.  

    if (m_particles == nullptr || m_particles->info().size != sizeof(GpuParticle) * context.desc.maxNumParticles) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(GpuParticle) * context.desc.maxNumParticles;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT
        | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_SHADER_READ_BIT;
      m_particles = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - State Buffer");

      ctx->clearBuffer(m_particles, 0, info.size, 0);
    }

    if (m_vb == nullptr || m_vb->info().size != sizeof(ParticleVertex) * getVertexCount()) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(ParticleVertex) * getVertexCount();
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT
        | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_SHADER_READ_BIT;
      m_vb = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Vertex Buffer");

      ctx->clearBuffer(m_vb, 0, info.size, 0);
    }

    if (m_ib == nullptr || m_ib->info().size != sizeof(uint32_t) * getIndexCount()) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(uint32_t) * getIndexCount();
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                 | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                 | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                 | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
      m_ib = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Index Buffer");

      std::vector<uint32_t> indices(getIndexCount());
      for (int i = 0; i < context.desc.maxNumParticles; i++) {
        indices[i * getIndicesPerParticle() + 0] = i * getVerticesPerParticle() + 0;
        indices[i * getIndicesPerParticle() + 1] = i * getVerticesPerParticle() + 1;
        indices[i * getIndicesPerParticle() + 2] = i * getVerticesPerParticle() + 2;

        indices[i * getIndicesPerParticle() + 3] = i * getVerticesPerParticle() + 2;
        indices[i * getIndicesPerParticle() + 4] = i * getVerticesPerParticle() + 1;
        indices[i * getIndicesPerParticle() + 5] = i * getVerticesPerParticle() + 3;
      }

      if (context.desc.enableMotionTrail) {
        for (int i = 0; i < context.desc.maxNumParticles; i++) {
          indices[i * getIndicesPerParticle() + 6]  = i * getVerticesPerParticle() + 1;
          indices[i * getIndicesPerParticle() + 7]  = i * getVerticesPerParticle() + 4;
          indices[i * getIndicesPerParticle() + 8]  = i * getVerticesPerParticle() + 3;

          indices[i * getIndicesPerParticle() + 9]  = i * getVerticesPerParticle() + 3;
          indices[i * getIndicesPerParticle() + 10] = i * getVerticesPerParticle() + 4;
          indices[i * getIndicesPerParticle() + 11] = i * getVerticesPerParticle() + 6;

          indices[i * getIndicesPerParticle() + 12] = i * getVerticesPerParticle() + 4;
          indices[i * getIndicesPerParticle() + 13] = i * getVerticesPerParticle() + 5;
          indices[i * getIndicesPerParticle() + 14] = i * getVerticesPerParticle() + 6;

          indices[i * getIndicesPerParticle() + 15] = i * getVerticesPerParticle() + 6;
          indices[i * getIndicesPerParticle() + 16] = i * getVerticesPerParticle() + 5;
          indices[i * getIndicesPerParticle() + 17] = i * getVerticesPerParticle() + 7;
        }
      }

      ctx->updateBuffer(m_ib, 0, info.size, indices.data());
    }

    if (m_spawnContextParticleMapBuffer == nullptr || m_spawnContextParticleMapBuffer->info().size != sizeof(uint16_t) * context.desc.maxNumParticles) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(uint16_t) * context.desc.maxNumParticles;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
      m_spawnContextParticleMapBuffer = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Spawn Context Map Buffer");
      ctx->clearBuffer(m_spawnContextParticleMapBuffer, 0, info.size, 0);
    }
  }

  void RtxParticleSystemManager::allocStaticBuffers(DxvkContext* ctx) {
    ScopedCpuProfileZone();

    if (m_cb.ptr() == nullptr) {
      const Rc<DxvkDevice>& device = ctx->getDevice();
      DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size = sizeof(ParticleSystemConstants);
      m_cb = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Constant Buffer");
    }

    if (m_spawnContextsBuffer == nullptr || m_spawnContextsBuffer->info().size < sizeof(GpuSpawnContext) * std::max(100u, (uint32_t) m_spawnContexts.size())) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(GpuSpawnContext) * std::max(100u, (uint32_t) m_spawnContexts.size());
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT;
      m_spawnContextsBuffer = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Spawn Context Buffer");
    }
  }

  void RtxParticleSystemManager::prepareForNextFrame() {
    // Spawn contexts dont persist across frames, this is because we want to support objects with unstable hashes.
    m_spawnContexts.clear();

    // Signals which version of the vertex data we are on due to simulation

    // Update material systems including unregistering systems that have no particles remaining
    for (auto keyPairIt = m_particleSystems.begin(); keyPairIt != m_particleSystems.end();) {
      ParticleSystem& particleSystem = *keyPairIt->second.get();

      auto now = GlobalTime::get().absoluteTimeMs();
      if ((particleSystem.lastSpawnTimeMs + (uint64_t)(maxParticleLife() * 1000)) < now) {
        keyPairIt = m_particleSystems.erase(keyPairIt);
        continue;
      }

      ++particleSystem.generationIdx;

      particleSystem.context.spawnParticleOffset = particleSystem.particleWriteOffset;
      particleSystem.context.spawnParticleCount = 0;
      particleSystem.spawnContextParticleMap.clear();

      ++keyPairIt;
    }
  }

} 
