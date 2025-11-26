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
  X(RtComponentPropertyType::NumberOrVector, 0.f, value, "Value", "The value to clamp.") \
  X(RtComponentPropertyType::Float, 0.f, minValue, "Min Value", "The minimum allowed value.") \
  X(RtComponentPropertyType::Float, 1.f, maxValue, "Max Value", "The maximum allowed value.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.f, result, "Result", "The clamped value, constrained to [Min Value, Max Value].")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Clamp : public RtRegisteredComponentBatch<Clamp<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType valuePropertyType_ = valuePropertyType;
  static constexpr RtComponentPropertyType minValuePropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType maxValuePropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType resultPropertyType = valuePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Clamp,
    /* the UI name */        "Clamp",
    /* the UI categories */  "Transform",
    /* the doc string */     "Constrains a value to a specified range.\n\n" \
      "If the value is less than Min Value, returns Min Value. " \
      "If the value is greater than Max Value, returns Max Value. " \
      "Otherwise, returns the value unchanged. Applies to each component of a vector individually.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Clamp::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      if constexpr (valuePropertyType == RtComponentPropertyType::Float) {
        m_result[i] = std::clamp(m_value[i], m_minValue[i], m_maxValue[i]);
      } else {
        // For vectors, clamp each component
        const auto& val = m_value[i];
        const float& minVal = m_minValue[i];
        const float& maxVal = m_maxValue[i];
        
        if constexpr (valuePropertyType == RtComponentPropertyType::Float2) {
          m_result[i] = Vector2(
            std::clamp(val.x, minVal, maxVal),
            std::clamp(val.y, minVal, maxVal)
          );
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float3) {
          m_result[i] = Vector3(
            std::clamp(val.x, minVal, maxVal),
            std::clamp(val.y, minVal, maxVal),
            std::clamp(val.z, minVal, maxVal)
          );
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float4) {
          m_result[i] = Vector4(
            std::clamp(val.x, minVal, maxVal),
            std::clamp(val.y, minVal, maxVal),
            std::clamp(val.z, minVal, maxVal),
            std::clamp(val.w, minVal, maxVal)
          );
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

