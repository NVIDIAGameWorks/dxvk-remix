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
#include <mutex>

#include "rtx_graph_types.h"
#include "../../dxvk_context.h"

// To set optional values, when using these macros, write them as a comma separated list of 
// `property.<name> = <value>`, i.e. `property.minValue = 0.0f, property.maxValue = 1.0f`
#define REMIX_COMPONENT_WRITE_INPUT_MEMBER(propertyType, defaultValue, name, uiName, docString, ...) \
  const std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define REMIX_COMPONENT_WRITE_STATE_MEMBER(propertyType, defaultValue, name, uiName, docString, ...) \
  std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define REMIX_COMPONENT_WRITE_OUTPUT_MEMBER(propertyType, defaultValue, name, uiName, docString, ...) \
  std::vector<RtComponentPropertyTypeToCppType<propertyType>>& m_##name;

#define REMIX_COMPONENT_WRITE_CTOR_ARGS(propertyType, defaultValue, name, uiName, docString, index, ...) \
    m_##name(*std::get_if<std::vector<RtComponentPropertyTypeToCppType<propertyType>>>(&values[indices[index++]])),

#define REMIX_COMPONENT_WRITE_CTOR_ARGS_WITH_COUNTER(propertyType, defaultValue, name, uiName, docString, ...) \
    REMIX_COMPONENT_WRITE_CTOR_ARGS(propertyType, defaultValue, name, uiName, docString, ctorIndex) \

#define REMIX_COMPONENT_WRITE_PROPERTY_SPEC_INPUT(propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::Input, \
    #name, \
    "inputs:" #name, \
    uiName, \
    docString \
  },

#define REMIX_COMPONENT_WRITE_PROPERTY_SPEC_STATE(propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::State, \
    #name, \
    "inputs:" #name, /* This needs to be inputs: due to how omnigraph works. */\
    uiName, \
    docString \
  },

#define REMIX_COMPONENT_WRITE_PROPERTY_SPEC_OUTPUT(propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    propertyType, \
    propertyValueForceType<RtComponentPropertyTypeToCppType<propertyType>>(defaultValue), \
    RtComponentPropertyIOType::Output, \
    #name, \
    "outputs:" #name, \
    uiName, \
    docString \
  },

#define REMIX_COMPONENT_WRITE_OPTIONAL_SPEC_PROPERTIES(propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    RtComponentPropertySpec& property = s_spec.properties[index]; \
    __VA_ARGS__; \
    if (!property.oldUsdNames.empty()) { \
      std::string prefix = "inputs:"; \
      if (property.ioType == RtComponentPropertyIOType::Output) { \
        prefix = "outputs:"; \
      } \
      for (std::string& oldName : property.oldUsdNames) { \
        oldName = prefix + oldName; \
      } \
    } \
    index++; \
  } \

#define REMIX_COMPONENT_WRITE_CLASS_MEMBERS(X_INPUTS, X_STATES, X_OUTPUTS) \
  X_INPUTS(REMIX_COMPONENT_WRITE_INPUT_MEMBER) \
  X_STATES(REMIX_COMPONENT_WRITE_STATE_MEMBER) \
  X_OUTPUTS(REMIX_COMPONENT_WRITE_OUTPUT_MEMBER) \
  const RtGraphBatch& m_batch;

#define REMIX_COMPONENT_WRITE_STATIC_SPEC(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, ...) \
  static std::once_flag s_once_flag; \
  static const std::string s_fullName = RtComponentPropertySpec::kUsdNamePrefix + #componentClass; \
  static RtComponentSpec s_spec = { \
    { \
      X_INPUTS(REMIX_COMPONENT_WRITE_PROPERTY_SPEC_INPUT) \
      X_STATES(REMIX_COMPONENT_WRITE_PROPERTY_SPEC_STATE) \
      X_OUTPUTS(REMIX_COMPONENT_WRITE_PROPERTY_SPEC_OUTPUT) \
    }, \
    XXH3_64bits(s_fullName.c_str(), s_fullName.size()), \
    versionNumber, \
    s_fullName, \
    uiName, \
    categories, \
    docString, \
    [](const RtGraphBatch& batch, \
        std::vector<RtComponentPropertyVector>& values, \
        const std::vector<size_t>& indices) { \
      size_t ctorIndex = 0; \
      return std::make_unique<componentClass>(batch, values, indices, ctorIndex); \
    } \
  }; \
  std::call_once(s_once_flag, [&]() { \
    if (!s_spec.properties.empty()) { \
      size_t index = 0; \
      X_INPUTS(REMIX_COMPONENT_WRITE_OPTIONAL_SPEC_PROPERTIES) \
      X_STATES(REMIX_COMPONENT_WRITE_OPTIONAL_SPEC_PROPERTIES) \
      X_OUTPUTS(REMIX_COMPONENT_WRITE_OPTIONAL_SPEC_PROPERTIES) \
    } \
    { \
      RtComponentSpec& spec = s_spec; \
      __VA_ARGS__; \
    } \
    /* Validate the spec after all properties are set */ \
    if (!s_spec.isValid()) { \
      Logger::err(str::format("Invalid component spec for ", #componentClass)); \
      assert(false && "Invalid component spec " #componentClass); \
    } \
  }); \
  return &s_spec;

#define REMIX_COMPONENT_WRITE_CTOR(componentClass, X_INPUTS, X_STATES, X_OUTPUTS) \
  componentClass(const RtGraphBatch& batch, \
            std::vector<RtComponentPropertyVector>& values, \
            const std::vector<size_t>& indices, \
            size_t& ctorIndex) : \
    X_INPUTS(REMIX_COMPONENT_WRITE_CTOR_ARGS_WITH_COUNTER) \
    X_STATES(REMIX_COMPONENT_WRITE_CTOR_ARGS_WITH_COUNTER) \
    X_OUTPUTS(REMIX_COMPONENT_WRITE_CTOR_ARGS_WITH_COUNTER) \
    m_batch(batch) {} \

#define REMIX_COMPONENT(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, ...) \
class componentClass : public RtRegisteredComponentBatch<componentClass> { \
private: \
  REMIX_COMPONENT_WRITE_CLASS_MEMBERS(X_INPUTS, X_STATES, X_OUTPUTS) \
public: \
  REMIX_COMPONENT_WRITE_CTOR(componentClass, X_INPUTS, X_STATES, X_OUTPUTS) \
  static const RtComponentSpec* getStaticSpec() { \
    REMIX_COMPONENT_WRITE_STATIC_SPEC(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, __VA_ARGS__) \
  } \
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final; \
};

