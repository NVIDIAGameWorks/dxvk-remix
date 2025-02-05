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
#pragma once


#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "rtx_resources.h"
#include "rtx/pass/particles/particle_system_binding_indices.h"
#include <random>

namespace dxvk {
  class RtxContext;

  /**
    * A particle system implementation that can handle multiple materials,
    * each material storing its own subset of simulated particles.
    *
    * The RtxParticleSystem class is responsible for:
    * - Managing buffers for particle data on the GPU.
    * - Spawning new particles for given materials.
    * - Simulating particle movement and behavior (possibly involving turbulence, gravity, etc.).
    * - Rendering the particle geometry (vertex and index buffers).
    */
  class RtxParticleSystemManager : public CommonDeviceObject {
    struct SpawnContext {
      uint32_t numberOfParticles;
      uint32_t instanceId;
      XXH64_hash_t particleSystemHash;
      uint32_t particleOffset;
    };

    struct ParticleSystem {
      ParticleSystem() = delete;

      const LegacyMaterialData materialData;
      const CategoryFlags categories;

      uint64_t lastSpawnTimeMs;

      GpuParticleSystem context;

      std::vector<uint16_t> spawnContextParticleMap;

      std::default_random_engine generator;

      uint32_t particleWriteOffset = 0;
      uint32_t generationIdx = 0;

      Rc<DxvkBuffer> m_particles;
      Rc<DxvkBuffer> m_spawnContextParticleMapBuffer;
      Rc<DxvkBuffer> m_vb;
      Rc<DxvkBuffer> m_ib;

      ParticleSystem(const RtxParticleSystemDesc& desc, const LegacyMaterialData& matData, const CategoryFlags& cats)
        : context(desc)
        , materialData(matData)
        , categories(cats) {
        // Seed the RNG using the particle system hash.
        generator = std::default_random_engine(calcHash());
      }

      XXH64_hash_t calcHash() const {
        return materialData.getHash() ^ context.desc.calcHash();
      }

      const Rc<DxvkBuffer>& getVertexBuffer() const {
        return m_vb;
      }

      const Rc<DxvkBuffer>& getIndexBuffer() const {
        return m_ib;
      }

      const uint32_t getGeneration() const {
        return generationIdx;
      }

      uint32_t getVertexCount() const {
        return context.desc.maxNumParticles * 4;
      }

      uint32_t getIndexCount() const {
        return context.desc.maxNumParticles * 6;
      }

      void allocStaticBuffers(DxvkContext* pCtx);
    };

    Rc<DxvkBuffer> m_cb;

    fast_unordered_cache<std::shared_ptr<ParticleSystem>> m_particleSystems;
    Rc<DxvkBuffer> m_spawnContextsBuffer;

    std::vector<SpawnContext> m_spawnContexts;
    bool m_initialized = false;

    RTX_OPTION("rtx.particles", bool, enable, true, "Enables dust particle simulation and rendering.");
    RTX_OPTION("rtx.particles", float, timeScale, 1.f, "Time modifier, can be used to slow/speed up time.");

    RTX_OPTION("rtx.particles.globalPreset", int, spawnRatePerCubicMeterPerSecond, 100, "Spawn rate per cubic meter per second of particles.");
    RTX_OPTION("rtx.particles.globalPreset", int, numberOfParticlesPerMaterial, 1024 * 96, "Maximum number of particles to simulate per material simultaneously.  There is a performance consideration, lower numbers are more performant.  Ideal is to tune this number for your specific needs.");
    RTX_OPTION("rtx.particles.globalPreset", float, minParticleLife, 3.f, "Minimum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxParticleLife, 6.f, "Maximum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, minParticleSize, 1.f, "Minimum size (in world units) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxParticleSize, 3.f, "Maximum size (in world units) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, opacityMultiplier, 1.f, "Multiplier for the opacity of a particle in relation to the opacity of the triangle spawning the particle.");
    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityFromNormal, 10.f, "Initial speed to apply on spawn (units/sec) along the normal vector of the spawning triangle.");
    RTX_OPTION("rtx.particles.globalPreset", float, gravityForce, -.5f, "Net influence of gravity acting on each particle (meters per second squared).");
    RTX_OPTION("rtx.particles.globalPreset", float, maxSpeed, 3.f, "Maximum speed of a particle in world space.");
    RTX_OPTION("rtx.particles.globalPreset", bool, useTurbulence, true, "Enable turbulence simulation.");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceAmplitude, 5.f, "How much turbulence influences the force of a particle.");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceFrequency, .05f, "The rate of change of turbulence forces.");


    void setupConstants(RtxContext* ctx, ParticleSystemConstants& constants);

    void allocStaticBuffers(DxvkContext* ctx);
    void writeSpawnContextsToGpu(RtxContext* ctx);

    void prepareForNextFrame();

    bool fetchParticleSystem(DxvkContext* ctx, const DrawCallState& drawCallState, const RtxParticleSystemDesc& desc, ParticleSystem** materialSystem);

    static uint32_t getNumberOfParticlesToSpawn(ParticleSystem* materialSystem, const DrawCallState& drawCallState);

  public:
    RtxParticleSystemManager(DxvkDevice* device);
    ~RtxParticleSystemManager() = default;

    /**
      * Displays ImGui settings for this particle system.
      */
    static void showImguiSettings();

    /**
      * Creates a valid descriptor for a particle system using the global settings.
      */
    static RtxParticleSystemDesc createGlobalParticleSystemDesc();

    /**
      * Spawns particles for a given instance
      * typically called once per draw call. This method:
      * - Retrieves or creates the MaterialSystem for the drawCallState.
      * - Determines how many particles to spawn.
      * - Schedules the spawn (particles are actually allocated during simulation).
      *
      * \param ctx            Command list to perform any potential allocation clears with.
      * \param desc           Description for the particle system.
      * \param instanceIdx    This is the index into the vector of instances held by instance manager.
      * \param drawCallState  The current draw call state referencing material info.
      */
    void spawnParticlesForMaterial(DxvkContext* ctx, const RtxParticleSystemDesc& desc, const uint32_t instanceIdx, const DrawCallState& drawCallState);

    /**
      * Issues the draw state (vertex buffer, index buffer) for rendering particles
      * onto the provided RtxContext. 
      *
      * \param ctx The RtxContext in which to set draw state.
      */
    void submitDrawState(RtxContext* ctx) const;

    /**
      * Performs the particle simulation steps:
      * - Writes current spawn contexts to GPU.
      * - Issues compute passes for updating particle positions, applying forces, and
      *   culling expired particles.
      * - Increments generation and cleans up for the next frame.
      *
      * \param ctx           The RtxContext for issuing GPU commands.
      */
    void simulate(RtxContext* ctx);
  };
}
