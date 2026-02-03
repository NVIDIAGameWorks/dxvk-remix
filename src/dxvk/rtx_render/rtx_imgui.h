#pragma once

#include <imgui\imgui.h>
#include <imgui\imgui_internal.h>
#include "..\rtx_render\rtx_option.h"
#include "..\rtx_render\rtx_gui_widgets.h"
#include "..\util\util_string.h"
#include "..\util\util_vector.h"
#include <type_traits>
#include <utility>

namespace RemixGui {

  IMGUI_API void SetTooltipUnformatted(const char* text);  // Same as SetTooltip, just without text formatting (so percentage signs do not interfere with tooltips when not desired).
  IMGUI_API bool IsItemHoveredDelay(float delay_in_seconds); // Same as IsItemHovered, but only returns true after the item was hovered for x amount of time
  IMGUI_API void SetTooltipToLastWidgetOnHover(const char* text);  // Conditionally sets tooltip if IsItemHovered() is true

  // RtxOption popup system - shows warnings when editing blocked options.
  // The popup figures out the blocking reason at render time and offers appropriate actions.
  //
  // Usage:
  // - Call CheckRtxOptionPopups() after the user modifies an option value
  // - Call RenderRtxOptionBlockedEditPopup() once per frame in the main UI loop
  //
  // Parameters:
  // - hash: optional hash for hash-set options (checks specific hash instead of entire option)
  // - onApplyAction: callback to apply user's intended action after clearing blockers (for hash sets)
  // Returns true if blocked (popup shown), false if not blocked.
  IMGUI_API bool CheckRtxOptionPopups(dxvk::RtxOptionImpl* impl,
                                      std::optional<XXH64_hash_t> hash = std::nullopt,
                                      std::function<void()> onApplyAction = nullptr);
  IMGUI_API void RenderRtxOptionBlockedEditPopup();
  
  // Format per-layer values for an RtxOption as a string.
  IMGUI_API std::string FormatOptionLayerValues(dxvk::RtxOptionImpl* impl,
                                                std::optional<XXH64_hash_t> hash = std::nullopt,
                                                bool includeInactive = true);
  

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
#define IMGUI_ADD_TOOLTIP(imguiCommand, tooltip) RemixGui::addTooltipAndPassthroughValue((imguiCommand), tooltip)

  // Build a full tooltip for an RtxOption, including layer info and blocking warnings.
  // This is a non-template function to avoid code bloat from template instantiations.
  // Declared here, implemented in rtx_imgui.cpp.
  IMGUI_API std::string BuildRtxOptionTooltip(dxvk::RtxOptionImpl* impl);

  // Macro for the common body of RtxOption widget wrappers.
  // widgetCall: the widget call expression using 'value' variable (e.g., DragFloat(label, &value, args...))
#define IMGUI_RTXOPTION_WIDGET(widgetCall) \
  RemixGui::RtxOptionUxWrapper wrapper(rtxOption); \
  auto value = rtxOption->get(); \
  bool changed = widgetCall; \
  if (changed) { \
    CheckRtxOptionPopups(rtxOption); \
    rtxOption->setDeferred(value); \
  } \
  return changed;

  IMGUI_API void TextCentered(const char* fmt, ...);
  IMGUI_API void TextWrappedCentered(const char* fmt, ...);
  IMGUI_API bool Checkbox(const char* label, dxvk::RtxOption<bool>* rtxOption);
  IMGUI_API bool ListBox(const char* label, int* current_item, const std::pair<const char*, const char*> items[], int items_count, int height_in_items = -1);
  IMGUI_API bool ListBox(const char* label, int* current_item, bool (*items_getter)(void* data, int idx, const char** out_text, const char** out_tooltip), void* data, int items_count, int height_in_items = -1);


