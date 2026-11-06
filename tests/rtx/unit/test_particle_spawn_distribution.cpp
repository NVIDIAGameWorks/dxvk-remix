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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "rtx/pass/particles/particle_spawn_distribution.h"
#include "../../../src/dxvk/rtx_render/rtx_particle_spawn_cache.h"

namespace {

  void expect(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void expectNear(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
      std::cerr << message << ": expected " << expected << ", got " << actual << ".\n";
      throw std::runtime_error(message);
    }
  }

  std::vector<float> makeDistribution(const std::vector<float>& weights, uint32_t offset = 0) {
    const uint32_t count = uint32_t(weights.size());
    const uint32_t blockCount = (count + dxvk::kParticleDistributionBlockSize - 1) / dxvk::kParticleDistributionBlockSize;
    std::vector<float> distribution(offset + count + blockCount + 8, -1234.0f);
    float total = 0.0f;

    for (uint32_t block = 0; block < blockCount; ++block) {
      float blockTotal = 0.0f;
      const uint32_t begin = block * dxvk::kParticleDistributionBlockSize;
      const uint32_t end = std::min(begin + dxvk::kParticleDistributionBlockSize, count);
      for (uint32_t i = begin; i < end; ++i) {
        blockTotal += weights[i];
        distribution[offset + i] = blockTotal;
      }
      total += blockTotal;
      distribution[offset + count + block] = total;
    }
    return distribution;
  }

  std::vector<uint32_t> sampleQuantiles(const std::vector<float>& distribution, uint32_t offset, uint32_t triangleCount, uint32_t sampleCount) {
    std::vector<uint32_t> counts(triangleCount, 0);

    for (uint32_t i = 0; i < sampleCount; ++i) {
      const float u = (float(i) + 0.5f) / sampleCount;
      const uint32_t triangle = dxvk::sampleParticleTriangle(distribution.data(), offset, triangleCount, u);

      expect(triangle < triangleCount, "A sample selected an out-of-range triangle");
      ++counts[triangle];
    }
    return counts;
  }

  void testTriangleBarycentricsStayInsideAtBoundaries() {
    const std::array<std::array<float, 2>, 7> samples = {{
      {{ 0.0f, 0.0f }}, {{ 1.0f, 0.0f }}, {{ 0.0f, 1.0f }}, {{ 1.0f, 1.0f }},
      {{ 0.25f, 0.75f }}, {{ 0.75f, 0.25f }}, {{ 0.5f, 0.5f }}
    }};

    for (const auto& sample : samples) {
      const auto barycentrics = dxvk::particleTriangleBarycentrics(sample[0], sample[1]);

      expect(barycentrics.x >= 0.0f && barycentrics.x <= 1.0f, "Boundary sample has an invalid first weight");
      expect(barycentrics.y >= 0.0f && barycentrics.y <= 1.0f, "Boundary sample has an invalid second weight");
      expect(barycentrics.z >= 0.0f && barycentrics.z <= 1.0f, "Boundary sample has an invalid third weight");
      expectNear(barycentrics.x + barycentrics.y + barycentrics.z, 1.0, 1e-6, "Boundary sample weights do not sum to one");
    }
  }

