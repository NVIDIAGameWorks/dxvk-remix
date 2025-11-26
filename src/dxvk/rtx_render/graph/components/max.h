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
#include "../../../../util/util_math.h"
#include "../../../../util/util_vector.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.f, a, "A", "The first value.") \
  X(RtComponentPropertyType::NumberOrVector, 0.f, b, "B", "The second value.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.f, result, "Result", "The maximum of A and B.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Max : public RtRegisteredComponentBatch<Max<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType aPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType bPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType resultPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Max,
    /* the UI name */        "Max",
    /* the UI categories */  "Transform",
    /* the doc string */     "Returns the larger of two values.\n\n" \
      "Outputs the maximum value between A and B.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Max::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      if constexpr (valuePropertyType == RtComponentPropertyType::Float) {
        m_result[i] = std::max(m_a[i], m_b[i]);
      } else {
        // For vectors, max each component
        const auto& valA = m_a[i];
        const auto& valB = m_b[i];
        
        if constexpr (valuePropertyType == RtComponentPropertyType::Float2) {
          m_result[i] = Vector2(std::max(valA.x, valB.x), std::max(valA.y, valB.y));
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float3) {
          m_result[i] = Vector3(std::max(valA.x, valB.x), std::max(valA.y, valB.y), std::max(valA.z, valB.z));
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float4) {
          m_result[i] = Vector4(std::max(valA.x, valB.x), std::max(valA.y, valB.y), std::max(valA.z, valB.z), std::max(valA.w, valB.w));
        }
      }
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Template instantiations are in rtx_component_list.cpp

}  // namespace components
}  // namespace dxvk

