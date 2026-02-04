#include "rtx_imgui.h"
#include "rtx_options.h"
#include <functional>
#include <optional>

namespace RemixGui {

  constexpr float kFixedTooltipWidth = 540; // so the text doesn't spread too wide

  // Popup state - stores what we need to compute display at render time
  static bool s_showPopup = false;
  static dxvk::RtxOptionImpl* s_popupImpl = nullptr;
  static const dxvk::RtxOptionLayer* s_popupTargetLayer = nullptr;
  static std::optional<XXH64_hash_t> s_popupHash;      // For hash-specific blocking (texture sets)
  static std::function<void()> s_onApplyCallback;      // For applying user's intended action after clear
  static ImVec2 s_popupPosition;                       // Position near the widget that triggered the popup
  static const char* s_popupId = "RtxOptionEditWarning";

  bool CheckRtxOptionPopups(dxvk::RtxOptionImpl* impl,
                            std::optional<XXH64_hash_t> hash,
                            std::function<void()> onApplyAction) {
    if (impl == nullptr) {
      return false;
    }

    const dxvk::RtxOptionLayer* targetLayer = impl->getTargetLayer();
    const dxvk::RtxOptionLayer* blockingLayer = impl->getBlockingLayer(targetLayer, hash);
    
    if (blockingLayer) {
      // Check if we're targeting rtx.conf layer and only blocked by derived layer
      const dxvk::RtxOptionLayer* rtxConfLayer = dxvk::RtxOptionLayer::getRtxConfLayer();
      const dxvk::RtxOptionLayer* derivedLayer = dxvk::RtxOptionLayer::getDerivedLayer();
      
      if (targetLayer == rtxConfLayer && blockingLayer == derivedLayer) {
        // Automatically clear the derived layer setting without showing popup
        impl->clearFromStrongerLayers(targetLayer, hash);
        
        // Apply the callback if there is one (for hash set operations)
        if (onApplyAction) {
          onApplyAction();
        }
        
        return false;  // Not blocked (auto-cleared)
      }
      
      // Check if we're in the User Graphics Settings menu and targeting user layer, blocked by quality layer
      const dxvk::RtxOptionLayer* userLayer = dxvk::RtxOptionLayer::getUserLayer();
      const dxvk::RtxOptionLayer* qualityLayer = dxvk::RtxOptionLayer::getQualityLayer();
      const bool isUserMenuOpen = dxvk::RtxOptions::showUI() == dxvk::UIType::Basic;
      
      if (isUserMenuOpen && targetLayer == userLayer && blockingLayer == qualityLayer) {
        // Automatically clear the quality layer setting without showing popup
        // This prevents the popup from hiding the User Graphics Settings menu
        impl->clearFromStrongerLayers(targetLayer, hash);
        
        // Apply the callback if there is one (for hash set operations)
        if (onApplyAction) {
          onApplyAction();
        }
        
        return false;  // Not blocked (auto-cleared)
      }
      
      // Show popup for other blocking scenarios
      s_popupImpl = impl;
      s_popupTargetLayer = targetLayer;
      s_popupHash = hash;
      s_onApplyCallback = std::move(onApplyAction);
      s_popupPosition = ImGui::GetMousePos();  // Capture position near the clicked widget
      s_showPopup = true;
      return true;  // Blocked - popup shown
    }
    return false;  // Not blocked
  }

  void RenderRtxOptionBlockedEditPopup() {
    if (s_showPopup) {
      // Position popup near the widget, but keep it on screen
      ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
      ImVec2 popupSize = ImVec2(420, 0);  // Approximate width
      ImVec2 pos = s_popupPosition;
      
      // Offset slightly so popup doesn't cover the cursor
      pos.x += 10;
      pos.y += 10;
      
      // Clamp to keep popup on screen (with some margin)
      float margin = 20.0f;
      if (pos.x + popupSize.x > viewportSize.x - margin) {
        pos.x = s_popupPosition.x - popupSize.x - 10;  // Flip to left of cursor
      }
      if (pos.y + 200 > viewportSize.y - margin) {  // Estimate popup height
        pos.y = viewportSize.y - 200 - margin;
      }
      if (pos.x < margin) {
        pos.x = margin;
      }
      if (pos.y < margin) {
        pos.y = margin;
      }
      
      ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing);
      ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
      ImGui::OpenPopup(s_popupId);
      s_showPopup = false;
    }

