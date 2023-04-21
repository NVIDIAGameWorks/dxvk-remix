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

#include <vulkan/vulkan.h>
#include "../../util/util_error.h"
#include "../../util/rc/util_rc.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/xxHash/xxhash.h"

namespace dxvk {
  enum class AssetType {
    Unknown,
    Buffer,
    Image1D,
    Image2D,
    Image3D,
  };

  enum class AssetCompression {
    None,
    GDeflate,
  };

  struct AssetInfo {
    AssetType type = AssetType::Unknown;
    AssetCompression compression = AssetCompression::None;

    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent;

    uint32_t mipLevels = 0;
    uint32_t looseLevels = 0;
    uint32_t numLayers = 0;

    const char* filename = nullptr;
  };

  class AssetData : public RcObject {
  public:
    virtual ~AssetData() = default;

    const AssetInfo& info() const {
      return m_info;
    }

    XXH64_hash_t hash() const {
      return m_hash;
    }

    virtual const void* data(int layer, int level) = 0;
    virtual void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const = 0;
    virtual void evictCache() = 0;

  protected:
    AssetData() = default;

    AssetInfo m_info;
    XXH64_hash_t m_hash;
  };

  class ImageAssetDataView : public AssetData {
  public:
    ImageAssetDataView(const Rc<AssetData>& sourceAsset, int minLevel)
      : m_sourceAsset(sourceAsset)
      , m_minLevel(minLevel) {
      if (sourceAsset->info().type != AssetType::Image1D &&
          sourceAsset->info().type != AssetType::Image2D &&
          sourceAsset->info().type != AssetType::Image3D) {
        throw DxvkError("Only image assets supported by image asset data view class!");
      }
      m_info = sourceAsset->info();
      m_hash = sourceAsset->hash();

      setMinLevel(minLevel);
    }

    const void* data(int layer, int level) override {
      return m_sourceAsset->data(layer, level + m_minLevel);
    }

    void evictCache() override {
      return m_sourceAsset->evictCache();
    }

    void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const override {
      return m_sourceAsset->placement(layer, face, level + m_minLevel,
        offset, size);
    }

    void setMinLevel(int minLevel) {
      const auto& srcInfo = m_sourceAsset->info();

      // minLevel must not be >= m_info.mipLevels
      if (minLevel >= srcInfo.mipLevels) {
        throw DxvkError("Minimum mip level is larger than the "
                        "number of source asset mip levels!");
      }

      // Patch asset view info
      m_info.mipLevels = srcInfo.mipLevels - minLevel;
      m_info.looseLevels = srcInfo.looseLevels > minLevel ?
        srcInfo.looseLevels - minLevel : 0;

      m_info.extent.width = std::max(1u, srcInfo.extent.width >> minLevel);
      m_info.extent.height = std::max(1u, srcInfo.extent.height >> minLevel);
      m_info.extent.depth = std::max(1u, srcInfo.extent.depth >> minLevel);

      m_minLevel = minLevel;
    }

  private:
    const Rc<AssetData> m_sourceAsset;
    int m_minLevel;
  };

} // namespace dxvk
