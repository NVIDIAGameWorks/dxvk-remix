/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include <cmath>
#include <algorithm>
#include <map>
#include <string>

#include "../../../../util/util_math.h"
#include "../rtx_graph_types.h"

namespace dxvk {
namespace components {

// Shared enums
enum class LoopingType : uint32_t {
  Loop = 0,
  PingPong = 1,
  NoLoop = 2,
  Clamp = 3,
};

// Shared enum value maps for component properties
inline const auto kLoopingTypeEnumValues = RtComponentPropertySpec::EnumPropertyMap{
  {"Loop", {LoopingType::Loop, "The value will wrap around from max to min."}},
  {"PingPong", {LoopingType::PingPong, "The value will bounce back and forth between min and max."}},
  {"NoLoop", {LoopingType::NoLoop, "The value will be unchanged."}},
  {"Clamp", {LoopingType::Clamp, "The value will be clamped to the range."}}
};

enum class InterpolationType : uint32_t {
  Linear = 0,
  Cubic = 1,
  EaseIn = 2,
  EaseOut = 3,
  EaseInOut = 4,
  Sine = 5,
  Exponential = 6,
  Bounce = 7,
  Elastic = 8,
};

inline const auto kInterpolationTypeEnumValues = RtComponentPropertySpec::EnumPropertyMap{
  {"Linear", {InterpolationType::Linear, "The float will have a constant velocity."}},
  {"Cubic", {InterpolationType::Cubic, "The float will change in a cubic curve over time."}},
  {"EaseIn", {InterpolationType::EaseIn, "The float will start slow, then accelerate."}},
  {"EaseOut", {InterpolationType::EaseOut, "The float will start fast, then decelerate."}},
  {"EaseInOut", {InterpolationType::EaseInOut, "The float will start slow, accelerate, then decelerate."}},
  {"Sine", {InterpolationType::Sine, "Smooth, natural motion using a sine wave."}},
  {"Exponential", {InterpolationType::Exponential, "Dramatic acceleration effect."}},
  {"Bounce", {InterpolationType::Bounce, "Bouncy, playful motion."}},
  {"Elastic", {InterpolationType::Elastic, "Spring-like motion."}}
};

// Apply interpolation/easing to a normalized time value (0-1)
inline float applyInterpolation(InterpolationType interpolation, float time) {
  switch(interpolation) {
    case InterpolationType::Linear:
      return time;
    case InterpolationType::Cubic:
      return time * time * time;
    case InterpolationType::EaseIn:
      return time * time;
    case InterpolationType::EaseOut:
      return 1.0f - (1.0f - time) * (1.0f - time);
    case InterpolationType::EaseInOut:
      return time < 0.5f ? 2.0f * time * time : 1.0f - 2.0f * (1.0f - time) * (1.0f - time);
    case InterpolationType::Sine:
      return sin(time * M_PI * 0.5f);
    case InterpolationType::Exponential:
      return time == 0.0f ? 0.0f : pow(2.0f, 10.0f * (time - 1.0f));
    case InterpolationType::Bounce:
      return 1.0f - pow(1.0f - time, 3.0f) * cos(time * M_PI * 3.0f);
    case InterpolationType::Elastic:
      return pow(2.0f, -10.0f * time) * sin((time - 0.075f) * M_PI * 2.0f / 0.3f) + 1.0f;
    default:
      return time; // fallback to linear
  }
}

// Maps an arbitrary float value to a restricted range, based on min/max range and looping type
// Returns the looped value and whether it's in reverse (for ping pong)
inline std::pair<float, bool> applyLooping(float value, float minRange, float maxRange, LoopingType loopingType) {
  if (unlikely(maxRange == minRange && loopingType != LoopingType::NoLoop)) {
    return {minRange, false}; // No range to loop in
  }
  
  float range = maxRange - minRange;
  float normalizedValue = (value - minRange) / range;
  bool isReversing = false;
  
  switch(loopingType) {
    case LoopingType::Loop:
      // Basically modulo
      normalizedValue = normalizedValue - std::floor(normalizedValue);
      break;
    case LoopingType::PingPong: {
      // Similar to loop, but every other cycle goes in reverse
      float cyclePosition = normalizedValue - std::floor(normalizedValue / 2.0f) * 2.0f;
      if (cyclePosition >= 1.0f) {
        normalizedValue = 2.0f - cyclePosition;
        isReversing = true;
      } else {
        normalizedValue = cyclePosition;
      }
      break;
    }
    case LoopingType::NoLoop:
      // No modification - let the value continue beyond range
      return {value, false};
    case LoopingType::Clamp:
      // Clamps the value to the range.
      normalizedValue = clamp(normalizedValue, 0.0f, 1.0f);
      break;
    default:
      normalizedValue = clamp(normalizedValue, 0.0f, 1.0f);
      break;
  }
  
  float loopedValue = minRange + normalizedValue * range;
  return {loopedValue, isReversing};
}

}  // namespace components
}  // namespace dxvk
