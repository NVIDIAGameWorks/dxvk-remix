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
#include "animation_utils.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, false, value, "Value", "An input boolean.  Every time this goes from false to true, the count is incremented.") \
  X(RtComponentPropertyType::Float, 0.0f, resetValue, "Reset Value", "If count reaches this value, it is reset to 0.  Does nothing if left as 0.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, true, prevFrameValue, "", "The value of the boolean from the previous frame.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, count, "Count", "The current count value.")


REMIX_COMPONENT( \
  /* the Component name */ CountToggles, \
  /* the UI name */        "Count Toggles", \
  /* the UI categories */  "Transform", \
  /* the doc string */     "Counts how many times an input switches from off to on.\n\n" \
    "Tracks the number of times a boolean input transitions from false to true, useful for counting button presses or state changes.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void CountToggles::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    if (m_value[i] && !m_prevFrameValue[i]) {
      m_count[i] += 1.0f;
      if (m_resetValue[i] > 0.0f && m_count[i] >= m_resetValue[i]) {
        m_count[i] = 0.0f;
      }
    }
    m_prevFrameValue[i] = m_value[i];
  }
}

}  // namespace components
}  // namespace dxvk
