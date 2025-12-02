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
  X(RtComponentPropertyType::NumberOrVector, 0.0f, input, "Input", "The value to detect changes from.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, previousValue, "", "The value from the previous frame.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, velocity, "Velocity", "The change in value from the previous frame (current - previous) / deltaTime.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Velocity : public RtRegisteredComponentBatch<Velocity<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType previousValuePropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType velocityPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Velocity,
    /* the UI name */        "Velocity",
    /* the UI categories */  "Transform",
    /* the doc string */     "Detects the rate of change of a value from frame to frame.\n\n" \
      "Calculates the difference between the current value and the previous frame's value. " \
      "Outputs the change per frame (velocity = (current - previous) / deltaTime).",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Velocity::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  float deltaTime = GlobalTime::get().deltaTime();
  for (size_t i = start; i < end; i++) {
      // Calculate velocity as the change from previous frame
      m_velocity[i] = (m_input[i] - m_previousValue[i]) / deltaTime;
      
      // Store current value for next frame
      m_previousValue[i] = m_input[i];
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk

