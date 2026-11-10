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
#pragma once

#ifdef __cplusplus
#include <cmath>
namespace dxvk {
#endif

struct ParticleDragStep {
  float velocityScale;
  float velocityIntegral;
  float accelerationIntegral;
};

inline ParticleDragStep particleDragStep(float dragCoefficient, float deltaTimeSecs) {
  ParticleDragStep step;
  step.velocityScale = 1.f;
  step.velocityIntegral = deltaTimeSecs;
  step.accelerationIntegral = deltaTimeSecs * deltaTimeSecs;

  if (dragCoefficient > 0.f && deltaTimeSecs > 0.f) {
    const float decay = dragCoefficient * deltaTimeSecs;
#ifdef __cplusplus
    step.velocityScale = std::exp(-decay);
#else
    step.velocityScale = exp(-decay);
#endif

    // Series evaluation avoids cancellation in both integrals near zero drag.
    if (decay < 0.1f) {
      step.velocityIntegral = deltaTimeSecs * (1.f - decay * (0.5f - decay * (1.f / 6.f - decay * (1.f / 24.f - decay / 120.f))));
      step.accelerationIntegral = deltaTimeSecs * deltaTimeSecs * (0.5f - decay * (1.f / 6.f - decay * (1.f / 24.f - decay * (1.f / 120.f - decay / 720.f))));
    } else {
      step.velocityIntegral = (1.f - step.velocityScale) / dragCoefficient;
      step.accelerationIntegral = (deltaTimeSecs - step.velocityIntegral) / dragCoefficient;
    }
  }
  return step;
}

inline float particleDragDisplacement(float displacement, float initialVelocity, float velocity,
    float maxVelocity, float deltaTimeSecs, bool constrained) {
  const bool startedAboveLimit = maxVelocity > 0.f && (initialVelocity > maxVelocity || initialVelocity < -maxVelocity);
  if (constrained || startedAboveLimit) {
    return velocity * deltaTimeSecs;
  }
  return displacement;
}

#ifdef __cplusplus
}
#endif
