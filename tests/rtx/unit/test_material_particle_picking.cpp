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
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../../../nv-private/hdremix/particlePicking.h"
#include "../../../src/dxvk/rtx_render/rtx_types.h"

namespace dxvk {
  Logger Logger::s_instance("test_material_particle_picking.log");
}

namespace {
  using namespace pxr;
  using namespace pxr::hdRemixPicking;

  void require(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void testSharedMaterialKeepsStablePickingValue() {
    MaterialPickingRegistry registry;
    const SdfPath material("/Looks/Fire");
    const SdfPath otherMaterial("/OtherLooks/Fire");

    const auto firstMeshOwner = registry.registerMaterial(material);
    const auto secondMeshOwner = registry.registerMaterial(SdfPath("/Looks/Fire"));
    const auto otherOwner = registry.registerMaterial(otherMaterial);

    require(firstMeshOwner.has_value(), "A material must receive a picking value.");
    require(firstMeshOwner == secondMeshOwner, "Meshes sharing a material must share its persistent picking value.");
    require(otherOwner.has_value() && otherOwner != firstMeshOwner, "Distinct material paths must have distinct picking values.");
    require(*firstMeshOwner >= 0x80000000u && *firstMeshOwner <= 0xfffffffeu, "Material picking values must use the reserved high range.");
    require(*otherOwner >= 0x80000000u && *otherOwner <= 0xfffffffeu, "Every material picking value must use the reserved high range.");
    require(registry.findValue(material) == firstMeshOwner, "Forward material lookup must return the registered value.");
    const auto* path = registry.findMaterial(*firstMeshOwner);
    require(path != nullptr && *path == material, "A picking value must resolve to the full material path.");
    const auto* otherPath = registry.findMaterial(*otherOwner);
    require(otherPath != nullptr && *otherPath == otherMaterial, "Materials with the same leaf name must remain distinguishable.");
  }

  void testRegistrationRetainsPreviousMaterialPickingValue() {
    MaterialPickingRegistry registry;
    const SdfPath previousMaterial("/Looks/PreviousEmitter");
    const SdfPath currentMaterial("/Looks/CurrentEmitter");
    const auto previousOwner = registry.registerMaterial(previousMaterial);

    const auto currentOwner = registry.registerMaterial(currentMaterial);
    const auto restoredOwner = registry.registerMaterial(previousMaterial);

    require(previousOwner.has_value() && currentOwner.has_value(), "Both emitter materials must be registered.");
    require(currentOwner != previousOwner, "A different bound material must receive a different owner.");
    require(restoredOwner == previousOwner, "Rebinding a previous material must reuse its original owner.");
    require(registry.findValue(previousMaterial) == previousOwner, "Registering a new material must retain the previous mapping.");
    const auto* previousPath = registry.findMaterial(*previousOwner);
    require(previousPath != nullptr && *previousPath == previousMaterial, "Live particles must still resolve their previous material owner.");
    const auto* currentPath = registry.findMaterial(*currentOwner);
    require(currentPath != nullptr && *currentPath == currentMaterial, "Restoring a previous material must retain the newer mapping.");
  }

  void testRegistryRejectsMissingMaterialsAndUnknownValues() {
    MaterialPickingRegistry registry;
    const SdfPath material("/Looks/Emitter");
    require(registry.empty(), "A fresh registry must not enable particle-neighborhood requests.");
    require(!registry.findValue(material).has_value(), "An unregistered material must not have a picking value.");
    require(!registry.registerMaterial(SdfPath()).has_value(), "An empty material path must not allocate a picking value.");
    require(!registry.findValue(SdfPath()).has_value(), "An empty material path must not have a registry entry.");
    require(registry.empty(), "Rejected empty material paths must leave neighborhood queries disabled.");
    require(registry.findMaterial(0x80000000u) == nullptr, "An unallocated material picking value must not resolve.");

    const auto owner = registry.registerMaterial(material);
    require(owner.has_value() && *owner == 0x80000000u, "Rejected empty paths must not consume the first material picking value.");
    require(!registry.empty(), "Registering a material must enable particle-neighborhood requests.");
    for (const uint32_t unknownValue : { 0u, 1u, 0x00ffffffu, 0x7fffffffu, 0x80000001u, 0xffffffffu }) {
      require(registry.findMaterial(unknownValue) == nullptr, "Unknown, mesh, and sentinel picking values must not resolve to a material.");
    }
  }

  void testMaterialAllocatorKeepsRangeAndDoesNotWrap() {
    static_assert(kFirstMaterialPickingValue == 0x80000000u);
    static_assert(kLastMaterialPickingValue == 0xfffffffeu);
    uint64_t nextValue = kFirstMaterialPickingValue;
    const auto first = allocateMaterialPickingValue(nextValue);
    require(first.has_value() && *first == 0x80000000u && nextValue == 0x80000001ull, "The allocator must start at the reserved high range.");

    nextValue = 0xfffffffdull;
    const auto penultimate = allocateMaterialPickingValue(nextValue);
    const auto last = allocateMaterialPickingValue(nextValue);
    require(penultimate.has_value() && *penultimate == 0xfffffffdu, "The penultimate material value must retain all 32 bits.");
    require(last.has_value() && *last == 0xfffffffeu && nextValue == 0xffffffffull, "The allocator must issue the final valid value without issuing the sentinel.");

    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
      require(!allocateMaterialPickingValue(nextValue).has_value(), "An exhausted allocator must not wrap or issue the sentinel.");
      require(nextValue == 0xffffffffull, "Repeated exhausted allocation must keep the cursor outside the valid range.");
    }

    constexpr std::array<uint64_t, 5> kInvalidCursors = {
      0, 0x7fffffffull, 0xffffffffull, 0x100000000ull, std::numeric_limits<uint64_t>::max()
    };
    for (const uint64_t invalidCursor : kInvalidCursors) {
      nextValue = invalidCursor;
      require(!allocateMaterialPickingValue(nextValue).has_value(), "Out-of-range cursors must not allocate a truncated material value.");
      require(nextValue == invalidCursor, "Rejected allocation must leave the cursor unchanged.");
    }
  }

