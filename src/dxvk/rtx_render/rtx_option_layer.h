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

#include <atomic>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "../util/config/config.h"
#include "rtx_option_constants.h"

namespace dxvk {

  // Forward declarations
  class RtxOptionImpl;
  class RtxOptionManager;
  union GenericValue;

  // Enum representing the target layer category for runtime RtxOption edits.
  // This is used with RtxOptionLayerTarget to control where GUI/code changes are applied.
  // The actual layer is determined by the edit target + the option's UserSettings flag:
  // 
  // User-driven (UI-initiated):
  //   - UserSettings flag → User Settings layer
  //   - No flag → Remix Config layer
  // 
  // Code-driven (Code-initiated):
  //   - UserSettings flag → Quality layer (or User Settings if Graphics Preset is Custom)
  //   - No flag → Derived layer
  enum class RtxOptionEditTarget {
    User,    // User-driven changes from any UI (User Graphics Menu or Dev Menu)
    Derived  // Code-driven changes (quality presets, OnChange callbacks, automation)
  };

  // Represents an RTX option layer that can override rendering settings.
  // Layers are prioritized and can be dynamically enabled/disabled at runtime.
  // Typical usage: stack multiple layers (default, app config, user config, runtime changes),
  // then resolve options based on priority and strength.
  class RtxOptionLayer {
    friend class RtxOptionImpl;
    friend class RtxOptionManager;

  public:
    enum class EnabledRequest : int8_t {
      NoRequest = -1,      // No request made this frame
      RequestDisabled = 0, // At least one component requested disabled, none requested enabled
      RequestEnabled = 1   // At least one component requested enabled (wins over disabled)
    };


    // Constructor for creating option layers
    // Should not be called directly. Use RtxOptionManager::acquireLayer instead.
    // filePath: The full path to the config file this layer was loaded from (empty for layers without files)
    // layerName: The display name for this layer (used for UI and layer identification)
    RtxOptionLayer(const Config& config, const std::string& filePath, const RtxOptionLayerKey& layerKey, const float blendStrength, const float blendThreshold);

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

    // Apply any pending changes from this layer to all options, then clear dirty state.
    // Returns true if any changes were applied.
    bool applyPendingChanges();

    // Mark this layer as dirty (e.g., changed values need reprocessing).
    void markDirty() { m_dirty = true; }

    // Apply this layer's values to all options
    void applyToAllOptions();
    
    // Remove this layer's values from all options (NoReset options are preserved)
    void removeFromAllOptions() const;

    void setConfig(const Config& config) {
      m_config = config;
    }

    bool isValid() const { return m_config.getOptions().size() > 0; }
    bool isEnabled() const { return m_enabled; }
    // Returns true if this layer is enabled and it's blend strength is at or above its threshold.
    // For non-float option types, values from inactive layers are not applied.
    bool isActive() const { return m_enabled && m_blendStrength >= m_blendThreshold; }
    const Config& getConfig() const { return m_config; }
    float getBlendStrength() const { return m_blendStrength; }
    float getBlendStrengthThreshold() const { return m_blendThreshold; }
    bool isDirty() const { return m_dirty; }
    bool isBlendStrengthDirty() const { return m_blendStrengthDirty; }
    const std::string& getName() const { return m_layerName; }
    const std::string& getFilePath() const { return m_filePath; }
    const RtxOptionLayerKey& getLayerKey() const { return m_layerKey; }

    // Returns true if this layer contains any option values.
    // This is computed dynamically by checking all global options.
    bool hasValues() const;
    
    // Mark that this layer has values (hint for optimization, actual check is dynamic)
    void setHasValues(bool hasValues) const { m_hasValues = hasValues; }

    // Returns true if this layer has unsaved changes (runtime modifications not yet persisted to disk)
    // Only meaningful for layers that have an associated config file (e.g., rtx.conf, user.conf)
    // This detects both:
    // - Runtime values that differ from the saved config (additions/modifications)
    // - config values that have been removed in the runtime.
    bool hasUnsavedChanges() const;
    
    // Called when a value in this layer changes - invalidates caches
    // Actual computation is deferred until the cached values are queried
    void onLayerValueChanged() const {
      m_unsavedChangesCacheDirty = true;
      m_miscategorizedOptionCountDirty = true;
    }
    
    // Returns the count of options in this layer that don't belong here based on their flags.
    // See RtxOptionFlags for more information.
    // Uses lazy caching - only recalculates when layer values change or category flags change.
    uint32_t countMiscategorizedOptions() const;
    
    // Migrates all miscategorized options from this layer to their correct layer.
    // See RtxOptionFlags for more information about what gets migrated where.
    // Returns the number of options that were migrated.
    uint32_t migrateMiscategorizedOptions();
    
    // Returns true if the saved config has values that would be removed on save
    // (i.e., values in config file but not in the layer)
    bool hasPendingRemovals() const;

    // Returns true if this layer has an associated config file that can be saved to disk
    // Layers without a file path (quality, derived, environment, default, config.cpp) return false
    bool hasSaveableConfigFile() const {
      return !m_filePath.empty();
    }

