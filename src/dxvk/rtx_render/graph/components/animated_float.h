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
#include "animation_utils.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled","If true, the float will be animated.", property.optional = true) \
  X(RtComponentPropertyType::Float, 0.0f, initialValue, "Initial Value", "The value at time t=0.") \
  X(RtComponentPropertyType::Float, 1.0f, finalValue, "Final Value", "The value at time t=duration.") \
  X(RtComponentPropertyType::Float, 1.0f, duration, "Duration", "How long it takes to animate from initial value to final value, in seconds.", property.minValue = 0.000001f) \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(LoopingType::Loop), loopingType, "Looping Type", \
    "What happens when the float reaches the final value.", \
    property.enumValues = kLoopingTypeEnumValues) \
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(InterpolationType::Linear), interpolation, "Interpolation", \
    "How the float will change over time.", \
    property.optional = true, \
    property.enumValues = kInterpolationTypeEnumValues) \

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, accumulatedTime, "", "How much time has passed since the animation started.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, currentValue, "Current Value", "The animated float value.")

REMIX_COMPONENT( \
  /* the Component name */ AnimatedFloat, \
  /* the UI name */        "Animated Float", \
  /* the UI categories */  "Transform", \
  /* the doc string */     "A single animated float value.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void AnimatedFloat::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  float deltaTime = GlobalTime::get().deltaTime();
  
  for (size_t i = start; i < end; i++) {
    if (m_enabled[i]) {
      if (m_duration[i] <= 0.0f) {
        m_currentValue[i] = m_finalValue[i];
        ONCE(Logger::err(str::format("AnimatedFloat: Duration must be positive. Setting current value to final value")));
        continue;
      }
      
      m_accumulatedTime[i] += deltaTime;
      
      // Use utility enums directly
      InterpolationType interpolation = static_cast<InterpolationType>(m_interpolation[i]);
      LoopingType loopingType = static_cast<LoopingType>(m_loopingType[i]);
      
      // Apply the looping
      auto loopResult = applyLooping(m_accumulatedTime[i], 0.0f, m_duration[i], loopingType);

      // normalize the looped time
      float loopedTime = loopResult.first;
      float normalizedTime = loopedTime / m_duration[i];
      const bool isReversing = loopResult.second;
      if (isReversing) {
        normalizedTime = 1.0f - normalizedTime;
      }
      float easedTime = applyInterpolation(interpolation, normalizedTime);
      if (isReversing) {
        easedTime = 1.0f - easedTime;
      }
      m_currentValue[i] = lerp(m_initialValue[i], m_finalValue[i], easedTime);
    }
  }
}

}  // namespace components
}  // namespace dxvk
