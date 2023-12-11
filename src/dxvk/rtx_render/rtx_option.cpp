/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_options.h"

namespace dxvk {
  Config RtxOptionImpl::s_startupOptions;
  Config RtxOptionImpl::s_customOptions;

  void fillHashTable(const std::vector<std::string>& rawInput, fast_unordered_set& hashTableOutput) {
    for (auto&& hashStr : rawInput) {
      const XXH64_hash_t h = std::stoull(hashStr, nullptr, 16);
      hashTableOutput.insert(h);
    }
  }

  void fillHashVector(const std::vector<std::string>& rawInput, std::vector<XXH64_hash_t>& hashVectorOutput) {
    for (auto&& hashStr : rawInput) {
      const XXH64_hash_t h = std::stoull(hashStr, nullptr, 16);
      hashVectorOutput.emplace_back(h);
    }
  }

  void fillIntVector(const std::vector<std::string>& rawInput, std::vector<int32_t>& intVectorOutput) {
    for (auto&& intStr : rawInput) {
      const int32_t i = std::stoi(intStr);
      intVectorOutput.emplace_back(i);
    }
  }

  std::string hashTableToString(const fast_unordered_set& hashTable) {
    std::stringstream ss;
    for (auto&& hash : hashTable) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << "0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << hash;
    }
    return ss.str();
  }

  std::string hashVectorToString(const std::vector<XXH64_hash_t>& hashVector) {
    std::stringstream ss;
    for (auto&& hash : hashVector) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << "0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << hash;
    }
    return ss.str();
  }
  
  std::string vectorToString(const std::vector<int32_t>& intVector) {
    std::stringstream ss;
    for (auto&& element : intVector) {
      if (ss.tellp() != std::streampos(0))
        ss << ", ";

      ss << element;
    }
    return ss.str();
  }

  const char* RtxOptionImpl::getTypeString() const {
    switch (type) {
    case OptionType::Bool: return "bool";
    case OptionType::Int: return "int";
    case OptionType::Float: return "float";
    case OptionType::HashSet: return "hash set"; 
    case OptionType::HashVector: return "hash vector";
    case OptionType::IntVector: return "int vector";
    case OptionType::Vector2: return "float2";
    case OptionType::Vector3: return "float3";
    case OptionType::Vector2i: return "int2";
    case OptionType::String: return "string";
    default:
      return "unknown type";
    }
  }

  std::string RtxOptionImpl::genericValueToString(ValueType valueType) const {
    const GenericValue& value = valueList[static_cast<int>(valueType)];

    switch (type) {
    case OptionType::Bool: return Config::generateOptionString(value.b);
    case OptionType::Int: return Config::generateOptionString(value.i);
    case OptionType::Float: return Config::generateOptionString(value.f);
    case OptionType::HashSet: return hashTableToString(*value.hashSet);
    case OptionType::HashVector: return hashVectorToString(*value.hashVector);
    case OptionType::IntVector: return vectorToString(*value.intVector);
    case OptionType::Vector2: return Config::generateOptionString(*value.v2);
    case OptionType::Vector3: return Config::generateOptionString(*value.v3);
    case OptionType::Vector2i: return Config::generateOptionString(*value.v2i);
    case OptionType::String: return *value.string;
    default:
      return "unknown type";
    }
  }

  void RtxOptionImpl::readOptions(const Config& options) {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.readOption(options, ValueType::Value);
    }
  }

  void RtxOptionImpl::writeOptions(Config& options, bool changedOptionsOnly) {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.writeOption(options, changedOptionsOnly);
    }
  }

  void RtxOptionImpl::resetOptions() {
    auto& globalRtxOptions = getGlobalRtxOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.resetOption();
    }
  }

  void RtxOptionImpl::readOption(const Config& options, RtxOptionImpl::ValueType valueType) {
    std::string fullName = getFullName();
    const char* env = environment == nullptr || strlen(environment) == 0 ? nullptr : environment;
    auto& value = valueList[(int) valueType];

    switch (type) {
    case OptionType::Bool:
      value.b = options.getOption<bool>(fullName.c_str(), value.b, env);
      break;
    case OptionType::Int:
      value.i = options.getOption<int>(fullName.c_str(), value.i, env);
      break;
    case OptionType::Float:
      value.f = options.getOption<float>(fullName.c_str(), value.f, env);
      break;
    case OptionType::HashSet:
      fillHashTable(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.hashSet);
      break;
    case OptionType::HashVector:
      fillHashVector(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.hashVector);
      break;
    case OptionType::IntVector:
      fillIntVector(options.getOption<std::vector<std::string>>(fullName.c_str()), *value.intVector);
      break;
    case OptionType::Vector2:
      *value.v2 = options.getOption<Vector2>(fullName.c_str(), *value.v2, env);
      break;
    case OptionType::Vector3:
      *value.v3 = options.getOption<Vector3>(fullName.c_str(), *value.v3, env);
      break;
    case OptionType::Vector2i:
      *value.v2i = options.getOption<Vector2i>(fullName.c_str(), *value.v2i, env);
      break;
    case OptionType::String:
      *value.string = options.getOption<std::string>(fullName.c_str(), *value.string, env);
      break;
    default:
      break;
    }
  }

  void RtxOptionImpl::writeOption(Config& options, bool changedOptionOnly) {
    if (flags & (uint32_t)RtxOptionFlags::NoSave)
      return;
    
    std::string fullName = getFullName();
    auto& value = valueList[(int) ValueType::Value];

    if (changedOptionOnly) {
      if (isDefault()) {
        return;
      }
    }

    switch (type) {
    case OptionType::Bool:
      options.setOption(fullName.c_str(), value.b);
      break;
    case OptionType::Int:
      options.setOption(fullName.c_str(), value.i);
      break;
    case OptionType::Float:
      options.setOption(fullName.c_str(), value.f);
      break;
    case OptionType::HashSet:
      options.setOption(fullName.c_str(), hashTableToString(*value.hashSet));
      break;
    case OptionType::HashVector:
      options.setOption(fullName.c_str(), hashVectorToString(*value.hashVector));
      break;
    case OptionType::IntVector:
      options.setOption(fullName.c_str(), vectorToString(*value.intVector));
      break;
    case OptionType::Vector2:
      options.setOption(fullName.c_str(), *value.v2);
      break;
    case OptionType::Vector3:
      options.setOption(fullName.c_str(), *value.v3);
      break;
    case OptionType::Vector2i:
      options.setOption(fullName.c_str(), *value.v2i);
      break;
    case OptionType::String:
      options.setOption(fullName.c_str(), *value.string);
      break;
    default:
      break;
    }
  }

  bool RtxOptionImpl::isDefault() const {
    auto& value = valueList[(int) ValueType::Value];
    auto& defaultValue = valueList[(int) ValueType::DefaultValue];

    bool isDefault = false;
    switch (type) {
    case OptionType::Bool:
      isDefault = (value.b == defaultValue.b);
      break;
    case OptionType::Int:
      isDefault = (value.i == defaultValue.i);
      break;
    case OptionType::Float:
      isDefault = (value.f == defaultValue.f);
      break;
    case OptionType::HashSet:
      isDefault = (*value.hashSet == *defaultValue.hashSet);
      break;
    case OptionType::HashVector:
      isDefault = (*value.hashVector == *defaultValue.hashVector);
      break;
    case OptionType::IntVector:
      isDefault = (*value.intVector == *defaultValue.intVector);
      break;
    case OptionType::Vector2:
      isDefault = (*value.v2 == *defaultValue.v2);
      break;
    case OptionType::Vector3:
      isDefault = (*value.v3 == *defaultValue.v3);
      break;
    case OptionType::Vector2i:
      isDefault = (*value.v2i == *defaultValue.v2i);
      break;
    case OptionType::String:
      isDefault = (*value.string == *defaultValue.string);
      break;
    default:
      break;
    }      
    return isDefault;
  }

  void RtxOptionImpl::resetOption() {
    if (flags & (uint32_t) RtxOptionFlags::NoReset)
      return;
    
    auto& value = valueList[(int) ValueType::Value];
    auto& defaultValue = valueList[(int) ValueType::DefaultValue];

    switch (type) {
    case OptionType::Bool:
      value.b = defaultValue.b;
      break;
    case OptionType::Int:
      value.i = defaultValue.i;
      break;
    case OptionType::Float:
      value.f = defaultValue.f;
      break;
    case OptionType::HashSet:
      *value.hashSet = *defaultValue.hashSet;
      break;
    case OptionType::HashVector:
      *value.hashVector = *defaultValue.hashVector;
      break;
    case OptionType::IntVector:
      *value.intVector = *defaultValue.intVector;
      break;
    case OptionType::Vector2:
      *value.v2 = *defaultValue.v2;
      break;
    case OptionType::Vector3:
      *value.v3 = *defaultValue.v3;
      break;
    case OptionType::Vector2i:
      *value.v2i = *defaultValue.v2i;
      break;
    case OptionType::String:
      *value.string = *defaultValue.string;
      break;
    default:
      break;
    }
  }

  bool RtxOptionImpl::writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    // Open the output file for writing
    std::ofstream outputFile(outputMarkdownFilePath);
    if (!outputFile.is_open()) {
      Logger::err(str::format("[RTX info] RTX Option: Failed to open output file ", outputMarkdownFilePath, " for writing"));
      return false;
    }

    // Write out the header for the file
    outputFile <<
      "# RTX Options\n";

    // Add description of Rtx Options
    outputFile <<
