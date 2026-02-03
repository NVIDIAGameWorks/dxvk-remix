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

#include "rtx_option.h"
#include "rtx_mod_manager.h"
#include "../util/util_env.h"
#include "../util/log/log.h"

#include <iomanip>
#include <sstream>

namespace {
  // Helper to split comma-separated paths into a vector
  std::vector<std::string> splitPaths(const std::string& paths) {
    std::vector<std::string> result;
    if (paths.empty()) {
      return result;
    }
    std::stringstream ss(paths);
    std::string path;
    while (std::getline(ss, path, ',')) {
      if (!path.empty()) {
        result.push_back(path);
      }
    }
    return result;
  }

  // Helper to create a zero-padded index prefix for layer names when multiple files share a priority
  // The LAST entry gets no prefix (so layer key lookups still work), earlier entries get prefixed
  std::string makeLayerName(size_t index, size_t total, const std::string& baseName) {
    if (total <= 1 || index == total - 1) {
      // Single entry or last entry - use base name with no prefix
      return baseName;
    }
    // Earlier entries get 2-digit zero-padded prefix for alphabetical ordering
    // e.g., for 3 files: "00_rtx.conf", "01_rtx.conf", "rtx.conf"
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << index << "_" << baseName;
    return oss.str();
  }
}

namespace dxvk {

  // ============================================================================
  // RtxOptionLayer implementation
  // ============================================================================

  RtxOptionLayer::RtxOptionLayer(const Config& config, const std::string& filePath, const RtxOptionLayerKey& layerKey, const float blendStrength, const float blendThreshold)
    : m_filePath(filePath)
    , m_layerName(layerKey.name)
    , m_layerKey{ layerKey.priority, m_layerName }  // name points to our owned m_layerName
    , m_enabled(true)
    , m_dirty(false)
    , m_config(config)
    , m_blendStrength(blendStrength)
    , m_blendThreshold(blendThreshold)
    , m_pendingEnabledRequest(EnabledRequest::NoRequest)
    , m_pendingMaxBlendStrength(kRtxOptionLayerEmptyBlendStrengthRequest)
    , m_pendingMinBlendThreshold(kRtxOptionLayerEmptyBlendThresholdRequest) {
#if RTX_OPTION_DEBUG_LOGGING
    Logger::info(str::format("[RTX Option]: Added option layer: ", m_layerName,
                             "\nFile: ", m_filePath.empty() ? "(none)" : m_filePath,
                             "\nPriority: ", std::to_string(m_layerKey.priority),
                             "\nStrength: ", std::to_string(m_blendStrength)));
#endif
  }

  RtxOptionLayer::~RtxOptionLayer() = default;

  void RtxOptionLayer::resolvePendingRequests() {
    // Resolve enabled state if any component made a request
    if (m_pendingEnabledRequest != EnabledRequest::NoRequest) {
      bool newEnabledState = (m_pendingEnabledRequest == EnabledRequest::RequestEnabled);
      if (m_enabled != newEnabledState) {
        m_enabled = newEnabledState;
        m_dirty = true;
      }
      m_pendingEnabledRequest = EnabledRequest::NoRequest;
    }
    
    // Resolve blend strength if any component made a request
    // Only set m_blendStrengthDirty, not m_dirty - blend changes don't require re-reading config values
    if (m_pendingMaxBlendStrength > kRtxOptionLayerEmptyBlendStrengthRequest) {
      if (m_blendStrength != m_pendingMaxBlendStrength) {
        m_blendStrength = m_pendingMaxBlendStrength;
        m_blendStrengthDirty = true;
      }
      m_pendingMaxBlendStrength = kRtxOptionLayerEmptyBlendStrengthRequest;
    }
    
    // Resolve blend threshold if any component made a request
    if (m_pendingMinBlendThreshold < kRtxOptionLayerEmptyBlendThresholdRequest) {
      if (m_blendThreshold != m_pendingMinBlendThreshold) {
        m_blendThreshold = m_pendingMinBlendThreshold;
        m_blendStrengthDirty = true;
      }
      m_pendingMinBlendThreshold = kRtxOptionLayerEmptyBlendThresholdRequest;
    }
  }

  bool RtxOptionLayer::applyPendingChanges() {
    bool anyChanges = false;
    
    // Handle enabled/disabled state changes
    if (m_dirty) {
      if (m_enabled) {
        // Apply layer values - this also updates blend strength via insertOptionLayerValue
        applyToAllOptions();
      } else {
        removeFromAllOptions();
      }
      m_dirty = false;
      anyChanges = true;
    }
    
    // Handle blend strength changes (only if not already handled above)
    // This updates runtime values set via setDeferred that aren't in the config
    if (m_blendStrengthDirty) {
      auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
      for (auto& [hash, optionPtr] : globalRtxOptions) {
        optionPtr->updateLayerBlendStrength(*this);
      }
      m_blendStrengthDirty = false;
      anyChanges = true;
    }
    
    return anyChanges;
  }

  void RtxOptionLayer::applyToAllOptions() {
    if (!isValid()) {
      return;
    }
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    for (auto& [hash, optionPtr] : globalRtxOptions) {
      optionPtr->readOptionLayer(*this);
    }
    // Blend strength is handled by applyToAllOptions for config-loaded options
    m_blendStrengthDirty = false;
  }

  void RtxOptionLayer::removeFromAllOptions() const {
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    for (auto& [hash, optionPtr] : globalRtxOptions) {
      if (optionPtr->getFlags() & (uint32_t) RtxOptionFlags::NoReset) {
        continue;
      }
      optionPtr->disableLayerValue(this);
    }
    onLayerValueChanged();
  }

  bool RtxOptionLayer::hasValues() const {
    // Quick check using cached hint
    if (!m_hasValues) {
      return false;
    }
    
    // Dynamically verify by checking if any option has a value in this layer
    for (const auto& [hash, optionPtr] : RtxOptionImpl::getGlobalOptionMap()) {
      if (optionPtr->hasValueInLayer(this)) {
        m_hasValues = true;
        return true;
      }
    }
    
    // No values found - update the cached hint
    m_hasValues = false;
    return false;
  }

  bool RtxOptionLayer::hasUnsavedChanges() const {
    if (!hasSaveableConfigFile()) {
      return false;
    }
    
    // Lazy evaluation - only recalculate if cache is dirty
    if (m_unsavedChangesCacheDirty) {
      recalculateUnsavedChangesInternal();
    }
    
    return m_hasUnsavedChanges;
  }
  
  void RtxOptionLayer::recalculateUnsavedChangesInternal() const {
    m_unsavedChangesCacheDirty = false;
    
    if (!hasSaveableConfigFile()) {
      m_hasUnsavedChanges = false;
      return;
    }
    
    const RtxOptionLayerKey layerKey = getLayerKey();
    
    // Check each option in this layer to see if it differs from saved config
    for (const auto& [optionHash, optionPtr] : RtxOptionImpl::getGlobalOptionMap()) {
      const GenericValue* layerValue = optionPtr->getGenericValue(this);
      if (!layerValue) {
        continue;  // Option not in this layer
      }
      
      const std::string fullName = optionPtr->getFullName();
      const std::string currentValue = optionPtr->genericValueToString(*layerValue);
      
      if (currentValue.empty()) {
        continue;  // Skip empty values
      }
      
      // Check if this option exists in saved config
      if (m_config.findOption(fullName.c_str())) {
        const std::string savedValue = m_config.getOption<std::string>(fullName.c_str(), "");
        
        // For hash sets, use order-independent comparison
        bool valuesEqual;
        if (optionPtr->getType() == OptionType::HashSet && layerValue->hashSet) {
          std::vector<std::string> savedHashStrings = m_config.getOption<std::vector<std::string>>(fullName.c_str());
          HashSetLayer savedHashes;
          savedHashes.parseFromStrings(savedHashStrings);
          valuesEqual = (*layerValue->hashSet == savedHashes);
        } else {
          valuesEqual = (currentValue == savedValue);
        }
        
        if (!valuesEqual) {
          m_hasUnsavedChanges = true;
          return;  // Found a modified value
        }
      } else {
        m_hasUnsavedChanges = true;
        return;  // Found a new value not in saved config
      }
    }
    
    // Also check if there are pending removals (saved config has values not in runtime)
    m_hasUnsavedChanges = hasPendingRemovals();
  }

