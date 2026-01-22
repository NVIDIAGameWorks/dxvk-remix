#pragma once

#include <imgui\imgui.h>
#include <imgui\imgui_internal.h>
#include "..\rtx_render\rtx_option.h"
#include "..\util\util_string.h"
#include "..\util\util_vector.h"
#include <type_traits>
#include <utility>

namespace RemixGui {
  // RAII wrapper for per-row UX around an RtxOption<T>.
  // Reserves a right-side button lane so wrapped widgets never overlap it.
  template <typename T>
  struct RtxOptionUxWrapper {
    dxvk::RtxOption<T>* option = nullptr;
    ImGuiWindow* window = nullptr;
    const ImGuiStyle* style = nullptr;

    ImRect rowBb {};
    bool isNonDefault = false;
    float circleRadius = 0.0f;
    ImGuiID id = 0;
    float lineHeight = 0.0f;
    ImVec2 startCursorPos {};

    // right-side lane reservation
    float reservedRightW = 0.0f;
    float prevWorkRectMaxX = 0.0f;
    bool clipPushed = false;
    bool channelsSplit = false;

    // Add this member to the struct:
// bool channelsSplit = false;

    explicit RtxOptionUxWrapper(dxvk::RtxOption<T>* rtxOption)
      : option(rtxOption) {
      window = ImGui::GetCurrentWindow();
      if (!window || window->SkipItems) {
        return;
      } else {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        style = &g.Style;

        const float fontSize = g.FontSize;
        const ImVec2& padding = style->FramePadding;

        startCursorPos = window->DC.CursorPos;
        lineHeight = ImMax(ImMin(window->DC.CurrLineSize.y, fontSize + padding.y * 2.0f), fontSize + padding.y * 2.0f);

        circleRadius = fontSize * 0.40f;
        const float touch = style->TouchExtraPadding.x;
        reservedRightW = padding.x + fontSize + circleRadius + touch + 1.0f;

        prevWorkRectMaxX = window->WorkRect.Max.x;
        window->WorkRect.Max.x = ImMax(window->WorkRect.Min.x, prevWorkRectMaxX - reservedRightW);

        ImVec2 clipMin = ImVec2(window->ClipRect.Min.x, window->ClipRect.Min.y);
        ImVec2 clipMax = ImVec2(window->WorkRect.Max.x, window->ClipRect.Max.y);
        window->DrawList->PushClipRect(clipMin, clipMax, true);
        clipPushed = true;

        isNonDefault = (option->get() != option->getDefaultValue());

        // Split channels so we can paint the background under content and the button later.
        window->DrawList->ChannelsSplit(2);
        window->DrawList->ChannelsSetCurrent(1);
        channelsSplit = true;

        ImGui::PushID((void*) option);
        ImGui::BeginGroup();
      }
    }

    ~RtxOptionUxWrapper() {
      if (!window || window->SkipItems) {
        return;
      } else {
        ImGui::EndGroup();

        // Bounds of children inside the group
        ImVec2 groupMin = ImGui::GetItemRectMin();
        ImVec2 groupMax = ImGui::GetItemRectMax();

        ImGui::PopID();

        // We want the background to include the right reset button lane.
        // Restore clip and layout BEFORE drawing the background so the right lane is included.
        if (clipPushed) {
          window->DrawList->PopClipRect();
          clipPushed = false;
        }
        window->WorkRect.Max.x = prevWorkRectMaxX;

        // Switch to background channel to draw under content.
        if (channelsSplit) {
          window->DrawList->ChannelsSetCurrent(0);
        }

        {
          const float y0 = startCursorPos.y;
          const float y1 = startCursorPos.y + lineHeight;

          const float padX = style->ItemInnerSpacing.x * 0.5f;

          // Left bound hugs the group content with a small inset.
          const float x0 = ImMax(window->WorkRect.Min.x, groupMin.x - padX);

          // Right bound extends to cover the reserved button lane but not beyond the row work area.
          const float x1 = prevWorkRectMaxX;

          if (ImGui::IsMouseHoveringRect(ImVec2(x0, y0), ImVec2(x1, y1))) {
            const ImU32 bg = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
            window->DrawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bg, 0.0f);
          } else if (isNonDefault) {
            const ImU32 bg = ImGui::GetColorU32(ImGuiCol_ChildBg);
            window->DrawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), bg, 0.0f);
          }
        }

