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

#include "rtx_option_layer_gui.h"

#include <algorithm>

#include "../imgui/imgui.h"
#include "rtx_option.h"
#include "rtx_options.h"
#include "../util/util_string.h"

namespace dxvk {

  namespace {
    // Check if a string matches the filter (case-insensitive)
    bool matchesFilter(const std::string& text, const std::string& filterLower) {
      if (filterLower.empty()) return true;
      std::string textLower = text;
      std::transform(textLower.begin(), textLower.end(), textLower.begin(), ::tolower);
      return textLower.find(filterLower) != std::string::npos;
    }
    
    // Compare a hash set to a config's stored hash strings (order-independent)
    bool hashSetEqualsConfig(const HashSetLayer& hashSet, const Config& config, const char* optionName) {
      // Use Config's built-in parsing for vector of strings
      std::vector<std::string> hashStrings = config.getOption<std::vector<std::string>>(optionName);
      
      // Convert strings to hashes using the standard function
      HashSetLayer configHashes;
      configHashes.parseFromStrings(hashStrings);
      
      return hashSet == configHashes;
    }
  }

  std::string OptionLayerUI::renderToString(const RtxOptionLayer* layer, const char* layerName) {
    if (!layer || !layer->hasUnsavedChanges()) {
      return str::format("No unsaved changes in ", layerName, ".");
    }
    
    std::string result = str::format("Unsaved changes to save to ", layerName, ":\n\n");
    
    const Config& savedConfig = layer->getConfig();
    
    // Collect items by status for sorting
    std::vector<std::string> addedItems;
    std::vector<std::string> removedItems;
    std::vector<std::string> modifiedItems;
    
    // Use forEachChange to collect items
    layer->forEachChange(
      // Added callback
      [&addedItems](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
        std::string name = optionPtr->getFullName();
        std::string currentValue = optionPtr->genericValueToString(*layerValue);
        addedItems.push_back(str::format("+ ", name, ": ", currentValue, "\n"));
      },
      // Modified callback
      [&modifiedItems, &savedConfig](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
        std::string name = optionPtr->getFullName();
        std::string currentValue = optionPtr->genericValueToString(*layerValue);
        std::string savedValue = savedConfig.getOption<std::string>(name.c_str(), "");
        
        // For hashsets, show the diff
        if (optionPtr->getType() == OptionType::HashSet && layerValue->hashSet) {
          std::vector<std::string> savedHashStrings = savedConfig.getOption<std::vector<std::string>>(name.c_str());
          HashSetLayer savedHashes;
          savedHashes.parseFromStrings(savedHashStrings);
          currentValue = layerValue->hashSet->diffToString(savedHashes);
          savedValue = "";
        }
        
        if (savedValue.empty()) {
          modifiedItems.push_back(str::format("* ", name, ": ", currentValue, "\n"));
        } else {
          modifiedItems.push_back(str::format("* ", name, ": ", savedValue, " -> ", currentValue, "\n"));
        }
      },
      // Removed callback
      [&removedItems](RtxOptionImpl* optionPtr, const std::string& savedValue) {
        std::string name = optionPtr->getFullName();
        removedItems.push_back(str::format("- ", name, ": ", savedValue, " (will be removed)\n"));
      },
      nullptr  // Don't need unchanged for this
    );
    
    // Sort each category alphabetically
    std::sort(addedItems.begin(), addedItems.end());
    std::sort(removedItems.begin(), removedItems.end());
    std::sort(modifiedItems.begin(), modifiedItems.end());
    
    // Output in order: added, removed, modified
    for (const auto& item : addedItems) {
      result += item;
    }
    for (const auto& item : removedItems) {
      result += item;
    }
    for (const auto& item : modifiedItems) {
      result += item;
    }
    
    return result;
  }