  bool RtxOptionLayer::hasPendingRemovals() const {
    if (!hasSaveableConfigFile()) {
      return false;
    }
    
    const RtxOptionLayerKey layerKey = getLayerKey();
    
    // Check each option in the saved config to see if it still exists in runtime
    for (const auto& [savedOptionName, savedOptionValue] : m_config.getOptions()) {
      // Skip non-rtx options - they're preserved but not managed by RtxOptionLayer
      if (savedOptionName.find("rtx.") == std::string::npos) {
        continue;
      }
      
      bool existsInRuntime = false;
      
      RtxOptionImpl* optionPtr = RtxOptionImpl::getOptionByFullName(savedOptionName);
      if (optionPtr) {
        const GenericValue* layerValue = optionPtr->getGenericValue(this);
        if (layerValue) {
          const std::string liveValueStr = optionPtr->genericValueToString(*layerValue);
          if (!liveValueStr.empty()) {
            existsInRuntime = true;
          }
        }
      }
      
      if (!existsInRuntime) {
        return true;  // Found an option that would be removed
      }
    }
    
    return false;
  }

  uint32_t RtxOptionLayer::countMiscategorizedOptions() const {
    // Return cached value if still valid
    if (!m_miscategorizedOptionCountDirty) {
      return m_miscategorizedOptionCount;
    }
    
    // Recalculate: count options in this layer that don't belong here based on their flags.
    // This enables the UI to offer migration of misplaced options to the correct layer.
    m_miscategorizedOptionCountDirty = false;
    m_miscategorizedOptionCount = 0;
    
    for (const auto& [hash, optionPtr] : RtxOptionImpl::getGlobalOptionMap()) {
      if (optionPtr->hasValueInLayer(this)) {
        const uint32_t optionFlags = optionPtr->getFlags();
        
        // Only consider layer filter flags when determining layer placement
        // (NoSave, NoReset are orthogonal - they don't affect which layer an option belongs in)
        const uint32_t layerFlags = optionFlags & kRtxOptionCategoryFlags;
        
        if (m_categoryFlags != 0) {
          // This layer is for options WITH specific flags (e.g., user.conf for UserSetting options)
          // Options WITHOUT those flags don't belong here and should migrate out
          if ((layerFlags & m_categoryFlags) == 0) {
            m_miscategorizedOptionCount++;
          }
        } else {
          // This layer is for general developer/modder options (no layer filter flags)
          // Options WITH layer filter flags don't belong here and should migrate to their designated layer
          if (layerFlags != 0) {
            m_miscategorizedOptionCount++;
          }
        }
      }
    }
    
    return m_miscategorizedOptionCount;
  }

