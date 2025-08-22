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
#include "rtx_graph_ogn_writer.h"
#include "rtx_graph_md_writer.h"

namespace dxvk {
namespace {
  template<typename T>
  T parseVector(const std::string& input) {
    if (input.empty()) {
      Logger::err("parseVector: Empty input string");
      return T(0.f);
    }
    
    // Reasonable size limit to prevent DoS
    constexpr size_t MAX_INPUT_SIZE = 1024;
    if (input.length() > MAX_INPUT_SIZE) {
      Logger::err(str::format("parseVector: Input string too long (", input.length(), " > ", MAX_INPUT_SIZE, ")"));
      return T(0.f);
    }
    
    // Validate vector type using template type checking
    static_assert(
      std::is_same_v<T, Vector2> || 
      std::is_same_v<T, Vector3> || 
      std::is_same_v<T, Vector4>,
      "parseVector only supports Vector2, Vector3, Vector4 types"
    );
    
    constexpr size_t expectedComponents = 
      std::is_same_v<T, Vector2> ? 2 :
      std::is_same_v<T, Vector3> ? 3 :
      std::is_same_v<T, Vector4> ? 4 : 0;
    
    // Bounds checking for string operations
    size_t start = input.find('(');
    if (start == std::string::npos) {
      start = 0;
    } else {
      start++;
      if (start >= input.length()) {
        Logger::err("parseVector: Invalid input format - empty after opening parenthesis");
        return T(0.f);
      }
    }

    // Safe pointer arithmetic with bounds checking
    const char* inputEnd = input.c_str() + input.length();
    const char* ptr = input.c_str() + start;
    T result = T(0.f);

    for (size_t i = 0; i < expectedComponents; i++) {
      // Check bounds before parsing
      if (ptr >= inputEnd) {
        Logger::err(str::format("parseVector: Unexpected end of string while parsing component ", i, " from: ", input));
        return T(0.f);
      }
      
      // Use strtof with proper error checking
      char* endptr;
      errno = 0; // Reset errno before strtof
      float value = std::strtof(ptr, &endptr);
      
      // Comprehensive error checking for strtof
      if (endptr == ptr) {
        Logger::err(str::format("parseVector: Failed to parse component ", i, " from string: `", input, "` starting from `", ptr, "`"));
        return T(0.f);
      }
      
      if (errno == ERANGE) {
        Logger::err(str::format("parseVector: Value out of range for component ", i, " in: ", input));
        return T(0.f);
      }
      
      // Check bounds after parsing
      if (endptr > inputEnd) {
        Logger::err(str::format("parseVector: Parsing went beyond input bounds for component ", i));
        return T(0.f);
      }
      
      result[i] = value;
      ptr = endptr;

      // Safe whitespace skipping with bounds checking
      while (ptr < inputEnd && (std::isspace(*ptr) || *ptr == ',')) {
        ptr++;
      }
    }

    return result;
  }
} // namespace

fast_unordered_map<const RtComponentSpec*>& getComponentSpecMap() {
  static fast_unordered_map<const RtComponentSpec*> s_componentSpecs;
  return s_componentSpecs;
}

std::mutex& getComponentSpecMapMutex() {
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

  // Check for duplicate registration and insert
  auto [iterator, wasInserted] = getComponentSpecMap().insert({spec->componentType, spec});
  if (!wasInserted) {
    Logger::err(str::format("Component spec for type ", spec->name, " already registered. Conflicting component spec: ", iterator->second->name));
    assert(false && "Multiple component specs mapped to a single ComponentType.");
  }

  if (!spec->oldNames.empty()) {
    for (const auto& oldName : spec->oldNames) {
      std::string fullOldName = RtComponentPropertySpec::kUsdNamePrefix + oldName;
      RtComponentType oldType = XXH3_64bits(fullOldName.c_str(), fullOldName.size());
      auto [oldIterator, oldWasInserted] = getComponentSpecMap().insert({oldType, spec});
      if (!oldWasInserted) {
        Logger::err(str::format("Component spec for legacy type name ", fullOldName, " already registered. Conflicting component spec: ", oldIterator->second->name));
        assert(false && "Multiple component specs mapped to a single ComponentType.");
      }
    }
  }

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

bool writeAllOGNSchemas(const char* outputFolderPath) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  const auto& map = getComponentSpecMap();
  bool success = true;
  for (const auto& pair : map) {
    const RtComponentSpec* spec = pair.second;
    success &= writeOGNSchema(spec, outputFolderPath);
    success &= writePythonStub(spec, outputFolderPath);
  }
  return success;
}

bool writeAllMarkdownDocs(const char* outputFolderPath) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  const auto& map = getComponentSpecMap();
  bool success = true;
  
