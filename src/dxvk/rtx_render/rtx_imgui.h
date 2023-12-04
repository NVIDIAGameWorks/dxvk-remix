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