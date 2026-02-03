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

#include "rtx_option_manager.h"
#include "../util/log/log.h"

#include <fstream>
#include <algorithm>

namespace dxvk {

  // ============================================================================
  // RtxOptionManager static method implementations
  // ============================================================================

  fast_unordered_cache<RtxOptionImpl*>& RtxOptionManager::getDirtyOptionMap() {
    // Since other static RtxOptions may try to access the global container on their intialization, 
    // they have to access it via this helper method and the global container has to be defined 
    // as static locally to ensure it is initialized on first use
    static fast_unordered_cache<RtxOptionImpl*> s_dirtyOptions = fast_unordered_cache<RtxOptionImpl*>();
    return s_dirtyOptions;
  }

  RtxOptionManager::RtxOptionLayerMap& RtxOptionManager::getLayerRegistry() {
    static RtxOptionLayerMap s_rtxOptionLayers = RtxOptionLayerMap();
    return s_rtxOptionLayers;
  }

  RtxOptionLayer* RtxOptionManager::getLayer(const RtxOptionLayerKey& layerKey) {
    auto& layerMap = getLayerRegistry();
    auto it = layerMap.find(layerKey);
    if (it != layerMap.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  RtxOptionLayer* RtxOptionManager::acquireLayer(
    const std::string& configPath, const RtxOptionLayerKey& layerKey,
    float blendStrength, float blendThreshold,
    bool isSystemLayer, const Config* config) {
    
    std::lock_guard<std::mutex> lock(s_layerMutex);
    
    auto& layerMap = getLayerRegistry();
    
    // Check if a layer with this priority/name already exists
    auto it = layerMap.find(layerKey);
    if (it != layerMap.end()) {
      // Layer already exists - increment ref count and return
      it->second->incrementRefCount();
      return it->second.get();
    }
    
    const uint32_t priority = layerKey.priority;
    const std::string layerName(layerKey.name);
    const bool isInDynamicRange = (priority >= kMinDynamicRtxOptionLayerPriority && 
                                    priority <= kMaxDynamicRtxOptionLayerPriority);
    
    // Validate/clamp priority based on layer type
    // System layers: must be outside the dynamic range (low priority or reserved high priority)
    // Dynamic layers: clamp into the dynamic range
    uint32_t effectivePriority = priority;
    
    if (isSystemLayer) {
      // System layers should use reserved priorities (outside dynamic range)
      assert(!isInDynamicRange &&
             "System layer priority must be outside the dynamic layer range");
    } else if (!isInDynamicRange) {
      // Dynamic layers: clamp into valid range
      effectivePriority = (priority < kMinDynamicRtxOptionLayerPriority) 
                          ? kMinDynamicRtxOptionLayerPriority 
                          : kMaxDynamicRtxOptionLayerPriority;
      Logger::warn(str::format("[RTX Option]: Priority ", priority, " for '", layerName, 
                               "' is below minimum for user layers. Clamping to ", effectivePriority, "."));
    }
    
    // Create the layer with validated priority
    RtxOptionLayerKey effectiveLayerKey = { effectivePriority, layerName };
    
    std::unique_ptr<RtxOptionLayer> layer;
    if (config) {
      layer = std::make_unique<RtxOptionLayer>(*config, configPath, effectiveLayerKey, blendStrength, blendThreshold);
    } else if (!configPath.empty()) {
      layer = std::make_unique<RtxOptionLayer>(Config::getOptionLayerConfig(configPath), configPath, effectiveLayerKey, blendStrength, blendThreshold);
    } else {
      // Create an empty layer with no file
      layer = std::make_unique<RtxOptionLayer>(Config(), configPath, effectiveLayerKey, blendStrength, blendThreshold);
    }
    
    // Insert into the registry
    RtxOptionLayer* result = layer.get();
    layerMap.emplace(result->getLayerKey(), std::move(layer));
    
    // Increment reference count for the new layer
    result->incrementRefCount();
    
    // Apply the layer to all options if it's enabled
    if (result->isEnabled()) {
      result->applyToAllOptions();
    }
    
    return result;
  }

  bool RtxOptionManager::unregisterLayer(const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      return false;
    }

    auto& layerMap = getLayerRegistry();
    auto it = layerMap.find(layer->getLayerKey());
    if (it != layerMap.end()) {
      // Remove the layer values from all RtxOptions
      // Note: NoReset flag is NOT checked here - when a layer is completely removed,
      // all its values should be removed. NoReset only applies to layer reset/clear operations.
      auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
      for (auto& rtxOptionMapEntry : globalRtxOptions) {
        RtxOptionImpl& rtxOption = *rtxOptionMapEntry.second;
        rtxOption.disableLayerValue(layer);
      }
      
      // Remove from the global layer map
      layerMap.erase(it);
      return true;
    }
    
    return false;
  }

  void RtxOptionManager::applyPendingValues(DxvkDevice* device, bool forceOnChange) {
    // First, process all pending layer changes (blend strength requests, enable/disable)
    {
      std::unique_lock<std::mutex> lock(RtxOptionImpl::getUpdateMutex());

      // Resolve pending requests and apply changes for each layer
      for (auto& [layerKey, optionLayerPtr] : getLayerRegistry()) {
        optionLayerPtr->resolvePendingRequests();
        optionLayerPtr->applyPendingChanges();
      }
    }

    // Then resolve dirty options and invoke callbacks
    constexpr static int32_t maxResolves = 4;
    int32_t numResolves = 0;

    // Iteratively resolve dirty options, invoke callbacks, repeat until none left
    while (numResolves < maxResolves) {
      std::unique_lock<std::mutex> lock(RtxOptionImpl::getUpdateMutex());

      auto& dirtyOptions = getDirtyOptionMap();

      // Need a second array to invoke onChange callbacks after updating values
      std::vector<RtxOptionImpl*> dirtyOptionsVector;
      dirtyOptionsVector.reserve(dirtyOptions.size());
      {
        for (auto& rtxOption : dirtyOptions) {
          const bool valueChanged = rtxOption.second->resolveValue(rtxOption.second->m_resolvedValue, false);
          if (forceOnChange || valueChanged) {
            dirtyOptionsVector.push_back(rtxOption.second);
          }
        }
      }
      dirtyOptions.clear();
      lock.unlock();

      // Invoke onChange callbacks after promoting all values
      for (RtxOptionImpl* rtxOption : dirtyOptionsVector) {
        rtxOption->invokeOnChangeCallback(device);
      }

      numResolves++;

      // If callbacks didn't generate any dirtied options, bail
      if (dirtyOptions.empty()) {
        break;
      }
    }

#if RTX_OPTION_DEBUG_LOGGING
    const bool unresolvedChanges = numResolves == maxResolves && !getDirtyOptionMap().empty();
    if (unresolvedChanges) {
      auto& dirtyOptions = getDirtyOptionMap();
      Logger::warn(str::format("Dirty RtxOptions remaining after ", maxResolves, " passes, suggesting cyclic dependency."));
      for (auto& rtxOption : dirtyOptions) {
        Logger::warn(str::format("- Abandoned resolve of option ", rtxOption.second->m_name));
      }
    }
#endif

    // Don't let dirty options persist across frames
    getDirtyOptionMap().clear();
  }

  void RtxOptionManager::logEffectiveValues() {
    Logger::info("Effective RtxOption values (after all config layers and migrations):");
    
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    for (const auto& [hash, optionPtr] : globalRtxOptions) {
      const RtxOptionImpl& rtxOption = *optionPtr;
      if (!rtxOption.isDefault()) {
        Logger::info(str::format("  ", rtxOption.getFullName(), " = ", rtxOption.getResolvedValueAsString()));
      }
    }
  }

  void RtxOptionManager::markOptionsWithCallbacksDirty() {
    std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
    
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    for (auto& [hash, optionPtr] : globalRtxOptions) {
      RtxOptionImpl& rtxOption = *optionPtr;
      if (rtxOption.m_onChangeCallback) {
        rtxOption.markDirty();
      }
    }
  }

  size_t RtxOptionManager::removeRedundantLayerValues(const RtxOptionLayer* layer) {
    if (!layer) {
      return 0;
    }

    std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
    
    size_t removedCount = 0;
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    
    for (auto& [hash, optionPtr] : globalRtxOptions) {
      RtxOptionImpl& rtxOption = *optionPtr;
      
      if (!rtxOption.hasValueInLayer(layer)) {
        continue;
      }
      
      if (rtxOption.isLayerValueRedundant(layer)) {
        rtxOption.disableLayerValue(layer);
        rtxOption.markDirty();
        removedCount++;
      }
    }
    
    if (removedCount > 0) {
      bool hasRemainingSettings = false;
      for (auto& [hash, optionPtr] : globalRtxOptions) {
        if (optionPtr->hasValueInLayer(layer)) {
          hasRemainingSettings = true;
          break;
        }
      }
      layer->setHasValues(hasRemainingSettings);
      layer->onLayerValueChanged();
    }
    
    return removedCount;
  }

  void RtxOptionManager::writeOptions(Config& options, const RtxOptionLayer* layer, bool changedOptionsOnly) {
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    for (auto& pPair : globalRtxOptions) {
      auto& impl = *pPair.second;
      impl.writeOption(options, layer, changedOptionsOnly);
    }
  }

  void RtxOptionManager::loadAllEnvironmentVariables() {
    const RtxOptionLayer* envLayer = RtxOptionLayer::getEnvironmentLayer();
    if (!envLayer) {
      Logger::warn("[RTX Option]: Failed to get environment layer for loading environment variables.");
      return;
    }
    
    auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();
    bool headerPrinted = false;
    
    for (auto& [hash, optionPtr] : globalRtxOptions) {
      RtxOptionImpl& impl = *optionPtr;
      
      // Try to load the env var; if successful, log it
      std::string envVarValue;
      if (impl.loadFromEnvironmentVariable(envLayer, &envVarValue)) {
        if (!headerPrinted) {
          Logger::info("Loading environment variable overrides:");
          headerPrinted = true;
        }
        Logger::info(str::format("  ", impl.getFullName(), " = ", envVarValue, " (from ", impl.getEnvironmentVariable(), ")"));
      }
    }
  }

  bool RtxOptionManager::writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
    // Open the output file for writing
    std::ofstream outputFile(outputMarkdownFilePath);
    if (!outputFile.is_open()) {
      Logger::err(str::format("[RTX Option]: Failed to open output file ", outputMarkdownFilePath, " for writing"));
      return false;
    }

    // Write out the header for the file
    outputFile <<
      "# RTX Options\n";

    // Add description of Rtx Options with link to RemixConfig.md
    outputFile <<
R"(
This file contains a complete reference of all configurable RTX Options in RTX Remix.

For detailed documentation on the RtxOption system architecture, including layers, priorities, and how values are resolved, see [RemixConfig.md](documentation/RemixConfig.md).

This file is auto-generated by RTX Remix. To regenerate it, run Remix with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1` defined in the environment variables.

)";

    // Helper function to write out a table of RtxOptions for a given value type category
    auto writeOutRtxOptionTable = [&](bool processLongEntryTypes) {
      // Write out a header for a Markdown table
      outputFile << 
        "| RTX Option | Type | Default Value | Min Value | Max Value | Description |\n"
        "| :-- | :-: | :-: | :-: | :-: | :-- |\n"; // Text alignment per column

      // Write out all RTX Options
      auto& globalRtxOptions = RtxOptionImpl::getGlobalOptionMap();

      // Need to sort the options alphabetically by full name.
      std::vector<RtxOptionImpl*> sortedOptions;
      sortedOptions.reserve(globalRtxOptions.size());
      for (const auto& rtxOptionMapEntry : globalRtxOptions) {
        sortedOptions.push_back(rtxOptionMapEntry.second);
      }  
      std::sort(sortedOptions.begin(), sortedOptions.end(), [](RtxOptionImpl* a, RtxOptionImpl* b) {
        return a->getFullName() < b->getFullName();
      });

      for (const RtxOptionImpl* rtxOptionsPtr : sortedOptions) {
        const RtxOptionImpl& rtxOption = *rtxOptionsPtr;

        // Allow processing of short or long value entry categories separately
        {
          bool isLongEntryType = false;

          switch (rtxOption.m_type) {
          case OptionType::HashSet:
          case OptionType::HashVector:
          case OptionType::VirtualKeys:
          case OptionType::String:
            isLongEntryType = true;
            break;
          default:
            break;
          }

          if (isLongEntryType != processLongEntryTypes)
            continue;
        }

        const GenericValue* defaultValue = rtxOption.getGenericValue(RtxOptionLayer::getDefaultLayer());
        std::string defaultValueString = defaultValue ? rtxOption.genericValueToString(*defaultValue) : "";
        std::string minValueString = rtxOption.minValue.has_value() ? rtxOption.genericValueToString(*rtxOption.minValue) : "";
        std::string maxValueString = rtxOption.maxValue.has_value() ? rtxOption.genericValueToString(*rtxOption.maxValue) : "";

        // Write the first portion of the result row to the the outputstream
        outputFile <<
          "|" << rtxOption.getFullName() <<
          "|" << rtxOption.getTypeString() <<
          "|" << defaultValueString <<
          "|" << minValueString <<
          "|" << maxValueString <<
          "|";

        // Preprocess option description for Markdown
        // Note: This is needed as often in our description strings we use various characters which need to be handled
        // when translated to markdown to avoid breaking formatting.

        for (const char* descriptionIterator = rtxOption.m_description; ; ++descriptionIterator) {
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

  std::mutex RtxOptionManager::s_layerMutex;

  void RtxOptionManager::releaseLayer(const RtxOptionLayer* layer) {
    if (layer == nullptr) {
      return;
    }
    
    std::lock_guard<std::mutex> lock(s_layerMutex);
    
    // Find the layer in the registry to ensure it exists
    auto& layerMap = getLayerRegistry();
    auto it = layerMap.find(layer->getLayerKey());
    if (it == layerMap.end()) {
      Logger::warn(str::format("RtxOptionManager: Attempted to release unknown layer '", layer->getLayerKey(), "."));
      return;
    }
    
    if (layer->getRefCount() == 0) {
      Logger::warn(str::format("RtxOptionManager: Layer '", layer->getLayerKey(), "' already has zero references."));
      return;
    }
    
    layer->decrementRefCount();
    
    // If reference count reached zero, remove the layer
    if (layer->getRefCount() == 0) {
      unregisterLayer(layer);
    }
  }

}  // namespace dxvk

// C interface for documentation generation - exported for unit testing
bool writeMarkdownDocumentation(const char* outputMarkdownFilePath) {
  return dxvk::RtxOptionManager::writeMarkdownDocumentation(outputMarkdownFilePath);
}
