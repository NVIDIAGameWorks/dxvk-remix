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
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <filesystem>

#include "rtx_asset_package.h"

class AssetPackageExtractor {
public:
  using PackageHandle = std::shared_ptr<dxvk::AssetPackage>;

  PackageHandle openPackage(const std::string& filename);
  bool extractPackage(PackageHandle package, const std::string& outputPath, int numThreads);

private:
  bool extractResourcePack(PackageHandle package, uint32_t numWorkers);
  void extractJob(PackageHandle package, std::atomic_uint32_t& resourceIdx);
  std::filesystem::path makeFullPath(const std::string& filename);
  std::vector<uint8_t> uncompressBlob(PackageHandle package, uint32_t blobIdx, size_t dataSize);
  void writeFile(const std::string& filename, const std::vector<uint8_t>& data);

  std::atomic_uint64_t m_totalUncompressed;

  std::filesystem::path m_outputPath;
  bool m_breakOnFailure = true;
  bool m_overwrite = true;
  bool m_checkCrc32 = false;

  mutable std::recursive_mutex m_mutex;
};