  void testHydraPickingValuesKeepSupportedMeshRange() {
    static_assert(kMaxHydraPickingValue == 0x00ffffffu);
    for (const int32_t supportedValue : { 0, 1, 0x0000ffff, 0x00ffffff }) {
      require(isHydraPickingValue(supportedValue), "Every supported 24-bit Hydra mesh ID, including zero, must remain valid.");
    }
    for (const int32_t unsupportedValue : { -1, std::numeric_limits<int32_t>::min(), 0x01000000, std::numeric_limits<int32_t>::max() }) {
      require(!isHydraPickingValue(unsupportedValue), "Unsupported Hydra IDs must be rejected instead of truncated into mesh or material owners.");
    }
  }

  void testMeshEmitterTakesPrecedenceOverMaterialEmitter() {
    require(getParticleEmitterSource(false, false) == ParticleEmitterSource::None, "No particle settings must produce no emitter owner.");
    require(getParticleEmitterSource(true, false) == ParticleEmitterSource::Mesh, "Mesh settings must select the mesh owner.");
    require(getParticleEmitterSource(false, true) == ParticleEmitterSource::Material, "Bound material settings must provide the fallback owner.");
    require(getParticleEmitterSource(true, true) == ParticleEmitterSource::Mesh, "Mesh settings must win when both mesh and bound material define an emitter.");
  }

  void testPickingRectanglesNormalizeAndClampCoordinates() {
    struct RectCase {
      std::array<uint32_t, 4> coordinates;
      PickingRect expected;
    };
    constexpr std::array<RectCase, 4> kCases = {{
      { { 31, 71, 30, 70 }, { 30, 70, 31, 71 } },
      { { 0, 0, 1, 1 }, { 0, 0, 1, 1 } },
      { { std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 65534, 65534 }, { 65534, 65534, 65535, 65535 } },
      { { std::numeric_limits<uint32_t>::max(), 2, 65535, 1 }, { 65535, 1, 65535, 2 } },
    }};

    for (const auto& test : kCases) {
      const auto actual = makePickingRect(test.coordinates[0], test.coordinates[1], test.coordinates[2], test.coordinates[3]);

      require(actual.left == test.expected.left && actual.top == test.expected.top &&
              actual.right == test.expected.right && actual.bottom == test.expected.bottom,
              "Picking requests must normalize reversed corners and clamp wide unsigned coordinates without wrapping.");
    }
  }

