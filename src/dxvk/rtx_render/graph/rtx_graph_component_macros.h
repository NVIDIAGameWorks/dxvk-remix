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

#define REMIX_COMPONENT_GENERATE_PROP_TYPE(propertyType, defaultValue, name, uiName, docString, ...) \
  static constexpr RtComponentPropertyType name##PropertyType = propertyType;

#define REMIX_COMPONENT_GENERATE_PROP_TYPES(X_INPUTS, X_STATES, X_OUTPUTS) \
  X_INPUTS(REMIX_COMPONENT_GENERATE_PROP_TYPE) \
  X_STATES(REMIX_COMPONENT_GENERATE_PROP_TYPE) \
  X_OUTPUTS(REMIX_COMPONENT_GENERATE_PROP_TYPE)

#define REMIX_COMPONENT_GENERATE_CLASS_TYPE(propertyType, defaultValue, name, uiName, docString, ...) \
  using name##CppType = RtComponentPropertyTypeToCppType<name##PropertyType>;

// To set optional values, when using these macros, write them as a comma separated list of 
// `property.<name> = <value>`, i.e. `property.minValue = 0.0f, property.maxValue = 1.0f`
// Note: minValue and maxValue are automatically converted to match the property's declared type
#define REMIX_COMPONENT_GENERATE_CONST_MEMBER(propertyType, defaultValue, name, uiName, docString, ...) \
  const std::vector<name##CppType>& m_##name;

// Note: States and Outputs both use mutable references (identical implementation)
#define REMIX_COMPONENT_GENERATE_MUTABLE_MEMBER(propertyType, defaultValue, name, uiName, docString, ...) \
  std::vector<name##CppType>& m_##name;

#define REMIX_COMPONENT_GENERATE_CTOR_ARGS_WITH_COUNTER(propertyType, defaultValue, name, uiName, docString, ...) \
    m_##name(*std::get_if<std::vector<name##CppType>>(&values[indices[ctorIndex++]])),

// Helper macro to reduce duplication between Input/State/Output property specs
#define REMIX_COMPONENT_GENERATE_PROPERTY_SPEC(ioType, usdPrefix, propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    name##PropertyType, /* resolved type */ \
    propertyValueForceType<name##CppType>(defaultValue), \
    RtComponentPropertyIOType::ioType, \
    #name, \
    usdPrefix #name, \
    uiName, \
    docString, \
    propertyType /* declaredType */ \
  },

