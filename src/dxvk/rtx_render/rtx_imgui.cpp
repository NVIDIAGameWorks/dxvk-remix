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

    if (!ImGui::IsItemHovered()) {
      return;
    }

    ImGui::SetTooltipUnformatted(text);
  }

  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption) {
    return IMGUI_ADD_TOOLTIP(Checkbox(label, &rtxOption->getValue()), rtxOption->getDescription());
  }
}
