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
  X(RtComponentPropertyType::Bool, false, condition, "Condition", "If true, output A. If false, output B.") \
  X(RtComponentPropertyType::Any, 0.0f, inputA, "Input A", "The value to output when condition is true.") \
  X(RtComponentPropertyType::Any, 0.0f, inputB, "Input B", "The value to output when condition is false.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Any, 0.0f, output, "Output", "The selected value based on the condition.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Select : public RtRegisteredComponentBatch<Select<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType conditionPropertyType = RtComponentPropertyType::Bool;
  static constexpr RtComponentPropertyType inputAPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType inputBPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType outputPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Select,
    /* the UI name */        "Select",
    /* the UI categories */  "Transform",
    /* the doc string */     "Selects between two values based on a boolean condition.\n\n" \
      "If the condition is true, outputs Input A. If the condition is false, outputs Input B. " \
      "Acts like a ternary operator or if-else statement.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Select::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      m_output[i] = m_condition[i] ? m_inputA[i] : m_inputB[i];
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Template instantiations are in rtx_component_list.cpp

}  // namespace components
}  // namespace dxvk

