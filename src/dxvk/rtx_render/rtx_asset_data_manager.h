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
    std::map<std::string, Rc<AssetPackage>> m_packages;
    std::string m_basePath;
  public:
    AssetDataManager();
    ~AssetDataManager();

    void initialize(const std::filesystem::path& basePath);
    Rc<AssetData> findAsset(const std::string& filename);
  };

} // namespace dxvk
