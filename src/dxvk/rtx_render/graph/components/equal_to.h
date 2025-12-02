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
#include "../rtx_graph_flexible_types.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0, a, "A","The first value to compare.") \
  X(RtComponentPropertyType::NumberOrVector, 0, b, "B","The second value to compare.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, result, "Result", "True if A == B, false otherwise")

// Manually declaring the class to allow for custom type handling.
// This is needed because the component accepts NumberOrVector.
template <RtComponentPropertyType aPropertyType, RtComponentPropertyType bPropertyType>
class EqualTo : public RtRegisteredComponentBatch<EqualTo<aPropertyType, bPropertyType>> {
private:
  static constexpr RtComponentPropertyType resultPropertyType = RtComponentPropertyType::Bool;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ EqualTo,
    /* the UI name */        "Equal To",
    /* the UI categories */  "Transform",
    /* the doc string */     "Returns true if A is equal to B, false otherwise.\n\n" \
      "For floating point values, this performs exact equality comparison. Vector == Vector compares all components.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      // Suppress narrowing conversion warnings - we intentionally allow flexible type conversions
      // This is needed to allow things like Vector3 == int32_t, etc.
      #pragma warning(push)
      #pragma warning(disable: 4244)
      m_result[i] = m_a[i] == m_b[i];
      #pragma warning(pop)
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Forward declaration for the instantiation function created by DEFINE_COMPARISON_OP_COMPONENT_CPP
void createTypeVariantsForEqualTo();

}  // namespace components
}  // namespace dxvk