  // RtxOption widget wrappers
  template <typename ... Args>
  IMGUI_API bool ColorEdit3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::ColorEdit3(label, value.data, std::forward<Args>(args)...))
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

    const bool result = RemixGui::Combo(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true, typename ... Args>
  IMGUI_API bool Combo(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(Combo(label, &value, std::forward<Args>(args)...))
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

    const bool result = RemixGui::DragInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  // Variant handling RtxOption as input
  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true, typename ... Args>
  IMGUI_API bool DragInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragInt(label, &value, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool DragInt2(const char* label, dxvk::RtxOption<dxvk::Vector2i>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragInt2(label, value.data, std::forward<Args>(args)...))
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

    const bool result = RemixGui::InputInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true, typename ... Args>
  IMGUI_API bool InputInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(InputInt(label, &value, std::forward<Args>(args)...))
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

    const bool result = RemixGui::SliderInt(label, &value, std::forward<Args>(args)...);

    if constexpr (std::is_integral_v<T>)
      *v = safeConvertIntegral<int, T>(value);
    else
      *v = static_cast<T>(value);

    return result;
  }

  template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true, typename ... Args>
  IMGUI_API bool SliderInt(const char* label, dxvk::RtxOption<T>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::SliderInt(label, (int*)&value, std::forward<Args>(args)...))
  }

  
  // Variant displaying megabytes as gigabytes,
  // as ImGui doesn't have a custom formatting to convert e.g. '1234' to '1.234'
  // Returns true if user modified the value.
  template <typename ... Args>
  IMGUI_API bool DragFloatMB_showGB(const char* label, dxvk::RtxOption<int>* rtxOption, Args&& ... args) {
    RemixGui::RtxOptionUxWrapper wrapper(rtxOption);
    float storage_gigabytes = float(rtxOption->get()) / 1024.f;
    // imgui for that float
    bool hasChanged = RemixGui::DragFloat(label, &storage_gigabytes, std::forward<Args>(args)...);

    if (hasChanged) {
      CheckRtxOptionPopups(rtxOption);
      // convert back to int megabytes, quantizing by 256mb
      constexpr int Quantize = 256;
      int quantizedMegabytes = int(storage_gigabytes * 1024 / Quantize) * Quantize;

      rtxOption->setDeferred(quantizedMegabytes);
    }

    return hasChanged;
  }

  template <typename ... Args>
  IMGUI_API bool DragFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragFloat(label, &value, std::forward<Args>(args)...))
  }

  // DragFloat wrapped by a checkbox.
  // Disabling the checkbox resets the value to the default value.
  // Enabling the checkbox sets the value to `enabledValue`.
  template <typename ... Args>
  IMGUI_API bool OptionalDragFloat(const char* label, dxvk::RtxOption<float>* rtxOption, float enabledValue, Args&& ... args) {
    // enabledValue and the default value can't match, otherwise the checkbox won't stay checked.
    assert(enabledValue != rtxOption->getDefaultValue());
    IMGUI_RTXOPTION_WIDGET(RemixGui::OptionalDragFloat(label, enabledValue, rtxOption->getDefaultValue(), &value, 0.9f, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool DragFloat2(const char* label, dxvk::RtxOption<dxvk::Vector2>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragFloat2(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool DragFloat3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragFloat3(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool DragFloat4(const char* label, dxvk::RtxOption<dxvk::Vector4>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragFloat4(label, value.data, std::forward<Args>(args)...))
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool DragIntRange2(const char* label, dxvk::RtxOption<dxvk::Vector2i>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::DragIntRange2(label, &value.x, &value.y, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool InputFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::InputFloat(label, &value, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool SliderFloat(const char* label, dxvk::RtxOption<float>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::SliderFloat(label, &value, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool SliderFloat2(const char* label, dxvk::RtxOption<dxvk::Vector2>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::SliderFloat2(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool SliderFloat3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::SliderFloat3(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool SliderFloat4(const char* label, dxvk::RtxOption<dxvk::Vector4>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::SliderFloat4(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool ColorPicker3(const char* label, dxvk::RtxOption<dxvk::Vector3>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::ColorPicker3(label, value.data, std::forward<Args>(args)...))
  }

  template <typename ... Args>
  IMGUI_API bool ColorPicker4(const char* label, dxvk::RtxOption<dxvk::Vector4>* rtxOption, Args&& ... args) {
    IMGUI_RTXOPTION_WIDGET(RemixGui::ColorPicker4(label, value.data, std::forward<Args>(args)...))
  }

  // Variant handling RtxOption as input
  template <typename ... Args>
  IMGUI_API bool InputText(const char* label, dxvk::RtxOption<std::string>* rtxOption, Args&& ... args) {
    RemixGui::RtxOptionUxWrapper wrapper(rtxOption);
    // Note: Includes the null terminator, so the maximum length of text is only 1023 bytes.
    constexpr std::uint32_t maxTextBytes = 1024;
    std::array<char, maxTextBytes> textBuffer{};
    const auto& value = rtxOption->get();
    // Note: textBuffer.size()-1 used as the null terminator is not copied and rather added in manually to handle
    // the case of the string being larger than the size of the buffer.
    const auto clampedTextSize = std::min(value.size(), textBuffer.size() - 1);

    std::memcpy(textBuffer.data(), value.data(), clampedTextSize);
    // Note: Add the null terminator to the end of however much was copied.
    textBuffer[clampedTextSize] = '\0';

    const auto changed = RemixGui::InputText(label, textBuffer.data(), textBuffer.size(), std::forward<Args>(args)...);

    if (changed) {
      CheckRtxOptionPopups(rtxOption);
      rtxOption->setDeferred(std::string(textBuffer.data()));
    } else if (ImGui::IsItemDeactivated()) {
      // If the text box loses focus when `ImGuiInputTextFlags_EnterReturnsTrue` is set, the input value would be lost.
      // This catches that case.
      if (strcmp(textBuffer.data(), rtxOption->get().c_str()) != 0) {
        CheckRtxOptionPopups(rtxOption);
        rtxOption->setDeferred(std::string(textBuffer.data()));
      }
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

      bool isChanged = RemixGui::Combo(m_widgetName, &comboIdx, getString, static_cast<void*>(&m_comboEntries), static_cast<int>(m_comboEntries.size()));

      *key = m_comboEntries[comboIdx].key;

      return isChanged;
    }

    // Variant handling RtxOption as input
    template <typename R>
    bool getKey(dxvk::RtxOption<R>* rtxOption) {
      IMGUI_RTXOPTION_WIDGET(getKey(&value))
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

    void addComboEntry(const ComboEntry& comboEntry) {
      assert(m_keyToComboIdx.find(comboEntry.key) == m_keyToComboIdx.end() && "Duplicate key found");
      m_comboEntries.push_back(comboEntry);
      m_keyToComboIdx[comboEntry.key] = m_comboEntries.size() - 1;
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