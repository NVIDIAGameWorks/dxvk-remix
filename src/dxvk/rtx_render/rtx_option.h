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
#pragma once

#include <algorithm>
#include <unordered_set>
#include <cassert>
#include <limits>

#include "../util/config/config.h"
#include "../util/xxHash/xxhash.h"
#include "../util/util_math.h"
#include "../util/util_env.h"
#include "rtx_utils.h"

namespace dxvk {
  // RtxOption refers to a serializable option, which can be of a basic type (i.e. int) or a class type (i.e. vector hash value)
  // On initialization, it retrieves a value from a Config object and add itself to a global list so that all options can be serialized 
  // into a file when requested.
  enum RtxOptionFlags
  {
    NoSave = 0x1,             // Don't serialize an rtx option, but it still gets a value from .conf files
    NoReset = 0x2,            // Don't reset an rtx option from UI
  };

  enum class OptionType {
    Bool,
    Int,
    Float,
    HashSet,
    HashVector,
    Vector2,
    Vector3,
    Vector2i,
    String,
  };

  union GenericValue {
    bool b;
    int i;
    float f;
    Vector2* v2;
    Vector3* v3;
    Vector2i* v2i;
    fast_unordered_set* hashSet;
    std::vector<XXH64_hash_t>* hashVector;
    std::string* string;
    int64_t value;
    void* pointer;
  };

  struct RtxOptionImpl {
    using RtxOptionMap = std::map<std::string, std::shared_ptr<RtxOptionImpl>>;
    enum class ValueType {
      Value = 0,
      DefaultValue = 1,
      Count = 2
    };

    const char* name;
    const char* category;
    const char* environment;
    const char* description; // Description string for the option that will get included in documentation
    OptionType type;
    GenericValue valueList[(int)ValueType::Count];
    uint32_t flags;

    RtxOptionImpl(const char* optionName, const char* optionCategory, const char* optionEnvironment, OptionType optionType, uint32_t optionFlags, const char* optionDescription) :
      name(optionName), 
      category(optionCategory), 
      environment(optionEnvironment), 
      type(optionType), 
      flags(optionFlags),
      description(optionDescription) { }

    std::string getFullName() const {
      return getFullName(category, name);
    }

    const char* getTypeString() const;
    std::string genericValueToString(ValueType valueType) const;

    void readOption(const Config& options, ValueType type);
    void writeOption(Config& options, bool changedOptionOnly);
    void resetOption();

    static std::string getFullName(const std::string& category, const std::string& name) {
      return category + "." + name;
    }
    static void setStartupConfig(const Config& options) { s_startupOptions = options; }
    static void setCustomConfig(const Config& options) { s_customOptions = options; }
    static void readOptions(const Config& options);
    static void writeOptions(Config& options, bool changedOptionsOnly);
    static void resetOptions();
    static bool writeMarkdownDocumentation(const char* outputMarkdownFilePath);

    // Returns a global container holding all serializable options
    static RtxOptionMap& getGlobalRtxOptionMap();

    // Config object holding start up settings
    static Config s_startupOptions;
    static Config s_customOptions;
  };

  template <typename T>
  class RtxOption {
  public:
    // Constructor for basic types like int, float
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    RtxOption(const char* category, const char* name, const char* environment, BasicType value, uint32_t flags = 0, const char* description = "") {
      if (allocateMemory(category, name, environment, flags, description)) {
        for (int i = 0; i < (int)RtxOptionImpl::ValueType::Count; i++) {
          pImpl->valueList[i].value = 0;
          *reinterpret_cast<BasicType*>(&pImpl->valueList[i].value) = value;
        }
      }
    }

    // Constructor for structs and classes
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    RtxOption(const char* category, const char* name, const char* environment, const ClassType& value, uint32_t flags = 0, const char* description = "") {
      if (allocateMemory(category, name, environment, flags, description)) {
        for (int i = 0; i < (int)RtxOptionImpl::ValueType::Count; i++) {
          pImpl->valueList[i].pointer = new ClassType(value);
        }
      }
    }

