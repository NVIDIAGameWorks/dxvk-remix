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
  X(RtComponentPropertyType::Bool, false, increment, "Increment", "When true, the counter increments by the increment value each frame.") \
  X(RtComponentPropertyType::Float, 1.0f, incrementValue, "Increment Value", "The value to add to the counter each frame when increment is true.", property.optional = true) \
  X(RtComponentPropertyType::Float, 0.0f, defaultValue, "Starting Value", "The initial value of the counter when the component is created.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, count, "", "The current counter value.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, value, "Value", "The current counter value.")


// Manually declaring the class to allow for custom initialize method
class Counter : public RtRegisteredComponentBatch<Counter> {
private:
  REMIX_COMPONENT_GENERATE_PROP_TYPES(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Counter,
    /* the UI name */        "Counter",
    /* the UI categories */  "Transform",
    /* the doc string */     "Counts up by a value every frame when a condition is true.\n\n"
      "Increments a counter by a specified value every frame that the input bool is true. "
      "Use `Starting Value` to set the initial counter value. "
      "Useful for tracking how many frames a condition has been active.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
    /* optional arguments: */
    spec.initialize = initialize; // Initialize callback to set initial value
  )
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final {
    for (size_t i = start; i < end; i++) {
      if (m_increment[i]) {
        m_count[i] += m_incrementValue[i];
      }
      
      m_value[i] = m_count[i];
    }
  }
  
  // Static wrapper for the initialize callback
  static void initialize(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index) {
    static_cast<Counter&>(batch).initializeInstance(context, index);
  }
  void initializeInstance(const Rc<DxvkContext>& context, const size_t index)  {
    // Set the initial counter value based on the default value
    m_count[index] = m_defaultValue[index];
    m_value[index] = m_count[index];
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk

