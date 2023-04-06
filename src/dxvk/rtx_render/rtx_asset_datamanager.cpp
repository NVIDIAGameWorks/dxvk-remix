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
#include "Tracy.hpp"
#include <gli/gli.hpp>

namespace dxvk {
  
  class GliTextureData : public AssetData {
  public:
    bool load(const char* filename) {
      m_texture = gli::load(filename);
      return !m_texture.empty();
    }

    AssetType type() const override {
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

    AssetCompression compression() const override {
      return AssetCompression::None;
    }

    VkFormat format() const override {
      return static_cast<VkFormat>(m_texture.format());
    }

    VkExtent3D extent(int level) const override {
      const auto ext = m_texture.extent(level);
      return VkExtent3D {
        static_cast<uint32_t>(ext.x),
        static_cast<uint32_t>(ext.y),
        static_cast<uint32_t>(ext.z),
      };
    }

    int levels() const override {
      return m_texture.levels();
    }

    int looseLevels() const override {
      return m_texture.levels();
    }

    int layers() const override {
      return m_texture.layers();
    }

    const void* data(int layer, int level) override {
      return m_texture.data(layer, 0, level);
    }

    void evictCache() override {
    }

    const char* filename() const override {
      assert(0 && "Data placement interface is not supported by GliTextureData");
      return nullptr;
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
  };

  class DdsFileParser {
  public:
    virtual ~DdsFileParser() {
      closeHandle();
    }

    bool parse(const char* filename) {
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

  public:

    ~DdsTextureData() override {
    }

    AssetType type() const override {
      if (m_width > 1 && m_height == 1 && m_depth == 1) {
        return AssetType::Image1D;
      }
      if (m_depth > 1) {
        return AssetType::Image3D;
      }
      return AssetType::Image2D;
    }

    AssetCompression compression() const override {
      return AssetCompression::None;
    }

    VkFormat format() const override {
      return m_format;
    }

    VkExtent3D extent(int level) const override {
      return VkExtent3D{
        std::max(m_width >> level, 1u),
        std::max(m_height >> level, 1u),
        std::max(m_depth >> level, 1u)
      };
    }

    int levels() const override {
      return m_levels;
    }

    int looseLevels() const override {
      return m_levels;
    }

    int layers() const override {
      return m_layers;
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

    const char* filename() const override {
      return m_filename.c_str();
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

    bool load(const char* filename) {
      return parse(filename);
    }
  };

  class PackagedAssetData : public AssetData {
  public:
    PackagedAssetData() = delete;
    PackagedAssetData(Rc<AssetPackage>& package, uint32_t assetIdx)
    : m_package(package)
    , m_assetIdx(assetIdx) {
      m_assetDesc = package->getAssetDesc(assetIdx);

      if (m_assetDesc == nullptr) {
        throw DxvkError("Asset description was not found in the package!");
      }
    }

    AssetType type() const override {
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

    AssetCompression compression() const override {
      const auto blobDesc = m_package->getDataBlobDesc(
        m_assetDesc->baseBlobIdx);

      // We support only the GDeflate compression method atm
      return blobDesc->compression != 0 ?
        AssetCompression::GDeflate : AssetCompression::None;
    }

    VkFormat format() const override {
      return static_cast<VkFormat>(m_assetDesc->format);
    }

    VkExtent3D extent(int level) const override {
      if (type() == AssetType::Buffer) {
        return VkExtent3D { m_assetDesc->size, 0, 1 };
      }

      return VkExtent3D{
        std::max<uint32_t>(m_assetDesc->width >> level, 1u),
        std::max<uint32_t>(m_assetDesc->height >> level, 1u),
        std::max<uint32_t>(m_assetDesc->depth >> level, 1u)
      };
    }

    int levels() const override {
      return m_assetDesc->numMips;
    }

    int looseLevels() const override {
      return m_assetDesc->numMips - m_assetDesc->numTailMips;
    }

    int layers() const override {
      return m_assetDesc->arraySize;
    }

    const void* data(int layer, int level) override {
      // TODO
      Logger::err("Direct access to asset data is "
                  "not supported by PackagedAssetData class.");
      return nullptr;
    }

    void evictCache() override { }

    const char* filename() const override {
      return m_package->getFilename().c_str();
    }

    void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const override {
      if (m_assetDesc->type == AssetPackage::AssetDesc::Type::IMAGE_CUBE) {
        layer = layer * 6 + face;
      }

      const uint32_t numLooseMips =
        m_assetDesc->numMips - m_assetDesc->numTailMips;
      const uint32_t baseBlobIdx =
        level >= numLooseMips ? m_assetDesc->tailBlobIdx :
        level + m_assetDesc->baseBlobIdx;

      const uint32_t blobIdx = baseBlobIdx + layer * numLooseMips;

      if (auto blobDesc = m_package->getDataBlobDesc(blobIdx)) {
        offset = blobDesc->offset;
        size = blobDesc->size;
        return;
      }

      assert(0 && "Data blob was not found!");
    }

  private:
    Rc<AssetPackage> m_package;
    const AssetPackage::AssetDesc* m_assetDesc = nullptr;
    uint32_t m_assetIdx;
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

  Rc<AssetData> AssetDataManager::find(const char* filename) {
    ZoneScoped;

    const char* extension = strrchr(filename, '.');
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
