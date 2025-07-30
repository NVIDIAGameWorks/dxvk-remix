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

#include "rtx_graph_types.h"

#include <ostream>
#include <mutex>
#include <sstream>
#include <algorithm>

#include "../util/util_enum.h"
#include "../util/util_string.h"

namespace dxvk {
namespace {
  template<typename T>
  T parseVector(const std::string& input) {
    // Skip opening parenthesis
    size_t start = input.find('(');
    if (start == std::string::npos) {
      start = 0;
    } else {
      start++;
    }

    // Parse numbers directly
    const char* ptr = input.c_str() + start;
    T result = T(0.f);
    size_t numComponents = sizeof(T) / sizeof(float);

    for (size_t i = 0; i < numComponents; i++) {
      char* endptr;
      float value = std::strtof(ptr, &endptr);

      // Check if strtof failed to parse a number
      if (endptr == ptr) {
        Logger::err(str::format("Failed to parse ", typeid(T).name(), " component ", i, " from string: `", input, "` starting from `", ptr, "`"));
        return T(0.f);
      }

      result[i] = value;
      ptr = endptr;

      // Skip whitespace (and stop if the character is the null terminator)
      while (*ptr != '\0' && (std::isspace(*ptr) || *ptr == ',')) {
        ptr++;
      }

      // Check if we unexpectedly reached end of string
      if (*ptr == '\0' && i < numComponents - 1) {
        Logger::err(str::format("Unexpected end of string while parsing ", typeid(T).name(), " component ", i, " from string: ", input));
        return T(0.f);
      }
    }

    return result;
  }
} // namespace

fast_unordered_map<const RtComponentSpec*>& getComponentSpecMap() {
  static fast_unordered_map<const RtComponentSpec*> s_componentSpecs;
  return s_componentSpecs;
}

static std::mutex& getComponentSpecMapMutex() {
  static std::mutex mtx;
  return mtx;
}

void registerComponentSpec(const RtComponentSpec* spec) {
  if (spec == nullptr) {
    Logger::err("Cannot register null component spec");
    return;
  }

  if (!spec->isValid()) {
    Logger::err(str::format("Cannot register invalid component spec: ", spec->name));
    return;
  }

  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());

  // Check for duplicate registration
  auto existing = getComponentSpecMap().find(spec->componentType);
  if (existing != getComponentSpecMap().end()) {
    Logger::err(str::format("Component spec for type ", spec->name, " already registered. Conflicting component spec: ", existing->second->name));
    assert(false && "Multiple component specs mapped to a single ComponentType.");
  }

  getComponentSpecMap()[spec->componentType] = spec;
}

const RtComponentSpec* getComponentSpec(const RtComponentType& componentType) {
  if (componentType == kInvalidComponentType) {
    Logger::err("Cannot get component spec for invalid component type");
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  auto iter = getComponentSpecMap().find(componentType);
  if (iter == getComponentSpecMap().end()) {
    return nullptr;
  }
  return iter->second;
}

std::ostream& operator << (std::ostream& os, RtComponentPropertyType type) {
  switch (type) {
    ENUM_NAME(RtComponentPropertyType::Bool);
    ENUM_NAME(RtComponentPropertyType::Float);
    ENUM_NAME(RtComponentPropertyType::Float2);
    ENUM_NAME(RtComponentPropertyType::Float3);
    ENUM_NAME(RtComponentPropertyType::Color3);
    ENUM_NAME(RtComponentPropertyType::Color4);
    ENUM_NAME(RtComponentPropertyType::Int32);
    ENUM_NAME(RtComponentPropertyType::Uint32);
    ENUM_NAME(RtComponentPropertyType::Uint64);
    ENUM_NAME(RtComponentPropertyType::MeshInstance);
    ENUM_NAME(RtComponentPropertyType::LightInstance);
    ENUM_NAME(RtComponentPropertyType::GraphInstance);
  }
  return os << static_cast<int32_t>(type);
}

std::ostream& operator << (std::ostream& os, RtComponentPropertyIOType type) {
  switch (type) {
    ENUM_NAME(RtComponentPropertyIOType::Input);
    ENUM_NAME(RtComponentPropertyIOType::State);
    ENUM_NAME(RtComponentPropertyIOType::Output);
  }
  return os << static_cast<int32_t>(type);
}

RtComponentPropertyValue propertyValueFromString(const std::string& str, const RtComponentPropertyType type) {
  switch (type) {
  case RtComponentPropertyType::Bool:
    return (str == "true" || str == "True" || str == "TRUE" || str == "1") ? uint8_t(1) : uint8_t(0);
  case RtComponentPropertyType::Float:
    return std::stof(str);
  case RtComponentPropertyType::Float2:
    return parseVector<Vector2>(str);
  case RtComponentPropertyType::Float3:
    return parseVector<Vector3>(str);
  case RtComponentPropertyType::Color3:
    return parseVector<Vector3>(str);
  case RtComponentPropertyType::Color4:
    return parseVector<Vector4>(str);
  case RtComponentPropertyType::Int32:
    return propertyValueForceType<int32_t>(std::stoi(str));
  case RtComponentPropertyType::Uint32:
    return propertyValueForceType<uint32_t>(std::stoul(str));
  case RtComponentPropertyType::Uint64:
    return propertyValueForceType<uint64_t>(std::stoull(str));
  case RtComponentPropertyType::MeshInstance:
    return propertyValueForceType<uint32_t>(std::stoull(str));
  case RtComponentPropertyType::LightInstance:
    return propertyValueForceType<uint32_t>(std::stoull(str));
  case RtComponentPropertyType::GraphInstance:
    return propertyValueForceType<uint32_t>(std::stoull(str));
  }
  Logger::err(str::format("Unknown property type in propertyValueFromString.  type: ", type, ", string: ", str));
  assert(false && "Unknown property type in propertyValueFromString");
  return RtComponentPropertyValue();
}

/**
 * Creates a RtComponentPropertyVector with the appropriate vector type based on the RtComponentPropertyType.
 *
 * @param type The RtComponentPropertyType enum value
 * @return RtComponentPropertyVector with the correct vector type initialized
 */
RtComponentPropertyVector propertyVectorFromType(const RtComponentPropertyType type) {
  switch (type) {
  case RtComponentPropertyType::Bool:         return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Bool>>{};
  case RtComponentPropertyType::Float:        return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>{};
  case RtComponentPropertyType::Float2:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float2>>{};
  case RtComponentPropertyType::Float3:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float3>>{};
  case RtComponentPropertyType::Color3:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color3>>{};
  case RtComponentPropertyType::Color4:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color4>>{};
  case RtComponentPropertyType::Int32:        return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Int32>>{};
  case RtComponentPropertyType::Uint32:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>{};
  case RtComponentPropertyType::Uint64:       return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint64>>{};
  case RtComponentPropertyType::MeshInstance: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::MeshInstance>>{};
  case RtComponentPropertyType::LightInstance: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::LightInstance>>{};
  case RtComponentPropertyType::GraphInstance: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::GraphInstance>>{};
  }
  assert(false && "Unknown property type in propertyVectorFromType");
  return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>{}; // fallback
}

} // namespace dxvk 
