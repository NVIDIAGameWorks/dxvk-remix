/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include <filesystem>
#include <map>
#include <mutex>
#include "../util/util_singleton.h"
#include "rtx_asset_data.h"
#include "rtx_asset_package.h"

namespace dxvk {
  // Asset Data Manager class is responsible for asset data discovery
  // and parsing.
  // Upon a successful asset discovery and (pre)parsing, Asset Data Manager
  // wraps the asset in an AssetData implementation that help to abstract
  // the access to actual data.
  class AssetDataManager : public Singleton<AssetDataManager> {
    using PackageSet = std::map<std::string, Rc<AssetPackage>>;

    struct SearchPathEntry {
      // Absolute, lowercased, with a trailing separator.
      std::string path;
      // Packages mounted from this directory. Only populated when RtxIo is enabled.
      PackageSet packages;
    };

    // Search paths in ascending precedence order: the LAST entry wins, matching the
    // reverse traversal in findAsset.
    std::vector<SearchPathEntry> m_searchPaths;
    // Guards the list. A USD mod's rebuild worker resolves textures through findAsset
    // while another mod's finished rebuild is installed on the render thread, so a
    // write can land mid-iteration without this.
    std::mutex m_searchPathMutex;
  public:
    AssetDataManager();
    ~AssetDataManager();

    /**
     * \brief Replace the entire search path list
     *
     * \p paths is in ascending precedence order - the last entry wins. Callers pass
     * the full list rather than mutating a shared set, because precedence across mods
     * is a property of the mod ordering and only the ModManager knows it.
     *
     * Packages already mounted for a path are carried over rather than re-opened, so
     * one mod reloading does not re-mount every other mod's packages. Paths that drop
     * out of the list have their packages released here.
     *
     * \param [in] paths Search paths, lowest precedence first
     */
    void setSearchPaths(const std::vector<std::filesystem::path>& paths);

    /**
     * \brief Find an asset
     *
     * The method attempts to find an asset. The search logic is the following:
     *
     *   1. first, method tries to directly use the provided file name
     *   2. if file is not found on disk, method attempts a search in
     *      the search paths set that is populated using addSearchPath() method
     *
     * \param [in] filename Asset file name
     */
    Rc<AssetData> findAsset(const std::string& filename, bool allowOnlyPartialDdsLoader = false);
  };

} // namespace dxvk
