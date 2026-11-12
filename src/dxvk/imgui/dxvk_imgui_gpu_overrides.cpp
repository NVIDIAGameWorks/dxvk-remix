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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "imgui.h"
#include "dxvk_imgui.h"
#include "rtx_render/rtx_imgui.h"
#include "dxvk_context.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_gpu_overrides.h"
#include "rtx_render/rtx_option_layer.h"
#include "rtx_render/rtx_option_manager.h"
#include "../util/util_string.h"

namespace dxvk {
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;

  namespace {
    struct GpuOverrideChoice {
      const char* label;
      const char* value;  // nullptr: no override
    };

    constexpr GpuOverrideChoice kGraphicsPresetChoices[] = {
      { "-", nullptr }, { "Ultra", "Ultra" }, { "High", "High" }, { "Medium", "Medium" }, { "Low", "Low" },
    };
    constexpr GpuOverrideChoice kDlssModeChoices[] = {
      { "-", nullptr }, { "Ultra Performance", "UltraPerformance" }, { "Performance", "Performance" },
      { "Balanced", "Balanced" }, { "Quality", "Quality" }, { "Full Resolution", "FullResolution" },
    };
    constexpr GpuOverrideChoice kRayReconstructionChoices[] = {
      { "-", nullptr }, { "On", "True" }, { "Off", "False" },
    };

    struct GpuOverrideColumn {
      const char* header;
      const char* prefix;
      const GpuOverrideChoice* choices;
      size_t choiceCount;
    };

    const GpuOverrideColumn kGpuOverrideColumns[] = {
      { "Graphics Preset", GpuOverrides::kGraphicsPresetPrefix, kGraphicsPresetChoices, std::size(kGraphicsPresetChoices) },
      { "DLSS Mode", GpuOverrides::kDlssModePrefix, kDlssModeChoices, std::size(kDlssModeChoices) },
      { "Ray Reconstruction", GpuOverrides::kRayReconstructionPrefix, kRayReconstructionChoices, std::size(kRayReconstructionChoices) },
    };

    bool equalsIgnoreCase(const std::string& a, const char* b) {
      return a.size() == strlen(b) &&
             std::equal(a.begin(), a.end(), b, [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
             });
    }

    // Edits apply at the end of the frame; show them until the resolved value catches up or they expire.
    struct PendingGpuOverrideEdit {
      std::optional<std::string> value;
      int frame;
    };
    constexpr int kPendingGpuOverrideFrames = 10;
  }

