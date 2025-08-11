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
#include <filesystem>

namespace dxvk {
namespace {
// Helper function to convert RtComponentPropertyType to OGN type string
std::string propertyTypeToOgnType(RtComponentPropertyType type) {
  switch (type) {
    case RtComponentPropertyType::Bool: return "bool";
    case RtComponentPropertyType::Float: return "float";
    case RtComponentPropertyType::Float2: return "float[2]";
    case RtComponentPropertyType::Float3: return "float[3]";
    case RtComponentPropertyType::Color3: return "colorf[3]";
    case RtComponentPropertyType::Color4: return "colorf[4]";
    case RtComponentPropertyType::Int32: return "int";
    case RtComponentPropertyType::Uint32: return "uint";
    case RtComponentPropertyType::Uint64: return "uint64";
    case RtComponentPropertyType::Prim: return "target";  // USD Relationship to a prim
    default: return "float"; // Default fallback
  }
}

// Helper function to get default value as JSON string
std::string getDefaultValueAsJson(const RtComponentPropertyValue& value, RtComponentPropertyType type) {
  switch (type) {
    case RtComponentPropertyType::Bool:
      return std::get<uint8_t>(value) ? "true" : "false";
    case RtComponentPropertyType::Float:
      return std::to_string(std::get<float>(value));
    case RtComponentPropertyType::Float2: {
      const auto& vec = std::get<Vector2>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), "]");
    }
    case RtComponentPropertyType::Float3:
    case RtComponentPropertyType::Color3: {
      const auto& vec = std::get<Vector3>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), ", ",
                          std::to_string(vec.z), "]");
    }
    case RtComponentPropertyType::Color4: {
      const auto& vec = std::get<Vector4>(value);
      return str::format("[", std::to_string(vec.x), ", ",
                          std::to_string(vec.y), ", ",
                          std::to_string(vec.z), ", ",
                          std::to_string(vec.w), "]");
    }
    case RtComponentPropertyType::Int32:
      return std::to_string(std::get<int32_t>(value));
    case RtComponentPropertyType::Uint32:
      return std::to_string(std::get<uint32_t>(value));
    case RtComponentPropertyType::Uint64:
      return std::to_string(std::get<uint64_t>(value));
    case RtComponentPropertyType::Prim:
      // Target relationships don't typically have default values in OGN
      return "null";
  }
  return "null";
}

// Helper function to escape JSON strings
std::string escapeJsonString(const std::string& input) {
  std::string output;
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

// Helper function to write a property to the OGN schema
void writePropertyToOGN(std::ofstream& outputFile, const RtComponentPropertySpec& prop, bool isLast) {
  outputFile << "      \"" << escapeJsonString(prop.name) << "\": {" << std::endl;
  if (!prop.enumValues.empty()) {
    // For enum documentation, we need to combine everything into the property docstring.
    outputFile << "        \"description\": [\"" << escapeJsonString(prop.docString) << "\\n" << "Allowed values: ";
    for (const auto& enumValue : prop.enumValues) { 
      outputFile << " - " << enumValue.first << ": " << enumValue.second.docString << "\\n ";
    }
    outputFile << "\"]," << std::endl;
  } else {
    outputFile << "        \"description\": [\"" << escapeJsonString(prop.docString) << "\"]," << std::endl;
  }
  outputFile << "        \"uiName\": \"" << escapeJsonString(prop.uiName) << "\"," << std::endl;
  outputFile << "        \"type\": \"" << propertyTypeToOgnType(prop.type) << "\"";

  // Target relationships don't have default values in OGN
  if (prop.type != RtComponentPropertyType::Prim) {
    outputFile << "," << std::endl;
    outputFile << "        \"default\": " << getDefaultValueAsJson(prop.defaultValue, prop.type);
  }

  // Add metadata if available
  bool hasMetadata = false;
  if (!prop.enumValues.empty() || 
      prop.type == RtComponentPropertyType::Color3 ||
      prop.type == RtComponentPropertyType::Color4) {
    
    outputFile << "," << std::endl;
    outputFile << "        \"metadata\": {" << std::endl;
    
    // Add uiType for color properties
    if (prop.type == RtComponentPropertyType::Color3 || prop.type == RtComponentPropertyType::Color4) {
      outputFile << "          \"uiType\": \"color\"";
      hasMetadata = true;
    }
    
    // Add allowedTokens for enum values
    if (!prop.enumValues.empty()) {
      if (hasMetadata) outputFile << "," << std::endl;
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
    }
    
    outputFile << std::endl;
    outputFile << "        }";
  }

  // Optional properties
  if (prop.optional) {
    outputFile << "," << std::endl;
    outputFile << "        \"optional\": true";
  }

  outputFile << std::endl;
  outputFile << "      }";
  if (!isLast) {
    outputFile << ",";
  }
  outputFile << std::endl;
}


}  // namespace

bool writeOGNSchema(const RtComponentSpec* spec, const char* outputFolderPath) {
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
      writePropertyToOGN(outputFile, *inputs[i], i == inputs.size() - 1);
    }
    outputFile << "    }";
    if (!states.empty() || !outputs.empty()) {
      outputFile << ",";
    }
    outputFile << std::endl;
  }
  
  // Write state section
  if (!states.empty()) {
    outputFile << "    \"state\": {" << std::endl;
    for (size_t i = 0; i < states.size(); ++i) {
      writePropertyToOGN(outputFile, *states[i], i == states.size() - 1);
    }
    outputFile << "    }";
    if (!outputs.empty()) {
      outputFile << ",";
    }
    outputFile << std::endl;
  }
  
  // Write outputs section
  if (!outputs.empty()) {
    outputFile << "    \"outputs\": {" << std::endl;
    for (size_t i = 0; i < outputs.size(); ++i) {
      writePropertyToOGN(outputFile, *outputs[i], i == outputs.size() - 1);
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

bool writePythonStub(const RtComponentSpec* spec, const char* outputFolderPath) {
  // Create the directory structure if it doesn't exist
  std::filesystem::path parentDir(outputFolderPath);
  std::filesystem::path filePath = parentDir / (spec->getClassName() + ".py");

  // Open the output file for writing
  std::optional<std::ofstream> outputFileHolder = util::createDirectoriesAndOpenFile(filePath);
  if (!outputFileHolder) {
    return false;
  }

  std::ofstream& outputFile = *outputFileHolder;

  outputFile << "from lightspeed.trex.omni.graph.ogn.OgnTemplateNodePyDatabase import OgnTemplateNodePyDatabase" << std::endl;
  outputFile << "# GENERATED FILE - DO NOT EDIT" << std::endl;
  outputFile << "# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime." << std::endl;
  outputFile << "class "<< escapeJsonString(spec->getClassName()) << ":" << std::endl;
  outputFile << "    @staticmethod" << std::endl;
  outputFile << "    def compute(db: OgnTemplateNodePyDatabase):" << std::endl;
  outputFile << "        return True" << std::endl;

  // Close the file
  outputFile.close();
  
  Logger::info(str::format("Component Schema Write: Successfully wrote python stub to ", filePath));
  return true;
}

}  // namespace dxvk 