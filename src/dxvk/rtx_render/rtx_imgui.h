#pragma once

#include <imgui\imgui.h>
#include <imgui\imgui_internal.h>
#include "..\rtx_render\rtx_option.h"
#include "..\util\util_vector.h"
#include <type_traits>
#include <utility>

namespace ImGui {

  IMGUI_API void          SetTooltipUnformatted(const char* text);  // Same as SetTooltip, just without text formatting (so percentage signs do not interfere with tooltips when not desired).
  IMGUI_API void SetTooltipToLastWidgetOnHover(const char* text);  // Conditionally sets tooltip if IsItemHovered() is true

  template<typename T>
  inline T addTooltipAndPassthroughValue(const T& value, const char* tooltip) {
    SetTooltipToLastWidgetOnHover(tooltip);
    return value;
  }

  template <typename Tin, typename Tout,
            std::enable_if_t<
              (std::is_same_v<Tin, uint8_t> || std::is_same_v<Tin, uint16_t> || std::is_same_v<Tin, uint32_t> ||
               std::is_same_v<Tin, int8_t> || std::is_same_v<Tin, int16_t> || std::is_same_v<Tin, int32_t> ||
               std::is_same_v<Tin, bool> || std::is_same_v<Tin, char> || std::is_same_v<Tin, size_t>) &&
              (std::is_same_v<Tout, uint8_t> || std::is_same_v<Tout, uint16_t> || std::is_same_v<Tout, uint32_t> ||
               std::is_same_v<Tout, int8_t> || std::is_same_v<Tout, int16_t> || std::is_same_v<Tout, int32_t> ||
               std::is_same_v<Tout, bool> || std::is_same_v<Tout, char> || std::is_same_v<Tout, size_t>), bool> = true>
  Tout safeConvertIntegral(const Tin& v) {
    if constexpr (std::is_same_v<Tin, Tout>)
      return v;
    else
      // Convert to a larger signed integer before checking the limits to ensure correctness
      return static_cast<Tout>(std::clamp(
        static_cast<int64_t>(v),
        static_cast<int64_t>(std::numeric_limits<Tout>::min()),
        static_cast<int64_t>(std::numeric_limits<Tout>::max())));
  }

  // Adds a tooltip to the imguiCommand and returns boolean result from the passed in imguiCommand
#define IMGUI_ADD_TOOLTIP(imguiCommand, tooltip) ImGui::addTooltipAndPassthroughValue((imguiCommand), tooltip)