  void testPointPickingRejectsAreaAndInvalidRectangles() {
    constexpr std::array<PickingRect, 3> kPointCases = {{
      { 30, 70, 31, 71 }, { 0, 0, 1, 1 }, { 65534, 65534, 65535, 65535 },
    }};
    for (const auto& rectangle : kPointCases) {
      require(isPointPickingRect(rectangle), "Every valid one-pixel rectangle, including border pixels, must be a point pick.");
    }

    constexpr std::array<PickingRect, 9> kNonPointCases = {{
      { 30, 70, 33, 73 }, { 30, 70, 31, 73 }, { 30, 70, 33, 71 },
      { 0, 0, 0, 1 }, { 0, 0, 1, 0 }, { 2, 2, 1, 1 },
      { -1, 0, 0, 1 }, { 65535, 0, 65536, 1 },
      { std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max() },
    }};
    for (const auto& rectangle : kNonPointCases) {
      require(!isPointPickingRect(rectangle), "Area, thin box, empty, inverted, and out-of-range rectangles must not receive point tolerance.");
    }
  }

  void testPointPickingNeighborhoodClampsAtCoordinateLimits() {
    struct RectCase {
      PickingRect exact;
      PickingRect expected;
    };
    constexpr std::array<RectCase, 5> kCases = {{
      { { 30, 70, 31, 71 }, { 29, 69, 32, 72 } },
      { { 0, 0, 1, 1 }, { 0, 0, 2, 2 } },
      { { 65534, 65534, 65535, 65535 }, { 65533, 65533, 65535, 65535 } },
      { { 30, 70, 33, 73 }, { 30, 70, 33, 73 } },
      { { -1, 0, 0, 1 }, { -1, 0, 0, 1 } },
    }};

    for (const auto& test : kCases) {
      const auto actual = expandPointPickingRect(test.exact);

      require(actual.left == test.expected.left && actual.top == test.expected.top &&
              actual.right == test.expected.right && actual.bottom == test.expected.bottom,
              "Point tolerance must add one pixel per side, clamp borders, and leave other requests unchanged.");
    }
  }

  void testPointPickingKeepsExactMaterialAheadOfNearbyMaterial() {
    MaterialPickingRegistry registry;
    const auto nearbyOwner = registry.registerMaterial(SdfPath("/Looks/NearbyEmitter"));
    const auto exactOwner = registry.registerMaterial(SdfPath("/Looks/ExactEmitter"));
    require(nearbyOwner.has_value() && exactOwner.has_value(), "Both material owners must be registered.");
    const auto isLiveMaterial = [&registry](uint32_t value) { return registry.findMaterial(value) != nullptr; };
    const std::vector<uint32_t> exactValues = { 42, *exactOwner };
    const std::vector<uint32_t> nearbyValues = { *nearbyOwner, 16 };

    const auto selected = selectPickingValues(PickingRect{ 30, 70, 31, 71 }, exactValues, nearbyValues, isLiveMaterial);

    require(selected == std::vector<uint32_t>{ *exactOwner }, "An exact-pixel material must win over a lower-ID material in its neighborhood.");
  }

  void testPointPickingChoosesOnlyRegisteredLiveNearbyMaterial() {
    MaterialPickingRegistry registry;
    const auto removedOwner = registry.registerMaterial(SdfPath("/Looks/RemovedEmitter"));
    const SdfPath liveMaterial("/Looks/LiveEmitter");
    const auto liveOwner = registry.registerMaterial(liveMaterial);
    require(removedOwner.has_value() && liveOwner.has_value(), "Removed and live owners must have distinct retained registry entries.");
    const auto isLiveMaterial = [&registry, &liveMaterial](uint32_t value) {
      const auto* path = registry.findMaterial(value);
      return path != nullptr && *path == liveMaterial;
    };
    const std::vector<uint32_t> exactValues = { *removedOwner, 42 };
    const std::vector<uint32_t> nearbyValues = { 0, 42, *removedOwner, kFirstMaterialPickingValue + 20, kInvalidPickingValue, *liveOwner };

    const auto selected = selectPickingValues(PickingRect{ 30, 70, 31, 71 }, exactValues, nearbyValues, isLiveMaterial);

    require(selected == std::vector<uint32_t>{ *liveOwner }, "Nearby mesh, deleted-material, unregistered, and sentinel IDs must not displace a live material owner.");
  }

