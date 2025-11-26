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

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, false, triggerToggle, "Trigger Toggle", "When this is true, the toggle switches to its opposite state (on becomes off, or off becomes on). Set this to true each time you want to flip the switch.") \
  X(RtComponentPropertyType::Bool, false, defaultState, "Starting State", "The initial state of the toggle when the component is created. Set to true to start in the 'on' state, or false to start in the 'off' state.")

#define LIST_STATES(X) \

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, isOn, "Is On", "The current state of the toggle: true means 'on', false means 'off'. This starts at the `Starting State` value and changes each time `Trigger Toggle` becomes true.") \


// Manually declaring the class to allow for custom initialize method
class Toggle : public RtRegisteredComponentBatch<Toggle> {
private:
  REMIX_COMPONENT_GENERATE_PROP_TYPES(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Toggle,
    /* the UI name */        "Toggle",
    /* the UI categories */  "Transform",
    /* the doc string */     "A switch that alternates between on (true) and off (false) states.\n\n"
      "Think of this like a light switch: each frame `Trigger Toggle` is true, the switch flips to the opposite position. "
      "Use `Starting State` to choose whether the switch begins in the on or off position.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
    /* optional arguments: */
    spec.initialize = initialize; // Initialize callback to set initial state
  )
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final {
    for (size_t i = start; i < end; i++) {
      if (m_triggerToggle[i]) {
        m_isOn[i] = !m_isOn[i];
      }
    }
  }
  
  // Static wrapper for the initialize callback
  static void initialize(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index) {
    static_cast<Toggle&>(batch).initializeInstance(context, index);
  }
  void initializeInstance(const Rc<DxvkContext>& context, const size_t index)  {
    // Set the initial state of the toggle based on the default state
    m_isOn[index] = m_defaultState[index];
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk
