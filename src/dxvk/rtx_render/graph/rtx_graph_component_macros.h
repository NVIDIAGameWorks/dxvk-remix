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

#include <vector>

#include "rtx_graph_types.h"
#include "../../dxvk_context.h"

// To set optional values, when using these macros, write them as a comma separated list of 
// `property.<name> = <value>`, i.e. `property.minValue = 0.0f, property.maxValue = 1.0f`
#define WRITE_INPUT_MEMBER(propertyType, defaultValue, name, docString, ...) \
  const std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define WRITE_STATE_MEMBER(propertyType, defaultValue, name, docString, ...) \
  std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define WRITE_OUTPUT_MEMBER(propertyType, defaultValue, name, docString, ...) \
  std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define WRITE_CTOR_ARGS(propertyType, defaultValue, name, docString, index, ...) \
    m_##name(*std::get_if<std::vector<RtComponentPropertyTypeToCppType<propertyType>>>(&values[indices[index++]])),

#define WRITE_CTOR_ARGS_WITH_COUNTER(propertyType, defaultValue, name, docString, ...) \
    WRITE_CTOR_ARGS(propertyType, defaultValue, name, docString, ctorIndex) \

#define WRITE_PROPERTY_SPEC_INPUT(propertyType, defaultValue, name, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::Input, \
    #name, \
    "inputs:" #name, \
    docString \
  },

#define WRITE_PROPERTY_SPEC_STATE(propertyType, defaultValue, name, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::State, \
    #name, \
    "inputs:" #name, \
    docString \
  },

#define WRITE_PROPERTY_SPEC_OUTPUT(propertyType, defaultValue, name, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::Output, \
    #name, \
    "outputs:" #name, \
    docString \
  },

#define WRITE_OPTIONAL_SPEC_PROPERTIES(propertyType, defaultValue, name, docString, ...) \
  { \
    RtComponentPropertySpec& property = s_spec.properties[index]; \
    __VA_ARGS__; \
    index++; \
  } \

#define REMIX_COMPONENT(componentClass, componentName, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, ...) \
class componentClass : public RtRegisteredComponentBatch<componentClass> { \
public: \
  /* Note: these are public to allow optional functions to just invoke static functions,*/ \
  /* rather than requiring the entire optional function to be defined in the macro.*/ \
  X_INPUTS(WRITE_INPUT_MEMBER) \
  X_STATES(WRITE_STATE_MEMBER) \
  X_OUTPUTS(WRITE_OUTPUT_MEMBER) \
  const RtGraphBatch& m_batch; \
  \
  static const RtComponentSpec* getStaticSpec() { \
    static bool s_once = false; \
    static RtComponentSpec s_spec = { \
      { \
        X_INPUTS(WRITE_PROPERTY_SPEC_INPUT) \
        X_STATES(WRITE_PROPERTY_SPEC_STATE) \
        X_OUTPUTS(WRITE_PROPERTY_SPEC_OUTPUT) \
      }, \
      XXH3_64bits(componentName, sizeof(componentName) - 1), \
      versionNumber, \
      componentName, \
      docString, \
      [](const RtGraphBatch& batch, \
          std::vector<RtComponentPropertyVector>& values, \
          const std::vector<size_t>& indices) { \
        size_t ctorIndex = 0; \
        return std::make_unique<componentClass>(batch, values, indices, ctorIndex); \
      } \
    }; \
    if (!s_once) { \
      s_once = true; \
      if (!s_spec.properties.empty()) { \
        size_t index = 0; \
        X_INPUTS(WRITE_OPTIONAL_SPEC_PROPERTIES) \
        X_STATES(WRITE_OPTIONAL_SPEC_PROPERTIES) \
        X_OUTPUTS(WRITE_OPTIONAL_SPEC_PROPERTIES) \
      } \
      { \
        RtComponentSpec& spec = s_spec; \
        __VA_ARGS__; \
      } \
      /* Validate the spec after all properties are set */ \
      if (!s_spec.isValid()) { \
        Logger::err(str::format("Invalid component spec for ", componentName)); \
        assert(false && "Invalid component spec " #componentName); \
      } \
    } \
    return &s_spec; \
  } \
  componentClass(const RtGraphBatch& batch, \
            std::vector<RtComponentPropertyVector>& values, \
            const std::vector<size_t>& indices, \
            size_t& ctorIndex) : \
    X_INPUTS(WRITE_CTOR_ARGS_WITH_COUNTER) \
    X_STATES(WRITE_CTOR_ARGS_WITH_COUNTER) \
    X_OUTPUTS(WRITE_CTOR_ARGS_WITH_COUNTER) \
    m_batch(batch) {} \
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final; \
};
