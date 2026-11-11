/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#define _CRT_SECURE_NO_WARNINGS
#define _FILE_OFFSET_BITS 64

#include <direct.h>

#include <unordered_map>

#include "rtx_asset_package_extractor.h"
#include "rtx_asset_package.h"

#include "util_string.h"
#include "util_error.h"

#include "dxgi_to_vulkan.h"

#include <gli/gli.hpp>

#include <rtxio/rtxioCompression.h>

#ifndef WIN32
#define _ftelli64 ftello
#endif

using namespace dxvk;

static const uint32_t kMaxWorkers = 32;

static inline uint64_t NowCountUs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::nanoseconds(now.time_since_epoch()).count() / 1000;
}

// Finds the DXGI format id (i.e. index into s_dxgiFormat2VkFormatDesc) and block
// layout description matching a given VkFormat.
static uint32_t findDxgiFormat(uint32_t vkFormat, const VkFormatDesc** outDesc = nullptr) {
  uint32_t dxgiFormat = 0;

  for (const auto& desc : s_dxgiFormat2VkFormatDesc) {
    if (desc.format == vkFormat) {
      if (outDesc) {
        *outDesc = &desc;
      }
      return dxgiFormat;
    }
    ++dxgiFormat;
  }

  return 0; // DXGI_FORMAT_UNKNOWN
}

// Computes the block-aligned image layout for a mip level, mirroring the
// row pitch / linear size fields expected in a DDS header.
static void getImageInfo(const VkFormatDesc& formatDesc, uint32_t width, uint32_t height,
    uint32_t& numBytes, uint32_t& rowBytes, uint32_t& numRows) {
  const uint32_t blockWidth = std::max(1u, formatDesc.blockWidth);
  const uint32_t blockHeight = std::max(1u, formatDesc.blockHeight);

  const uint32_t blocksWide = std::max(1u, (width + blockWidth - 1) / blockWidth);
  const uint32_t blocksHigh = std::max(1u, (height + blockHeight - 1) / blockHeight);

  rowBytes = blocksWide * formatDesc.bytesPerBlock * formatDesc.bytesPerTexel;
  numRows = blocksHigh;
  numBytes = rowBytes * numRows;
}

template<typename T>
void append(std::vector<uint8_t>& data, T& obj) {
  const size_t pos = data.size();
  data.resize(pos + sizeof(T));
  new (data.data() + pos) T(obj);
}

template<>
void append(std::vector<uint8_t>& data, const std::vector<uint8_t>& obj) {
  const size_t pos = data.size();
  data.resize(pos + obj.size());
  memcpy(data.data() + pos, obj.data(), obj.size());
}

static void writeDDSHeader(std::vector<uint8_t>& data, const AssetPackage::AssetDesc* desc) {
  const VkFormatDesc* formatDesc = nullptr;
  const uint32_t dxgiFormat = findDxgiFormat(desc->format, &formatDesc);

  uint32_t numBytes, rowBytes, numRows;
  getImageInfo(*formatDesc, desc->width, desc->height, numBytes, rowBytes, numRows);

  gli::detail::dds_header header{};
  gli::detail::dds_header10 header10{};

  memset(header.Reserved1, 0, sizeof(header.Reserved1));
  memset(header.Reserved2, 0, sizeof(header.Reserved2));

  const bool isCompressed = formatDesc->bytesPerBlock > 1;

  header.Size = sizeof(header);
  header.Flags = gli::detail::DDSD_CAPS | gli::detail::DDSD_HEIGHT |
    gli::detail::DDSD_WIDTH | gli::detail::DDSD_PIXELFORMAT;
  header.Height = desc->height;
  header.Width = desc->width;
  header.Pitch = isCompressed ? numBytes : rowBytes;
  header.Depth = desc->depth;
  header.MipMapLevels = desc->numMips;

  header.Format.size = sizeof(gli::detail::dds_pixel_format);
  header.Format.flags = gli::dx::DDPF_FOURCC;
  header.Format.fourCC = gli::dx::D3DFMT_DX10;
  header.Format.bpp = 0;
  header.Format.Mask = glm::u32vec4(0);

  header.SurfaceFlags = 0;
  header.CubemapFlags = 0;

  header10.ArraySize = desc->arraySize;
  header10.Format.DDS = static_cast<gli::dx::dxgi_format_dds>(dxgiFormat);
  header10.AlphaFlags = gli::detail::DDS_ALPHA_MODE_STRAIGHT;

  switch (desc->type) {
  case AssetPackage::AssetDesc::Type::IMAGE_1D:
    header10.ResourceDimension = gli::detail::D3D10_RESOURCE_DIMENSION_TEXTURE1D;
    break;
  case AssetPackage::AssetDesc::Type::IMAGE_2D:
    header10.ResourceDimension = gli::detail::D3D10_RESOURCE_DIMENSION_TEXTURE2D;
    break;
  case AssetPackage::AssetDesc::Type::IMAGE_3D:
    header10.ResourceDimension = gli::detail::D3D10_RESOURCE_DIMENSION_TEXTURE3D;
    header.Flags |= gli::detail::DDSD_DEPTH;
    break;
  case AssetPackage::AssetDesc::Type::IMAGE_CUBE:
    header10.ResourceDimension = gli::detail::D3D10_RESOURCE_DIMENSION_TEXTURE2D;
    header10.MiscFlag |= gli::detail::D3D10_RESOURCE_MISC_TEXTURECUBE;
    header.CubemapFlags |= gli::detail::DDSCAPS2_CUBEMAP_ALLFACES | gli::detail::DDSCAPS2_CUBEMAP;
    break;
  };

  header.Flags |= desc->numMips > 1 ? gli::detail::DDSD_MIPMAPCOUNT : 0;
  header.Flags |= isCompressed ? gli::detail::DDSD_LINEARSIZE : gli::detail::DDSD_PITCH;

  const uint32_t kDDSFileSignature = 0x20534444; // 'DDS '
  append(data, kDDSFileSignature);
  append(data, header);
  append(data, header10);
}

