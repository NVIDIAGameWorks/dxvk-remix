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
  X(RtComponentPropertyType::Uint32, static_cast<uint32_t>(InterpolationType::Linear), easingType, "Easing Type", \
    "The type of easing to apply.", \
    property.enumValues = kInterpolationTypeEnumValues) \
  X(RtComponentPropertyType::Bool, false, shouldReverse, "Should Reverse", "If true, the easing is applied backwards. If `Value` is coming from a loopFloat component that is using `pingpong`, hook this up to `isReversing` from that component.", property.optional = true) \
  X(RtComponentPropertyType::Float, 0.0f, outputMin, "Output Min", "What a `Value` of `Input Min` maps to.") \
  X(RtComponentPropertyType::Float, 1.0f, outputMax, "Output Max", "What a `Value` of `Input Max` maps to.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, interpolatedValue, "Interpolated Value", "The final interpolated value after applying input normalization, easing, and output mapping.")

REMIX_COMPONENT( \
  /* the Component name */ InterpolateFloat, \
  /* the UI name */        "Interpolate Float", \
  /* the UI categories */  "Transform", \
  /* the doc string */     "Interpolates a value from an input range to an output range with optional easing. " \
    "\nCombines normalization (reverse LERP), easing, and mapping (LERP) into a single component. " \
    "\n\nNote input values outside of input range are valid, and that easing can lead to the output value being " \
    "outside of the output range even when input is inside the input range." \
    "\nInverted input ranges (Input Max < Input Min) are supported - the min/max will be swapped and the normalized value inverted.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void InterpolateFloat::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  for (size_t i = start; i < end; i++) {
    // Step 1: Normalize input value to 0-1 range (reverse LERP / float_to_strength)
    float normalizedValue = m_value[i];
    bool shouldInvert = false;
    
    // Check if the range is inverted
    float minVal = m_inputMin[i];
    float maxVal = m_inputMax[i];
    if (minVal > maxVal) {
      // Swap min and max
      std::swap(minVal, maxVal);
      shouldInvert = true;
    }
    
    if (maxVal == minVal) {
      ONCE(Logger::err(str::format("InterpolateFloat: Input Min and Input Max are the same. Setting normalized value to 0.0f. Input Min: ", m_inputMin[i], " Input Max: ", m_inputMax[i])));
      normalizedValue = 0.0f; // Avoid division by zero
    } else {
      if (m_clampInput[i]) {
        normalizedValue = clamp(normalizedValue, minVal, maxVal);
      }
      normalizedValue = (normalizedValue - minVal) / (maxVal - minVal);
      
      // Invert normalized value if the range was inverted
      if (shouldInvert) {
        normalizedValue = 1.0f - normalizedValue;
      }
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
    m_interpolatedValue[i] = lerp(m_outputMin[i], m_outputMax[i], easedValue);
  }
}

}  // namespace components
}  // namespace dxvk
