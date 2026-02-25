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

#include <rtx_shaders/particle_system_spawn.h>
#include <rtx_shaders/particle_system_evolve.h>
#include <rtx_shaders/particle_system_generate_geometry.h>
#include "math.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class ParticleSystemSpawn : public ManagedShader {
      SHADER_SOURCE(ParticleSystemSpawn, VK_SHADER_STAGE_COMPUTE_BIT, particle_system_spawn)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        CONSTANT_BUFFER(PARTICLE_SYSTEM_BINDING_CONSTANTS)

        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT)
        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT)
        SAMPLER2D(PARTICLE_SYSTEM_BINDING_ANIMATION_DATA_INPUT)

        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
        END_PARAMETER()
    };

    class ParticleSystemEvolve : public ManagedShader {
      SHADER_SOURCE(ParticleSystemEvolve, VK_SHADER_STAGE_COMPUTE_BIT, particle_system_evolve)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        CONSTANT_BUFFER(PARTICLE_SYSTEM_BINDING_CONSTANTS)

        TEXTURE2D(PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT)
        TEXTURE2D(PARTICLE_SYSTEM_BINDING_PREV_PRIMARY_SCREEN_SPACE_MOTION_INPUT)
        SAMPLER2D(PARTICLE_SYSTEM_BINDING_ANIMATION_DATA_INPUT)

        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT)
        END_PARAMETER()
    };

    class ParticleSystemGenerateGeometry : public ManagedShader {
      SHADER_SOURCE(ParticleSystemGenerateGeometry, VK_SHADER_STAGE_COMPUTE_BIT, particle_system_generate_geometry)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        CONSTANT_BUFFER(PARTICLE_SYSTEM_BINDING_CONSTANTS)

        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT)
        STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT)
        SAMPLER2D(PARTICLE_SYSTEM_BINDING_ANIMATION_DATA_INPUT)

        RW_STRUCTURED_BUFFER(PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT)
        END_PARAMETER()
    };
  }


  void RtxParticleSystemManager::maxSpeedOnChange(DxvkDevice* device) {

    auto floatToVector3 = [](const GenericValue& src, GenericValue& dest, bool destHasExistingValue) {
      if (!destHasExistingValue) {
        *dest.v3 = Vector3(src.f, src.f, src.f);
        return true;
      }
      return false;
    };

    if (maxSpeed.migrateValuesTo(&maxSpawnVelocityObject(), floatToVector3) &&
        maxSpeed.migrateValuesTo(&maxTargetVelocityObject(), floatToVector3)) {
      maxSpeed.clearFromStrongerLayers(RtxOptionLayer::getDefaultLayer());
      Logger::info("[Deprecated Config] rtx.particles.globalPreset.maxSpeed has been migrated to rtx.particles.globalPreset.maxSpawnVelocity and rtx.particles.globalPreset.maxTargetVelocity."
                   "Please re-save your config to get rid of this message.");
    } 
  }

  RtxParticleSystemManager::ConservativeCounter::ConservativeCounter(DxvkContext* ctx, const uint32_t framesInFlight, const uint32_t upperBound)
    : m_framesInFlight(framesInFlight)
    , m_upperBound(upperBound) {
    const Rc<DxvkDevice>& device = ctx->getDevice();
    DxvkBufferCreateInfo info;
    info.size = sizeof(int);
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT  | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    m_countGpu = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - ConservativeCounter Buffer");
    ctx->clearBuffer(m_countGpu, 0, info.size, 0);

    info.size *= framesInFlight;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    m_countsHost = device->createBuffer(info, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - ConservativeCounter HOST Buffer");
    memset(m_countsHost->mapPtr(0), 0, info.size);
  }

  void RtxParticleSystemManager::ConservativeCounter::postSimulation(DxvkContext* ctx, uint32_t frameIdx) {
    // for this to work correct, we must have performed the steps in preSimulation
    assert(m_frameLastCounted == frameIdx);

    // copy the GPU data into host data - will be ready at some future frame
    const uint32_t currentIdx = frameIdx % m_framesInFlight;
    ctx->copyBuffer(m_countsHost, sizeof(int) * currentIdx, m_countGpu, 0, sizeof(int));
  }

  uint32_t RtxParticleSystemManager::ConservativeCounter::preSimulation(DxvkContext* ctx, uint32_t cpuSpawns, uint32_t frameIdx) {
    // only re-read from GPU once per frame slot
    assert(m_frameLastCounted == kInvalidFrameIndex || m_frameLastCounted < frameIdx);

    // read back the GPU-written counter for this slot
    const int* gpuData = reinterpret_cast<int*>(m_countsHost->mapPtr(0));

    // track all GPU subtractions (deaths) between current and previous frame - just in case frames are skipped for some reason
    const uint32_t end = frameIdx;
    const uint32_t begin = std::max(m_frameLastCounted + 1, frameIdx - m_framesInFlight);
    for(uint32_t i = begin ; i<= end ; i++) {
      int gpuDeaths = gpuData[i % m_framesInFlight];
      cachedTotal += gpuDeaths;
    }

    // tally the CPU additions (spawns)
    cachedTotal += cpuSpawns;

    // safety checks to ensure things working as expected
    assert(cachedTotal >= 0);
    assert(cachedTotal <= m_upperBound);

    // clear out the GPU buffer to ready for current frame of simulation data
    ctx->clearBuffer(m_countGpu, 0, sizeof(int), 0);

    // we use this for safety checks
    m_frameLastCounted = frameIdx;

    return cachedTotal;
  }

  RtxParticleSystemManager::RtxParticleSystemManager(DxvkDevice* device)
    : CommonDeviceObject(device) { }

  void RtxParticleSystemManager::showImguiSettings() {
    if (RemixGui::CollapsingHeader("Particle System")) {
      ImGui::PushID("rtx_particles");
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      RemixGui::Checkbox("Enable", &enableObject());
      ImGui::BeginDisabled(!enable());
      RemixGui::Checkbox("Enable Spawning", &enableSpawningObject());
      RemixGui::DragFloat("Time Scale", &timeScaleObject(), 0.01f, 0.f, 1.f, "%.2f");

      if (RemixGui::CollapsingHeader("Global Preset", ImGuiTreeNodeFlags_CollapsingHeader)) {
        ImGui::Indent();
        ImGui::TextWrapped("The following settings will be applied to all particle systems created using the texture tagging mechanism.  Particle systems created via USD assets are not affected by these.");
        RemixGui::Separator();

        RemixGui::DragInt("Number of Particles Per Material", &numberOfParticlesPerMaterialObject(), 0.1f, 1, 10000000);

        const auto colourPickerOpts = ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_DisplayRGB;
        if (RemixGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Indent();
          ImGui::PushID("spawn");
          RemixGui::DragInt("Spawn Rate Per Second", &spawnRatePerSecondObject(), 0.1f, 0, 100000);
          RemixGui::DragFloat("Burst Duration (s)", &spawnBurstDurationObject(), 0.1f, 0.f, 100000.f, "%.2f");
          RemixGui::Separator();
          RemixGui::Checkbox("Use Spawn Texture Coordinates", &useSpawnTexcoordsObject());
          RemixGui::Separator();
          RemixGui::InputFloat3("Max Velocity", &maxSpawnVelocityObject());
          RemixGui::DragFloat("Initial Velocity From Motion", &initialVelocityFromMotionObject(), 0.01f, -5000.f, 5000.f, "%.2f");
          RemixGui::DragFloat("Initial Velocity From Normal", &initialVelocityFromNormalObject(), 0.01f, -5000.f, 5000.f, "%.2f");
          RemixGui::DragFloat("Initial Velocity Cone Angle", &initialVelocityConeAngleDegreesObject(), 0.01f, 0.f, 180.f, "%.2f");
          RemixGui::DragFloat("Initial Rotation Deviation", &initialRotationDeviationDegreesObject(), 0.01f, 0.f, 180.f, "%.2f");
          RemixGui::Separator();
          RemixGui::DragFloat("Min Life", &minParticleLifeObject(), 0.01f, 0.01f, 1000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Max Life", &maxParticleLifeObject(), 0.01f, 0.01f, 1000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::Separator();
          RemixGui::DragFloat2("Min Size", &minSpawnSizeObject(), 0.01f, 0.01f, 100.f, "%.2f");
          RemixGui::DragFloat2("Max Size", &maxSpawnSizeObject(), 0.01f, 0.01f, 100.f, "%.2f");;
          RemixGui::DragFloat("Min Rotation Speed", &minSpawnRotationSpeedObject(), 0.01f, -100.f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Max Rotation Speed", &maxSpawnRotationSpeedObject(), 0.01f, -100.f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::ColorPicker4("Minimum Color Tint", &minSpawnColorObject(), colourPickerOpts);
          RemixGui::ColorPicker4("Maximum Color Tint", &maxSpawnColorObject(), colourPickerOpts);
          ImGui::PopID();
          ImGui::Unindent();
        }

        if (RemixGui::CollapsingHeader("Target", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Indent();
          ImGui::PushID("target");
          RemixGui::InputFloat3("Max Velocity", &maxTargetVelocityObject());
          RemixGui::DragFloat2("Min Size", &minTargetSizeObject(), 0.01f, 0.01f, 100.f, "%.2f");
          RemixGui::DragFloat2("Max Size", &maxTargetSizeObject(), 0.01f, 0.01f, 100.f, "%.2f");
          RemixGui::DragFloat("Min Rotation Speed", &minTargetRotationSpeedObject(), 0.01f, -1000.f, 1000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Max Rotation Speed", &maxTargetRotationSpeedObject(), 0.01f, -1000.f, 1000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::ColorPicker4("Minimum Color Tint", &minTargetColorObject(), colourPickerOpts);
          RemixGui::ColorPicker4("Maximum Color Tint", &maxTargetColorObject(), colourPickerOpts);
          ImGui::PopID();
          ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Indent();
          RemixGui::DragFloat("Gravity Force", &gravityForceObject(), 0.01f, -1000.f, 1000.f, "%.2f");
          ImGui::BeginDisabled();
          RemixGui::DragFloat("Max Speed", &maxSpeedObject(), 0.01f, 0.f, 100000.f, "%.2f");
          ImGui::EndDisabled();
          RemixGui::DragFloat("Drag Coefficient", &dragCoefficientObject(), 0.01f, 0.f, 100.f, "%.2f");

          if (ImGui::CollapsingHeader("Attractor", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            RemixGui::InputFloat3("Position", &attractorPositionObject());
            RemixGui::InputFloat("Radius", &attractorRadiusObject());
            RemixGui::InputFloat("Force", &attractorForceObject());
            ImGui::Unindent();
          }

          if (ImGui::CollapsingHeader("Restrict Motion", ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen)) {
            RemixGui::Checkbox("X", &restrictVelocityXObject());
            RemixGui::Checkbox("Y", &restrictVelocityYObject());
            RemixGui::Checkbox("Z", &restrictVelocityZObject());
          }

          RemixGui::Checkbox("Enable Particle World Collisions", &enableCollisionDetectionObject());
          ImGui::BeginDisabled(!enableCollisionDetection());
          RemixGui::DragFloat("Collision Restitution", &collisionRestitutionObject(), 0.01f, 0.f, 1.f, "%.2f");
          RemixGui::DragFloat("Collision Thickness", &collisionThicknessObject(), 0.01f, 0.f, 10000.f, "%.2f");
          {
            static auto collisionModeCombo = RemixGui::ComboWithKey<ParticleCollisionMode>(
              "Collision Mode",
              RemixGui::ComboWithKey<ParticleCollisionMode>::ComboEntries { {
                {ParticleCollisionMode::Bounce, "Bounce"},
                {ParticleCollisionMode::Stop, "Stop"},
                {ParticleCollisionMode::Kill, "Kill"},
            } });
            collisionModeCombo.getKey(&collisionModeObject());
          }

          ImGui::EndDisabled();

          RemixGui::Checkbox("Simulate Turbulence", &useTurbulenceObject());
          ImGui::BeginDisabled(!useTurbulence());
          RemixGui::DragFloat("Turbulence Force", &turbulenceForceObject(), 0.01f, 0.f, 1000.f, "%.2f");
          RemixGui::DragFloat("Turbulence Frequency", &turbulenceFrequencyObject(), 0.01f, 0.f, 10.f, "%.2f");
          ImGui::EndDisabled();
          ImGui::Unindent();
        }

        if (RemixGui::CollapsingHeader("Visual", ImGuiTreeNodeFlags_DefaultOpen)) {
          RemixGui::Checkbox("Align Particles with Velocity", &alignParticlesToVelocityObject());
          RemixGui::Checkbox("Enable Motion Trail", &enableMotionTrailObject());
          ImGui::BeginDisabled(!enableMotionTrail());
          RemixGui::DragFloat("Motion Trail Length Multiplier", &motionTrailMultiplierObject(), 0.01f, 0.001f, 10000.f, "%.2f");
          ImGui::EndDisabled();
          {
            static auto billboardTypeCombo = RemixGui::ComboWithKey<ParticleBillboardType>(
              "Billboard Type",
              RemixGui::ComboWithKey<ParticleBillboardType>::ComboEntries { {
                {ParticleBillboardType::FaceCamera_Spherical, "Classic billboard"},
                {ParticleBillboardType::FaceCamera_UpAxisLocked, "Cylindrical billboard (fix up axis)"},
                {ParticleBillboardType::FaceWorldUp, "Horizontal plane (face up axis)"},
                {ParticleBillboardType::FaceCamera_Position, "Face camera position"},
            } });
            billboardTypeCombo.getKey(&billboardTypeObject());
          }

          {
            static auto billboardTypeCombo = RemixGui::ComboWithKey<ParticleSpriteSheetMode>(
              "Sprite Sheet Type",
              RemixGui::ComboWithKey<ParticleSpriteSheetMode>::ComboEntries { {
                {ParticleSpriteSheetMode::UseMaterialSpriteSheet, "Prefer Material (Default)"},
                {ParticleSpriteSheetMode::OverrideMaterial_Lifetime, "Particle Lifetime"},
                {ParticleSpriteSheetMode::OverrideMaterial_Random, "Select Random"},
            } });
            billboardTypeCombo.getKey(&spriteSheetModeObject());
          }

          {
            static auto randomAxisFlipeCombo = RemixGui::ComboWithKey<ParticleRandomFlipAxis>(
              "Random Axis Flipping",
              RemixGui::ComboWithKey<ParticleRandomFlipAxis>::ComboEntries { {
                {ParticleRandomFlipAxis::None, "None"},
                {ParticleRandomFlipAxis::Horizontal, "Horizontal"},
                {ParticleRandomFlipAxis::Vertical, "Vertical"},
                {ParticleRandomFlipAxis::Both, "Both"},
            } });
            randomAxisFlipeCombo.getKey(&randomFlipAxisObject());
          }

          ImGui::Unindent();
        }
        ImGui::Unindent();
      }
      ImGui::Unindent();
      ImGui::EndDisabled();
      ImGui::PopID();
    }
  }

  void RtxParticleSystemManager::setupConstants(RtxContext* ctx, ParticleSystemConstants& constants) {
    ScopedCpuProfileZone();
    const RtCamera& camera = ctx->getSceneManager().getCamera();
    constants.viewToWorld = camera.getViewToWorld();
    constants.prevWorldToProjection = camera.getPreviousViewToProjection() * camera.getPreviousWorldToView();

    constants.renderingWidth = ctx->getSceneManager().getCamera().m_renderResolution[0];
    constants.renderingHeight = ctx->getSceneManager().getCamera().m_renderResolution[1];

    constants.frameIdx = device()->getCurrentFrameId();

    constants.upDirection.x = ctx->getSceneManager().getSceneUp().x;
    constants.upDirection.y = ctx->getSceneManager().getSceneUp().y;
    constants.upDirection.z = ctx->getSceneManager().getSceneUp().z;
    constants.deltaTimeSecs = std::min(kMinimumParticleLife, GlobalTime::get().deltaTime()) * timeScale();
    constants.invDeltaTimeSecs = constants.deltaTimeSecs > 0 ? (1.f / constants.deltaTimeSecs) : 0.f;
    constants.absoluteTimeSecs = GlobalTime::get().absoluteTimeMs() * 0.001f * timeScale();

    constants.resolveTransparencyThreshold = RtxOptions::resolveTransparencyThreshold();
    constants.minParticleSize = 2.f; // NOTE: In pixels
    constants.sceneScale = RtxOptions::sceneScale();
  }

  // Please re-profile performance if any of these structures change in size.  As a minimum performance requirement, always preserve a 16 byte alignment.
  static_assert(sizeof(GpuParticle) == 12 * 4, "Unexpected, please check perf");
  static_assert(sizeof(GpuParticleSystemDesc) % (4 * 4) == 0, "Unexpected, please check perf");

  RtxParticleSystemDesc RtxParticleSystemManager::createGlobalParticleSystemDesc() {
    RtxParticleSystemDesc desc;
    desc.initialVelocityFromMotion = RtxParticleSystemManager::initialVelocityFromMotion();
    desc.initialVelocityFromNormal = RtxParticleSystemManager::initialVelocityFromNormal();
    desc.initialVelocityConeAngleDegrees = RtxParticleSystemManager::initialVelocityConeAngleDegrees();
    desc.alignParticlesToVelocity = RtxParticleSystemManager::alignParticlesToVelocity();
    desc.gravityForce = RtxParticleSystemManager::gravityForce();
    desc.useTurbulence = RtxParticleSystemManager::useTurbulence() ? 1 : 0;
    desc.turbulenceFrequency = RtxParticleSystemManager::turbulenceFrequency();
    desc.turbulenceForce = RtxParticleSystemManager::turbulenceForce();
    desc.minTimeToLive = RtxParticleSystemManager::minParticleLife();
    desc.maxTimeToLive = RtxParticleSystemManager::maxParticleLife();
    desc.maxNumParticles = RtxParticleSystemManager::numberOfParticlesPerMaterial();
    desc.minSpawnRotationSpeed = RtxParticleSystemManager::minSpawnRotationSpeed();
    desc.useSpawnTexcoords = RtxParticleSystemManager::useSpawnTexcoords() ? 1 : 0;
    desc.enableCollisionDetection = RtxParticleSystemManager::enableCollisionDetection() ? 1 : 0;
    desc.alignParticlesToVelocity = RtxParticleSystemManager::alignParticlesToVelocity() ? 1 : 0;
    desc.collisionRestitution = RtxParticleSystemManager::collisionRestitution();
    desc.collisionThickness = RtxParticleSystemManager::collisionThickness();
    desc.enableMotionTrail = RtxParticleSystemManager::enableMotionTrail() ? 1 : 0;
    desc.motionTrailMultiplier = RtxParticleSystemManager::motionTrailMultiplier();
    desc.spawnRatePerSecond = (float) RtxParticleSystemManager::spawnRatePerSecond();
    desc.hideEmitter = 0;
    desc.billboardType = RtxParticleSystemManager::billboardType();
    desc.spriteSheetMode = RtxParticleSystemManager::spriteSheetMode();
    desc.spriteSheetCols = 1;
    desc.spriteSheetRows = 1;
    desc.collisionMode = RtxParticleSystemManager::collisionMode();
    desc.attractorPosition = RtxParticleSystemManager::attractorPosition();
    desc.attractorForce = RtxParticleSystemManager::attractorForce();
    desc.attractorRadius = RtxParticleSystemManager::attractorRadius();
    desc.dragCoefficient = RtxParticleSystemManager::dragCoefficient();
    desc.spawnBurstDuration = RtxParticleSystemManager::spawnBurstDuration();
    desc.restrictVelocityX = RtxParticleSystemManager::restrictVelocityX();
    desc.restrictVelocityY = RtxParticleSystemManager::restrictVelocityY();
    desc.restrictVelocityZ = RtxParticleSystemManager::restrictVelocityZ();
    desc.randomFlipAxis = RtxParticleSystemManager::randomFlipAxis();
    desc.initialRotationDeviationDegrees = RtxParticleSystemManager::initialRotationDeviationDegrees();

    // Convert spawn/target RTX_OPTIONS to animated vector format (spawn at index 0, target at index 1)
    const Vector4& minSpawn = RtxParticleSystemManager::minSpawnColor();
    const Vector4& maxSpawn = RtxParticleSystemManager::maxSpawnColor();
    const Vector4& minTarget = RtxParticleSystemManager::minTargetColor();
    const Vector4& maxTarget = RtxParticleSystemManager::maxTargetColor();
    desc.minColor = { vec4(minSpawn.x, minSpawn.y, minSpawn.z, minSpawn.w), vec4(minTarget.x, minTarget.y, minTarget.z, minTarget.w) };
    desc.maxColor = { vec4(maxSpawn.x, maxSpawn.y, maxSpawn.z, maxSpawn.w), vec4(maxTarget.x, maxTarget.y, maxTarget.z, maxTarget.w) };

    const Vector2& minSpawnSz = RtxParticleSystemManager::minSpawnSize();
    const Vector2& maxSpawnSz = RtxParticleSystemManager::maxSpawnSize();
    const Vector2& minTargetSz = RtxParticleSystemManager::minTargetSize();
    const Vector2& maxTargetSz = RtxParticleSystemManager::maxTargetSize();
    desc.minSize = { vec2(minSpawnSz.x, minSpawnSz.y), vec2(minTargetSz.x, minTargetSz.y) };
    desc.maxSize = { vec2(maxSpawnSz.x, maxSpawnSz.y), vec2(maxTargetSz.x, maxTargetSz.y) };

    desc.minRotationSpeed = { RtxParticleSystemManager::minSpawnRotationSpeed(), RtxParticleSystemManager::minTargetRotationSpeed() };
    desc.maxRotationSpeed = { RtxParticleSystemManager::maxSpawnRotationSpeed(), RtxParticleSystemManager::maxTargetRotationSpeed() };

    const Vector3& maxSpawnVel = RtxParticleSystemManager::maxSpawnVelocity();
    const Vector3& maxTargetVel = RtxParticleSystemManager::maxTargetVelocity();
    desc.maxVelocity = { vec3(maxSpawnVel.x, maxSpawnVel.y, maxSpawnVel.z), vec3(maxTargetVel.x, maxTargetVel.y, maxTargetVel.z) };

    return desc;
  }

  bool RtxParticleSystemManager::fetchParticleSystem(DxvkContext* ctx, const DrawCallState& drawCallState, RtxParticleSystemDesc desc, const MaterialData& renderMaterialData, ParticleSystem** materialSystem) {
    ScopedCpuProfileZone();
    if (desc.maxNumParticles == 0) {
      return false;
    }

    // Update spritesheet data in the particle system with that of the material
    bool disableMaterialSpriteSheet = false;
    if (desc.spriteSheetMode != ParticleSpriteSheetMode::UseMaterialSpriteSheet) {
      uint8_t unused;
      renderMaterialData.getSpriteSheetData(desc.spriteSheetRows, desc.spriteSheetCols, unused);
      disableMaterialSpriteSheet = true;
    }

    const XXH64_hash_t particleSystemHash = drawCallState.getMaterialData().getHash() ^ desc.calcHash();

    auto& materialSystemIt = m_particleSystems.find(particleSystemHash);
    if (materialSystemIt == m_particleSystems.end()) {
      // Strip out any custom particle defined in the target material to avoid creating duplicated, nested systems.
      MaterialData particleRenderMaterial(renderMaterialData);
      particleRenderMaterial.m_particleSystem = std::nullopt;

      if(disableMaterialSpriteSheet) {
        // Disable the material spritesheet
        particleRenderMaterial.setSpriteSheetData(1, 1, 0);
      }

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

    const auto& desc = particleSystem->context.desc;
    const float spawnRate = desc.spawnRatePerSecond;
    const float burstSeconds = desc.spawnBurstDuration;

    // How much time does this spawn step represent?
    float effectiveElapsedSecs = 0.0f;

    if (burstSeconds <= 0.0f) {
      // Continuous mode
      effectiveElapsedSecs = GlobalTime::get().deltaTime();
    } else {
      // Burst mode (only spawn when enough time has passed since last burst)
      const uint64_t nowMs = GlobalTime::get().absoluteTimeMs();
      const uint64_t burstMs = (uint64_t)(burstSeconds * 1000);

      uint64_t elapsedMs;
      if (particleSystem->lastSpawnTimeMs == 0u) {
        // First burst: pretend one full burst interval has elapsed
        elapsedMs = burstMs;
      } else {
        elapsedMs = nowMs - particleSystem->lastSpawnTimeMs;
      }

      // Not time for a new burst yet
      if (elapsedMs < burstMs) {
        return 0;
      }

      float elapsedSecs = elapsedMs * 0.001f;

      // Clamp to avoid absurd bursts after long stalls
      effectiveElapsedSecs = std::min(elapsedSecs, burstSeconds * 4.0f);

      // Record time of this burst window (even if we later emit 0 due to capacity)
      particleSystem->lastSpawnTimeMs = nowMs;
    }

    float lambda = spawnRate * effectiveElapsedSecs;

    // Poisson dist won't work well with these values (inf loop)
    if (isnan(lambda) || lambda < 0.0f) {
      return 0;
    }

    std::poisson_distribution<uint32_t> distribution(lambda);
    uint32_t numParticles = std::min(distribution(particleSystem->generator), desc.maxNumParticles);

    // Capacity checks
    if (particleSystem->context.particleCount + particleSystem->context.spawnParticleCount + numParticles >= desc.maxNumParticles) {
      return 0;
    }

    // Don't allow wraparound: CPU and GPU are implicitly synchronized, and this would allow the CPU to jump ahead.
    if (particleSystem->context.particleHeadOffset + numParticles >= desc.maxNumParticles) {
      numParticles = desc.maxNumParticles - particleSystem->context.particleHeadOffset;
    }

    return numParticles;
  }

  void RtxParticleSystemManager::spawnParticles(DxvkContext* ctx, const RtxParticleSystemDesc& desc, const uint32_t instanceId, const DrawCallState& drawCallState, const MaterialData& renderMaterialData) {
    ScopedCpuProfileZone();
    if (!enable() || !enableSpawning()) {
      return;
    }

    m_initialized = true;

    ParticleSystem* particleSystem = nullptr;
    if (!fetchParticleSystem(ctx, drawCallState, desc, renderMaterialData, &particleSystem)) {
      return;
    }

    uint32_t numParticles = 0;

    const bool isNumParticlesConstant = particleSystem->context.desc.spawnRatePerSecond >= particleSystem->context.desc.maxNumParticles;
    if (isNumParticlesConstant) {
      numParticles = particleSystem->context.desc.maxNumParticles - particleSystem->context.spawnParticleCount;
    } else {
      numParticles = getNumberOfParticlesToSpawn(particleSystem, drawCallState);
    }

    if (numParticles == 0) {
      return;
    }

    assert (isNumParticlesConstant || (particleSystem->context.particleHeadOffset + numParticles) <= particleSystem->context.desc.maxNumParticles);

    // Register the spawn context data
    SpawnContext spawnCtx;
    spawnCtx.numberOfParticles = numParticles;
    spawnCtx.particleOffset = particleSystem->context.particleHeadOffset;
    spawnCtx.instanceId = instanceId;
    spawnCtx.particleSystemHash = particleSystem->getHash();

    // Update material specific counters
    particleSystem->context.particleHeadOffset += numParticles;
    particleSystem->context.spawnParticleCount += numParticles;

    // Map the particles to a context for spawn
    particleSystem->spawnContextParticleMap.insert(particleSystem->spawnContextParticleMap.end(), spawnCtx.numberOfParticles, m_spawnContexts.size());
    assert(particleSystem->spawnContextParticleMap.size() <= particleSystem->context.desc.maxNumParticles);

    // Mark the time 
    particleSystem->lastSpawnTimeMs = GlobalTime::get().absoluteTimeMs();

    // Track this spawn context by copying off
    m_spawnContexts.emplace_back(std::move(spawnCtx));
  }

  void RtxParticleSystemManager::simulate(RtxContext* ctx) {
    if (!enable() || !m_initialized) {
      m_spawnContexts.clear();
      m_particleSystems.clear();
      m_spawnContextsBuffer = nullptr;
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

      const auto& rtOutput = ctx->getResourceManager().getRaytracingOutput();
      ctx->bindResourceView(PARTICLE_SYSTEM_BINDING_PREV_WORLD_POSITION_INPUT,
                            rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Read,
                                                                                               rtOutput.getPreviousPrimaryWorldPositionWorldTriangleNormal().matchesWriteFrameIdx(constants.frameIdx - 1)), nullptr);
      const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

      for (auto& system : m_particleSystems) {
        auto& conservativeCount = system.second->getCounter();

        GpuParticleSystem& particleSystem = system.second->context;

        const bool isNumParticlesConstant = particleSystem.desc.spawnRatePerSecond >= particleSystem.desc.maxNumParticles;

        if (isNumParticlesConstant) {
          particleSystem.simulateParticleCount = particleSystem.desc.maxNumParticles;
          particleSystem.particleCount = particleSystem.desc.maxNumParticles;
          particleSystem.spawnParticleCount = particleSystem.desc.maxNumParticles;
          particleSystem.particleHeadOffset = particleSystem.desc.maxNumParticles;
          particleSystem.particleTailOffset = 0;
          particleSystem.spawnParticleOffset = 0;
        } else {
          // Update some constants on the system based on our counters
          particleSystem.particleCount = conservativeCount->preSimulation(ctx, particleSystem.spawnParticleCount, frameIdx);

          const uint32_t max = particleSystem.desc.maxNumParticles;
          const uint32_t head = particleSystem.particleHeadOffset;

          particleSystem.particleTailOffset = (head + max - particleSystem.particleCount) % max;
          particleSystem.simulateParticleCount = (particleSystem.particleCount - particleSystem.spawnParticleCount);
        }

        if (particleSystem.particleCount == 0) {
          if (!isNumParticlesConstant) {
            conservativeCount->postSimulation(ctx, frameIdx);
          }
          continue;
        }

        // Finalize some constants to the GPU data
        constants.particleSystem = particleSystem;

        assert(particleSystem.particleCount >= particleSystem.spawnParticleCount);

        // Update CB
        const DxvkBufferSliceHandle cSlice = m_cb->allocSlice();
        ctx->invalidateBuffer(m_cb, cSlice);
        ctx->writeToBuffer(m_cb, 0, sizeof(ParticleSystemConstants), &constants);
        ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_CONSTANTS, DxvkBufferSlice(m_cb));

        ctx->bindResourceSampler(PARTICLE_SYSTEM_BINDING_ANIMATION_DATA_INPUT, ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
        ctx->bindResourceView(PARTICLE_SYSTEM_BINDING_ANIMATION_DATA_INPUT, system.second->getAnimationDataTexture(), nullptr);

        DxvkBarrierControlFlags barrierControl;

        // Disable barriers for write after writes - we ensure particle implementation complies with this optimization, 
        //  since we write to particle buffer from both the spawning and evolve kernels, but only ever to unique slots
        //  of the buffer.
        if (!isNumParticlesConstant) {
          barrierControl.set(DxvkBarrierControl::IgnoreWriteAfterWrite);
        }

        ctx->setBarrierControl(barrierControl);

        // Handle spawning
        if (particleSystem.spawnParticleCount > 0) {
          const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { particleSystem.spawnParticleCount, 1, 1 }, VkExtent3D { 128, 1, 1 });

          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXTS_INPUT, DxvkBufferSlice(m_spawnContextsBuffer));
          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_SPAWN_CONTEXT_PARTICLE_MAPPING_INPUT, DxvkBufferSlice(system.second->getSpawnContextMappingBuffer()));
          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT, DxvkBufferSlice(system.second->getParticlesBuffer()));

          ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ParticleSystemSpawn::getShader());

          ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
        }

        // Handle simulation updates
        if (particleSystem.simulateParticleCount > 0) {
          const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { particleSystem.simulateParticleCount, 1, 1 }, VkExtent3D { 128, 1, 1 });

          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT_OUTPUT, DxvkBufferSlice(system.second->getParticlesBuffer()));
          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_COUNTER_OUTPUT, DxvkBufferSlice(system.second->getCounter()->getGpuCountBuffer()));

          ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ParticleSystemEvolve::getShader());

          ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
        }

        // Handle geometry creation - note, should move this into own loop (barrier latency)
        {
          const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { particleSystem.particleCount, 1, 1 }, VkExtent3D { 128, 1, 1 });

          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_PARTICLES_BUFFER_INPUT, DxvkBufferSlice(system.second->getParticlesBuffer()));
          ctx->bindResourceBuffer(PARTICLE_SYSTEM_BINDING_VERTEX_BUFFER_OUTPUT, DxvkBufferSlice(system.second->getVertexBuffer()));

          ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ParticleSystemGenerateGeometry::getShader());

          ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
        }

        ctx->setBarrierControl(DxvkBarrierControlFlags());

        if (!isNumParticlesConstant) {
          conservativeCount->postSimulation(ctx, ctx->getDevice()->getCurrentFrameId());
        }
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
      gpuCtx.spawnPrevObjectToWorld = pTargetInstance->getPrevTransform();

      gpuCtx.indices32bit = pTargetInstance->getBlas()->modifiedGeometryData.indexBuffer.indexType() == VK_INDEX_TYPE_UINT32 ? 1 : 0;
      gpuCtx.numTriangles = pTargetInstance->getBlas()->modifiedGeometryData.indexCount / 3;
      gpuCtx.spawnMeshIndexIdx = pTargetInstance->surface.indexBufferIndex;
      gpuCtx.spawnMeshPositionsIdx = pTargetInstance->surface.positionBufferIndex;
      gpuCtx.spawnMeshPrevPositionsIdx = pTargetInstance->surface.previousPositionBufferIndex;

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

      // This is a conservative estimate, so we must clamp particles to the known maximum
      const uint32_t numParticles = std::min(particleSystem.context.particleCount + particleSystem.context.spawnParticleCount, particleSystem.context.desc.maxNumParticles);
      if (numParticles == 0) {
        continue;
      }

      // This is used to uniquely hash particle system geometry data - we do this because the particle data is hashed differently from regular D3D9 geometry.
      const XXH64_hash_t particleHashConstant = XXH3_64bits_withSeed(&numParticles, sizeof(numParticles), particleSystem.getHash());

      const DxvkBufferSlice& vertexSlice = DxvkBufferSlice(particleSystem.getVertexBuffer());
      const DxvkBufferSlice& indexSlice = DxvkBufferSlice(particleSystem.getIndexBuffer());

      RasterGeometry particleGeometry;
      particleGeometry.indexBuffer = RasterBuffer(indexSlice, 0, sizeof(uint32_t), VK_INDEX_TYPE_UINT32);
      particleGeometry.indexCount = particleSystem.getIndicesPerParticle() * numParticles;
      particleGeometry.vertexCount = particleSystem.getVerticesPerParticle() * numParticles;
      particleGeometry.positionBuffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, position), sizeof(ParticleVertex), VK_FORMAT_R32G32B32_SFLOAT);
      particleGeometry.color0Buffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, color), sizeof(ParticleVertex), VK_FORMAT_B8G8R8A8_UNORM);
      particleGeometry.texcoordBuffer = RasterBuffer(vertexSlice, offsetof(ParticleVertex, texcoord), sizeof(ParticleVertex), VK_FORMAT_R32G32_SFLOAT);
      particleGeometry.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      particleGeometry.cullMode = VK_CULL_MODE_NONE;
      particleGeometry.frontFace = VK_FRONT_FACE_CLOCKWISE;
      particleGeometry.hashes[HashComponents::Indices] = particleHashConstant ^ particleSystem.getGeneration();
      particleGeometry.hashes[HashComponents::VertexPosition] = particleHashConstant ^ particleSystem.getGeneration();
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
      newDrawCallState.materialData.textureColorArg1Source = RtTextureArgSource::Texture;
      newDrawCallState.materialData.textureColorArg2Source = RtTextureArgSource::VertexColor0;
      newDrawCallState.materialData.textureColorOperation = DxvkRtTextureOperation::Modulate;
      newDrawCallState.materialData.textureAlphaArg1Source = RtTextureArgSource::Texture;
      newDrawCallState.materialData.textureAlphaArg2Source = RtTextureArgSource::VertexColor0;
      newDrawCallState.materialData.textureAlphaOperation = DxvkRtTextureOperation::Modulate;
      newDrawCallState.materialData.isVertexColorBakedLighting = false;

      ctx->getSceneManager().submitDrawState(ctx, newDrawCallState, &particleSystem.materialData);
    }
  }

  RtxParticleSystemManager::ParticleSystem::ParticleSystem(const RtxParticleSystemDesc& desc, const MaterialData& matData, const LegacyMaterialData& legacyMatData, const CategoryFlags& cats, const uint32_t seed) : context(desc)
    , materialData(matData)
    , legacyMaterialData(legacyMatData)
    , categories(cats)
    , lastSpawnTimeMs(GlobalTime::get().absoluteTimeMs()) {
    // Seed the RNG with a parameter from the manager, so we get unique random values for each particle system
    generator = std::default_random_engine(seed);
    // Store this hash since it cannot change now.
    // NOTE: This material data hash is stable within a run, but since hash depends on VK handles, it is not reliable across runs.
    m_cachedHash = materialData.getHash() ^ desc.calcHash();
    context.numVerticesPerParticle = getVerticesPerParticle();

    // Copy CPU-only animation data from RtxParticleSystemDesc
    animationData.minColor = desc.minColor;
    animationData.maxColor = desc.maxColor;
    animationData.minSize = desc.minSize;
    animationData.maxSize = desc.maxSize;
    animationData.maxVelocity = desc.maxVelocity;
    animationData.minRotationSpeed = desc.minRotationSpeed;
    animationData.maxRotationSpeed = desc.maxRotationSpeed;

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
      float2(0.5f, -0.5f),
      float2(0.5f,  0.0f),

      // HEAD quad (stretched)
      float2(-0.5f, 0.0f),
      float2(-0.5f, 0.5f),
      float2(0.5f, 0.0f),
      float2(0.5f, 0.5f)
    };

    if (desc.enableMotionTrail) {
      memcpy(&context.particleVertexOffsets[0], &offsetsMotionTrail[0], sizeof(offsetsMotionTrail));
    } else {
      memcpy(&context.particleVertexOffsets[0], &offsets[0], sizeof(offsets));
    }


  }

  void RtxParticleSystemManager::ParticleSystem::allocStaticBuffers(DxvkContext* ctx) {
    ScopedCpuProfileZone();

    if (m_count == nullptr) {
      m_count = new ConservativeCounter(ctx, 10, context.desc.maxNumParticles);
    }

    // Handle the reallocation of all GPU and CPU data structures.  

    const Rc<DxvkDevice>& device = ctx->getDevice();
    if (m_particles == nullptr || m_particles->info().size != sizeof(GpuParticle) * context.desc.maxNumParticles) {
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

      ctx->clearBuffer(m_particles, 0, info.size, GpuParticle::kBufferClearValue);
    }

    if (m_vb == nullptr || m_vb->info().size != sizeof(ParticleVertex) * getMaxVertexCount()) {
      DxvkBufferCreateInfo info;
      info.size = sizeof(ParticleVertex) * getMaxVertexCount();
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

    if (m_ib == nullptr || m_ib->info().size != sizeof(uint32_t) * getMaxIndexCount()) {
      DxvkBufferCreateInfo info;
      info.size = sizeof(uint32_t) * getMaxIndexCount();
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
      m_ib = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Index Buffer");

      std::vector<uint32_t> indices(getMaxIndexCount());
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
          indices[i * getIndicesPerParticle() + 6] = i * getVerticesPerParticle() + 1;
          indices[i * getIndicesPerParticle() + 7] = i * getVerticesPerParticle() + 4;
          indices[i * getIndicesPerParticle() + 8] = i * getVerticesPerParticle() + 3;

          indices[i * getIndicesPerParticle() + 9] = i * getVerticesPerParticle() + 3;
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

      ctx->writeToBuffer(m_ib, 0, info.size, indices.data());
    }

    if (m_spawnContextParticleMapBuffer == nullptr || m_spawnContextParticleMapBuffer->info().size != sizeof(uint16_t) * context.desc.maxNumParticles) {
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

    if (!m_animationState.isValid()) {
      const uint32_t width = 256; // this parameter controls the resolution of the interpolation space for all system level parameters
      const uint32_t height = (uint32_t) ParticleAnimationDataRows::Count;
      m_animationState = device->getCommon()->getResources().createImageResource(Rc<DxvkContext>(ctx), "RTX Particles - Animation Data Tex", { width, height, 1 }, VK_FORMAT_R16G16B16A16_SFLOAT);

      // Helper to sample and interpolate from animation keyframes at normalized position u in [0,1]
      auto sampleAnimation = [](const auto& data, float u, auto defaultValue) {
        if (data.empty()) return decltype(data[0])(defaultValue);
        if (data.size() == 1) return data[0];
        const float pos = u * (data.size() - 1);
        const size_t idx0 = std::min((size_t) pos, data.size() - 1);
        const size_t idx1 = std::min(idx0 + 1, data.size() - 1);
        return lerp(data[idx0], data[idx1], pos - (float) idx0);
      };

      // Helper to write a vec4 to the animation texture
      auto writePixel = [](u16vec4& out, const vec4& val) {
        out.x = glm::packHalf1x16(val.x);
        out.y = glm::packHalf1x16(val.y);
        out.z = glm::packHalf1x16(val.z);
        out.w = glm::packHalf1x16(val.w);
      };

      std::vector<u16vec4> animData(width * height);
      for (uint32_t x = 0; x < width; ++x) {
        // Normalized particle life is 1 at birth and 0 at death, so reverse u for correct texture lookup
        const float u = 1.0f - float(x) / (width - 1u);

        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MinColor * width + x], sampleAnimation(animationData.minColor, u, vec4(1.0f)));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MaxColor * width + x], sampleAnimation(animationData.maxColor, u, vec4(1.0f)));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MinSize * width + x], vec4(sampleAnimation(animationData.minSize, u, vec2(1.0f, 1.0f)), 0, 0));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MaxSize * width + x], vec4(sampleAnimation(animationData.maxSize, u, vec2(1.0f, 1.0f)), 0, 0));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MinRotationSpeed * width + x], vec4(sampleAnimation(animationData.minRotationSpeed, u, 0.0f), 0, 0, 0));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MaxRotationSpeed * width + x], vec4(sampleAnimation(animationData.maxRotationSpeed, u, 0.0f), 0, 0, 0));
        writePixel(animData[(uint32_t) ParticleAnimationDataRows::MaxVelocity * width + x], vec4(sampleAnimation(animationData.maxVelocity, u, vec3(0.0f, 0.0f, 0.0f)), 0));
      }

      // Upload to the image
      VkImageSubresourceLayers subresources = {};
      subresources.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      subresources.mipLevel = 0;
      subresources.baseArrayLayer = 0;
      subresources.layerCount = 1;

      ctx->updateImage(m_animationState.image, subresources, { 0, 0, 0 }, { width, height, 1 }, animData.data(), width * sizeof(u16vec4), width * height * sizeof(u16vec4));
    }
  }

  void RtxParticleSystemManager::allocStaticBuffers(DxvkContext* ctx) {
    ScopedCpuProfileZone();

    const Rc<DxvkDevice>& device = ctx->getDevice();

    if (m_cb.ptr() == nullptr) {
      DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
      info.size = sizeof(ParticleSystemConstants);
      m_cb = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "RTX Particles - Constant Buffer");
    }

    if (m_spawnContextsBuffer == nullptr || m_spawnContextsBuffer->info().size < sizeof(GpuSpawnContext) * std::max(100u, (uint32_t) m_spawnContexts.size())) {
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
      const uint64_t maxTimeBetweenSpawnEventsMs = (uint64_t)((particleSystem.context.desc.spawnBurstDuration + particleSystem.context.desc.maxTimeToLive) * 1000);
      if ((particleSystem.lastSpawnTimeMs + maxTimeBetweenSpawnEventsMs) < now) {
        keyPairIt = m_particleSystems.erase(keyPairIt);
        continue;
      }

      ++particleSystem.generationIdx;

      particleSystem.context.spawnParticleOffset = particleSystem.context.particleHeadOffset;
      particleSystem.context.spawnParticleCount = 0;

      // Handle wrap around at a safe point (we have already ensured that spawning cant induce a wrap around early).
      if (particleSystem.context.particleHeadOffset >= particleSystem.context.desc.maxNumParticles) {
        particleSystem.context.particleHeadOffset = 0;
      }

      particleSystem.spawnContextParticleMap.clear();

      ++keyPairIt;
    }
  }

}
