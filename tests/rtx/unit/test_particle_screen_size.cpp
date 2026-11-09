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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "rtx/pass/particles/particle_screen_size.h"

namespace {
  using namespace dxvk;

  void expect(bool condition, const char* message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void expectNear(float actual, float expected, const char* message) {
    const float tolerance = std::max(0.00000001f, std::abs(expected) * 0.00001f);
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
      std::cerr << message << ": expected " << expected << ", got " << actual << ".\n";
      throw std::runtime_error(message);
    }
  }

  void testBoundsPreserveTheRequestedFootprint() {
    struct Case {
      float projected;
      float minimum;
      float maximum;
      float expected;
    };
    const Case cases[] = {
      { 0.2f, 0.f, 0.f, 0.2f },
      { 0.2f, 0.8f, 0.f, 0.8f },
      { 2.f, 0.8f, 0.f, 2.f },
      { 8.f, 0.f, 1.2f, 1.2f },
      { 0.2f, 0.f, 1.2f, 0.2f },
      { 1.f, 0.8f, 1.2f, 1.f },
      { 0.1f, 0.9f, 0.9f, 0.9f },
      { 9.f, 0.9f, 0.9f, 0.9f },
      { 0.25f, 2.f, 1.f, 2.f },
      { 4.f, 2.f, 1.f, 2.f },
    };
    for (const auto& value : cases) {
      const auto result = adjustParticleScreenSize(value.projected, value.minimum, value.maximum, 0.f);
      expectNear(value.projected * result.scale, value.expected, "Projected footprint must obey its enabled bounds");
      expectNear(result.opacityScale, 1.f, "Zero compensation must preserve authored opacity");
    }

    const float width = 0.1f;
    const float height = 0.2f;
    const auto result = adjustParticleScreenSize(height, 0.8f, 1.2f, 0.f);
    expectNear(width * result.scale, 0.4f, "Uniform scaling must preserve a rectangular billboard's width");
    expectNear(height * result.scale, 0.8f, "Uniform scaling must preserve a rectangular billboard's height");
  }

  void testCoveragePreservesAreaAndNeverBoostsOpacity() {
    for (float original : { 0.001f, 0.01f, 0.1f, 0.25f, 0.5f, 1.f }) {
      const auto result = adjustParticleScreenSize(original, 1.f, 0.f, 1.f);
      const float rendered = original * result.scale;
      expectNear(rendered * rendered * result.opacityScale, original * original,
        "Full compensation must preserve integrated projected coverage when enlarging");
      expect(result.opacityScale >= 0.f && result.opacityScale <= 1.f, "Compensation must remain a finite attenuation");
    }
    expectNear(adjustParticleScreenSize(0.25f, 1.f, 0.f, 0.25f).opacityScale, 0.5f,
      "Fractional compensation must allow an artistic blend toward full coverage preservation");
    expectNear(adjustParticleScreenSize(0.25f, 1.f, 0.f, 0.f).opacityScale, 1.f,
      "Visibility-preserving mode must leave opacity unchanged");
    for (float coverage : { 0.f, 0.25f, 1.f }) {
      expectNear(adjustParticleScreenSize(8.f, 0.f, 1.f, coverage).opacityScale, 1.f,
        "A maximum-size cap must never amplify opacity");
      expectNear(adjustParticleScreenSize(1.f, 0.8f, 1.2f, coverage).opacityScale, 1.f,
        "A billboard inside its screen-size band must keep its original opacity");
    }
  }

  void testInvalidSettingsAndProjectionsStayFinite() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    expect(!particleScreenSizeEnabled(0.f, 0.f), "Both zero bounds must disable screen sizing");
    expect(!particleScreenSizeEnabled(nan, infinity), "Nonfinite bounds must not enable screen sizing");
    expect(!particleScreenSizeEnabled(-1.f, -2.f), "Negative bounds must not enable screen sizing");
    expect(particleScreenSizeEnabled(0.8f, nan), "A valid minimum must survive an invalid maximum");
    expect(particleScreenSizeEnabled(infinity, 1.2f), "A valid maximum must survive an invalid minimum");

    for (float invalid : { 0.f, -1.f, nan, infinity, -infinity }) {
      const auto result = adjustParticleScreenSize(invalid, 0.8f, 1.2f, 1.f);
      expectNear(result.scale, 1.f, "Invalid projected sizes must preserve geometry scale");
      expectNear(result.opacityScale, 1.f, "Invalid projected sizes must preserve opacity");
    }
    for (float invalid : { -1.f, nan, infinity, -infinity }) {
      const auto result = adjustParticleScreenSize(0.25f, invalid, invalid, 0.f);
      expectNear(result.scale, 1.f, "Invalid bounds must act as disabled limits");
      expectNear(result.opacityScale, 1.f, "Invalid bounds must not change opacity");
    }
    for (float coverage : { nan, infinity, -infinity, 2.f }) {
      expectNear(adjustParticleScreenSize(0.25f, 1.f, 0.f, coverage).opacityScale, 0.0625f,
        "Invalid or excessive compensation must use the bounded full-coverage setting");
    }
    expectNear(adjustParticleScreenSize(0.25f, 1.f, 0.f, -1.f).opacityScale, 1.f,
      "Negative compensation must clamp to visibility-preserving mode");