std::filesystem::path AssetPackageExtractor::makeFullPath(const std::string& filename) {
  static std::mutex m_pathMutex;
  std::lock_guard<std::mutex> _(m_pathMutex);

  std::filesystem::path path(filename);
  std::list<std::filesystem::path> splitPaths;

  // Make the full path
  std::filesystem::path fullPath = m_outputPath / path;

  // Check if we are still within the m_outputPath
  {
    auto rel = std::filesystem::relative(fullPath, m_outputPath);
    if (rel.empty() || *rel.begin() == "..") {
      Logger::err(str::format("Likely fatal: path ", path.string()," escapes the base folder."));

      if (m_breakOnFailure) {
        exit(-1);
      }
      return {};
    }
  }

  // Skip filename
  path = fullPath.parent_path();
  {
    static std::unordered_set<std::string> uniquePaths;
    const std::string pathStr = path.string();

    if (uniquePaths.count(pathStr) > 0) {
      // check if folder exsits
      int result = _mkdir(pathStr.c_str());
      if (result == 0 || errno == EEXIST) {
        return fullPath;
      }
    } else {
      uniquePaths.insert(pathStr);
    }
  }

  // Split path
  while (!path.empty()) {
    splitPaths.push_back(path);

    auto parentPath = path.parent_path();
    if (parentPath.root_path() == parentPath)
      break;

    path = std::move(parentPath);
  }

  // Create folder tree structure
  for (auto it = splitPaths.rbegin(); it != splitPaths.rend(); ++it) {
    int result = _mkdir(it->string().c_str());

    if (result != 0 && errno != EEXIST) {
      Logger::err(str::format("Unable to create folder: ", it->string()));

      if (m_breakOnFailure)
        exit(-1);
    }
  }

  return fullPath;
}

std::vector<uint8_t> AssetPackageExtractor::uncompressBlob(PackageHandle package,
    uint32_t blobIdx, size_t dataSize) {
  auto blobDesc = package->getDataBlobDesc(blobIdx);

  if (!blobDesc)
    return {};

  std::vector<uint8_t> compressed(blobDesc->size);

  size_t readSize = 0;
  {
    std::lock_guard<std::recursive_mutex> _(m_mutex);
    readSize = package->readDataBlob(blobIdx, compressed.data(), compressed.size());
  }

  if (readSize != compressed.size()) {
    Logger::err(str::format("Read failed! Got: ", readSize,
        " bytes, expected: ", compressed.size(), " bytes."));

    if (m_breakOnFailure)
      exit(-1);
  }

  // No compression case
  if (blobDesc->compression == 0)
    return compressed;

  std::vector<uint8_t> uncompressed(dataSize);

  if (!rtxioDecompress(uncompressed.data(), dataSize, compressed.data(), compressed.size(), 4)) {
    Logger::err(str::format("Blob ", blobIdx, " RTXIO decompression failed!"));

    if (m_breakOnFailure)
      exit(-1);

    return uncompressed;
  }

  if (m_checkCrc32) {
    const uint32_t crc32 = rtxioCrc32(0, uncompressed.data(), dataSize);

    if (crc32 != blobDesc->crc32) {
      Logger::err(str::format("Blob ", blobIdx, " CRC32 check failed! Got: ",
          crc32, " expected: ", blobDesc->crc32));

      if (m_breakOnFailure)
        exit(-1);
    }
  }

  m_totalUncompressed += uncompressed.size();

  return uncompressed;
}

void AssetPackageExtractor::writeFile(const std::string& filename, const std::vector<uint8_t>& data) {
  const auto fullPath = makeFullPath(filename);

  if (fullPath.empty()) {
    return;
  }

  if (FILE* f = fopen(fullPath.string().c_str(), m_overwrite ? "wb" : "wbx")) {
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
  } else {
    Logger::err(str::format("Unable to open: ", filename));

    if (!m_overwrite && errno == EEXIST)
      Logger::err("File exists!");

    if (m_breakOnFailure)
      exit(-1);
  }
}

