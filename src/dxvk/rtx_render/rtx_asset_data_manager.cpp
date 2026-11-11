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
#include "rtx_asset_data_manager.h"
#include "rtx_utils.h"
#include <unordered_set>
#include "rtx_options.h"
#include "rtx_asset_package.h"
#include "rtx_file_watch.h"
#include "rtx_io.h"
#include "dxvk_scoped_annotation.h"
#include <gli/gli.hpp>
#ifdef WITH_RTXIO
#include <rtxio/rtxioCompression.h>
#endif
#include <shlwapi.h>

namespace dxvk {

  // TODO: cache texture mips in CPU-RAM that are less than 32x32 (2^5) to reduce disk-access
  static constexpr uint8_t kMipLevelsToCache = 5;

  inline size_t getImageLayerSize(VkFormat format, const VkExtent3D& extent0, uint32_t level) {
    const DxvkFormatInfo* formatInfo = imageFormatInfo(format);
    const VkExtent3D levelExtent = util::computeMipLevelExtent(extent0, level);
    const VkExtent3D elementCount = util::computeBlockCount(levelExtent, formatInfo->blockSize);
    const size_t rowPitch = elementCount.width * formatInfo->elementSize;
    return rowPitch * elementCount.height;
  }

  class GliTextureData : public AssetData {
    AssetType type() const {
      switch (m_texture.target()) {
      case gli::target::TARGET_1D:
      case gli::target::TARGET_1D_ARRAY:
        return AssetType::Image1D;
      case gli::target::TARGET_2D:
      case gli::target::TARGET_2D_ARRAY:
      case gli::target::TARGET_CUBE:
      case gli::target::TARGET_CUBE_ARRAY:
        return AssetType::Image2D;
      case gli::target::TARGET_3D:
        return AssetType::Image3D;
      default:
        break;
      }

      assert(0 && "Unsupported gli image target type!");

      return AssetType::Unknown;
    }

    VkExtent3D extent(int level) const {
      const auto ext = m_texture.extent(level);
      return VkExtent3D {
        static_cast<uint32_t>(ext.x),
        static_cast<uint32_t>(ext.y),
        static_cast<uint32_t>(ext.z),
      };
    }

  public:
    bool load(const std::string& filename) {
      m_texture = gli::load(filename.c_str());

      if (!m_texture.empty()) {
        m_filename = filename;

        m_info.type = type();
        m_info.compression = AssetCompression::None;
        m_info.format = static_cast<VkFormat>(m_texture.format());
        m_info.extent = extent(0);
        m_info.mipLevels = m_texture.levels();
        m_info.mininumLevelsToUpload = std::min(uint32_t(kMipLevelsToCache), m_info.mipLevels);
        m_info.numLayers = m_texture.layers();
        m_info.lastWriteTime = std::filesystem::last_write_time(m_filename);
        m_info.filename = m_filename.c_str();

        m_hash = XXH64_std_hash<std::string> {}(m_filename);

        return true;
      }

      return false;
    }

    const void* data(int layer, int level) override {
      return m_texture.data(layer, 0, level);
    }

    void evictCache(int, int) override { }

    void releaseSource() override { }

    void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const override {
      assert(0 && "Data placement interface is not supported by GliTextureData");
      offset = 0;
      size = 0;
    }

  private:
    gli::texture m_texture;
    std::string m_filename;
  };

  class DdsFileParser {
  public:
    virtual ~DdsFileParser() {
      closeHandle();
    }

