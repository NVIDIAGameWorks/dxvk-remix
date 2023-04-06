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

#include <memory>
#include <string>

#include "../../util/rc/util_rc.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#ifdef WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko64
#define ftell64 ftello64
#define fopen_s(pFile,filename,mode) (((*(pFile))=fopen((filename),(mode)))==NULL)
#endif

namespace dxvk {

  // A trivial assets package file container
  class AssetPackage : public RcObject {
  public:
    static constexpr uint32_t kMagic = 0xbaadd00d;
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kNoAssetIdx = ~0;

    struct Header {
      uint32_t magic;
      uint32_t version;
      uint64_t dictOffset;
    };

    struct AssetDesc {
      enum class Type : uint8_t {
        UNKNOWN,
        IMAGE_1D,
        IMAGE_2D,
        IMAGE_3D,
        IMAGE_CUBE,
        BUFFER,
      };

      uint16_t nameIdx;

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
      uint64_t compression : 8;
      uint64_t flags : 8;

      uint32_t size;
      uint32_t crc32;
    };

    static_assert(sizeof(BlobDesc) == 16, "Blob description structure size overrun!");

    AssetPackage() = default;
    explicit AssetPackage(const std::string& filename)
      : m_filename { filename } { }

    ~AssetPackage() {
      closeFileHandle();
    }

    bool initialize(const char* filename = nullptr) {
      if (m_filename.empty() && nullptr == filename)
        return false;

      closeFileHandle();

      if (m_filename.empty() && nullptr != filename)
        m_filename = filename;

      if (openFileHandle()) {
        Header header { 0 };
        if (sizeof(header) != fread(&header, 1, sizeof(header), m_handle)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        if (header.magic != kMagic) {
          Logger::err(str::format("File ", m_filename, " is not an asset package."));
          return false;
        }

        if (header.version != kVersion) {
          Logger::err(str::format("Asset package ", m_filename, " version mismatch. "
                                  "Got: ", header.version, ", expected: ", kVersion));
          return false;
        }

        if (0 != fseek64(m_handle, header.dictOffset, SEEK_SET)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        if (1 != fread(&m_assetCount, 2, 1, m_handle)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        if (1 != fread(&m_blobCount, 2, 1, m_handle)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        const size_t dictSize =
          m_assetCount * sizeof(AssetDesc) + m_blobCount * sizeof(BlobDesc);

        m_metadata.reset(new uint8_t[dictSize]);

        if (dictSize != fread(m_metadata.get(), 1, dictSize, m_handle)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        size_t nameTableOffset = ftell64(m_handle);
        fseek64(m_handle, 0, SEEK_END);
        size_t nameTableSize = ftell64(m_handle) - nameTableOffset;
        fseek64(m_handle, nameTableOffset, SEEK_SET);

        std::unique_ptr<char[]> names(new char[nameTableSize]);

        if (1 != fread(names.get(), nameTableSize, 1, m_handle)) {
          Logger::err(str::format("Malformed asset package ", m_filename));
          return false;
        }

        closeFileHandle();

        auto namesPtr = names.get();
        for (uint32_t n = 0; n < m_assetCount; n++) {
          m_nameHash.emplace(namesPtr, n);
          namesPtr += strlen(namesPtr) + 1;
        }

        return true;
      }

      return false;
    }

    bool openFileHandle() {
      if (nullptr == m_handle) {
        if (0 != fopen_s(&m_handle, m_filename.c_str(), "rb")) {
          Logger::info(str::format("Unable to open package file ", m_filename));
        }
      }

      return nullptr != m_handle;
    }

    void closeFileHandle() {
      if (m_handle) {
        fclose(m_handle);
        m_handle = nullptr;
      }
    }

    uint32_t getAssetCount() const {
      return m_assetCount;
    }

    const AssetDesc* getAssetDesc(uint32_t idx) const {
      if (!m_metadata || idx >= m_assetCount)
        return nullptr;

      const size_t offs = idx * sizeof(AssetDesc);

      return reinterpret_cast<const AssetDesc*>(m_metadata.get() + offs);
    }

    const BlobDesc* getDataBlobDesc(uint32_t idx) const {
      if (!m_metadata || idx >= m_blobCount)
        return nullptr;

      const size_t offs = m_assetCount * sizeof(AssetDesc) +
        idx * sizeof(BlobDesc);

      return reinterpret_cast<const BlobDesc*>(m_metadata.get() + offs);
    }

    size_t readDataBlob(uint32_t idx, void* out, size_t outSize) {
      if (auto blobDesc = getDataBlobDesc(idx)) {
        if (outSize < blobDesc->size)
          return 0;

        if (!openFileHandle())
          return 0;

        if (0 == fseek64(m_handle, blobDesc->offset, SEEK_SET))
          return fread(out, 1, blobDesc->size, m_handle);
      }

      return 0;
    }

    size_t getDataSize() {
      if (!openFileHandle())
        return 0;

      if (m_handle && 0 == fseek64(m_handle, 0, SEEK_SET)) {
        Header header { 0 };
        if (0 == fread(&header, 1, sizeof(header), m_handle))
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

    const std::string& getFilename() const {
      return m_filename;
    }

  private:
    std::string m_filename;
    FILE* m_handle = nullptr;

    uint32_t m_assetCount = 0;
    uint32_t m_blobCount = 0;

    std::unique_ptr<uint8_t[]> m_metadata;
    std::unordered_map<std::string, uint32_t> m_nameHash;
  };

} // namespace dxvk

#undef fseek64
#ifndef WIN32
#undef fopen_s
#endif