void AssetPackageExtractor::extractJob(PackageHandle package, std::atomic_uint32_t& resourceIdx) {
  std::vector<uint8_t> resourceData;

  while (true) {
    const uint32_t localResourceIdx = resourceIdx++;

    if (localResourceIdx >= package->getAssetCount())
      break;

    auto resourceDesc = package->getAssetDesc(localResourceIdx);
    auto resourceName = package->getAssetName(localResourceIdx);

    if (resourceDesc->type == AssetPackage::AssetDesc::Type::BUFFER) {
      // nothing to do
      // resourceData = uncompressBlob(package, resourceDesc->baseBlobIdx, resourceDesc->size);
    } else if (resourceDesc->type == AssetPackage::AssetDesc::Type::RAW_FILE) {
      // nothing to do
    } else if (resourceDesc->type == AssetPackage::AssetDesc::Type::IMAGE_2D) {
      Logger::info(str::format("Extracting image ", resourceName));

      writeDDSHeader(resourceData, resourceDesc);

      const VkFormatDesc* formatDesc = nullptr;
      findDxgiFormat(resourceDesc->format, &formatDesc);

      const uint32_t numLooseMips = resourceDesc->numMips - resourceDesc->numTailMips;
      for (uint32_t slice = 0; slice < resourceDesc->arraySize; slice++) {
        // read every loose mip level
        for (uint32_t mip = 0; mip < numLooseMips; mip++) {
          const uint32_t blobIndex = resourceDesc->baseBlobIdx +
              mip + slice * numLooseMips;

          uint32_t numBytes, rowBytes, numRows;
          getImageInfo(*formatDesc, std::max(1, resourceDesc->width >> mip),
              std::max(1, resourceDesc->height >> mip), numBytes, rowBytes, numRows);

          const auto data = uncompressBlob(package, blobIndex, numBytes);
          append(resourceData, data);
        }

        // read tail blob
        if (numLooseMips < resourceDesc->numMips) {
          size_t tailSize = 0;
          for (uint32_t mip = numLooseMips; mip < resourceDesc->numMips; mip++) {
            const uint32_t width = std::max(1, resourceDesc->width >> mip);
            const uint32_t height = std::max(1, resourceDesc->height >> mip);

            uint32_t numBytes, rowBytes, numRows;
            getImageInfo(*formatDesc, width, height, numBytes, rowBytes, numRows);

            tailSize += numBytes;
          }

          const auto data = uncompressBlob(package, resourceDesc->tailBlobIdx + slice, tailSize);
          append(resourceData, data);
        }
      }
    } else {
      Logger::warn("Unsupported resource type! Skipping.");
    }

    if (!resourceData.empty()) {
      writeFile(resourceName, resourceData);
      resourceData.clear();
    }
  }
}

bool AssetPackageExtractor::extractResourcePack(PackageHandle package, uint32_t numWorkers) {
  const uint64_t startTimeUs = NowCountUs();

  numWorkers = std::min(kMaxWorkers, std::min(numWorkers,
      std::thread::hardware_concurrency()));

  std::atomic_uint32_t resourceIdx = 0;

  std::thread workers[kMaxWorkers];
  uint32_t jobIdx = 0;
  while (jobIdx < numWorkers - 1) {
    auto& resourceJob = workers[jobIdx++];

    resourceJob = std::thread([this, package, &resourceIdx]() mutable {
      extractJob(package, resourceIdx);
    });
  }

  extractJob(package, resourceIdx);

  for (auto& resourceJob : workers) {
    if (resourceJob.joinable())
      resourceJob.join();
  }

  const uint64_t totalTimeUs = NowCountUs() - startTimeUs;
#if 0
  printf("%d resources processed.\n", package->GetNumResources());
  printf("Total size: %zd MB.\n", GTotalUncompressed.load() / 1000 / 1000);
  printf("Decompression throughput: %0.2f MB/s.\n",
      static_cast<double>(GTotalUncompressed.load()) / totalTimeUs);
#endif
  return true;
}

bool AssetPackageExtractor::extractPackage(
  AssetPackageExtractor::PackageHandle package,
  const std::string& outputPath, int numThreads) {
  m_outputPath = outputPath;
  m_breakOnFailure = true;
  m_overwrite = true;
  m_checkCrc32 = false;

  return extractResourcePack(package, numThreads);
}

AssetPackageExtractor::PackageHandle
AssetPackageExtractor::openPackage(const std::string& filename) {
  auto package = std::make_shared<AssetPackage>(filename);

  if (!package->initialize()) {
    Logger::err(str::format("Unable to initialize resource package ", filename));
    return {};
  }

  package->closeAllFiles();

  return std::move(package);
}
