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
    private:
      Rc<DxvkBuffer> m_particles;
      Rc<DxvkBuffer> m_spawnContextParticleMapBuffer;
      Rc<DxvkBuffer> m_vb;
      Rc<DxvkBuffer> m_ib;

      XXH64_hash_t m_cachedHash = kEmptyHash;

    public:
      const MaterialData materialData;
      const CategoryFlags categories;

      uint64_t lastSpawnTimeMs;

      GpuParticleSystem context;

      std::vector<uint16_t> spawnContextParticleMap;

      std::mt19937 generator;

      uint32_t particleWriteOffset = 0; 
      uint32_t generationIdx = 0;

      ParticleSystem() = delete;
      ParticleSystem(const RtxParticleSystemDesc& desc, const MaterialData& matData, const CategoryFlags& cats, const uint32_t seed)
        : context(desc)
        , materialData(matData)
        , categories(cats) {
        // Seed the RNG with a parameter from the manager, so we get unique random values for each particle system
        generator = std::default_random_engine(seed);
        // Store this hash since it cannot change now.
        // NOTE: This material data hash is stable within a run, but since hash depends on VK handles, it is not reliable across runs.
        m_cachedHash = materialData.getHash() ^ context.desc.calcHash();
      }

      XXH64_hash_t getHash() const {
        return m_cachedHash;
      }

      const Rc<DxvkBuffer>& getParticlesBuffer() const {
        return m_particles;
      }

      const Rc<DxvkBuffer>& getSpawnContextMappingBuffer() const {
        return m_spawnContextParticleMapBuffer;
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

    // Monotonically increases as new particle systems are created.  Never decrements.
    uint32_t m_particleSystemCounter = 0;

    std::vector<SpawnContext> m_spawnContexts;
    bool m_initialized = false;

    RTX_OPTION("rtx.particles", bool, enable, true, "Enables dust particle simulation and rendering.");
    RTX_OPTION("rtx.particles", float, timeScale, 1.f, "Time modifier, can be used to slow/speed up time.");

    RTX_OPTION("rtx.particles.globalPreset", int, spawnRatePerSecond, 100, "Number of particles (per system) to spawn per second on average.");
    RTX_OPTION("rtx.particles.globalPreset", int, numberOfParticlesPerMaterial, 1024 * 96, "Maximum number of particles to simulate per material simultaneously.  There is a performance consideration, lower numbers are more performant.  Ideal is to tune this number for your specific needs.");
    RTX_OPTION("rtx.particles.globalPreset", float, minParticleLife, 3.f, "Minimum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxParticleLife, 6.f, "Maximum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, minParticleSize, 1.f, "Minimum size (in world units) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxParticleSize, 3.f, "Maximum size (in world units) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, minRotationSpeed, .1f, "Minimum rotation speed (in revolutions per second) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxRotationSpeed, 1.f, "Maximum rotation speed (in revolutions per second) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, minSpawnColor, Vector4(1.f), "Minimum range of the color to tint a particle with when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, maxSpawnColor, Vector4(1.f), "Minimum range of the color to tint a particle with when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityFromNormal, 10.f, "Initial speed to apply on spawn (units/sec) along the normal vector of the spawning triangle.");
    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityConeAngleDegrees, 0.f, "Specifies the half angle, in degrees, of the random emission cone  around the triangles surface normal when spawning a new particle.  A value in the range of 0 to 180 degrees is expected.");
    RTX_OPTION("rtx.particles.globalPreset", float, gravityForce, -.5f, "Net influence of gravity acting on each particle (meters per second squared).");
    RTX_OPTION("rtx.particles.globalPreset", float, maxSpeed, 3.f, "Maximum speed of a particle in world space.");
    RTX_OPTION("rtx.particles.globalPreset", bool, useSpawnTexcoords, false, "Use the texcoords of the emitter mesh when spawning particles.");
    RTX_OPTION("rtx.particles.globalPreset", bool, alignParticlesToVelocity, false, "Rotates the particles such that they are always aligned with their direction of travel, in this mode we ignore rotation speed.");
    RTX_OPTION("rtx.particles.globalPreset", bool, enableCollisionDetection, false, "Enables particle collisions with the world.");
    RTX_OPTION("rtx.particles.globalPreset", float, collisionRestitution, .5, "The fraction of velocity retained after a collision with scene geometry. 1.0 = perfectly elastic (no speed loss), 0.0 = completely inelastic (velocity zeroed). Values outside [0,1] will be clamped to this range.");
    RTX_OPTION("rtx.particles.globalPreset", float, collisionThickness, 5.f, "The maximum penetration depth (in world units) at which a particle will still collide with geometry.  Particles that penetrate deeper than this value are considered to have passed through thin objects and will not collide.");
    RTX_OPTION("rtx.particles.globalPreset", bool, useTurbulence, true, "Enable turbulence simulation.");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceAmplitude, 5.f, "How much turbulence influences the force of a particle.");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceFrequency, .05f, "The rate of change of turbulence forces.");


    void setupConstants(RtxContext* ctx, ParticleSystemConstants& constants);

    void allocStaticBuffers(DxvkContext* ctx);
    void writeSpawnContextsToGpu(RtxContext* ctx);

    void prepareForNextFrame();

    bool fetchParticleSystem(DxvkContext* ctx, const DrawCallState& drawCallState, const RtxParticleSystemDesc& desc, const MaterialData* overrideMaterial, ParticleSystem** materialSystem);

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
      * \param ctx              Command list to perform any potential allocation clears with.
      * \param desc             Description for the particle system.
      * \param instanceIdx      This is the index into the vector of instances held by instance manager.
      * \param drawCallState    The current draw call state referencing material info.
      * \param overrideMaterial The material preference to apply to particle system
      */
    void spawnParticles(DxvkContext* ctx, const RtxParticleSystemDesc& desc, const uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData* overrideMaterial);

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
