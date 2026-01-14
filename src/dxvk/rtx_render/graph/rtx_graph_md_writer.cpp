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

#include "rtx_graph_md_writer.h"
#include "../util/util_env.h"
#include "../util/util_filesys.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace dxvk {
namespace {

// Helper function to format float values nicely
std::string formatFloat(float value) {
  // Check for special float values
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0 ? "Infinity" : "-Infinity";
  }
  if (value == std::numeric_limits<float>::max()) {
    return "FLT_MAX";
  }
  if (value == std::numeric_limits<float>::lowest()) {
    return "FLT_MIN";
  }
  
  // Check if the value is effectively an integer
  if (std::floor(value) == value) {
    // For integer-like values, format with minimal decimal
    int intValue = static_cast<int>(value);
    return std::to_string(intValue) + ".0";
  }
  // For non-integer values, use to_string and remove trailing zeros
  std::string result = std::to_string(value);
  // Remove trailing zeros after the decimal point
  size_t dotPos = result.find('.');
  if (dotPos != std::string::npos) {
    size_t lastNonZero = result.find_last_not_of('0');
    if (lastNonZero != std::string::npos && lastNonZero > dotPos) {
      result.erase(lastNonZero + 1);
    }
  }
  return result;
}

// Helper function to escape markdown special characters
std::string escapeMarkdown(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '*': output += "\\*"; break;
      case '_': output += "\\_"; break;
      case '`': output += "\\`"; break;
      case '#': output += "\\#"; break;
      case '+': output += "\\+"; break;
      case '-': output += "\\-"; break;
      case '.': output += "\\."; break;
      case '!': output += "\\!"; break;
      case '[': output += "\\["; break;
      case ']': output += "\\]"; break;
      case '(': output += "\\("; break;
      case ')': output += "\\)"; break;
      case '\n': output += "<br/>"; break;  // Convert newlines to HTML line breaks for table compatibility
      case '\r': break;  // Skip carriage returns
      default: output += c; break;
    }
  }
  return output;
}

