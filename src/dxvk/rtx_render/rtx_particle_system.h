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

    class ConservativeCounter : public RcObject {
      const uint32_t m_framesInFlight;
      const uint32_t m_upperBound;

      uint32_t m_frameLastCounted = kInvalidFrameIndex;

      Rc<DxvkBuffer> m_countGpu;
      Rc<DxvkBuffer> m_countsHost; // copy target to access counts on the CPU - buffer contains entire "frames in flight" data (offset)

      ConservativeCounter() = delete;

    public:
      int cachedTotal = 0;

      ConservativeCounter(DxvkContext* ctx, const uint32_t framesInFlight, const uint32_t upperBound);

      const Rc<DxvkBuffer>& getGpuCountBuffer() const { return m_countGpu; }

      uint32_t preSimulation(DxvkContext* ctx, uint32_t newAdditions, uint32_t frameIdx);
      void postSimulation(DxvkContext* ctx, uint32_t frameIdx);
    };

    struct ParticleSystem {
    private:
      Rc<DxvkBuffer> m_particles;
      Rc<DxvkBuffer> m_spawnContextParticleMapBuffer;
      Rc<DxvkBuffer> m_vb;
      Rc<DxvkBuffer> m_ib;
      Rc<ConservativeCounter> m_count;

      XXH64_hash_t m_cachedHash = kEmptyHash;

    public:
      const MaterialData materialData;
      const LegacyMaterialData legacyMaterialData;
      const CategoryFlags categories;

      uint64_t lastSpawnTimeMs;

      GpuParticleSystem context;

      std::vector<uint16_t> spawnContextParticleMap;

      std::mt19937 generator;

      uint32_t generationIdx = 0;

      ParticleSystem() = delete;
      ParticleSystem(const RtxParticleSystemDesc& desc, const MaterialData& matData, const LegacyMaterialData& legacyMatData, const CategoryFlags& cats, const uint32_t seed);

      XXH64_hash_t getHash() const {
        return m_cachedHash;
      }

      const Rc<ConservativeCounter>& getCounter() const {
        return m_count;
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

      uint32_t getVerticesPerParticle() const {
        return context.desc.enableMotionTrail ? 8 : 4;
      }

      uint32_t getIndicesPerParticle() const {
        return context.desc.enableMotionTrail ? 18 : 6;
      }

      uint32_t getMaxVertexCount() const {
        return context.desc.maxNumParticles * getVerticesPerParticle();
      }

      uint32_t getMaxIndexCount() const {
        return context.desc.maxNumParticles * getIndicesPerParticle();
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

    RTX_OPTION("rtx.particles", bool, enable, true, "Enables particle simulation and rendering.");
    RTX_OPTION("rtx.particles", bool, enableSpawning, true, "Controls whether or not any particle system can currently spawn new particles.");
    RTX_OPTION("rtx.particles", float, timeScale, 1.f, "Time modifier, can be used to slow/speed up time.");

    RTX_OPTION("rtx.particles.globalPreset", int, spawnRatePerSecond, 100, "Number of particles (per system) to spawn per second on average.");
    RTX_OPTION("rtx.particles.globalPreset", int, numberOfParticlesPerMaterial, 10000, "Maximum number of particles to simulate per material simultaneously.  There is a performance consideration, lower numbers are more performant.  Ideal is to tune this number for your specific needs.");

    RTX_OPTION("rtx.particles.globalPreset", float, minParticleLife, 1.f, "Minimum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxParticleLife, 1.f, "Maximum lifetime (in seconds) to give to a particle when spawned.");

    RTX_OPTION("rtx.particles.globalPreset", float, minSpawnSize, 10.f, "Minimum size (in centimeters) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxSpawnSize, 10.f, "Maximum size (in centimeters) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, minSpawnRotationSpeed, 0.f, "Minimum rotation speed (in revolutions per second) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxSpawnRotationSpeed, 0.f, "Maximum rotation speed (in revolutions per second) to give to a particle when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, minSpawnColor, Vector4(1.f), "Minimum range of the color to tint a particle with when spawned.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, maxSpawnColor, Vector4(1.f), "Minimum range of the color to tint a particle with when spawned.");

    RTX_OPTION("rtx.particles.globalPreset", float, minTargetSize, 0.f, "Minimum size (in centimeters) picked from a range, to be used as the target animation state, at the end of the particles life.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxTargetSize, 0.f, "Maximum size (in centimeters) picked from a range, to be used as the target animation state, at the end of the particles life.");
    RTX_OPTION("rtx.particles.globalPreset", float, minTargetRotationSpeed, 0.f, "Minimum rotation speed (in revolutions per second) picked from a range, to be used as the target animation state, at the end of the particles life.  Only used if alignParticlesToVelocity is false.");
    RTX_OPTION("rtx.particles.globalPreset", float, maxTargetRotationSpeed, 0.f, "Maximum rotation speed (in revolutions per second) picked from a range, to be used as the target animation state, at the end of the particles life.  Only used if alignParticlesToVelocity is false.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, minTargetColor, Vector4(1.f, 1.f, 1.f, 0.f), "Minimum RGBA color picked from a range, to be used as the target animation state, at the end of the particles life.");
    RTX_OPTION("rtx.particles.globalPreset", Vector4, maxTargetColor, Vector4(1.f, 1.f, 1.f, 0.f), "Maximum RGBA color picked from a range, to be used as the target animation state, at the end of the particles life.");

    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityFromMotion, 0.f, "Multiplier for initial velocity applied at spawn time, based on the spawning objects current velocity.");
    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityFromNormal, 0.f, "Initial speed to apply on spawn (centimeters per sec) along the normal vector of the spawning triangle.");
    RTX_OPTION("rtx.particles.globalPreset", float, initialVelocityConeAngleDegrees, 0.f, "Specifies the half angle, in degrees, of the random emission cone  around the triangles surface normal when spawning a new particle.  A value in the range of 0 to 180 degrees is expected.");
    RTX_OPTION("rtx.particles.globalPreset", float, gravityForce, -.98f, "Net influence of gravity acting on each particle (centimeters per second squared).");
    RTX_OPTION("rtx.particles.globalPreset", float, maxSpeed, -1.f, "Maximum speed of a particle in world space (in centimeters per second).  Negative values imply unlimited.");
    RTX_OPTION("rtx.particles.globalPreset", bool, useSpawnTexcoords, false, "Use the texture coordinates of the emitter mesh when spawning particles.");
    RTX_OPTION("rtx.particles.globalPreset", bool, alignParticlesToVelocity, false, "Rotates the particles such that they are always aligned with their direction of travel, in this mode we ignore rotation speed.");
    RTX_OPTION("rtx.particles.globalPreset", bool, enableCollisionDetection, false, "Enables particle collisions with the world.");
    RTX_OPTION("rtx.particles.globalPreset", float, collisionRestitution, .5, "The fraction of velocity retained after a collision with scene geometry. 1.0 = perfectly elastic (no speed loss), 0.0 = completely inelastic (velocity zeroed). Values outside [0,1] will be clamped to this range.");
    RTX_OPTION("rtx.particles.globalPreset", float, collisionThickness, 5.f, "The maximum penetration depth (in centimeters) at which a particle will still collide with geometry.  Particles that penetrate deeper than this value are considered to have passed through thin objects and will not collide.");
    RTX_OPTION("rtx.particles.globalPreset", bool, useTurbulence, false, "Enable turbulence simulation.");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceForce, 5.f, "How much turbulence influences the velocity of a particle as an external force (represented in centimeters per second squared).");
    RTX_OPTION("rtx.particles.globalPreset", float, turbulenceFrequency, .05f, "Frequency (rate of change) of the turbulence forces. Lower values change slowly; higher values change rapidly.  This is specified in centimeters.");
    RTX_OPTION("rtx.particles.globalPreset", bool, enableMotionTrail, false, "Elongates the particle with respect to velocity, texture edges are preserved, with only the center being stretched which provides a motion blur like effect on the particles themselves.  This will automatically align particles rotation with their individual velocity (similar to rtx.particles.globalPreset.alignParticlesToVelocity) and so rotation parameters are no longer taken into account when this setting is enabled.");
    RTX_OPTION("rtx.particles.globalPreset", float, motionTrailMultiplier, 1.f, "When enableMotionTrail is set to enabled, this value can be used to increase (or decrease) the length of the tail artificially, which is determined by the velocity.  A value of 1 (the default) will ensure each particle is the exact size of the motion over the previous frame.  Values greater than 1 will increase that size linearly.  Likewise for smaller than 1.  0 and below is an invalid value.");


    void setupConstants(RtxContext* ctx, ParticleSystemConstants& constants);

    void allocStaticBuffers(DxvkContext* ctx);
    void writeSpawnContextsToGpu(RtxContext* ctx);

    void prepareForNextFrame();

    bool fetchParticleSystem(DxvkContext* ctx, const DrawCallState& drawCallState, const RtxParticleSystemDesc& desc, const MaterialData& renderMaterialData, ParticleSystem** materialSystem);

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
      * \param ctx                Command list to perform any potential allocation clears with.
      * \param desc               Description for the particle system.
      * \param instanceIdx        This is the index into the vector of instances held by instance manager.
      * \param drawCallState      The current draw call state referencing material info.
      * \param renderMaterialData The material preference to apply to particle system
      */
    void spawnParticles(DxvkContext* ctx, const RtxParticleSystemDesc& desc, const uint32_t instanceIdx, const DrawCallState& drawCallState, const MaterialData& overrideMaterial);

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
