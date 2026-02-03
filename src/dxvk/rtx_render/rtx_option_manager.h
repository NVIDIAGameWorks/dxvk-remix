/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <algorithm>

#include "../util/xxHash/xxhash.h"
#include "../util/util_fast_cache.h"
#include "rtx_option.h"  // For RtxOptionImpl static methods

namespace dxvk {

  // Forward declarations
  class DxvkDevice;
  class Config;

  // Helper function to convert and clamp RtxOptionLayer priority value for components
  inline uint32_t clampComponentLayerPriority(float priorityValue) {
    uint32_t priority = static_cast<uint32_t>(std::round(priorityValue));
    return std::clamp(priority, kMinDynamicRtxOptionLayerPriority, kMaxDynamicRtxOptionLayerPriority);
  }

  // Non-templated helper class for global RtxOption operations
  // Use this for static operations that don't depend on a specific option type
  // This is the central hub for:
  // - Global option registry management
  // - Dirty option tracking
  // - Bulk operations on all options
  // - Serialization (save/load)
  class RtxOptionManager {
  public:
    using RtxOptionMap = std::map<XXH64_hash_t, RtxOptionImpl*>;
    using RtxOptionLayerMap = std::map<RtxOptionLayerKey, std::unique_ptr<RtxOptionLayer>>;

    // ============================================================================
    // Global registry access
    // ============================================================================
    
    // Map of options that have pending value changes
    static fast_unordered_cache<RtxOptionImpl*>& getDirtyOptionMap();
    
    // Global registry of all option layers
    static RtxOptionLayerMap& getLayerRegistry();
    
    // Lookup a layer by its key
    static RtxOptionLayer* getLayer(const RtxOptionLayerKey& layerKey);

    // ============================================================================
    // Layer management
    // ============================================================================
    
    // Acquire a layer by config path and priority. Creates the layer if it doesn't exist.
    // Increments reference count. Each acquire must be matched with a release (for dynamic layers).
    // System layers (isSystemLayer=true) use reserved priorities and are never released.
    // Parameters:
    //   - configPath: Path to config file, or empty for programmatic layers
    //   - layerKey: Priority and name for the layer
    //   - blendStrength/blendThreshold: Blending parameters (default 1.0/0.1)
    //   - isSystemLayer: If true, validates priority is in system range; if false, clamps to dynamic range
    //   - config: Optional pre-loaded config (if null and configPath is set, loads from file)
    static RtxOptionLayer* acquireLayer(
      const std::string& configPath, const RtxOptionLayerKey& layerKey,
      float blendStrength = 1.0f, float blendThreshold = 0.1f,
      bool isSystemLayer = false, const Config* config = nullptr);
    
    // Release a previously acquired layer. Decrements the reference count.
    // When the count reaches zero, the layer is removed from the system.
    // Safe to call with nullptr (no-op).
    static void releaseLayer(const RtxOptionLayer* layer);

    // ============================================================================
    // Serialization
    // ============================================================================
    
    // Write all option values to config
    static void writeOptions(Config& options, const RtxOptionLayer* layer, bool changedOptionsOnly);
    
    // Generate markdown documentation for all options
    static bool writeMarkdownDocumentation(const char* outputMarkdownFilePath);
    
    // Load environment variable overrides for all options
    static void loadAllEnvironmentVariables();

    // ============================================================================
    // Bulk operations on all options
    // ============================================================================

    // Apply all pending set() calls, synchronize dirty option layers, and invoke onChange callbacks
    // Call at end of frame in dxvk-cs thread
    // forceOnChange causes callbacks for all dirty options, even if the resolved value is unchanged
    static void applyPendingValues(DxvkDevice* device, bool forceOnChange);

    // Log all effective (resolved) RtxOption values
    static void logEffectiveValues();

    // Mark all options with onChange callbacks as dirty
    // Call once during initialization after all option layers are loaded
    static void markOptionsWithCallbacksDirty();

    // Remove all redundant values from a layer
    // A value is redundant if lower priority layers would resolve to the same value
    static size_t removeRedundantLayerValues(const RtxOptionLayer* layer);

  private:
    static std::mutex s_layerMutex;
    
    // Remove a layer from the registry and all options (internal use only)
    static bool unregisterLayer(const RtxOptionLayer* layer);
  };

}  // namespace dxvk

// Export function for unit testing
extern "C" __declspec(dllexport) bool writeMarkdownDocumentation(const char* outputMarkdownFilePath);