  uint32_t RtxOptionLayer::migrateMiscategorizedOptions() {
    uint32_t migratedCount = 0;
    
    for (const auto& [hash, optionPtr] : RtxOptionImpl::getGlobalOptionMap()) {
      if (optionPtr->hasValueInLayer(this)) {
        const uint32_t optionFlags = optionPtr->getFlags();
        
        // Only consider layer filter flags when determining layer placement
        // (NoSave, NoReset are orthogonal - they don't affect which layer an option belongs in)
        const uint32_t layerFlags = optionFlags & kRtxOptionCategoryFlags;
        
        bool shouldMigrate = false;
        if (m_categoryFlags != 0) {
          // This layer is for options WITH specific flags (e.g., user.conf for UserSetting options)
          // Options WITHOUT those flags don't belong here and should migrate out
          shouldMigrate = (layerFlags & m_categoryFlags) == 0;
        } else {
          // This layer is for general developer/modder options (no layer filter flags)
          // Options WITH layer filter flags don't belong here and should migrate to their designated layer
          shouldMigrate = layerFlags != 0;
        }
        
        if (shouldMigrate) {
          // Determine destination layer based on the option's flags
          RtxOptionLayer* destLayer = nullptr;
          if ((layerFlags & RtxOptionFlags::UserSetting) != 0) {
            // UserSetting options belong in the User layer
            destLayer = s_userLayer;
          } else {
            // Developer/modder options belong in rtx.conf
            destLayer = getRtxConfLayer();
          }
          
          if (destLayer && destLayer != this) {
            optionPtr->moveLayerValue(this, destLayer);
            migratedCount++;
          }
        }
      }
    }
    
    return migratedCount;
  }

  bool RtxOptionLayer::save() {
    if (!hasSaveableConfigFile()) {
      Logger::warn(str::format("[RTX Option]: Cannot save layer '", getName(), "' - no associated config file."));
      return false;
    }
    
    // Write all options from this layer into a Config (save all values, not just changed)
    Config layerConfig;
    RtxOptionManager::writeOptions(layerConfig, this, false);
    setConfig(layerConfig);
    
    // Save the config to disk using the layer's stored file path
    Config::serializeCustomConfig(getConfig(), getFilePath(), "rtx.");
    
    // Clear the unsaved changes cache since we just saved
    m_hasUnsavedChanges = false;
    m_unsavedChangesCacheDirty = false;
    
    Logger::info(str::format("[RTX Option]: Saved layer config to '", getFilePath(), "'"));
    return true;
  }

  bool RtxOptionLayer::reload() {
    if (!hasSaveableConfigFile()) {
      Logger::warn(str::format("[RTX Option]: Cannot reload layer '", getName(), "' - no associated config file."));
      return false;
    }

    // Remove current layer values from all options (NoReset options are preserved)
    removeFromAllOptions();

    // Reload the config from disk using the layer's stored file path
    Config reloadedConfig = Config::getOptionLayerConfig(getFilePath());
    setConfig(reloadedConfig);

    // Re-apply the layer values to all options
    if (isValid()) {
      applyToAllOptions();
    }

    // Clear caches and update hasValues hint
    m_hasUnsavedChanges = false;
    m_unsavedChangesCacheDirty = false;
    m_miscategorizedOptionCountDirty = true;  // Will recalculate on next query
    setHasValues(isValid());

    Logger::info(str::format("[RTX Option]: Reloaded layer config from '", getFilePath(), "'"));
    return true;
  }

