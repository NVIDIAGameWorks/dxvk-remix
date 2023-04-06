#include "dxvk_imgui_extensions.h"

namespace ImGui {

  void ImGui::SetTooltipToLastWidgetOnHover(const char* fmt, ...) {
    if (ImGui::IsItemHovered()) {
      va_list args;
      va_start(args, fmt);
      SetTooltip(fmt, args);
      va_end(args);
    }
  }
}
