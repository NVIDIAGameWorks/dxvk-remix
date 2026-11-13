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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#define RTX_REMIX_PNEXT_CHECK_STRUCTS
#include "../../../src/dxvk/rtx_render/rtx_remix_pnext.h"
#include <remix/remix.h>

#include "../../../src/dxvk/rtx_render/rtx_types.h"
#include "rtx/pass/particles/particle_spawn_mask.h"

namespace dxvk {
  Logger Logger::s_instance("test_particle_spawn_mask.log");
}

namespace {
  using namespace dxvk;

  constexpr uint32_t kOff = uint32_t(ParticleSpawnMaskSource::Off);
  constexpr uint32_t kAlpha = uint32_t(ParticleSpawnMaskSource::Alpha);
  constexpr uint32_t kLuminance = uint32_t(ParticleSpawnMaskSource::Luminance);
  constexpr uint32_t kCombined = uint32_t(ParticleSpawnMaskSource::AlphaTimesLuminance);
  constexpr uint32_t kGrayscale = uint32_t(ParticleSpawnMaskResponse::Grayscale);
  constexpr uint32_t kCutout = uint32_t(ParticleSpawnMaskResponse::Cutout);
  const float kNan = std::numeric_limits<float>::quiet_NaN();
  const float kInf = std::numeric_limits<float>::infinity();

  void require(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void requireNear(float actual, float expected, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 0.000001f) {
      std::cerr << message << ": expected " << expected << ", got " << actual << std::endl;
      throw std::runtime_error(message);
    }
  }

  void requireUv(vec2 actual, vec2 expected, const char* message) {
    requireNear(actual.x, expected.x, message);
    requireNear(actual.y, expected.y, message);
  }

  void testSourceSelectsOnlyRequestedChannels() {
    requireNear(particleSpawnMaskWeight(0.25f, 0.5f, kOff), 1.f, "Off must have unit weight");
    requireNear(particleSpawnMaskWeight(0.25f, 0.5f, kAlpha), 0.25f, "Alpha weight");
    requireNear(particleSpawnMaskWeight(0.25f, 0.5f, kLuminance), 0.5f, "Luminance weight");
    requireNear(particleSpawnMaskWeight(0.25f, 0.5f, kCombined), 0.125f, "Combined weight");
    requireNear(particleSpawnMaskWeight(0.25f, kNan, kAlpha), 0.25f, "Alpha must ignore invalid luminance");
    requireNear(particleSpawnMaskWeight(kNan, 0.5f, kLuminance), 0.5f, "Luminance must ignore invalid alpha");
    requireNear(particleSpawnMaskWeight(1.f, 1.f, 4), 0.f, "Unknown mask sources must have no weight");
  }

  void testSelectedChannelsClampFiniteValuesAndRejectNonfiniteValues() {
    requireNear(particleSpawnMaskWeight(-0.5f, 2.f, kAlpha), 0.f, "Negative alpha must clamp to zero");
    requireNear(particleSpawnMaskWeight(2.f, -0.5f, kAlpha), 1.f, "Alpha above one must clamp to one");
    requireNear(particleSpawnMaskWeight(2.f, -0.5f, kLuminance), 0.f, "Negative luminance must clamp to zero");
    requireNear(particleSpawnMaskWeight(-0.5f, 2.f, kLuminance), 1.f, "Luminance above one must clamp to one");
    requireNear(particleSpawnMaskWeight(2.f, 0.25f, kCombined), 0.25f, "Combined mode must clamp each channel before multiplying");

    for (const float invalid : { kNan, kInf, -kInf }) {
      requireNear(particleSpawnMaskWeight(invalid, 1.f, kAlpha), 0.f, "Nonfinite alpha must have zero weight");
      requireNear(particleSpawnMaskWeight(1.f, invalid, kLuminance), 0.f, "Nonfinite luminance must have zero weight");
      requireNear(particleSpawnMaskWeight(invalid, 1.f, kCombined), 0.f, "Combined mode must reject nonfinite alpha");
      requireNear(particleSpawnMaskWeight(1.f, invalid, kCombined), 0.f, "Combined mode must reject nonfinite luminance");
      require(!particleSpawnMaskAccept(invalid, 1.f, kAlpha, kCutout, 0.f, 0.f), "Invalid alpha must not pass a zero cutoff");
      require(!particleSpawnMaskAccept(1.f, invalid, kLuminance, kGrayscale, 0.f, 0.f), "Invalid luminance must not pass probability zero");
    }
  }

