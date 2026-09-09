/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once

#include "rtx_types.h"
#include "rtx_lights.h"
#include "rtx_mod_manager.h"
#include "rtx_asset_replacer.h"
#include "rtx_utils.h"
#include "rtx_option.h"

namespace dxvk {
  class DxvkContext;

  /**
   * \brief USD Mod
   *
   * Handles asset replacements importing from a USD.
   */
  class UsdMod final : public Mod {
  public:
    ~UsdMod() override;

    void load(const Rc<DxvkContext>& context) override;
    void unload() override;
    bool checkForChanges(const Rc<DxvkContext>& context) override;
    bool applyPendingRebuild(const Rc<DxvkContext>& context, AssetChanges& changes) override;
    void onDestroy() override;

    static const ModTypeInfo& getTypeInfo();

    static void loadUsdPlugins();

    RTX_OPTION("rtx.usd", bool, reloadOnChanged, true,
               "Watches for changes to USD files and reloads them in-game when saved. Uses OS file-change notifications (no polling overhead). "
               "Note: changes made during the first few seconds of initial stage load may be missed if the mod directory is not yet registered with the file watcher. "
               "Set to False to suppress automatic reloads; the file watcher still runs so this can be toggled at runtime without restarting.");

    RTX_OPTION("rtx.usd", bool, watchDependencies, true,
               "When reloadOnChanged is True, also triggers a reload when a USD file this mod actually depends on changes "
               "(sublayers, references, payloads - anything composed into the mod's stage), not just mod.usda itself. "
               "Useful when textures or mesh data live in separate USD files that mod.usda references.");

  private:
    friend struct UsdModTypeInfo;
    explicit UsdMod(const Mod::Path& usdFilePath);

    // Using pimpl to hide USD from headers.
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };

} // namespace dxvk