        // Merge channels so subsequent draws are normal.
        if (channelsSplit) {
          window->DrawList->ChannelsMerge();
          channelsSplit = false;
        }

        // Draw the reset button on top.
        ImGuiContext& g = *ImGui::GetCurrentContext();
        const float fontSize = g.FontSize;
        const ImVec2& padding = style->FramePadding;

        const float yCenter = startCursorPos.y + lineHeight * 0.5f;
        const ImVec2 circleCenter(prevWorkRectMaxX - padding.x - fontSize, yCenter);

        ImRect hitBb(ImVec2(circleCenter.x - circleRadius, circleCenter.y - circleRadius),
                     ImVec2(circleCenter.x + circleRadius, circleCenter.y + circleRadius));
        hitBb.Expand(style->TouchExtraPadding);

        id = window->GetID((void*) option);

        if (ImGui::ItemAdd(hitBb, id)) {
          bool hovered = false;
          bool held = false;
          bool pressed = ImGui::ButtonBehavior(hitBb, id, &hovered, &held, ImGuiButtonFlags_PressedOnClick);
          if (pressed) {
            option->resetToDefault();
          }

          const ImU32 fill = isNonDefault ? ImGui::GetColorU32((ImU32) 0xFFffc734) : ImGui::GetColorU32((ImU32) 0xFF464646);
          const ImU32 outline = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_Border);

          window->DrawList->AddCircleFilled(circleCenter, circleRadius, fill);
          window->DrawList->AddCircle(circleCenter, circleRadius, outline, 0, 1.0f);

          if (hovered) {
            ImGui::SetTooltip("Reset to default (%s)", option->getName().c_str());
          }
        }
      }
    }

    RtxOptionUxWrapper(const RtxOptionUxWrapper&) = delete;
    RtxOptionUxWrapper& operator=(const RtxOptionUxWrapper&) = delete;
  };

  bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderFloat2(const char* label, float v[2], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderFloat3(const char* label, float v[3], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderFloat4(const char* label, float v[4], float v_min, float v_max, const char* format = "%.3f", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);

  bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderInt2(const char* label, int v[2], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderInt3(const char* label, int v[3], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  bool SliderInt4(const char* label, int v[4], int v_min, int v_max, const char* format = "%d", ImGuiSliderFlags flags = 0, float overlayAlpha = 0.8f);
  
  bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption);
  bool Checkbox(const char* label, bool* v, float boxScale = .9f);

  bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
  bool InputFloat2(const char* label, float v[2], const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
  bool InputFloat3(const char* label, float v[3], const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
  bool InputFloat4(const char* label, float v[4], const char* format = "%.3f", ImGuiInputTextFlags flags = 0);
  bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0);
  bool InputInt2(const char* label, int v[2], ImGuiInputTextFlags flags = 0);
  bool InputInt3(const char* label, int v[3], ImGuiInputTextFlags flags = 0);
  bool InputInt4(const char* label, int v[4], ImGuiInputTextFlags flags = 0);

  bool InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0, ImGuiInputTextCallback callback = NULL, void* user_data = NULL);

  bool DragFloat(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);     // If v_min >= v_max we have no bound
  bool DragFloat2(const char* label, float v[2], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
  bool DragFloat3(const char* label, float v[3], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
  bool DragFloat4(const char* label, float v[4], float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", ImGuiSliderFlags flags = 0);
  bool DragInt(const char* label, int* v, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0);  // If v_min >= v_max we have no bound
  bool DragInt2(const char* label, int v[2], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0);
  bool DragInt3(const char* label, int v[3], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0);
  bool DragInt4(const char* label, int v[4], float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", ImGuiSliderFlags flags = 0);
  bool DragIntRange2(const char* label, int* v_current_min, int* v_current_max, float v_speed = 1.0f, int v_min = 0, int v_max = 0, const char* format = "%d", const char* format_max = NULL, ImGuiSliderFlags flags = 0);
  
  bool Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1);
  bool Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);
  bool Combo(const char* label, int* current_item, bool (*itemsGetter)(void*, int, const char**, const char**), void* data, int items_count, int popup_max_height_in_items = -1);

  bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0);

  void Separator();

  bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
  bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags = 0);
  bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags = 0);
  bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags = 0, const float* ref_col = NULL);

} // namespace remixGui