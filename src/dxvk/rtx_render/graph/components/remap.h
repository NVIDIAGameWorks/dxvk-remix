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
  X(RtComponentPropertyType::Float, 0.0f, value, "Value", "The input value to interpolate.") \
  X(RtComponentPropertyType::Float, 0.0f, inputMin, "Input Min", "If `Value` equals `Input Min`, the output will be `Output Min`.") \
  X(RtComponentPropertyType::Float, 1.0f, inputMax, "Input Max", "If `Value` equals `Input Max`, the output will be `Output Max`.") \
  X(RtComponentPropertyType::Bool, false, clampInput, "Clamp Input", "If true, `value` will be clamped to the input range.", property.optional = true) \
  X(RtComponentPropertyType::Enum, static_cast<uint32_t>(InterpolationType::Linear), easingType, "Easing Type", \
    "The type of easing to apply.", \
    property.enumValues = kInterpolationTypeEnumValues) \
  X(RtComponentPropertyType::Bool, false, shouldReverse, "Should Reverse", "If true, the easing is applied backwards. If `Value` is coming from a Loop component that is using `pingpong`, hook this up to `isReversing` from that component.", property.optional = true) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, outputMin, "Output Min", "What a `Value` of `Input Min` maps to.") \
  X(RtComponentPropertyType::NumberOrVector, 1.0f, outputMax, "Output Max", "What a `Value` of `Input Max` maps to.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, output, "Output", "The final remapped value after applying input normalization, easing, and output mapping.")

// Manually declaring the class to allow for custom applySceneOverrides method
template <RtComponentPropertyType rangePropertyType>
class Remap : public RtRegisteredComponentBatch<Remap<rangePropertyType>> {
private:
  static constexpr RtComponentPropertyType valuePropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType inputMinPropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType inputMaxPropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType clampInputPropertyType = RtComponentPropertyType::Bool;
  static constexpr RtComponentPropertyType easingTypePropertyType = RtComponentPropertyType::Enum;
  static constexpr RtComponentPropertyType shouldReversePropertyType = RtComponentPropertyType::Bool;
  static constexpr RtComponentPropertyType outputMinPropertyType = rangePropertyType;
  static constexpr RtComponentPropertyType outputMaxPropertyType = rangePropertyType;
  static constexpr RtComponentPropertyType outputPropertyType = rangePropertyType;
  REMIX_COMPONENT_BODY(
    /* the Component class name */ Remap,
    /* the UI name */        "Remap",
    /* the UI categories */  "Transform",
    /* the doc string */     "Smoothly maps a value from one range to another range with customizable easing curves.\n\n" \
      "Remaps a value from an input range to an output range with optional easing. " \
      "Values will be normalized (mapped from input range to 0-1), eased (changed from linear to some curve), then mapped (0-1 value to output range).\n\n" \
      "Note: Input values outside of input range are valid, and easing can lead to the output value being " \
      "outside of the output range even when input is inside the input range.\n\n" \
      "Inverted ranges (max < min) are supported.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
    spec.oldNames = {"InterpolateFloat"} // TODO: remove this after new versions of the demo are shared.
  )
  void Remap::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      // Step 1: Normalize input value to 0-1 range (reverse LERP / float_to_strength)
      float normalizedValue = m_value[i];
      if (m_inputMax[i] == m_inputMin[i]) {
        ONCE(Logger::err(str::format("Remap: Input Min and Input Max are the same. Setting normalized value to 0.0f. Input Min: ", m_inputMin[i], " Input Max: ", m_inputMax[i])));
        normalizedValue = 0.0f; // Avoid division by zero
      } else {
        if (m_clampInput[i]) {
          if (m_inputMin[i] > m_inputMax[i]) {
            normalizedValue = clamp(normalizedValue, m_inputMax[i], m_inputMin[i]);
          } else {
            normalizedValue = clamp(normalizedValue, m_inputMin[i], m_inputMax[i]);
          }
        }
        normalizedValue = (normalizedValue - m_inputMin[i]) / (m_inputMax[i] - m_inputMin[i]);
      }
      
      // cache in a const value to avoid double branch
      const bool shouldReverse = m_shouldReverse[i];
  
      // Step 2: Apply easing (ease component logic)
      // If should reverse, flip the input value
      if (shouldReverse) {
        normalizedValue = 1.0f - normalizedValue;
      }
      
      // Apply the easing
      float easedValue = applyInterpolation(static_cast<InterpolationType>(m_easingType[i]), normalizedValue);
      
      // If should reverse, flip the output back
      if (shouldReverse) {
        easedValue = 1.0f - easedValue;
      }
      
      // Step 3: Map eased value to output range (LERP / strength_to_float)
      m_output[i] = lerp(m_outputMin[i], m_outputMax[i], easedValue);
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

}  // namespace components
}  // namespace dxvk

