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
  X(RtComponentPropertyType::NumberOrVector, Vector3(0.0f, 0.0f, 1.0f), input, "Input", "The vector to normalize.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, Vector3(0.0f, 0.0f, 1.0f), output, "Output", "The normalized vector with length 1. Returns (0,1), (0,0,1), or (0,0,0,1) if the input vector has zero length.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Normalize : public RtRegisteredComponentBatch<Normalize<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType outputPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Normalize,
    /* the UI name */        "Normalize",
    /* the UI categories */  "Transform",
    /* the doc string */     "Normalizes a vector to have length 1.\n\n" \
      "Divides the vector by its length to produce a unit vector (length 1) in the same direction. " \
      "If the input vector has zero length, returns a default vector to avoid division by zero.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Normalize::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      auto vec = m_input[i];
      float len = length(vec);
      
      if (len > 1e-8f) {
        m_output[i] = vec / len;
      } else {
        // Return default vector for zero-length input
        if constexpr (valuePropertyType == RtComponentPropertyType::Float2) {
          m_output[i] = Vector2(0.0f, 1.0f);
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float3) {
          m_output[i] = Vector3(0.0f, 0.0f, 1.0f);
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float4) {
          m_output[i] = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
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

