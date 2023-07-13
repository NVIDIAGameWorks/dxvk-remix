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

#include "../../util/util_renderprocessor.h"
#include "../../util/thread.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/sync/sync_signal.h"
#include "rtx_texture.h"
#include "rtx_sparse_unique_cache.h"

namespace dxvk {
  class DxvkContext;
  struct ManagedTexture;

  class RtxTextureManager : public RenderProcessor<Rc<ManagedTexture>> {
  public:
    RtxTextureManager(DxvkDevice* device);
    ~RtxTextureManager();

    /**
      * \brief Returns a constant reference to the object table of texture cache.
      * \return Constant reference to the object table of texture cache.
      */
    const std::vector<TextureRef>& getTextureTable() const {
      return m_textureCache.getObjectTable();
    }

    /**
      * \brief Initializes the resource manager.
      * \param [in] ctx The context used to initialize the resource manager.
    */
    void initialize(const Rc<DxvkContext>& ctx);

    /**
      * \brief Preloads a texture asset with the specified color space and context.
      * \param [in] assetData Asset data to preload.
      * \param [in] colorSpace Color space of the texture.
      * \param [in] context The context used to preload the texture.
      * \param [in] forceLoad Force the asset to be loaded, even if it's already been loaded.
      * \return Pointer to the loaded texture resource.
    */
    Rc<ManagedTexture> preloadTextureAsset(const Rc<AssetData>& assetData, ColorSpace colorSpace, const Rc<DxvkContext>& context, bool forceLoad);

    /**
      * \brief Adds a texture to the resource manager.
      * \param [in] immediateContext The immediate context used to add the texture.
      * \param [in] inputTexture The texture to be added.
      * \param [in] sampler The sampler used for the texture.
      * \param [in] allowAsync Whether asynchronous texture upload is allowed for this texture.
      * \param [out] textureIndexOut Index of the added texture in resource table.
    */
    void addTexture(Rc<DxvkContext>& immediateContext, TextureRef inputTexture, Rc<DxvkSampler> sampler, bool allowAsync, uint32_t& textureIndexOut);

    /**
      * \brief Synchronizes the resource manager.
      * \param [in] dropRequests Whether to drop pending requests or not.
      */
    void synchronize(bool dropRequests = false);

    /**
      * \brief Wakes the resource manager thread.
      */
    void kickoff();

    /**
      * \brief Finalizes all pending texture promotions.
      */
    void finalizeAllPendingTexturePromotions();

    /**
      * \brief Demotes all resident material textures from high resolution
      */
    void demoteAllTextures();

    /**
      * \brief Clears all resources managed by the resource manager.
      */
    void clear();

    /**
      * \brief Performs garbage collection on the resource manager.
      */
    void garbageCollection();

    /**
      * \brief Recalculates the available memory for texture data
      * \param [in] context Active dxvk context
      */
    void updateMemoryBudgets(const Rc<DxvkContext>& context);

    /**
      * \brief Calculates the number of mip levels to preload based on the number of total mip levels.
      * \param [in] mipLevels The number of total mip levels.
      * \return The number of mip levels to preload.
      */
    static int calcPreloadMips(int mipLevels);

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

  protected:
    void work(Rc<ManagedTexture>& item, Rc<DxvkContext>& ctx, Rc<DxvkCommandList>& cmd) override;

    bool wakeWorkerCondition() override;

  private:
    void flushRtxIo(bool async);

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

    DxvkDevice* m_pDevice;
    std::atomic<bool> m_dropRequests = false;
    bool m_kickoff = false;

    dxvk::high_resolution_clock::time_point m_batchStartTime { dxvk::high_resolution_clock::duration(0) };
    dxvk::high_resolution_clock::duration m_lastBatchDuration { dxvk::high_resolution_clock::duration(0) };

    VkDeviceSize m_textureBudgetMib = 0;
    uint32_t m_promotionStartFrame = 0;
    bool m_preloadInflight = false;

    fast_unordered_cache<Rc<ManagedTexture>> m_assetHashToTextures;

    RTX_OPTION("rtx.texturemanager", uint32_t, budgetPercentageOfAvailableVram, 50, "The percentage of available VRAM we should use for material textures.  If material textures are required beyond this budget, then those textures will be loaded at lower quality.  Important note, it's impossible to perfectly match the budget while maintaining reasonable quality levels, so use this as more of a guideline.  If the replacements assets are simply too large for the target GPUs available vid mem, we may end up going overbudget regularly.  Defaults to 50% of the available VRAM.");
    RTX_OPTION("rtx.texturemanager", bool, showProgress, false, "Show texture loading progress in the HUD.");

    bool isTextureSuboptimal(const Rc<ManagedTexture>& texture) const;
    void scheduleTextureLoad(TextureRef& texture, Rc<DxvkContext>& immediateContext, bool allowAsync);
    void loadTexture(const Rc<ManagedTexture>& texture, Rc<DxvkContext>& ctx);

    VkDeviceSize overBudgetMib(VkDeviceSize percentageOfBudget = 100) const;
  };

} // namespace dxvk