// Helper function to get value as readable string
std::string getValueAsString(const RtComponentPropertyValue& value, const RtComponentPropertySpec& prop) {
  // For enum properties, look up the enum name
  if (!prop.enumValues.empty()) {
    for (const auto& [enumName, enumEntry] : prop.enumValues) {
      if (value == enumEntry.value) {
        return enumName;
      }
    }
    // If not found, fall through to default formatting
  }
  
  switch (prop.type) {
    case RtComponentPropertyType::Bool:
      return std::get<uint32_t>(value) ? "true" : "false";
    case RtComponentPropertyType::Float:
      return formatFloat(std::get<float>(value));
    case RtComponentPropertyType::Float2: {
      const auto& vec = std::get<Vector2>(value);
      return str::format("[", formatFloat(vec.x), ", ",
                         formatFloat(vec.y), "]");
    }
    case RtComponentPropertyType::Float3: {
      const auto& vec = std::get<Vector3>(value);
      return str::format("[", formatFloat(vec.x), ", ",
                         formatFloat(vec.y), ", ",
                         formatFloat(vec.z), "]");
    }
    case RtComponentPropertyType::Float4: {
      const auto& vec = std::get<Vector4>(value);
      return str::format("[", formatFloat(vec.x), ", ",
                         formatFloat(vec.y), ", ",
                         formatFloat(vec.z), ", ",
                         formatFloat(vec.w), "]");
    }
    case RtComponentPropertyType::Enum:
      return std::to_string(std::get<uint32_t>(value));
    case RtComponentPropertyType::String:
      return "\"" + escapeMarkdown(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::AssetPath:
      return "\"" + escapeMarkdown(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::Hash: {
      // Format hash as hex string with 0x prefix
      std::ostringstream ss;
      ss << "0x" << std::hex << std::get<uint64_t>(value);
      return ss.str();
    }
    case RtComponentPropertyType::Prim:
      // Prim references don't use the default value field,
      // as it isn't really applicable.
      return "None";
    case RtComponentPropertyType::Any:
    case RtComponentPropertyType::NumberOrVector:
      // Flexible types should not have default values
      return "None";
  }
  return "None";
}

// Helper function to write a property table row
void writePropertyTableRow(std::ofstream& outputFile, const RtComponentPropertySpec& prop) {
  outputFile << "| " << escapeMarkdown(prop.name) << " | ";
  outputFile << escapeMarkdown(prop.uiName) << " | ";
  
  // Use declaredType to show the original type declaration (e.g., NumberOrVector for flexible types)
  outputFile << prop.declaredType << " | ";
  
  outputFile << prop.ioType << " | ";
  
  // For flexible types, show a simple default value
  std::string defaultValueStr = (prop.type != prop.declaredType) 
    ? "0" 
    : getValueAsString(prop.defaultValue, prop);
  outputFile << escapeMarkdown(defaultValueStr) << " | ";
  
  outputFile << (prop.optional ? "Yes" : "No") << " | " << std::endl;
}

// Helper function to write enum values if they exist
void writeEnumValues(std::ofstream& outputFile, const RtComponentPropertySpec& prop) {
  if (!prop.enumValues.empty()) {
    // Write the underlying type of the enum
    outputFile << "Underlying Type: `" << prop.type << "`" << std::endl << std::endl;
    
    outputFile << std::endl << "**Allowed Values:**" << std::endl << std::endl;
    
    // Convert to vector and sort by enum value
    std::vector<std::pair<std::string, RtComponentPropertySpec::EnumProperty>> sortedEnums(
      prop.enumValues.begin(), prop.enumValues.end());
    
    std::sort(sortedEnums.begin(), sortedEnums.end(),
      [](const auto& a, const auto& b) {
        return a.second.value < b.second.value;
      });
    
    // Output each enum value, marking the default
    for (const auto& [enumName, enumEntry] : sortedEnums) {
      outputFile << "- " << escapeMarkdown(enumName) << " (`" 
                 << escapeMarkdown(getValueAsString(enumEntry.value, prop)) << "`): "
                 << escapeMarkdown(enumEntry.docString);
      
      // Mark if this is the default value
      if (prop.defaultValue == enumEntry.value) {
        outputFile << " *(default)*";
      }
      
      outputFile << std::endl;
    }
  }
}

// Helper function to write min/max value constraints if they exist
void writeMinMaxValues(std::ofstream& outputFile, const RtComponentPropertySpec& prop) {
  // Check if minValue or maxValue are set (they default to false, which is a uint32_t with value 0)
  const bool hasMinValue = !(std::holds_alternative<uint32_t>(prop.minValue) && std::get<uint32_t>(prop.minValue) == 0);
  const bool hasMaxValue = !(std::holds_alternative<uint32_t>(prop.maxValue) && std::get<uint32_t>(prop.maxValue) == 0);
  
  if (hasMinValue || hasMaxValue) {
    outputFile << std::endl << "**Value Constraints:**" << std::endl << std::endl;
    
    if (hasMinValue) {
      outputFile << "- **Minimum Value:** " 
                 << escapeMarkdown(getValueAsString(prop.minValue, prop)) 
                 << std::endl;
    }
    if (hasMaxValue) {
      outputFile << "- **Maximum Value:** " 
                 << escapeMarkdown(getValueAsString(prop.maxValue, prop)) 
                 << std::endl;
    }
  }
}

// Helper function to write a property section (inputs, states, or outputs)
void writePropertySection(std::ofstream& outputFile, 
                         const std::vector<const RtComponentPropertySpec*>& properties,
                         const std::string& sectionName) {
  if (properties.empty()) {
    return;
  }
  
  outputFile << "## " << sectionName << " Properties" << std::endl << std::endl;
  outputFile << "| Property | Display Name | Type | IO Type | Default Value | Optional |" << std::endl;
  outputFile << "|----------|--------------|------|---------|---------------|----------|" << std::endl;
  
  for (const auto& prop : properties) {
    writePropertyTableRow(outputFile, *prop);
  }
  outputFile << std::endl;
  
  // Write detailed descriptions
  for (const auto& prop : properties) {
    outputFile << "### " << escapeMarkdown(prop->uiName) << std::endl << std::endl;
    outputFile << escapeMarkdown(prop->docString) << std::endl << std::endl;
    writeEnumValues(outputFile, *prop);
    writeMinMaxValues(outputFile, *prop);
    
    outputFile << std::endl;
  }
}

}  // namespace

bool writeComponentMarkdown(const RtComponentSpec* spec, RtComponentType componentType, const ComponentSpecVariantMap& variants, const char* outputFolderPath) {
  // Create the directory structure if it doesn't exist
  std::filesystem::path parentDir(outputFolderPath);
  std::filesystem::path filePath = parentDir / (spec->getClassName() + ".md");

  // Open the output file for writing
  std::optional<std::ofstream> outputFileHolder = util::createDirectoriesAndOpenFile(filePath);
  if (!outputFileHolder) {
    return false;
  }

  std::ofstream& outputFile = *outputFileHolder;
  
  // Write the component header
  outputFile << "# " << escapeMarkdown(spec->uiName) << std::endl << std::endl;
  
  // Write component description
  if (!spec->docString.empty()) {
    outputFile << escapeMarkdown(spec->docString) << std::endl << std::endl;
  }
  
  // Write component metadata
  outputFile << "## Component Information" << std::endl << std::endl;
  outputFile << "- **Name:** `" << spec->getClassName() << "`" << std::endl;
  outputFile << "- **UI Name:** " << escapeMarkdown(spec->uiName) << std::endl;
  outputFile << "- **Version:** " << spec->version << std::endl;
  if (!spec->categories.empty()) {
    outputFile << "- **Categories:** " << escapeMarkdown(spec->categories) << std::endl;
  }
  outputFile << std::endl;
  
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
  writePropertySection(outputFile, inputs, "Input");
  
  // Write state section
  writePropertySection(outputFile, states, "State");
  
  // Write outputs section
  writePropertySection(outputFile, outputs, "Output");
  
  // Write flexible type combinations if applicable
  // First, check if the component actually has any flexible type properties
  bool hasFlexibleTypes = false;
  for (const auto& prop : spec->properties) {
    if (prop.type != prop.declaredType) {
      hasFlexibleTypes = true;
      break;
    }
  }
  
  if (hasFlexibleTypes && !variants.empty()) {
    // Collect all combinations from the registered variants
    std::vector<std::unordered_map<std::string, RtComponentPropertyType>> combinations;
    for (const auto* variant : variants) {
      if (!variant->resolvedTypes.empty()) {
        combinations.push_back(variant->resolvedTypes);
      }
    }
    
    if (!combinations.empty()) {
      // Get property names in a consistent order (alphabetical for now)
      std::vector<std::string> propNames;
      for (const auto& [propName, unused_propertyType] : combinations[0]) {
        propNames.push_back(propName);
      }
      std::sort(propNames.begin(), propNames.end());
      
      // Sort combinations based on the enum order of their types
      // Compare lexicographically: first property type, then second, etc.
      std::sort(combinations.begin(), combinations.end(),
        [&propNames](const std::unordered_map<std::string, RtComponentPropertyType>& a,
                     const std::unordered_map<std::string, RtComponentPropertyType>& b) {
          // Compare each property in order
          for (const auto& propName : propNames) {
            auto aType = static_cast<int>(a.at(propName));
            auto bType = static_cast<int>(b.at(propName));
            if (aType != bType) {
              return aType < bType;
            }
          }
          return false; // All equal
        });
      
      outputFile << "## Valid Type Combinations" << std::endl << std::endl;
      outputFile << "This component supports flexible types. The following type combinations are valid:" << std::endl << std::endl;
      
      // Create a table of all valid combinations
      outputFile << "| # | ";
      bool firstCol = true;
      for (const auto& propName : propNames) {
        if (!firstCol) {
          outputFile << " | ";
        }
        outputFile << escapeMarkdown(propName);
        firstCol = false;
      }
      outputFile << " |" << std::endl;
      
      // Table separator
      outputFile << "|---";
      for (size_t i = 0; i < propNames.size(); ++i) {
        outputFile << "|---";
      }
      outputFile << "|" << std::endl;
      
      // Write each combination as a row
      for (size_t i = 0; i < combinations.size(); ++i) {
        outputFile << "| " << (i + 1) << " | ";
        bool firstProp = true;
        for (const auto& propName : propNames) {
          if (!firstProp) {
            outputFile << " | ";
          }
          outputFile << combinations[i].at(propName);
          firstProp = false;
        }
        outputFile << " |" << std::endl;
      }
      outputFile << std::endl;
    }
  }
  
  // Write usage notes
  outputFile << "## Usage Notes" << std::endl << std::endl;
  outputFile << "This component is part of the RTX Remix graph system. "
             << "It is intended for use in the Remix Toolkit and Runtime only."
             << std::endl << std::endl;
  
  // Write back to index link
  outputFile << "---" << std::endl;
  outputFile << "[← Back to Component Index](index.md)" << std::endl;
  
  // Close the file
  outputFile.close();
  
  Logger::info(str::format("Component Markdown Write: Successfully wrote component documentation to ",
                           filePath));
  return true;
}

bool writeMarkdownIndex(const std::vector<const RtComponentSpec*>& specs, const char* outputFolderPath) {
  // Create the directory structure if it doesn't exist
  std::filesystem::path parentDir(outputFolderPath);
  std::filesystem::path filePath = parentDir / "index.md";

  // Open the output file for writing
  std::optional<std::ofstream> outputFileHolder = util::createDirectoriesAndOpenFile(filePath);
  if (!outputFileHolder) {
    return false;
  }

  std::ofstream& outputFile = *outputFileHolder;
  
  // Write the index header
  outputFile << "# RTX Remix Component Documentation" << std::endl << std::endl;
  outputFile << "This documentation provides detailed information about all available components "
             << "in the RTX Remix graph system." << std::endl << std::endl;
  outputFile << "## Available Components" << std::endl << std::endl;
  
  // Group components by category
  std::unordered_map<std::string, std::vector<const RtComponentSpec*>>
      categorizedComponents;
  std::vector<const RtComponentSpec*> uncategorizedComponents;
  
  for (const auto& spec : specs) {
    if (!spec->categories.empty()) {
      categorizedComponents[std::string(spec->categories)].push_back(spec);
    } else {
      uncategorizedComponents.push_back(spec);
    }
  }
  
  // Sort components alphabetically by UI name within each category
  for (auto& [category, components] : categorizedComponents) {
    std::sort(components.begin(), components.end(),
      [](const RtComponentSpec* a, const RtComponentSpec* b) {
        return a->uiName < b->uiName;
      });
  }
  
  // Sort uncategorized components alphabetically by UI name
  std::sort(uncategorizedComponents.begin(), uncategorizedComponents.end(),
    [](const RtComponentSpec* a, const RtComponentSpec* b) {
      return a->uiName < b->uiName;
    });
  
  static constexpr size_t kMaxDescriptionLength = 100;
  // Write categorized components
  for (const auto& [category, components] : categorizedComponents) {
    outputFile << "### " << escapeMarkdown(category) << std::endl << std::endl;
    outputFile << "| Component | Description | Version |" << std::endl;
    outputFile << "|-----------|-------------|---------|" << std::endl;
    
    for (const auto& spec : components) {
      outputFile << "| [" << escapeMarkdown(spec->uiName) << "](" << spec->getClassName() << ".md) | ";
      std::string description = spec->docString.empty() ? "No description available" :
                                std::string(spec->docString.substr(0, kMaxDescriptionLength)) +
                                (spec->docString.length() > kMaxDescriptionLength ? "..." : "");
      outputFile << escapeMarkdown(description) << " | ";
      outputFile << spec->version << " |" << std::endl;
    }
    outputFile << std::endl;
  }
  
  // Write uncategorized components
  if (!uncategorizedComponents.empty()) {
    outputFile << "### Uncategorized Components" << std::endl << std::endl;
    outputFile << "| Component | Description | Version |" << std::endl;
    outputFile << "|-----------|-------------|---------|" << std::endl;
    
    for (const auto& spec : uncategorizedComponents) {
      outputFile << "| [" << escapeMarkdown(spec->uiName) << "](" << spec->getClassName() << ".md) | ";
      std::string description = spec->docString.empty() ? "No description available" :
                                std::string(spec->docString.substr(0, kMaxDescriptionLength)) +
                                (spec->docString.length() > kMaxDescriptionLength ? "..." : "");
      outputFile << escapeMarkdown(description) << " | ";
      outputFile << spec->version << " |" << std::endl;
    }
    outputFile << std::endl;
  }
  
  // Write statistics
  outputFile << "## Statistics" << std::endl << std::endl;
  outputFile << "- **Total Components:** " << specs.size() << std::endl;
  outputFile << "- **Categorized Components:** " << (specs.size() - uncategorizedComponents.size()) << std::endl;
  outputFile << "- **Categories:** " << categorizedComponents.size() << std::endl;
  outputFile << std::endl;
  
  // Write footer
  outputFile << "---" << std::endl;
  outputFile << "*Generated automatically from component specifications*" << std::endl;
  
  // Close the file
  outputFile.close();
  
  Logger::info(str::format("Markdown Index Write: Successfully wrote index to ",
                           filePath));
  return true;
}

}  // namespace dxvk 