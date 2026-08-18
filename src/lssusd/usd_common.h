#include "../dxvk/rtx_render/rtx_types.h"
#include "remix_category_names.h"

namespace dxvk {
  static_assert(sizeof(kRemixCategoryNames) / sizeof(kRemixCategoryNames[0]) == (size_t) InstanceCategories::Count,
                "Please add/remove the category name in remix_category_names.h.");

  // Used when reading/writing with Remix USD mods.
  static const char* getInstanceCategorySubKey(InstanceCategories cat) {
    if (cat >= InstanceCategories::Count) {
      return "";
    }
    return kRemixCategoryNames[(uint32_t) cat];
  }
}