  void testOffPreservesBirthsWithIrrelevantInvalidInputs() {
    for (const uint32_t response : { kGrayscale, kCutout, 99u }) {
      require(particleSpawnMaskAccept(kNan, kInf, kOff, response, kNan, kNan), "Off must preserve births without inspecting mask inputs");
      require(particleSpawnMaskAccept(0.f, 0.f, kOff, response, -kInf, 1.f), "Off must preserve black births and ignore the random sample");
    }
  }

  void testGrayscaleReducesBirthCountWithoutRetries() {
    constexpr uint32_t kSampleCount = 2048;
    struct Case {
      float alpha;
      float luminance;
      uint32_t source;
      uint32_t expectedCount;
    };
    const std::array<Case, 7> cases = {{
      { 0.f, 1.f, kAlpha, 0 }, { 1.f, 1.f, kAlpha, kSampleCount },
      { 0.25f, 1.f, kAlpha, kSampleCount / 4 }, { 0.5f, 1.f, kAlpha, kSampleCount / 2 },
      { 1.f, 0.75f, kLuminance, kSampleCount * 3 / 4 },
      { 0.5f, 0.5f, kCombined, kSampleCount / 4 }, { 0.25f, 0.5f, kCombined, kSampleCount / 8 },
    }};
    for (const Case& testCase : cases) {
      uint32_t accepted = 0;
      for (uint32_t i = 0; i < kSampleCount; ++i) {
        const float sample = (float(i) + 0.5f) / float(kSampleCount);
        const bool first = particleSpawnMaskAccept(testCase.alpha, testCase.luminance, testCase.source, kGrayscale, kNan, sample);
        const bool repeated = particleSpawnMaskAccept(testCase.alpha, testCase.luminance, testCase.source, kGrayscale, kNan, sample);
        require(first == repeated, "Repeated acceptance with the same random sample must be deterministic");
        accepted += uint32_t(first);
      }
      require(accepted == testCase.expectedCount, "One mask decision per birth must retain the expected grayscale fraction");
    }
  }

  void testGrayscaleUsesStrictProbabilityBoundaryAndValidSamples() {
    require(particleSpawnMaskAccept(0.5f, 1.f, kAlpha, kGrayscale, 1.f, std::nextafter(0.5f, 0.f)), "Samples immediately below the weight must pass");
    require(!particleSpawnMaskAccept(0.5f, 1.f, kAlpha, kGrayscale, 0.f, 0.5f), "Samples equal to the weight must be rejected");
    require(!particleSpawnMaskAccept(0.f, 1.f, kAlpha, kGrayscale, 0.f, 0.f), "Black must reject even random sample zero");
    require(particleSpawnMaskAccept(1.f, 1.f, kAlpha, kGrayscale, kInf, std::nextafter(1.f, 0.f)), "White must accept every valid sample independently of cutoff");
    for (const float invalidSample : { -0.01f, 1.f, 2.f, kNan, kInf, -kInf }) {
      require(!particleSpawnMaskAccept(1.f, 1.f, kAlpha, kGrayscale, 0.f, invalidSample), "Grayscale must reject samples outside the finite unit interval");
    }
  }

