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

#include <mutex>
#include <queue>

#include "../../util/thread.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/sync/sync_signal.h"
#include "rtx_sparse_unique_cache.h"
#include "rtx_common_object.h"

namespace dxvk {
  class DxvkContext;
  struct ManagedTexture;
  struct AsyncRunner;
  struct AsyncRunner_RTXIO;
  struct FeedbackAccum;

  struct SamplerFeedback {
    dxvk::mutex                     m_idToTexture_mutex{};
    std::vector<Rc<ManagedTexture>> m_idToTexture{};
    uint16_t*                       m_related{};
    uint8_t*                        m_noisyMipcount{};
    FeedbackAccum*                  m_accumulatedMipcount{};

    // Variables needed for optimization
    std::atomic_uint16_t  m_idToTexture_count{ 0 };
    uint8_t*              m_cachedAssetMipcount{};
    uint32_t              m_cachedAssetMipcount_length{ 0 };
    uint32_t*             m_cachedGpubuf{ 0 };

    bool associate(uint16_t stampWithList, uint16_t stampToAdd);
    uint32_t fetchNoisyMipCounts(const uint32_t* src_gpubuf);
    void accumulateMipCounts(uint32_t len, uint32_t curframe, bool canReset);
  };

  class RtxTextureManager : public CommonDeviceObject {
  public:
    explicit RtxTextureManager(DxvkDevice* device);
    ~RtxTextureManager();

    RtxTextureManager(const RtxTextureManager&) = delete;
    RtxTextureManager(RtxTextureManager&&) noexcept = delete;
    RtxTextureManager& operator=(const RtxTextureManager&) = delete;
    RtxTextureManager& operator=(RtxTextureManager&&) noexcept = delete;

    void startAsync();

    /**
      * \return Linearized table of textures in the texture cache.
      */
    const std::vector<TextureRef>& getTextureTable() const {
      return m_textureCache.getObjectTable();
    }

    /**
      * \brief Preloads a texture asset with the specified color space and context.
      * \param [in] assetData Asset data to preload.
      * \param [in] colorSpace Color space of the texture.
      * \param [in] forceLoad Force the asset to be loaded, even if it's already been loaded.
      * \return Pointer to the loaded texture resource.
    */
    Rc<ManagedTexture> preloadTextureAsset(const Rc<AssetData>& assetData, ColorSpace colorSpace, bool forceLoad);

    /**
      * \brief Adds a texture to the resource manager.
      * \param [in] inputTexture The texture to be added.
      * \param [in] associatedFeedbackStamp A sampler feedback stamp from which to inherit a sampled mip count (written on GPU).
      * \param [in] async If a texture is allowed to be loaded asynchronously.
      * \param [out] textureIndexOut Index of the added texture in resource table.
    */
    void addTexture(const TextureRef&  inputTexture, uint16_t associatedFeedbackStamp, bool async, uint32_t& textureIndexOut);

    /**
      * \brief Submit staging-to-device texture uploads, that are currently ready from async thread.
      */
    void submitTexturesToDeviceLocal(DxvkContext* ctx, DxvkBarrierSet& execBarriers, DxvkBarrierSet& execAcquires);

    /**
      * \brief Clears texture cache when scene is absent.
      * Textures are only demoted if VRAM usage exceeds budget, preventing
      * blur pop when returning from full-screen menus.
      */
    void clear();
    
    void prepareSamplerFeedback(DxvkContext* ctx);
    void copySamplerFeedbackToHost(DxvkContext* ctx);

    /**
      * \brief Performs garbage collection on the resource manager.
      */
    void garbageCollection(const uint32_t* gpuAccessedMips);
    
    /**
      * \brief Manages texture VRAM budget by demoting textures when over budget.
      * 
      * Demotes textures that were previously rendered (m_frameLastUsed != UINT32_MAX).
      * Newly loaded textures (m_frameLastUsed == UINT32_MAX) are preserved since they
      * haven't been rendered yet and are needed for the incoming scene.
      */
    void manageBudgetWithPriority();

    /**
      * \brief Returns a unique hash key for the resource manager.
      * \return A unique hash key.
      */
    static XXH64_hash_t getUniqueKey();

    inline static bool getShowProgress() {
      return showProgress();
    }

    // Do not use. This is here temporarily for WAR for REMIX-1557
    void releaseTexture(TextureRef& textureRef) {
      m_textureCache.free(textureRef);
    }

    void requestHotReload(const Rc<ManagedTexture>& tex);
    void processAllHotReloadRequests();

  private:
    void scheduleTextureLoad(const Rc<ManagedTexture>& texture, bool async, bool forceUnload = false);

  private:
    struct TextureHashFn {
      size_t operator() (const TextureRef& tex) const {
        return tex.getUniqueKey();
      }
    };
    struct TextureEquality {
      bool operator()(const TextureRef& lhs, const TextureRef& rhs) const {
        return lhs.getUniqueKey() == rhs.getUniqueKey();
      }
    };
    SparseUniqueCache<TextureRef, TextureHashFn, TextureEquality> m_textureCache;

    AsyncRunner*       m_asyncThread;
    AsyncRunner_RTXIO* m_asyncThread_rtxio;

    fast_unordered_cache<Rc<ManagedTexture>> m_assetHashToTextures;
    dxvk::mutex m_assetHashToTextures_mutex;

    SamplerFeedback m_sf = {};
    bool m_wasTextureBudgetPressure = false;

    RTX_OPTION("rtx.texturemanager", bool, showProgress, false, "Show texture loading progress in the HUD.");

    struct RcManagedTextureHash {
      std::size_t operator()(const Rc<ManagedTexture>& w) const noexcept { return std::hash<void*>{}(w.ptr()); }
    };

    dxvk::mutex m_hotreloadMutex{};
    std::unordered_set<Rc<ManagedTexture>, RcManagedTextureHash> m_hotreloadRequests{};
  };

} // namespace dxvk
