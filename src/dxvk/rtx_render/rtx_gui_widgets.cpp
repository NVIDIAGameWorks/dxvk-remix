#include "rtx_gui_widgets.h"
#include "rtx_imgui.h"

namespace ImGui {
  inline float GetFormLabelColumnWidth(float fallback_pixels = 7.0f * GetFontSize()) {
    ImGuiWindow* w = GetCurrentWindow();
    if (!w) {
      return fallback_pixels;
    } else {
      ImGuiContext& g = *GImGui;
      const ImGuiStyle& style = g.Style;

      // Remaining horizontal space on THIS line (accounts for SameLine/columns/tables/WorkRect edits).
      const float avail = GetContentRegionAvail().x;
      if (avail <= 0.0f) {
        return ImMax(1.0f, fallback_pixels);
      } else {
        // If we are on the same visual line as a previous item, prefer a tighter split.
        const bool sameLine = (w->DC.CursorPos.y == w->DC.CursorPosPrevLine.y);

        // 50% on fresh rows (form layout), 40% when inline with SameLine().
        const float ratio = sameLine ? 0.40f : 0.50f;
        const float labelW = ImMax(fallback_pixels, ImFloor(avail * ratio));

        return ImMax(1.0f, labelW);
      }
    }
  }

  inline float GetRowFieldWidth() {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); 
    return ImMax(1.0f, w->WorkRect.Max.x - w->DC.CursorPos.x - GImGui->Style.FramePadding.x);
  }

  // Draw a left-column label clipped to [cursor.x, cursor.x + colW], keep cursor ready for the field.
  inline void ItemLabelLeftClipped(const char* label, float colW) {
    ImGuiWindow* window = GetCurrentWindow();
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const ImVec2 start = ImVec2(window->DC.CursorPos.x + style.FramePadding.x, window->DC.CursorPos.y + style.FramePadding.y);
    const float h = GetTextLineHeight() + style.FramePadding.y;

    const ImRect label_bb(start, ImVec2(start.x + colW, start.y + h));
    ItemSize(label_bb, style.FramePadding.y);

    if (label && label[0] && ItemAdd(label_bb, window->GetID(label))) {
      ImRect clipped_bb = label_bb;
      clipped_bb.Min.y += window->DC.CurrLineTextBaseOffset;
      clipped_bb.Max.y += window->DC.CurrLineTextBaseOffset;

      RenderTextEllipsis(GetWindowDrawList(), clipped_bb.Min, clipped_bb.Max, clipped_bb.Max.x, clipped_bb.Max.x, label, nullptr, nullptr);

      if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        SetTooltip("%s", label);
      }
    }

    window->DC.CursorPos = ImVec2(label_bb.Max.x, start.y - style.FramePadding.y);
  }
}

namespace RemixGui {
  using namespace ImGui;

  struct FieldRow {
    ImGuiWindow* window;
    ImVec2 rowStart;
  };

  static inline bool shouldSkip() {
    ImGuiWindow* window = GetCurrentWindow();
    return window->SkipItems;
  }

  static inline FieldRow beginFieldRow(const char* label) {
    ImGuiWindow* window = GetCurrentWindow();
    FieldRow fr;
    fr.window = window;
    fr.rowStart = window->DC.CursorPos;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const float labelCol = ImGui::GetFormLabelColumnWidth();
    ImGui::ItemLabelLeftClipped(label, labelCol);

    // Add horizontal padding between the label column and the field.
    window->DC.CursorPos.x += style.ItemInnerSpacing.x;

    return fr;
  }

  static inline void endFieldRow(const FieldRow& fr) {
    const float rowH = GetFrameHeight();
    const float endY = ImMax(fr.window->DC.CursorPos.y, fr.rowStart.y + rowH);
    fr.window->DC.CursorPos = ImVec2(fr.rowStart.x, endY);
  }

  template <typename F>
  static inline bool withLabeledRow(const char* label, F&& fn) {
    if (shouldSkip()) {
      return false;
    } else {
      FieldRow fr = beginFieldRow(label);
      PushID(label);
      const bool changed = fn();
      PopID();
      endFieldRow(fr);
      return changed;
    }
  }

