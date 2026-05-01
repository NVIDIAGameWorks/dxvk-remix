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
#include "rtx_option_blocking_popup.h"
#include "rtx_options.h"
#include "rtx_imgui.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace RemixGui {

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
        {
          dxvk::RtxOptionLayerTarget userTarget(dxvk::RtxOptionEditTarget::User);
          dxvk::RtxOptions::graphicsPreset.setDeferred(dxvk::GraphicsPreset::Custom);
        }

        // Apply the callback if there is one (for hash set operations)
        if (onApplyAction) {
          onApplyAction();
        }

        return false;  // Not blocked (auto-cleared)
      }

      if (dxvk::RtxOptions::autoUnblockOptionEdits()) {
        impl->clearFromStrongerLayers(targetLayer, hash);
        if (targetLayer != qualityLayer && impl->hasValueInLayer(qualityLayer, hash)) {
          // editing a value that's set by the quality preset, need to change the preset to custom
          dxvk::RtxOptionLayerTarget userTarget(dxvk::RtxOptionEditTarget::User);
          dxvk::RtxOptions::graphicsPreset.setDeferred(dxvk::GraphicsPreset::Custom);
        }
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
    const ImGuiStyle& imStyle = ImGui::GetStyle();
    const char* const buttonLabels[] = { "Cancel", "Unblock Option", "Always Unblock" };
    float buttonWidth = 140.0f;
    for (const char* label : buttonLabels) {
      buttonWidth = std::max(buttonWidth, ImGui::CalcTextSize(label).x + imStyle.FramePadding.x * 2.0f);
    }
    float spacing = 8.0f;

    float totalWidth = buttonWidth * 3.0f + spacing * 2.0f;
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

    ImGui::SameLine(0, spacing);

    if (ImGui::Button("Always Unblock", ImVec2(buttonWidth, 0))) {
      dxvk::RtxOptions::autoUnblockOptionEdits.setDeferred(true);
      if (s_popupImpl) {
        s_popupImpl->clearFromStrongerLayers(s_popupTargetLayer, s_popupHash);
      }
      if (blockedByPreset) {
        dxvk::RtxOptionLayerTarget userTarget(dxvk::RtxOptionEditTarget::User);
        dxvk::RtxOptions::graphicsPreset.setDeferred(dxvk::GraphicsPreset::Custom);
      }
      if (s_onApplyCallback) {
        s_onApplyCallback();
      }
      s_popupImpl = nullptr;
      s_onApplyCallback = nullptr;
      s_popupHash = std::nullopt;
      s_popupTargetLayer = nullptr;
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Same action as 'Unblock Option', but will also remember this decision so you will not be asked again. (stored as rtx.autoUnblockOptionEdits in the User layer)");
    }

    ImGui::EndPopup();
  }

} // namespace RemixGui
