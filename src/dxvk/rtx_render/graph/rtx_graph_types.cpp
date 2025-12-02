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

  // Central registry storing all component specs.
  // Key: base component type (same for all template variants)
  // Value: vector of all variants for that component type
  // Using function-local statics ensures safe initialization order (initialized on first use).
  // Note: safe initialization order is needed to avoid a crash during static init for unit tests.
  static fast_unordered_map<ComponentSpecVariantMap>& getComponentSpecMap() {
    static fast_unordered_map<ComponentSpecVariantMap> s_componentSpecs;
    return s_componentSpecs;
  }
  
  static std::mutex& getComponentSpecMapMutex() {
    static std::mutex s_mutex;
    return s_mutex;
  }

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

  // Get or create the variant vector for this base component type
  ComponentSpecVariantMap& variantVec = getComponentSpecMap()[spec->componentType];
  
  // Check for duplicate registration by comparing resolvedTypes
  for (const auto* existingSpec : variantVec) {
    if (existingSpec->resolvedTypes == spec->resolvedTypes) {
      // If it's the exact same spec pointer, this is idempotent - just return
      if (existingSpec == spec) {
        return;
      }
      // Different spec with same resolved types is an error
      Logger::err(str::format("Component spec variant for type ", spec->name, 
                             " already registered with different spec pointer. Conflicting component spec: ", existingSpec->name));
      assert(false && "Duplicate component spec variant registration with different pointer.");
      return;
    }
  }
  
  // Add this variant to the vector
  variantVec.push_back(spec);

  // Register old names (they point to the same variant vector)
  if (!spec->oldNames.empty()) {
    for (const auto& oldName : spec->oldNames) {
      std::string fullOldName = RtComponentPropertySpec::kUsdNamePrefix + oldName;
      RtComponentType oldType = XXH3_64bits(fullOldName.c_str(), fullOldName.size());
      
      // Get or create the variant vector for this old name
      ComponentSpecVariantMap& oldVariantVec = getComponentSpecMap()[oldType];
      
      // Check for duplicate registration of old name
      bool alreadyRegistered = false;
      for (const auto* existingSpec : oldVariantVec) {
        if (existingSpec->resolvedTypes == spec->resolvedTypes) {
          // If it's the exact same spec pointer, this is idempotent - skip adding again
          if (existingSpec == spec) {
            alreadyRegistered = true;
            break;
          }
          // Different spec with same resolved types is an error
          Logger::err(str::format("Component spec variant for legacy type name ", fullOldName, 
                                 " already registered with different spec pointer. ",
                                 "Conflicting component spec: ", existingSpec->name));
          assert(false && "Duplicate component spec variant registration for old name with different pointer.");
          return;
        }
      }
      
      // Add to old name variant vector (if not already present)
      if (!alreadyRegistered) {
        oldVariantVec.push_back(spec);
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
  auto baseIter = getComponentSpecMap().find(componentType);
  if (baseIter == getComponentSpecMap().end()) {
    return nullptr;
  }
  
  const ComponentSpecVariantMap& variantVec = baseIter->second;
  if (variantVec.empty()) {
    return nullptr;
  }
  
  // Return the first variant (for non-templated components, there's only one)
  // For templated components, caller should use getAllComponentSpecVariants and search
  return variantVec[0];
}

// Returns empty static vector if component type not found
static const ComponentSpecVariantMap s_emptyVariantVec;

const ComponentSpecVariantMap& getAllComponentSpecVariants(const RtComponentType& componentType) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  auto baseIter = getComponentSpecMap().find(componentType);
  if (baseIter == getComponentSpecMap().end()) {
    return s_emptyVariantVec;
  }
  
  return baseIter->second;
}

const RtComponentSpec* getAnyComponentSpecVariant(const RtComponentType& componentType) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  auto baseIter = getComponentSpecMap().find(componentType);
  if (baseIter == getComponentSpecMap().end()) {
    return nullptr;
  }
  
  const ComponentSpecVariantMap& variantVec = baseIter->second;
  if (variantVec.empty()) {
    return nullptr;
  }
  
  // Return any variant (the first one)
  return variantVec[0];
}

bool writeAllOGNSchemas(const char* outputFolderPath) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  const auto& map = getComponentSpecMap();
  bool success = true;
  
  // Track which base components we've already written (to avoid duplicates from oldNames)
  std::unordered_set<std::string> writtenBaseComponents;
  
  for (const auto& [componentType, variantVec] : map) {
    // Get any variant (all variants have the same name)
    const RtComponentSpec* spec = variantVec.empty() ? nullptr : variantVec[0];
    
    if (spec == nullptr) {
      continue;
    }
    
    // Skip if we've already written this base component (handles oldNames)
    if (writtenBaseComponents.count(spec->name)) {
      continue;
    }
    writtenBaseComponents.insert(spec->name);
    
    success &= writeOGNSchema(spec, componentType, variantVec, outputFolderPath);
    success &= writePythonStub(spec, componentType, variantVec, outputFolderPath);
  }
  return success;
}

