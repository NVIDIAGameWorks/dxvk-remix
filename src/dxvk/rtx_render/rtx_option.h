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
#include <mutex>

#include "../util/config/config.h"
#include "../util/xxHash/xxhash.h"
#include "../util/util_math.h"
#include "../util/util_env.h"
#include "../util/util_keybind.h"
#include "rtx_utils.h"

#ifndef RTX_OPTION_DEBUG_LOGGING
// Set this to true to log any time a dirty value is accessed.
#define RTX_OPTION_DEBUG_LOGGING false
#endif

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
    IntVector,
    Vector2,
    Vector3,
    Vector2i,
    String,
    VirtualKeys,
    Vector4
  };

  union GenericValue {
    bool b;
    int i;
    float f;
    Vector2* v2;
    Vector3* v3;
    Vector4* v4;
    Vector2i* v2i;
    fast_unordered_set* hashSet;
    std::vector<XXH64_hash_t>* hashVector;
    std::vector<int32_t>* intVector;
    VirtualKeys* virtualKeys;
    std::string* string;
    int64_t value;
    void* pointer;
  };

  struct RtxOptionImpl {
    using RtxOptionMap = std::map<XXH64_hash_t, std::shared_ptr<RtxOptionImpl>>;
    enum class ValueType {
      Value = 0,
      DefaultValue = 1,
      PendingValue = 2,
      Count = 3,
    };

    XXH64_hash_t hash;
    const char* name;
    const char* category;
    const char* environment = nullptr;
    const char* description; // Description string for the option that will get included in documentation
    OptionType type;
    GenericValue valueList[(int)ValueType::Count];
    std::optional<GenericValue> minValue;
    std::optional<GenericValue> maxValue;
    uint32_t flags = 0;
    std::function<void()> onChangeCallback;

    RtxOptionImpl(XXH64_hash_t hash, const char* optionName, const char* optionCategory, OptionType optionType, const char* optionDescription) :
      hash(hash),
      name(optionName), 
      category(optionCategory), 
      type(optionType), 
      description(optionDescription) { }
    
    ~RtxOptionImpl();

    std::string getFullName() const {
      return getFullName(category, name);
    }

    const char* getTypeString() const;
    std::string genericValueToString(ValueType valueType) const;
    std::string genericValueToString(const GenericValue& value) const;
    void copyValue(ValueType source, ValueType target);

    void readOption(const Config& options, ValueType type);
    void writeOption(Config& options, bool changedOptionOnly);

    bool isDefault() const;
    bool isEqual(ValueType a, ValueType b) const;

    void resetOption();

    void markDirty() {
      getDirtyRtxOptionMap()[hash] = this;
    }

    void invokeOnChangeCallback() const;

    // Returns true if the value was changed
    bool clampValue(ValueType type);

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

    // Returns a global container holding all dirty options
    static fast_unordered_cache<RtxOptionImpl*>& getDirtyRtxOptionMap();

    // Config object holding start up settings
    static Config s_startupOptions;
    static Config s_customOptions;

    // track if the configs have been loaded.
    inline static bool s_isInitialized = false;

    // Mutex to prevent race conditions when clearing dirty RtxOptions
    inline static std::mutex s_updateMutex;
  };

  template <typename T>
  struct RtxOptionArgs {
    const char* environment = nullptr;
    uint32_t flags = 0;

    // TODO these need to be written into the docs
    std::optional<T> minValue;
    std::optional<T> maxValue;

    typedef void (*RtxOptionOnChangeCallback)();
    RtxOptionOnChangeCallback onChangeCallback = nullptr;
  };

  template <typename T>
  class RtxOption {
  private:
    // Helper function to check if a type is clampable
    static constexpr bool isClampable() {
      return std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> ||
             std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
             std::is_same_v<T, size_t> || std::is_same_v<T, char> || std::is_same_v<T, float> || 
             std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> || std::is_same_v<T, Vector2i>;
    }

  public:
    // Factory function.  Should never be called directly.  Use RTX_OPTION_FULL instead.
    static RtxOption<T> privateMacroFactory(const char* category, const char* name, const T& value, const char* description = "", RtxOptionArgs<T> args = {}) {
      return RtxOption<T>(category, name, value, description, args);
    }

    const T& operator()() const {
      return getValue();
    }

    const T& get() const {
      return getValue();
    }

    // Sets the pending value of this option, which will be promoted to the current value at the end of the frame.
    void setDeferred(const T& v) {
      setValue(v);
    }

    // TODO[REMIX-4105]: This is a hack to quickly fix set-then-read in the same frame.
    // Remove this once the uses have been refactored.
    void setImmediately(const T& v) {
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      // Set the pending value to invoke the onChange callback and avoid reverting the value at the end of the frame.
      *getValuePtr<T>(RtxOptionImpl::ValueType::PendingValue) = v;
      // Also set the current value, so that the value is immediately available.
      *getValuePtr<T>(RtxOptionImpl::ValueType::Value) = v;
      // This function sets the pending and immediate values separately, so they both need to be clamped.
      pImpl->clampValue(RtxOptionImpl::ValueType::PendingValue);
      pImpl->clampValue(RtxOptionImpl::ValueType::Value);
      // Mark the option as dirty so that the onChange callback is invoked, even though the value already changed mid frame.
      pImpl->markDirty();
    }

    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    void addHash(const XXH64_hash_t& value) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      getValuePtr<fast_unordered_set>(RtxOptionImpl::ValueType::PendingValue)->insert(value);
      pImpl->markDirty();
    }

    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    void removeHash(const XXH64_hash_t& value) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      getValuePtr<fast_unordered_set>(RtxOptionImpl::ValueType::PendingValue)->erase(value);
      pImpl->markDirty();
    }

    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    bool containsHash(const XXH64_hash_t& value) const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      return getValuePtr<fast_unordered_set>(RtxOptionImpl::ValueType::Value)->count(value) > 0;
    }

    T& getDefaultValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      return *getValuePtr<T>(RtxOptionImpl::ValueType::DefaultValue);
    }

    void setDefaultValue(const T& v) const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      *getValuePtr<T>(RtxOptionImpl::ValueType::DefaultValue) = v;
      pImpl->clampValue(RtxOptionImpl::ValueType::DefaultValue);
    }

    void resetToDefault() {
      setValue(getDefaultValue());
    }

    std::string getName() const {
      return pImpl->getFullName();
    }
    const char* getDescription() const {
      return pImpl->description;
    }

    OptionType getOptionType() const {
      if constexpr (std::is_same_v<T, bool>) return OptionType::Bool;
      if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
                    std::is_same_v<T, size_t> || std::is_same_v<T, char>) 
        return OptionType::Int;
      if constexpr (std::is_same_v<T, float>) return OptionType::Float;
      if constexpr (std::is_same_v<T, fast_unordered_set>) return OptionType::HashSet;
      if constexpr (std::is_same_v<T, std::vector<XXH64_hash_t>>) return OptionType::HashVector;
      if constexpr (std::is_same_v<T, std::vector<int32_t>>) return OptionType::IntVector;
      if constexpr (std::is_same_v<T, Vector4>) return OptionType::Vector4;
      if constexpr (std::is_same_v<T, Vector2>) return OptionType::Vector2;
      if constexpr (std::is_same_v<T, Vector3>) return OptionType::Vector3;
      if constexpr (std::is_same_v<T, Vector2i>) return OptionType::Vector2i;
      if constexpr (std::is_same_v<T, std::string>) return OptionType::String;
      if constexpr (std::is_same_v<T, VirtualKeys>) return OptionType::VirtualKeys;
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
    static void initializeRtxOptions() {
      // This method is called every time a dxvk context is created, which may happen multiple times.
      // Need to ensure RtxOption isn't invoking change callbacks during the intitialization step.
      RtxOptionImpl::s_isInitialized = false;

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
    
    // This will apply all of the RtxOption::set() calls that have been made since the last time it was called.
    // This should be called at the very end of the frame in the dxvk-cs thread.
    // Before the first frame is rendered, it also needs to be called at least once during initialization.
    // It's currently called twice during init, due to multiple sections that set many Options then immediately use them.
    static void applyPendingValues() {
      std::unique_lock<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      
      auto& dirtyOptions = RtxOptionImpl::getDirtyRtxOptionMap();
      // Need a second array so that we can invoke onChange callbacks after updating values and clearing the dirty list.
      std::vector<RtxOptionImpl*> dirtyOptionsVector;
      dirtyOptionsVector.reserve(dirtyOptions.size());
      {
        for (auto& rtxOption : dirtyOptions) {
          rtxOption.second->copyValue(RtxOptionImpl::ValueType::PendingValue, RtxOptionImpl::ValueType::Value);
          dirtyOptionsVector.push_back(rtxOption.second);
        }
      }
      dirtyOptions.clear();
      lock.unlock();

      // Invoke onChange callbacks after promoting all the values, so that newly set values will be updated at the end of the next frame
      for (RtxOptionImpl* rtxOption : dirtyOptionsVector) {
        rtxOption->invokeOnChangeCallback();
      }
    }

    template<typename = std::enable_if_t<isClampable()>>
    void setMinValue(const T& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      setMinMaxValueHelper(v, pImpl->minValue);
      bool changed = pImpl->clampValue(RtxOptionImpl::ValueType::PendingValue);
      if (changed) {
        pImpl->markDirty();
      }
    }

    template<typename = std::enable_if_t<isClampable()>>
    std::optional<T> getMinValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      return getMinMaxValueHelper<T>(pImpl->minValue);
    }
    
    template<typename = std::enable_if_t<isClampable()>>
    void setMaxValue(const T& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      setMinMaxValueHelper(v, pImpl->maxValue);
      bool changed = pImpl->clampValue(RtxOptionImpl::ValueType::PendingValue);
      if (changed) {
        pImpl->markDirty();
      }
    }

    template<typename = std::enable_if_t<isClampable()>>
    std::optional<T> getMaxValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      return getMinMaxValueHelper<T>(pImpl->maxValue);
    }

  private:
    // Prevent `new RtxOption` from being used.  Use the RTX_OPTION macro to create RtxOptions.
    static void* operator new(size_t size) = delete;
    static void operator delete(void* ptr) = delete;

    // Prevent `new RtxOption[N]` from being used.  Use the RTX_OPTION macro to create RtxOptions.
    static void* operator new[](size_t size) = delete;
    static void operator delete[](void* ptr) = delete;

    // Delete copy constructor and assignment operator to prevent accidental copying of RtxOption objects
    RtxOption(const RtxOption&) = delete;
    RtxOption& operator=(const RtxOption&) = delete;

    // Do not call these constructors directly. Use RTX_OPTION macro to declare and initialize RtxOption objects.
    // Constructor for basic types like int, float
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    RtxOption(const char* category, const char* name, BasicType value, const char* description, RtxOptionArgs<T> args) {
      if (allocateMemory(category, name, description, args)) {
        for (int i = 0; i < (int)RtxOptionImpl::ValueType::Count; i++) {
          pImpl->valueList[i].value = 0;
          *reinterpret_cast<BasicType*>(&pImpl->valueList[i].value) = value;
        }
      }
    }

    // Do not call these constructors directly. Use RTX_OPTION macro to declare and initialize RtxOption objects.
    // Constructor for structs and classes
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    RtxOption(const char* category, const char* name, const ClassType& value, const char* description, RtxOptionArgs<T> args) {
      if (allocateMemory(category, name, description, args)) {
        for (int i = 0; i < (int)RtxOptionImpl::ValueType::Count; i++) {
          pImpl->valueList[i].pointer = new ClassType(value);
        }
      }
    }

    T& getValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