  void testRespawnEligibilityRetainsRejectedLifetimesAndLegacyOffBehavior() {
    struct Case {
      bool dead;
      bool sleeping;
      float timeToLive;
      bool legacyEligible;
      bool maskedRejectedEligible;
    };
    const std::array<Case, 12> cases = {{
      { false, false, 1.f, false, false },
      { false, true, 1.f, true, false },
      { true, false, 1.f, true, true },
      { true, true, 1.f, true, true },
      { false, false, 0.f, false, true },
      { false, true, 0.f, true, true },
      { true, false, 0.f, true, true },
      { true, true, 0.f, true, true },
      { false, false, -1.f, false, true },
      { false, true, -1.f, true, true },
      { true, false, -1.f, true, true },
      { true, true, -1.f, true, true },
    }};
    for (const Case& testCase : cases) {
      for (const bool rejected : { false, true }) {
        require(particleSpawnMaskCanRespawn(kOff, rejected, testCase.dead, testCase.sleeping, testCase.timeToLive) == testCase.legacyEligible,
                "Masking Off must preserve legacy dead-or-sleeping eligibility even for rejected slots");
        for (const uint32_t source : { kAlpha, kLuminance, kCombined }) {
          const bool expected = rejected ? testCase.maskedRejectedEligible : testCase.legacyEligible;
          require(particleSpawnMaskCanRespawn(source, rejected, testCase.dead, testCase.sleeping, testCase.timeToLive) == expected,
                  "Enabled masks must hold rejected slots until expiry or death and preserve ordinary slot eligibility");
        }
      }
    }
  }

  void testConstantCountDoesNotRefillRejectedGraySlotsBeforeExpiry() {
    constexpr uint32_t kSlotCount = 64;
    constexpr float kFrameTime = 1.f / 64.f;
    struct Slot {
      bool dead = true;
      bool sleeping = false;
      bool rejected = false;
      float timeToLive = 0.f;
    };
    std::array<Slot, kSlotCount> slots;
    uint32_t attempts = 0;

    const auto visitAllSlots = [&](uint32_t frame) {
      uint32_t frameAttempts = 0;
      for (uint32_t i = 0; i < kSlotCount; ++i) {
        Slot& slot = slots[i];
        if (!particleSpawnMaskCanRespawn(kAlpha, slot.rejected, slot.dead, slot.sleeping, slot.timeToLive)) {
          continue;
        }
        ++frameAttempts;
        ++attempts;
        // The second frame would accept every initially rejected slot if sleeping allowed an immediate retry.
        const float sample = (float((i + frame * (kSlotCount / 2)) % kSlotCount) + 0.5f) / float(kSlotCount);
        const bool accepted = particleSpawnMaskAccept(0.5f, 1.f, kAlpha, kGrayscale, 0.5f, sample);
        slot.dead = false;
        slot.rejected = !accepted;
        slot.sleeping = !accepted;
        slot.timeToLive = 1.f;
      }
      return frameAttempts;
    };
    const auto visibleCount = [&] {
      uint32_t count = 0;
      for (const Slot& slot : slots) {
        count += uint32_t(!slot.dead && !slot.rejected);
      }
      return count;
    };

    for (uint32_t frame = 0; frame < 20; ++frame) {
      const uint32_t frameAttempts = visitAllSlots(frame);
      require(frameAttempts == (frame == 0 ? kSlotCount : 0), "Constant-count mode must make no repeated birth attempts during rejected slots' positive lifetimes");
      require(visibleCount() == kSlotCount / 2, "A half-gray mask must remain half-populated over 20 frames instead of gradually filling every slot");
      for (Slot& slot : slots) {
        slot.timeToLive -= kFrameTime;
        require(slot.timeToLive > 0.f, "The repeated-frame regression must remain before normal lifetime expiry");
      }
    }
    require(attempts == kSlotCount, "Each candidate slot must receive only one mask sample before expiry");

    // Advance all slots to normal expiry without marking them dead to exercise the lifetime branch directly.
    for (Slot& slot : slots) {
      slot.timeToLive = 0.f;
      slot.sleeping = true;
    }
    require(visitAllSlots(21) == kSlotCount && attempts == 2 * kSlotCount, "Expired slots, including rejected slots, must be eligible for a fresh birth attempt");
    require(visibleCount() == kSlotCount / 2, "A fresh lifetime must still apply one half-gray decision per candidate");
    require(!slots[kSlotCount - 1].rejected, "A formerly rejected slot must be able to accept a new sample after normal expiry");
  }