  void OptionLayerUI::renderToImGui(const RtxOptionLayer* layer, const RenderOptions& options) {
    if (!layer) {
      return;
    }
    
    // Legend for hashset entries - only shown on hashset options
    static const char* s_hashsetLegend = 
      "\n\n--- Hashset Entry Legend ---\n"
      "+0x...: Hash added to category\n"
      "~0x...: Hash removed from category\n"
      "-0x...: Negative entry (overrides lower layers)\n"
      "+-0x...: New negative entry added\n"
      "~-0x...: Negative entry removed";
    
    // Build a combined list of all items to render for efficient clipping
    struct RenderItem {
      std::string text;
      ImVec4 color;
      const char* tooltip;
      OptionType type;
    };
    
    std::vector<RenderItem> items;
    const Config& savedConfig = layer->getConfig();
    
    // Use forEachChange to build items directly
    layer->forEachChange(
      // Added callback - always shown
      [&items, &options](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
        std::string displayName = optionPtr->getFullName();
        
        // Apply filter
        if (!matchesFilter(displayName, options.filter)) return;
        
        std::string currentValue = optionPtr->genericValueToString(*layerValue);
        const OptionType optionType = optionPtr->getType();
        
        items.push_back({ "+ " + displayName + ": " + currentValue, 
                          ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "New option (not yet saved to config)", optionType });
      },
      
      // Modified callback - always shown
      [&items, &options, &savedConfig](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
        std::string displayName = optionPtr->getFullName();
        
        // Apply filter
        if (!matchesFilter(displayName, options.filter)) return;
        
        std::string currentValue = optionPtr->genericValueToString(*layerValue);
        const OptionType optionType = optionPtr->getType();
        
        // Get saved value for display
        std::string displaySavedValue = savedConfig.getOption<std::string>(displayName.c_str(), "");
        std::string displayCurrentValue = currentValue;
        
        if (optionType == OptionType::HashSet && layerValue->hashSet) {
          // For hashsets, show only the diff
          std::vector<std::string> savedHashStrings = savedConfig.getOption<std::vector<std::string>>(displayName.c_str());
          HashSetLayer savedHashes;
          savedHashes.parseFromStrings(savedHashStrings);
          displayCurrentValue = layerValue->hashSet->diffToString(savedHashes);
          displaySavedValue = "";
        }
        
        std::string text = displaySavedValue.empty() 
          ? "* " + displayName + ": " + displayCurrentValue
          : "* " + displayName + ": " + displaySavedValue + " -> " + displayCurrentValue;
        
        items.push_back({ text,
                          ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "Modified from saved value", optionType });
      },
      
      // Removed callback - always shown
      [&items, &options](RtxOptionImpl* optionPtr, const std::string& savedValue) {
        std::string optionName = optionPtr->getFullName();
        
        // Apply filter
        if (!matchesFilter(optionName, options.filter)) return;
        
        OptionType optionType = optionPtr->getType();
        
        items.push_back({ "- " + optionName + ": " + savedValue + " (will be removed)",
                          ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Will be removed on save", optionType });
      },
      
      // Unchanged callback - only shown if requested
      options.showUnchanged ? RtxOptionLayer::OptionChangeCallback([&items, &options](RtxOptionImpl* optionPtr, const GenericValue* layerValue) {
        std::string displayName = optionPtr->getFullName();
        
        // Apply filter
        if (!matchesFilter(displayName, options.filter)) return;
        
        std::string currentValue = optionPtr->genericValueToString(*layerValue);
        const OptionType optionType = optionPtr->getType();
        
        items.push_back({ displayName + "=" + currentValue,
                          ImGui::GetStyleColorVec4(ImGuiCol_Text), "Unchanged option", optionType });
      }) : RtxOptionLayer::OptionChangeCallback()
    );
    
    // Sort alphabetically - gives order: Modified (*), Added (+), Removed (-), Unchanged (rtx...)
    std::sort(items.begin(), items.end(), [](const RenderItem& a, const RenderItem& b) {
      return a.text < b.text;
    });
    
    if (items.empty()) {
      return;
    }
    
    // Create a child window with bounded height so the clipper knows what's visible.
    // Without this, ImGui considers all items "visible" and renders them all.
    const float maxHeight = 300.0f;
    const float itemHeight = ImGui::GetTextLineHeightWithSpacing();
    // Add extra height for horizontal scrollbar to prevent it from overlapping content
    const float scrollbarSize = ImGui::GetStyle().ScrollbarSize;
    const float contentHeight = itemHeight * items.size() + scrollbarSize;
    
    const float childHeight = std::min(contentHeight, maxHeight);
    
    // Use unique ID if provided, otherwise generate one from the ImGui ID stack
    const char* childId = options.uniqueId ? options.uniqueId : "##LayerStateList";
    ImGui::BeginChild(childId, ImVec2(0, childHeight), false, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Use ImGuiListClipper for efficient rendering of large lists
    // This only renders items that are visible in the scroll area
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(items.size()));
    
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const RenderItem& item = items[i];
        
        ImGui::PushStyleColor(ImGuiCol_Text, item.color);
        ImGui::TextUnformatted(item.text.c_str());
        ImGui::PopStyleColor(1);
        
        if (ImGui::IsItemHovered()) {
          std::string fullTooltip = item.tooltip;
          if (item.type == OptionType::HashSet) {
            fullTooltip += s_hashsetLegend;
          }
          ImGui::SetTooltip("%s", fullTooltip.c_str());
        }
      }
    }
    
    clipper.End();
    ImGui::EndChild();
  }