#define REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_INPUT(propertyType, defaultValue, name, uiName, docString, ...) \
  REMIX_COMPONENT_GENERATE_PROPERTY_SPEC(Input, "inputs:", propertyType, defaultValue, name, uiName, docString, ##__VA_ARGS__)

// Note: State uses "inputs:" prefix due to how omnigraph works
#define REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_STATE(propertyType, defaultValue, name, uiName, docString, ...) \
  REMIX_COMPONENT_GENERATE_PROPERTY_SPEC(State, "inputs:", propertyType, defaultValue, name, uiName, docString, ##__VA_ARGS__)

#define REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_OUTPUT(propertyType, defaultValue, name, uiName, docString, ...) \
  REMIX_COMPONENT_GENERATE_PROPERTY_SPEC(Output, "outputs:", propertyType, defaultValue, name, uiName, docString, ##__VA_ARGS__)

#define REMIX_COMPONENT_GENERATE_RESOLVED_TYPE(propertyType, defaultValue, name, uiName, docString, ...) \
  {#name, name##PropertyType},

#define REMIX_COMPONENT_GENERATE_OPTIONAL_SPEC_PROPERTIES(propertyType, defaultValue, name, uiName, docString, ...) \
  { \
    RtComponentPropertySpec& property = s_spec.properties[index]; \
    __VA_ARGS__; \
    /* Enforce type correctness for minValue and maxValue after they're set */ \
    /* Check if minValue was set (not default) and convert to correct type */ \
    if (property.minValue != kFalsePropertyValue) { \
      property.minValue = convertPropertyValueToType<name##CppType>(property.minValue); \
    } \
    /* Check if maxValue was set (not default) and convert to correct type */ \
    if (property.maxValue != kFalsePropertyValue) { \
      property.maxValue = convertPropertyValueToType<name##CppType>(property.maxValue); \
    } \
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

// Macro for common component body (types, members, constructor, and getStaticSpec)
// This is used when manually declaring a component class (e.g., for flexible types)
// The variable arguments here and in the X_ property list macros are used to pass in optional values.
// See RtComponentSpec and RtComponentPropertySpec in rtx_graph_types.h for the lists of optional values.
#define REMIX_COMPONENT_BODY(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, ...) \
private: \
  X_INPUTS(REMIX_COMPONENT_GENERATE_CLASS_TYPE) \
  X_STATES(REMIX_COMPONENT_GENERATE_CLASS_TYPE) \
  X_OUTPUTS(REMIX_COMPONENT_GENERATE_CLASS_TYPE) \
  X_INPUTS(REMIX_COMPONENT_GENERATE_CONST_MEMBER) \
  X_STATES(REMIX_COMPONENT_GENERATE_MUTABLE_MEMBER) \
  X_OUTPUTS(REMIX_COMPONENT_GENERATE_MUTABLE_MEMBER) \
  const RtGraphBatch& m_batch; \
public: \
  componentClass(const RtGraphBatch& batch, \
            std::vector<RtComponentPropertyVector>& values, \
            const std::vector<size_t>& indices, \
            size_t& ctorIndex) : \
    X_INPUTS(REMIX_COMPONENT_GENERATE_CTOR_ARGS_WITH_COUNTER) \
    X_STATES(REMIX_COMPONENT_GENERATE_CTOR_ARGS_WITH_COUNTER) \
    X_OUTPUTS(REMIX_COMPONENT_GENERATE_CTOR_ARGS_WITH_COUNTER) \
    m_batch(batch) {} \
  /* Static factory function to replace lambda for createComponentBatch */ \
  static std::unique_ptr<RtComponentBatch> createBatch( \
      const RtGraphBatch& batch, \
      std::vector<RtComponentPropertyVector>& values, \
      const std::vector<size_t>& indices) { \
    size_t ctorIndex = 0; \
    return std::make_unique<componentClass>(batch, values, indices, ctorIndex); \
  } \
  static const RtComponentSpec* getStaticSpec() { \
    static std::once_flag s_once_flag; \
    static const std::string s_fullName = RtComponentPropertySpec::kUsdNamePrefix + #componentClass; \
    static RtComponentSpec s_spec = { \
      { \
        X_INPUTS(REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_INPUT) \
        X_STATES(REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_STATE) \
        X_OUTPUTS(REMIX_COMPONENT_GENERATE_PROPERTY_SPEC_OUTPUT) \
      }, \
      XXH3_64bits(s_fullName.c_str(), s_fullName.size()), /* componentType */ \
      versionNumber, \
      s_fullName, /* name */ \
      uiName, \
      categories, \
      docString, \
      { \
        X_INPUTS(REMIX_COMPONENT_GENERATE_RESOLVED_TYPE) \
        X_STATES(REMIX_COMPONENT_GENERATE_RESOLVED_TYPE) \
        X_OUTPUTS(REMIX_COMPONENT_GENERATE_RESOLVED_TYPE) \
      }, /* resolvedTypes */ \
      &componentClass::createBatch \
    }; \
    std::call_once(s_once_flag, [&]() { \
      if (!s_spec.properties.empty()) { \
        size_t index = 0; \
        X_INPUTS(REMIX_COMPONENT_GENERATE_OPTIONAL_SPEC_PROPERTIES) \
        X_STATES(REMIX_COMPONENT_GENERATE_OPTIONAL_SPEC_PROPERTIES) \
        X_OUTPUTS(REMIX_COMPONENT_GENERATE_OPTIONAL_SPEC_PROPERTIES) \
      } \
      { \
        RtComponentSpec& spec = s_spec; \
        __VA_ARGS__; \
      } \
      /* Note: registerComponentSpec() is called by RtRegisteredComponentBatch<Derived>::registerType() */ \
      /* Validate the spec after all properties are set */ \
      if (!s_spec.isValid()) { \
        Logger::err(str::format("Invalid component spec for ", #componentClass)); \
        assert(false && "Invalid component spec " #componentClass); \
      } \
    }); \
    return &s_spec; \
  }

// Standard component with concrete types - creates a single type alias
// The variable arguments here and in the X_ property list macros are used to pass in optional values.
// See RtComponentSpec and RtComponentPropertySpec in rtx_graph_types.h for the lists of optional values.
#define REMIX_COMPONENT(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, ...) \
class componentClass : public RtRegisteredComponentBatch<componentClass> { \
private: \
  REMIX_COMPONENT_GENERATE_PROP_TYPES(X_INPUTS, X_STATES, X_OUTPUTS) \
  REMIX_COMPONENT_BODY(componentClass, uiName, categories, docString, versionNumber, X_INPUTS, X_STATES, X_OUTPUTS, __VA_ARGS__) \
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final; \
};
