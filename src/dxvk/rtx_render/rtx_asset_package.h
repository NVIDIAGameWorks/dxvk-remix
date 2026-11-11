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

#include "../../util/rc/util_rc.h"
#include "usdg.h"

namespace dxvk {

  // A trivial assets package file container
  class AssetPackage : public RcObject {
  public:
    using AssetDesc = usdg::AssetDesc;
    using BlobDesc = usdg::BlobDesc;

    static constexpr uint32_t kNoAssetIdx = usdg::kNoAssetIdx;

    AssetPackage() = default;
    explicit AssetPackage(const std::string& filename)
      : m_filename { filename } { }

    ~AssetPackage() {
      close();
    }

    bool initialize(const char* filename = nullptr) {
      if (m_filename.empty()) {
        if (nullptr == filename) {
          return false;
        }

        m_filename = filename;
      }

      close();

      return usdg::OpenPackage(m_filename.c_str(), &m_handle);
    }

    void close() {
      if (m_handle) {
        usdg::ClosePackage(m_handle);
        m_handle = nullptr;
      }
    }

    uint32_t getAssetCount() const {
      return usdg::GetAssetsCount(m_handle);
    }

    const AssetDesc* getAssetDesc(uint32_t idx) const {
      return usdg::GetAssetDesc(m_handle, idx);
    }

    const BlobDesc* getDataBlobDesc(uint32_t idx) const {
      return usdg::GetDataBlobDesc(m_handle, idx);
    }

    size_t readDataBlob(uint32_t idx, void* out, size_t outSize) {
      return usdg::ReadDataBlob(m_handle, idx, out, outSize);
    }

    uint32_t findAsset(const std::string& assetName) const {
      return usdg::FindAsset(m_handle, assetName.c_str());
    }

    const std::string& getFilename() const {
      return m_filename;
    }

    const char* getVolumeFilename(uint8_t volume) const {
      return usdg::GetVolumeFilename(m_handle, volume);
    }
  private:
    std::string m_filename;

    usdg::PackageHandle m_handle = nullptr;
  };

} // namespace dxvk
