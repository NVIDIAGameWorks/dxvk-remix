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
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <limits>
#include <mutex>
#include <atomic>

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
  class DxvkDevice;
  
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
    HashSet,     // Merges when present in multiple layers.
    HashVector,  // Does not merge when present in multiple layers. Use when order & number of elements is important.
    Vector2,
    Vector3,
    Vector2i,
    String,
    VirtualKeys,
    Vector4
  };

  enum class OptionLayerType {
    User,
    Rtx,
    Quality,
    None
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
    VirtualKeys* virtualKeys;
    std::string* string;
    int64_t value;
    void* pointer;
  };

  // Forward declaration
  class RtxOptionLayerManager;

  // Represents an RTX option layer that can override rendering settings.
  // Layers are prioritized and can be dynamically enabled/disabled at runtime.
  // Typical usage: stack multiple layers (default, app config, user config, runtime changes),
  // then resolve options based on priority and strength.
  class RtxOptionLayer {
    friend struct RtxOptionImpl;
    friend class RtxOptionLayerManager;

  public:
    enum class EnabledRequest : int8_t {
      NoRequest = -1,      // No request made this frame
      RequestDisabled = 0, // At least one component requested disabled, none requested enabled
      RequestEnabled = 1   // At least one component requested enabled (wins over disabled)
    };

    enum class SystemLayerPriority : uint32_t {
      Default = 0,
      DxvkConf = 1,
      RtxConf = 2,
      Quality = 3,
      Mod = 4,
      NONE = 0xFFFFFFFE,
      USER = 0xFFFFFFFF
    };

    // Constructor for creating option layers
    // Should not be called directly. Use RtxOptionLayerManager::acquireLayer instead.
    RtxOptionLayer(const Config& config, const std::string& configName, const uint32_t priority, const float blendStrength, const float blendThreshold);

    ~RtxOptionLayer();

    // Delete copy constructor and copy assignment - atomic members cannot be copied
    RtxOptionLayer(const RtxOptionLayer&) = delete;
    RtxOptionLayer& operator=(const RtxOptionLayer&) = delete;

    // Delete move operations - atomic members make moves non-trivial, use construct-in-place instead
    RtxOptionLayer(RtxOptionLayer&&) = delete;
    RtxOptionLayer& operator=(RtxOptionLayer&&) = delete;

    // Request to enable/disable this layer. Multiple components can call this per frame.
    // The layer will be enabled if ANY component requests it to be enabled.
    // Use this for components that share control of a layer.
    void requestEnabled(bool enabled) {
      if (enabled) {
        m_pendingEnabledRequest = EnabledRequest::RequestEnabled;
      } else if (m_pendingEnabledRequest == EnabledRequest::NoRequest) {
        m_pendingEnabledRequest = EnabledRequest::RequestDisabled;
      }
    }

    // Request a blend strength. Multiple components can call this per frame.
    // The final blend strength will be the MAX of all requests.
    // Use this for components that share control of a layer.
    void requestBlendStrength(float strength) {
      if (strength > m_pendingMaxBlendStrength) {
        m_pendingMaxBlendStrength = strength;
      }
    }

    // Request a blend threshold. Multiple components can call this per frame.
    // The final blend threshold will be the MIN of all requests.
    // Use this for components that share control of a layer.
    void requestBlendThreshold(float threshold) {
      if (threshold < m_pendingMinBlendThreshold) {
        m_pendingMinBlendThreshold = threshold;
      }
    }

    // Resolve all pending requests accumulated during the frame.
    // Should be called once per frame before option resolution.
    void resolvePendingRequests();

    // Mark this layer as dirty (e.g., changed values need reprocessing).
    void setDirty(bool dirty) const { m_dirty = dirty; }
    void setBlendStrengthDirty(bool dirty) const {
      setDirty(dirty);
      m_blendStrengthDirty = dirty;
    }

    void setConfig(const Config& config) {
      m_config = config;
    }

    const bool isValid() const { return m_config.getOptions().size() > 0; }
    const bool isEnabled() const { return m_enabled; }
    const Config& getConfig() const { return m_config; }
    const uint32_t getPriority() const { return m_priority; }
    const float getBlendStrength() const { return m_blendStrength; }
    const float getBlendStrengthThreshold() const { return m_blendThreshold; }
    const bool isDirty() const { return m_dirty; }
    const bool isBlendStrengthDirty() const { return m_blendStrengthDirty; }
    const std::string& getName() const { return m_configName; }

    // Get the pending enabled state for UI display (returns current state if no pending request)
    bool getPendingEnabled() const {
      if (m_pendingEnabledRequest != EnabledRequest::NoRequest) {
        return m_pendingEnabledRequest == EnabledRequest::RequestEnabled;
      }
      return m_enabled;
    }

    // Get the pending blend strength for UI display (returns current strength if no pending request)
    float getPendingBlendStrength() const {
      if (m_pendingMaxBlendStrength > kEmptyBlendStrengthRequest) {
        return m_pendingMaxBlendStrength;
      }
      return m_blendStrength;
    }

    // Get the pending blend threshold for UI display (returns current threshold if no pending request)
    float getPendingBlendThreshold() const {
      if (m_pendingMinBlendThreshold < kEmptyBlendThresholdRequest) {
        return m_pendingMinBlendThreshold;
      }
      return m_blendThreshold;
    }

    static bool shouldResetSettings() { return s_resetRuntimeSettings; }
    static void setResetSettings(bool reset) { s_resetRuntimeSettings = reset; }

    // Minimum priority value for user option layers.
    // System layers use priorities 0-99, user layers use 100+.
    // Ensures built-in configs (like rtx.conf) always have lower priority than user configs.
    static constexpr uint32_t s_userOptionLayerOffset = 100;

    // Reserved highest priority for runtime modifications (e.g., GUI changes)
    static constexpr uint32_t s_runtimeOptionLayerPriority = 0xFFFFFFFF;

    // Sentinel values for pending request tracking
    // Blend strength uses MAX logic, so initialize below valid range [0.0, 1.0]
    static constexpr float kEmptyBlendStrengthRequest = -1.0f;
    // Blend threshold uses MIN logic, so initialize above valid range [0.0, 1.0]
    static constexpr float kEmptyBlendThresholdRequest = 2.0f;

  private:
    // Reference counting - only accessible by RtxOptionLayerManager
    // Thread-safe read of the reference count.
    // Uses acquire ordering to ensure any writes from other threads are visible.
    const size_t getRefCount() const { return m_refCount.load(std::memory_order_acquire); }
    
    // Thread-safe increment of the reference count.
    // Uses fetch_add with acq_rel ordering to atomically increment and synchronize with other threads.
    void incrementRefCount() const { m_refCount.fetch_add(1, std::memory_order_acq_rel); }
    
    // Thread-safe decrement of the reference count with zero-check.
    // Uses compare-exchange loop to atomically check if count > 0 and decrement if true.
    // This prevents race conditions where multiple threads might decrement past zero.
    // The loop handles spurious failures from compare_exchange_weak.
    void decrementRefCount() const {
      size_t expected = m_refCount.load(std::memory_order_acquire);
      while (expected > 0) {
        // Try to atomically decrement from expected to expected-1
        if (m_refCount.compare_exchange_weak(expected, expected - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
          // Successfully decremented
          break;
        }
        // compare_exchange_weak failed (either spuriously or because another thread modified m_refCount)
        // 'expected' is now updated with the current value, so we loop again with the new value
      }
    }

    std::string m_configName;

    mutable bool m_enabled;
    mutable bool m_dirty;
    mutable bool m_blendStrengthDirty;
    mutable std::atomic<size_t> m_refCount = 0;

    Config m_config;

    // Layer priority used to order blending.
    // Higher priority layers blend on top of lower ones,
    // using m_blendStrength as the blend factor (like lerp(low, high, m_blendStrength)).
    // Multiple layers can share the same priority; they are ordered alphabetically by name.
    uint32_t m_priority;

    // Blend weight for this layer in the [0,1] range.
    // Controls how strongly this layer influences the final result.
    // 0 = no effect, 1 = fully applied.
    mutable float m_blendStrength;

    // Only used for non-float variables in a layer. These variables will be only enabled when strength larger than threshold.
    mutable float m_blendThreshold;

    // Pending requests from multiple components during the current frame.
    // These are accumulated and resolved once per frame before option resolution.
    // If any component requests enabled=true, the enum will be RequestEnabled.
    // If only false requests are made, it will be RequestDisabled.
    // If no requests are made, it will be NoRequest (no change).
    mutable EnabledRequest m_pendingEnabledRequest;
    // Tracks the maximum requested blend strength for this frame.
    mutable float m_pendingMaxBlendStrength;
    // Tracks the minimum requested blend threshold for this frame.
    mutable float m_pendingMinBlendThreshold;

    // Global static flag to indicate runtime settings need resetting.
    static bool s_resetRuntimeSettings;
  };

  struct RtxOptionImpl {
    using RtxOptionMap = std::map<XXH64_hash_t, std::shared_ptr<RtxOptionImpl>>;
    enum class ValueType {
      Value = 0,
      PendingValue = 1,
      DefaultValue = 2,
    };

    // Represents a single option value along with its priority and blend strength.
    // Used in the option layer system to resolve final settings when multiple layers are active.
    // The actual priority is stored in the optionLayerValueQueue key for this value.
    struct PrioritizedValue {
      PrioritizedValue() { }
      PrioritizedValue(const GenericValue& v, const float b, const float threshold) : value(v), blendStrength(b), blendThreshold(threshold) { }

      mutable GenericValue value; // The actual option value
      mutable float blendStrength = 1.0f; // Blend weight, which allows smooth interpolation between overlapping option layers.
      mutable float blendThreshold = 0.5; // Blending strength threshold for this option layer. Only applicable to non-float variables. The option is enabled only when the blend strength exceeds this threshold.
    };

    XXH64_hash_t hash;
    const char* name;
    const char* category;
    const char* environment = nullptr;
    const char* description; // Description string for the option that will get included in documentation
    OptionType type;
    GenericValue resolvedValue;
    std::optional<GenericValue> minValue;
    std::optional<GenericValue> maxValue;
    uint32_t flags = 0;
    std::function<void(DxvkDevice* device)> onChangeCallback;

    // --- Containers for option layers ---
    // 
    // Key type for layer maps: (priority, config name view)
    // Multiple layers can share the same priority value and are ordered alphabetically.
    // The string_view points to the layer's owned name string.
    struct LayerKey {
      uint32_t priority;
      std::string_view configName;
      
      // Comparison operator for map ordering
      bool operator<(const LayerKey& other) const {
        if (priority != other.priority) {
          return priority > other.priority;
        }
        return configName < other.configName;
      }
    };
    
    // Stores all RTX option layers, keyed by (priority, config name).
    // Layers are stored as unique_ptr so the key's string_view can safely point to the layer's name.
    // Each RtxOptionLayer can represent a source of settings
    // (default configs, app configs, user configs, runtime GUI, etc.).
    using RtxOptionLayerMap = std::map<LayerKey, std::unique_ptr<RtxOptionLayer>>;
    
    std::map<LayerKey, PrioritizedValue> optionLayerValueQueue;

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

    const GenericValue& getGenericValue(const ValueType valueType) const;
    GenericValue& getGenericValue(const ValueType valueType);

    const char* getTypeString() const;
    std::string genericValueToString(ValueType valueType) const;
    std::string genericValueToString(const GenericValue& value) const;
    void copyValue(const GenericValue& source, GenericValue& target);
    bool resolveValue(GenericValue& value, const bool ignoreChangedOption);
    void addWeightedValue(const GenericValue& source, const float weight, GenericValue& target);

    void readValue(const Config& options, const std::string& fullName, GenericValue& value);
    void readOption(const Config& options, ValueType type);
    void writeOption(Config& options, bool changedOptionOnly);

    void insertEmptyOptionLayer(const RtxOptionLayer* layer);
    void insertOptionLayerValue(const GenericValue& value, const RtxOptionLayer* layer);
    void readOptionLayer(const RtxOptionLayer& optionLayer);
    void disableLayerValue(const RtxOptionLayer* layer);
    void disableTopLayer();
    void updateLayerBlendStrength(const RtxOptionLayer& optionLayer);

    bool isDefault() const;
    bool isEqual(const GenericValue& aValue, const GenericValue& bValue) const;

    void resetOption();

    void markDirty() {
      getDirtyRtxOptionMap()[hash] = this;
    }

    void invokeOnChangeCallback(DxvkDevice* device) const;

    // Returns true if the value was changed
    bool clampValue(GenericValue& value);

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

    // Returns a global container holding all option layers
    static RtxOptionLayerMap& getRtxOptionLayerMap();
    // Get an option layer from the global map by priority and config name
    // Returns a pointer to the layer, or nullptr if not found
    static RtxOptionLayer* getRtxOptionLayer(const uint32_t priority, const std::string_view configName);
    // Add an option layer to global option layer map
    // Returns a pointer to the newly created layer, or nullptr if the layer was invalid
    // If config is provided, uses it directly; otherwise loads from configPath
    static const RtxOptionLayer* addRtxOptionLayer(
      const std::string& configPath, const uint32_t priority, const bool isSystemOptionLayer,
      const float blendStrength, const float blendThreshold, const Config* config = nullptr);
    // Remove an option layer from the global option layer map by pointer
    // Returns true if the layer was found and removed
    static bool removeRtxOptionLayer(const RtxOptionLayer* layer);
    // Get or create the runtime layer (for dynamic UI changes)
    static const RtxOptionLayer* getRuntimeLayer();
    // Get or create the default layer (for in-code default values)
    static const RtxOptionLayer* getDefaultLayer();

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

    // NOTE: onChange handlers can be invoked before the DxvkDevice is created,
    // so these callbacks need to null check the device.
    typedef void (*RtxOptionOnChangeCallback)(DxvkDevice* device);
    RtxOptionOnChangeCallback onChangeCallback = nullptr;
  };

  // Non-templated helper class for global RtxOption operations
  // Use this for static operations that don't depend on a specific option type
  class RtxOptionManager {
  public:
    static void setStartupConfig(const Config& options) {
      RtxOptionImpl::setStartupConfig(options);
    }
    static void setCustomConfig(const Config& options) {
      RtxOptionImpl::setCustomConfig(options);
    }
    static void readOptions(const Config& options) {
      RtxOptionImpl::readOptions(options);
    }
    static void writeOptions(Config& options, bool changedOptionsOnly) {
      RtxOptionImpl::writeOptions(options, changedOptionsOnly);
    }
    static void resetOptions() {
      RtxOptionImpl::resetOptions();
    }

    // Update all RTX options after setStartupConfig() and setCustomConfig() have been called
    static void initializeRtxOptions() {
      // This method is called every time a dxvk context is created, which may happen multiple times.
      // Need to ensure RtxOption isn't invoking change callbacks during the initialization step.
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

    // Add a new RTX option layer (e.g., user config, runtime changes) to all global options.
    // Reads the option layer into every global RtxOptionImpl instance.
    static void addRtxOptionLayer(const RtxOptionLayer& optionLayer) {
      // Do nothing for invalid(empty) layers
      if (!optionLayer.isValid()) {
        return;
      }

      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
        rtxOption.readOptionLayer(optionLayer);
      }
    }

    // Remove an existing RTX option layer from all global options.
    static void removeRtxOptionLayer(const RtxOptionLayer& optionLayer) {
      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
        rtxOption.disableLayerValue(&optionLayer);
      }
    }

    // Update an existing RTX option layer from all global options.
    static void updateRtxOptionLayer(const RtxOptionLayer& optionLayer) {
      auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
        rtxOption.updateLayerBlendStrength(optionLayer);
      }
    }

    // Apply all pending option values and synchronize dirty option layers.
    static void applyPendingValuesOptionLayers() {
      std::unique_lock<std::mutex> lock(RtxOptionImpl::s_updateMutex);

      // First, resolve all pending requests from components for this frame
      for (auto& [layerKey, optionLayerPtr] : RtxOptionImpl::getRtxOptionLayerMap()) {
        optionLayerPtr->resolvePendingRequests();
      }

      // Then apply dirty option layers (enable or disable).
      for (auto& [layerKey, optionLayerPtr] : RtxOptionImpl::getRtxOptionLayerMap()) {
        RtxOptionLayer& optionLayer = *optionLayerPtr;
        if (optionLayer.isDirty()) {
          if (optionLayer.isEnabled()) {
            RtxOptionManager::addRtxOptionLayer(optionLayer);
          } else {
            RtxOptionManager::removeRtxOptionLayer(optionLayer);
          }
        }

        if (optionLayer.isBlendStrengthDirty()) {
          RtxOptionManager::updateRtxOptionLayer(optionLayer);
        }

        optionLayer.setDirty(false);
        optionLayer.setBlendStrengthDirty(false);
      }

      // If a reset was requested, remove runtime option layers (unless they are marked NoReset), so underlying config layers can take effect again.
      if (RtxOptionLayer::shouldResetSettings()) {
        auto& globalRtxOptions = RtxOptionImpl::getGlobalRtxOptionMap();
        for (auto& rtxOptionMapEntry : globalRtxOptions) {
          RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second.get();
          if (rtxOption.optionLayerValueQueue.begin()->first.priority == RtxOptionLayer::s_runtimeOptionLayerPriority &&
              ((rtxOption.flags & (uint32_t) RtxOptionFlags::NoReset) == 0)) {
            // Erase runtime option, so we can enable option layer configs
            rtxOption.disableTopLayer();
            rtxOption.markDirty();
          }
        }
        RtxOptionLayer::setResetSettings(false);
      }

      lock.unlock();
    }

    // This will apply all of the RtxOption::set() calls that have been made since the last time it was called.
    // This should be called at the very end of the frame in the dxvk-cs thread.
    // Before the first frame is rendered, it also needs to be called at least once during initialization.
    // It's currently called twice during init, due to multiple sections that set many Options then immediately use them.
    // forceOnChange causes the onChange callback to be called even if the value has not changed 
    static void applyPendingValues(DxvkDevice* device, bool forceOnChange) {

      constexpr static int32_t maxResolves = 4;
      int32_t numResolves = 0;

      // Iteratively resolve the dirty options, invoke callbacks, rinse and repeat until until no 
      // dirty options are left. 
      while (numResolves < maxResolves) {
        std::unique_lock<std::mutex> lock(RtxOptionImpl::s_updateMutex);

        auto& dirtyOptions = RtxOptionImpl::getDirtyRtxOptionMap();

        // Need a second array so that we can invoke onChange callbacks after updating values and clearing the dirty list.
        std::vector<RtxOptionImpl*> dirtyOptionsVector;
        dirtyOptionsVector.reserve(dirtyOptions.size());
        {
          for (auto& rtxOption : dirtyOptions) {
            const bool valueChanged = rtxOption.second->resolveValue(rtxOption.second->resolvedValue, false);
            if (forceOnChange || valueChanged) {
              dirtyOptionsVector.push_back(rtxOption.second);
            }
          }
        }
        dirtyOptions.clear();
        lock.unlock();

        // Invoke onChange callbacks after promoting all the values
        for (RtxOptionImpl* rtxOption : dirtyOptionsVector) {
          rtxOption->invokeOnChangeCallback(device);
        }

        numResolves++;

        // If the callbacks didn't generate any dirtied options, bail
        if (dirtyOptions.empty()) {
          break;
        }
      }

#if RTX_OPTION_DEBUG_LOGGING
      const bool unresolvedChanges = numResolves == maxResolves && !dirtyOptions.empty();
      if (unresolvedChanges) {
        auto& dirtyOptions = RtxOptionImpl::getDirtyRtxOptionMap();

        Logger::warn(str::format("Dirty RtxOptions remaining after ", maxResolves, " passes of resolving callbacks, suggesting a cyclic dependency."));
        for (auto& rtxOption : dirtyOptions) {
          Logger::warn(str::format("- Abandoned resolve of option ", rtxOption.second->name));
        }
      }
#endif

      // Don't let dirty options persist across frames and explode the dirty option processing in the case of circular dependencies
      RtxOptionImpl::getDirtyRtxOptionMap().clear();
    }
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

    // Check if a hash exists in lower priority layers (below runtime layer)
    // This is useful to warn users that removing a hash from the runtime layer
    // won't actually remove it from the final resolved value, since lower layers
    // will still contribute it via additive combination.
    template<typename = std::enable_if_t<std::is_same_v<T, fast_unordered_set>>>
    const std::string_view retrieveNonRuntimeConfigName(const XXH64_hash_t& value) const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      
      // Iterate through all layers except the runtime layer (highest priority)
      for (const auto& [layerKey, prioritizedValue] : pImpl->optionLayerValueQueue) {
        // Skip the runtime layer - we only care about lower priority layers
        if (layerKey.priority == RtxOptionLayer::s_runtimeOptionLayerPriority) {
          continue;
        }
        
        // Check if this layer's hash set contains the value
        const fast_unordered_set* layerHashSet = prioritizedValue.value.hashSet;
        if (layerHashSet && layerHashSet->count(value) > 0) {
          return layerKey.configName;
        }
      }
      
      return std::string_view {};
    }

    T& getDefaultValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      return *getValuePtr<T>(RtxOptionImpl::ValueType::DefaultValue);
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
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      
      bool changed = setMinMaxValueHelper(v, pImpl->minValue);
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
      bool changed = setMinMaxValueHelper(v, pImpl->maxValue);
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
        pImpl->resolvedValue.value = 0;
        *reinterpret_cast<BasicType*>(&pImpl->resolvedValue.value) = value;
        // Push default value to option layer priority queue
        const RtxOptionLayer* defaultLayer = RtxOptionImpl::getDefaultLayer();
        if (defaultLayer) {
          pImpl->insertOptionLayerValue(pImpl->resolvedValue, defaultLayer);
        }

        initializeClamping(args);
      }
    }

    // Do not call these constructors directly. Use RTX_OPTION macro to declare and initialize RtxOption objects.
    // Constructor for structs and classes
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    RtxOption(const char* category, const char* name, const ClassType& value, const char* description, RtxOptionArgs<T> args) {
      if (allocateMemory(category, name, description, args)) {
        pImpl->resolvedValue.pointer = new ClassType(value);
        // Push default value to option layer priority queue
        const RtxOptionLayer* defaultLayer = RtxOptionImpl::getDefaultLayer();
        if (defaultLayer) {
          pImpl->insertOptionLayerValue(pImpl->resolvedValue, defaultLayer);
        }

        initializeClamping(args);
      }
    }

    const T& getValue() const {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
#if RTX_OPTION_DEBUG_LOGGING
      // Print out a warning whenever a dirty value is accessed.
      if (!pImpl->isEqual(resolvedValue, getValue(RtxOptionImpl::ValueType::PendingValue)) {
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

      pImpl->markDirty();
    }

    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    void setValue(const ClassType& v) {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::s_updateMutex);
      assert(RtxOptionImpl::s_isInitialized && "Trying to access an RtxOption before the config files have been loaded."); 
      ClassType* valuePtr = getValuePtr<ClassType>(RtxOptionImpl::ValueType::PendingValue);
      *valuePtr = v;

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
        globalRtxOptions[optionHash] = pImpl;
        return true;
      } else {
        assert(false && str::format("RtxOption with the same name already exists: ", fullName).c_str());
        // If the variable already exists, use the existing object
        pImpl = pOption->second;
        return false;
      }
    }

    // This needs to be done after the resolvedValue is initialized in the constructor.
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

    // Helper function to get the appropriate GenericValue based on ValueType
    GenericValue* getGenericValuePtr(RtxOptionImpl::ValueType type) const {
      switch (type) {
      case RtxOptionImpl::ValueType::Value: {
        return &pImpl->resolvedValue;
      }
      case RtxOptionImpl::ValueType::PendingValue: {
        if (pImpl->optionLayerValueQueue.empty() || 
            pImpl->optionLayerValueQueue.begin()->first.priority != RtxOptionLayer::s_runtimeOptionLayerPriority) {
          const RtxOptionLayer* runtimeLayer = RtxOptionImpl::getRuntimeLayer();
          if (runtimeLayer) {
            pImpl->insertEmptyOptionLayer(runtimeLayer);
          }
        }
        if (pImpl->optionLayerValueQueue.empty()) {
          return nullptr;
        }
        return &pImpl->optionLayerValueQueue.begin()->second.value;
      }
      case RtxOptionImpl::ValueType::DefaultValue: {
        if (pImpl->optionLayerValueQueue.empty()) {
          return nullptr;
        }
        // Get the lowest priority value (last element in descending priority order)
        return &pImpl->optionLayerValueQueue.rbegin()->second.value;
      }
      default:
        return nullptr;
      }
    }

    // Get pointer to basic types
    template <typename BasicType, std::enable_if_t<std::is_pod_v<BasicType>, bool> = true>
    BasicType* getValuePtr(RtxOptionImpl::ValueType type) const {
      GenericValue* genericValue = getGenericValuePtr(type);
      return genericValue ? reinterpret_cast<BasicType*>(genericValue) : nullptr;
    }

    // Get pointer to structs and classes
    template <typename ClassType, std::enable_if_t<!std::is_pod_v<ClassType>, bool> = true>
    ClassType* getValuePtr(RtxOptionImpl::ValueType type) const {
      GenericValue* genericValue = getGenericValuePtr(type);
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
