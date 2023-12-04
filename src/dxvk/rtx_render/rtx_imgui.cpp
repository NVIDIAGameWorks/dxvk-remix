#include "rtx_imgui.h"

namespace ImGui {

  void ImGui::SetTooltipUnformatted(const char* text) {
    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, ImGuiWindowFlags_None);
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
  }

  void SetTooltipToLastWidgetOnHover(const char* text) {
    // Note: Don't display tooltips for empty strings, easily detectable if the first character in the string is the null terminator.
    if (text[0] == '\0') {
      return;
    }

    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      return;
    }

    ImGui::SetTooltipUnformatted(text);
  }

  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption) {
    return IMGUI_ADD_TOOLTIP(Checkbox(label, &rtxOption->getValue()), rtxOption->getDescription());
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
  
  // Copied from imgui_widgets.cpp
  static float CalcMaxPopupHeightFromItemCount(int items_count) {
    ImGuiContext& g = *GImGui;
    if (items_count <= 0)
      return FLT_MAX;
    return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
  }

  bool ImGui::Combo(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int height_in_items) {
    const bool value_changed = Combo(label, current_item, Items_PairGetter, (void*) items, items_count, height_in_items);
    return value_changed;
  }

  // Old API, prefer using BeginCombo() nowadays if you can.
  bool ImGui::Combo(const char* label, int* current_item, bool (*items_getter)(void*, int, const char**, const char**), void* data, int items_count, int popup_max_height_in_items) {
    ImGuiContext& g = *GImGui;

    // Call the getter to obtain the preview string which is a parameter to BeginCombo()
    const char* preview_value = NULL;
    if (*current_item >= 0 && *current_item < items_count) {
      items_getter(data, *current_item, &preview_value, nullptr);
    }

    // The old Combo() API exposed "popup_max_height_in_items". The new more general BeginCombo() API doesn't have/need it, but we emulate it here.
    if (popup_max_height_in_items != -1 && !(g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint)) {
      SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, CalcMaxPopupHeightFromItemCount(popup_max_height_in_items)));
    }

    if (!BeginCombo(label, preview_value, ImGuiComboFlags_None)) {
      return false;
    }

    // Display items
    // FIXME-OPT: Use clipper (but we need to disable it on the appearing frame to make sure our call to SetItemDefaultFocus() is processed)
    bool value_changed = false;
    for (int i = 0; i < items_count; i++) {
      PushID(i);
      const bool item_selected = (i == *current_item);
      const char* item_text;
      const char* item_tooltip;
      if (!items_getter(data, i, &item_text, &item_tooltip)) {
        item_text = "*Unknown item*";
      }
      if (Selectable(item_text, item_selected)) {
        value_changed = true;
        *current_item = i;
      }
      if (item_selected) {
        SetItemDefaultFocus();
      }
      if (item_tooltip && item_tooltip[0] != '\0' && ImGui::IsItemHovered()) {
        SetTooltipUnformatted(item_tooltip);
      }
      PopID();
    }

    EndCombo();

    if (value_changed) {
      MarkItemEdited(g.LastItemData.ID);
    }

    return value_changed;
  }
  
  bool ImGui::ListBox(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int height_items) {
    const bool value_changed = ListBox(label, current_item, Items_PairGetter, (void*) items, items_count, height_items);
    return value_changed;
  }

  // This is merely a helper around BeginListBox(), EndListBox().
  // Considering using those directly to submit custom data or store selection differently.
  bool ImGui::ListBox(const char* label, int* current_item, bool (*items_getter)(void*, int, const char**, const char**), void* data, int items_count, int height_in_items) {
    ImGuiContext& g = *GImGui;

    // Calculate size from "height_in_items"
    if (height_in_items < 0) {
      height_in_items = ImMin(items_count, 7);
    }
    float height_in_items_f = height_in_items + 0.25f;
    ImVec2 size(0.0f, ImFloor(GetTextLineHeightWithSpacing() * height_in_items_f + g.Style.FramePadding.y * 2.0f));

    if (!BeginListBox(label, size)) {
      return false;
    }

    // Assume all items have even height (= 1 line of text). If you need items of different height,
    // you can create a custom version of ListBox() in your code without using the clipper.
    bool value_changed = false;
    ImGuiListClipper clipper;
    clipper.Begin(items_count, GetTextLineHeightWithSpacing()); // We know exactly our line height here so we pass it as a minor optimization, but generally you don't need to.
    while (clipper.Step())
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const char* item_text;
        const char* item_tooltip;
        if (!items_getter(data, i, &item_text, &item_tooltip)) {
          item_text = "*Unknown item*";
        }

        PushID(i);
        const bool item_selected = (i == *current_item);
        if (Selectable(item_text, item_selected)) {
          *current_item = i;
          value_changed = true;
        }
        if (item_selected) {
          SetItemDefaultFocus();
        }
        if (item_tooltip && item_tooltip[0] != '\0' && ImGui::IsItemHovered()) {
          SetTooltipUnformatted(item_tooltip);
        }
        PopID();
      }
    EndListBox();

    if (value_changed) {
      MarkItemEdited(g.LastItemData.ID);
    }

    return value_changed;
  }
}
