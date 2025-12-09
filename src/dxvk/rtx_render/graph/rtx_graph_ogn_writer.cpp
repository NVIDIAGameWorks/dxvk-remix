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

#include "rtx_graph_ogn_writer.h"
#include "../util/util_env.h"
#include "../util/util_filesys.h"
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace dxvk {
namespace {
// Helper function to escape JSON strings
std::string escapeJsonString(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;      
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += c; break;
    }
  }
  return output;
}

// Helper function to convert RtComponentPropertyType to OGN type string
std::string propertyTypeToOgnType(RtComponentPropertyType type) {
  switch (type) {
    case RtComponentPropertyType::Bool: return "bool";
    case RtComponentPropertyType::Float: return "float";
    case RtComponentPropertyType::Float2: return "float[2]";
    case RtComponentPropertyType::Float3: return "float[3]";
    case RtComponentPropertyType::Float4: return "float[4]";
    case RtComponentPropertyType::Enum: return "token";
    case RtComponentPropertyType::String: return "token";
    case RtComponentPropertyType::AssetPath: return "token";
    case RtComponentPropertyType::Hash: return "token";
    case RtComponentPropertyType::Prim: return "target";  // USD Relationship to a prim
    case RtComponentPropertyType::Any: return "any";  // Flexible type
    case RtComponentPropertyType::NumberOrVector: return "numeric_array_elements";  // Flexible type
  }
  return "unknown";
}

// Helper function to convert OGN type string to BaseDataType enum name for Python
std::string ognTypeToBaseDataType(const std::string& ognType) {
  if (ognType == "bool") return "BOOL";
  if (ognType == "float") return "FLOAT";
  if (ognType == "float[2]") return "FLOAT, 2";
  if (ognType == "float[3]") return "FLOAT, 3";
  if (ognType == "float[4]") return "FLOAT, 4";
  if (ognType == "colorf[3]") return "FLOAT, 3";
  if (ognType == "colorf[4]") return "FLOAT, 4";
  if (ognType == "int") return "INT";
  if (ognType == "uint") return "UINT";
  if (ognType == "uint64") return "UINT64";
  if (ognType == "double") return "DOUBLE";
  if (ognType == "double[2]") return "DOUBLE, 2";
  if (ognType == "double[3]") return "DOUBLE, 3";
  if (ognType == "double[4]") return "DOUBLE, 4";
  if (ognType == "token") return "TOKEN";
  if (ognType == "target") return "RELATIONSHIP";
  if (ognType == "unknown") return "FLOAT";  // Fallback for unknown types
  return "FLOAT";  // Default fallback
}

// Helper function to get all possible OGN types for a flexible property
// Returns a JSON array string like ["float", "int", "float[2]", "float[3]", "colorf[3]"]
std::string getFlexiblePropertyTypeUnion(const ComponentSpecVariantMap& variants, const std::string& propertyName) {
  if (variants.empty()) {
    return "";
  }
  
  // Collect all unique types for this property across all variants
  std::set<std::string> uniqueTypes;
  for (const auto* variant : variants) {
    if (variant->resolvedTypes.empty()) {
      continue;  // Not a templated component
    }
    
    auto it = variant->resolvedTypes.find(propertyName);
    if (it != variant->resolvedTypes.end()) {
      std::string ognType = propertyTypeToOgnType(it->second);
      uniqueTypes.insert(ognType);
    }
  }
  
  // Build JSON array string
  if (uniqueTypes.empty()) {
    return "";
  }

  if (uniqueTypes.size() == 1) {
    return "\"" + *uniqueTypes.begin() + "\"";
  }

  if (uniqueTypes.find("target") != uniqueTypes.end()) {
    // Target is special, and cannot be contained in a union type - so just output `any`.
    return "\"any\"";
  }
  
  std::string result = "[";
  bool first = true;
  for (const auto& type : uniqueTypes) {
    if (!first) {
      result += ", ";
    }
    result += "\"" + type + "\"";
    first = false;
  }
  result += "]";
  
  return result;
}

