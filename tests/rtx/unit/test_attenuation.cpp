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

// NV-DXVK start: Regression coverage for shared CPU/GPU HDR attenuation storage.
#include "../../../src/dxvk/shaders/rtx/utility/packing_helpers.h"

int main() {
  // Exercise the same scalar HDR codec used by PSR payloads and the final attenuation buffers.
  if (packSharedExponent(0.0f, 0.0f, 0.0f) != 0u ||
      packSharedExponent(1.0f, 0.0f, 0.0f) != 0x80000100u ||
      packSharedExponent(1.0f, 1.0f, 1.0f) != 0x84020100u) {
    std::cerr << "Invalid RGB9E5 attenuation encoding" << std::endl;
    return -1;
  }
  for (int exponent = -24; exponent <= 15; ++exponent) {
    for (float multiplier : { 1.0f, 1.25f, 1.998f, 1.999f }) {
      const float maxValue = std::min(65408.0f, std::exp2(float(exponent)) * multiplier);
      const std::array<float, 3> input = { maxValue, maxValue * 0.33f, maxValue * 0.001f };
      const uint32_t packed = packSharedExponent(input[0], input[1], input[2]);
      // One half-step is the maximum rounding error at the selected shared exponent.
      const float tolerance = std::exp2(float(packed >> 27) - 25.0f) + maxValue * 1e-6f;
      for (uint32_t channel = 0; channel < 3; ++channel) {
        const float actual = unpackSharedExponentComponent(packed, channel * 9);
        if (!std::isfinite(actual) || std::abs(actual - input[channel]) > tolerance) {
          std::cerr << "Attenuation round-trip failed at exponent " << exponent << std::endl;
          return -1;
        }
      }
    }
  }
  // Regression: the old UNORM payload clipped the visible camera gain to one.
  // Opacity and gain must survive both the PSR handoff and the GBuffer handoff.
  for (float opacity : { 0.0f, 0.33f, 0.5f, 1.0f }) {
    for (float gain : { 0.0f, 1.0f, 25.0f, 562.5f }) {
      const float expected = opacity * gain;
      const uint32_t psr = packSharedExponent(expected, expected, expected);
      const float continued = unpackSharedExponentComponent(psr, 0);
      const uint32_t gbuffer = packSharedExponent(continued, continued, continued);
      const float visible = unpackSharedExponentComponent(gbuffer, 0);
      if (std::abs(visible - expected) > std::max(1e-7f, expected / 256.0f)) {
        std::cerr << "Visible camera intensity was lost during PSR/GBuffer storage" << std::endl;
        return -1;
      }
    }
  }
  const uint32_t clamped = packSharedExponent(-1.0f, 65504.0f, 100000.0f);
  if (unpackSharedExponentComponent(clamped, 0) != 0.0f ||
      unpackSharedExponentComponent(clamped, 9) != 65408.0f ||
      unpackSharedExponentComponent(clamped, 18) != 65408.0f) {
    std::cerr << "Attenuation exceeded the finite RGB9E5 range" << std::endl;
    return -1;
  }

  return 0;
}
// NV-DXVK end
