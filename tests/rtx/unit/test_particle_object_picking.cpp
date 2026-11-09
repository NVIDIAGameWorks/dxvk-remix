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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>

#include "../../../src/dxvk/rtx_render/rtx_materials.h"
#include "../../../src/dxvk/shaders/rtx/pass/particles/particle_system_binding_indices.h"

namespace dxvk {
  Logger Logger::s_instance("test_particle_object_picking.log");
}

// Picking IDs reuse simulation padding and have a separate draw-order buffer.
static_assert(sizeof(GpuParticle) == 48);
static_assert(offsetof(GpuParticle, rotation) == 40);
static_assert(PARTICLE_SYSTEM_BINDING_OBJECT_PICKING_OUTPUT != PARTICLE_SYSTEM_BINDING_SPAWN_DISTRIBUTION_OUTPUT);
static_assert(offsetof(GpuParticle, timeToLive) == 42);
static_assert(offsetof(GpuParticle, emitterObjectPickingValue) == 44);
static_assert(sizeof(GpuSpawnContext) == 144);
static_assert(offsetof(GpuSpawnContext, emitterObjectPickingValue) == 128);
static_assert(sizeof(ParticleVertex) == 24);
static_assert(offsetof(ParticleVertex, color) == 12);
static_assert(offsetof(ParticleVertex, texcoord) == 16);
static_assert(sizeof(decltype(GpuParticle::emitterObjectPickingValue)) == 4);
static_assert(sizeof(decltype(GpuSpawnContext::emitterObjectPickingValue)) == 4);

int main() {
  using namespace dxvk;

  RtSurface surface;
  surface.positionBufferIndex = 0;
  surface.associatedGeometryHash = 0;
  surface.clipPlane = Vector4(0.f);
  if (surface.particlePickingBufferIndex != kSurfaceInvalidBufferIndex) {
    std::cerr << "Ordinary surfaces must default to surface picking" << std::endl;
    return -1;
  }

  // Surface.data0b.z stores flags0. data2.x carries the
  // ordinary surface picking value or the particle buffer descriptor.
  // data2.w carries the particle flag independently.
  constexpr size_t kFlags0Offset = 12;
  constexpr size_t kPickingValueOffset = 32;
  constexpr size_t kFlags1Offset = 44;
  constexpr size_t kPrefixSize = 8;
  constexpr unsigned char kGuard = 0xcd;
  std::array<unsigned char, kSurfaceGPUSize + 2 * kPrefixSize> storage;

  // IDs share their low 24 bits: truncating the owner would make these two
  // emitters indistinguishable. Zero also covers an unpickable emitter.
  constexpr std::array<uint32_t, 4> kPickingValues = {
    0, 0x01000001u, 0xfe000001u, 0xffffffffu
  };

  for (const uint32_t pickingValue : kPickingValues) {
    surface.objectPickingValue = pickingValue;
    for (uint32_t flags = 0; flags < 8; ++flags) {
      surface.normalFormat = (flags & 1) ? VK_FORMAT_R32_UINT : VK_FORMAT_UNDEFINED;
      surface.isVertexColorBakedLighting = (flags & 2) != 0;
      surface.particlePickingBufferIndex = (flags & 4) ? 0xfffe : kSurfaceInvalidBufferIndex;

      // Game-provided particle geometry must retain surface picking unless it
      // explicitly carries an ID buffer. The alpha particle flag alone must
      // not authorize a per-particle buffer read.
      for (const bool isParticle : { false, true }) {
        for (const uint16_t primitivesPerParticle : { 2, 6 }) {
          surface.alphaState.isParticle = isParticle;
          surface.particlePickingPrimitivesPerParticle = primitivesPerParticle;
          storage.fill(kGuard);
          size_t offset = kPrefixSize;
          surface.writeGPUData(storage.data(), offset, SIZE_MAX, 0);

          uint16_t packedFlags0;
          uint32_t packedPickingValue;
          uint32_t packedFlags1;
          std::memcpy(&packedFlags0, storage.data() + kPrefixSize + kFlags0Offset, sizeof(packedFlags0));
          std::memcpy(&packedPickingValue, storage.data() + kPrefixSize + kPickingValueOffset, sizeof(packedPickingValue));
          std::memcpy(&packedFlags1, storage.data() + kPrefixSize + kFlags1Offset, sizeof(packedFlags1));

          const uint32_t expectedPickingData = (flags & 4)
            ? 0xfffeu | (uint32_t(primitivesPerParticle) << 16)
            : pickingValue;
          if (packedFlags0 != flags || packedPickingValue != expectedPickingData ||
              ((packedFlags1 & (1u << 21)) != 0) != isParticle ||
              offset != kPrefixSize + kSurfaceGPUSize) {
            std::cerr << "Invalid particle picking surface packing: flags=" << flags
                      << ", isParticle=" << isParticle << ", pickingValue=" << pickingValue << std::endl;
            return -1;
          }

          for (size_t i = 0; i < kPrefixSize; ++i) {
            if (storage[i] != kGuard || storage[kPrefixSize + kSurfaceGPUSize + i] != kGuard) {
              std::cerr << "Particle picking overwrote GPU surface bounds" << std::endl;
              return -1;
            }
          }
        }
      }
    }
  }

  std::cout << "Particle object picking packing passed" << std::endl;
  return 0;
}
