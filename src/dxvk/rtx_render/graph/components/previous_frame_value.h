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
  X(RtComponentPropertyType::Any, 0.0f, input, "Input", "The value to store for the next frame.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Any, 0.0f, previousValue, "", "The value from the previous frame.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Any, 0.0f, output, "Output", "The value from the previous frame.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class PreviousFrameValue : public RtRegisteredComponentBatch<PreviousFrameValue<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType previousValuePropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType outputPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ PreviousFrameValue,
    /* the UI name */        "Previous Frame Value",
    /* the UI categories */  "Transform",
    /* the doc string */     "Outputs the value from the previous frame.\n\n" \
      "Stores the input value and outputs it on the next frame. " \
      "Useful for detecting changes between frames or implementing delay effects.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void PreviousFrameValue::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      // Output the stored value from the previous frame
      m_output[i] = m_previousValue[i];
      
      // Store the current input for the next frame
      m_previousValue[i] = m_input[i];
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Template instantiations are in rtx_component_list.cpp

}  // namespace components
}  // namespace dxvk

