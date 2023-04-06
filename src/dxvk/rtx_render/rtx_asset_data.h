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

  class AssetData : public RcObject {
  public:
    virtual ~AssetData() = default;

    virtual AssetType type() const = 0;
    virtual AssetCompression compression() const = 0;
    virtual VkFormat format() const = 0;
    virtual VkExtent3D extent(int level) const = 0;
    virtual int levels() const = 0;
    virtual int looseLevels() const = 0;
    virtual int layers() const = 0;
    virtual const void* data(int layer, int level) = 0;
    virtual const char* filename() const = 0;
    virtual void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const = 0;
    virtual void evictCache() = 0;
  };

  class ImageAssetDataView : public AssetData {
  public:
    ImageAssetDataView(const Rc<AssetData>& sourceAsset, int minLevel)
      : m_sourceAsset(sourceAsset)
      , m_minLevel(minLevel) {
      if (sourceAsset->type() != AssetType::Image1D &&
          sourceAsset->type() != AssetType::Image2D &&
          sourceAsset->type() != AssetType::Image3D) {
        throw DxvkError("Only image assets supported by image asset data view class!");
      }
    }

    AssetType type() const override {
      return m_sourceAsset->type();
    }

    AssetCompression compression() const override {
      return m_sourceAsset->compression();
    }

    VkFormat format() const override {
      return m_sourceAsset->format();
    }

    VkExtent3D extent(int level) const override {
      return m_sourceAsset->extent(level + m_minLevel);
    }

    int levels() const override {
      return m_sourceAsset->levels() - m_minLevel;
    }

    int looseLevels() const override {
      return std::max(0, m_sourceAsset->looseLevels() - m_minLevel);
    }

    int layers() const override {
      return m_sourceAsset->layers();
    }

    const void* data(int layer, int level) override {
      return m_sourceAsset->data(layer, level + m_minLevel);
    }

    void evictCache() override {
      return m_sourceAsset->evictCache();
    }

    const char* filename() const override {
      return m_sourceAsset->filename();
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
      m_minLevel = minLevel;
    }

  private:
    const Rc<AssetData> m_sourceAsset;
    int m_minLevel;
  };

} // namespace dxvk