    // Get the pending enabled state for UI display (returns current state if no pending request)
    bool getPendingEnabled() const {
      if (m_pendingEnabledRequest != EnabledRequest::NoRequest) {
        return m_pendingEnabledRequest == EnabledRequest::RequestEnabled;
      }
      return m_enabled;
    }

    // Get the pending blend strength for UI display (returns current strength if no pending request)
    float getPendingBlendStrength() const {
      if (m_pendingMaxBlendStrength > kRtxOptionLayerEmptyBlendStrengthRequest) {
        return m_pendingMaxBlendStrength;
      }
      return m_blendStrength;
    }

    // Get the pending blend threshold for UI display (returns current threshold if no pending request)
    float getPendingBlendThreshold() const {
      if (m_pendingMinBlendThreshold < kRtxOptionLayerEmptyBlendThresholdRequest) {
        return m_pendingMinBlendThreshold;
      }
      return m_blendThreshold;
    }

    // ============================================================================
    // Layer save/reload methods
    // ============================================================================
    
    // Save this layer's current values to its config file
    // Returns true if save was successful, false if layer has no file path
    bool save();
    
    // Reload this layer's values from its config file, discarding any unsaved changes
    // Returns true if reload was successful, false if layer has no file path
    bool reload();
    
    // Export unsaved changes from this layer to a config file.
    // If the target file already exists, the changes are merged intelligently:
    // - For hash set options: new opinions override conflicting opinions in the file
    // - For other options: new values overwrite existing values
    // Returns true if export was successful, false if layer has no unsaved changes
    bool exportUnsavedChanges(const std::string& exportPath) const;
    
    // Callback types for forEachChange
    using OptionChangeCallback = std::function<void(RtxOptionImpl*, const GenericValue*)>;
    using RemovedOptionCallback = std::function<void(RtxOptionImpl*, const std::string& savedValue)>;
    
    // Iterate through options in this layer, calling the appropriate callback for each state.
    // Any callback can be left null/empty to skip processing that category of options.
    // - addedCallback: called for options present in runtime but not in saved config
    // - modifiedCallback: called for options present in both runtime and saved config, but with different values
    // - removedCallback: called for options present in saved config but not in runtime (only if the layer has an associated .conf file)
    // - unchangedCallback: called for options present in both runtime and saved config with same values
    void forEachChange(
      const OptionChangeCallback& addedCallback,
      const OptionChangeCallback& modifiedCallback,
      const RemovedOptionCallback& removedCallback,
      const OptionChangeCallback& unchangedCallback
    ) const;

    // ============================================================================
    // Static factory methods for creating layers from environment variables
    // ============================================================================
    
    // Resolve config file paths from an environment variable
    // Returns vector of paths (may be multiple for comma-separated env var values)
    // If env var is not set, returns the defaultFileName
    static std::vector<std::string> resolveConfigPaths(const char* envVarName, const char* defaultFileName);

    // Create layer(s) from paths that may come from an environment variable
    // Handles multiple files by creating numbered layers (e.g., "00_rtx.conf", "01_rtx.conf")
    // Returns the created layers in order (first file = lowest priority within same SystemLayerPriority)
    static std::vector<RtxOptionLayer*> createLayersFromEnvVar(
        const char* envVarName,
        const char* defaultFileName,
        const RtxOptionLayerKey& baseLayer);

    // ============================================================================
    // System layer management
    // ============================================================================
    
    // Initialize all system layers and build the merged config for DxvkOptions.
    // Must be called once during startup before any RtxOptions are accessed.
    // Returns the merged config from all config-file-based layers.
    static const Config& initializeSystemLayers();
    
    // Get the merged configuration from config.cpp, all dxvk.conf, and all rtx.conf.
    // This includes config.cpp, dxvk.conf, rtx.conf, and baseGameMod rtx.conf.
    // Available after initializeSystemLayers() is called.
    static const Config& getMergedConfig() { return s_mergedConfig; }
    
    // Accessors for system layers
    static const RtxOptionLayer* getUserLayer() { return s_userLayer; }
    static const RtxOptionLayer* getDefaultLayer();
    static RtxOptionLayer* getRtxConfLayer() { return s_rtxConfLayer; }
    static const RtxOptionLayer* getEnvironmentLayer() { return s_environmentLayer; }
    static const RtxOptionLayer* getQualityLayer() { return s_qualityLayer; }
    static const RtxOptionLayer* getDerivedLayer() { return s_derivedLayer; }

  private:
    // Set the category flags for this layer.
    // This enables detection of options that don't belong in this layer.
    // Called during initializeSystemLayers() - not part of public API.
    void setCategoryFlags(uint32_t flags) {
      if (m_categoryFlags != flags) {
        m_categoryFlags = flags;
        m_miscategorizedOptionCountDirty = true;
      }
    }
    
    // Internal helper to recalculate unsaved changes - called lazily from hasUnsavedChanges()
    void recalculateUnsavedChangesInternal() const;
    
