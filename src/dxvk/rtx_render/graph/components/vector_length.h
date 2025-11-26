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
#include "../../../../util/util_vector.h"
#include "../../../../util/util_math.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, input, "Input", "The value to measure. For vectors, returns length.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, length, "Length", "The length (magnitude) of the input vector.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class VectorLength : public RtRegisteredComponentBatch<VectorLength<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType lengthPropertyType = RtComponentPropertyType::Float;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ VectorLength,
    /* the UI name */        "Vector Length",
    /* the UI categories */  "Transform",
    /* the doc string */     "Calculates the length (magnitude) of a vector.\n\n" \
      "Computes the Euclidean length of the vector using the formula: sqrt(x² + y² + z² + ...).",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void VectorLength::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      m_length[i] = length(m_input[i]);
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Template instantiations are in rtx_component_list.cpp

}  // namespace components
}  // namespace dxvk