  void testPointPickingCanSelectNearbyMaterialWhenExactPixelIsEmpty() {
    MaterialPickingRegistry registry;
    const auto owner = registry.registerMaterial(SdfPath("/Looks/ThinParticleEmitter"));
    require(owner.has_value(), "The thin particle's owner must be registered.");
    const auto isLiveMaterial = [&registry](uint32_t value) { return registry.findMaterial(value) != nullptr; };

    const auto selected = selectPickingValues(PickingRect{ 30, 70, 31, 71 }, {}, { *owner }, isLiveMaterial);

    require(selected == std::vector<uint32_t>{ *owner }, "A visible thin particle must remain selectable when the exact pixel misses all geometry.");
  }

  void testPointPickingPreservesExactMeshValuesWithoutLiveNearbyMaterial() {
    MaterialPickingRegistry registry;
    const auto removedOwner = registry.registerMaterial(SdfPath("/Looks/RemovedEmitter"));
    require(removedOwner.has_value(), "A removed material must retain its historical registry entry.");
    const auto isLiveMaterial = [](uint32_t) { return false; };
    const std::array<std::vector<uint32_t>, 4> exactResults = {{ {}, { 0 }, { 42 }, { 42, 0, 42 } }};
    const std::vector<uint32_t> nearbyValues = { 3, *removedOwner, kFirstMaterialPickingValue + 20, kInvalidPickingValue };

    for (const auto& exactValues : exactResults) {
      const auto selected = selectPickingValues(PickingRect{ 30, 70, 31, 71 }, exactValues, nearbyValues, isLiveMaterial);

      require(selected == exactValues, "A point pick without a live nearby material must preserve the exact result, including zero and empty hits.");
    }
  }

  void testPointPickingPreservesExactResultForEmptyNeighborhood() {
    const auto isLiveMaterial = [](uint32_t value) { return value == kFirstMaterialPickingValue; };
    const std::vector<uint32_t> exactValues = { 0, 42 };

    const auto selected = selectPickingValues(PickingRect{ 30, 70, 31, 71 }, exactValues, {}, isLiveMaterial);

    require(selected == exactValues, "An empty or failed neighborhood readback must preserve the original mesh result.");
  }

  void testAreaPickingPreservesAllExactValues() {
    MaterialPickingRegistry registry;
    const auto firstOwner = registry.registerMaterial(SdfPath("/Looks/FirstEmitter"));
    const auto secondOwner = registry.registerMaterial(SdfPath("/Looks/SecondEmitter"));
    require(firstOwner.has_value() && secondOwner.has_value(), "Both area-pick material owners must be registered.");
    const auto isLiveMaterial = [&registry](uint32_t value) { return registry.findMaterial(value) != nullptr; };
    const std::vector<uint32_t> exactValues = { 42, *secondOwner, 0, *firstOwner, 42 };
    const std::vector<uint32_t> nearbyValues = { *firstOwner };

    const auto selected = selectPickingValues(PickingRect{ 30, 70, 34, 73 }, exactValues, nearbyValues, isLiveMaterial);

    require(selected == exactValues, "Area picks must preserve all mesh and material results without point prioritization or reordering.");
  }

  void testPreferredMaterialIsDeterministicAcrossInputOrdering() {
    constexpr uint32_t kEarlierOwner = kFirstMaterialPickingValue + 7;
    constexpr uint32_t kLaterOwner = kFirstMaterialPickingValue + 12;
    const auto isLiveMaterial = [kEarlierOwner, kLaterOwner](uint32_t value) { return value == kEarlierOwner || value == kLaterOwner; };
    const std::array<std::vector<uint32_t>, 3> permutations = {{
      { kLaterOwner, 42, kEarlierOwner, kLaterOwner },
      { kEarlierOwner, kLaterOwner, 42, kEarlierOwner },
      { 42, kEarlierOwner, kLaterOwner },
    }};

    for (const auto& values : permutations) {
      const auto selected = findPreferredMaterialPickingValue(values, isLiveMaterial);

      require(selected.has_value() && *selected == kEarlierOwner, "Overlapping live material candidates must select the same owner regardless of readback ordering or duplicates.");
    }
  }

