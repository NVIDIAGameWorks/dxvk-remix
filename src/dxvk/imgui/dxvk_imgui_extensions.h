#pragma once

#include "imgui.h"

namespace ImGui {

#define IMGUI_WITH_TOOLTIP(imguiCommand, tooltip, ...) \
    imguiCommand; \
    if (ImGui::IsItemHovered()) \
      ImGui::SetTooltip(tooltip, ##__VA_ARGS__)

  IMGUI_API void SetTooltipToLastWidgetOnHover(const char* fmt, ...) IM_FMTARGS(1);  // Conditionally sets tooltip if ImGui::IsItemHovered() is true

}
