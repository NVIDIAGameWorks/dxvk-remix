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
#include "../../../../util/util_vector.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, value, "Value", "The input value to apply looping to.") \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, minRange, "Min Range", "The minimum value of the looping range.") \
  X(RtComponentPropertyType::NumberOrVector, 1.0f, maxRange, "Max Range", "The maximum value of the looping range.") \
  X(RtComponentPropertyType::Enum, static_cast<uint32_t>(LoopingType::Loop), loopingType, "Looping Type", \
    "How the value should loop within the range.", \
    property.enumValues = kLoopingTypeEnumValues)

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, loopedValue, "Looped Value", "The value with looping applied.") \
  X(RtComponentPropertyType::Bool, false, isReversing, "Is Reversing", "True if the value is in the reverse phase of ping pong looping. If passing `loopedValue` to a `Remap` component, hook this up to `shouldReverse` from that component.")

// Manually declaring the class to support templating
template <RtComponentPropertyType valuePropertyType>
class Loop : public RtRegisteredComponentBatch<Loop<valuePropertyType>> {
private:
  static constexpr RtComponentPropertyType valuePropertyType_ = valuePropertyType;
  static constexpr RtComponentPropertyType minRangePropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType maxRangePropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType loopingTypePropertyType = RtComponentPropertyType::Enum;
  static constexpr RtComponentPropertyType loopedValuePropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType isReversingPropertyType = RtComponentPropertyType::Bool;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Loop,
    /* the UI name */        "Loop",
    /* the UI categories */  "Transform",
    /* the doc string */     "Wraps a number back into a range when it goes outside the boundaries.\n\n"\
      "Applies looping behavior to a value. Value is unchanged if it is inside the range.\n"\
      "Component outputs Min Range if Min Range == Max Range and looping type is not None.\n"\
      "Inverted ranges (max < min) are supported, but the results are undefined and may change without warning.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  void Loop::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      if constexpr (valuePropertyType == RtComponentPropertyType::Float) {
        auto result = applyLooping(m_value[i], m_minRange[i], m_maxRange[i], static_cast<LoopingType>(m_loopingType[i]));
        m_loopedValue[i] = result.first;
        m_isReversing[i] = result.second;
      } else {
        // For vectors, apply looping per component
        const auto& val = m_value[i];
        const auto& minVal = m_minRange[i];
        const auto& maxVal = m_maxRange[i];
        const LoopingType loopType = static_cast<LoopingType>(m_loopingType[i]);
        
        // Track if any component is reversing
        bool anyReversing = false;
        
        if constexpr (valuePropertyType == RtComponentPropertyType::Float2) {
          auto resultX = applyLooping(val.x, minVal.x, maxVal.x, loopType);
          auto resultY = applyLooping(val.y, minVal.y, maxVal.y, loopType);
          m_loopedValue[i] = Vector2(resultX.first, resultY.first);
          anyReversing = resultX.second || resultY.second;
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float3) {
          auto resultX = applyLooping(val.x, minVal.x, maxVal.x, loopType);
          auto resultY = applyLooping(val.y, minVal.y, maxVal.y, loopType);
          auto resultZ = applyLooping(val.z, minVal.z, maxVal.z, loopType);
          m_loopedValue[i] = Vector3(resultX.first, resultY.first, resultZ.first);
          anyReversing = resultX.second || resultY.second || resultZ.second;
        } else if constexpr (valuePropertyType == RtComponentPropertyType::Float4) {
          auto resultX = applyLooping(val.x, minVal.x, maxVal.x, loopType);
          auto resultY = applyLooping(val.y, minVal.y, maxVal.y, loopType);
          auto resultZ = applyLooping(val.z, minVal.z, maxVal.z, loopType);
          auto resultW = applyLooping(val.w, minVal.w, maxVal.w, loopType);
          m_loopedValue[i] = Vector4(resultX.first, resultY.first, resultZ.first, resultW.first);
          anyReversing = resultX.second || resultY.second || resultZ.second || resultW.second;
        }
        
        m_isReversing[i] = anyReversing;
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

