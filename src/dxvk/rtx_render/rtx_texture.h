/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_utils.h"
#include "rtx_asset_data.h"
#include "rtx_constants.h"

namespace gli {
  class texture;
  class texture2d;
}

namespace dxvk {
  class DxvkContext;
  class DxvkDevice;

  // Sentinel value used to indicate a key needs to be generated for this object.
  static const size_t kInvalidTextureKey = ~0ull;

  enum class ColorSpace {
    FORCE_BC_SRGB,
    AUTO
  };


  constexpr uint8_t MAX_MIPS = 32;

  // The ManagedTexture holds streaming state for a given texture.
  struct ManagedTexture : public RcObject {
    enum struct State {
      kUnknown,         // Texture was not initialized, its state is unknown.
      kInitialized,     // Texture was initialized and image asset data discovered.
      kQueuedForUpload, // Texture image upload or RTX IO request is in-flight.
      kVidMem,          // Texture image is in VID memory (either partial, or full mip-chain).
      kFailed           // Texture image to upload or read, or was dropped.
    };

    // Stage 1 - Texture initialized, image asset data discovered.
    Rc<AssetData>       m_assetData     = {};
    ColorSpace          m_colorSpace    = { ColorSpace::AUTO };
    size_t              m_uniqueKey     = kInvalidTextureKey;
    bool                m_canDemote       = true;

    // Stage 2 - Video memory
    // The range [m_currentMip_begin, m_currentMip_end) defines the mip-levels that were used to create m_currentMipView.
    // Maintains: 'm_currentMip_begin < m_currentMip_end'
    // 'm_currentMip_end-1' is usually the smallest-resolution mip-level (1x1). m_currentMip_end is not included into the range.
    // Example: asset is 1024x1024 (11 mips total), m_currentMip_begin=4, m_currentMip_end=11,
    //          then m_currentMipView contains an image with mipcount=7: 64x64, 32x32, 16x16, 8x8, 4x4, 2x2, 1x1
    Rc<DxvkImageView>   m_currentMipView     = {};
    uint32_t            m_currentMip_begin   = 0;
    uint32_t            m_currentMip_end     = 0;

    std::atomic_uint8_t m_requestedMips      = 0;
    std::atomic<State>  m_state              = State::kUnknown;
    uint64_t            m_completionSyncpt   = 0; // completion syncpoint value for RTX IO

    // Texture streaming
    uint16_t            m_samplerFeedbackStamp = 0; // unique linear index of this asset; required to keep 
                                                    // the data structure access simple (i.e. with a linear index, it's just an offset in array)
    mutable uint32_t    m_frameLastUsed                   = UINT32_MAX;
    mutable uint32_t    m_frameLastUsedForSamplerFeedback = UINT32_MAX;

  public:
    bool hasUploadedMips(uint32_t requiredMips, bool exact) const;
    void requestMips(uint32_t requiredMips);
    std::pair<uint16_t, uint16_t> calcRequiredMips_BeginEnd() const;

    DxvkImageCreateInfo imageCreateInfo() const;
  };


  inline size_t handleOrUniqueKey(size_t uniqueKey, VkImageView v) {
    return uniqueKey == kInvalidTextureKey ? XXH3_64bits(&v, sizeof(v)) : uniqueKey;
  }


  struct TextureRef {
    friend struct TextureUtils;

    TextureRef() = default;

    // True vidmem texture-ref
    //  uniqueKey can be used to link this TextureRef to another TextureRef (e.g. HOST promoted TextureRef's)
    explicit TextureRef(Rc<DxvkImageView> image, size_t uniqueKey = kInvalidTextureKey)
      : m_imageView{ std::move(image) }
      , m_managedTexture{ nullptr }
      , m_uniqueKey{ handleOrUniqueKey(uniqueKey, m_imageView->handle()) } { }

    // Promised reference to a future texture
    explicit TextureRef(const Rc<ManagedTexture>& managedTexture)
      : m_imageView{ nullptr }
      , m_managedTexture{ managedTexture }
      , m_uniqueKey{ managedTexture.ptr() ? managedTexture->m_uniqueKey : kInvalidTextureKey } { }
      
    bool isImageEmpty() const {
      return getImageView() == nullptr;
    }