  void testTriangleBarycentricsMatchUniformAreaDistribution() {
    constexpr uint32_t kSamplesPerAxis = 512;
    constexpr double kSampleCount = double(kSamplesPerAxis) * kSamplesPerAxis;
    std::array<double, 3> firstMoments = {};
    std::array<double, 3> secondMoments = {};
    std::array<uint32_t, 3> cornerCounts = {};
    uint32_t centralCount = 0;

    for (uint32_t y = 0; y < kSamplesPerAxis; ++y) {
      for (uint32_t x = 0; x < kSamplesPerAxis; ++x) {
        const float u = (float(x) + 0.5f) / kSamplesPerAxis;
        const float v = (float(y) + 0.5f) / kSamplesPerAxis;
        const auto barycentrics = dxvk::particleTriangleBarycentrics(u, v);
        const std::array<float, 3> weights = { barycentrics.x, barycentrics.y, barycentrics.z };

        expectNear(weights[0] + weights[1] + weights[2], 1.0, 1e-6, "Interior sample weights do not sum to one");
        for (uint32_t i = 0; i < 3; ++i) {
          expect(weights[i] >= 0.0f && weights[i] <= 1.0f, "Interior sample lies outside the triangle");
          firstMoments[i] += weights[i];
          secondMoments[i] += double(weights[i]) * weights[i];
          cornerCounts[i] += weights[i] > 0.5f;
        }
        centralCount += weights[0] >= 1.0f / 6.0f && weights[1] >= 1.0f / 6.0f && weights[2] >= 1.0f / 6.0f;
      }
    }

    for (uint32_t i = 0; i < 3; ++i) {
      expectNear(firstMoments[i] / kSampleCount, 1.0 / 3.0, 0.002, "Uniform triangle first moment is wrong");
      expectNear(secondMoments[i] / kSampleCount, 1.0 / 6.0, 0.002, "Uniform triangle second moment is wrong");
      expectNear(cornerCounts[i] / kSampleCount, 0.25, 0.003, "Corner quarter has incorrect sample density");
    }
    expectNear(centralCount / kSampleCount, 0.25, 0.004, "Central quarter has incorrect sample density");
  }

  void testTriangleAreaMatchesWorldSpaceEdges() {
    const float area = dxvk::particleTriangleAreaWeight(dxvk::Vector3(2.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 3.0f, 0.0f));
    const float reverseArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(0.0f, 3.0f, 0.0f), dxvk::Vector3(2.0f, 0.0f, 0.0f));

