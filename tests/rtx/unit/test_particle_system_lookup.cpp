/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <iostream>
#include <memory>

#include "../../../src/dxvk/rtx_render/rtx_particle_system.h"
#include "../../../src/dxvk/rtx_render/rtx_types.h"
#include "../../../src/dxvk/rtx_render/rtx_instance_manager.h"

namespace dxvk {
  Logger Logger::s_instance("test_particle_system_lookup.log");

  namespace {
    void require(bool condition, const char* message) {
      if (!condition) {
        throw DxvkError(message);
      }
    }
  }

  struct ParticleSystemLookupTestAccess {
    static void testEmitterSurvivesCompaction() {
      RtInstance removed(1, 0);
      RtInstance other(2, 1);
      RtInstance emitter(kInvalidInstanceId, 2);
      RtxParticleSystemManager::SpawnContext spawn{};
      spawn.instanceId = 2;
      spawn.instanceIdentity = emitter.getCacheIdentity();
      std::vector<RtInstance*> instances{ &removed, &other, &emitter };
      std::unordered_map<uint64_t, const RtInstance*> lookup;
      bool built = false;
      auto resolve = [&]() { return RtxParticleSystemManager::resolveSpawnInstance(spawn, instances, lookup, built); };
      require(resolve() == &emitter && !built, "Unmoved emitters must use the index fast path.");
      instances = { &emitter, &other };
      require(resolve() == &emitter, "A queued emitter must survive swap-and-pop compaction before simulation.");
      lookup.clear(); built = false;
      instances = { &other, &emitter, &removed };
      require(resolve() == &emitter, "A reused old slot must not redirect births onto another object.");
      lookup.clear(); built = false;
      RtInstance replacement(kInvalidInstanceId, 2);
      instances = { &other, &replacement };
      require(resolve() == nullptr, "Retired emitters must not bind to a replacement allocation with the same public ID.");
    }

    static void testSpawnContextFindsSystem(bool matchingMaterialHashes, bool isExternal) {
      OpaqueMaterialData opaque;
      opaque.setRoughnessConstant(0.375f);
      const MaterialData renderMaterial(opaque);
      const XXH64_hash_t renderMaterialHash = renderMaterial.getHash();

      DrawCallState drawCall;
      drawCall.isExternalDraw = isExternal;
      const XXH64_hash_t legacyMaterialHash = matchingMaterialHashes
        ? renderMaterialHash
        : renderMaterialHash ^ 0xa51e9274c83b60dfull;
      drawCall.modifyMaterialData().setHashOverride(legacyMaterialHash);

      RtxParticleSystemDesc desc;
      desc.maxNumParticles = 64;
      desc.minTimeToLive = 0.5f;
      desc.maxTimeToLive = 2.f;
      desc.spawnRatePerSecond = 10.f;
      desc.spawnDistribution = ParticleSpawnDistribution::BySurfaceArea;
      desc.minSize.push_back({ 0.25f, 0.5f });
      desc.maxSize.push_back({ 1.f, 2.f });
      const XXH64_hash_t descriptorHash = desc.calcHash();
      const XXH64_hash_t cacheKey = XXH3_64bits_withSeed(&isExternal, sizeof(isExternal), drawCall.getMaterialData().getHash() ^ descriptorHash);
      const XXH64_hash_t renderIdentity = XXH3_64bits_withSeed(&isExternal, sizeof(isExternal), renderMaterialHash ^ descriptorHash);

      auto system = std::make_shared<RtxParticleSystemManager::ParticleSystem>(
        desc, renderMaterial, drawCall.getMaterialData(), CategoryFlags{}, isExternal, 42);
      decltype(RtxParticleSystemManager::m_particleSystems) systems;
      systems.emplace(cacheKey, system);

      RtxParticleSystemManager::SpawnContext spawnContext{};
      spawnContext.particleSystemHash = system->getLookupHash();
      const auto found = systems.find(spawnContext.particleSystemHash);

      require(found != systems.end(), "A spawn context must find its particle system when the render material is replaced.");
      require(found->second == system, "A spawn context must resolve to the same cached particle system.");
      require(system->getLookupHash() == cacheKey, "Particle lookup identity must use the original draw material and particle descriptor.");
      require(system->getHash() == renderIdentity, "Particle rendering identity must retain the render material and particle descriptor.");
      require(system->materialData.getHash() == renderMaterialHash, "The particle system must retain its replacement render material.");
      require(system->legacyMaterialData.getHash() == legacyMaterialHash, "The particle system must retain its original draw material.");

      require(system->isExternalDraw == isExternal, "Particle draws must preserve their game/API origin.");
      const bool otherOrigin = !isExternal;
      const XXH64_hash_t otherKey = XXH3_64bits_withSeed(&otherOrigin, sizeof(otherOrigin), legacyMaterialHash ^ descriptorHash);
      require(system->getLookupHash() != otherKey, "Game and API emitters must retain distinct lookup identities.");

      if (matchingMaterialHashes) {
        require(system->getLookupHash() == system->getHash(), "Matching material hashes must preserve the Toolkit lookup control.");
      } else {
        require(system->getLookupHash() != system->getHash(), "Replacement materials must keep distinct lookup and rendering identities.");
        require(systems.find(system->getHash()) == systems.end(), "The rendering identity must not alias the original-material cache key.");
      }
    }
  };
}

int main() {
  try {
    dxvk::ParticleSystemLookupTestAccess::testEmitterSurvivesCompaction();
    for (const bool isExternal : { false, true }) {
      dxvk::ParticleSystemLookupTestAccess::testSpawnContextFindsSystem(false, isExternal);
      dxvk::ParticleSystemLookupTestAccess::testSpawnContextFindsSystem(true, isExternal);
    }
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "Particle system lookup tests passed." << std::endl;
  return 0;
}
