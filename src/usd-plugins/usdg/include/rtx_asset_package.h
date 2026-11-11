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

#include <assert.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../util/log/log.h"
#include "../../../util/log/log_debug.h"
#include "../../../util/util_string.h"
#include "usdg.h"

#ifdef WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#define fopen_s(pFile,filename,mode) (((*(pFile))=fopen((filename),(mode)))==NULL)
#endif

class AssetPackageWriter;

namespace dxvk {

  // A trivial assets package file container
  class AssetPackage {
  public:
    static constexpr uint32_t kMagic = 0xbaadd00d;
    static constexpr uint32_t kVersion = 2;
    static constexpr uint32_t kNoAssetIdx = ~0;
    static constexpr uint32_t kMaxVolumes = 1 << 4;

    struct Header {
      uint32_t magic;
      uint32_t version;
      uint64_t dictOffset;
    };

#ifdef USDG_API
    using AssetDesc = usdg::AssetDesc;
    using BlobDesc = usdg::BlobDesc;
#else
    struct AssetDesc {
      enum class Type : uint8_t {
        UNKNOWN,
        IMAGE_1D,
        IMAGE_2D,
        IMAGE_3D,
        IMAGE_CUBE,
        BUFFER,
        RAW_FILE,
      };

      uint16_t unused;

      Type type;
      uint8_t format;

      union {
        uint32_t size;
        struct {
          uint16_t width;
          uint16_t height;
        };
      };
      uint16_t depth;

      uint16_t numMips;
      uint16_t numTailMips;
      uint16_t arraySize;

      uint16_t baseBlobIdx;
      uint16_t tailBlobIdx;
    };

    static_assert(sizeof(AssetDesc) == 20, "Asset description structure size overrun!");

    struct BlobDesc {
      uint64_t offset : 40;
      uint64_t compression : 4;
      uint64_t volume : 4;
      uint64_t flags : 8;

      uint32_t size;
      uint32_t crc32;
    };

    static_assert(sizeof(BlobDesc) == 16, "Blob description structure size overrun!");
#endif

    AssetPackage() = default;
    explicit AssetPackage(const std::string& filename)
      : m_baseFilename { filename } { }

    ~AssetPackage() {
      closeAllFiles();
    }

