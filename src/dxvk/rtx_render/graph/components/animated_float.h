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

#include "../rtx_graph_component_macros.h"
#include "../../../util/util_globaltime.h"

namespace dxvk {
namespace components {

enum class AnimatedFloatLooping  : uint32_t{
  Loop = 0,
  PingPong = 1,
  Continue = 2,
  Freeze = 3,
};

enum class AnimatedFloatInterpolation  : uint32_t{
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

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled","If true, the float will be animated.", property.optional = true) \
  X(RtComponentPropertyType::Float, 0.0f, initialValue, "Initial Value", "The value at time t=0.") \
  X(RtComponentPropertyType::Float, 1.0f, finalValue, "Final Value", "The value at time t=duration.") \
  X(RtComponentPropertyType::Float, 1.0f, duration, "Duration", "How long it takes to animate from initial value to final value, in seconds.", property.minValue = 0.000001f) \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(AnimatedFloatLooping::Loop), loopingType, "Looping Type", \
    "What happens when the float reaches the final value.", \
    property.enumValues = { \
      {"Loop", {AnimatedFloatLooping::Loop, "The value will return to the initial value."}}, \
      {"PingPong", {AnimatedFloatLooping::PingPong, "The value will play in reverse until it reaches the initial value, then loop."}}, \
      {"Continue", {AnimatedFloatLooping::Continue, "The value will continue accumulating (a linear animation will preserve the velocity)."}}, \
      {"Freeze", {AnimatedFloatLooping::Freeze, "The value will freeze at the final value."}} \
    }) \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(AnimatedFloatInterpolation::Linear), interpolation, "Interpolation", \
    "How the float will change over time.", \
    property.optional = true, \
    property.enumValues = { \
      {"Linear", {AnimatedFloatInterpolation::Linear, "The float will have a constant velocity."}}, \
      {"Cubic", {AnimatedFloatInterpolation::Cubic, "The float will change in a cubic curve over time."}}, \
      {"EaseIn", {AnimatedFloatInterpolation::EaseIn, "The float will start slow, then accelerate."}}, \
      {"EaseOut", {AnimatedFloatInterpolation::EaseOut, "The float will start fast, then decelerate."}}, \
      {"EaseInOut", {AnimatedFloatInterpolation::EaseInOut, "The float will start slow, accelerate, then decelerate."}}, \
      {"Sine", {AnimatedFloatInterpolation::Sine, "Smooth, natural motion using sine wave."}}, \
      {"Exponential", {AnimatedFloatInterpolation::Exponential, "Dramatic acceleration effect."}}, \
      {"Bounce", {AnimatedFloatInterpolation::Bounce, "Bouncy, playful motion."}}, \
      {"Elastic", {AnimatedFloatInterpolation::Elastic, "Spring-like motion."}} \
    }) \

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, accumulatedTime, "", "How much time has passed since the animation started.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, currentValue, "Current Value", "The animated float value.")

REMIX_COMPONENT( \
  /* the Component name */ AnimatedFloat, \
  /* the UI name */        "Animated Float", \
  /* the UI categories */  "animation", \
  /* the doc string */     "A single animated float value.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

float applyInterpolation(AnimatedFloatInterpolation interpolation, float time) {
  switch(interpolation) {
    case AnimatedFloatInterpolation::Linear:
      return time;
    case AnimatedFloatInterpolation::Cubic:
      return time * time * time;
    case AnimatedFloatInterpolation::EaseIn:
      return time * time;
    case AnimatedFloatInterpolation::EaseOut:
      return 1.0f - (1.0f - time) * (1.0f - time);
    case AnimatedFloatInterpolation::EaseInOut:
      return time < 0.5f ? 2.0f * time * time : 1.0f - 2.0f * (1.0f - time) * (1.0f - time);
    case AnimatedFloatInterpolation::Sine:
      return sin(time * M_PI * 0.5f);
    case AnimatedFloatInterpolation::Exponential:
      return time == 0.0f ? 0.0f : pow(2.0f, 10.0f * (time - 1.0f));
    case AnimatedFloatInterpolation::Bounce:
      return 1.0f - pow(1.0f - time, 3.0f) * cos(time * M_PI * 3.0f);
    case AnimatedFloatInterpolation::Elastic:
      return pow(2.0f, -10.0f * time) * sin((time - 0.075f) * M_PI * 2.0f / 0.3f) + 1.0f;
    default:
      ONCE(Logger::err(str::format("AnimatedFloat: Unknown interpolation type: ", static_cast<uint32_t>(interpolation))));
      return time; // fallback to linear
  }
}

void AnimatedFloat::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  float deltaTime = GlobalTime::get().deltaTime();
  // simple example update function.
  for (size_t i = start; i < end; i++) {
    if (m_enabled[i]) {
      if (m_duration[i] <= 0.0f) {
        m_currentValue[i] = m_finalValue[i];
        ONCE(Logger::err(str::format("AnimatedFloat: Duration must be positive. Setting current value to final value")));
        continue;
      }
      m_accumulatedTime[i] += deltaTime;
      float time = 0.0f;
      AnimatedFloatInterpolation interpolation = static_cast<AnimatedFloatInterpolation>(m_interpolation[i]);
      switch(static_cast<AnimatedFloatLooping>(m_loopingType[i])) {
        case AnimatedFloatLooping::Loop:
          if (m_accumulatedTime[i] >= m_duration[i]) {
            m_accumulatedTime[i] = fmod(m_accumulatedTime[i], m_duration[i]);
          }
          time = m_accumulatedTime[i] / m_duration[i];
          m_currentValue[i] = lerp(m_initialValue[i], m_finalValue[i], applyInterpolation(interpolation, time));
          break;
        case AnimatedFloatLooping::PingPong:
          if (m_accumulatedTime[i] >= m_duration[i] * 2.0f) {
            m_accumulatedTime[i] = fmod(m_accumulatedTime[i], m_duration[i] * 2.0f);
          }
          time = m_accumulatedTime[i];
          if (time > m_duration[i]) {
            time = m_duration[i] * 2.0f - time;
          }
          time = time / m_duration[i];
          m_currentValue[i] = lerp(m_initialValue[i], m_finalValue[i], applyInterpolation(interpolation, time));
          break;
        case AnimatedFloatLooping::Continue:
          time = fmod(m_accumulatedTime[i], m_duration[i]) / m_duration[i];
          m_currentValue[i] = lerp(m_initialValue[i], m_finalValue[i], applyInterpolation(interpolation, time));
          m_currentValue[i] += (m_finalValue[i] - m_initialValue[i]) * std::floor(m_accumulatedTime[i] / m_duration[i]);
          break;
        case AnimatedFloatLooping::Freeze:
          if (m_accumulatedTime[i] >= m_duration[i]) {
            m_currentValue[i] = m_finalValue[i];
          } else {
            m_currentValue[i] = lerp(m_initialValue[i], m_finalValue[i], applyInterpolation(interpolation, m_accumulatedTime[i] / m_duration[i]));
          }
          break;
        default:
          ONCE(Logger::err(str::format("AnimatedFloat: Unknown looping type: ", static_cast<uint32_t>(m_loopingType[i]))));
          break;
      }
    }
  }
}

}  // namespace components
}  // namespace dxvk