  IMGUI_API bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption);
  IMGUI_API bool Combo(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int popup_max_height_in_items = -1);
  IMGUI_API bool Combo(const char* label, int* current_item, bool(*items_getter)(void* data, int idx, const char** out_text, const char** out_tooltip), void* data, int items_count, int popup_max_height_in_items = -1);
  IMGUI_API bool ListBox(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int height_in_items = -1);
  IMGUI_API bool ListBox(const char* label, int* current_item, bool (*items_getter)(void* data, int idx, const char** out_text, const char** out_tooltip), void* data, int items_count, int height_in_items = -1);


  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool ColorEdit3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(ColorEdit3(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling integral types (excluding <int>) of various precisions as input
  template<typename T, std::enable_if_t<!std::is_same_v<T, int> && (std::is_integral_v<T> || std::is_enum_v<T>), bool> = true,
           typename ... Args>
  IMGUI_API bool Combo(const char* label, T* v, Args&& ... args) {
    int value;
    
    if constexpr (std::is_integral_v<T>)
      value = safeConvertIntegral<T, int>(*v);
    else
      value = static_cast<int>(*v);

    const bool result = Combo(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true,
    typename ... Args>
    IMGUI_API bool Combo(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(Combo(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling integral types (excluding <int>) of various precisions as input
  template<typename T, std::enable_if_t<!std::is_same_v<T, int> && (std::is_integral_v<T> || std::is_enum_v<T>), bool> = true,
           typename ... Args>
  IMGUI_API bool DragInt(const char* label, T* v, Args&& ... args) {
    int value;

    if constexpr (std::is_integral_v<T>)
      value = safeConvertIntegral<T, int>(*v);
    else
      value = static_cast<int>(*v);

    const bool result = DragInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true,
    typename ... Args>
  IMGUI_API bool DragInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragInt(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragInt2(const char* label, dxvk::RtxOption<dxvk::Vector2i>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragInt2(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling integral types (excluding <int>) of various precisions as input
  template<typename T, std::enable_if_t<!std::is_same_v<T, int> && (std::is_integral_v<T> || std::is_enum_v<T>), bool> = true,
    typename ... Args>
  IMGUI_API bool InputInt(const char* label, T* v, Args&& ... args) {
    int value;

    if constexpr (std::is_integral_v<T>)
      value = safeConvertIntegral<T, int>(*v);
    else
      value = static_cast<int>(*v);

    const bool result = InputInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true,
            typename ... Args>
  IMGUI_API bool InputInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(InputInt(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling integral types (excluding <int>) of various precisions as input
  template<typename T, std::enable_if_t<!std::is_same_v<T, int> && (std::is_integral_v<T> || std::is_enum_v<T>), bool> = true,
           typename ... Args>
  IMGUI_API bool SliderInt(const char* label, T* v, Args&& ... args) {
    int value;

    if constexpr (std::is_integral_v<T>)
      value = safeConvertIntegral<T, int>(*v);
    else
      value = static_cast<int>(*v);

    const bool result = SliderInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true, 
            typename ... Args>
  IMGUI_API bool SliderInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(SliderInt(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragFloat(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // DragFloat wrapped by a checkbox.
  // Disabling the checkbox resets the value to the default value.
  // Enabling the checkbox sets the value to `enabledValue`.
  template <typename ... Args>
  IMGUI_API bool OptionalDragFloat(const char* label, dxvk::RtxOption<float>* rtxOption, float enabledValue, Args&& ... args) {
    // enabledValue and the default value can't match, otherwise the checkbox won't stay checked.
    assert(enabledValue != rtxOption->getDefaultValue());
    bool enabled = rtxOption->getValue() != rtxOption->getDefaultValue();
    std::string hiddenLabel = dxvk::str::format("##", label);
    bool changed = IMGUI_ADD_TOOLTIP(Checkbox(hiddenLabel.c_str(), &enabled), "Check to enable the option.\nUncheck to disable it and reset to default value.");
     ImGui::SameLine();
    if (changed) {
      rtxOption->setValue(enabled ? enabledValue : rtxOption->getDefaultValue());
    }
    if (enabled) {
      changed |= IMGUI_ADD_TOOLTIP(DragFloat(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
    } else {
      ImGui::TextDisabled("%s (Disabled)", label);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltipUnformatted(rtxOption->getDescription());
      }
    }

    return changed;
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragFloat2(const char* label, dxvk::RtxOption<dxvk::Vector2>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragFloat2(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragFloat3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragFloat3(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragFloat4(const char* label, dxvk::RtxOption<dxvk::Vector4>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragFloat4(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragIntRange2(const char* label, dxvk::RtxOption<dxvk::Vector2i>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(DragIntRange2(label, &rtxOption->getValue().x, &rtxOption->getValue().y, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool InputFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(InputFloat(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool SliderFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(SliderFloat(label, &rtxOption->getValue(), std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool SliderFloat2(const char* label, dxvk::RtxOption<dxvk::Vector2>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(SliderFloat2(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool SliderFloat3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(SliderFloat3(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool SliderFloat4(const char* label, dxvk::RtxOption<dxvk::Vector4>* rtxOption, Args&& ... args) {
    return IMGUI_ADD_TOOLTIP(SliderFloat4(label, rtxOption->getValue().data, std::forward<Args>(args)...), rtxOption->getDescription());
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool InputText(const char* label, dxvk::RtxOption<std::string>* rtxOption, Args&& ... args) {
    // Note: Includes the null terminator, so the maximum length of text is only 1023 bytes.
    constexpr std::uint32_t maxTextBytes = 1024;
    std::array<char, maxTextBytes> textBuffer{};
    const auto& value = rtxOption->getValue();
    // Note: textBuffer.size()-1 used as the null terminator is not copied and rather added in manually to handle
    // the case of the string being larger than the size of the buffer.
    const auto clampedTextSize = std::min(value.size(), textBuffer.size() - 1);

    std::memcpy(textBuffer.data(), value.data(), clampedTextSize);
    // Note: Add the null terminator to the end of however much was copied.
    textBuffer[clampedTextSize] = '\0';

    const auto changed = IMGUI_ADD_TOOLTIP(InputText(label, textBuffer.data(), textBuffer.size(), std::forward<Args>(args)...), rtxOption->getDescription());

    if (changed) {
      rtxOption->setValue(std::string(textBuffer.data()));
    }

    return changed;
  }

  // Combo Box with unique key per combo entry
  // The combo entries are displayed in the order they appear in ComboEntries
  template<typename T>
  class ComboWithKey {
  public:
    struct ComboEntry {
      T key;
      const char* name = nullptr;
      const char* tooltip = nullptr;
    };
    using ComboEntries = std::vector<ComboEntry>;

    ComboWithKey(const char* widgetName, ComboEntries&& comboEntries)
      : m_comboEntries { std::move(comboEntries) }
      , m_widgetName { widgetName } {
      for (int i = 0; i < m_comboEntries.size(); i++) {
        T key = m_comboEntries[i].key;
        assert(m_keyToComboIdx.find(key) == m_keyToComboIdx.end() && "Duplicate key found");
        m_keyToComboIdx[key] = i;
      }
    }

    ~ComboWithKey() = default;

    ComboWithKey(const ComboWithKey&) = delete;
    ComboWithKey(ComboWithKey&&) noexcept = delete;
    ComboWithKey& operator=(const ComboWithKey&) = delete;
    ComboWithKey& operator=(ComboWithKey&&) noexcept = delete;

    template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true>
    bool getKey(T* key) {
      auto it = m_keyToComboIdx.find(*key);

      int comboIdx = it != m_keyToComboIdx.end() ? it->second : 0;

      bool isChanged = Combo(m_widgetName, &comboIdx, getString, static_cast<void*>(&m_comboEntries), static_cast<int>(m_comboEntries.size()));

      *key = m_comboEntries[comboIdx].key;

      return isChanged;
    }

    // Variant handling RtxOption as input
    template <typename R>
    bool getKey(dxvk::RtxOption<R>* rtxOption) {
      return IMGUI_ADD_TOOLTIP(getKey(&rtxOption->getValue()), rtxOption->getDescription());
    }

    ComboEntry* getComboEntry(const T& key) {
      auto it = m_keyToComboIdx.find(key);

      if (it == m_keyToComboIdx.end()) {
        return nullptr;
      }

      int comboIdx = it->second;

      return &m_comboEntries[comboIdx];
    }

    void removeComboEntry(const T& key) {
      auto it = m_keyToComboIdx.find(key);

      if (it == m_keyToComboIdx.end()) {
        return;
      }

      const int comboIdx = it->second;

      // Remove the corresponding elements in containers
      m_comboEntries.erase(m_comboEntries.begin() + comboIdx);
      m_keyToComboIdx.erase(it);
    }

  private:
    static bool getString(void* data, int entryIdx, const char** out_text, const char** out_tooltip) {
      const ComboEntries& v = *reinterpret_cast<const ComboEntries*>(data);

      if (entryIdx >= v.size())
        return false;

      if (out_text) {
        *out_text = v[entryIdx].name;
      }
      if (out_tooltip) {
        *out_tooltip = v[entryIdx].tooltip;
      }

      return true;
    }

    ComboEntries m_comboEntries;
    const char* m_widgetName;
    std::unordered_map<T /*key*/, int /*comboIdx*/> m_keyToComboIdx;
  };
}