    RtxOption::~RtxOption() {
      for (int i = 0; i < (int)RtxOptionImpl::ValueType::Count; i++) {
        GenericValue& value = pImpl->valueList[i];

        switch (pImpl->type) {
        case OptionType::HashSet:
          delete value.hashSet;
          break;
        case OptionType::HashVector:
          delete value.hashVector;
          break;
        case OptionType::Vector2:
          delete value.v2;
          break;
        case OptionType::Vector3:
          delete value.v3;
          break;
        case OptionType::Vector2i:
          delete value.v2i;
          break;
        case OptionType::String:
          delete value.string;
          break;
        default:
          break;
        }
      }
    }

    T& getValue() const {
      return *getValuePtr<T>(RtxOptionImpl::ValueType::Value);
    }

    void setValue(const T& v) const {
      *getValuePtr<T>(RtxOptionImpl::ValueType::Value) = v;
    }

    T& getDefaultValue() const {
      return *getValuePtr<T>(RtxOptionImpl::ValueType::DefaultValue);
    }

    void setDefaultValue(const T& v) const {
      *getValuePtr<T>(RtxOptionImpl::ValueType::DefaultValue) = v;
    }

    const char* getDescription() const {
      return pImpl->description;
    }

    OptionType getOptionType() {
      if constexpr (std::is_same_v<T, bool>) return OptionType::Bool;
      if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
                    std::is_same_v<T, size_t> || std::is_same_v<T, char>) 
        return OptionType::Int;
      if constexpr (std::is_same_v<T, float>) return OptionType::Float;
      if constexpr (std::is_same_v<T, fast_unordered_set>) return OptionType::HashSet;
      if constexpr (std::is_same_v<T, std::vector<XXH64_hash_t>>) return OptionType::HashVector;
      if constexpr (std::is_same_v<T, Vector2>) return OptionType::Vector2;
      if constexpr (std::is_same_v<T, Vector3>) return OptionType::Vector3;
      if constexpr (std::is_same_v<T, Vector2i>) return OptionType::Vector2i;
      if constexpr (std::is_same_v<T, std::string>) return OptionType::String;
      if constexpr (std::is_enum_v<T>) return OptionType::Int;
      assert(!"RtxOption - unsupported type");
      return OptionType::Int;
    }

    static void setStartupConfig(const Config& options) { RtxOptionImpl::setStartupConfig(options); }
    static void setCustomConfig(const Config& options) { RtxOptionImpl::setCustomConfig(options); }
    static void readOptions(const Config& options) { RtxOptionImpl::readOptions(options); }
    static void writeOptions(Config& options, bool changedOptionsOnly) { RtxOptionImpl::writeOptions(options, changedOptionsOnly); }
    static void resetOptions() { RtxOptionImpl::resetOptions(); }

