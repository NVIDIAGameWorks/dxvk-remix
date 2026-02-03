/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace dxvk {

  // ============================================================================
  // RtxOption Flags
  // ============================================================================
  
  // Flags that control RtxOption behavior and layer placement.
  //
  // Some options belong in specific layers based on their flags:
  // - UserSetting: End-user options (graphics quality, preferences) → User or Quality layers
  // - NoSave: Runtime-only options → Derived layer only (never saved to disk)
  // - (no flags): Developer/modder options → rtx.conf or dxvk.conf layers
  //
  // The layer migration system detects when options are in the wrong layer:
  // - UserSetting options in rtx.conf/dxvk.conf → should migrate to user.conf
  // - Non-UserSetting options in user.conf → should migrate to rtx.conf
  //
  // NoSave and NoReset are orthogonal to layer placement and don't affect migration.
  enum RtxOptionFlags
  {
    NoSave = 0x1,       // Runtime-only option - routed to Derived layer, never saved to config files
    NoReset = 0x2,      // Don't reset this option when layer is cleared via UI
    UserSetting = 0x4,  // End-user setting - belongs in User or Quality layers, not in mod configs
  };
  
  // Mask of flags that determine which layer an option belongs in.
  // Options with these flags belong in specific layers (e.g., UserSetting → User layer).
  // Options without these flags are general developer/modder options (→ rtx.conf).
  // Used by RtxOptionLayer::countDisallowedOptions() to detect options in the wrong layer.
  // Note: NoSave and NoReset are NOT included - they don't affect layer placement.
  static constexpr uint32_t kRtxOptionCategoryFlags = RtxOptionFlags::UserSetting;

  // ============================================================================
  // RtxOption Types
  // ============================================================================
  
  // The type of value stored in an RtxOption
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

  // ============================================================================
  // RtxOption Environment Variable Names
  // ============================================================================
  static constexpr const char* kRtxOptionDxvkConfEnvVar = "DXVK_CONFIG_FILE";
  static constexpr const char* kRtxOptionRtxConfEnvVar = "DXVK_RTX_CONFIG_FILE";
  static constexpr const char* kRtxOptionAppConfigExeEnvVar = "DXVK_USE_CONF_FOR_EXE";  // Override exe path for app config matching

  // ============================================================================
  // RtxOption Config File Names
  // ============================================================================
  static constexpr const char* kRtxOptionDxvkConfFileName = "dxvk.conf";
  static constexpr const char* kRtxOptionRtxConfFileName = "rtx.conf";
  static constexpr const char* kRtxOptionUserConfFileName = "user.conf";

  // ============================================================================
  // RtxOptionLayer Priority Constants
  // ============================================================================
  
  // Dynamic layers (component-managed, runtime-created) use priorities in this range.
  // System layers use priorities outside this range (0-99 for low priority, near-max for USER).
  static constexpr uint32_t kMinDynamicRtxOptionLayerPriority = 100;
  // Max value is set to 10,000,000 to ensure no data loss when converting between float and uint32_t
  // in RtxOptionLayerAction. Float has 24 bits of precision, so values up to 2^24 (16,777,216) can be
  // represented exactly. This limit provides ample range for priority values while maintaining precision.
  static constexpr uint32_t kMaxDynamicRtxOptionLayerPriority = 10000000;
  static constexpr uint32_t kDefaultDynamicRtxOptionLayerPriority = 10000;

  // ============================================================================
  // RtxOptionLayer Blend Constants
  // ============================================================================
  
  // Blend strength uses MAX logic, so initialize below valid range [0.0, 1.0]
  static constexpr float kRtxOptionLayerEmptyBlendStrengthRequest = -1.0f;
  // Blend threshold uses MIN logic, so initialize above valid range [0.0, 1.0]
  static constexpr float kRtxOptionLayerEmptyBlendThresholdRequest = 2.0f;

  // ============================================================================
  // RtxOptionLayer Key
  // ============================================================================
  
  // Key type for layer maps and system layer definitions.
  // Multiple layers can share the same priority value and are ordered alphabetically by name.
  struct RtxOptionLayerKey {
    uint32_t priority;
    std::string_view name;
    
    // Comparison operator for map ordering (higher priority first, then alphabetical)
    bool operator<(const RtxOptionLayerKey& other) const {
      if (priority != other.priority) {
        return priority > other.priority;
      }
      return name < other.name;
    }
    
    bool operator==(const RtxOptionLayerKey& other) const {
      return priority == other.priority && name == other.name;
    }
    
    std::string toString() const {
      return "'" + std::string(name) + "' (priority: " + std::to_string(priority) + ")";
    }
    
    friend std::ostream& operator<<(std::ostream& os, const RtxOptionLayerKey& key) {
      return os << "'" << key.name << "' (priority: " << key.priority << ")";
    }
  };
  
  // ============================================================================
  // System Layer Keys
  // ============================================================================
  // Priority determines override order (higher value overrides lower value)
  // System layers defined here should be outside the ranged defined by kMinDynamicRtxOptionLayerPriority and kMaxDynamicRtxOptionLayerPriority.
  // Currently that means 0-99, or 0xFFFFFFFF - 100 to 0xFFFFFFFF.
  
  static constexpr RtxOptionLayerKey kRtxOptionLayerDefaultKey     = { 0,          "Default Values" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerDxvkConfKey    = { 1,          "DXVK Config" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerConfigCppKey   = { 2,          "Hardcoded EXE Config" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerRtxConfKey     = { 3,          "Remix Config" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerBaseGameModKey = { 4,          "baseGameMod Remix Config" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerEnvironmentKey = { 5,          "Environment Variable Overrides" };  // Env vars set initial value, can be overridden by code
  static constexpr RtxOptionLayerKey kRtxOptionLayerDerivedKey     = { 6,          "Derived Settings" };  // OnChange callbacks when no menu is open
  static constexpr RtxOptionLayerKey kRtxOptionLayerUserKey        = { 0xFFFFFFFE, "User Settings" };
  static constexpr RtxOptionLayerKey kRtxOptionLayerQualityKey     = { 0xFFFFFFFF, "Quality Presets" };  // Highest priority when preset is not Custom

}  // namespace dxvk

