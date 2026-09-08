#include "../dxvk/rtx_render/rtx_types.h"
#include "remix_category_names.h"

namespace dxvk {
  // NV-DXVK start: Use shared Remix category metadata
  static_assert(sizeof(kRemixCategoryEntries) / sizeof(kRemixCategoryEntries[0]) == (size_t) InstanceCategories::Count,
                "Please add/remove the category entry in remix_category_names.h.");

  // Used when reading/writing with Remix USD mods.
  static const char* getInstanceCategorySubKey(InstanceCategories cat) {
    if (cat >= InstanceCategories::Count) {
      return "";
    }
    return kRemixCategoryEntries[(uint32_t) cat].attr;
  }
  // NV-DXVK end
}