    // Update all RTX options after setStartupConfig() and setCustomConfig() have been called
    static void updateRtxOptions() {
      // WAR: DxvkInstance() and subsequently this is called twice making the doc being re-written 
      // with RtxOptions already updated from config files below
      static bool hasDocumentationBeenWritten = false;
     
      // Write out to the markdown file before the RtxOptions defaults are updated
      // with those from configs
      if (!hasDocumentationBeenWritten && env::getEnvVar("DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD") == "1") {
        RtxOptionImpl::writeMarkdownDocumentation("RtxOptions.md");
        hasDocumentationBeenWritten = true;
      }

      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
        rtxOption.readOption(RtxOptionImpl::s_startupOptions, RtxOptionImpl::ValueType::DefaultValue);
        rtxOption.readOption(RtxOptionImpl::s_customOptions, RtxOptionImpl::ValueType::Value);
      }
    }

  private:
    bool allocateMemory(const char* category, const char* name, const char* environment, uint32_t flags, const char* description) {
      const std::string fullName = RtxOptionImpl::getFullName(category, name);
      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      auto pOption = globalRtxOptions.find(fullName);
      if (pOption == globalRtxOptions.end()) {
        // Cannot find existing object, make a new one
        pImpl = std::make_shared<RtxOptionImpl>(name, category, environment, getOptionType(), flags, description);
        globalRtxOptions[fullName] = pImpl;
        return true;
      } else {
        // If the variable already exists, use the existing object
        pImpl = pOption->second;
        return false;
      }
    }

    // Get pointer to basic types
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    BasicType* getValuePtr(RtxOptionImpl::ValueType type) const {
      return reinterpret_cast<BasicType*>(&pImpl->valueList[(int)type]);
    }

    // Get pointer to structs and classes 
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    ClassType* getValuePtr(RtxOptionImpl::ValueType type) const {
      return reinterpret_cast<ClassType*>(pImpl->valueList[(int)type].pointer);
    }

    // All data should be inside this object in order to be accessed globally and locally
    std::shared_ptr<RtxOptionImpl> pImpl;
  };

// The RTX_OPTION* macros provide a convenient way to declare a serializable option
#define RTX_OPTION_FULL(category, type, name, value, environment, flags, description) \
  private: inline static RtxOption<type> m_##name = RtxOption<type>(category, #name, environment, type(value), static_cast<uint32_t>(flags), description); \
  private: static RtxOption<type>& name##Object() { return m_##name; } \
  public : static const type& name() { return m_##name.getValue(); } \
  private: static type& name##Ref() { return m_##name.getValue(); } \
  public : static const char* name##Description() { return m_##name.getDescription(); }

#define RW_RTX_OPTION_FULL(category, type, name, value, environment, flags, description) \
  private: inline static RtxOption<type> m_##name = RtxOption<type>(category, #name, environment, type(value), static_cast<uint32_t>(flags), description); \
  public : static RtxOption<type>& name##Object() { return m_##name; } \
  public : static const type& name() { return m_##name.getValue(); } \
  public : static type& name##Ref() { return m_##name.getValue(); } \
  public : static const char* name##Description() { return m_##name.getDescription(); }

#define RTX_OPTION_ENV(category, type, name, value, environment, description) RTX_OPTION_FULL(category, type, name, value, environment, 0, description)
#define RTX_OPTION_FLAG(category, type, name, value, flags, description) RTX_OPTION_FULL(category, type, name, value, "", static_cast<uint32_t>(flags), description)
#define RTX_OPTION_FLAG_ENV(category, type, name, value, flags, environment, description) RTX_OPTION_FULL(category, type, name, value, environment, static_cast<uint32_t>(flags), description)
#define RTX_OPTION(category, type, name, value, description) RTX_OPTION_FULL(category, type, name, value, "", 0, description)

#define RW_RTX_OPTION_ENV(category, type, name, value, environment, description) RW_RTX_OPTION_FULL(category, type, name, value, environment, 0, description)
#define RW_RTX_OPTION_FLAG(category, type, name, value, flags, description) RW_RTX_OPTION_FULL(category, type, name, value, "", static_cast<uint32_t>(flags), description)
#define RW_RTX_OPTION_FLAG_ENV(category, type, name, value, flags, environment, description) RW_RTX_OPTION_FULL(category, type, name, value, environment, static_cast<uint32_t>(flags), description)
#define RW_RTX_OPTION(category, type, name, value, description) RW_RTX_OPTION_FULL(category, type, name, value, "", 0, description)

#define RTX_OPTION_CLAMP(name, minValue, maxValue) name##Ref() = std::clamp(name(), minValue, maxValue);
#define RTX_OPTION_CLAMP_MAX(name, maxValue) name##Ref() = std::min(name(), maxValue);
#define RTX_OPTION_CLAMP_MIN(name, minValue) name##Ref() = std::max(name(), minValue);
}
