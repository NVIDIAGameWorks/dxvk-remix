/*
* Copyright (c) 2024-2026, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

// ============================================================================
// Standalone curve interpolation utilities for animation systems.
//
// This header is intentionally free of any engine-specific dependencies
// (no dxvk, no remixapi) so it can be shared across DLL boundaries.
//
// The vec-dependent functions (ColorGradientData, bakeColorGradient,
// combineToVec2, combineToVec3) are templated over the vector type.
// Any type with .x/.y/.z/.w float members works (dxvk::vec*, remixapi_Float*D).
// ============================================================================

#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace CurveUtils {

  // Default resolution for baking sparse keyframes to linear arrays, this seems like a good balance of quality/performance, can be adjusted later if necessary.
  constexpr uint32_t kDefaultAnimationResolution = 256;

  // -------------------------------------------------------------------------
  // Basic math
  // -------------------------------------------------------------------------

  inline float curveLerp(float a, float b, float t) {
    return a + (b - a) * t;
  }

  /**
   * Cubic bezier interpolation.
   * @param p0 Start point (at t=0)
   * @param p1 First control point
   * @param p2 Second control point
   * @param p3 End point (at t=1)
   * @param t Parameter in [0, 1]
   */
  inline float bezierInterpolate(float p0, float p1, float p2, float p3, float t) {
    const float u = 1.0f - t;
    return u * u * u * p0 +
           3.0f * u * u * t * p1 +
           3.0f * u * t * t * p2 +
           t * t * t * p3;
  }

  // -------------------------------------------------------------------------
  // Keyframe interval lookup
  // -------------------------------------------------------------------------

  inline size_t findKeyframeInterval(const float* times, size_t numKeyframes, float t) {
    if (numKeyframes <= 1) {
      return 0;
    }
    // Clamp to first/last interval for out-of-range values
    if (t <= times[0]) {
      return 0;
    }
    if (t >= times[numKeyframes - 1]) {
      return numKeyframes - 2;
    }
    for (size_t j = 0; j < numKeyframes - 1; ++j) {
      if (t >= times[j] && t <= times[j + 1]) {
        return j;
      }
    }
    return numKeyframes - 2;
  }

  // -------------------------------------------------------------------------
  // Tangent types
  // -------------------------------------------------------------------------

  enum class TangentType {
    Linear,   // Linear interpolation between keyframes
    Auto,     // Automatically computed smooth tangent
    Smooth,   // Smooth tangent (C1 continuity)
    Flat,     // Zero tangent (horizontal)
    Step,     // Step function — hold previous value until next keyframe
    Custom    // Custom tangent values provided
  };

  inline TangentType parseTangentType(const char* token) {
    if (!token) return TangentType::Linear;
    if (strcmp(token, "linear") == 0)  return TangentType::Linear;
    if (strcmp(token, "auto") == 0)    return TangentType::Auto;
    if (strcmp(token, "smooth") == 0)  return TangentType::Smooth;
    if (strcmp(token, "flat") == 0)    return TangentType::Flat;
    if (strcmp(token, "step") == 0)    return TangentType::Step;
    if (strcmp(token, "custom") == 0)  return TangentType::Custom;
    return TangentType::Linear;
  }

  // -------------------------------------------------------------------------
  // Float curve data (no vec dependency)
  // -------------------------------------------------------------------------

  struct FloatCurveData {
    std::vector<float>  times;
    std::vector<float>  values;
    std::vector<TangentType> inTangentTypes;
    std::vector<TangentType> outTangentTypes;
    std::vector<float>  inTangentValues;
    std::vector<float>  outTangentValues;
    std::vector<float>  inTangentTimes;
    std::vector<float>  outTangentTimes;
    std::vector<bool>   tangentBrokens;

    bool isValid() const {
      return !times.empty() && !values.empty() && times.size() == values.size();
    }
    bool hasFullTangentData() const {
      return inTangentValues.size() == times.size() &&
             outTangentValues.size() == times.size();
    }
    bool hasFullTangentTimeData() const {
      return inTangentTimes.size() == times.size() &&
             outTangentTimes.size() == times.size();
    }
  };

  // -------------------------------------------------------------------------
  // Bezier FCurve evaluation (Newton-Raphson for non-uniform X tangents)
  // -------------------------------------------------------------------------

  /**
   * Evaluate a Bezier FCurve at a given time using Newton-Raphson.
   *
   * Unlike linear interpolation of t, this correctly handles non-uniform
   * X tangents by solving for the parameter t where X(t) = target_time.
   *
   * @param x0, x1, x2, x3  X (time) control points of the Bezier segment
   * @param y0, y1, y2, y3  Y (value) control points of the Bezier segment
   * @param targetTime      The time value to evaluate the curve at
   * @return                The Y value at that time
   */
  inline float evalBezierFCurve(float x0, float x1, float x2, float x3,
                                float y0, float y1, float y2, float y3,
                                float targetTime) {
    if (fabsf(x3 - x0) < 1e-9f) {
      return y0;
    }

    float t = (targetTime - x0) / (x3 - x0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    for (int i = 0; i < 8; ++i) {
      float u = 1.0f - t;
      float u2 = u * u, u3 = u2 * u;
      float t2 = t * t, t3 = t2 * t;

      float x = u3 * x0 + 3.0f * u2 * t * x1 + 3.0f * u * t2 * x2 + t3 * x3;
      float error = x - targetTime;
      if (fabsf(error) < 1e-6f) {
        break;
      }

      float dx = 3.0f * u2 * (x1 - x0) + 6.0f * u * t * (x2 - x1) + 3.0f * t2 * (x3 - x2);
      if (fabsf(dx) < 1e-9f) {
        break;
      }

      t = t - error / dx;
      t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    float u = 1.0f - t;
    float u2 = u * u, u3 = u2 * u;
    float t2 = t * t, t3 = t2 * t;
    return u3 * y0 + 3.0f * u2 * t * y1 + 3.0f * u * t2 * y2 + t3 * y3;
  }

  // -------------------------------------------------------------------------
  // Float curve baking (no vec dependency)
  // -------------------------------------------------------------------------

  inline bool bakeFloatCurve(const FloatCurveData& curve, std::vector<float>& out,
                             uint32_t resolution = kDefaultAnimationResolution,
                             float defaultValue = 0.0f) {
    if (!curve.isValid()) {
      out.resize(resolution, defaultValue);
      return false;
    }

    const bool hasTangents     = curve.hasFullTangentData();
    const bool hasTangentTimes = curve.hasFullTangentTimeData();
    out.resize(resolution);

    for (uint32_t i = 0; i < resolution; ++i) {
      const float u = float(i) / (resolution - 1);
      const size_t idx0 = findKeyframeInterval(curve.times.data(), curve.times.size(), u);
      const size_t idx1 = std::min(idx0 + 1, curve.times.size() - 1);

      if (idx0 == idx1 || curve.times[idx1] == curve.times[idx0]) {
        out[i] = curve.values[idx0];
      } else {
        TangentType outType = TangentType::Linear;
        if (curve.outTangentTypes.size() > idx0) outType = curve.outTangentTypes[idx0];

        if (outType == TangentType::Step) {
          out[i] = curve.values[idx0];
        } else if (hasTangents) {
          const float y0 = curve.values[idx0];
          const float y3 = curve.values[idx1];
          const float y1 = y0 + curve.outTangentValues[idx0];
          const float y2 = y3 + curve.inTangentValues[idx1];

          if (hasTangentTimes) {
            const float fx0 = curve.times[idx0];
            const float fx3 = curve.times[idx1];
            const float fx1 = fx0 + curve.outTangentTimes[idx0];
            const float fx2 = fx3 + curve.inTangentTimes[idx1];
            out[i] = evalBezierFCurve(fx0, fx1, fx2, fx3, y0, y1, y2, y3, u);
          } else {
            const float localT = (u - curve.times[idx0]) / (curve.times[idx1] - curve.times[idx0]);
            out[i] = bezierInterpolate(y0, y1, y2, y3, localT);
          }
        } else {
          const float localT = (u - curve.times[idx0]) / (curve.times[idx1] - curve.times[idx0]);
          out[i] = curveLerp(curve.values[idx0], curve.values[idx1], localT);
        }
      }
    }
    return true;
  }

  // -------------------------------------------------------------------------
  // Templated color/vec utilities
  //
  // Vec2T must have float members: x, y
  // Vec3T must have float members: x, y, z
  // Vec4T must have float members: x, y, z, w
  // All must be aggregate-initializable.
  // -------------------------------------------------------------------------

  /**
   * Gradient data for color animation (simpler than curves — no tangents).
   */
  template<typename Vec4T>
  struct ColorGradientDataT {
    std::vector<float>  times;
    std::vector<Vec4T>  values;

    bool isValid() const {
      return !times.empty() && !values.empty() && times.size() == values.size();
    }
  };

  /**
   * Bake a color gradient to a linear array.
   */
  template<typename Vec4T>
  inline bool bakeColorGradient(const ColorGradientDataT<Vec4T>& gradient,
                                std::vector<Vec4T>& out,
                                uint32_t resolution = kDefaultAnimationResolution,
                                Vec4T defaultValue = Vec4T{1.f, 1.f, 1.f, 1.f}) {
    if (!gradient.isValid()) {
      out.resize(resolution, defaultValue);
      return false;
    }

    out.resize(resolution);
    for (uint32_t i = 0; i < resolution; ++i) {
      const float u = float(i) / (resolution - 1);
      const size_t idx0 = findKeyframeInterval(gradient.times.data(), gradient.times.size(), u);
      const size_t idx1 = std::min(idx0 + 1, gradient.times.size() - 1);

      if (idx0 == idx1 || gradient.times[idx1] == gradient.times[idx0]) {
        out[i] = gradient.values[idx0];
      } else {
        const float t = (u - gradient.times[idx0]) / (gradient.times[idx1] - gradient.times[idx0]);
        const auto& v0 = gradient.values[idx0];
        const auto& v1 = gradient.values[idx1];
        out[i] = Vec4T{
          curveLerp(v0.x, v1.x, t),
          curveLerp(v0.y, v1.y, t),
          curveLerp(v0.z, v1.z, t),
          curveLerp(v0.w, v1.w, t)
        };
      }
    }
    return true;
  }

  /**
   * Combine two baked float channels into a Vec2T array.
   */
  template<typename Vec2T>
  inline bool combineToVec2(const std::vector<float>& xChannel, bool hasX,
                            const std::vector<float>& yChannel, bool hasY,
                            std::vector<Vec2T>& out, Vec2T defaultValue,
                            uint32_t resolution = kDefaultAnimationResolution) {
    if (!hasX && !hasY) {
      out.resize(resolution, defaultValue);
      return false;
    }
    out.resize(resolution);
    for (uint32_t i = 0; i < resolution; ++i) {
      out[i].x = hasX && i < xChannel.size() ? xChannel[i] : defaultValue.x;
      out[i].y = hasY && i < yChannel.size() ? yChannel[i] : defaultValue.y;
    }
    return true;
  }

  /**
   * Combine three baked float channels into a Vec3T array.
   */
  template<typename Vec3T>
  inline bool combineToVec3(const std::vector<float>& xChannel, bool hasX,
                            const std::vector<float>& yChannel, bool hasY,
                            const std::vector<float>& zChannel, bool hasZ,
                            std::vector<Vec3T>& out, Vec3T defaultValue,
                            uint32_t resolution = kDefaultAnimationResolution) {
    if (!hasX && !hasY && !hasZ) {
      out.resize(resolution, defaultValue);
      return false;
    }
    out.resize(resolution);
    for (uint32_t i = 0; i < resolution; ++i) {
      out[i].x = hasX && i < xChannel.size() ? xChannel[i] : defaultValue.x;
      out[i].y = hasY && i < yChannel.size() ? yChannel[i] : defaultValue.y;
      out[i].z = hasZ && i < zChannel.size() ? zChannel[i] : defaultValue.z;
    }
    return true;
  }

} // namespace CurveUtils