  bool RtxOptionLayer::exportUnsavedChanges(const std::string& exportPath) const {
    // Check if there are any unsaved changes to export
    if (!hasUnsavedChanges()) {
      Logger::warn(str::format("[RTX Option]: No unsaved changes to export from layer '", getName(), "'."));
      return false;
    }

    // Load existing config from export path (or create empty if file doesn't exist)
    Config exportConfig = Config::getOptionLayerConfig(exportPath);
    const bool isNewFile = exportConfig.getOptions().empty();
    
    // Lambda to process a single option and add it to the export config
    auto processOption = [&exportConfig, this](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
      const std::string fullName = optionPtr->getFullName();
      const OptionType optionType = optionPtr->getType();
      
      // Check if this is a hash set option
      if (optionType == OptionType::HashSet && layerValue->hashSet) {
        // For hash sets, compute only the newly added opinions (delta)
        std::vector<std::string> savedHashStrings = m_config.getOption<std::vector<std::string>>(fullName.c_str());
        HashSetLayer savedHashes;
        savedHashes.parseFromStrings(savedHashStrings);
        
        // Compute added opinions (new positive or negative entries compared to saved config)
        // This includes both newly added positives and newly added negatives
        HashSetLayer addedOpinions = layerValue->hashSet->computeAddedOpinions(savedHashes);
        
        // Only proceed if there are new values to add
        if (!addedOpinions.empty()) {
          // If the option already exists in the export config, merge with it
          if (exportConfig.findOption(fullName.c_str())) {
            std::vector<std::string> existingHashStrings = exportConfig.getOption<std::vector<std::string>>(fullName.c_str());
            HashSetLayer existingHashes;
            existingHashes.parseFromStrings(existingHashStrings);
            
            // Merge: addedOpinions is stronger (overrides conflicts), existingHashes is weaker (fills gaps)
            addedOpinions.mergeFrom(existingHashes);
            exportConfig.setOption(fullName, addedOpinions.toString());
          } else {
            exportConfig.setOption(fullName, addedOpinions.toString());
          }
        }
      } else {
        // For non-hash set options, just use the current value
        const std::string currentValue = optionPtr->genericValueToString(*layerValue);
        if (!currentValue.empty()) {
          exportConfig.setOption(fullName, currentValue);
        }
      }
    };
    
    // Use forEachChange to process all changed options
    forEachChange(
      processOption,  // Added callback
      processOption,  // Modified callback
      nullptr,        // Don't need removed options for export
      nullptr         // Don't need unchanged options for export
    );
    
    // Serialize the config to the export path
    Config::serializeCustomConfig(exportConfig, exportPath, "rtx.");
    
    if (isNewFile) {
      Logger::info(str::format("[RTX Option]: Created new config file with unsaved changes: ", exportPath));
    } else {
      Logger::info(str::format("[RTX Option]: Merged unsaved changes into existing config file: ", exportPath));
    }
    
    return true;
  }

  void RtxOptionLayer::forEachChange(
    const OptionChangeCallback& addedCallback,
    const OptionChangeCallback& modifiedCallback,
    const RemovedOptionCallback& removedCallback,
    const OptionChangeCallback& unchangedCallback
  ) const {
    const bool hasSaveableConfig = hasSaveableConfigFile();
    
    // First pass: iterate through runtime options
    if (addedCallback || modifiedCallback || unchangedCallback) {
      for (const auto& [optionHash, optionPtr] : RtxOptionImpl::getGlobalOptionMap()) {
        const GenericValue* layerValue = optionPtr->getGenericValue(this);
        if (!layerValue) {
          continue;  // Option not in this layer
        }
        
        if (!hasSaveableConfig) {
          // Layer has no saveable config - treat all as "unchanged" (just existing)
          if (unchangedCallback) {
            unchangedCallback(optionPtr, layerValue);
          }
        } else {
          const std::string fullName = optionPtr->getFullName();
          const std::string currentValue = optionPtr->genericValueToString(*layerValue);
          
          if (m_config.findOption(fullName.c_str())) {
            // Option exists in saved config - check if it's modified or unchanged
            const std::string savedValue = m_config.getOption<std::string>(fullName.c_str(), "");
            
            bool valuesEqual;
            if (optionPtr->getType() == OptionType::HashSet && layerValue->hashSet) {
              // For hash sets, use order-independent comparison
              std::vector<std::string> savedHashStrings = m_config.getOption<std::vector<std::string>>(fullName.c_str());
              HashSetLayer savedHashes;
              savedHashes.parseFromStrings(savedHashStrings);
              valuesEqual = (*layerValue->hashSet == savedHashes);
            } else {
              valuesEqual = (currentValue == savedValue);
            }
            
            if (!valuesEqual) {
              if (modifiedCallback) {
                modifiedCallback(optionPtr, layerValue);
              }
            } else {
              if (unchangedCallback) {
                unchangedCallback(optionPtr, layerValue);
              }
            }
          } else {
            // Option doesn't exist in saved config - it's new
            if (addedCallback) {
              addedCallback(optionPtr, layerValue);
            }
          }
        }
      }
    }
    
    // Second pass: find removed options (in saved config but not in runtime)
    // Only consider options that are RtxOptions (exist in the global RtxOption map)
    if (removedCallback && hasSaveableConfig) {
      for (const auto& [savedOptionName, savedOptionValue] : m_config.getOptions()) {
        // Find the corresponding RtxOption - if none exists, this isn't an RtxOption
        // so we shouldn't consider it for removal tracking
        RtxOptionImpl* optionPtr = RtxOptionImpl::getOptionByFullName(savedOptionName);
        if (optionPtr) {
          const GenericValue* layerValue = optionPtr->getGenericValue(this);
          if (!layerValue) {
            // Option exists in saved config but NOT in runtime layer - it was removed
            removedCallback(optionPtr, savedOptionValue);
          }
        }
      }
    }
  }