#if RTX_OPTION_DEBUG_LOGGING
      // Print out a warning whenever a dirty value is accessed.
      if (!pImpl->isEqual(RtxOptionImpl::ValueType::Value, RtxOptionImpl::ValueType::PendingValue)) {
        Logger::warn(str::format("RtxOption retrieved a dirty value: ", pImpl->getFullName().c_str(),
            " has value: ", pImpl->genericValueToString(RtxOptionImpl::ValueType::Value),
            " and pending value: ", pImpl->genericValueToString(RtxOptionImpl::ValueType::PendingValue)));
      }
#endif
      return *getValuePtr<T>(RtxOptionImpl::ValueType::Value);
    }

    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    void setValue(const BasicType& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
      BasicType* valuePtr = getValuePtr<BasicType>(RtxOptionImpl::ValueType::PendingValue);
      *valuePtr = v;
      pImpl->clampValue(RtxOptionImpl::ValueType::PendingValue);
      pImpl->markDirty();
    }

    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    void setValue(const ClassType& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
      ClassType* valuePtr = getValuePtr<ClassType>(RtxOptionImpl::ValueType::PendingValue);
      *valuePtr = v;
      pImpl->clampValue(RtxOptionImpl::ValueType::PendingValue);
      pImpl->markDirty();
    }

    bool allocateMemory(const char* category, const char* name, const char* description, RtxOptionArgs<T> args) {
      const std::string fullName = RtxOptionImpl::getFullName(category, name);
      const XXH64_hash_t optionHash = StringToXXH64(fullName, 0);
      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      auto pOption = globalRtxOptions.find(optionHash);
      if (pOption == globalRtxOptions.end()) {
        // Cannot find existing object, make a new one
        pImpl = std::make_shared<RtxOptionImpl>(optionHash, name, category, getOptionType(), description);
        pImpl->environment = args.environment;
        pImpl->flags = args.flags;
        // Need to wrap this so we can cast it to the correct type
        pImpl->onChangeCallback = args.onChangeCallback;
        if constexpr (isClampable()) {
          if (args.minValue.has_value()) {
            setMinValue(args.minValue.value());
          }
          if (args.maxValue.has_value()) {
            setMaxValue(args.maxValue.value());
          }
        } else if (args.minValue.has_value() || args.maxValue.has_value()) {
          // If this happens on an option that should be clampable, the type probably needs to be added to isClampable(), and supported in the clampValue() method.
          assert(false && "RtxOption - args.minValue and args.maxValue are not supported for types not included in isClampable().");
        }
        globalRtxOptions[optionHash] = pImpl;
        return true;
      } else {
        assert(false && str::format("RtxOption with the same name already exists: ", fullName).c_str());
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

    // Helper methods to reduce code duplication between numeric and vector types
    void setMinMaxValueHelper(const T& v, std::optional<GenericValue>& targetValue) {
      if constexpr (std::is_pod_v<T>) {
        // For POD types (int, float, etc.), store directly in the value field
        GenericValue gv;
        if constexpr (std::is_same_v<T, float>) {
          gv.f = v;
        } else {
          // For bool, int, and other POD types, use the value field
          gv.value = static_cast<int64_t>(v);
        }
        targetValue = std::optional<GenericValue>(gv);
      } else {
        // For non-POD types (vectors, etc.), store as pointer
        if (!targetValue.has_value()) {
          targetValue = std::optional<GenericValue>();
          // Note: This is a `new` with no matching `delete`. This is safe because the
          // RtxOptionImpl object is never destroyed, and follows the pattern used in the constructor.
          targetValue.value().pointer = new T();
        }
        *reinterpret_cast<T*>(targetValue.value().pointer) = v;
      }
    }

    std::optional<T> getMinMaxValueHelper(const std::optional<GenericValue>& sourceValue) const {
      if (!sourceValue.has_value()) {
        return std::nullopt;
      } 
      if constexpr (std::is_pod_v<T>) {
        // For POD types (int, float, etc.), retrieve from the appropriate union member
        const GenericValue& gv = sourceValue.value();
        if constexpr (std::is_same_v<T, float>) {
          return std::optional<T>(gv.f);
        } else {
          // For bool, int, and other POD types, use the value field
          return std::optional<T>(static_cast<T>(gv.value));
        }
      } else {
        // For non-POD types (vectors, etc.), retrieve from pointer
        return std::optional<T>(*reinterpret_cast<T*>(sourceValue.value().pointer));
      }
    }

    // All data should be inside this object in order to be accessed globally and locally
    std::shared_ptr<RtxOptionImpl> pImpl;
  };

  // TODO[REMIX-4105] delete this after refactoring forceRebuildOMMs and hasEnableDebugResolveModeChanged to use an onChange listener instead.
  // Checks if value has changed from prevValue and updates prevValue if it did.
  // This can be used to check if an RtxOption value changed. 
  // PrevValue variable should be declared as a local/member variable rather than a static one due to:
  //      Using static variables with local initialization burns a branch predictor slot and 
  //      does an extra memory load from an unrelated chunk of memory 
  //      for every static variable declared this way. This is pretty wasteful.
  template <typename T>
  bool hasValueChanged(const T& value, T& prevValue) {
    if (value == prevValue) {
      return false;
    } else {
      prevValue = value;
      return true;
    }
  }

  // So we can access this function from anywhere.
  extern "C" __declspec(dllexport) bool writeMarkdownDocumentation(const char* outputMarkdownFilePath);

// The RTX_OPTION* macros provide a convenient way to declare a serializable option
// Example usage, presuming "optionName" is the name of the option:
// optionName(); // Get the current value of the option
// optionName.set(value); // Set the value of the option
// ImGUI::Checkbox("Option Name", &optionName); // Draw the option in the UI
#define RTX_OPTION_FULL(category, type, name, value, environment, flags, description, ...) \
  public: inline static RtxOption<type> name = RtxOption<type>::privateMacroFactory(category, #name, type(value), description, [](){ \
    RtxOptionArgs<type> args; \
    __VA_ARGS__; \
    return args; \
  }()); \
  public: static RtxOption<type>& name##Object() { return name; } 