R"(RTX Options are configurable parameters for RTX pipeline components. They can be set via rtx.conf in a following format:

```
<RTX Option int scalar> = <Integer value>
<RTX Option float scalar> = <Floating point value>
<RTX Option int vector> = <Integer value>, <Integer value>, ...
<RTX Option float vector> = <Floating point value>, <Floating point value>, ...
<RTX Option boolean> = True/False
<RTX Option string> = <String value, no quotes>
<RTX Option hash set/vector> = <Hex string>, <Hex string>, ...
```

Practical examples of syntax:

```
rtx.someIntScalar = 38
rtx.someFloatScalar = 29.39
rtx.someIntVector = 1, -2, 3
rtx.someFloatVector = 1.0, -2.0, 3.0
rtx.someBoolean = True
# Note: Leading whitespace in a string is removed, allowing for nicer option formatting like this without messing up the string.
# Additionally, strings should not be surrounded with quotes as these will be treated as part of the string.
rtx.someString = This is a string
# Note: 0x prefix on hash hex values here is optional, similarly these values are case-insensitive. 16 hex characters = 64 bit hash.
rtx.someHashSet = 8DD6F568BD126398, EEF8EFD4B8A1B2A5, ...
```

RTX Options may be set in multiple places, specifically a hardcoded set in `src/util/config/config.cpp` which is assigned per-application based on process name, and the two user-configurable files `dxvk.conf` and `rtx.conf`. If not set the options will inherit their default values.
The full order of precedence for how each set of options overrides the previous is as follows:

