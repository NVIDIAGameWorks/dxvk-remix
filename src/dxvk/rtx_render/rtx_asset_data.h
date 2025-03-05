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

#include <filesystem>
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

    uint32_t mipLevels             = 0; // Amount of mip levels that is available on disk
    uint32_t mininumLevelsToUpload = 0; // Minimum amount of tail mips that can be uploaded,
                                        // e.g. if asset has 10 mips total, and MIN=6, then the asset
                                        // cannot upload mips 8-9, 7-9, etc separately, it can only do mips 4-9 ('tail mips').
                                        // Keeping this value large can be beneficial:
                                        // e.g. if MIN=5, then mips 32x32, 16x16, 8x8, 4x4, 2x2, 1x1
                                        // will be requested to be uploaded as one transaction, reducing the reads from disk.
    uint32_t numLayers = 0;

    std::filesystem::file_time_type lastWriteTime = {};

    const char* filename = nullptr;

    bool matches(const AssetInfo& other) const {
      return other.type == type &&
             other.compression == compression &&
             other.format == format &&
             other.extent.width == extent.width &&
             other.extent.height == extent.height &&
             other.extent.depth == extent.depth &&
             other.mipLevels == mipLevels &&
             other.mininumLevelsToUpload == mininumLevelsToUpload &&
             other.numLayers == numLayers &&
             other.lastWriteTime == lastWriteTime;
    }
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

    /**
     * \brief Get asset data
     *
     * Returns a pointer to asset data. Loads asset data from the
     * source when needed and stores it in the internal cache.
     * The internal cache will stay around until this object is
     * destroyed or cache is evicted using the evictCache() method.
     *
     * Note: for performance reasons the source media may
     * remain open after this function completes. To release
     * the source media use releaseSource() method.
     * \param [in] layer Image layer, ignored if asset is not an image
     * \param [in] level Image level, ignored if asset is not an image
     * \returns Pointer to data
     */
    virtual const void* data(int layer, int level) = 0;

    /**
     * \brief Get asset data location in the source
     *
     * \param [in] layer Image layer, ignored if asset is not an image
     * \param [in] face Cube image face, ignored if asset is not an image
     * \param [in] level Image level, ignored if asset is not an image
     * \param [out] offset Offset in the source media
     * \param [out] size Data size in the source media
     */
    virtual void placement(
      int       layer,
      int       face,
      int       level,
      uint64_t& offset,
      size_t&   size) const = 0;

    /**
     * \brief Release cached resources
     *
     * Releases the internally allocated memory for a given
     * subresource.
     * \param [in] layer Image layer, ignored if asset is not an image
     * \param [in] level Image level, ignored if asset is not an image
     */
    virtual void evictCache(int layer, int level) = 0;

    /**
     * \brief Release source media
     *
     * The source media may remain open for performace reasons as
     * long as asset data object is alive. (i.e. same file is not
     * opened/closed multiple times while loading separate image layers)
     * This function sets a hint on the source media that it will not be
     * needed anytime soon (e.g. image asset has been uploaded to GPU) and
     * may be released as well as the OS resources it uses.
     */
    virtual void releaseSource() = 0;

  protected:
    AssetData() = default;

    AssetInfo m_info;
    XXH64_hash_t m_hash;
  };

} // namespace dxvk