  std::vector<std::string> RtxOptionLayer::resolveConfigPaths(const char* envVarName, const char* defaultFileName) {
    // Check environment variable (may contain comma-separated paths)
    const std::string envVarPath = env::getEnvVar(envVarName);
    if (!envVarPath.empty()) {
      Logger::info(str::format("Using config paths from ", envVarName, ": ", envVarPath));
      return splitPaths(envVarPath);
    }
    
    // Default: use the file name in current directory
    return { defaultFileName };
  }

  std::vector<RtxOptionLayer*> RtxOptionLayer::createLayersFromEnvVar(
      const char* envVarName,
      const char* defaultFileName,
      const RtxOptionLayerKey& baseLayer) {
    
    std::vector<RtxOptionLayer*> layers;
    // DXVK supports comma separated paths to load multiple files from a single environment variable.
    std::vector<std::string> paths = resolveConfigPaths(envVarName, defaultFileName);
    
    for (size_t i = 0; i < paths.size(); ++i) {
      std::string layerName = makeLayerName(i, paths.size(), std::string(baseLayer.name));
      RtxOptionLayerKey layerKey = { baseLayer.priority, layerName };
      auto registeredLayer = RtxOptionManager::acquireLayer(paths[i], layerKey, 1.0f, 0.1f, true, nullptr);
      if (registeredLayer) {
        layers.push_back(registeredLayer);
      }
    }
    return layers;
  }

  const RtxOptionLayer* RtxOptionLayer::getDefaultLayer() {
    // Default layer must be created lazily because RtxOption static constructors
    // call this during static init (the rest of the system layers are made in initializeSystemLayers())
    if (!s_defaultLayer) {
      Config emptyConfig;
      auto layer = std::make_unique<RtxOptionLayer>(emptyConfig, "", kRtxOptionLayerDefaultKey, 1.0f, 0.1f);
      
      auto& layerMap = RtxOptionManager::getLayerRegistry();
      auto [it, inserted] = layerMap.emplace(layer->getLayerKey(), std::move(layer));
      
      if (inserted) {
        s_defaultLayer = it->second.get();
      }
    }
    return s_defaultLayer;
  }

