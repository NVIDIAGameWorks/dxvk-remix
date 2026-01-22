#include "rtx_imgui.h"

namespace RemixGui {

  void SetTooltipUnformatted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.31f, 0.31f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.99f, 0.96f, 0.78f, 0.920f));
    ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePreviousTooltip, ImGuiWindowFlags_None);
    ImGui::TextUnformatted(text);
    ImGui::EndTooltip();
    ImGui::PopStyleColor(2);
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