    if (!ImGui::BeginPopupModal(s_popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      return;
    }

    // Determine current state at render time
    bool blockedByPreset = false;
    std::vector<std::string> blockingLayerNames;
    
    if (s_popupImpl) {
      const dxvk::RtxOptionLayer* qualityLayer = dxvk::RtxOptionLayer::getQualityLayer();
      const dxvk::RtxOptionLayerKey targetKey = s_popupTargetLayer 
        ? s_popupTargetLayer->getLayerKey() 
        : dxvk::kRtxOptionLayerDefaultKey;
      
      // Check all layers stronger than target for blocking values
      s_popupImpl->forEachLayerValue([&](const dxvk::RtxOptionLayer* layer, const dxvk::GenericValue&) {
        if (layer->getLayerKey() < targetKey) {
          if (layer == qualityLayer) {
            blockedByPreset = true;
          } else {
            blockingLayerNames.push_back(layer->getName());
          }
        } else {
          return false; // Stop checking layers when they are not stronger than the target layer
        }
        return true; // Continue checking layers
      }, s_popupHash);
    }
    
    bool blockedByOtherLayers = !blockingLayerNames.empty();
    
    // Build message based on current state
    std::string message;
    if (s_popupImpl) {
      std::string optionName = s_popupImpl->getFullName();
      bool isHashSpecific = s_popupHash.has_value();
      
      // For hash-specific messages, indicate we're talking about a texture hash
      std::string itemDesc = isHashSpecific ? "This texture hash" : ("Setting '" + optionName + "'");
      
      // Build layer list string
      std::string layerListStr;
      if (!blockingLayerNames.empty()) {
        layerListStr = "'" + blockingLayerNames[0] + "'";
        if (blockingLayerNames.size() > 1) {
          layerListStr += " (and " + std::to_string(blockingLayerNames.size() - 1) + " other layer";
          if (blockingLayerNames.size() > 2) {
            layerListStr += "s";
          }
          layerListStr += ")";
        }
      }

      message = itemDesc + " cannot be modified.\n\n";

      if (blockedByPreset && blockedByOtherLayers) {
        message += "It is controlled by the Graphics Preset.\nIt is also set in a stronger layer:\n  " + layerListStr + ".\n\n";
      } else if (blockedByPreset) {
        message += "It is controlled by the Graphics Preset.\n\n";
      } else if (blockedByOtherLayers) {
        message += "It is set in a stronger layer:\n  " + layerListStr + "\n\n";
      }

      if (blockedByPreset) {
        message += "'Unblock Option' will:\n"
          "  - Switch the Graphics Preset to Custom\n"
          "  - Move other quality settings to the User layer\n"
          "  - Clear this setting from all stronger layers";
      } else if (blockedByOtherLayers) {
        message += "'Unblock Option' will clear this setting from the above layer(s).";
      }
    }
    
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::Spacing();
    
    // Show per-layer values in a collapsible section
    if (s_popupImpl && ImGui::TreeNode("View per-layer values")) {
      std::string layerValues = FormatOptionLayerValues(s_popupImpl, s_popupHash, true);
      if (!layerValues.empty()) {
        ImGui::TextUnformatted(layerValues.c_str());
      } else {
        ImGui::TextDisabled("No layer values found.");
      }
      ImGui::TreePop();
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Show one action at a time - prioritize preset switch first
    // After switching to Custom, if still blocked by User layer, user will see clear option next time
    float buttonWidth = 140.0f;
    float spacing = 8.0f;
    
    float totalWidth = buttonWidth * 2 + spacing;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - totalWidth) * 0.5f);
    
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
      s_popupImpl = nullptr;
      s_onApplyCallback = nullptr;
      s_popupHash = std::nullopt;
      s_popupTargetLayer = nullptr;
      ImGui::CloseCurrentPopup();
    }
    
    ImGui::SameLine(0, spacing);
    
    if (ImGui::Button("Unblock Option", ImVec2(buttonWidth, 0))) {
      // Clear this specific option from all stronger layers
      if (s_popupImpl) {
        s_popupImpl->clearFromStrongerLayers(s_popupTargetLayer, s_popupHash);
      }
      
      // If blocked by preset, also switch to Custom
      if (blockedByPreset) {
        dxvk::RtxOptionLayerTarget userTarget(dxvk::RtxOptionEditTarget::User);
        dxvk::RtxOptions::graphicsPreset.setDeferred(dxvk::GraphicsPreset::Custom);
      }
      
      // Apply the callback if there is one (for hash set operations)
      if (s_onApplyCallback) {
        s_onApplyCallback();
      }
      
      s_popupImpl = nullptr;
      s_onApplyCallback = nullptr;
      s_popupHash = std::nullopt;
      s_popupTargetLayer = nullptr;
      ImGui::CloseCurrentPopup();
    }
      
    ImGui::EndPopup();
  }

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
