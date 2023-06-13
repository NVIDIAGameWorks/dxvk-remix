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
    std::map<uint32_t, std::tuple<std::string, PackageSet>> m_packageSets;
    std::map<uint32_t, std::string> m_searchPaths;
  public:
    AssetDataManager();
    ~AssetDataManager();

    /**
     * \brief Add a search path
     *
     * Adds a path to the search paths set, assigns priority.
     * Every search path in the search set has a priority, and the whole set is
     * traversed in the reverse order, i.e. paths with larger priority values
     * have higher priority. The method will also attempt to discover and mount
     * packages in the location specified by the path.
     * Note: in the current implementation every search path must have a unique
     * priority. The previous path will be overriden if the incoming path has
     * same priority.
     *
     * \param [in] priority Search path priority
     * \param [in] path Search path
     */
    void addSearchPath(uint32_t priority, const std::filesystem::path& path);

    /**
     * \brief Clear the search paths set
     *
     * Clears the search paths set and mounted packages.
     */
    void clearSearchPaths() {
      m_searchPaths.clear();
      m_packageSets.clear();
    }

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
    Rc<AssetData> findAsset(const std::string& filename);
  };

} // namespace dxvk