    bool parse(const std::string& filename) {
      using namespace gli::detail;

      m_filename = filename;

      if (openHandle() == nullptr)
        return false;

      std::fseek(m_file, 0, SEEK_END);
      m_fileSize = std::ftell(m_file);
      std::fseek(m_file, 0, SEEK_SET);

      dds_header header;
      dds_header10 header10;

      if (m_fileSize < sizeof(FOURCC_DDS) + sizeof(header))
        return false;

      char fourcc[sizeof(FOURCC_DDS)];
      std::fread(fourcc, sizeof(fourcc), 1, m_file);
      if (std::memcmp(fourcc, FOURCC_DDS, 4) != 0)
        return false;

      std::fread(&header, sizeof(header), 1, m_file);

      if ((header.Format.flags & gli::dx::DDPF_FOURCC) &&
          (header.Format.fourCC == gli::dx::D3DFMT_DX10 || header.Format.fourCC == gli::dx::D3DFMT_GLI1)) {
        if (m_fileSize < sizeof(FOURCC_DDS) + sizeof(header) + sizeof(header10))
          return false;

        std::fread(&header10, sizeof(header10), 1, m_file);
      }

      m_dataOffset = std::ftell(m_file);

      auto format = get_dds_format(header, header10);
      m_format = static_cast<VkFormat>(format);

      m_levels = (header.Flags & DDSD_MIPMAPCOUNT) ? int(header.MipMapLevels) : 1;

      m_layers = int(std::max(header10.ArraySize, 1u));

      m_faces = 1;
      if (header.CubemapFlags & DDSCAPS2_CUBEMAP)
        m_faces = int(glm::bitCount(header.CubemapFlags & DDSCAPS2_CUBEMAP_ALLFACES));

      m_width = header.Width;
      m_height = header.Height;
      m_depth = 1;
      if (header.CubemapFlags & DDSCAPS2_VOLUME)
        m_depth = header.Depth;

      size_t blockSize = gli::block_size(format);
      glm::ivec3 blockExtent = gli::block_extent(format);
      assert(m_levelSizes.size() >= m_levels && "DDS level sizes array overrun! Increase array size.");
      for (int level = 0; level < m_levels; ++level) {
        VkExtent3D levelExtent {
          std::max(header.Width >> level, 1u), std::max(header.Height >> level, 1u), 1u
        };
        uint32_t widthBlocks = std::max(1u, (levelExtent.width + blockExtent.x - 1) / blockExtent.x);
        uint32_t heightBlocks = std::max(1u, (levelExtent.height + blockExtent.y - 1) / blockExtent.y);
        size_t levelSize = widthBlocks * heightBlocks * blockSize;
        m_levelSizes[level] = levelSize;
        m_sizeOfAllLevels += levelSize;
      }

      if (m_sizeOfAllLevels * (m_layers * m_faces) + m_dataOffset > m_fileSize)
        return false;

      closeHandle();

      return true;
    }

    FILE* openHandle() {
      assert(!m_filename.empty() && "DDS filename cannot be empty");
      if (m_file == nullptr) {
        errno = 0;
        m_file = std::fopen(m_filename.c_str(), "rb");

        if (m_file == nullptr) {
          if (errno == EMFILE) {
            throw DxvkError("Unable to open a DDS file: too many open files. "
                            "Please consider using AssetData::releaseSource() "
                            "method to keep the number of open files low.");
          }
        }
      }
      return m_file;
    }

    void closeHandle() {
      if (m_file) {
        std::fclose(m_file);
        m_file = nullptr;
      }
    }

  protected:
    std::string m_filename;

    long m_fileSize = 0;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_depth = 0;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    long m_dataOffset = 0;
    int m_levels = 0;
    int m_layers = 0;
    int m_faces = 0;
    std::array<size_t, 16> m_levelSizes = {};
    size_t m_sizeOfAllLevels = 0;

    void getDataPlacement(int layer, int face, int level, long& offset, size_t& size) const {
      int linearFace = layer * m_faces + face;
      offset = m_dataOffset + linearFace * m_sizeOfAllLevels;

      for (int i = 0; i < level; ++i)
        offset += m_levelSizes[i];

      size = m_levelSizes[level];
    }

  private:
    FILE* m_file = nullptr;
  };

  class DdsTextureData : public DdsFileParser, public AssetData {
    HANDLE m_hFile{};
    HANDLE m_hMapping{};
    const uint8_t* m_lpBaseAddress{};

