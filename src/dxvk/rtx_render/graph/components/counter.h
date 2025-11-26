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
  X(RtComponentPropertyType::Float, 1.0f, incrementValue, "Increment Value", "The value to add to the counter each frame when increment is true.", property.optional = true)

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, count, "", "The current counter value.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, value, "Value", "The current counter value.")

REMIX_COMPONENT( \
  /* the Component name */ Counter, \
  /* the UI name */        "Counter", \
  /* the UI categories */  "Transform", \
  /* the doc string */     "Counts up by a value every frame when a condition is true.\n\n" \
    "Increments a counter by a specified value every frame that the input bool is true. " \
    "Useful for tracking how many frames a condition has been active.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void Counter::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    if (m_increment[i]) {
      m_count[i] += m_incrementValue[i];
    }
    
    m_value[i] = m_count[i];
  }
}

}  // namespace components
}  // namespace dxvk