    DxvkImageView* getImageView() const {
      if (m_imageView.ptr())
        return m_imageView.ptr();

      if (m_managedTexture.ptr())
        return m_managedTexture->m_currentMipView.ptr();

      return nullptr;
    }

    XXH64_hash_t getImageHash() const {
      XXH64_hash_t result = 0;
      if (const DxvkImageView* resolvedImageView = getImageView()) {
        result = resolvedImageView->image()->getHash();
      }

      if (result == 0 && m_managedTexture.ptr() != nullptr) {
        // NOTE: only replacement textures should have an m_managedTexture pointer.  To avoid changing game texture
        // hashes, all ImageHash modifications should be inside this block.
        const XXH64_hash_t assetDataHash = m_managedTexture->m_assetData->hash();
        result = XXH64(&assetDataHash, sizeof(assetDataHash), result);
        // Needed to distinguish materials that load the same file different ways (i.e. raw vs sRGB)
        result = XXH64(&m_uniqueKey, sizeof(m_uniqueKey), result);
      }

      return result;
    }

    size_t getUniqueKey() const {
      assert(m_uniqueKey != kInvalidTextureKey);
      return m_uniqueKey;
    }

    const Rc<ManagedTexture>& getManagedTexture() const {
      return m_managedTexture;
    }

    // Checks to see if theres a valid texture by checking the key
    bool isValid() const {
      return (m_uniqueKey != kInvalidTextureKey);
    }

    void tryRequestMips(uint32_t requiredMips) {
      if (m_managedTexture.ptr()) {
        assert(!m_imageView.ptr());
        m_managedTexture->requestMips(requiredMips);
      }
    }

    void demote() {
      assert((m_imageView.ptr() && !m_managedTexture.ptr()) || (!m_imageView.ptr() && m_managedTexture.ptr()));

      if (m_imageView.ptr()) {
        m_imageView = nullptr;
      } else if (m_managedTexture.ptr()) {
        m_managedTexture->requestMips(0);
      }
    }

  private:
    Rc<DxvkImageView> m_imageView;
    Rc<ManagedTexture> m_managedTexture;

    size_t m_uniqueKey = kInvalidTextureKey;
  };

  struct TextureUtils {

    enum class MemoryAperture {
      HOST,
      VID
    };

    static VkFormat toSRGB(const VkFormat format) {
      switch (format) {
      case VK_FORMAT_R8_UNORM:
        return VK_FORMAT_R8_SRGB;
      case VK_FORMAT_R8G8_UNORM:
        return VK_FORMAT_R8G8_SRGB;
      case VK_FORMAT_R8G8B8_UNORM:
        return VK_FORMAT_R8G8B8_SRGB;
      case VK_FORMAT_B8G8R8_UNORM:
        return VK_FORMAT_B8G8R8_SRGB;
      case VK_FORMAT_R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_SRGB;
      case VK_FORMAT_B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_SRGB;
      case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        return VK_FORMAT_A8B8G8R8_SRGB_PACK32;

      case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
      case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
      case VK_FORMAT_BC2_UNORM_BLOCK:
        return VK_FORMAT_BC2_SRGB_BLOCK;
      case VK_FORMAT_BC3_UNORM_BLOCK:
        return VK_FORMAT_BC3_SRGB_BLOCK;
      case VK_FORMAT_BC7_UNORM_BLOCK:
        return VK_FORMAT_BC7_SRGB_BLOCK;
      default:
        return format;
      }
    }

    static inline bool isBC(const VkFormat format) {
      return format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC7_SRGB_BLOCK;
    }

    static inline bool isLDR(const VkFormat format) {
      return (isBC(format) && format != VK_FORMAT_BC6H_UFLOAT_BLOCK &&
             format != VK_FORMAT_BC6H_SFLOAT_BLOCK) || format < VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    }

    /// Filename can be KTX or DDS files
    enum class MipsToLoad {
      HighMips,
      LowMips,
      All
    };

    static Rc<ManagedTexture> createTexture(const Rc<AssetData>& assetData, ColorSpace colorSpace);
  };

  void loadTextureRtxIo(const Rc<ManagedTexture>& texture,
                        const Rc<DxvkImageView>& dstImage,
                        const uint32_t mipLevels_begin,
                        const uint32_t mipLevels_end /* non-inclusive */ );

} // namespace dxvk