1. Default option value (Implicit)
2. `dxvk.conf` ("User Config")
3. Per-application `config.cpp` configuration ("Built-in Config")
4. `rtx.conf` ("RTX User Config")
  1. `baseGameModPath/rtx.conf` (Mod-specific extension of "RTX User Config")

Additionally, upon saving options from the Remix UI options are written only to rtx.conf.

Tables below enumerate all the options and their defaults set by RTX Remix. Note that this information is auto-generated by the RTX Remix application. To re-generate this file, run Remix with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1` defined in the environment variables.

)";

    // Helper function to write out a table of RtxOptions for a given value type category
    auto writeOutRtxOptionTable = [&](bool processLongEntryTypes) {
      // Write out a header for a Markdown table
      outputFile << 
        "| RTX Option | Type | Default Value | Description |\n"
        "| :-- | :-: | :-: | :-- |\n"; // Text alignment per column

      // Write out all RTX Options
      auto& globalRtxOptions = getGlobalRtxOptionMap();
      for (const auto& rtxOptionMapEntry : globalRtxOptions) {
        const RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();

        // Allow processing of short or long value entry categories separately
        {
          bool isLongEntryType = false;

          switch (rtxOption.type) {
          case OptionType::HashSet:
          case OptionType::HashVector:
          case OptionType::IntVector:
          case OptionType::String:
            isLongEntryType = true;
            break;
          }

          if (isLongEntryType != processLongEntryTypes)
            continue;
        }

        std::string defaultValueString = rtxOption.genericValueToString(ValueType::DefaultValue);

        // Write the first portion of the result row to the the outputstream
        outputFile <<
          "|" << rtxOption.getFullName() <<
          "|" << rtxOption.getTypeString() <<
          "|" << defaultValueString <<
          "|";

        // Preprocess option description for Markdown
        // Note: This is needed as often in our description strings we use various characters which need to be handled
        // when translated to markdown to avoid breaking formatting.

        for (const char* descriptionIterator = rtxOption.description; ; ++descriptionIterator) {
          // Todo: Proper UTF-8 handling here if we ever need that. Not too hard to detect UTF-8 codepoints
          // (see: https://stackoverflow.com/a/40054802 for example), just would need to do this if option strings
          // actually start having anything beyond ASCII in them (otherwise this logic will break down and may
          // corrupt the displayed text).
          const auto currentCharacter = *descriptionIterator;

          if (currentCharacter == '\0') {
            break;
          }

          switch (currentCharacter) {
          // Note: Escape < and > as these act as HTML tags in various contexts.
          case '<': outputFile << "\\<"; break;
          case '>': outputFile << "\\>"; break;
          // Note: Convert newlines to HTML line breaks.
          case '\n': outputFile << "<br>"; break;
          // Note: Escape general Markdown syntax characters (this disallows usage of Markdown in description strings which
          // may be undesirable at some point, but for now our description strings are authored in a way that is shared
          // between the UI as well so they are usually not authored with expectation of Markdown syntax to function).
          case '\\': outputFile << "\\\\"; break;
          case '`': outputFile << "\\`"; break;
          case '*': outputFile << "\\*"; break;
          case '_': outputFile << "\\_"; break;
          case '{': outputFile << "\\{"; break;
          case '}': outputFile << "\\}"; break;
          case '[': outputFile << "\\["; break;
          case ']': outputFile << "\\]"; break;
          case '(': outputFile << "\\("; break;
          case ')': outputFile << "\\)"; break;
          case '#': outputFile << "\\#"; break;
          case '+': outputFile << "\\+"; break;
          case '-': outputFile << "\\-"; break;
          case '.': outputFile << "\\."; break;
          case '!': outputFile << "\\!"; break;
          // Note: Non-standard Markdown, but escaping it this way should work probably (if not then switch to using a HTML entity).
          case '|': outputFile << "\\|"; break;
          default:
            outputFile.put(currentCharacter);

            break;
          }
        }

        // Write the final portion of the result row to the the outputstream
        outputFile << "|\n";
      }
    };

    // Split short and long entry value types into two tables to improve readability for short value types
    // Long entry value types can be very long and drag out the width for the default value column
    outputFile << "## Simple Types\n";
    writeOutRtxOptionTable(false);

    outputFile << std::endl;

    outputFile << "## Complex Types\n";
    writeOutRtxOptionTable(true);

    outputFile.close();

    return true;
  }

  RtxOptionImpl::RtxOptionMap& RtxOptionImpl::getGlobalRtxOptionMap() {
    // Since other static RtxOptions may try to access the global container on their intialization, 
    // they have to access it via this helper method and the global container has to be defined 
    // as static locally to ensure it is initialized on first use
    static RtxOptionMap s_rtxOptions = RtxOptionMap();
    return s_rtxOptions;
  }
}