    const auto extreme = adjustParticleScreenSize(1e-30f, 1.f, 0.f, 0.f);
    expect(std::isfinite(extreme.scale), "Extreme valid enlargement must retain a finite scale");
    expectNear(extreme.opacityScale, 1.f, "Zero compensation must stay defined when inverse area underflows");
    const auto overflow = adjustParticleScreenSize(std::numeric_limits<float>::min(), std::numeric_limits<float>::max(), 0.f, 1.f);
    expectNear(overflow.scale, 1.f, "Unrepresentable enlargement must leave geometry unchanged");
    expectNear(overflow.opacityScale, 1.f, "Unrepresentable enlargement must leave opacity unchanged");
  }

  void testMotionTrailsPreserveCoverage() {
    const float trailAreaRatio = 10.f / 22.f;
    expectNear(particleScreenSizeOpacity(2.f, 1.f, 1.f, 9.f), trailAreaRatio,
      "Motion-trail coverage must include the unscaled trail length");
    expectNear(particleScreenSizeOpacity(2.f, 1.f, 0.f, 1.f), 0.5f,
      "A trail with no base height must scale coverage along its width");
    expectNear(particleScreenSizeOpacity(2.f, 1.f, 1.f, 0.f), 0.25f,
      "A billboard without a motion trail must use its squared scale");
    expectNear(particleScreenSizeOpacity(2.f, 0.5f, 1.f, 9.f), std::sqrt(trailAreaRatio),
      "Fractional trail compensation must blend the actual area ratio");
    for (float coverage : { 0.f, 0.25f, 1.f }) {
      expectNear(particleScreenSizeOpacity(0.5f, coverage, 1.f, 9.f), 1.f,
        "Shrinking a motion trail must never boost opacity");
    }
  }

  void testProjectionDecisions() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const dxvk::float4 right(1.f, 0.f, 0.f, 0.f);
    const dxvk::float4 up(0.f, 2.f, 0.f, 0.f);
    const vec2 renderSize(640.f, 480.f);
    for (float depth : { -10.f, -1e-6f, 0.f, 1e-6f, 1e-5f }) {
      const auto result = measureParticleScreenSize(dxvk::float4(0.f, 0.f, depth, depth), right, up, renderSize);
      expect(result.action == ParticleScreenSizeAction::KeepWorldSize,
        "Behind-camera and camera-plane particles must keep ray-traceable world geometry");
      expectNear(result.projectedSize, 0.f, "Unsuitable depth must bypass projection division");
    }
    const dxvk::float4 center(0.f, 0.f, 10.f, 10.f);
    const auto measured = measureParticleScreenSize(center, right, up, renderSize);
    expect(measured.action == ParticleScreenSizeAction::Adjust, "Front-camera particles must use measured sizing");
    expectNear(measured.projectedSize, 48.f, "Measurement must use the longest full edge in internal pixels");
    expectNear(adjustParticleScreenSize(measured.projectedSize, 0.f, 24.f, 1.f).scale, 0.5f,
      "Measured footprint must drive maximum-size adjustment");
    const auto halfResolution = measureParticleScreenSize(center, right, up, vec2(320.f, 240.f));
    expectNear(halfResolution.projectedSize, 24.f, "Internal resolution must affect the measured footprint");
    const auto tilted = measureParticleScreenSize(dxvk::float4(5.f, 0.f, 10.f, 10.f),
      dxvk::float4(2.f, 0.f, 0.f, 2.f), dxvk::float4(0.f, 0.f, 0.f, 0.f), renderSize);
    expectNear(tilted.projectedSize, 32.f, "Tilted edges must include the off-axis projection differential");
    for (float invalid : { nan, infinity, -infinity }) {
      for (int component = 0; component < 4; ++component) {
        dxvk::float4 badCenter = center;
        badCenter[component] = invalid;
        expect(measureParticleScreenSize(badCenter, right, up, renderSize).action == ParticleScreenSizeAction::Clear,
          "Every nonfinite center component must clear geometry");
        dxvk::float4 badEdge = right;
        badEdge[component] = invalid;
        expect(measureParticleScreenSize(center, badEdge, up, renderSize).action == ParticleScreenSizeAction::Clear,
          "Nonfinite right edges must clear geometry");
        expect(measureParticleScreenSize(center, right, badEdge, renderSize).action == ParticleScreenSizeAction::Clear,
          "Nonfinite up edges must clear geometry");
      }
    }
    const dxvk::float4 zero(0.f, 0.f, 0.f, 0.f);
    expect(measureParticleScreenSize(center, zero, zero, renderSize).action == ParticleScreenSizeAction::Clear,
      "A zero projected footprint must clear geometry");
    expect(measureParticleScreenSize(center, dxvk::float4(std::numeric_limits<float>::max(), 0.f, 0.f, 0.f), up,
      renderSize).action == ParticleScreenSizeAction::Clear, "Overflowing projection arithmetic must clear geometry");
    for (float invalid : { 0.f, -1.f, nan, infinity }) {
      expect(measureParticleScreenSize(center, right, up, vec2(invalid, 480.f)).action == ParticleScreenSizeAction::Clear,
        "Invalid render width must clear geometry");
      expect(measureParticleScreenSize(center, right, up, vec2(640.f, invalid)).action == ParticleScreenSizeAction::Clear,
        "Invalid render height must clear geometry");
    }
  }
}

int main() {
  try {
    testBoundsPreserveTheRequestedFootprint();
    testCoveragePreservesAreaAndNeverBoostsOpacity();
    testInvalidSettingsAndProjectionsStayFinite();
    testMotionTrailsPreserveCoverage();
    testProjectionDecisions();
    std::cout << "Particle screen-size bounds, aspect ratio, coverage, and invalid-input checks passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Particle screen-size test failed: " << error.what() << "\n";
    return -1;
  }
}