    bool initialize(const char* filename = nullptr) {
      if (m_baseFilename.empty() && nullptr == filename)
        return false;

      closeAllFiles();

      if (m_baseFilename.empty() && nullptr != filename)
        m_baseFilename = filename;

      if (openFile()) {
        auto handle = m_handle[0];

        Header header { 0 };
        if (sizeof(header) != fread(&header, 1, sizeof(header), handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        if (header.magic != kMagic) {
          Logger::err(str::format("File ", m_baseFilename, " is not an asset package."));
          return false;
        }

        if (header.version > kVersion) {
          Logger::err(str::format("Asset package ", m_baseFilename, " version mismatch. "
                                  "Got: ", header.version, ", expected: ", kVersion));
          return false;
        }

        if (0 != fseek64(handle, header.dictOffset, SEEK_SET)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        uint16_t assetCount;
        if (1 != fread(&assetCount, 2, 1, handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        uint16_t blobCount;
        if (1 != fread(&blobCount, 2, 1, handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        m_assetDescs.resize(assetCount);
        if (assetCount != fread(m_assetDescs.data(), sizeof(AssetDesc), assetCount, handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        m_blobDescs.resize(blobCount);
        if (blobCount != fread(m_blobDescs.data(), sizeof(BlobDesc), blobCount, handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        size_t nameTableOffset = ftell64(handle);
        fseek64(handle, 0, SEEK_END);
        size_t nameTableSize = ftell64(handle) - nameTableOffset;
        fseek64(handle, nameTableOffset, SEEK_SET);

        std::unique_ptr<char[]> names(new char[nameTableSize]);

        if (1 != fread(names.get(), nameTableSize, 1, handle)) {
          Logger::err(str::format("Malformed asset package ", m_baseFilename));
          return false;
        }

        closeFile();

        auto namesPtr = names.get();
        for (uint32_t n = 0; n < static_cast<uint32_t>(m_assetDescs.size()); n++) {
          m_nameHash.emplace(namesPtr, n);

          // Convert \ to /
          uint32_t p = 0;
          while (namesPtr[p]) {
            if (namesPtr[p] == '\\'){
              namesPtr[p] = '/';
            }
            ++p;
          }
          m_nameHash.emplace(namesPtr, n);

          namesPtr += strlen(namesPtr) + 1;
        }

        return true;
      }

      return false;
    }

    bool openFile(uint32_t volume=0) {
      assert(volume < kMaxVolumes);

      auto& handle = m_handle[volume];

      if (nullptr == handle) {
        if (0 != fopen_s(&handle, getVolumeFilename(volume).c_str(), "rb")) {
          Logger::info(str::format("Unable to open package file ", m_baseFilename));
        }
      }

      return nullptr != handle;
    }

    bool createFile(uint32_t volume=0) {
      assert(volume < kMaxVolumes);

      auto& handle = m_handle[volume];

      if (nullptr == handle) {
        if (0 != fopen_s(&handle, getVolumeFilename(volume).c_str(), "wb")) {
          Logger::info(str::format("Unable to open package file ", m_baseFilename));
        }
      }

      return nullptr != handle;
    }

    void closeFile(uint32_t volume=0) {
      if (m_handle[volume]) {
        fclose(m_handle[volume]);
        m_handle[volume] = nullptr;
      }
    }

    void closeAllFiles() {
      for (uint32_t volume = 0; volume < kMaxVolumes; volume++) {
        closeFile(volume);
      }
    }

    uint32_t getAssetCount() const {
      return static_cast<uint32_t>(m_assetDescs.size());
    }

    const AssetDesc* getAssetDesc(uint32_t idx) const {
      if (idx >= getAssetCount())
        return nullptr;

      return &m_assetDescs[idx];
    }

    const std::string& getAssetName(uint32_t idx) const {
      static std::string invalidName;

      if (idx >= getAssetCount())
        return invalidName;

      for (auto& [name, assetIdx] : m_nameHash) {
        if (idx == assetIdx) {
          return name;
        }
      }

      return invalidName;
    }

    const BlobDesc* getDataBlobDesc(uint32_t idx) const {
      if (idx >= m_blobDescs.size())
        return nullptr;

      return &m_blobDescs[idx];
    }

    size_t readDataBlob(uint32_t idx, void* out, size_t outSize) {
      if (auto blobDesc = getDataBlobDesc(idx)) {
        if (outSize < blobDesc->size)
          return 0;

        if (!openFile(blobDesc->volume))
          return 0;

        auto handle = m_handle[blobDesc->volume];

        if (0 == fseek64(handle, blobDesc->offset, SEEK_SET))
          return fread(out, 1, blobDesc->size, handle);
      }

      return 0;
    }

    size_t getDataSize() {
      if (!openFile())
        return 0;

      auto handle = m_handle[0];

      if (handle && 0 == fseek64(handle, 0, SEEK_SET)) {
        Header header { 0 };
        if (0 == fread(&header, 1, sizeof(header), handle))
          return 0;

        return header.dictOffset;
      }

      return 0;
    }

    uint32_t findAsset(const std::string& filename) const {
      auto it = m_nameHash.find(filename);

      if (it != m_nameHash.end()) {
        return it->second;
      }

      return kNoAssetIdx;
    }

    const std::string& getBaseFilename() const {
      return m_baseFilename;
    }

    const std::string& getVolumeFilename(uint32_t volume=0) {
      if (volume == 0) {
        return getBaseFilename();
      }

      assert(volume < kMaxVolumes);

      auto& volumeFilename = m_volumeFilename[volume];

      if (volumeFilename.empty()) {
        volumeFilename = m_baseFilename;

        char vidx = static_cast<char>(volume);
        char ext[4] = { '.', '0' + vidx / 10, '0' + vidx % 10, 0 };

        volumeFilename += ext;
      }

      return volumeFilename;
    }

    FILE* getVolumeHandle(uint32_t volume=0) const {
      assert(volume < kMaxVolumes);
      return m_handle[volume];
    }

  private:
    // Reserves an asset index for an asset name.
    // Returns true when asset already exists in the package.
    bool reserveAsset(const std::string& name, uint32_t& index) {
      if (!name.empty() && m_nameHash.count(name) > 0) {
        index = m_nameHash[name];
        return true;
      }

      index = static_cast<uint32_t>(m_assetDescs.size());
      m_assetDescs.resize(index + 1);

      if (!name.empty()) {
        m_nameHash[name] = index;
      }

      return false;
    }

    std::string m_baseFilename;

    std::string m_volumeFilename[kMaxVolumes];
    FILE* m_handle[kMaxVolumes] = { nullptr };

    std::vector<AssetDesc> m_assetDescs;
    std::vector<BlobDesc> m_blobDescs;

    std::unordered_map<std::string, uint32_t> m_nameHash;

    friend class AssetPackageWriter;
  };

} // namespace dxvk

#undef fseek64
#ifndef WIN32
#undef fopen_s
#endif
