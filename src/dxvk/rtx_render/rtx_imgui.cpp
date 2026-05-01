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
#include "rtx_imgui.h"
#include <algorithm>
#include <optional>

namespace RemixGui {

  constexpr float kFixedTooltipWidth = 540; // so the text doesn't spread too wide

  void SetTooltipUnformatted(const char* text) {
    // fixed size tooltip for readability
    // -1 to preserve automatic resize on Y
    ImGui::SetNextWindowSizeConstraints(
      ImVec2(kFixedTooltipWidth, -1),
      ImVec2(kFixedTooltipWidth, -1)
    );
    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, ImGuiWindowFlags_None);
    // NOTE: imgui has an optimization for "%s" format string that directly puts 'text' without formatting
    ImGui::TextWrapped("%s", text);
    ImGui::EndTooltip();
  }

  void SetTooltipUnformattedUnwrapped(const char* text) {
    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, ImGuiWindowFlags_None);
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
  }

  bool IsItemHoveredDelay(float delay_in_seconds) {
    return ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && GImGui->HoveredIdTimer > delay_in_seconds;
  }

  void SetTooltipToLastWidgetOnHover(const char* text) {
    // Note: Don't display tooltips for empty strings, easily detectable if the first character in the string is the null terminator.
    if (text[0] == '\0') {
      return;
    }

    if (!IsItemHoveredDelay(0.5f)) {
      return;
    }

    SetTooltipUnformatted(text);
  }

  std::string FormatOptionLayerValues(dxvk::RtxOptionImpl* impl,
                                      std::optional<XXH64_hash_t> hash,
                                      bool includeInactive) {
    if (!impl) {
      return "";
    }

    std::string result;
    const bool isHashSet = (impl->getType() == dxvk::OptionType::HashSet);
    const bool isFloatType = (impl->getType() == dxvk::OptionType::Float ||
                              impl->getType() == dxvk::OptionType::Vector2 ||
                              impl->getType() == dxvk::OptionType::Vector3 ||
                              impl->getType() == dxvk::OptionType::Vector4);

    impl->forEachLayerValue([&](const dxvk::RtxOptionLayer* layer, const dxvk::GenericValue& value) {
      // Build status indicator for inactive or partially blended layers
      std::string statusPrefix;
      if (!layer->isActive()) {
        statusPrefix = "(inactive) ";
      } else if (layer->getBlendStrength() < 1.0f && isFloatType) {
        char buf[32];
        snprintf(buf, sizeof(buf), "(%.0f%%) ", layer->getBlendStrength() * 100.0f);
        statusPrefix = buf;
      }

      std::string valueStr;
      if (isHashSet) {
        const dxvk::HashSetLayer* hashSet = value.hashSet;
        if (!hashSet) {
          return true; // Continue
        }

        if (hash.has_value()) {
          // Show specific hash status
          if (hashSet->hasPositive(*hash)) {
            valueStr = "Added";
          } else if (hashSet->hasNegative(*hash)) {
            valueStr = "Removed";
          } else {
            return true; // This layer has no opinion on this hash, skip
          }
        } else {
          // Show counts
          size_t positiveCount = hashSet->size();
          size_t negativeCount = hashSet->negativeSize();

          if (positiveCount > 0 && negativeCount > 0) {
            valueStr = std::to_string(positiveCount) + " added, " + std::to_string(negativeCount) + " removed";
          } else if (positiveCount > 0) {
            valueStr = std::to_string(positiveCount) + " added";
          } else if (negativeCount > 0) {
            valueStr = std::to_string(negativeCount) + " removed";
          } else {
            return true; // Empty, skip
          }
        }
      } else {
        valueStr = impl->genericValueToString(value);
      }

      result += "  " + statusPrefix + layer->getName() + ": " + valueStr + "\n";
      return true; // Continue
    }, hash, includeInactive);

    return result;
  }

  void TextCentered(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(fmt).x) * 0.5f);
    ImGui::TextV(fmt, args);
    va_end(args);
  }

  void TextWrappedCentered(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(fmt).x) * 0.5f);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);
  }

  // Build a full tooltip for an RtxOption, including layer info and blocking warnings.
  // Non-template implementation to avoid code bloat from template instantiations in the header.
  std::string BuildRtxOptionTooltip(dxvk::RtxOptionImpl* impl) {
    if (!impl) {
      return "";
    }

    std::string result = impl->getDescription() ? impl->getDescription() : "";

    if (!result.empty()) {
      result += "\n\n";
    }
    result += impl->getFullName();

    // Add flag info
    const uint32_t flags = impl->getFlags();
    if (flags != 0) {
      std::string flagInfo;
      if (flags & dxvk::RtxOptionFlags::UserSetting) {
        flagInfo += "UserSetting";
      }
      if (flags & dxvk::RtxOptionFlags::NoSave) {
        if (!flagInfo.empty()) flagInfo += ", ";
        flagInfo += "NoSave";
      }
      if (flags & dxvk::RtxOptionFlags::NoReset) {
        if (!flagInfo.empty()) flagInfo += ", ";
        flagInfo += "NoReset";
      }
      if (!flagInfo.empty()) {
        result += "\n[Flags: " + flagInfo + "]";
      }
    }

    // Get per-layer values using the shared function
    std::string layerInfo = FormatOptionLayerValues(impl, std::nullopt, true);
    if (!layerInfo.empty()) {
      if (!result.empty()) {
        result += "\n\n";
      }
      result += "Values by layer:\n" + layerInfo;
    }

    // Check for blocking layers
    const dxvk::RtxOptionLayer* targetLayer = impl->getTargetLayer();
    if (targetLayer) {
      const dxvk::RtxOptionLayerKey& targetKey = targetLayer->getLayerKey();
      std::string blockingLayers;
      
      impl->forEachLayerValue([&](const dxvk::RtxOptionLayer* layer, const dxvk::GenericValue&) {
        if (layer->getLayerKey() < targetKey) {
          if (!blockingLayers.empty()) {
            blockingLayers += ", ";
          }
          blockingLayers += layer->getName();
        }
        return true;
      }, std::nullopt, true);
      
      if (!blockingLayers.empty()) {
        result += "\n[!] Editing blocked by: " + blockingLayers;
      }
    }

    return result;
  }

  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption) {
    IMGUI_RTXOPTION_WIDGET(Checkbox(label, &value, 0.9f))
  }

  static bool Items_PairGetter(void* data, int idx, const char** out_text, const char** out_tooltip) {
    std::pair<const char*, const char*>* items = reinterpret_cast<std::pair<const char*, const char*>*>(data);
    if (out_text) {
      *out_text = items[idx].first;
    }
    if (out_tooltip) {
      *out_tooltip = items[idx].second;
    }
    return true;
  }
  
  bool ListBox(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int height_items) {
    const bool value_changed = ListBox(label, current_item, Items_PairGetter, (void*) items, items_count, height_items);
    return value_changed;
  }

  // This is merely a helper around BeginListBox(), EndListBox().
  // Considering using those directly to submit custom data or store selection differently.
  bool ListBox(const char* label, int* current_item, bool (*items_getter)(void*, int, const char**, const char**), void* data, int items_count, int height_in_items) {
    ImGuiContext& g = *GImGui;

    // Calculate size from "height_in_items"
    if (height_in_items < 0) {
      height_in_items = ImMin(items_count, 7);
    }
    float height_in_items_f = height_in_items + 0.25f;
    ImVec2 size(0.0f, ImFloor(ImGui::GetTextLineHeightWithSpacing() * height_in_items_f + g.Style.FramePadding.y * 2.0f));

    if (!ImGui::BeginListBox(label, size)) {
      return false;
    }

    // Assume all items have even height (= 1 line of text). If you need items of different height,
    // you can create a custom version of ListBox() in your code without using the clipper.
    bool value_changed = false;
    ImGuiListClipper clipper;
    clipper.Begin(items_count, ImGui::GetTextLineHeightWithSpacing()); // We know exactly our line height here so we pass it as a minor optimization, but generally you don't need to.
    while (clipper.Step())
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const char* item_text;
        const char* item_tooltip;
        if (!items_getter(data, i, &item_text, &item_tooltip)) {
          item_text = "*Unknown item*";
        }

        ImGui::PushID(i);
        const bool item_selected = (i == *current_item);
        if (ImGui::Selectable(item_text, item_selected)) {
          *current_item = i;
          value_changed = true;
        }
        if (item_selected) {
          ImGui::SetItemDefaultFocus();
        }
        if (item_tooltip && item_tooltip[0] != '\0' && ImGui::IsItemHovered()) {
          SetTooltipUnformatted(item_tooltip);
        }
        ImGui::PopID();
      }
    ImGui::EndListBox();

    if (value_changed) {
      ImGui::MarkItemEdited(g.LastItemData.ID);
    }

    return value_changed;
  }
}