  void OptionLayerUI::displayContents(const RtxOptionLayer& optionLayer, const std::string& filterLower) {
    // Generate a unique ID based on the layer name
    std::string uniqueId = std::string("##") + optionLayer.getName() + "Contents";
    
    // Render with all options visible (including unchanged)
    RenderOptions renderOptions;
    renderOptions.showUnchanged = true;
    renderOptions.uniqueId = uniqueId.c_str();
    renderOptions.filter = filterLower;
    
    renderToImGui(&optionLayer, renderOptions);
  }

  bool OptionLayerUI::renderLayerButtons(RtxOptionLayer* layer, const char* idSuffix, float buttonWidth) {
    if (!layer) return false;
    
    bool anyClicked = false;
    const bool hasUnsaved = layer->hasUnsavedChanges();
    
    // Calculate button width if not provided
    if (buttonWidth <= 0) {
      buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    }
    
    // Save button (disabled if no unsaved changes)
    ImGui::BeginDisabled(!hasUnsaved);
    const std::string saveId = std::string("Save##") + idSuffix;
    if (ImGui::Button(saveId.c_str(), ImVec2(buttonWidth, 0))) {
      layer->save();
      anyClicked = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Save unsaved changes to %s.", layer->getFilePath().c_str());
    }
    ImGui::EndDisabled();
    
    ImGui::SameLine();
    
    // Reload button
    const std::string reloadId = std::string("Reload##") + idSuffix;
    if (ImGui::Button(reloadId.c_str(), ImVec2(buttonWidth, 0))) {
      layer->reload();
      anyClicked = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Reload from disk, discarding unsaved changes.");
    }
    
    ImGui::SameLine();
    
    // Reset button
    const std::string resetId = std::string("Reset##") + idSuffix;
    if (ImGui::Button(resetId.c_str(), ImVec2(buttonWidth, 0))) {
      layer->removeFromAllOptions();
      anyClicked = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Clear all settings from this layer, resulting in an empty file if saved.");
    }
    
    ImGui::SameLine();
    
    // Clean button (remove redundant)
    const std::string cleanId = std::string("Clean##") + idSuffix;
    if (ImGui::Button(cleanId.c_str(), ImVec2(buttonWidth, 0))) {
      RtxOptionManager::removeRedundantLayerValues(layer);
      anyClicked = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Remove entries that have no effect. If this layer's value\n"
                        "matches what lower layers already resolve to, it's removed.");
    }
    
    return anyClicked;
  }

} // namespace dxvk