  void testCutoutKeepsBlackExcludedAtZeroAndClampsThreshold() {
    for (const uint32_t source : { kAlpha, kLuminance, kCombined }) {
      for (const float threshold : { -1.f, 0.f, 0.5f, 1.f, 2.f }) {
        require(!particleSpawnMaskAccept(0.f, 0.f, source, kCutout, threshold, kNan), "Black must remain excluded even with a nonpositive cutoff");
        require(particleSpawnMaskAccept(1.f, 1.f, source, kCutout, threshold, kNan), "White must pass every finite clamped cutoff without a random sample");
      }
      for (const float invalidThreshold : { kNan, kInf, -kInf }) {
        require(!particleSpawnMaskAccept(1.f, 1.f, source, kCutout, invalidThreshold, 0.f), "Nonfinite cutoffs must reject");
      }
    }
    require(particleSpawnMaskAccept(0.5f, 1.f, kAlpha, kCutout, 0.5f, kInf), "Cutout equality must pass and ignore random samples");
    require(!particleSpawnMaskAccept(0.5f, 1.f, kAlpha, kCutout, std::nextafter(0.5f, 1.f), 0.f), "A weight immediately below cutoff must be rejected");
    require(particleSpawnMaskAccept(0.5f, 0.5f, kCombined, kCutout, 0.25f, 0.f), "Combined mode must compare its product against cutoff");
  }

  void testUnknownModesRejectInsteadOfEnablingBirths() {
    for (const uint32_t source : { 4u, std::numeric_limits<uint32_t>::max() }) {
      require(!particleSpawnMaskAccept(1.f, 1.f, source, kGrayscale, 0.f, 0.f), "Unknown sources must reject grayscale births");
      require(!particleSpawnMaskAccept(1.f, 1.f, source, kCutout, 0.f, 0.f), "Unknown sources must reject cutout births");
    }
    require(!particleSpawnMaskAccept(1.f, 1.f, kAlpha, 2, 0.f, 0.f), "Unknown responses must reject enabled masks");
  }

  void testSourceUvInterpolationAndAffineTransform() {
    const vec2 a(-1.f, 0.f);
    const vec2 b(3.f, 0.f);
    const vec2 c(-1.f, 4.f);
    requireUv(particleSpawnMaskInterpolateUv(a, b, c, vec3(1.f, 0.f, 0.f)), a, "First UV vertex");
    requireUv(particleSpawnMaskInterpolateUv(a, b, c, vec3(0.f, 1.f, 0.f)), b, "Second UV vertex");
    requireUv(particleSpawnMaskInterpolateUv(a, b, c, vec3(0.f, 0.f, 1.f)), c, "Third UV vertex");
    const vec2 uv = particleSpawnMaskInterpolateUv(a, b, c, vec3(0.25f, 0.25f, 0.5f));
    requireUv(uv, vec2(0.f, 2.f), "The source sample must use barycentric UV interpolation");

    const vec4 rowU(2.f, 3.f, 0.5f, 4.f);
    const vec4 rowV(-1.f, 0.5f, 0.25f, -2.f);
    const vec4 sprite(0.5f, 0.25f, 0.5f, 0.75f);
    requireUv(particleSpawnMaskUv(uv, rowU, rowV, sprite, false), vec2(10.5f, -0.75f), "Unanimated masks must retain affine UVs without applying sprite bounds");
    requireUv(particleSpawnMaskUv(uv, rowU, rowV, sprite, true), vec2(0.75f, 0.8125f), "Animated masks must wrap negative and positive UVs before selecting a sprite frame");
    requireUv(uv, vec2(0.f, 2.f), "Sampling the source mask must leave source UV data unchanged");
  }