    expectNear(area, 6.0, 1e-6, "Triangle weight is not twice its area");
    expectNear(reverseArea, area, 1e-6, "Winding changes the triangle area weight");
  }

  void testTriangleAreaTracksScaleAndDeformation() {
    const float xyArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(2.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 3.0f, 0.0f));
    const float yzArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(0.0f, 3.0f, 0.0f), dxvk::Vector3(0.0f, 0.0f, 4.0f));
    const float rotatedArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(0.0f, 2.0f, 0.0f), dxvk::Vector3(-3.0f, 0.0f, 0.0f));
    const float uniformScaledArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(4.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 6.0f, 0.0f));
    const float deformedArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(4.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 3.0f, 0.0f));

    expectNear(yzArea / xyArea, 2.0, 1e-6, "Nonuniform scale does not change relative oriented face weights");
    expectNear(rotatedArea, xyArea, 1e-6, "Rigid rotation changes area weight");
    expectNear(uniformScaledArea / xyArea, 4.0, 1e-6, "Uniform scale does not change area quadratically");
    expectNear(deformedArea / xyArea, 2.0, 1e-6, "Deformed edges do not update area weight");
  }

  void testTriangleAreaExcludesDegenerateAndNonfiniteEdges() {
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<dxvk::Vector3, 5> invalidEdges = {
      dxvk::Vector3(0.0f, 0.0f, 0.0f), dxvk::Vector3(2.0f, 0.0f, 0.0f),
      dxvk::Vector3(infinity, 0.0f, 0.0f), dxvk::Vector3(nan, 0.0f, 0.0f),
      dxvk::Vector3(0.0f, 1e30f, 0.0f)
    };
    const dxvk::Vector3 otherEdge(1e30f, 0.0f, 0.0f);

    for (const auto& edge : invalidEdges) {
      const float area = dxvk::particleTriangleAreaWeight(edge, otherEdge);

      expectNear(area, 0.0, 0.0, "Invalid triangle contributes nonzero or nonfinite area");
    }
  }

  void testUnequalFacesReceiveAreaProportionalSamples() {
    const std::vector<float> distribution = makeDistribution({ 1.0f, 9.0f });

    const auto counts = sampleQuantiles(distribution, 0, 2, 10000);

    expect(counts[0] == 1000, "Small face did not receive its area-proportional count");
    expect(counts[1] == 9000, "Large face did not receive its area-proportional count");
  }

  void testSubdivisionPreservesEqualRegionDensity() {
    std::vector<float> weights(129, 1.0f);
    weights[0] = 128.0f;
    const std::vector<float> distribution = makeDistribution(weights);
    const std::vector<float> unrefinedDistribution = makeDistribution({ 128.0f, 128.0f });

    const auto refinedCounts = sampleQuantiles(distribution, 0, uint32_t(weights.size()), 65536);
    const auto unrefinedCounts = sampleQuantiles(unrefinedDistribution, 0, 2, 65536);

    expect(refinedCounts[0] == 32768, "A large face did not receive half of the equal-area region samples");
    expect(refinedCounts[0] == unrefinedCounts[0], "Subdividing the other region changed this region's density");
    for (uint32_t i = 1; i < refinedCounts.size(); ++i) {
      expect(refinedCounts[i] == 256, "Equal subdivided faces did not receive equal samples");
    }
  }

  void testWorldAreaChangesUpdateSelectionProbabilities() {
    const float xyArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(2.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 3.0f, 0.0f));
    const float yzArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(0.0f, 3.0f, 0.0f), dxvk::Vector3(0.0f, 0.0f, 4.0f));
    const float deformedXyArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(4.0f, 0.0f, 0.0f), dxvk::Vector3(0.0f, 3.0f, 0.0f));
    const auto originalDistribution = makeDistribution({ xyArea, yzArea });
    const auto deformedDistribution = makeDistribution({ deformedXyArea, yzArea });

    const auto originalCounts = sampleQuantiles(originalDistribution, 0, 2, 12000);
    const auto deformedCounts = sampleQuantiles(deformedDistribution, 0, 2, 12000);

    expect(originalCounts[0] == 4000 && originalCounts[1] == 8000, "Nonuniformly scaled faces have incorrect probabilities");
    expect(deformedCounts[0] == 6000 && deformedCounts[1] == 6000, "Deformation did not update face selection probabilities");
  }

  void testDistributionSkipsZeroAreaFacesAndBlocks() {
    constexpr uint32_t kTriangleCount = 513;
    std::vector<float> weights(kTriangleCount, 0.0f);
    weights[255] = 1.0f;
    weights[256] = 3.0f;
    weights[512] = 4.0f;
    const auto distribution = makeDistribution(weights, 11);
    const std::array<float, 4> samples = { 0.0f, 0.125f, 0.5f, std::nextafter(1.0f, 0.0f) };
    const std::array<uint32_t, 4> expected = { 255, 256, 512, 512 };

    for (uint32_t i = 0; i < samples.size(); ++i) {
      const uint32_t triangle = dxvk::sampleParticleTriangle(distribution.data(), 11, kTriangleCount, samples[i]);

      expect(triangle == expected[i], "CDF boundary or zero-weight run selected the wrong triangle");
    }

    std::vector<float> trailingWeights(kTriangleCount, 0.0f);
    trailingWeights[512] = 2.0f;
    const auto trailingDistribution = makeDistribution(trailingWeights);

    const uint32_t trailingTriangle = dxvk::sampleParticleTriangle(trailingDistribution.data(), 0, kTriangleCount, 0.0f);

    expect(trailingTriangle == 512, "Zero-total blocks were not skipped");
  }

  void testDistributionHandlesPartialBlocksAndOffsets() {
    for (const uint32_t triangleCount : { 1u, 255u, 256u, 257u, 513u }) {
      constexpr uint32_t kOffset = 19;
      const auto distribution = makeDistribution(std::vector<float>(triangleCount, 1.0f), kOffset);

      const auto counts = sampleQuantiles(distribution, kOffset, triangleCount, triangleCount);

      for (const uint32_t count : counts) {
        expect(count == 1, "A partial block or offset changed uniform triangle selection");
      }

      const uint32_t first = dxvk::sampleParticleTriangle(distribution.data(), kOffset, triangleCount, 0.0f);
      const uint32_t last = dxvk::sampleParticleTriangle(distribution.data(), kOffset, triangleCount, 1.0f);

      expect(first == 0, "A zero random sample did not select the first positive face");
      expect(last == triangleCount - 1, "A rounded upper-end sample escaped the final positive face");
    }
  }

  void testBlockCountsDoNotOverflowAtTriangleCountLimit() {
    const uint32_t maxTriangles = std::numeric_limits<uint32_t>::max();

    const uint32_t empty = dxvk::particleDistributionBlockCount(0);
    const uint32_t exactBlock = dxvk::particleDistributionBlockCount(256);
    const uint32_t partialBlock = dxvk::particleDistributionBlockCount(257);
    const uint32_t largest = dxvk::particleDistributionBlockCount(maxTriangles);

    expect(empty == 0, "An empty emitter requires a distribution block");
    expect(exactBlock == 1, "An exact block requires an extra block");
    expect(partialBlock == 2, "A partial block was omitted");
    expect(largest == 16777216, "Block count overflowed for a large emitter");
  }

  void testEmptyAndInvalidDistributionsReturnNoTriangle() {
    constexpr uint32_t kInvalidTriangle = std::numeric_limits<uint32_t>::max();
    const std::array<float, 4> totals = { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };

    const uint32_t empty = dxvk::sampleParticleTriangle(nullptr, 0, 0, 0.5f);

    expect(empty == kInvalidTriangle, "An empty emitter selected a triangle");
    for (const float total : totals) {
      const std::vector<float> distribution = { total, total };

      const uint32_t triangle = dxvk::sampleParticleTriangle(distribution.data(), 0, 1, 0.5f);

      expect(triangle == kInvalidTriangle, "An invalid total area selected a triangle");
    }
  }

  void testInvalidRandomSamplesReturnNoTriangle() {
    const auto distribution = makeDistribution({ 1.0f });
    const std::array<float, 3> samples = {
      std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity()
    };

    for (const float sample : samples) {
      const uint32_t triangle = dxvk::sampleParticleTriangle(distribution.data(), 0, 1, sample);

      expect(triangle == dxvk::kInvalidParticleTriangle, "A nonfinite random value selected a triangle");
    }
  }

  dxvk::Matrix4 makeScaledTransform(float x, float y, float z) {
    dxvk::Matrix4 transform;
    transform[0][0] = x;
    transform[1][1] = y;
    transform[2][2] = z;
    return transform;
  }

  void testAreaMetricMatchesKnownAnisotropicTransform() {
    const auto transform = makeScaledTransform(2.0f, 3.0f, 4.0f);

    const auto metric = dxvk::particleSpawnAreaMetric(transform);

    expectNear(metric[0], 4.0 / 29.0, 1e-14, "Metric has an incorrect x-axis scale");
    expectNear(metric[3], 9.0 / 29.0, 1e-14, "Metric has an incorrect y-axis scale");
    expectNear(metric[5], 16.0 / 29.0, 1e-14, "Metric has an incorrect z-axis scale");
    expectNear(metric[1], 0.0, 0.0, "Orthogonal axes have a nonzero xy metric");
    expectNear(metric[2], 0.0, 0.0, "Orthogonal axes have a nonzero xz metric");
    expectNear(metric[4], 0.0, 0.0, "Orthogonal axes have a nonzero yz metric");
  }

  void testAreaTransformRemovesExtremeCommonScale() {
    auto original = makeScaledTransform(2.0f, 3.0f, -4.0f);
    original[1][0] = 0.25f;
    original[2][1] = -0.5f;
    const auto normalized = dxvk::particleSpawnAreaTransform(original);
    for (const int exponent : { -120, -80, 0, 80, 120 }) {
      auto scaled = original;
      for (uint32_t column = 0; column < 3; column++) {
        for (uint32_t row = 0; row < 3; row++) {
          scaled[column][row] = std::ldexp(original[column][row], exponent);
        }
      }
      scaled[3] = dxvk::Vector4(100.0f, -200.0f, 300.0f, 1.0f);
      const auto result = dxvk::particleSpawnAreaTransform(scaled);
      for (uint32_t column = 0; column < 4; column++) {
        for (uint32_t row = 0; row < 3; row++) {
          expectNear(result[column][row], normalized[column][row], 0.0, "Weight transform depends on uniform scale or translation");
        }
      }
      expectNear(result[1][1] / result[0][0], 1.5, 1e-6, "Normalization changed anisotropic ratios");
      expect(result[2][2] < 0, "Normalization changed reflection");
    }
    for (const float scale : { 0.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() }) {
      const auto invalid = dxvk::particleSpawnAreaTransform(makeScaledTransform(scale, scale, scale));
      for (uint32_t column = 0; column < 3; column++) {
        for (uint32_t row = 0; row < 3; row++) {
          expect(invalid[column][row] == 0, "Invalid transform did not produce zero-area weights");
        }
      }
    }
  }

  void testAreaCacheReusesTranslationRotationAndUniformScale() {
    const auto original = makeScaledTransform(2.0f, 3.0f, 4.0f);
    const auto originalMetric = dxvk::particleSpawnAreaMetric(original);
    auto translated = original;
    translated[3] = dxvk::Vector4(100.0f, -200.0f, 300.0f, 1.0f);
    auto rotated = original;
    const float cosine = std::cos(0.7f);
    const float sine = std::sin(0.7f);
    rotated[0] = dxvk::Vector4(2.0f * cosine, 2.0f * sine, 0.0f, 0.0f);
    rotated[1] = dxvk::Vector4(-3.0f * sine, 3.0f * cosine, 0.0f, 0.0f);
    const auto uniformlyScaled = makeScaledTransform(4.0f, 6.0f, 8.0f);
    const auto uniformlyReflected = makeScaledTransform(-4.0f, -6.0f, -8.0f);

    const bool translationReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(translated));
    const bool rotationReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(rotated));
    const bool scaleReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(uniformlyScaled));
    const bool reflectionReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(uniformlyReflected));

    expect(translationReuse, "Translation unnecessarily invalidates the area cache");
    expect(rotationReuse, "World-space rigid rotation unnecessarily invalidates the area cache");
    expect(scaleReuse, "Uniform scale unnecessarily invalidates the area cache");
    expect(reflectionReuse, "Uniform reflection unnecessarily invalidates the area cache");
  }

  void testAreaCacheInvalidatesNonuniformScaleAndShear() {
    const auto original = makeScaledTransform(2.0f, 3.0f, 4.0f);
    const auto originalMetric = dxvk::particleSpawnAreaMetric(original);
    const auto changedScale = makeScaledTransform(2.0f, 3.0f, 5.0f);
    auto sheared = original;
    sheared[1][0] = 0.25f;
    auto objectRotated = original;
    objectRotated[0] = dxvk::Vector4(0.0f, 3.0f, 0.0f, 0.0f);
    objectRotated[1] = dxvk::Vector4(-2.0f, 0.0f, 0.0f, 0.0f);

    const bool scaleReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(changedScale));
    const bool shearReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(sheared));
    const bool objectRotationReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, dxvk::particleSpawnAreaMetric(objectRotated));

    expect(!scaleReuse, "Nonuniform scale changes reused stale area weights");
    expect(!shearReuse, "A shear change reused stale area weights");
    expect(!objectRotationReuse, "Rotation before an anisotropic world scale reused stale area weights");
  }

  void testAreaCacheRejectsNonfiniteAndCollapsedTransforms() {
    const auto validMetric = dxvk::particleSpawnAreaMetric(dxvk::Matrix4());
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<dxvk::Matrix4, 3> transforms = {
      makeScaledTransform(infinity, 1.0f, 1.0f),
      makeScaledTransform(nan, 1.0f, 1.0f),
      makeScaledTransform(0.0f, 0.0f, 0.0f)
    };

    for (const auto& transform : transforms) {
      const auto invalidMetric = dxvk::particleSpawnAreaMetric(transform);

      const bool currentReuse = dxvk::canReuseParticleSpawnAreas(validMetric, invalidMetric);
      const bool previousReuse = dxvk::canReuseParticleSpawnAreas(invalidMetric, validMetric);
      const bool selfReuse = dxvk::canReuseParticleSpawnAreas(invalidMetric, invalidMetric);

      expect(!currentReuse, "An invalid current transform reused the area cache");
      expect(!previousReuse, "An invalid previous transform reused the area cache");
      expect(!selfReuse, "An invalid transform was accepted through equality");
    }
  }

  void testAreaCacheRejectsAreaChangesOnThinEmitters() {
    const float originalScale = 0.001f;
    const float changedScale = originalScale * std::sqrt(1.1f);
    const auto originalMetric = dxvk::particleSpawnAreaMetric(makeScaledTransform(originalScale, 1.0f, 1.0f));
    const auto changedMetric = dxvk::particleSpawnAreaMetric(makeScaledTransform(changedScale, 1.0f, 1.0f));
    const float originalArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(originalScale, 0.0f, 0.0f), dxvk::Vector3(0.0f, 1.0f, 0.0f));
    const float changedArea = dxvk::particleTriangleAreaWeight(dxvk::Vector3(changedScale, 0.0f, 0.0f), dxvk::Vector3(0.0f, 1.0f, 0.0f));

    const bool canReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, changedMetric);

    for (uint32_t i = 0; i < originalMetric.size(); ++i) {
      expect(std::abs(originalMetric[i] - changedMetric[i]) < 1e-7, "Thin-emitter regression does not exercise a small normalized metric change");
    }
    expectNear(changedArea / originalArea, 1.04880884817, 2e-6, "Thin-emitter fixture has an incorrect relative face-area change");
    expect(!canReuse, "A thin emitter reused face weights despite a significant relative area change");
  }

  void testAreaCacheRejectsChangesNearSingularity() {
    const auto original = makeScaledTransform(1.0f, 1.0f, 1e-6f);
    const auto originalMetric = dxvk::particleSpawnAreaMetric(original);
    const auto changed = makeScaledTransform(1.0f, 1.0f, 2e-6f);
    const auto changedMetric = dxvk::particleSpawnAreaMetric(changed);

    const bool unchangedReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, originalMetric);
    const bool changedReuse = dxvk::canReuseParticleSpawnAreas(originalMetric, changedMetric);

    expect(unchangedReuse, "An identical near-singular metric cannot reuse its area cache");
    expect(!changedReuse, "A near-singular change was accepted by an absolute metric tolerance");
  }

}

