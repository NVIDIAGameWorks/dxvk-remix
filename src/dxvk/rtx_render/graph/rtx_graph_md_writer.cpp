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
#include <filesystem>
#include <unordered_map>

namespace dxvk {
namespace {

// Helper function to escape markdown special characters
std::string escapeMarkdown(const std::string& input) {
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

// Helper function to get default value as readable string
std::string getDefaultValueAsString(const RtComponentPropertyValue& value, RtComponentPropertyType type) {
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
    case RtComponentPropertyType::String:
      return "\"" + escapeMarkdown(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::AssetPath:
      return "\"" + escapeMarkdown(std::get<std::string>(value)) + "\"";
    case RtComponentPropertyType::Prim:
      // Prim references don't use the default value field,
      // as it isn't really applicable.
      return "None";
  }
  return "None";
}

// Helper function to write a property table row
void writePropertyTableRow(std::ofstream& outputFile, const RtComponentPropertySpec& prop) {
  outputFile << "| " << escapeMarkdown(prop.name) << " | ";
  outputFile << escapeMarkdown(prop.uiName) << " | ";
  outputFile << prop.type << " | ";
  outputFile << prop.ioType << " | ";
  outputFile << escapeMarkdown(getDefaultValueAsString(prop.defaultValue, prop.type)) << " | ";
  outputFile << (prop.optional ? "Yes" : "No") << " | " << std::endl;
}

// Helper function to write enum values if they exist
void writeEnumValues(std::ofstream& outputFile, const RtComponentPropertySpec& prop) {
  if (!prop.enumValues.empty()) {
    outputFile << std::endl << "**Allowed Values:**" << std::endl << std::endl;
    
    for (const auto& enumValue : prop.enumValues) {
      outputFile << "- " << escapeMarkdown(enumValue.first) << ": "
                 << escapeMarkdown(enumValue.second.docString) << std::endl;
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
    outputFile << std::endl;
  }
}

}  // namespace

bool writeComponentMarkdown(const RtComponentSpec* spec, const char* outputFolderPath) {
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
  outputFile << "**⚠️ Warning**: The Component System is not ready for use yet. "
             << "The information below is not finalized." << std::endl << std::endl;
  outputFile << "This documentation provides detailed information about all available components "
             << "in the RTX Remix graph system." << std::endl << std::endl;
  outputFile << "## Available Components" << std::endl << std::endl;
  
  // Group components by category
  std::unordered_map<std::string, std::vector<const RtComponentSpec*>>
      categorizedComponents;
  std::vector<const RtComponentSpec*> uncategorizedComponents;
  
  for (const auto& spec : specs) {
    if (!spec->categories.empty()) {
      categorizedComponents[spec->categories].push_back(spec);
    } else {
      uncategorizedComponents.push_back(spec);
    }
  }
  static constexpr size_t kMaxDescriptionLength = 100;
  // Write categorized components
  for (const auto& [category, components] : categorizedComponents) {
    outputFile << "### " << escapeMarkdown(category) << std::endl << std::endl;
    outputFile << "| Component | Description | Version |" << std::endl;
    outputFile << "|-----------|-------------|---------|" << std::endl;
    
    for (const auto& spec : components) {
      outputFile << "| [" << escapeMarkdown(spec->uiName) << "](" << spec->getClassName() << ".md) | ";
      outputFile << escapeMarkdown(spec->docString.empty() ? "No description available" :
                                   spec->docString.substr(0, kMaxDescriptionLength) +
                                   (spec->docString.length() > kMaxDescriptionLength ? "..." : ""))
                 << " | ";
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
      outputFile << escapeMarkdown(spec->docString.empty() ? "No description available" :
                                   spec->docString.substr(0, kMaxDescriptionLength) +
                                   (spec->docString.length() > kMaxDescriptionLength ? "..." : ""))
                 << " | ";
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