  std::vector<const RtComponentSpec*> specs;
  specs.reserve(map.size());
  
  for (const auto& pair : map) {
    const RtComponentSpec* spec = pair.second;
    specs.push_back(spec);
    success &= writeComponentMarkdown(spec, outputFolderPath);
  }
  
  success &= writeMarkdownIndex(specs, outputFolderPath);
  return success;
}

std::ostream& operator << (std::ostream& os, RtComponentPropertyType type) {
  switch (type) {
    case RtComponentPropertyType::Bool: return os << "Bool";
    case RtComponentPropertyType::Float: return os << "Float";
    case RtComponentPropertyType::Float2: return os << "Float2";
    case RtComponentPropertyType::Float3: return os << "Float3";
    case RtComponentPropertyType::Color3: return os << "Color3";
    case RtComponentPropertyType::Color4: return os << "Color4";
    case RtComponentPropertyType::Int32: return os << "Int32";
    case RtComponentPropertyType::Uint32: return os << "Uint32";
    case RtComponentPropertyType::Uint64: return os << "Uint64";
    case RtComponentPropertyType::Prim: return os << "Prim";
    case RtComponentPropertyType::String: return os << "String";
    case RtComponentPropertyType::AssetPath: return os << "AssetPath";
  }
  return os << static_cast<int32_t>(type);
}

std::ostream& operator << (std::ostream& os, RtComponentPropertyIOType type) {
  switch (type) {
    case RtComponentPropertyIOType::Input: return os << "Input";
    case RtComponentPropertyIOType::State: return os << "State";
    case RtComponentPropertyIOType::Output: return os << "Output";
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
  case RtComponentPropertyType::Prim:
    return propertyValueForceType<uint32_t>(std::stoull(str));
  case RtComponentPropertyType::String:
  case RtComponentPropertyType::AssetPath:
    return str;
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
  case RtComponentPropertyType::Bool:   return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Bool>>{};
  case RtComponentPropertyType::Float:  return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>{};
  case RtComponentPropertyType::Float2: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float2>>{};
  case RtComponentPropertyType::Float3: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float3>>{};
  case RtComponentPropertyType::Color3: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color3>>{};
  case RtComponentPropertyType::Color4: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Color4>>{};
  case RtComponentPropertyType::Int32:  return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Int32>>{};
  case RtComponentPropertyType::Uint32: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint32>>{};
  case RtComponentPropertyType::Uint64: return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Uint64>>{};
  case RtComponentPropertyType::Prim:   return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Prim>>{};
  case RtComponentPropertyType::String: return std::vector<std::string>{};
  case RtComponentPropertyType::AssetPath: return std::vector<std::string>{};
  }
  assert(false && "Unknown property type in propertyVectorFromType");
  return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>{}; // fallback
}

} // namespace dxvk

// Export functions for unit testing
bool writeAllOGNSchemas(const char* outputFolderPath) {
  return dxvk::writeAllOGNSchemas(outputFolderPath);
}

bool writeAllMarkdownDocs(const char* outputFolderPath) {
  return dxvk::writeAllMarkdownDocs(outputFolderPath);
} 
