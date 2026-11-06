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

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "../../util/util_matrix.h"

namespace dxvk {

  using ParticleSpawnAreaMetric = std::array<double, 6>;

  // Weight construction needs only relative areas. Remove a common scale in double precision
  // so even very small finite transforms reach the shader without underflowing their areas.
  inline Matrix4 particleSpawnAreaTransform(const Matrix4& transform) {
    double squaredScale = 0.0;
    for (uint32_t column = 0; column < 3; column++) {
      for (uint32_t row = 0; row < 3; row++) {
        squaredScale += double(transform[column][row]) * double(transform[column][row]);
      }
    }
    Matrix4 normalized(0.0f);
    if (squaredScale > 0.0 && std::isfinite(squaredScale)) {
      const double scale = std::sqrt(squaredScale / 3.0);
      for (uint32_t column = 0; column < 3; column++) {
        for (uint32_t row = 0; row < 3; row++) {
          normalized[column][row] = float(double(transform[column][row]) / scale);
        }
      }
    }
    return normalized;
  }

  // Normalizing A^T A removes translation, rigid rotation, and uniform scale from relative face areas.
  inline ParticleSpawnAreaMetric particleSpawnAreaMetric(const Matrix4& transform) {
    ParticleSpawnAreaMetric metric = {};
    uint32_t index = 0;
    for (uint32_t column = 0; column < 3; column++) {
      for (uint32_t other = column; other < 3; other++) {
        for (uint32_t row = 0; row < 3; row++) {
          metric[index] += double(transform[column][row]) * double(transform[other][row]);
        }
        index++;
      }
    }
    const double trace = metric[0] + metric[3] + metric[5];
    for (double& value : metric) {
      value = trace > 0.0 && std::isfinite(trace) ? value / trace : std::numeric_limits<double>::quiet_NaN();
    }
    return metric;
  }

  inline bool canReuseParticleSpawnAreas(const ParticleSpawnAreaMetric& previous, const ParticleSpawnAreaMetric& current) {
    for (uint32_t i = 0; i < current.size(); i++) {
      if (!std::isfinite(previous[i]) || !std::isfinite(current[i])) {
        return false;
      }
    }
    if (previous == current) {
      return true;
    }
    const auto determinant = [](const ParticleSpawnAreaMetric& metric) {
      return metric[0] * (metric[3] * metric[5] - metric[4] * metric[4])
        - metric[1] * (metric[1] * metric[5] - metric[4] * metric[2])
        + metric[2] * (metric[1] * metric[4] - metric[3] * metric[2]);
    };
    const double minimumDeterminant = std::min(determinant(previous), determinant(current));
    if (minimumDeterminant <= 0.0) {
      return false;
    }
    // For a positive metric of trace one, four times its determinant is a lower bound for its smallest eigenvalue.
    // Scaling the tolerance by that bound prevents thin or sheared emitters from reusing changed face areas.
    const double tolerance = 4e-6 * minimumDeterminant;
    for (uint32_t i = 0; i < current.size(); i++) {
      if (std::abs(previous[i] - current[i]) > tolerance / 3.0) {
        return false;
      }
    }
    return true;
  }

}