  void testDescriptorDefaultsHashAndGpuTransfer() {
    RtxParticleSystemDesc desc;
    require(desc.spawnMaskSource == kOff, "New particle systems must default to masking Off");
    require(desc.spawnMaskResponse == kGrayscale && desc.spawnMaskThreshold == 0.5f, "Mask response and cutoff defaults");
    const XXH64_hash_t defaultHash = desc.calcHash();
    desc.spawnMaskSource = kCombined;
    require(desc.calcHash() != defaultHash, "Mask source must participate in particle system identity");
    const XXH64_hash_t sourceHash = desc.calcHash();
    desc.spawnMaskResponse = kCutout;
    require(desc.calcHash() != sourceHash, "Mask response must participate in particle system identity");
    const XXH64_hash_t responseHash = desc.calcHash();
    desc.spawnMaskThreshold = 0.375f;
    require(desc.calcHash() != responseHash, "Mask cutoff must participate in particle system identity");

    const GpuParticleSystem system(desc);
    require(system.desc.spawnMaskSource == kCombined && system.desc.spawnMaskResponse == kCutout && system.desc.spawnMaskThreshold == 0.375f,
            "GPU descriptor construction must preserve the authored mask configuration");
  }

  void testGpuMaskLayoutAndPackedValues() {
    static_assert(sizeof(GpuParticleSystemDesc) == 128);
    static_assert(offsetof(GpuParticleSystemDesc, minScreenSize) == 96);
    static_assert(offsetof(GpuParticleSystemDesc, maxScreenSize) == 100);
    static_assert(offsetof(GpuParticleSystemDesc, screenSizeCoverage) == 104);
    static_assert(offsetof(GpuParticleSystemDesc, spawnMaskSource) == 112);
    static_assert(offsetof(GpuParticleSystemDesc, spawnMaskResponse) == 116);
    static_assert(offsetof(GpuParticleSystemDesc, spawnMaskThreshold) == 120);
    static_assert(offsetof(GpuParticleSystemDesc, spawnMaskPad) == 124);
    RtxParticleSystemDesc desc;
    desc.spawnMaskSource = kCombined;
    desc.spawnMaskResponse = kCutout;
    desc.spawnMaskThreshold = 0.375f;
    std::array<unsigned char, 128> packed{};
    std::memcpy(packed.data(), &desc.getGpuDesc(), packed.size());
    uint32_t source;
    uint32_t response;
    float threshold;
    std::memcpy(&source, packed.data() + 112, sizeof(source));
    std::memcpy(&response, packed.data() + 116, sizeof(response));
    std::memcpy(&threshold, packed.data() + 120, sizeof(threshold));
    require(source == kCombined && response == kCutout && threshold == 0.375f, "Packed mask fields must occupy the shared CPU/Slang descriptor offsets");

    static_assert(sizeof(GpuSpawnContext) == 224);
    static_assert(offsetof(GpuSpawnContext, emitterObjectPickingValue) == 128);
    static_assert(offsetof(GpuSpawnContext, spawnMaskUvU) == 144);
    static_assert(offsetof(GpuSpawnContext, spawnMaskUvV) == 160);
    static_assert(offsetof(GpuSpawnContext, spawnMaskSprite) == 176);
    static_assert(offsetof(GpuSpawnContext, spawnMaskConstant) == 192);
    static_assert(offsetof(GpuSpawnContext, spawnMaskStatus) == 208);
    static_assert(offsetof(GpuSpawnContext, spawnMaskTextureIdx) == 212);
    static_assert(offsetof(GpuSpawnContext, spawnMaskSamplerIdx) == 214);
    static_assert(offsetof(GpuSpawnContext, spawnMaskAnimated) == 216);
    static_assert(offsetof(GpuSpawnContext, spawnMaskSrgb) == 220);
    static_assert(sizeof(ParticleSpawnDistributionConstants) == 240);
  }

