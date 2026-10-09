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

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <mutex>
#include <set>
#include <utility>

#include "rtx_gpu_overrides.h"
#include "rtx_dlss.h"
#include "rtx_options.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk {

  namespace {

    std::string s_currentGpuId;

    // -1: no override. Written when presets are applied and read every frame by DLSS.
    std::atomic<int32_t> s_dlssMode { -1 };

    bool equalsIgnoreCase(std::string_view a, std::string_view b) {
      return a.size() == b.size() &&
             std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
             });
    }

    bool parseHex(std::string_view text, uint32_t& value) {
      if (text.empty()) {
        return false;
      }
      const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
      return ec == std::errc() && end == text.data() + text.size();
    }

    // Logs each unrecognized (key, value) pair once.
    void warnUnknownValue(const std::string& key, const std::string& value) {
      static std::mutex s_mutex;
      static std::set<std::pair<std::string, std::string>> s_logged;
      std::lock_guard<std::mutex> lock(s_mutex);
      if (s_logged.emplace(key, value).second) {
        Logger::warn(str::format("[GPU Override] Ignoring unrecognized value '", value, "' for ", key, "."));
      }
    }

    std::optional<std::string> findString(const char* prefix, const std::string& gpuId) {
      if (gpuId.empty()) {
        return std::nullopt;
      }
      return GpuOverrides::findActiveValue(prefix + gpuId);
    }

    constexpr std::pair<GraphicsPreset, const char*> kGraphicsPresetNames[] = {
      { GraphicsPreset::Ultra, "Ultra" },
      { GraphicsPreset::High, "High" },
      { GraphicsPreset::Medium, "Medium" },
      { GraphicsPreset::Low, "Low" },
    };

    constexpr std::pair<DLSSProfile, const char*> kDlssModeNames[] = {
      { DLSSProfile::UltraPerf, "UltraPerformance" },
      { DLSSProfile::MaxPerf, "Performance" },
      { DLSSProfile::Balanced, "Balanced" },
      { DLSSProfile::MaxQuality, "Quality" },
      { DLSSProfile::FullResolution, "FullResolution" },
    };

  }

  void GpuOverrides::registerNamespaces() {
    auto registerNamespace = [](const char* prefix, OptionType type, const char* defaultValue, const char* description) {
      DynamicOptionNamespace ns;
      ns.prefix = prefix;
      ns.type = type;
      ns.defaultValue = defaultValue;
      ns.description = description;
      RtxOptionManager::registerDynamicNamespace(ns);
    };
    registerNamespace(kGraphicsPresetPrefix, OptionType::String, "",
                      "Graphics preset used for this GPU when rtx.graphicsPreset is Auto: Ultra, High, Medium or Low.");
    registerNamespace(kDlssModePrefix, OptionType::String, "",
                      "DLSS mode used for this GPU when rtx.qualityDLSS is Auto: UltraPerformance, Performance, Balanced, Quality or FullResolution.");
    registerNamespace(kRayReconstructionPrefix, OptionType::Bool, "True",
                      "Default for rtx.enableRayReconstruction on this GPU.");
  }

  std::string GpuOverrides::formatGpuId(uint32_t vendorId, uint32_t deviceId) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%04X_%04X", vendorId, deviceId);
    return buffer;
  }

  bool GpuOverrides::isCanonicalGpuId(std::string_view gpuId) {
    const size_t separator = gpuId.find('_');
    if (separator == std::string_view::npos) {
      return false;
    }
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    return parseHex(gpuId.substr(0, separator), vendorId) &&
           parseHex(gpuId.substr(separator + 1), deviceId) &&
           gpuId == formatGpuId(vendorId, deviceId);
  }

  std::optional<GraphicsPreset> GpuOverrides::parseGraphicsPreset(std::string_view name) {
    for (const auto& [preset, presetName] : kGraphicsPresetNames) {
      if (equalsIgnoreCase(name, presetName)) {
        return preset;
      }
    }
    return std::nullopt;
  }

  std::optional<DLSSProfile> GpuOverrides::parseDlssMode(std::string_view name) {
    for (const auto& [profile, profileName] : kDlssModeNames) {
      if (equalsIgnoreCase(name, profileName)) {
        return profile;
      }
    }
    return std::nullopt;
  }

  const char* GpuOverrides::graphicsPresetName(GraphicsPreset preset) {
    for (const auto& [value, name] : kGraphicsPresetNames) {
      if (value == preset) {
        return name;
      }
    }
    return "Unknown";
  }

  const char* GpuOverrides::dlssModeName(DLSSProfile profile) {
    for (const auto& [value, name] : kDlssModeNames) {
      if (value == profile) {
        return name;
      }
    }
    return "Unknown";
  }

  void GpuOverrides::setCurrentGpu(uint32_t vendorId, uint32_t deviceId) {
    s_currentGpuId = formatGpuId(vendorId, deviceId);
  }

  const std::string& GpuOverrides::getCurrentGpuId() {
    return s_currentGpuId;
  }

  std::optional<std::string> GpuOverrides::findActiveValue(const std::string& key) {
    const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(key);
    if (option == nullptr || !option->isDynamic()) {
      return std::nullopt;
    }
    option->tagInvalidationScope();
    const RtxOptionLayer* defaultLayer = RtxOptionLayer::getDefaultLayer();
    const bool isBool = option->getType() == OptionType::Bool;
    std::optional<std::string> value;
    // Visits active layers strongest first under the update mutex.
    option->forEachLayerValue([&](const RtxOptionLayer* layer, const GenericValue& layerValue) {
      if (layer == defaultLayer) {
        return true;
      }
      value = isBool ? std::string(layerValue.b ? "True" : "False") : *layerValue.string;
      return false;
    });
    return value;
  }

  std::optional<GraphicsPreset> GpuOverrides::findGraphicsPreset(const std::string& gpuId) {
    const std::optional<std::string> value = findString(kGraphicsPresetPrefix, gpuId);
    if (!value) {
      return std::nullopt;
    }
    const std::optional<GraphicsPreset> preset = parseGraphicsPreset(*value);
    if (!preset) {
      warnUnknownValue(kGraphicsPresetPrefix + gpuId, *value);
    }
    return preset;
  }

  std::optional<DLSSProfile> GpuOverrides::findDlssMode(const std::string& gpuId) {
    const std::optional<std::string> value = findString(kDlssModePrefix, gpuId);
    if (!value) {
      return std::nullopt;
    }
    const std::optional<DLSSProfile> profile = parseDlssMode(*value);
    if (!profile) {
      warnUnknownValue(kDlssModePrefix + gpuId, *value);
    }
    return profile;
  }

  std::optional<bool> GpuOverrides::findRayReconstruction(const std::string& gpuId) {
    const std::optional<std::string> value = findString(kRayReconstructionPrefix, gpuId);
    if (!value) {
      return std::nullopt;
    }
    return *value == "True";
  }

  void GpuOverrides::apply() {
    applyRayReconstructionDefault();
    updateDlssMode();
  }

  void GpuOverrides::updateDlssMode() {
    const std::optional<DLSSProfile> profile = findDlssMode(s_currentGpuId);
    s_dlssMode.store(profile ? static_cast<int32_t>(*profile) : -1, std::memory_order_relaxed);
  }

  std::optional<DLSSProfile> GpuOverrides::getDlssMode() {
    const int32_t profile = s_dlssMode.load(std::memory_order_relaxed);
    if (profile < 0) {
      return std::nullopt;
    }
    return static_cast<DLSSProfile>(profile);
  }

  void GpuOverrides::applyRayReconstructionDefault() {
    RtxOption<bool>& option = RtxOptions::enableRayReconstructionObject();
    const RtxOptionLayer* derivedLayer = RtxOptionLayer::getDerivedLayer();
    const std::optional<bool> value = findRayReconstruction(s_currentGpuId);

    bool setByEnvironment = false;
    {
      std::lock_guard<std::mutex> lock(RtxOptionImpl::getUpdateMutex());
      setByEnvironment = option.hasValueInLayer(RtxOptionLayer::getEnvironmentLayer());
    }

    if (value && !setByEnvironment) {
      Logger::info(str::format("[GPU Override] GPU ", s_currentGpuId, " defaults Ray Reconstruction to ", *value ? "on" : "off", "."));
      option.setImmediately(*value, derivedLayer);
    } else {
      option.clearImmediately(derivedLayer);
    }
  }

  void GpuOverrides::logInvalidEntries() {
    auto checkIds = [](const char* prefix, auto&& checkValue) {
      for (const DynamicOptionEntry& entry : RtxOptionManager::enumerateDynamicOptions(prefix, true)) {
        if (!isCanonicalGpuId(entry.suffix)) {
          Logger::warn(str::format("[GPU Override] ", prefix, entry.suffix,
                                   " is not a GPU ID in the form VVVV_DDDD (uppercase hexadecimal) and will not match."));
        }
        checkValue(entry);
      }
    };
    checkIds(kGraphicsPresetPrefix, [](const DynamicOptionEntry& entry) {
      const std::string key = kGraphicsPresetPrefix + entry.suffix;
      const std::optional<std::string> value = findActiveValue(key);
      if (value && !parseGraphicsPreset(*value)) {
        warnUnknownValue(key, *value);
      }
    });
    checkIds(kDlssModePrefix, [](const DynamicOptionEntry& entry) {
      const std::string key = kDlssModePrefix + entry.suffix;
      const std::optional<std::string> value = findActiveValue(key);
      if (value && !parseDlssMode(*value)) {
        warnUnknownValue(key, *value);
      }
    });
    checkIds(kRayReconstructionPrefix, [](const DynamicOptionEntry&) { });
  }

}
