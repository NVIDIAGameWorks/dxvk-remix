#include "rtx_imgui.h"

namespace ImGui {

  void SetTooltipToLastWidgetOnHover(const char* fmt, ...) {
    if (fmt != "" && ImGui::IsItemHovered()) {
      va_list args;
      va_start(args, fmt);
      ImGui::SetTooltip(fmt, args);
      va_end(args);
    }
  }

  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption) {
    return IMGUI_ADD_TOOLTIP(Checkbox(label, &rtxOption->getValue()), rtxOption->getDescription());
  }
}
