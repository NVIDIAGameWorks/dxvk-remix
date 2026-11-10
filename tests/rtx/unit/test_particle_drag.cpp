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
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "rtx/pass/particles/particle_drag.h"

namespace {
  struct Particle {
    float position = 0.f;
    float velocity = 100000.f;
  };

  void expectNear(double actual, double expected, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 0.00005 * std::max(1.0, std::abs(expected))) {
      std::cerr << message << ": expected " << expected << ", got " << actual << '\n';
      throw std::runtime_error(message);
    }
  }

  void advance(Particle& particle, float drag, float acceleration, float dt) {
    const auto step = dxvk::particleDragStep(drag, dt);
    particle.position += particle.velocity * step.velocityIntegral + acceleration * step.accelerationIntegral;
    particle.velocity = particle.velocity * step.velocityScale + acceleration * step.velocityIntegral;
  }

  void checkAnalyticMotion(const Particle& particle, float drag, float acceleration, double elapsed) {
    const double decay = std::exp(-double(drag) * elapsed);
    const double integral = -std::expm1(-double(drag) * elapsed) / drag;
    expectNear(particle.velocity, 100000.0 * decay + acceleration * integral, "Velocity must match elapsed time");
    expectNear(particle.position, 100000.0 * integral + acceleration * (elapsed - integral) / drag,
      "Distance must match elapsed time");
  }

  void testFrameRates() {
    for (float drag : { 0.01f, 1.f, 10.f, 30.f, 100.f }) {
      for (float acceleration : { 0.f, -200.f, 200.f }) {
        for (int fps : { 30, 60, 120, 144, 240 }) {
          Particle particle;
          const float dt = 1.f / float(fps);
          for (int frame = 0; frame < fps; frame++) {
            advance(particle, drag, acceleration, dt);
          }
          checkAnalyticMotion(particle, drag, acceleration, double(dt) * fps);
        }
      }
    }
  }

  void testCameraFrameTimeChanges() {
    const std::array<float, 5> frameTimes = { 1.f / 240.f, 1.f / 120.f, 1.f / 30.f, 1.f / 60.f, 1.f / 45.f };
    Particle particle;
    double elapsed = 0.0;
    for (int frame = 0; frame < 200; frame++) {
      const float dt = frameTimes[frame % frameTimes.size()];
      advance(particle, 10.f, -200.f, dt);
      elapsed += dt;
      checkAnalyticMotion(particle, 10.f, -200.f, elapsed);
    }
  }

  void testBirthStepPartition() {
    for (float age : { 0.f, 0.000001f, 0.003f, 1.f / 60.f, 1.f / 30.f }) {
      Particle whole;
      Particle split;
      advance(whole, 30.f, -200.f, age);
      for (int step = 0; step < 16; step++) {
        advance(split, 30.f, -200.f, age / 16.f);
      }
      expectNear(split.velocity, whole.velocity, "Subframe age must not change damping");
      expectNear(split.position, whole.position, "Subframe age must not change distance");
    }
  }

  void testLimits() {
    const auto disabled = dxvk::particleDragStep(0.f, 0.02f);
    expectNear(disabled.velocityScale, 1.0, "Disabled drag must preserve velocity");
    expectNear(disabled.velocityIntegral, 0.02, "Disabled drag must preserve the timestep");
    expectNear(disabled.accelerationIntegral, 0.0004, "Disabled drag must retain semi-implicit movement");
    Particle paused;
    advance(paused, 30.f, -200.f, 0.f);
    expectNear(paused.velocity, 100000.0, "A paused simulation must not damp velocity");
    expectNear(paused.position, 0.0, "A paused simulation must not move");
    const auto tiny = dxvk::particleDragStep(0.00000001f, 1.f / 30.f);
    expectNear(tiny.velocityIntegral, 1.0 / 30.0, "Tiny drag must avoid cancellation in displacement");
    expectNear(tiny.accelerationIntegral * 1800.f, 1.0, "Tiny drag must integrate acceleration accurately");
    Particle strong;
    advance(strong, 30.f, 0.f, 1.f / 30.f);
    expectNear(strong.velocity, 100000.0 / std::exp(1.0), "Strong drag must not stop particles after one long frame");
  }

  void testMotionConstraints() {
    for (float initialVelocity : { -200.f, -50.f, 50.f, 200.f }) {
      for (float drag : { 0.f, 1.f, 30.f, 100.f }) {
        for (float dt : { 1.f / 240.f, 1.f / 60.f, 1.f / 30.f }) {
          const float maxVelocity = 100.f;
          const auto step = dxvk::particleDragStep(drag, dt);
          const float integratedVelocity = initialVelocity * step.velocityScale;
          const float velocity = std::clamp(integratedVelocity, -maxVelocity, maxVelocity);
          const float freeDisplacement = initialVelocity * step.velocityIntegral;
          const float displacement = dxvk::particleDragDisplacement(freeDisplacement, initialVelocity, velocity,
            maxVelocity, dt, velocity != integratedVelocity);
          if (std::abs(displacement) > maxVelocity * dt + 0.00001f) {
            throw std::runtime_error("Drag must not bypass authored velocity limits during a step");
          }
          expectNear(dxvk::particleDragDisplacement(freeDisplacement, initialVelocity, integratedVelocity,
            0.f, dt, false), freeDisplacement, "A disabled speed limit must retain analytic displacement");
          expectNear(dxvk::particleDragDisplacement(freeDisplacement, initialVelocity, 0.f,
            maxVelocity, dt, true), 0.0, "Stopped and restricted axes must not move");
          expectNear(dxvk::particleDragDisplacement(freeDisplacement, initialVelocity, -velocity,
            maxVelocity, dt, true), -velocity * dt, "Collision responses must retain their post-response movement");
        }
      }
    }
  }
}

int main() {
  try {
    testFrameRates();
    testCameraFrameTimeChanges();
    testBirthStepPartition();
    testLimits();
    testMotionConstraints();
    std::cout << "PASS: frame-rate invariance, changing frame times, partial steps, drag limits and motion constraints\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
