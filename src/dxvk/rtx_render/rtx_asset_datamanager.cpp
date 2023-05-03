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
#include "rtx_asset_datamanager.h"
#include "rtx_utils.h"
#include "rtx_options.h"
#include "rtx_asset_package.h"
#include "rtx_game_capturer_paths.h"
#include "rtx_io.h"
#include "dxvk_scoped_annotation.h"
#include <gli/gli.hpp>

namespace dxvk {
  
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
        m_info.looseLevels = m_texture.levels();
        m_info.numLayers = m_texture.layers();
        m_info.filename = m_filename.c_str();

        m_hash = XXH64_std_hash<std::string> {}(m_filename);

        return true;
      }

      return false;
    }

    const void* data(int layer, int level) override {
      return m_texture.data(layer, 0, level);
    }

    void evictCache() override {
    }

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
        m_file = gli::detail::open_file(m_filename.c_str(), "rb");
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
    std::array<size_t, 16> m_levelSizes;
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
    std::unordered_map<int, std::vector<uint8_t>> m_data;

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
    }

    const void* data(int layer, int level) override {
      int key = (layer * m_faces + 0) * m_levels + level;
      const auto& it = m_data.find(key);
      if (it != m_data.end())
        return it->second.data();

      long dataOffset;
      size_t dataSize;
      getDataPlacement(layer, 0, level, dataOffset, dataSize);

      if (m_fileSize < dataOffset + dataSize)
        return nullptr;

      auto file = openHandle();

      std::vector<uint8_t> data;
      std::fseek(file, dataOffset, SEEK_SET);
      data.resize(dataSize);
      std::fread(data.data(), dataSize, 1, file);

      const void* rawData = data.data();
      m_data[key] = std::move(data);
      return rawData;
    }

    void evictCache() override {
      m_data.clear();
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
        m_info.looseLevels = m_levels;
        m_info.numLayers = m_layers;
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

      m_info.type = type();
      m_info.compression = compression();
      m_info.format = static_cast<VkFormat>(m_assetDesc->format);
      m_info.extent = extent(0);
      m_info.mipLevels = m_assetDesc->numMips;
      m_info.looseLevels = m_assetDesc->numMips - m_assetDesc->numTailMips;
      m_info.numLayers = m_assetDesc->arraySize;
      m_info.filename = m_package->getFilename().c_str();

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
        return VkExtent3D { m_assetDesc->size, 0, 1 };
      }

      return VkExtent3D{
        std::max<uint32_t>(m_assetDesc->width >> level, 1u),
        std::max<uint32_t>(m_assetDesc->height >> level, 1u),
        std::max<uint32_t>(m_assetDesc->depth >> level, 1u)
      };
    }

    const void* data(int layer, int level) override {
      uint32_t blobIdx = getBlobIndex(layer, 0, level);

      const auto& it = m_data.find(blobIdx);
      if (it != m_data.end())
        return it->second.data();

      if (auto blobDesc = m_package->getDataBlobDesc(blobIdx)) {
        if (blobDesc->compression != 0) {
          throw DxvkError("Compressed data blobs are not supported for CPU readback.");
        }

        std::vector<uint8_t> data(blobDesc->size);
        m_package->readDataBlob(blobIdx, data.data(), data.size());

        const void* rawData = data.data();
        m_data[blobIdx] = std::move(data);
        return rawData;
      }

      return nullptr;
    }

    void evictCache() override {
      m_data.clear();
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
      const uint32_t baseBlobIdx =
        level >= numLooseMips ? m_assetDesc->tailBlobIdx :
        level + m_assetDesc->baseBlobIdx;

      return baseBlobIdx + layer * numLooseMips;
    }

    Rc<AssetPackage> m_package;
    const AssetPackage::AssetDesc* m_assetDesc = nullptr;
    uint32_t m_assetIdx;

    std::unordered_map<uint32_t, std::vector<uint8_t>> m_data;
  };

  AssetDataManager::AssetDataManager() {
  }

  AssetDataManager::~AssetDataManager() {
  }

  void AssetDataManager::initialize(const std::filesystem::path& path) {
    if (m_package != nullptr) {
      return;
    }

    m_basePath = path;
    m_basePath.make_preferred();

    if (RtxIo::enabled()) {
      std::string packagePath;

      // Find the pkg (if it exists)
      for (const auto& entry : std::filesystem::directory_iterator(path))
        if (entry.path().extension() == ".pkg")
          packagePath = entry.path().string();

      // Try to initialize the replacements packages
      Rc<AssetPackage> package = new AssetPackage(packagePath);

      if (package->initialize()) {
        m_package = std::move(package);
      }
    }
  }

  Rc<AssetData> AssetDataManager::findAsset(const std::string& filename) {
    ScopedCpuProfileZone();

    const char* extension = strrchr(filename.c_str(), '.');
    const bool isDDS = extension ? _stricmp(extension, ".dds") == 0 : false;
    // Only allow DDS even though GLI supports KTX and KMG formats as well: we haven't tested those.
    const bool isSupported = isDDS;

    if (!isSupported) {
      Logger::err(str::format("Unsupported image file format, please convert to DDS using Remix Export: ", filename));
      return nullptr;
    }

    if (RtxIo::enabled() && m_package != nullptr) {
      auto relativePath = std::filesystem::relative(filename, m_basePath);
      uint32_t assetIdx = m_package->findAsset(relativePath.string());
      if (AssetPackage::kNoAssetIdx != assetIdx) {
        return new PackagedAssetData(m_package, assetIdx);
      }
    }

    if (isDDS && RtxOptions::Get()->usePartialDdsLoader()) {
      Rc<DdsTextureData> dds = new DdsTextureData;
      if (dds->load(filename)) {
        return dds;
      }
    }

    // Fallback to GLI
    Rc<GliTextureData> gli = new GliTextureData;
    if (gli->load(filename)) {
      Logger::warn(str::format("The GLI library was used to load image file '", filename,
                               "'. Image data will reside in CPU memory!"));
      return gli;
    }

    Logger::err(str::format("Failed to load image file: ", filename));
    return nullptr;
  }

} // namespace dxvk