  static inline void endFieldRowFromLastItem(const FieldRow& fr) {
    ImVec2 minP = GetItemRectMin();
    ImVec2 maxP = GetItemRectMax();

    const float rowBaseline = fr.rowStart.y + GetFrameHeight();
    const float endY = ImMax(maxP.y, rowBaseline);
    fr.window->DC.CursorPos = ImVec2(fr.rowStart.x, endY);
  }

  static inline void renderFilledOverlay(const ImRect& frameBb, float t, float rounding, float alpha) {
    ImRect fill(frameBb.Min, frameBb.Max);
    fill.Max.x = frameBb.Min.x + (frameBb.GetWidth() * t);
    ImVec4 base = GetStyle().Colors[ImGuiCol_TabActive];
    base.w = ImClamp(alpha, 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(frameBb.Min, frameBb.Max, true);
    dl->AddRectFilled(fill.Min, fill.Max, ImGui::GetColorU32(base), rounding);
    dl->PopClipRect();
  }

  static inline float computeNormalizedValue(ImGuiDataType type, const void* pData, const void* pMin, const void* pMax) {
    float t = 0.0f;
    if (type == ImGuiDataType_Float) {
      const float v = *(const float*) pData;
      const float vmin = *(const float*) pMin;
      const float vmax = *(const float*) pMax;
      if (vmax != vmin) {
        t = (v - vmin) / (vmax - vmin);
      }
    } else if (type == ImGuiDataType_S32) {
      const int v = *(const int*) pData;
      const int vmin = *(const int*) pMin;
      const int vmax = *(const int*) pMax;
      if (vmax != vmin) {
        t = float(v - vmin) / float(vmax - vmin);
      }
    }
    return ImClamp(t, 0.0f, 1.0f);
  }

  static bool SliderScalar_NoGrabOverlay(const char* label, ImGuiDataType type, void* p_data, const void* p_min, const void* p_max, const char* fmt, ImGuiSliderFlags flags, float overlayAlpha) {
    return withLabeledRow(label, [&]() {
      if (!fmt) {
        fmt = ImGui::DataTypeGetInfo(type)->PrintFmt;
      }

      SetNextItemWidth(GetRowFieldWidth());

      ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
      const bool changed = ImGui::SliderScalar("##v", type, p_data, p_min, p_max, fmt, flags);
      ImGui::PopStyleColor(2);

      ImVec2 minP = GetItemRectMin();
      ImVec2 maxP = GetItemRectMax();

      const ImRect frameBb(minP, maxP);

      const ImGuiStyle& style = GetStyle();
      const float t = computeNormalizedValue(type, p_data, p_min, p_max);
      renderFilledOverlay(frameBb, t, style.FrameRounding, overlayAlpha);

      ImGuiContext& g = *GImGui;
      char valueBuf[64];
      const char* valueEnd = valueBuf + ImGui::DataTypeFormatString(valueBuf, IM_ARRAYSIZE(valueBuf), type, p_data, fmt);
      if (g.LogEnabled) {
        ImGui::LogSetNextTextDecoration("{", "}");
      }
      ImGui::RenderTextClipped(frameBb.Min, frameBb.Max, valueBuf, valueEnd, nullptr, ImVec2(0.5f, 0.5f));

      return changed;
    });
  }

  static bool SliderScalarN_NoGrabOverlay(const char* label, ImGuiDataType type, void* v, int components, const void* v_min, const void* v_max, const char* fmt, ImGuiSliderFlags flags, float overlayAlpha) {
    return withLabeledRow(label, [&]() {
      ImGuiContext& g = *GImGui;
      const ImGuiDataTypeInfo* ti = ImGui::DataTypeGetInfo(type);
      size_t tsize = ti->Size;
      const char* effFmt = fmt ? fmt : ti->PrintFmt;

      bool changed = false;

      PushMultiItemsWidths(components, CalcItemWidth());

      for (int i = 0; i < components; i++) {
        PushID(i);
        if (i > 0) {
          SameLine(0.0f, g.Style.ItemInnerSpacing.x);
        }

        // Width for this component is already set by PushMultiItemsWidths()
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0, 0, 0, 0));
        changed |= ImGui::SliderScalar("##v", type, v, v_min, v_max, effFmt, flags);
        ImGui::PopStyleColor(2);

        ImVec2 minP = GetItemRectMin();
        ImVec2 maxP = GetItemRectMax();
        const ImRect frameBb(minP, maxP);

        const ImGuiStyle& style = GetStyle();
        const float t = computeNormalizedValue(type, v, v_min, v_max);
        renderFilledOverlay(frameBb, t, style.FrameRounding, overlayAlpha);

        {
          char valueBuf[64];
          const char* valueEnd = valueBuf + ImGui::DataTypeFormatString(valueBuf, IM_ARRAYSIZE(valueBuf), type, v, effFmt);
          ImGui::RenderTextClipped(frameBb.Min, frameBb.Max, valueBuf, valueEnd, nullptr, ImVec2(0.5f, 0.5f));
        }

        PopID();
        PopItemWidth();
        v = (void*) ((char*) v + tsize);
      }

      return changed;
    });
  }

  bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalar_NoGrabOverlay(label, ImGuiDataType_Float, v, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_Float, v, 2, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_Float, v, 3, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_Float, v, 4, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalar_NoGrabOverlay(label, ImGuiDataType_S32, v, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_S32, v, 2, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_S32, v, 3, &v_min, &v_max, format, flags, overlayAlpha);
  }
  bool SliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format, ImGuiSliderFlags flags, float overlayAlpha) {
    return SliderScalarN_NoGrabOverlay(label, ImGuiDataType_S32, v, 4, &v_min, &v_max, format, flags, overlayAlpha);
  }

  // Shared core: runs an invisible ImGui::Checkbox, then draws our custom box overlay.
  static inline bool checkboxCore(const char* id, bool* v, float boxScale) {
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    PushStyleColor(ImGuiCol_CheckMark, ImVec4(0, 0, 0, 0));

    const bool changed = ImGui::Checkbox(id, v);

    PopStyleColor(4);
    PopStyleVar(1);

    ImVec2 pMin = GetItemRectMin();
    ImVec2 pMax = GetItemRectMax();
    const ImRect frameBb(pMin, pMax);

    const float boxSz = ImFloor(g.FontSize * boxScale);
    const float y = frameBb.Min.y + (frameBb.GetHeight() - boxSz) * 0.5f;
    const float x = frameBb.Min.x;
    const ImRect boxBb(ImVec2(x, y), ImVec2(x + boxSz, y + boxSz));

    const bool hovered = IsItemHovered();
    const bool held = IsItemActive();

    ImU32 baseCol = GetColorU32(held ? ImGuiCol_FrameBgActive : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
    ImVec4 base = ColorConvertU32ToFloat4(baseCol);
    base.x *= 0.5f;
    base.y *= 0.5f;
    base.z *= 0.5f;

    ImDrawList* dl = GetWindowDrawList();
    dl->AddRectFilled(boxBb.Min, boxBb.Max, GetColorU32(base), style.FrameRounding);

    if (*v) {
      const float pad = ImMax(1.0f, ImFloor(boxSz / 6.0f));
      const ImU32 checkCol = GetColorU32(ImGuiCol_CheckMark);
      const float thickness = ImMax(1.0f, boxSz * 0.08f);
      const ImVec2 a(boxBb.Min.x + pad, (boxBb.Min.y + boxBb.Max.y) * 0.5f);
      const ImVec2 b(a.x + (boxSz - pad * 2) * 0.35f, a.y + (boxSz - pad * 2) * 0.45f);
      const ImVec2 c(boxBb.Max.x - pad, boxBb.Min.y + pad);
      dl->AddLine(a, b, checkCol, thickness);
      dl->AddLine(b, c, checkCol, thickness);
    }

    RenderNavHighlight(frameBb, GetItemID(), ImGuiNavHighlightFlags_TypeThin);
    return changed;
  }

  bool Checkbox(const char* label, bool* v, float boxScale /*=1.f*/) {
    if (shouldSkip()) {
      return false;
    } 

    FieldRow fr = beginFieldRow(label);
    PushID(label);
    BeginGroup();
    const bool changed = checkboxCore("##v", v, boxScale);
    EndGroup();
    PopID();
    endFieldRowFromLastItem(fr);
    return changed;
  }

  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption) {
    RtxOptionUxWrapper wrapper(rtxOption);
    bool value = rtxOption->get();
    bool changed = IMGUI_ADD_TOOLTIP(Checkbox(label, &value, 0.9f), rtxOption->getDescription());
    if (changed) {
      rtxOption->setDeferred(value);
    }
    return changed;
  }

  static void RenderArrowChevron(ImDrawList* draw_list, ImVec2 pos, ImU32 col, ImGuiDir dir, float scale) {
    const float h = draw_list->_Data->FontSize * 1.0f;
    const float r = h * 0.45f * scale;
    const float thickness = ImMax(1.0f, h * 0.08f);

    ImVec2 center = ImVec2(pos.x + h * 0.5f, pos.y + h * 0.5f * scale);

    ImVec2 p1;
    ImVec2 p2;
    ImVec2 p3;

    switch (dir) {
    case ImGuiDir_Up:
    {
      p1 = ImVec2(center.x - r, center.y + r * 0.6f);
      p2 = ImVec2(center.x, center.y - r * 0.6f);
      p3 = ImVec2(center.x + r, center.y + r * 0.6f);
      break;
    }
    case ImGuiDir_Down:
    {
      p1 = ImVec2(center.x - r, center.y - r * 0.6f);
      p2 = ImVec2(center.x, center.y + r * 0.6f);
      p3 = ImVec2(center.x + r, center.y - r * 0.6f);
      break;
    }
    case ImGuiDir_Left:
    {
      p1 = ImVec2(center.x + r * 0.6f, center.y - r);
      p2 = ImVec2(center.x - r * 0.6f, center.y);
      p3 = ImVec2(center.x + r * 0.6f, center.y + r);
      break;
    }
    case ImGuiDir_Right:
    {
      p1 = ImVec2(center.x - r * 0.6f, center.y - r);
      p2 = ImVec2(center.x + r * 0.6f, center.y);
      p3 = ImVec2(center.x - r * 0.6f, center.y + r);
      break;
    }
    default:
    {
      return;
    }
    }

    draw_list->AddLine(p1, p2, col, thickness);
    draw_list->AddLine(p2, p3, col, thickness);
  }

  bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags/* = 0*/) {

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) {
      return false;
    } else {
      ImGuiContext& g = *GImGui;
      const ImGuiStyle& style = g.Style;
      const ImGuiID id = window->GetID(label);

      // Top pad participates in content height
      const float topPad = 8.0f;
      Dummy(ImVec2(0.0f, topPad));

      // Uppercase copy (render-only)
      const char* labelEnd = FindRenderedTextEnd(label);
      const int labelLen = (int) (labelEnd - label);
      ImVector<char> upper;
      upper.resize(labelLen + 1);
      for (int i = 0; i < labelLen; i++) {
        upper[i] = (char) toupper((unsigned char) label[i]);
      }
      upper[labelLen] = '\0';

      // Use core for layout/toggle; hide default visuals later
      PushFont(g.IO.Fonts->Fonts[1]);
      flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
      flags &= ~ImGuiTreeNodeFlags_Framed;

      const bool isOpen = ImGui::TreeNodeBehavior(id, flags, "", "");
      const ImRect frameBb = g.LastItemData.DisplayRect;

      const bool hovered = IsItemHovered();
      const bool held = IsItemActive();
      const bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;

      // Compute mask color to match background correctly
      ImU32 maskCol;
      if (held && hovered) {
        maskCol = GetColorU32(ImGuiCol_HeaderActive);
      } else if (hovered) {
        maskCol = GetColorU32(ImGuiCol_HeaderHovered);
      } else if (selected) {
        maskCol = GetColorU32(ImGuiCol_Header);
      } else {
        maskCol = GetColorU32((window->Flags & ImGuiWindowFlags_ChildWindow) ? ImGuiCol_ChildBg : ImGuiCol_WindowBg);
      }

      // Overpaint the left arrow strip so no bg is visible
      {
        const float leftStripW = g.FontSize + style.FramePadding.x * 2.0f;
        const ImRect maskBb(ImVec2(frameBb.Min.x, frameBb.Min.y),
                            ImVec2(frameBb.Min.x + leftStripW, frameBb.Max.y));
        window->DrawList->AddRectFilled(maskBb.Min, maskBb.Max, maskCol);
      }

      // Custom uppercase label (left)
      {
        const ImVec2 padding(style.FramePadding.x, ImMin(window->DC.CurrLineTextBaseOffset, style.FramePadding.y));
        const ImU32 textCol = GetColorU32(ImGuiCol_Text);
        const ImVec2 textPos(frameBb.Min.x + padding.x, frameBb.Min.y + padding.y);
        RenderText(textPos, upper.Data);
      }

      // Custom chevron (right)
      {
        const ImU32 textCol = GetColorU32(ImGuiCol_Text);
        const float fs = GetFontSize();
        const ImVec2 arrowPos(frameBb.Max.x - fs - style.FramePadding.x,
                              frameBb.Min.y + (frameBb.GetHeight() - fs) * 0.5f);
        RenderArrowChevron(window->DrawList, arrowPos, textCol, isOpen ? ImGuiDir_Down : ImGuiDir_Up, 0.5f);
      }

      PopFont();

      // Separator + spacing (register height so scroll reaches bottom)
      {
        const float halfPadY = style.ItemSpacing.y * 0.5f;
        const float yLine = frameBb.Max.y + halfPadY;
        const float x1 = frameBb.Min.x + style.FramePadding.x;
        const float x2 = frameBb.Max.x - style.FramePadding.x;
        window->DrawList->AddLine(ImVec2(x1, yLine), ImVec2(x2, yLine), GetColorU32(ImGuiCol_Separator), 1.0f);
        ItemSize(ImVec2(0.0f, style.ItemSpacing.y), 0.0f);
      }

      IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Openable | (isOpen ? ImGuiItemStatusFlags_Opened : 0));
      return isOpen;
    }
  }

  bool DragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed, int v_min, int v_max, const char* format, const char* format_max, ImGuiSliderFlags flags) {
    if (shouldSkip()) {
      return false;
    } else {
      FieldRow fr = beginFieldRow(label);

      ImGuiContext& g = *GImGui;
      const ImGuiStyle& style = g.Style;

      const float avail = GetContentRegionAvail().x;
      const float gap = style.ItemInnerSpacing.x;
      float w0 = ImFloor((avail - gap) * 0.5f);
      float w1 = ImMax(1.0f, avail - gap - w0);

      PushID(label);
      BeginGroup();

      SetNextItemWidth(w0);
      int minMin = (v_min >= v_max) ? INT_MIN : v_min;
      int minMax = (v_min >= v_max) ? *v_current_max : ImMin(v_max, *v_current_max);
      ImGuiSliderFlags minFlags = flags | ((minMin == minMax) ? ImGuiSliderFlags_ReadOnly : 0);
      bool valueChanged = ImGui::DragInt("##min", v_current_min, v_speed, minMin, minMax, format, minFlags);

      SameLine(0.0f, gap);

      SetNextItemWidth(w1);
      int maxMin = (v_min >= v_max) ? *v_current_min : ImMax(v_min, *v_current_min);
      int maxMax = (v_min >= v_max) ? INT_MAX : v_max;
      ImGuiSliderFlags maxFlags = flags | ((maxMin == maxMax) ? ImGuiSliderFlags_ReadOnly : 0);
      valueChanged |= ImGui::DragInt("##max", v_current_max, v_speed, maxMin, maxMax, format_max ? format_max : format, maxFlags);

      EndGroup();
      PopID();

      endFieldRowFromLastItem(fr);
      return valueChanged;
    }
  }

  bool InputFloat(const char* label, float* v, float step, float stepFast, const char* format, ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      ImGuiInputTextFlags f = flags | ImGuiInputTextFlags_CharsScientific;
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalar("##v", ImGuiDataType_Float, (void*) v, (void*) (step > 0.0f ? &step : nullptr), (void*) (stepFast > 0.0f ? &stepFast : nullptr), format ? format : "%.3f", f);
    });
  }

  bool InputFloat2(const char* label, float v[2], const char* format, ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_Float, v, 2, nullptr, nullptr, format ? format : "%.3f", flags | ImGuiInputTextFlags_CharsScientific);
    });
  }

  bool InputFloat3(const char* label, float v[3], const char* format, ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_Float, v, 3, nullptr, nullptr, format ? format : "%.3f", flags | ImGuiInputTextFlags_CharsScientific);
    });
  }

  bool InputFloat4(const char* label, float v[4], const char* format, ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_Float, v, 4, nullptr, nullptr, format ? format : "%.3f", flags | ImGuiInputTextFlags_CharsScientific);
    });
  }

  bool InputInt(const char* label, int* v, int step, int stepFast, ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      const char* format = (flags & ImGuiInputTextFlags_CharsHexadecimal) ? "%08X" : "%d";
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalar("##v", ImGuiDataType_S32, (void*) v, (void*) (step > 0 ? &step : nullptr), (void*) (stepFast > 0 ? &stepFast : nullptr), format, flags);
    });
  }

  bool InputInt2(const char* label, int v[2], ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_S32, v, 2, nullptr, nullptr, "%d", flags);
    });
  }

  bool InputInt3(const char* label, int v[3], ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_S32, v, 3, nullptr, nullptr, "%d", flags);
    });
  }

  bool InputInt4(const char* label, int v[4], ImGuiInputTextFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::InputScalarN("##v", ImGuiDataType_S32, v, 4, nullptr, nullptr, "%d", flags);
    });
  }

  bool InputText(const char* label, char* buf, size_t bufSize, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData) {
    IM_ASSERT(!(flags & ImGuiInputTextFlags_Multiline));
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return InputTextEx("##v", nullptr, buf, (int) bufSize, ImVec2(0, 0), flags, callback, userData);
    });
  }

  bool DragFloat(const char* label, float* v, float vSpeed, float vMin, float vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalar("##v", ImGuiDataType_Float, v, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragFloat2(const char* label, float v[2], float vSpeed, float vMin, float vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalarN("##v", ImGuiDataType_Float, v, 2, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragFloat3(const char* label, float v[3], float vSpeed, float vMin, float vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalarN("##v", ImGuiDataType_Float, v, 3, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragFloat4(const char* label, float v[4], float vSpeed, float vMin, float vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      return ImGui::DragScalarN("##v", ImGuiDataType_Float, v, 4, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragInt(const char* label, int* v, float vSpeed, int vMin, int vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalar("##v", ImGuiDataType_S32, v, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragInt2(const char* label, int v[2], float vSpeed, int vMin, int vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalarN("##v", ImGuiDataType_S32, v, 2, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragInt3(const char* label, int v[3], float vSpeed, int vMin, int vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalarN("##v", ImGuiDataType_S32, v, 3, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  bool DragInt4(const char* label, int v[4], float vSpeed, int vMin, int vMax, const char* format, ImGuiSliderFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::DragScalarN("##v", ImGuiDataType_S32, v, 4, vSpeed, &vMin, &vMax, format, flags);
    });
  }

  // Getter for the old Combo() API: const char*[]
  static bool Items_ArrayGetter(void* data, int idx, const char** out_text, const char** out_tooltip) {
    const char* const* items = (const char* const*) data;
    if (out_text) {
      *out_text = items[idx];
    }
    return true;
  }

  // Getter for the old Combo() API: "item1\0item2\0item3\0"
  static bool Items_SingleStringGetter(void* data, int idx, const char** out_text, const char** out_tooltip) {
    const char* items_separated_by_zeros = (const char*) data;
    int items_count = 0;
    const char* p = items_separated_by_zeros;
    while (*p) {
      if (idx == items_count) {
        break;
      }
      p += strlen(p) + 1;
      items_count++;
    }
    if (!*p) {
      return false;
    }
    if (out_text) {
      *out_text = p;
    }
    return true;
  }


  static float CalcMaxPopupHeightFromItemCount(int items_count) {
    ImGuiContext& g = *GImGui;
    if (items_count <= 0) {
      return FLT_MAX;
    }
    return (g.FontSize + g.Style.ItemSpacing.y) * items_count - g.Style.ItemSpacing.y + (g.Style.WindowPadding.y * 2);
  }

  // Old API, prefer using BeginCombo() nowadays if you can.
  bool Combo(const char* label, int* currentItem, bool (*itemsGetter)(void*, int, const char**, const char**), void* data, int itemsCount, int popupMaxHeightInItems) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) {
      return false;
    } else {
      ImGuiContext& g = *GImGui;

      const char* previewValue = nullptr;
      if (*currentItem >= 0 && *currentItem < itemsCount) {
        itemsGetter(data, *currentItem, &previewValue, nullptr);
      }

      if (popupMaxHeightInItems != -1 && !(g.NextWindowData.Flags & ImGuiNextWindowDataFlags_HasSizeConstraint)) {
        SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, CalcMaxPopupHeightFromItemCount(popupMaxHeightInItems)));
      }

      FieldRow fr = beginFieldRow(label);

      PushID(label);
      SetNextItemWidth(GetRowFieldWidth());
      if (!BeginCombo("##v", previewValue, ImGuiComboFlags_None)) {
        PopID();
        endFieldRowFromLastItem(fr);
        return false;
      }

      bool valueChanged = false;
      for (int i = 0; i < itemsCount; i++) {
        PushID(i);
        const bool itemSelected = (i == *currentItem);
        const char* itemText;
        const char* itemTooltip;
        if (!itemsGetter(data, i, &itemText, &itemTooltip)) {
          itemText = "*Unknown item*";
        }
        if (Selectable(itemText, itemSelected)) {
          valueChanged = true;
          *currentItem = i;
        }
        if (itemSelected) {
          SetItemDefaultFocus();
        }
        if (itemTooltip && itemTooltip[0] != '\0' && IsItemHovered()) {
          SetTooltipUnformatted(itemTooltip);
        }
        PopID();
      }

      EndCombo();
      PopID();

      if (valueChanged) {
        MarkItemEdited(g.LastItemData.ID);
      }

      endFieldRowFromLastItem(fr);
      return valueChanged;
    }
  }


  bool Combo(const char* label, int* currentItem, const char* const items[], int itemsCount, int heightInItems) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return Combo("##v", currentItem, Items_ArrayGetter, (void*) items, itemsCount, heightInItems);
    });
  }

  // Combo box helper allowing to pass all items in a single string literal holding multiple zero-terminated items "item1\0item2\0"
  bool Combo(const char* label, int* currentItem, const char* itemsSeparatedByZeros, int heightInItems) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      int itemsCount = 0;
      const char* p = itemsSeparatedByZeros;
      while (*p) {
        p += strlen(p) + 1;
        itemsCount++;
      }
      return Combo("##v", currentItem, Items_SingleStringGetter, (void*) itemsSeparatedByZeros, itemsCount, heightInItems);
    });
  }

  void Separator() {
    ImGui::Dummy(ImVec2(0, 12));
  }

  bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::ColorEdit3("##v", col, flags | ImGuiColorEditFlags_NoLabel);
    });
  }

  bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::ColorEdit4("##v", col, flags | ImGuiColorEditFlags_NoLabel);
    });
  }

  bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::ColorPicker3("##v", col, flags | ImGuiColorEditFlags_NoLabel);
    });
  }

  bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags, const float* refCol) {
    return withLabeledRow(label, [&]() {
      SetNextItemWidth(GetRowFieldWidth());
      return ImGui::ColorPicker4("##v", col, flags | ImGuiColorEditFlags_NoLabel, refCol);
    });
  }
}
