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
#include "dxvk_context_state.h"
#include "rtx_asset_data.h"

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

  // The ManagedTexture holds streaming state for a given texture.
  //  A texture can be loaded in many states, if it's initial 
  //  memory location is host (system) memory, then there are 
  //  various stages of promotion. Handled by this object.
  //  
  //  Host (System Memory) -> StrippedMips (Video Memory) -> Full (Video Memory)
  //
  // Once the texture is in it's final state, the owning TextureRef can choose to
  //  promote the 'allMipsImageView' to its 'm_imageView', and release the managed texture.
  //  When a managed texture is fully promoted, the members of TextureRef are modified in 
  //  TextureRef::finalizePendingPromotion.
  // 
  // Members of TextureRef created from a ManagedTexture depend on the residency of the managed texture.
  //  - HostMem and QueuedForUpload: TextureRef has an empty imageView and holds a reference to managedTexture
  //  - VidMem : imageView contains the allMipsImageView from ManagedTexture, and the managedTexture reference is empty.
  //
  // For textures in system memory, we split the mips into two sets:
  //  - Bigger mips: data is stored in linearImageDataLargeMips; this is loaded only as needed for an upload and
  //    discarded immediately after, to conserve CPU memory
  //  - Smaller mips: data is stored in linearImageDataSmallMips and kept as long as the texture object is alive,
  //    to reduce latency when textures are first used
  struct ManagedTexture : public RcObject {
    friend struct TextureUtils;

    enum struct State {
      kUnknown,                                         // Texture was not initialized, its state is unknown.
      kInitialized,                                     // Texture was initialized and image asset data discovered.
      kQueuedForUpload,                                 // Texture image upload or RTX IO request is in-flight.
      kVidMem,                                          // Texture image is in VID memory (either partial, or full mip-chain).
      kFailed                                           // Texture image to upload or read, or was dropped.
    };

    // Stage 1 - Texture initialized, image asset data discovered.
    Rc<ImageAssetDataView> assetData;
    int mipCount = 0;                                   // how many mips in the original asset
    int minPreloadedMip = -1;                           // highest resolution mip pre-loaded
    uint64_t completionSyncpt = 0;                      // completion syncpoint value

    // Stage 2 - Video memory (stripped top mips) cache.
    Rc<DxvkImageView> smallMipsImageView = nullptr;

    // Stage 3 - Video memory (full mip chain loaded).
    Rc<DxvkImageView> allMipsImageView = nullptr;

    std::atomic<State> state = State::kUnknown;
    size_t uniqueKey = kInvalidTextureKey;
    bool canDemote = true;
    uint32_t frameQueuedForUpload = 0;

    bool good() const {
      return state != State::kUnknown && state != State::kFailed;
    }

    void demote() {
      if (canDemote && (state == ManagedTexture::State::kVidMem || state == ManagedTexture::State::kFailed)) {
        // Evict large image
        allMipsImageView = nullptr;
        completionSyncpt = ~0;
        smallMipsImageView = nullptr;
        state = ManagedTexture::State::kInitialized;
        minPreloadedMip = -1;
      }
    }

  private:
    DxvkImageCreateInfo futureImageDesc;
  };

  struct TextureRef {
    friend struct TextureUtils;

    TextureRef()
    { }
        
    // True vidmem texture-ref
    //  uniqueKey can be used to link this TextureRef to another TextureRef (e.g. HOST promoted TextureRef's)
    TextureRef(Rc<DxvkSampler> sample, Rc<DxvkImageView> image, size_t uniqueKey = kInvalidTextureKey)
      : sampler(std::move(sample))
      , m_imageView(std::move(image)) {
      // If a key has been provided, use it (i.e. has this RtTexture been created from a promotion).
      if (uniqueKey == kInvalidTextureKey) {
        VkImageView view = m_imageView->handle();
        this->m_uniqueKey = XXH64(&view, sizeof(view), 0);
      } else
        this->m_uniqueKey = uniqueKey;
    }

    // Convenience version of the above constructor
    explicit TextureRef(const DxvkShaderResourceSlot& slot)
      : TextureRef(slot.sampler, slot.imageView)
    { }

    // Promised reference to a future texture
    explicit TextureRef(const Rc<ManagedTexture>& managedTexture)
      : m_uniqueKey(managedTexture.ptr() ? managedTexture->uniqueKey : kInvalidTextureKey)
      , m_imageView(managedTexture.ptr() ? managedTexture->allMipsImageView : nullptr) // Load the full quality image view directly into texture ref (if it exists)
      , m_managedTexture(managedTexture) 
    { }
      
    bool isImageEmpty() const {
      return getImageView() == nullptr;
    }

    DxvkImageView* getImageView() const {
      if (m_imageView.ptr())
        return m_imageView.ptr();

      if (m_managedTexture.ptr())
        return m_managedTexture->smallMipsImageView.ptr();

      return nullptr;
    }

    XXH64_hash_t getImageHash() const {
      const DxvkImageView* resolvedImageView = getImageView();

      if (resolvedImageView)
        return resolvedImageView->image()->getHash();

      return 0;
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

    // Are the full resource and mips are ready
    bool isFullyResident() const {
      return m_imageView.ptr();
    }

    // Theres still pending promotion work in the managed texture
    bool isPromotable() const {
      return !isFullyResident() && m_managedTexture.ptr();
    }

    void demote() {
      if (m_managedTexture != nullptr) {
        // This texture should never be demoted
        if (!m_managedTexture->canDemote)
          return;

        m_managedTexture->demote();
      }
      m_imageView = nullptr;
    }

    // If we have a valid full resource here, the managed texture has served it's purpose.
    bool finalizePendingPromotion() {
      if (!isFullyResident() && (m_managedTexture->state == ManagedTexture::State::kVidMem)) {
        // Promote the managed texture full mip chain view to our TextureRef
        m_imageView = m_managedTexture->allMipsImageView;
      }
      return isFullyResident();
    }

    Rc<DxvkSampler> sampler;

    mutable uint32_t frameLastUsed = 0xFFFFFFFF;

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

    static void loadTexture(Rc<ManagedTexture> texture, const Rc<DxvkContext>& ctx, const bool isPreloading, int minimumMipLevel);
  };

} // namespace dxvk