  private:
    AssetType type() const {
      if (m_width > 1 && m_height == 1 && m_depth == 1) {
        return AssetType::Image1D;
      }
      if (m_depth > 1) {
        return AssetType::Image3D;
      }
      return AssetType::Image2D;
    }

  public:

    ~DdsTextureData() override {
      DdsTextureData::releaseSource();
    }

    const void* data(int layer, int level) override {
      long dataOffset;
      size_t dataSize;
      getDataPlacement(layer, 0, level, dataOffset, dataSize);

      if (m_fileSize < dataOffset + dataSize) {
        Logger::warn(str::format("Corrupted DDS file discovered: ", m_filename));
        return nullptr;
      }
      if (!m_hFile) {
        // NOTE: relaxing the share mode from 0 (no sharing) to FILE_SHARE_READ,
        //       so that other CreateFile calls in this process and other processes can still open it for reading;
        //       see the table in https://learn.microsoft.com/en-us/windows/win32/fileio/creating-and-opening-files
        HANDLE hFile = CreateFile(m_filename.c_str(),
                                  GENERIC_READ, FILE_SHARE_READ, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
          ONCE(Logger::warn(str::format("CreateFile fail (error=", GetLastError(), "): ", m_filename)));
          return nullptr;
        }
        assert(GetFileSize(hFile, NULL) == m_fileSize);

        HANDLE hMapping = CreateFileMapping(hFile, NULL,
                                            PAGE_READONLY, 0, 0, NULL);
        if (hMapping == NULL) {
          ONCE(Logger::warn(str::format("CreateFileMapping fail (error=", GetLastError(), "): ", m_filename)));
          CloseHandle(hFile);
          return nullptr;
        }

        LPVOID lpBaseAddress = MapViewOfFile(hMapping, 
                                             FILE_MAP_READ, 0, 0, 0);
        if (lpBaseAddress == NULL) {
          ONCE(Logger::warn(str::format("MapViewOfFile fail (error=", GetLastError(), "): ", m_filename)));
          CloseHandle(hMapping);
          CloseHandle(hFile);
          return nullptr;
        }

        m_hFile = hFile;
        m_hMapping = hMapping;
        m_lpBaseAddress = (const uint8_t*)lpBaseAddress;
      }

      return m_lpBaseAddress + dataOffset;
    }

    void evictCache(int layer, int level) override {
    }

    void releaseSource() override {
      if (m_hFile) {
        UnmapViewOfFile(m_lpBaseAddress);
        CloseHandle(m_hMapping);
        CloseHandle(m_hFile);
        m_hFile = nullptr;
        m_hMapping = nullptr;
        m_lpBaseAddress = nullptr;
      }

      closeHandle();
    }

    void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset64,
      size_t&   size) const override {
      long offset;
      getDataPlacement(layer, face, level, offset, size);
      offset64 = offset;
    }