// Helper function to get default value as JSON string
std::string getDefaultValueAsJson(const RtComponentPropertyValue& value, RtComponentPropertyType type) {
  switch (type) {
    case RtComponentPropertyType::Bool:
      return std::get<uint32_t>(value) ? "true" : "false";
    case RtComponentPropertyType::Float:
      return std::to_string(std::get<float>(value));
    case RtComponentPropertyType::Float2: {
      const auto& vec = std::get<Vector2>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), "]");
    }
    case RtComponentPropertyType::Float3: {
      const auto& vec = std::get<Vector3>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), ", ",
                          std::to_string(vec.z), "]");
    }
    case RtComponentPropertyType::Float4: {
      const auto& vec = std::get<Vector4>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), ", ",
                          std::to_string(vec.z), ", ",
                          std::to_string(vec.w), "]");
    }
    case RtComponentPropertyType::Enum:
      return std::to_string(std::get<uint32_t>(value));
    case RtComponentPropertyType::String:
      return "\"" + escapeJsonString(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::AssetPath:
      return "\"" + escapeJsonString(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::Hash: {
      // Hash is stored as uint64_t but output as a hex string token in OGN
      std::ostringstream ss;
      ss << "\"0x" << std::hex << std::get<uint64_t>(value) << "\"";
      return ss.str();
    }
    case RtComponentPropertyType::Prim:
      // Target relationships don't typically have default values in OGN
      return "null";
    case RtComponentPropertyType::Any:
    case RtComponentPropertyType::NumberOrVector:
      // Flexible types should not have default values
      return "null";
  }
  return "null";
}

// Helper function to write a property to the OGN schema
void writePropertyToOGN(std::ofstream& outputFile, const RtComponentSpec& spec, const ComponentSpecVariantMap& variants, const RtComponentPropertySpec& prop, bool isLast) {
  // Check if this is a flexible type property (needed throughout the function)
  // If type != declaredType, it means a flexible type was resolved to a concrete type
  bool isFlexibleType = (prop.type != prop.declaredType);
  
  outputFile << "      \"" << escapeJsonString(prop.name) << "\": {" << std::endl;
  if (!prop.enumValues.empty()) {
    // For enum documentation, we need to combine everything into the property docstring.
    outputFile << "        \"description\": [\"" << escapeJsonString(prop.docString) << "\\n" << "Allowed values:\\n";
    std::string defaultEnumValueString = "";
    for (const auto& enumValue : prop.enumValues) { 
      assert(enumValue.first != "None" && "None enum values will cause python errors in the toolkit, and should be renamed.");
      outputFile << " - " << enumValue.first << ": " << enumValue.second.docString << "\\n ";
      if (enumValue.second.value == prop.defaultValue) {
        defaultEnumValueString = enumValue.first;
      }
    }
    outputFile << "\"]," << std::endl;
    outputFile << "        \"type\": \"token\"," << std::endl;

    outputFile << "        \"default\": \"" << defaultEnumValueString << "\"," << std::endl;
  } else {
    outputFile << "        \"description\": [\"" << escapeJsonString(prop.docString) << "\"]," << std::endl;
    
    // Check if this is a flexible type property
    // If type != declaredType, it means a flexible type was resolved to a concrete type
    if (isFlexibleType) {
      // Output union type for flexible properties
      std::string typeUnion = getFlexiblePropertyTypeUnion(variants, prop.name);
      if (!typeUnion.empty()) {
        outputFile << "        \"type\": " << typeUnion << "," << std::endl;
      } else {
        // Fallback to single type if enumeration fails
        outputFile << "        \"type\": \"" << propertyTypeToOgnType(prop.type) << "\"," << std::endl;
      }
    } else {
      // Regular property with concrete type
      outputFile << "        \"type\": \"" << propertyTypeToOgnType(prop.type) << "\"," << std::endl;
    }

    // Target relationships and flexible types don't have default values in OGN
    if (prop.type != RtComponentPropertyType::Prim && !isFlexibleType) {
      outputFile << "        \"default\": " << getDefaultValueAsJson(prop.defaultValue, prop.type) << "," << std::endl;
    }
  }


  // Add metadata if available
  bool hasMetadata = false;
  // For flexible types, don't add color metadata (since they can resolve to any type)
  bool isColorType = !isFlexibleType && prop.treatAsColor && 
                     (prop.type == RtComponentPropertyType::Float3 || prop.type == RtComponentPropertyType::Float4);
  
  if (!prop.enumValues.empty() || isColorType || prop.isSettableOutput || !prop.allowedPrimTypes.empty()) {
    
    outputFile << "        \"metadata\": {" << std::endl;
    
    // Add uiType for color properties
    if (isColorType) {
      outputFile << "          \"uiType\": \"color\"";
      hasMetadata = true;
    }
    
    // Add allowedTokens for enum values
    if (!prop.enumValues.empty()) {
      if (hasMetadata) {
        outputFile << "," << std::endl;
      }
      outputFile << "          \"allowedTokens\": [";
      bool first = true;
      for (const auto& enumValue : prop.enumValues) {
        if (!first) {
          outputFile << ", ";
        }
        outputFile << "\"" << escapeJsonString(enumValue.first) << "\"";
        first = false;
      }
      outputFile << "]";
      hasMetadata = true;
    }
    
    // Add outputOnly for settable output properties
    if (prop.isSettableOutput) {
      if (hasMetadata) {
        outputFile << "," << std::endl;
      }
      outputFile << "          \"outputOnly\": \"1\"";
      hasMetadata = true;
    }
    
    // Add filterPrimTypes for Prim properties with allowed prim types
    if (!prop.allowedPrimTypes.empty()) {
      if (hasMetadata) {
        outputFile << "," << std::endl;
      }
      outputFile << "          \"filterPrimTypes\": [";
      bool first = true;
      for (const auto& primType : prop.allowedPrimTypes) {
        if (!first) {
          outputFile << ", ";
        }
        std::string primTypeName = primTypeToString(primType);
        outputFile << "\"" << escapeJsonString(primTypeName) << "\"";
        first = false;
      }
      outputFile << "]";
      hasMetadata = true;
    }
    
    outputFile << std::endl;
    outputFile << "        }," << std::endl;
  }

  // Optional properties
  if (prop.optional) {
    outputFile << "        \"optional\": true," << std::endl;
  }

  outputFile << "        \"uiName\": \"" << escapeJsonString(prop.uiName) << "\"" << std::endl;


  outputFile << "      }";
  if (!isLast) {
    outputFile << ",";
  }
  outputFile << std::endl;
}


}  // namespace