bool writeAllMarkdownDocs(const char* outputFolderPath) {
  std::lock_guard<std::mutex> lock(getComponentSpecMapMutex());
  const auto& map = getComponentSpecMap();
  bool success = true;
  
  std::vector<const RtComponentSpec*> specs;
  std::unordered_set<std::string> writtenBaseComponents;
  
  for (const auto& [componentType, variantVec] : map) {
    // Get any variant for the index
    const RtComponentSpec* spec = variantVec.empty() ? nullptr : variantVec[0];
    
    if (spec == nullptr) {
      continue;
    }
    
    // Skip if we've already written this base component (handles oldNames)
    if (writtenBaseComponents.count(spec->name)) {
      continue;
    }
    writtenBaseComponents.insert(spec->name);
    
    specs.push_back(spec);
    
    // Write documentation including all variants
    success &= writeComponentMarkdown(spec, componentType, variantVec, outputFolderPath);
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
    case RtComponentPropertyType::Float4: return os << "Float4";
    case RtComponentPropertyType::Enum: return os << "Enum";
    case RtComponentPropertyType::String: return os << "String";
    case RtComponentPropertyType::AssetPath: return os << "AssetPath";
    case RtComponentPropertyType::Hash: return os << "Hash";
    case RtComponentPropertyType::Prim: return os << "Prim";
    case RtComponentPropertyType::Any: return os << "Any";
    case RtComponentPropertyType::NumberOrVector: return os << "NumberOrVector";
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
  try {
    switch (type) {
    case RtComponentPropertyType::Bool:
      return (str == "true" || str == "True" || str == "TRUE" || str == "1") ? kTruePropertyValue : kFalsePropertyValue;
    case RtComponentPropertyType::Float:
      return std::stof(str);
    case RtComponentPropertyType::Float2:
      return parseVector<Vector2>(str);
    case RtComponentPropertyType::Float3:
      return parseVector<Vector3>(str);
    case RtComponentPropertyType::Float4:
      return parseVector<Vector4>(str);
    case RtComponentPropertyType::Enum:
      return propertyValueForceType<uint32_t>(std::stoul(str));\
    case RtComponentPropertyType::String:
    case RtComponentPropertyType::AssetPath:
      return str;
    case RtComponentPropertyType::Hash:
      // Hash is stored as uint64_t but represented as a token in USD/OGN
      // Parse as hex (base 16) - works with or without 0x prefix
      return propertyValueForceType<uint64_t>(std::stoull(str, nullptr, 16));
    case RtComponentPropertyType::Prim:
      // Should never be reached (prim properties should be UsdRelationships, so they shouldn't ever have a string value).  Just in case, return an invalid value.
      return kInvalidRtComponentPropertyValue;
    case RtComponentPropertyType::Any:
    case RtComponentPropertyType::NumberOrVector:
      // Flexible types should not be parsed from strings directly
      Logger::err(str::format("Flexible types (Any, NumberOrVector) cannot be parsed from strings directly. type: ", type, ", string: ", str));
      return kInvalidRtComponentPropertyValue;
    }
    Logger::err(str::format("Unknown property type in propertyValueFromString.  type: ", type, ", string: ", str));
  } catch (const std::invalid_argument& e) {
    Logger::err(str::format("propertyValueFromString: Invalid argument for type ", type, " conversion: '", str, "' - ", e.what()));
  } catch (const std::out_of_range& e) {
    Logger::err(str::format("propertyValueFromString: Out of range for type ", type, " conversion: '", str, "' - ", e.what()));
  }
  assert(false && "Error parsing component property value in propertyValueFromString.");
  return kInvalidRtComponentPropertyValue;
}

/**
 * Creates a RtComponentPropertyVector with the appropriate vector type based on the RtComponentPropertyType.
 *
 * @param type The RtComponentPropertyType enum value
 * @return RtComponentPropertyVector with the correct vector type initialized
 */
RtComponentPropertyVector propertyVectorFromType(const RtComponentPropertyType type) {
  switch (type) {
  case RtComponentPropertyType::Bool:      return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Bool>>{};
  case RtComponentPropertyType::Float:     return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float>>{};
  case RtComponentPropertyType::Float2:    return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float2>>{};
  case RtComponentPropertyType::Float3:    return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float3>>{};
  case RtComponentPropertyType::Float4:    return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Float4>>{};
  case RtComponentPropertyType::Enum:      return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Enum>>{};
  case RtComponentPropertyType::String:    return std::vector<std::string>{};
  case RtComponentPropertyType::AssetPath: return std::vector<std::string>{};
  case RtComponentPropertyType::Hash:      return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Hash>>{};
  case RtComponentPropertyType::Prim:      return std::vector<RtComponentPropertyTypeToCppType<RtComponentPropertyType::Prim>>{};
  case RtComponentPropertyType::Any:
  case RtComponentPropertyType::NumberOrVector:
    // Flexible types should not be used to create property vectors directly
    Logger::err(str::format("Flexible types (Any, NumberOrVector) cannot be used to create property vectors directly. type: ", type));
    return std::vector<float>{}; // fallback
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