    bool load(const std::string& filename) {
      if (parse(filename)) {
        m_info.type = type();
        m_info.compression = AssetCompression::None;
        m_info.format = m_format;
        m_info.extent = { m_width, m_height, m_depth };
        m_info.mipLevels = m_levels;
        m_info.mininumLevelsToUpload = std::min(int(kMipLevelsToCache), m_levels);
        m_info.numLayers = m_layers;
        m_info.lastWriteTime = std::filesystem::last_write_time(m_filename);
        m_info.filename = m_filename.c_str();

        m_hash = XXH64_std_hash<std::string> {}(m_filename);

        return true;
      }
      return false;
    }
  };

  class PackagedAssetData : public AssetData {
  public:
    PackagedAssetData() = delete;
    PackagedAssetData(const Rc<AssetPackage>& package, uint32_t assetIdx)
    : m_package(package)
    , m_assetIdx(assetIdx) {
      m_assetDesc = package->getAssetDesc(assetIdx);

      if (m_assetDesc == nullptr) {
        throw DxvkError("Asset description was not found in the package!");
      }

      auto dataBlobDesc = package->getDataBlobDesc(m_assetDesc->baseBlobIdx);

      if (dataBlobDesc == nullptr) {
        throw DxvkError("Asset data blob was not found in the package!");
      }

      m_info.type = type();
      m_info.compression = compression();
      m_info.format = static_cast<VkFormat>(m_assetDesc->format);
      m_info.extent = extent(0);
      m_info.mipLevels = m_assetDesc->numMips;
      // At the moment RTX IO can only load mip tail all at once,
      // i.e. we need to upload a mip amount of max(N, numTailMips).
      m_info.mininumLevelsToUpload = std::clamp<uint16_t>(m_assetDesc->numTailMips, 1, m_assetDesc->numMips);
      m_info.numLayers = m_assetDesc->arraySize;
      m_info.lastWriteTime = std::filesystem::last_write_time(m_package->getFilename());
      m_info.filename = m_package->getVolumeFilename(dataBlobDesc->volume);

      m_hash = XXH64_std_hash<std::string> {}(m_package->getFilename());
      m_hash ^= XXH3_64bits(&m_assetIdx, sizeof(m_assetIdx));
    }

    AssetType type() const {
      switch (m_assetDesc->type) {
      case AssetPackage::AssetDesc::Type::BUFFER:
        return AssetType::Buffer;
      case AssetPackage::AssetDesc::Type::IMAGE_1D:
        return AssetType::Image1D;
      case AssetPackage::AssetDesc::Type::IMAGE_2D:
      case AssetPackage::AssetDesc::Type::IMAGE_CUBE:
        return AssetType::Image2D;
      case AssetPackage::AssetDesc::Type::IMAGE_3D:
        return AssetType::Image3D;
      case AssetPackage::AssetDesc::Type::UNKNOWN:
      default:
        break;
      }

      assert(0 && "Unknown asset type");

      return AssetType::Unknown;
    }

    AssetCompression compression() const {
      const auto blobDesc = m_package->getDataBlobDesc(
        m_assetDesc->baseBlobIdx);

      // We support only the GDeflate compression method atm
      return blobDesc->compression != 0 ?
        AssetCompression::GDeflate : AssetCompression::None;
    }

    VkExtent3D extent(int level) const {
      if (type() == AssetType::Buffer) {
        return VkExtent3D { m_assetDesc->size, 1, 1 };
      }

      return VkExtent3D{
        std::max<uint32_t>(m_assetDesc->width >> level, 1u),
        std::max<uint32_t>(m_assetDesc->height >> level, 1u),
        std::max<uint32_t>(m_assetDesc->depth >> level, 1u)
      };
    }

    const void* data(int layer, int level) override {
      const uint32_t blobIdx = getBlobIndex(layer, 0, level);
      const bool isTail = isTailLevel(level);

      const auto it = m_data.find(blobIdx);
      if (it != m_data.end() && !it->second.empty()) {
        const uint8_t* data = it->second.data();

        if (type() == AssetType::Buffer || !isTail) {
          return data;
        }

        // For images we need to handle the tail
        return data + getTailOffset(level);
      }

      if (auto blobDesc = m_package->getDataBlobDesc(blobIdx)) {
#ifdef WITH_RTXIO
        if (blobDesc->compression != RTXIO_COMPRESSION_NONE &&
          blobDesc->compression != RTXIO_COMPRESSION_GDEFLATE_1_0) {
          throw DxvkError("Unsupported asset data compression method discovered.");
        }
#else
        if (blobDesc->compression != 0) {
          throw DxvkError("Compressed data blobs cannot be read without RTX IO.");
        }
#endif

        std::vector<uint8_t> data(blobDesc->size);
        m_package->readDataBlob(blobIdx, data.data(), data.size());

#ifdef WITH_RTXIO
        if (blobDesc->compression == RTXIO_COMPRESSION_GDEFLATE_1_0) {
          size_t uncompressedSize;

          if (type() != AssetType::Buffer) {
            if (!isTail) {
              // Calculate loose image layer uncompressed size
              uncompressedSize = getImageLayerSize(m_info.format, m_info.extent, level);
            } else {
              // Handle tail images
              buildTailLayout();
              uncompressedSize = getTailOffset(m_assetDesc->numMips);
            }
          } else {
            uncompressedSize = m_info.extent.width;
          }

          std::vector<uint8_t> uncompressed(uncompressedSize);

          if (!rtxioDecompress(uncompressed.data(), uncompressedSize, data.data(), data.size(), 8)) {
            throw DxvkError("Unable to decompress asset data.");
          }

          data = std::move(uncompressed);
        }
#endif

        const auto [insertedIterator, insertionSuccessful] = m_data.try_emplace(blobIdx, std::move(data));

        // Note: Element with this blobIdx shouldn't exist due to being checked earlier, and because evicting the
        // cache erases the element fully. If in the future the vector is kept around in the map to reuse its memory
        // (due to using clear rather than freeing the memory fully) then this logic will have to change.
        assert(insertionSuccessful);

        const uint8_t* rawData = insertedIterator->second.data();

        if (type() == AssetType::Buffer || !isTail) {
          return rawData;
        }

        buildTailLayout();
        return rawData + getTailOffset(level);
      }

      return nullptr;
    }

    void evictCache(int layer, int level) override {
      const uint32_t blobIdx = getBlobIndex(layer, 0, level);

      // Note: Release the vector stored at the given blob index to free up its memory fully.
      m_data.erase(blobIdx);
    }

    void releaseSource() override {
    }

    void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const override {

      uint32_t blobIdx = getBlobIndex(layer, face, level);

      if (auto blobDesc = m_package->getDataBlobDesc(blobIdx)) {
        offset = blobDesc->offset;
        size = blobDesc->size;
        return;
      }

      assert(0 && "Data blob was not found!");
    }

  private:
    // Returns the byte offset of the given level inside a layer's tail blob.
    // Passing numMips returns the size of the whole tail blob.
    // Each layer has its own tail blob that holds the tail mips tightly packed,
    // so the layout is the same for all layers.
    size_t getTailOffset(uint32_t level) const {
      return m_tailLayout[level - m_info.tailStartLevel()];
    }

    void buildTailLayout() {
      if (!m_tailLayout.empty()) {
        return;
      }

      m_tailLayout.reserve(m_assetDesc->numTailMips + 1);

      uint32_t tailImageOffset = 0;
      for (uint32_t n = m_info.tailStartLevel(); n < m_assetDesc->numMips; n++) {
        m_tailLayout.push_back(tailImageOffset);
        tailImageOffset += getImageLayerSize(m_info.format, m_info.extent, n);
      }

      // The last entry is the total size of the tail blob.
      m_tailLayout.push_back(tailImageOffset);
    }

    bool isTailLevel(int level) const {
      return type() != AssetType::Buffer && level >= m_assetDesc->numMips - m_assetDesc->numTailMips;
    }

    uint32_t getBlobIndex(int       layer,
                          int       face,
                          int       level) const {
      if (m_assetDesc->type == AssetPackage::AssetDesc::Type::BUFFER) {
        return m_assetDesc->baseBlobIdx;
      }

      if (m_assetDesc->type == AssetPackage::AssetDesc::Type::IMAGE_CUBE) {
        layer = layer * 6 + face;
      }

      const uint32_t numLooseMips =
        m_assetDesc->numMips - m_assetDesc->numTailMips;

      // Loose blobs are stored layer-major and followed by one tail blob per layer.
      if (level >= numLooseMips) {
        return m_assetDesc->tailBlobIdx + layer;
      }

      return m_assetDesc->baseBlobIdx + level + layer * numLooseMips;
    }

    Rc<AssetPackage> m_package;
    const AssetPackage::AssetDesc* m_assetDesc = nullptr;
    uint32_t m_assetIdx;

    std::unordered_map<uint32_t, std::vector<uint8_t>> m_data;
    std::vector<uint32_t> m_tailLayout;
  };

  AssetDataManager::AssetDataManager() {
  }

  AssetDataManager::~AssetDataManager() {
    FileWatch::get().removeAllWatchDirs();
  }

  void AssetDataManager::setSearchPaths(const std::vector<std::filesystem::path>& paths) {
    // Normalise paths (standardize case and trailing separator)
    auto normalise = [](const std::filesystem::path& p) {
      auto s = std::filesystem::absolute(p).make_preferred().string();
      std::for_each(s.begin(), s.end(), [](char& c) { c = tolower(c); });
      if (!s.empty() && s.back() != '\\' && s.back() != '/') {
        s.push_back('\\');
      }
      return s;
    };

    // deduplicate keeping the last occurrence of each path (last = highest
    // precedence within a mod). Process in reverse so earlier occurrences are the ones skipped.
    std::unordered_set<std::string> seen;
    seen.reserve(paths.size());
    std::vector<std::string> normalised;
    normalised.reserve(paths.size());
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
      auto s = normalise(*it);
      if (seen.insert(s).second) {
        normalised.push_back(std::move(s));
      }
    }
    std::reverse(normalised.begin(), normalised.end());

    // Held across the mounting below: findAsset must never observe a path whose packages
    // have not been attached yet, or an asset under a freshly published path resolves as
    // missing on another mod's worker.
    std::lock_guard<std::mutex> lock(m_searchPathMutex);

    // Move existing entries into a map for O(1) carry-over lookup.
    // Entries that survive into rebuilt keep their mounted packages; entries that
    // fall off the end of oldEntries are destroyed here, releasing their packages.
    std::unordered_map<std::string, SearchPathEntry> oldEntries;
    oldEntries.reserve(m_searchPaths.size());
    for (auto& e : m_searchPaths) {
      oldEntries.emplace(e.path, std::move(e));
    }
    m_searchPaths.clear();

    std::vector<SearchPathEntry> rebuilt;
    rebuilt.reserve(normalised.size());

    for (auto& searchPath : normalised) {
      auto it = oldEntries.find(searchPath);
      if (it != oldEntries.end()) {
        rebuilt.push_back(std::move(it->second));
        oldEntries.erase(it);
        continue;
      }

      // New path: mount packages inline so we hold a direct ref without a second search.
      Logger::info(str::format("Adding asset search path: ", searchPath));
      SearchPathEntry& entry = rebuilt.emplace_back(SearchPathEntry { searchPath, PackageSet {} });

      // Find the packages
      std::error_code ec;
      std::vector<std::filesystem::directory_entry> packages;
      for (const auto& entry : std::filesystem::directory_iterator(searchPath, ec)) {
        if (entry.path().extension() == ".pkg" || entry.path().extension() == ".rtxio") {
          packages.push_back(entry);
        }
      }

      // error_code overload: a mod directory that vanishes mid-reload only warns here.
      if (ec) {
        Logger::warn(str::format("Could not enumerate packages under: ", searchPath,
                                 " (", ec.message(), ")"));
      }

      // Sort packages in natural order
      std::sort(packages.begin(), packages.end(), [](const auto& a, const auto& b) {
        return ::StrCmpLogicalW(a.path().filename().wstring().c_str(),
                                b.path().filename().wstring().c_str()) < 0;
      });

      for (const auto& fsEntry : packages) {
        const auto packagePath = fsEntry.path().string();
        Rc<AssetPackage> package = new AssetPackage(packagePath);
        if (package->initialize()) {
          entry.packages.emplace_back(std::move(package));
          Logger::info(str::format("Mounted a package at: ", fsEntry.path()));
        } else {
          Logger::warn(str::format("Corrupted package discovered at: ", fsEntry.path()));
        }
      }
    }

    for (const auto& [path, _] : oldEntries) {
      Logger::info(str::format("Removing asset search path: ", path));
    }
    // oldEntries goes out of scope here, releasing the stale PackageSets.

    m_searchPaths = std::move(rebuilt);

    // Arm all entries, not just new ones. installDir is idempotent for already-watched dirs.
    for (const auto& entry : m_searchPaths) {
      FileWatch::get().installDir(entry.path.c_str());
    }
  }

  void AssetDataManager::mountPackage(const std::string& path, SearchPathPlacement placement) {
    // Make path preferred and lowercase
    auto packagePath = std::filesystem::absolute(path).make_preferred().string();
    std::for_each(packagePath.begin(), packagePath.end(), [](char& c) { c = tolower(c); });

    auto packageSearchPath = std::filesystem::path(packagePath).remove_filename().string();

    SearchPathEntry* entry = nullptr;

    std::lock_guard<std::mutex> lock(m_searchPathMutex);

    for (auto& searchPathEntry : m_searchPaths) {
      if (searchPathEntry.path == packageSearchPath) {
        for (const auto& package : searchPathEntry.packages) {
          if (package->getFilename() == packagePath) {
            // already mounted
            return;
          }
        }
        entry = &searchPathEntry;
        break;
      }
    }

    const bool isNewEntry = entry == nullptr;

    if (entry == nullptr) {
      m_searchPaths.push_back({});
      entry = &m_searchPaths.back();
    }

    entry->path = packageSearchPath;

    Rc<AssetPackage> package = new AssetPackage(packagePath);
    if (package->initialize()) {
      if (placement == SearchPathPlacement::Bottom) {
        entry->packages.emplace_front(std::move(package));
      } else {
        entry->packages.emplace_back(std::move(package));
      }
      Logger::info(str::format("Mounted a package at: ", packagePath));
    } else {
      Logger::warn(str::format("Corrupted package discovered at: ", packagePath));
      if (isNewEntry) {
        m_searchPaths.pop_back();
        return;
      }
    }

    FileWatch::get().installDir(packageSearchPath.c_str());
  }

  Rc<AssetData> AssetDataManager::findAsset(const std::string& filename, bool allowOnlyPartialDdsLoader) {
    ScopedCpuProfileZone();

    if (filename.empty()) {
      return {};
    }

    const char* extension = strrchr(filename.c_str(), '.');
    const bool isDDS = extension ? _stricmp(extension, ".dds") == 0 : false;

    if (isDDS && RtxOptions::usePartialDdsLoader()) {
      Rc<DdsTextureData> dds = new DdsTextureData;
      if (dds->load(filename)) {
        return dds;
      }
    }

    // Held across the whole walk: a USD mod rebuild finishing on the render thread
    // mutates m_searchPaths, and this runs on another mod's rebuild worker. The
    // empty() early-out folded into the loop rather than read outside the lock.
    std::lock_guard<std::mutex> lock(m_searchPathMutex);
    // Highest precedence last, so walk the list backwards
    for (auto itBase = m_searchPaths.rbegin(); itBase != m_searchPaths.rend(); ++itBase) {
      const auto& basePath = itBase->path;

      std::string relativePath = filename;

      // Check for absolute path and trim the base part
      if ((filename[1] == ':' || filename[0] == '\\') && basePath.length() < filename.length()) {
        relativePath = filename.substr(basePath.length());
      }

      auto& packages = itBase->packages;

      // Iterate package set in reverse alphabetical order
      for (auto it = packages.rbegin(); it != packages.rend(); ++it) {
        uint32_t assetIdx = (*it)->findAsset(relativePath);
        if (AssetPackage::kNoAssetIdx != assetIdx) {
          return new PackagedAssetData(*it, assetIdx);
        }
      }
    }

    // NOTE: 'allowOnlyPartialDdsLoader' can be set to true,
    //       if a raw file is required for file streaming.
    // NOTE: GliTextureData fully loads a file into RAM,
    //       so it's better to avoid it for texture streaming
    if (allowOnlyPartialDdsLoader) {
      return nullptr;
    }

    // Fallback to GLI
    Rc<GliTextureData> gli = new GliTextureData;
    if (gli->load(filename)) {
      Logger::warn(str::format("The GLI library was used to load image file '", filename,
                               "'. Image data will reside in CPU memory!"));
      return gli;
    }

    return nullptr;
  }

} // namespace dxvk