    // Cached pointers to system layers (initialized once during initializeSystemLayers)
    inline static const RtxOptionLayer* s_defaultLayer = nullptr;
    inline static RtxOptionLayer* s_rtxConfLayer = nullptr;
    inline static const RtxOptionLayer* s_environmentLayer = nullptr;
    inline static const RtxOptionLayer* s_qualityLayer = nullptr;
    inline static const RtxOptionLayer* s_derivedLayer = nullptr;
    inline static RtxOptionLayer* s_userLayer = nullptr;

    // Merged config from all config-file-based layers
    inline static Config s_mergedConfig;

    // Reference counting - only accessible by RtxOptionManager
    // Thread-safe read of the reference count.
    // Uses acquire ordering to ensure any writes from other threads are visible.
    size_t getRefCount() const { return m_refCount.load(std::memory_order_acquire); }
    
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

    std::string m_filePath;   // Full path to the config file (empty for layers without files)
    std::string m_layerName;  // Display name for this layer (owns the string that m_layerKey.name points to)
    RtxOptionLayerKey m_layerKey;      // Layer key for registry lookups (name points to m_layerName)

    bool m_enabled;
    bool m_dirty;
    bool m_blendStrengthDirty;
    
    // Cache-related state (mutable because they are computed lazily from const methods)
    mutable bool m_hasValues = false;        // True if this layer has any option values
    mutable bool m_hasUnsavedChanges = false;  // Cached result: true if layer has runtime changes not yet saved
    mutable bool m_unsavedChangesCacheDirty = false;  // True if m_hasUnsavedChanges needs recalculation
    mutable std::atomic<size_t> m_refCount = 0;  // Reference counting is orthogonal to const-ness
    
    // Layer placement validation - detects options that don't belong in this layer
    uint32_t m_categoryFlags = 0;  // Options must have these flags to belong here (0 = general developer options only)
    mutable uint32_t m_miscategorizedOptionCount = 0;  // Cached count of options that should migrate to another layer
    mutable bool m_miscategorizedOptionCountDirty = true;  // True if m_miscategorizedOptionCount needs recalculation

    Config m_config;

    // Blend weight for this layer in the [0,1] range.
    // Controls how strongly this layer influences the final result.
    // 0 = no effect, 1 = fully applied.
    float m_blendStrength;

    // Only used for non-float variables in a layer. These variables will be only enabled when strength larger than threshold.
    float m_blendThreshold;

    // Pending requests from multiple components during the current frame.
    // These are accumulated and resolved once per frame before option resolution.
    // If any component requests enabled=true, the enum will be RequestEnabled.
    // If only false requests are made, it will be RequestDisabled.
    // If no requests are made, it will be NoRequest (no change).
    EnabledRequest m_pendingEnabledRequest;
    // Tracks the maximum requested blend strength for this frame.
    float m_pendingMaxBlendStrength;
    // Tracks the minimum requested blend threshold for this frame.
    float m_pendingMinBlendThreshold;
  };

  // RAII helper to temporarily set the target layer for RtxOption edits.
  // The current target layer is stored in thread-local storage to allow different
  // threads (e.g., main thread vs render thread) to independently control their edit target.
  //
  // Usage:
  //   void showUserMenu() {
  //     RtxOptionLayerTarget target(RtxOptionEditTarget::User);
  //     // All RtxOption changes in this scope are user-driven (routed by UserSetting flag)
  //     someOption.setDeferred(newValue);
  //   }
  //
  //   void updateQualityPreset() {
  //     RtxOptionLayerTarget target(RtxOptionEditTarget::Derived);
  //     // All RtxOption changes in this scope are code-driven (routed by UserSetting flag)
  //     someOption.setDeferred(newValue);
  //   }
  class RtxOptionLayerTarget {
  public:
    // Construct with a target type. The layer is resolved from the target type.
    explicit RtxOptionLayerTarget(RtxOptionEditTarget target)
      : m_previousTarget(s_currentTarget) {
      s_currentTarget = target;
    }

    // Destructor restores the previous target
    ~RtxOptionLayerTarget() {
      s_currentTarget = m_previousTarget;
    }

    // Delete copy operations to prevent misuse
    RtxOptionLayerTarget(const RtxOptionLayerTarget&) = delete;
    RtxOptionLayerTarget& operator=(const RtxOptionLayerTarget&) = delete;

  private:
    // RtxOptionImpl needs access to getEditTarget() for getTargetLayer()
    friend class RtxOptionImpl;
    
    // Get the active edit target.
    // NOTE: This is private - external code should use RtxOptionImpl::getTargetLayer()
    // which applies flag-based routing rules (e.g., NoSave -> Derived layer).
    static RtxOptionEditTarget getEditTarget() {
      return s_currentTarget;
    }

    RtxOptionEditTarget m_previousTarget;

    // Thread-local current target. Defaults to Derived for programmatic/derived changes
    // when no menu is open. User target is set when in the User Graphics Menu,
    // Modder target is set when in the Dev Menu, Quality target is set when
    // applying graphics preset changes.
    // Note: Using thread_local ensures that each thread has its own target,
    // which is important for multi-threaded rendering where different threads
    // may be operating on different menus.
    inline static thread_local RtxOptionEditTarget s_currentTarget = RtxOptionEditTarget::Derived;
  };

} // namespace dxvk
