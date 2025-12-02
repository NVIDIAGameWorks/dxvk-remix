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
#include "../rtx_graph_flexible_types.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0, a, "A","The dividend (value to be divided).") \
  X(RtComponentPropertyType::NumberOrVector, 0, b, "B","The divisor (value to divide by).")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0, quotient, "Quotient", "A / B")

// Manually declaring the class to allow for custom type handling.
// This is needed because the component accepts NumberOrVector.
template <RtComponentPropertyType aPropertyType, RtComponentPropertyType bPropertyType, RtComponentPropertyType quotientPropertyType>
class Divide : public RtRegisteredComponentBatch<Divide<aPropertyType, bPropertyType, quotientPropertyType>> {
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Divide,
    /* the UI name */        "Divide",
    /* the UI categories */  "Transform",
    /* the doc string */     "Divides one number or vector by another.\n\n" \
      "Vector / Number will divide all components of the vector by the number. Vector / vector will divide each piece separately, to create (a.x / b.x, a.y / b.y, ...). Vector / Vector will error if the vectors aren't the same size.\n\n" \
      "Note: Division by zero will produce infinity or NaN.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      // Suppress narrowing conversion warnings - we intentionally allow flexible type conversions
      // This is needed to allow things like Vector3 / int32_t, etc.
      #pragma warning(push)
      #pragma warning(disable: 4244)
      m_quotient[i] = m_a[i] / m_b[i];
      #pragma warning(pop)
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Forward declaration for the instantiation function created by DEFINE_BINARY_OP_COMPONENT_CPP
void createTypeVariantsForDivide();

}  // namespace components
}  // namespace dxvk