  void ImGUI::showGpuOverrides(const Rc<DxvkContext>& ctx) {
    if (!RemixGui::CollapsingHeader("Per-GPU Overrides", collapsingHeaderClosedFlags)) {
      return;
    }

    static std::set<std::string> s_draftGpuIds;
    static std::map<std::string, PendingGpuOverrideEdit> s_pendingEdits;
    static uint64_t s_rtxConfGeneration = 0;

    const RtxOptionLayer* rtxConfLayer = RtxOptionLayer::getRtxConfLayer();
    // Edits queued before a reload or clear of rtx.conf are dropped, so stop showing them.
    const uint64_t rtxConfGeneration = rtxConfLayer ? rtxConfLayer->getGeneration() : 0;
    if (rtxConfGeneration != s_rtxConfGeneration) {
      s_pendingEdits.clear();
      s_rtxConfGeneration = rtxConfGeneration;
    }

    ImGui::Indent();

    const VkPhysicalDeviceProperties& deviceProperties = ctx->getDevice()->adapter()->devicePropertiesExt().core.properties;
    const std::string currentGpuId = GpuOverrides::formatGpuId(deviceProperties.vendorID, deviceProperties.deviceID);

    ImGui::TextWrapped(
      "Per-GPU defaults saved to the Remix Config layer (rtx.conf). Changes apply the next time the graphics preset is applied, "
      "such as at startup, when the preset changes, or when deleting user settings resets the preset to Auto.");
    ImGui::Text("Current GPU: %s (%s)", deviceProperties.deviceName, currentGpuId.c_str());

    // GPU ID -> whether each column has a value in some layer, including layers below their blend threshold.
    std::map<std::string, std::array<bool, std::size(kGpuOverrideColumns)>> rows;
    // Key -> config text of rtx.conf entries that failed to parse; they have no value but can be replaced or removed here.
    std::map<std::string, std::string> rejectedValues;
    for (size_t column = 0; column < std::size(kGpuOverrideColumns); ++column) {
      const std::string prefix = kGpuOverrideColumns[column].prefix;
      for (const DynamicOptionEntry& entry : RtxOptionManager::enumerateDynamicOptions(prefix, true)) {
        rows[entry.suffix][column] = true;
      }
      if (rtxConfLayer) {
        for (auto& [key, text] : rtxConfLayer->getRejectedEntries(prefix)) {
          rows[key.substr(prefix.size())];
          rejectedValues[key] = std::move(text);
        }
      }
    }
    for (auto it = s_draftGpuIds.begin(); it != s_draftGpuIds.end();) {
      if (rows.count(*it)) {
        it = s_draftGpuIds.erase(it);
      } else {
        rows[*it];
        ++it;
      }
    }

    ImGui::BeginDisabled(rows.count(currentGpuId) > 0);
    if (ImGui::Button("Add Current GPU")) {
      s_draftGpuIds.insert(currentGpuId);
    }
    ImGui::EndDisabled();

    const int frame = ImGui::GetFrameCount();
    auto requestEdit = [&](const std::string& key, const char* value) {
      RtxOptionLayerTarget target(RtxOptionEditTarget::User);
      const DynamicOptionResult result = value ? RtxOptionManager::setDynamicValue(key, value) : RtxOptionManager::clearDynamicValue(key);
      if (result == DynamicOptionResult::Success) {
        s_pendingEdits[key] = { value ? std::optional<std::string>(value) : std::nullopt, frame };
      }
    };

    if (rows.empty()) {
      ImGui::TextDisabled("No per-GPU overrides configured.");
    } else if (ImGui::BeginTable("GpuOverrides", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("GPU");
      for (const GpuOverrideColumn& column : kGpuOverrideColumns) {
        ImGui::TableSetupColumn(column.header);
      }
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableHeadersRow();

      for (const auto& [gpuId, hasValue] : rows) {
        ImGui::PushID(gpuId.c_str());
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (GpuOverrides::isCanonicalGpuId(gpuId)) {
          ImGui::TextUnformatted(gpuId.c_str());
        } else {
          ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", gpuId.c_str());
          RemixGui::SetTooltipToLastWidgetOnHover("Not a GPU ID in the form VVVV_DDDD (uppercase hexadecimal); this entry never matches.");
        }
        if (gpuId == currentGpuId) {
          ImGui::SameLine();
          ImGui::TextDisabled("(current)");
        }

        for (size_t column = 0; column < std::size(kGpuOverrideColumns); ++column) {
          const GpuOverrideColumn& columnInfo = kGpuOverrideColumns[column];
          const std::string key = columnInfo.prefix + gpuId;
          std::optional<std::string> value = GpuOverrides::findActiveValue(key);
          bool inactive = hasValue[column] && !value;
          const auto rejected = rejectedValues.find(key);
          bool invalid = rejected != rejectedValues.end();

          auto pending = s_pendingEdits.find(key);
          if (pending != s_pendingEdits.end()) {
            const bool caughtUp = !invalid && pending->second.value.has_value() == value.has_value() &&
                                  (!value || equalsIgnoreCase(*value, pending->second.value->c_str()));
            if (caughtUp || frame - pending->second.frame > kPendingGpuOverrideFrames) {
              s_pendingEdits.erase(pending);
            } else {
              value = pending->second.value;
              inactive = false;
              invalid = false;
            }
          }

          int selected = value ? -1 : 0;
          for (size_t i = 1; value && i < columnInfo.choiceCount; ++i) {
            if (equalsIgnoreCase(*value, columnInfo.choices[i].value)) {
              selected = static_cast<int>(i);
            }
          }

          ImGui::TableSetColumnIndex(static_cast<int>(column + 1));
          ImGui::SetNextItemWidth(-FLT_MIN);
          const bool unrecognized = selected < 0;
          const bool flagged = invalid || inactive || unrecognized;
          if (flagged) {
            const bool disabledStyle = inactive && !invalid;
            ImGui::PushStyleColor(ImGuiCol_Text, disabledStyle ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
          }
          const std::string comboId = std::string("##") + columnInfo.header;
          const char* preview = invalid ? rejected->second.c_str()
                              : inactive ? "inactive"
                              : unrecognized ? value->c_str()
                              : columnInfo.choices[selected].label;
          const bool open = ImGui::BeginCombo(comboId.c_str(), preview);
          if (flagged) {
            ImGui::PopStyleColor();
            if (invalid) {
              const std::string tooltip = value
                ? str::format("Invalid value in rtx.conf; it is ignored. In effect from another layer: ", *value, ".")
                : std::string("Invalid value in rtx.conf; it is ignored and no override is in effect.");
              RemixGui::SetTooltipToLastWidgetOnHover(tooltip.c_str());
            } else {
              RemixGui::SetTooltipToLastWidgetOnHover(inactive ? "Only set in layers below their blend threshold; no override is in effect."
                                                               : "Unrecognized value; this override is ignored.");
            }
          }
          if (open) {
            for (size_t i = 0; i < columnInfo.choiceCount; ++i) {
              if (ImGui::Selectable(columnInfo.choices[i].label, static_cast<int>(i) == selected)) {
                requestEdit(key, columnInfo.choices[i].value);
              }
            }
            ImGui::EndCombo();
          }

          // Edits go to rtx.conf; a stronger layer such as user.conf keeps its value.
          if (const RtxOptionImpl* option = RtxOptionImpl::getOptionByFullName(key)) {
            if (const RtxOptionLayer* blockingLayer = option->getBlockingLayer(RtxOptionLayer::getRtxConfLayer())) {
              ImGui::TextDisabled("Set in %s", blockingLayer->getName().c_str());
            }
          }
        }

        ImGui::TableSetColumnIndex(static_cast<int>(std::size(kGpuOverrideColumns) + 1));
        if (ImGui::SmallButton("Remove")) {
          for (const GpuOverrideColumn& column : kGpuOverrideColumns) {
            requestEdit(column.prefix + gpuId, nullptr);
          }
          s_draftGpuIds.erase(gpuId);
        }

        ImGui::PopID();
      }

      ImGui::EndTable();
    }

    ImGui::Unindent();
  }
}