int main() {
  try {
    testTriangleBarycentricsStayInsideAtBoundaries();
    testTriangleBarycentricsMatchUniformAreaDistribution();
    testTriangleAreaMatchesWorldSpaceEdges();
    testTriangleAreaTracksScaleAndDeformation();
    testTriangleAreaExcludesDegenerateAndNonfiniteEdges();
    testUnequalFacesReceiveAreaProportionalSamples();
    testSubdivisionPreservesEqualRegionDensity();
    testWorldAreaChangesUpdateSelectionProbabilities();
    testDistributionSkipsZeroAreaFacesAndBlocks();
    testDistributionHandlesPartialBlocksAndOffsets();
    testBlockCountsDoNotOverflowAtTriangleCountLimit();
    testEmptyAndInvalidDistributionsReturnNoTriangle();
    testInvalidRandomSamplesReturnNoTriangle();
    testAreaMetricMatchesKnownAnisotropicTransform();
    testAreaTransformRemovesExtremeCommonScale();
    testAreaCacheReusesTranslationRotationAndUniformScale();
    testAreaCacheInvalidatesNonuniformScaleAndShear();
    testAreaCacheRejectsNonfiniteAndCollapsedTransforms();
    testAreaCacheRejectsAreaChangesOnThinEmitters();
    testAreaCacheRejectsChangesNearSingularity();
    std::cout << "All particle spawn distribution tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Particle spawn distribution regression failed: " << error.what() << "\n";
    return -1;
  }
}