bool writeOGNSchema(const RtComponentSpec* spec, RtComponentType componentType, const ComponentSpecVariantMap& variants, const char* outputFolderPath) {
  // Create the directory structure if it doesn't exist
  std::filesystem::path parentDir(outputFolderPath);
  std::filesystem::path filePath = parentDir / (spec->getClassName() + ".ogn");

  // Open the output file for writing
  std::optional<std::ofstream> outputFileHolder = util::createDirectoriesAndOpenFile(filePath);
  if (!outputFileHolder) {
    return false;
  }

  std::ofstream& outputFile = *outputFileHolder;
  
  // Write the OGN schema header comment
  outputFile << "{" << std::endl;
  
  // Start the node definition
  outputFile << "  \"" << escapeJsonString(spec->name) << "\": {" << std::endl;

  // Write the node properties
  outputFile << "    \"description\": [\"" << escapeJsonString(spec->docString) << "\"]," << std::endl;
  outputFile << "    \"version\": " << spec->version << "," << std::endl;
  outputFile << "    \"uiName\": \"" << escapeJsonString(spec->uiName) << "\"," << std::endl;
  outputFile << "    \"language\": \"python\"," << std::endl;
  outputFile << "    \"categoryDefinitions\": \"config/CategoryDefinition.json\"," << std::endl;
  outputFile << "    \"categories\": \"" << escapeJsonString(spec->categories) << "\"," << std::endl;

  // Separate properties by IO type
  std::vector<const RtComponentPropertySpec*> inputs, states, outputs;
  for (const auto& prop : spec->properties) {
    switch (prop.ioType) {
      case RtComponentPropertyIOType::Input:
        inputs.push_back(&prop);
        break;
      case RtComponentPropertyIOType::State:
        states.push_back(&prop);
        break;
      case RtComponentPropertyIOType::Output:
        outputs.push_back(&prop);
        break;
    }
  }
  
  // Write inputs section
  if (!inputs.empty()) {
    outputFile << "    \"inputs\": {" << std::endl;
    for (size_t i = 0; i < inputs.size(); ++i) {
      writePropertyToOGN(outputFile, *spec, variants, *inputs[i], i == inputs.size() - 1);
    }
    outputFile << "    }";
    // Note: if we ever add states back in, we need to restore the !states.empty() check.
    if (/*!states.empty() ||*/ !outputs.empty()) {
      outputFile << ",";
    }
    outputFile << std::endl;
  }
  
  // Disabled the state section - this shows up as editable properties in the Toolkit UI, and filtering them there is non-trivial.
  // Write state section
  // if (!states.empty()) {
  //   outputFile << "    \"state\": {" << std::endl;
  //   for (size_t i = 0; i < states.size(); ++i) {
  //     writePropertyToOGN(outputFile, *spec, variants, *states[i], i == states.size() - 1);
  //   }
  //   outputFile << "    }";
  //   if (!outputs.empty()) {
  //     outputFile << ",";
  //   }
  //   outputFile << std::endl;
  // }
  // If this section is restored, also restore the !states.empty() check in the inputs section above.
  
  // Write outputs section
  if (!outputs.empty()) {
    outputFile << "    \"outputs\": {" << std::endl;
    for (size_t i = 0; i < outputs.size(); ++i) {
      writePropertyToOGN(outputFile, *spec, variants, *outputs[i], i == outputs.size() - 1);
    }
    outputFile << "    }" << std::endl;
  }
  
  // Close the node definition and root object
  outputFile << "  }" << std::endl;
  outputFile << "}" << std::endl;
  
  // Close the file
  outputFile.close();
  
  Logger::info(str::format("Component Schema Write: Successfully wrote OGN schema to ", filePath));
  return true;
}

