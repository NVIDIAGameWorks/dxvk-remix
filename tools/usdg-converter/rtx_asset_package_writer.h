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
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <tuple>

#include "rtx_asset_package.h"

struct PackageJobDesc;
class AsyncAssetPackager;

enum class AssetSourceType {
  None,
  File,
  Image,
  Buffer
};

class AssetPackageWriter {
  friend class AsyncAssetPackager;
public:
  using PackageHandle = std::shared_ptr<dxvk::AssetPackage>;

  AssetPackageWriter();
  ~AssetPackageWriter();

  float getProgressApprox() const;

  PackageHandle createPackage(const std::string& filename);
  void finalizePackage(const PackageHandle& package);
  void waitPackageCompletion(const PackageHandle& package);

  uint32_t addUsdAsset(const PackageHandle& package,
    const std::string& name, const std::string& usdFile);

  uint32_t addImageAsset(const PackageHandle& package, int volume,
    const std::string& name, uint32_t compressionLevel,
    const std::string& imageFile);

  uint32_t addBufferAsset(const PackageHandle& package, int volume,
    const std::string& name, uint32_t compressionLevel,
    const void* data, size_t size);

  // Transfers descriptors of unmodified asset from the source package to the destination package.
  // The blob descriptors keep pointing at the source's resource volumes, so no data is copied.
  // Assets for which shouldSkip returns true, or that already exist in the destination,
  // are not transferred.
  // Returns the number of added assets.
  uint32_t transferAssetDescriptors(const PackageHandle& dst, const PackageHandle& src,
    const std::function<bool(const std::string&)>& shouldSkip);

  // Returns the highest volume index referenced in the package.
  static uint32_t getLastVolume(const PackageHandle& package);

  static bool isImageAssetUnchanged(const std::string& path,
    const PackageHandle& package, uint32_t assetIdx);

private:
  void scheduleJob(PackageJobDesc&& jobDesc);
  bool packageJob(const PackageJobDesc& jobDesc);

  bool finalizePackageImpl(const PackageHandle& package);
  bool writeImage(const PackageJobDesc& jobDesc);
  bool writeBuffer(const PackageJobDesc& jobDesc);
  bool writeFile(const PackageJobDesc& jobDesc);

  std::tuple<uint32_t, bool> reserveAsset(const PackageHandle& package,
    const std::string& name, AssetSourceType type);

  const std::unique_ptr<AsyncAssetPackager>& getPackager(const PackageHandle& package) const;
  std::unique_lock<std::recursive_mutex> lockPackage(const PackageHandle& package) const;

  std::unordered_map<std::string, std::unique_ptr<AsyncAssetPackager>> m_asyncPackagers;

  mutable std::recursive_mutex m_mutex;
};
