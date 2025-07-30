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

#include "../rtx_graph_component_macros.h"
#include "../../../util/util_globaltime.h"

namespace dxvk {

enum class AnimatedFloatLooping  : uint32_t{
  Never = 0,
  ByValue = 1,
  ByTime = 2
};

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "If true, the float will be animated.") \
  X(RtComponentPropertyType::Float, 0.0f, initialValue, "The input float.") \
  X(RtComponentPropertyType::Float, 0.0f, speed, "The speed of the animation, per second.") \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(AnimatedFloatLooping::Never), loopingType, "How the looping is controlled.", \
    property.enumValues = { \
      {"Never", uint32_t(AnimatedFloatLooping::Never)}, \
      {"Loop by value", uint32_t(AnimatedFloatLooping::ByValue)}, \
      {"Loop by time", uint32_t(AnimatedFloatLooping::ByTime)} \
    }) \
  X(RtComponentPropertyType::Float, 0.0f, loopingValue, "The time or value at which to loop back to the initial value.  If looping by time, this is the time in seconds.  If looping by value, this is the value at which to loop.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, state, "If looping by time, this is how much time has passed since the last time the animation looped.  If looping by value, this is abs(current value - initial value).")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, currentValue, "The animated float value.")

REMIX_COMPONENT( \
  /* the C++ class name */ AnimatedFloatComponent, \
  /* the USD name */       "remix.animated.float", \
  /* the doc string */     "A single animated float value.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void AnimatedFloatComponent::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  float deltaTime = GlobalTime::get().deltaTime();
  // simple example update function.
  for (size_t i = start; i < end; i++) {
    if (m_enabled[i]) {
      switch(static_cast<AnimatedFloatLooping>(m_loopingType[i])) {
        case AnimatedFloatLooping::Never:
          m_state[i] += m_speed[i] * deltaTime;
          m_currentValue[i] = m_state[i] + m_initialValue[i];
          break;
        case AnimatedFloatLooping::ByValue:
          if (m_speed[i] * (m_loopingValue[i] - m_initialValue[i]) <= 0.f) {
            // Speed is positive while looping value is lower than initial value, or vice versa.
            // this means we're never going to reach the looping value, so just set to initial value.
            m_currentValue[i] = m_initialValue[i];
          } else {
            m_state[i] = fmod(m_state[i] + m_speed[i] * deltaTime, m_loopingValue[i] - m_initialValue[i]);
            m_currentValue[i] = m_initialValue[i] - m_state[i];
          }
          break;
        case AnimatedFloatLooping::ByTime:
          m_state[i] += deltaTime;
          m_currentValue[i] = m_initialValue[i] + m_speed[i] * fmod(m_state[i], m_loopingValue[i]);
          break;
        default:
          ONCE(Logger::err(str::format("AnimatedFloatComponent: Unknown looping type: ", m_loopingType[i])));
          break;
      }
    }
  }
}

}  // namespace dxvk