#define RTX_OPTION_ENV(category, type, name, value, environmentVar, description) RTX_OPTION_FULL(category, type, name, value, environment, 0, description, \
    args.environment = environmentVar )
#define RTX_OPTION_FLAG(category, type, name, value, flagsVar, description) RTX_OPTION_FULL(category, type, name, value, "", static_cast<uint32_t>(flags), description, \
    args.flags = flagsVar )
#define RTX_OPTION_FLAG_ENV(category, type, name, value, flagsVar, environmentVar, description) RTX_OPTION_FULL(category, type, name, value, environment, static_cast<uint32_t>(flags), description, \
    args.environment = environmentVar, \
    args.flags = flagsVar )
#define RTX_OPTION(category, type, name, value, description) RTX_OPTION_FULL(category, type, name, value, "", 0, description, {})
#define RTX_OPTION_ARGS(category, type, name, value, description, ...) RTX_OPTION_FULL(category, type, name, value, "", 0, description, __VA_ARGS__)

// NOTE: these are deprecated.  Use args.minValue and args.maxValue as part of RTX_OPTION_ARGS instead.
#define RTX_OPTION_CLAMP(name, minValue, maxValue) name##Object().setDeferred(std::clamp(name(), minValue, maxValue));
#define RTX_OPTION_CLAMP_MAX(name, maxValue) name##Object().setDeferred(std::min(name(), maxValue));
#define RTX_OPTION_CLAMP_MIN(name, minValue) name##Object().setDeferred(std::max(name(), minValue));
}
