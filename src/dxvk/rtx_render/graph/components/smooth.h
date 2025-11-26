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
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, input, "Input", "The value to smooth.") \
  X(RtComponentPropertyType::Float, 0.1f, smoothingFactor, "Smoothing Factor",\
    "The smoothing factor (0-1000). 0 means output never changes. Larger values = faster changes.\n\n"\
    "Time for output to be within 1% of input for different factors:\n"\
    "- 1: 6.6 seconds\n"\
    "- 10: 0.66 seconds\n"\
    "- 100: 0.066 seconds\n"\
    "- 1000: 0.0066 seconds\n\n"\
    "Formula: output = lerp(input, previousOutput, exp2(-smoothingFactor*deltaTime))",\
    property.minValue = 0.0f, property.maxValue = 1000.0f, property.optional = true)

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, initialized, "", "Tracks if the smooth value has been initialized.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, output, "Output", "The smoothed output value.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Smooth : public RtRegisteredComponentBatch<Smooth<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType smoothingFactorPropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType initializedPropertyType = RtComponentPropertyType::Bool;
  static constexpr RtComponentPropertyType outputPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Smooth,
    /* the UI name */        "Smooth",
    /* the UI categories */  "Transform",
    /* the doc string */     "Applies exponential smoothing to a value over time.\n\n" \
      "Uses a moving average filter to smooth out rapid changes in the input value. " \
      "The smoothing factor controls how much smoothing is applied: 0 means output never changes. Larger values = faster changes. \n",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Smooth::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    float deltaTime = GlobalTime::get().deltaTime();
    for (size_t i = start; i < end; i++) {
      // On the first frame, initialize smoothedValue to the input value to avoid lerping from 0
      if (!m_initialized[i]) {
        m_output[i] = m_input[i];
        m_initialized[i] = true;
        continue;
      }
      
      float factor = std::clamp(m_smoothingFactor[i], 0.0f, 1000.0f);
      // using a framerate independent smoothing algorithm from https://www.gamedeveloper.com/programming/improved-lerp-smoothing-
      factor = exp2(-factor*deltaTime);
      m_output[i] = lerp(m_input[i], m_output[i], factor);
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk

