/*
* Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
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
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <functional>
#include <limits>
#include <mutex>
#include <atomic>
#include <optional>
#include <initializer_list>
#include <utility>

#include "../util/config/config.h"
#include "../util/xxHash/xxhash.h"
#include "../util/util_math.h"
#include "../util/util_env.h"
#include "../util/util_keybind.h"
#include "../util/util_hash_set_layer.h"
#include "rtx_utils.h"
#include "rtx_option_layer.h"

// Forward declaration - full definition in rtx_option_manager.h
class RtxOptionManager;

#ifndef RTX_OPTION_DEBUG_LOGGING
// Set this to true to log any time a dirty value is accessed.
#define RTX_OPTION_DEBUG_LOGGING false
#endif

namespace dxvk {
  class DxvkDevice;
  
  // RtxOption refers to a serializable option, which can be of a basic type (i.e. int) or a class type (i.e. vector hash value)
  // On initialization, it retrieves a value from a Config object and add itself to a global list so that all options can be serialized 
  // into a file when requested.

  union GenericValue {
    bool b;
    int i;
    float f;
    Vector2* v2;
    Vector3* v3;
    Vector4* v4;
    Vector2i* v2i;
    HashSetLayer* hashSet;
    std::vector<XXH64_hash_t>* hashVector;
    VirtualKeys* virtualKeys;
    std::string* string;
    int64_t value;
    void* pointer;
  };

  // Releases heap-allocated memory owned by a GenericValue based on its OptionType.
  // For pointer types (Vector2, HashSet, etc.), deletes the underlying object.
  // For value types (Bool, Int, Float), this is a no-op.
  void releaseGenericValue(GenericValue& value, OptionType type);

  // RtxOptionImpl is the base class for all RtxOption<T> instances.
  // It stores type-erased data and provides non-type-specific operations.
  // RtxOption<T> inherits from this class to add type-specific functionality.
  class RtxOptionImpl {
    friend class RtxOptionManager;
    template<typename T> friend class RtxOption;

  public:
    using RtxOptionMap = std::map<XXH64_hash_t, RtxOptionImpl*>;  // Raw pointers - everything lives forever

    // Static synchronization and initialization state
    // These use function-local statics to avoid static initialization order issues
    // (RtxOption<T> instances may be constructed before file-scope statics)
    static std::mutex& getUpdateMutex() {
      static std::mutex mutex;
      return mutex;
    }
    static bool isInitialized() { return s_isInitialized; }
    
    // Register an option in the global registry (called during construction)
    // Returns true if registration succeeded, false if an option with the same hash already exists
    static bool registerOption(XXH64_hash_t hash, RtxOptionImpl* option);
    
    // Get the global option map (for iteration by manager)
    // Uses function-local static to ensure map exists before any RtxOption<T> registration
    static RtxOptionMap& getGlobalOptionMap() {
      static RtxOptionMap map;
      return map;
    }
    
    // Look up an option by its full name (category.name)
    // Returns nullptr if the option doesn't exist
    static RtxOptionImpl* getOptionByFullName(const std::string& fullName) {
      const XXH64_hash_t optionHash = StringToXXH64(fullName, 0);
      auto& optionMap = getGlobalOptionMap();
      auto it = optionMap.find(optionHash);
      return (it != optionMap.end()) ? it->second : nullptr;
    }
    
    // Called by RtxOptions during initialization
    static void setInitialized(bool initialized) { s_isInitialized = initialized; }

  private:
    static bool s_isInitialized;

    // Represents a single option value along with its priority and blend strength.
    // Used in the option layer system to resolve final settings when multiple layers are active.
    // The actual priority is stored in the m_optionLayerValueQueue key for this value.
    // This struct owns the GenericValue and manages its memory via RAII.
    struct PrioritizedValue {
      PrioritizedValue() { }
      PrioritizedValue(const GenericValue& v, OptionType t, float b, float threshold)
        : value(v), optionType(t), blendStrength(b), blendThreshold(threshold) { }

      // Destructor releases heap-allocated memory owned by the GenericValue
      ~PrioritizedValue() {
        releaseGenericValue(value, optionType);
      }

      // Delete copy operations to prevent double-free
      PrioritizedValue(const PrioritizedValue&) = delete;
      PrioritizedValue& operator=(const PrioritizedValue&) = delete;

      // Move constructor: transfers ownership
      PrioritizedValue(PrioritizedValue&& other) noexcept
        : value(other.value), optionType(other.optionType),
          blendStrength(other.blendStrength), blendThreshold(other.blendThreshold) {
        other.value.pointer = nullptr;
        other.optionType = OptionType::Bool;  // Safe no-op type for destructor
      }

      // Move assignment: releases current value and transfers ownership
      PrioritizedValue& operator=(PrioritizedValue&& other) noexcept {
        if (this != &other) {
          releaseGenericValue(value, optionType);
          value = other.value;
          optionType = other.optionType;
          blendStrength = other.blendStrength;
          blendThreshold = other.blendThreshold;
          other.value.pointer = nullptr;
          other.optionType = OptionType::Bool;
        }
        return *this;
      }

      mutable GenericValue value; // The actual option value (owned by this struct)
      OptionType optionType = OptionType::Bool; // Type of value, used for proper cleanup
      mutable float blendStrength = 1.0f; // Blend weight, which allows smooth interpolation between overlapping option layers.
      mutable float blendThreshold = 0.5; // Blending strength threshold for this option layer. Only applicable to non-float variables. The option is enabled only when the blend strength exceeds this threshold.
    };

  public:
    virtual ~RtxOptionImpl();

    // Public API - accessible to all derived classes and external code
    std::string getFullName() const {
      return getFullName(m_category, m_name);
    }
    const char* getName() const { return m_name; }
    const char* getDescription() const { return m_description; }
    const char* getEnvironmentVariable() const { return m_environment; }
    OptionType getType() const { return m_type; }
    uint32_t getFlags() const { return m_flags; }
    
    // Gets the layer that this option will write to if a write function is called.
    // The result depends on the EditTarget for this thread, as well as the option's flags.
    const RtxOptionLayer* getTargetLayer(const RtxOptionLayer* explicitLayer = nullptr) const;
    
    bool isDefault() const;
    bool hasValueInLayer(const RtxOptionLayer* layer, std::optional<XXH64_hash_t> hash = std::nullopt) const;
    
    // Public helper methods - used by documentation and layer management
    const GenericValue* getGenericValue(const RtxOptionLayer* layer) const;
    std::string genericValueToString(const GenericValue& value) const;
    std::string getResolvedValueAsString() const;
    const GenericValue& getResolvedValue() const { return m_resolvedValue; }
    
    // Min/max values - public for documentation generation
    std::optional<GenericValue> minValue;
    std::optional<GenericValue> maxValue;
    
    // Layer value operations
    void readOptionLayer(const RtxOptionLayer& optionLayer);
    void readOption(const Config& options, const RtxOptionLayer* layer);
    bool loadFromEnvironmentVariable(const RtxOptionLayer* envLayer, std::string* outValue = nullptr);
    void disableLayerValue(const RtxOptionLayer* layer);
    void updateLayerBlendStrength(const RtxOptionLayer& optionLayer);
    void moveLayerValue(const RtxOptionLayer* sourceLayer, const RtxOptionLayer* destLayer);
    void clearFromStrongerLayers(const RtxOptionLayer* targetLayer = nullptr,
                                  std::optional<XXH64_hash_t> hash = std::nullopt);
    const RtxOptionLayer* getBlockingLayer(const RtxOptionLayer* targetLayer = nullptr,
                                           std::optional<XXH64_hash_t> hash = std::nullopt) const;
    const std::map<RtxOptionLayerKey, PrioritizedValue>& getLayerValueQueue() const { return m_optionLayerValueQueue; }
    
    // Iterate through layers that have values for this option.
    // Callback signature: bool callback(const RtxOptionLayer* layer, const GenericValue& value)
    //   - Returns true to continue iteration, false to stop early
    // Parameters:
    //   - hash: For hash set options, only iterates layers that have an opinion about this hash
    //   - includeInactiveLayers: If false (default), skips layers below blend threshold (for non-float types)
    //                            If true, includes all layers (useful for UI that wants to show inactive layers)
    // Layers are visited in priority order (highest first).
    void forEachLayerValue(std::function<bool(const RtxOptionLayer*, const GenericValue&)> callback,
                           std::optional<XXH64_hash_t> hash = std::nullopt,
                           bool includeInactiveLayers = false) const;
    
    // Change tracking
    void markDirty();
    bool isDirty() const;
    void invokeOnChangeCallback(DxvkDevice* device) const;

    // Migrate all layer values from this option to another option.
    // The lambda does all type conversion (read from src, write to dest).
    // bool isDestValueNew will be supplied to the transform indicating that the dest already has a value in its layer
    // Returns true if all data was migrated successfully
    bool migrateValuesTo(RtxOptionImpl* destOption, std::function<bool(const GenericValue& src, GenericValue& dest, bool isDestValueNew)> transform);

    // Static method for full name construction
    static std::string getFullName(const std::string& category, const std::string& name) {
      return category + "." + name;
    }


  protected:
    // Protected constructor - only derived RtxOption<T> can construct
    RtxOptionImpl(XXH64_hash_t hash, const char* optionName, const char* optionCategory, OptionType optionType, const char* optionDescription) :
      m_hash(hash),
      m_name(optionName), 
      m_category(optionCategory), 
      m_type(optionType), 
      m_description(optionDescription) { }

    // Protected data members - accessible to derived classes
    XXH64_hash_t m_hash;
    const char* m_name;
    const char* m_category;
    const char* m_environment = nullptr;
    const char* m_description; // Description string for the option that will get included in documentation
    OptionType m_type;
    GenericValue m_resolvedValue;
    uint32_t m_flags = 0;
    std::function<void(DxvkDevice* device)> m_onChangeCallback;
    
    std::map<RtxOptionLayerKey, PrioritizedValue> m_optionLayerValueQueue;

    // Returns pointer to value in layer, creating a new entry if not found
    std::pair<GenericValue*, bool> getOrCreateGenericValue(const RtxOptionLayer* layer);

    const char* getTypeString() const;

    // Returns true if the weaker layers resolve to the same value that the layer contains.
    bool isLayerValueRedundant(const RtxOptionLayer* layer) const;

  protected:
    // Protected methods - used by derived classes and friend classes
    void copyValue(const GenericValue& source, GenericValue& target);
    bool resolveValue(GenericValue& value, const RtxOptionLayer* excludeLayer = nullptr);
    void addWeightedValue(const GenericValue& source, const float weight, GenericValue& target);

    void readValue(const Config& options, const std::string& fullName, GenericValue& value);
    void writeOption(Config& options, const RtxOptionLayer* layer, bool changedOptionOnly);

    void insertOptionLayerValue(const GenericValue& value, const RtxOptionLayer* layer);

    bool isEqual(const GenericValue& aValue, const GenericValue& bValue) const;

    // Returns true if the value was changed
    bool clampValue(GenericValue& value);
  };

  template <typename T>
  struct RtxOptionArgs {
    const char* environment = nullptr;
    uint32_t flags = 0;

    // TODO these need to be written into the docs
    std::optional<T> minValue;
    std::optional<T> maxValue;

    // NOTE: onChange handlers can be invoked before the DxvkDevice is created,
    // so these callbacks need to null check the device.
    typedef void (*RtxOptionOnChangeCallback)(DxvkDevice* device);
    RtxOptionOnChangeCallback onChangeCallback = nullptr;
  };

  template <typename T>
  class RtxOption : public RtxOptionImpl {
  private:
    // Helper function to check if a type is clampable
    static constexpr bool isClampable() {
      return std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> ||
             std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
             std::is_same_v<T, size_t> || std::is_same_v<T, char> || std::is_same_v<T, float> || 
             std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> || std::is_same_v<T, Vector2i> ||
             std::is_enum_v<T>;
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
    
    // Get the resolved value without acquiring the mutex.
    // IMPORTANT: Only call this when the mutex is already held by the calling context.
    const T& getValueNoLock() const {
      assert(RtxOptionImpl::isInitialized() && "Trying to access an RtxOption before the config files have been loaded.");
      return *getResolvedValuePtr<T>();
    }


    // Sets a value on a specific layer.
    // If layer is nullptr, uses the current target layer from RtxOptionLayerTarget (defaults to user layer).
    // The value will be promoted to the resolved value at the end of the frame during resolution.
    void setDeferred(const T& v, const RtxOptionLayer* layer = nullptr) {
      setValue(v, layer);
    }

    // TODO[REMIX-4105]: Code that depends on this should be refactored to be able to use setDeferred() instead.
    // Sets a value on a layer and immediately resolves it to be available this frame.
    // If layer is nullptr, uses the current target layer from RtxOptionLayerTarget.
    void setImmediately(const T& v, const RtxOptionLayer* layer = nullptr) {
      assert(RtxOptionImpl::isInitialized() && "Trying to access an RtxOption before the config files have been loaded."); 
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      
      const RtxOptionLayer* targetLayer = getTargetLayer(layer);
      if (!targetLayer) {
        return;
      }
      
      // Set the value on the layer (create if not present)
      T* valuePtr = getOrCreateValuePtr<T>(targetLayer);
      if (!valuePtr) {
        return;
      }
      
      *valuePtr = v;
      
      // Notify layer that a value changed (unsaved changes will be recalculated lazily)
      targetLayer->onLayerValueChanged();
      
      // Immediately resolve all layers into the resolved value so it's available this frame
      resolveValue(m_resolvedValue);
      
      // Mark the option as dirty so that the onChange callback is invoked and cleanup happens
      markDirty();
    }

    // Add a hash to a hash set option in a specific layer.
    // This layer will contribute this hash to the resolved set.
    // If layer is nullptr, uses the current target layer from RtxOptionLayerTarget.
    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    void addHash(const XXH64_hash_t& value, const RtxOptionLayer* layer = nullptr) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      
      const RtxOptionLayer* targetLayer = getTargetLayer(layer);
      if (!targetLayer) {
        return;
      }
      
      HashSetLayer* hashSet = getOrCreateValuePtr<HashSetLayer>(targetLayer);
      if (!hashSet) {
        return;
      }
      
      hashSet->add(value);
      targetLayer->onLayerValueChanged();
      markDirty();
    }

    // Remove a hash from a hash set option in a specific layer.
    // This layer will exclude this hash from the resolved set, overriding lower priority layers.
    // If layer is nullptr, uses the current target layer from RtxOptionLayerTarget.
    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    void removeHash(const XXH64_hash_t& value, const RtxOptionLayer* layer = nullptr) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      
      const RtxOptionLayer* targetLayer = getTargetLayer(layer);
      if (!targetLayer) {
        return;
      }
      
      HashSetLayer* hashSet = getOrCreateValuePtr<HashSetLayer>(targetLayer);
      if (!hashSet) {
        return;
      }
      
      hashSet->remove(value);
      targetLayer->onLayerValueChanged();
      markDirty();
    }

    // Clear any opinion about a hash from a specific layer.
    // The hash will be neither added nor removed by this layer.
    // If layer is nullptr, uses the current target layer from RtxOptionLayerTarget.
    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    void clearHash(const XXH64_hash_t& value, const RtxOptionLayer* layer = nullptr) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      
      const RtxOptionLayer* targetLayer = getTargetLayer(layer);
      if (!targetLayer) {
        return;
      }
      
      HashSetLayer* hashSet = getOrCreateValuePtr<HashSetLayer>(targetLayer);
      if (!hashSet) {
        return;
      }
      
      hashSet->clear(value);
      targetLayer->onLayerValueChanged();
      
      // If the hash set is now empty, remove it from the layer
      if (hashSet->empty()) {
        disableLayerValue(targetLayer);
      }
      
      markDirty();
    }

    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    bool containsHash(const XXH64_hash_t& value) const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      return m_resolvedValue.hashSet->count(value) > 0;
    }

    T& getDefaultValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      const T* value = getValuePtr<T>(RtxOptionLayer::getDefaultLayer());
      // Default value is always set at construction, so this should never be null
      assert(value != nullptr);
      return const_cast<T&>(*value);
    }

    void resetToDefault() {
      setValue(getDefaultValue());
    }


    OptionType getOptionType() const {
      if constexpr (std::is_same_v<T, bool>) {
        return OptionType::Bool;
      }
      if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> ||
                    std::is_same_v<T, size_t> || std::is_same_v<T, char>) {
        return OptionType::Int;
      }
      if constexpr (std::is_same_v<T, float>) {
        return OptionType::Float;
      }
      if constexpr (std::is_same_v<T, fast_unordered_set>) {
        return OptionType::HashSet;
      }
      if constexpr (std::is_same_v<T, std::vector<XXH64_hash_t>>) {
        return OptionType::HashVector;
      }
      if constexpr (std::is_same_v<T, Vector4>) {
        return OptionType::Vector4;
      }
      if constexpr (std::is_same_v<T, Vector2>) {
        return OptionType::Vector2;
      }
      if constexpr (std::is_same_v<T, Vector3>) {
        return OptionType::Vector3;
      }
      if constexpr (std::is_same_v<T, Vector2i>) {
        return OptionType::Vector2i;
      }
      if constexpr (std::is_same_v<T, std::string>) {
        return OptionType::String;
      }
      if constexpr (std::is_same_v<T, VirtualKeys>) {
        return OptionType::VirtualKeys;
      }
      if constexpr (std::is_enum_v<T>) {
        return OptionType::Int;
      }
      assert(!"RtxOption - unsupported type");
      return OptionType::Int;
    }

    template<typename = std::enable_if_t<isClampable()>>
    void setMinValue(const T& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      
      bool changed = setMinMaxValueHelper(v, minValue);
      if (changed) {
        markDirty();
      }
    }

    template<typename = std::enable_if_t<isClampable()>>
    std::optional<T> getMinValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      return getMinMaxValueHelper<T>(minValue);
    }
    
    template<typename = std::enable_if_t<isClampable()>>
    void setMaxValue(const T& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      bool changed = setMinMaxValueHelper(v, maxValue);
      if (changed) {
        markDirty();
      }
    }

    template<typename = std::enable_if_t<isClampable()>>
    std::optional<T> getMaxValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      return getMinMaxValueHelper<T>(maxValue);
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
    RtxOption(const char* category, const char* name, BasicType value, const char* description, RtxOptionArgs<T> args) 
      : RtxOptionImpl(0, name, category, getOptionType(), description) {  // hash filled in by allocateMemory
      if (allocateMemory(category, name, description, args)) {
        m_resolvedValue.value = 0;
        *reinterpret_cast<BasicType*>(&m_resolvedValue.value) = value;
        // Push default value to option layer priority queue
        const RtxOptionLayer* defaultLayer = RtxOptionLayer::getDefaultLayer();
        if (defaultLayer) {
          insertOptionLayerValue(m_resolvedValue, defaultLayer);
        }

        initializeClamping(args);
      }
    }

    // Do not call these constructors directly. Use RTX_OPTION macro to declare and initialize RtxOption objects.
    // 
    // Special constructor for fast_unordered_set (hash set options).
    // This is needed because the public type is fast_unordered_set, but internally we store HashSetLayer
    // which supports both positive and negative entries for layer merging. The generic class constructor
    // would incorrectly store the value as a pointer rather than allocating a HashSetLayer.
    template <typename ClassType, std::enable_if_t<std::is_same_v<ClassType, fast_unordered_set>, bool> = true>
    RtxOption(const char* category, const char* name, const ClassType& value, const char* description, RtxOptionArgs<T> args) 
      : RtxOptionImpl(0, name, category, getOptionType(), description) {  // hash filled in by allocateMemory
      // All hash set options should have empty defaults - values come from config files at runtime
      assert(value.empty() && "Hash set RtxOptions should have empty {} defaults");
      
      if (allocateMemory(category, name, description, args)) {
        m_resolvedValue.hashSet = new HashSetLayer();
        
        const RtxOptionLayer* defaultLayer = RtxOptionLayer::getDefaultLayer();
        if (defaultLayer) {
          insertOptionLayerValue(m_resolvedValue, defaultLayer);
        }

        initializeClamping(args);
      }
    }

    // Do not call these constructors directly. Use RTX_OPTION macro to declare and initialize RtxOption objects.
    // Constructor for structs and classes (excluding fast_unordered_set which has its own specialization)
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType> && !std::is_same_v<ClassType, fast_unordered_set>, bool> = true>
    RtxOption(const char* category, const char* name, const ClassType& value, const char* description, RtxOptionArgs<T> args) 
      : RtxOptionImpl(0, name, category, getOptionType(), description) {  // hash filled in by allocateMemory
      if (allocateMemory(category, name, description, args)) {
        m_resolvedValue.pointer = new ClassType(value);
        // Push default value to option layer priority queue
        const RtxOptionLayer* defaultLayer = RtxOptionLayer::getDefaultLayer();
        if (defaultLayer) {
          insertOptionLayerValue(m_resolvedValue, defaultLayer);
        }

        initializeClamping(args);
      }
    }

    const T& getValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      assert(RtxOptionImpl::isInitialized() && "Trying to access an RtxOption before the config files have been loaded."); 
#if RTX_OPTION_DEBUG_LOGGING
      // Print out a warning whenever a dirty value is accessed.
      if (isDirty()) {
        GenericValueWrapper freshValue(type);
        const_cast<RtxOption*>(this)->resolveValue(freshValue.data);
        if (!isEqual(m_resolvedValue, freshValue.data)) {
          Logger::warn(str::format("RtxOption retrieved a dirty value: ", getFullName().c_str(),
              " has cached value: ", genericValueToString(m_resolvedValue),
              " but would resolve to: ", genericValueToString(freshValue.data)));
        }
      }
#endif
      return *getResolvedValuePtr<T>();
    }

    template <typename U, std::enable_if_t<std::is_same_v<U, T>, bool> = true>
    void setValue(const U& v, const RtxOptionLayer* layer = nullptr) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      assert(RtxOptionImpl::isInitialized() && "Trying to access an RtxOption before the config files have been loaded.");
      
      const RtxOptionLayer* targetLayer = getTargetLayer(layer);
      if (!targetLayer) {
        return;
      }
      
      T* valuePtr = getOrCreateValuePtr<T>(targetLayer);
      if (valuePtr) {
        *valuePtr = v;
        // Notify layer that a value changed (unsaved changes will be recalculated lazily)
        targetLayer->onLayerValueChanged();
      }

      markDirty();
    }

    bool allocateMemory(const char* category, const char* name, const char* description, RtxOptionArgs<T> args) {
      const std::string fullName = RtxOptionImpl::getFullName(category, name);
      const XXH64_hash_t optionHash = StringToXXH64(fullName, 0);
      
      // Set up option metadata before registration
      m_hash = optionHash;
      m_environment = args.environment;
      m_flags = args.flags;
      m_onChangeCallback = args.onChangeCallback;
      
      // Register in the global option map
      if (!RtxOptionImpl::registerOption(optionHash, this)) {
        assert(false && str::format("RtxOption with the same name already exists: ", fullName).c_str());
        return false;
      }
      return true;
    }

    // This needs to be done after the m_resolvedValue is initialized in the constructor.
    void initializeClamping(RtxOptionArgs<T> args) {
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
    }

    // Get pointer to resolved value for basic types (POD)
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    BasicType* getResolvedValuePtr() const {
      return reinterpret_cast<BasicType*>(&const_cast<RtxOption*>(this)->m_resolvedValue);
    }

    // Get pointer to resolved value for structs and classes (non-POD), except fast_unordered_set
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType> && !std::is_same_v<ClassType, fast_unordered_set>, bool> = true>
    ClassType* getResolvedValuePtr() const {
      return reinterpret_cast<ClassType*>(const_cast<RtxOption*>(this)->m_resolvedValue.pointer);
    }
    
    // Special case for fast_unordered_set: return pointer to the positives member of the internal HashSetLayer
    template <typename ClassType, std::enable_if_t<std::is_same_v<ClassType, fast_unordered_set>, bool> = true>
    ClassType* getResolvedValuePtr() const {
      return &const_cast<RtxOption*>(this)->m_resolvedValue.hashSet->m_positives;
    }

    // Get pointer to basic types for a specific layer (read-only - returns nullptr if not present)
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    const BasicType* getValuePtr(const RtxOptionLayer* layer) const {
      const GenericValue* genericValue = getGenericValue(layer);
      return genericValue ? reinterpret_cast<const BasicType*>(genericValue) : nullptr;
    }

    // Get pointer to structs and classes for a specific layer (read-only - returns nullptr if not present)
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    const ClassType* getValuePtr(const RtxOptionLayer* layer) const {
      const GenericValue* genericValue = getGenericValue(layer);
      return genericValue ? reinterpret_cast<const ClassType*>(genericValue->pointer) : nullptr;
    }

    // Get or create pointer to basic types for a specific layer (mutable - creates if not present)
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    BasicType* getOrCreateValuePtr(const RtxOptionLayer* layer) {
      auto [genericValue, _] = getOrCreateGenericValue(layer);
      return genericValue ? reinterpret_cast<BasicType*>(genericValue) : nullptr;
    }

    // Get or create pointer to structs and classes for a specific layer (mutable - creates if not present)
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    ClassType* getOrCreateValuePtr(const RtxOptionLayer* layer) {
      auto [genericValue, _] = getOrCreateGenericValue(layer);
      return genericValue ? reinterpret_cast<ClassType*>(genericValue->pointer) : nullptr;
    }

    // Helper methods to reduce code duplication between numeric and vector types
    bool setMinMaxValueHelper(const T& v, std::optional<GenericValue>& targetValue) {
      bool changed = false;

      if constexpr (std::is_pod_v<T>) {
        // For POD types (int, float, etc.), store directly in the value field
        GenericValue gv;
        if constexpr (std::is_same_v<T, float>) {
          changed = targetValue.has_value() ? targetValue.value().f != v : true;
          gv.f = v;
        } else {
          // For bool, int, and other POD types, use the value field
          uint64_t value = static_cast<uint64_t>(v);
          changed = targetValue.has_value() ? targetValue.value().value != value : true;
          gv.value = value;
        }

        targetValue = std::optional<GenericValue>(gv);

      } else {
        // For non-POD types (vectors, etc.), store as pointer
        if (!targetValue.has_value()) {
          targetValue = std::optional<GenericValue>(GenericValue{});
          // Note: This is a `new` with no matching `delete`. This is safe because the
          // RtxOptionImpl object is never destroyed, and follows the pattern used in the constructor.
          targetValue.value().pointer = new T();
        }

        T* valuePtr { reinterpret_cast<T*>(targetValue.value().pointer) };
        changed = *valuePtr != v;
        *valuePtr = v;
      }

      return changed;
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
// The RTX_OPTION* macros provide a convenient way to declare a serializable option
// Example usage, presuming "optionName" is the name of the option:
// optionName(); // Get the current value of the option
// optionName.set(value); // Set the value of the option
// RemixGui::Checkbox("Option Name", &optionName); // Draw the option in the UI
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
}