  const Config& RtxOptionLayer::initializeSystemLayers() {
    Logger::info("Initializing RtxOption system layers...");

    // Helper to add a layer and merge its config into s_mergedConfig
    auto addLayerAndMerge = [](const RtxOptionLayer* layer) {
      if (layer && layer->isValid()) {
        s_mergedConfig.merge(layer->getConfig());
      }
    };

    // ============================================================================
    // Create RtxOption layers from config files
    // Each layer loads its own config file. Layers are created in priority order.
    // 
    // Priority order (lowest to highest - later layers override earlier):
    //   1. dxvk.conf - User's DXVK settings (lowest config file priority)
    //   2. config.cpp - Per-application defaults built into the code (overrides dxvk.conf)
    //   3. rtx.conf - RTX-specific user settings (overrides config.cpp)
    //   4. baseGameMod rtx.conf - Mod-specific RTX settings (if present)
    //   5. quality/environment - Programmatic layers (no config files)
    //   6. user.conf - User settings (highest priority)
    // ============================================================================

    // 1. dxvk.conf layer(s) - may have multiple via DXVK_CONFIG_FILE env var (lowest priority)
    auto dxvkLayers = createLayersFromEnvVar(kRtxOptionDxvkConfEnvVar, kRtxOptionDxvkConfFileName, kRtxOptionLayerDxvkConfKey);
    for (const auto* layer : dxvkLayers) {
      addLayerAndMerge(layer);
    }

    // 2. config.cpp layer - per-application defaults (no file, loaded from code)
    // Check for DXVK_USE_CONF_FOR_EXE to override the exe path (used by tests to simulate different games)
    std::string appExePath = env::getEnvVar(kRtxOptionAppConfigExeEnvVar);
    if (appExePath.empty()) {
      appExePath = env::getExePath();
    }
    Config appConf = Config::getAppConfig(appExePath);
    addLayerAndMerge(RtxOptionManager::acquireLayer("", kRtxOptionLayerConfigCppKey, 1.0f, 0.1f, true, &appConf));

    // 3. rtx.conf layer(s) - may have multiple via DXVK_RTX_CONFIG_FILE env var (overrides config.cpp)
    // The last layer has highest priority and is stored as s_rtxConfLayer
    auto rtxLayers = createLayersFromEnvVar(kRtxOptionRtxConfEnvVar, kRtxOptionRtxConfFileName, kRtxOptionLayerRtxConfKey);
    for (const auto* layer : rtxLayers) {
      addLayerAndMerge(layer);
    }
    if (!rtxLayers.empty()) {
      s_rtxConfLayer = rtxLayers.back();
    }

    // 4. baseGameMod rtx.conf layer - only if mod path is detected (overrides rtx.conf)
    // s_mergedConfig now contains dxvk.conf + config.cpp + rtx.conf settings
    const std::string baseGameModPath = ModManager::getBaseGameModPath(
      s_mergedConfig.getOption<std::string>("rtx.baseGameModRegex", "", ""),
      s_mergedConfig.getOption<std::string>("rtx.baseGameModPathRegex", "", ""));
    
    if (!baseGameModPath.empty()) {
      Logger::info(str::format("Found base game mod path: ", baseGameModPath));
      const std::string rtxModPath = baseGameModPath + "/" + kRtxOptionRtxConfFileName;
      addLayerAndMerge(RtxOptionManager::acquireLayer(rtxModPath, kRtxOptionLayerBaseGameModKey, 1.0f, 0.1f, true, nullptr));
    }

    s_mergedConfig.logOptions("Effective Combined Config for DXVK Options");

    // 5. Programmatic layers without config files (not included in merged config)
    s_derivedLayer = RtxOptionManager::acquireLayer("", kRtxOptionLayerDerivedKey, 1.0f, 0.1f, true, nullptr);
    s_environmentLayer = RtxOptionManager::acquireLayer("", kRtxOptionLayerEnvironmentKey, 1.0f, 0.1f, true, nullptr);
    s_qualityLayer = RtxOptionManager::acquireLayer("", kRtxOptionLayerQualityKey, 1.0f, 0.1f, true, nullptr);

    // 6. user.conf (user settings layer) - highest priority for end-user changes (not included in merged config)
    // User layer is designated for UserSetting options only; other options are miscategorized here.
    s_userLayer = RtxOptionManager::acquireLayer(kRtxOptionUserConfFileName, kRtxOptionLayerUserKey, 1.0f, 0.1f, true, nullptr);
    if (s_userLayer) {
      s_userLayer->setCategoryFlags(RtxOptionFlags::UserSetting);
    }
    
    // Load environment variable overrides into the environment layer
    RtxOptionManager::loadAllEnvironmentVariables();

    Logger::info("RtxOption system layer initialization complete.");

    return s_mergedConfig;
  }

}  // namespace dxvk