  void testApiMaskDefaultsAndOptionalChain() {
    auto mask = remix::InstanceInfoParticleSpawnMaskEXT{};
    require(mask.sType == REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SPAWN_MASK_EXT && mask.pNext == nullptr,
            "The mask wrapper must initialize its type and chain pointer");
    require(mask.maskSource == REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_OFF && mask.maskResponse == REMIXAPI_PARTICLE_SPAWN_MASK_RESPONSE_GRAYSCALE && mask.maskThreshold == 0.5f,
            "API mask defaults must match runtime defaults");
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_OFF) == kOff);
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_ALPHA) == kAlpha);
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_LUMINANCE) == kLuminance);
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_ALPHA_TIMES_LUMINANCE) == kCombined);
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_RESPONSE_GRAYSCALE) == kGrayscale);
    static_assert(uint32_t(REMIXAPI_PARTICLE_SPAWN_MASK_RESPONSE_CUTOUT) == kCutout);
    static_assert(REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SPAWN_MASK_EXT == 33);

    auto info = remixapi_InstanceInfo{};
    info.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    auto particles = remix::InstanceInfoParticleSystemEXT{};
    auto distribution = remix::InstanceInfoParticleSpawnDistributionEXT{};
    distribution.spawnDistribution = REMIXAPI_PARTICLE_SPAWN_DISTRIBUTION_BY_SURFACE_AREA;
    info.pNext = &particles;
    particles.pNext = &distribution;
    require(pnext::find<remixapi_InstanceInfoParticleSpawnMaskEXT>(&info) == nullptr, "Existing particle chains must work without a mask extension");

    mask.maskSource = REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_ALPHA_TIMES_LUMINANCE;
    mask.maskResponse = REMIXAPI_PARTICLE_SPAWN_MASK_RESPONSE_CUTOUT;
    mask.maskThreshold = 0.375f;
    for (const bool maskFirst : { false, true }) {
      info.pNext = maskFirst ? static_cast<void*>(&mask) : static_cast<void*>(&particles);
      mask.pNext = maskFirst ? static_cast<void*>(&particles) : static_cast<void*>(&distribution);
      particles.pNext = maskFirst ? static_cast<void*>(&distribution) : static_cast<void*>(&mask);
      const auto* result = pnext::find<remixapi_InstanceInfoParticleSpawnMaskEXT>(&info);
      require(result == &mask && result->maskSource == REMIXAPI_PARTICLE_SPAWN_MASK_SOURCE_ALPHA_TIMES_LUMINANCE && result->maskResponse == REMIXAPI_PARTICLE_SPAWN_MASK_RESPONSE_CUTOUT && result->maskThreshold == 0.375f,
              "The mask must retain its values in either particle extension chain order");
      require(pnext::find<remixapi_InstanceInfoParticleSystemEXT>(&info) == &particles && pnext::find<remixapi_InstanceInfoParticleSpawnDistributionEXT>(&info) == &distribution,
              "Masking must coexist with particle settings and surface area distribution");
    }
  }
}

int main() {
  try {
    testSourceSelectsOnlyRequestedChannels();
    testSelectedChannelsClampFiniteValuesAndRejectNonfiniteValues();
    testOffPreservesBirthsWithIrrelevantInvalidInputs();
    testGrayscaleReducesBirthCountWithoutRetries();
    testGrayscaleUsesStrictProbabilityBoundaryAndValidSamples();
    testRespawnEligibilityRetainsRejectedLifetimesAndLegacyOffBehavior();
    testConstantCountDoesNotRefillRejectedGraySlotsBeforeExpiry();
    testCutoutKeepsBlackExcludedAtZeroAndClampsThreshold();
    testUnknownModesRejectInsteadOfEnablingBirths();
    testSourceUvInterpolationAndAffineTransform();
    testDescriptorDefaultsHashAndGpuTransfer();
    testGpuMaskLayoutAndPackedValues();
    testApiMaskDefaultsAndOptionalChain();
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "Particle spawn mask tests passed." << std::endl;
  return 0;
}