  void testPreferredMaterialRetainsFullWidthBoundaryValue() {
    const auto isLiveMaterial = [](uint32_t value) { return value == kLastMaterialPickingValue; };
    const std::vector<uint32_t> values = { 0, kMaxHydraPickingValue, 0x7fffffffu, kInvalidPickingValue, kLastMaterialPickingValue };

    const auto selected = findPreferredMaterialPickingValue(values, isLiveMaterial);

    require(selected.has_value() && *selected == kLastMaterialPickingValue, "The final valid 32-bit material owner must survive selection without truncation or sentinel confusion.");
  }

  void testPreferredMaterialRejectsMeshAndSentinelNamespace() {
    const auto isLiveMaterial = [](uint32_t) { return true; };
    const std::vector<uint32_t> values = { 0, 1, kMaxHydraPickingValue, 0x01000000u, 0x7fffffffu, kInvalidPickingValue };

    const auto selected = findPreferredMaterialPickingValue(values, isLiveMaterial);

    require(!selected.has_value(), "Mesh, unreserved, and sentinel IDs must never become material owners even if the liveness predicate accepts them.");
  }

  void testDrawCallParticlePickingFallsBackOnlyWhenAbsent() {
    dxvk::DrawCallState drawCall;
    require(!drawCall.particleObjectPickingValue.has_value(), "Ordinary draw calls must default to no particle picking override.");
    constexpr std::array<uint32_t, 5> kPickingValues = {
      0, 0x00abcdefu, 0x80abcdefu, 0xfeabcdefu, 0xffffffffu
    };

    for (const uint32_t drawCallId : kPickingValues) {
      drawCall.drawCallID = drawCallId;
      drawCall.particleObjectPickingValue.reset();
      require(drawCall.getParticleObjectPickingValue() == drawCallId, "Absent particle picking must retain the complete ordinary draw call ID.");
      for (const uint32_t particleOwner : kPickingValues) {
        drawCall.particleObjectPickingValue = particleOwner;
        require(drawCall.getParticleObjectPickingValue() == particleOwner, "An explicit particle owner, including zero, must override the draw call ID without truncation.");
        require(drawCall.drawCallID == drawCallId, "Particle picking overrides must not change ordinary surface picking.");
      }
    }
  }

}

int main() {
  try {
    testSharedMaterialKeepsStablePickingValue();
    testRegistrationRetainsPreviousMaterialPickingValue();
    testRegistryRejectsMissingMaterialsAndUnknownValues();
    testMaterialAllocatorKeepsRangeAndDoesNotWrap();
    testHydraPickingValuesKeepSupportedMeshRange();
    testMeshEmitterTakesPrecedenceOverMaterialEmitter();
    testPickingRectanglesNormalizeAndClampCoordinates();
    testPointPickingRejectsAreaAndInvalidRectangles();
    testPointPickingNeighborhoodClampsAtCoordinateLimits();
    testPointPickingKeepsExactMaterialAheadOfNearbyMaterial();
    testPointPickingChoosesOnlyRegisteredLiveNearbyMaterial();
    testPointPickingCanSelectNearbyMaterialWhenExactPixelIsEmpty();
    testPointPickingPreservesExactMeshValuesWithoutLiveNearbyMaterial();
    testPointPickingPreservesExactResultForEmptyNeighborhood();
    testAreaPickingPreservesAllExactValues();
    testPreferredMaterialIsDeterministicAcrossInputOrdering();
    testPreferredMaterialRetainsFullWidthBoundaryValue();
    testPreferredMaterialRejectsMeshAndSentinelNamespace();
    testDrawCallParticlePickingFallsBackOnlyWhenAbsent();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All material particle picking tests passed." << std::endl;
  return 0;
}