bool writePythonStub(const RtComponentSpec* spec, RtComponentType componentType, const ComponentSpecVariantMap& variants, const char* outputFolderPath) {
  // Create the directory structure if it doesn't exist
  std::filesystem::path parentDir(outputFolderPath);
  std::filesystem::path filePath = parentDir / (spec->getClassName() + ".py");

  // Open the output file for writing
  std::optional<std::ofstream> outputFileHolder = util::createDirectoriesAndOpenFile(filePath);
  if (!outputFileHolder) {
    return false;
  }

  std::ofstream& outputFile = *outputFileHolder;

  std::string databaseClassName = spec->getClassName() + "Database";

  bool hasFlexibleTypes = !variants.empty() && !variants[0]->resolvedTypes.empty();
  
  outputFile << "# GENERATED FILE - DO NOT EDIT" << std::endl;
  outputFile << "# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime." << std::endl;
  outputFile << "from __future__ import annotations" << std::endl;
  outputFile << std::endl;
  outputFile << "from typing import TYPE_CHECKING" << std::endl;
  outputFile << std::endl;
  if (hasFlexibleTypes) {
    outputFile << "import omni.graph.core as og" << std::endl;
    outputFile << std::endl;
  }
  outputFile << "if TYPE_CHECKING:" << std::endl;
  outputFile << "    from lightspeed.trex.logic.ogn.ogn." << databaseClassName << " import " << databaseClassName << std::endl;
  outputFile << std::endl;
  outputFile << std::endl;
  outputFile << "class "<< escapeJsonString(spec->getClassName()) << ":" << std::endl;
  outputFile << "    @staticmethod" << std::endl;
  outputFile << "    def compute(_db: " << databaseClassName << "):" << std::endl;
  outputFile << "        return True" << std::endl;
  outputFile << std::endl;
  
  // If this component has flexible types, generate on_connection_type_resolve
  if (hasFlexibleTypes) {
    // Collect all combinations from the registered variants
    std::vector<std::unordered_map<std::string, RtComponentPropertyType>> combinations;
    for (const auto* variant : variants) {
      if (!variant->resolvedTypes.empty()) {
        combinations.push_back(variant->resolvedTypes);
      }
    }
    
    // Get property names in a consistent order (alphabetical)
    std::vector<std::string> propNames;
    if (!combinations.empty()) {
      for (const auto& [propName, unused_propertyType] : combinations[0]) {
        propNames.push_back(propName);
      }
      std::sort(propNames.begin(), propNames.end());
      
      // Sort combinations based on the enum order of their types
      std::sort(combinations.begin(), combinations.end(),
        [&propNames](const std::unordered_map<std::string, RtComponentPropertyType>& a,
                     const std::unordered_map<std::string, RtComponentPropertyType>& b) {
          for (const auto& propName : propNames) {
            auto aType = static_cast<int>(a.at(propName));
            auto bType = static_cast<int>(b.at(propName));
            if (aType != bType) {
              return aType < bType;
            }
          }
          return false;
        });
      
      // Deduplicate combinations based on their OGN type signatures
      // (String, AssetPath, and Hash all map to "token", so we need to remove duplicates)
      std::vector<std::unordered_map<std::string, RtComponentPropertyType>> uniqueCombinations;
      std::set<std::string> seenSignatures;
      
      for (const auto& combo : combinations) {
        // Build a signature string from the OGN types of all properties
        std::string signature;
        for (const auto& propName : propNames) {
          if (!signature.empty()) {
            signature += "|";
          }
          signature += propName + ":" + propertyTypeToOgnType(combo.at(propName));
        }
        
        // Only add this combination if we haven't seen this signature before
        if (seenSignatures.insert(signature).second) {
          uniqueCombinations.push_back(combo);
        }
      }
      
      // Replace combinations with deduplicated list
      combinations = std::move(uniqueCombinations);
    }
    
    outputFile << "    @staticmethod" << std::endl;
    outputFile << "    def on_connection_type_resolve(node) -> None:" << std::endl;
    outputFile << "        \"\"\"Resolve flexible types based on connected attribute types.\"\"\"" << std::endl;
    outputFile << "        # Valid type combinations for this component:" << std::endl;
    
    // Generate a comment listing all valid combinations
    for (size_t i = 0; i < combinations.size(); ++i) {
      outputFile << "        # Combination " << (i + 1) << ": ";
      bool first = true;
      for (const auto& propName : propNames) {
        if (!first) {
          outputFile << ", ";
        }
        outputFile << propName << "=" << propertyTypeToOgnType(combinations[i].at(propName));
        first = false;
      }
      outputFile << std::endl;
    }
    
    // Separate input and output flexible properties
    std::vector<std::string> flexibleInputs, flexibleOutputs;
    for (const auto& prop : spec->properties) {
      if (prop.type != prop.declaredType) {  // It's a flexible type
        if (prop.ioType == RtComponentPropertyIOType::Input) {
          flexibleInputs.push_back(prop.name);
        } else if (prop.ioType == RtComponentPropertyIOType::Output) {
          flexibleOutputs.push_back(prop.name);
        }
      }
    }
    
    // Generate type checking logic
    if (!flexibleInputs.empty() && !flexibleOutputs.empty()) {
      outputFile << std::endl;
      // Get attributes for all flexible properties
      outputFile << "        # Get attributes" << std::endl;
      for (const auto& propName : flexibleInputs) {
        outputFile << "        input_" << propName << " = node.get_attribute(\"inputs:" << propName << "\")" << std::endl;
      }
      for (const auto& propName : flexibleOutputs) {
        outputFile << "        output_" << propName << " = node.get_attribute(\"outputs:" << propName << "\")" << std::endl;
      }
      outputFile << std::endl;
      
      // Get current types
      outputFile << "        # Get current types of connected attributes" << std::endl;
      for (const auto& propName : flexibleInputs) {
        outputFile << "        type_" << propName << " = input_" << propName << ".get_resolved_type()" << std::endl;
      }
      outputFile << std::endl;
      
      // Generate type checking logic for each combination
      outputFile << "        # Check all valid type combinations and resolve output types" << std::endl;
      for (size_t i = 0; i < combinations.size(); ++i) {
        const auto& combo = combinations[i];
        
        // Build condition to check if inputs match this combination
        if (i > 0) {
          outputFile << "        el";
        } else {
          outputFile << "        ";
        }
        outputFile << "if (";
        
        for (size_t j = 0; j < flexibleInputs.size(); ++j) {
          const auto& propName = flexibleInputs[j];
          const auto& propertyType = combo.at(propName);
          std::string ognType = propertyTypeToOgnType(propertyType);
          
          outputFile << "type_" << propName << " == og.Type(og.BaseDataType." << ognTypeToBaseDataType(ognType) << ")";
          if (j < flexibleInputs.size() - 1) {
            outputFile << " and " << std::endl << "            ";
          }
        }
        outputFile << "):" << std::endl;
        
        // Set output types for this combination
        for (const auto& propName : flexibleOutputs) {
          const auto& propertyType = combo.at(propName);
          std::string ognType = propertyTypeToOgnType(propertyType);
          outputFile << "            output_" << propName << ".set_resolved_type(og.Type(og.BaseDataType." << ognTypeToBaseDataType(ognType) << "))" << std::endl;
        }
      }
    }
  }

  // Close the file
  outputFile.close();
  
  Logger::info(str::format("Component Schema Write: Successfully wrote python stub to ", filePath));
  return true;
}

}  // namespace dxvk 