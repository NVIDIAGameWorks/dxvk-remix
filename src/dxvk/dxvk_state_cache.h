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

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "dxvk_state_cache_types.h"
// NV-DXVK start: compile rt shaders on shader compilation threads
#include "dxvk_raytracing.h"
// NV-DXVK end

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief State cache
   * 
   * The shader state cache stores state vectors and
   * render pass formats of all pipelines used in a
   * game, which allows DXVK to compile them ahead
   * of time instead of compiling them on the first
   * draw.
   */
  class DxvkStateCache : public RcObject {

  public:

    DxvkStateCache(
      const DxvkDevice*           device,
            DxvkPipelineManager*  pipeManager,
            DxvkRenderPassPool*   passManager);
    
    ~DxvkStateCache();

    /**
     * Adds a graphics pipeline to the cache
     * 
     * If the pipeline is not already cached, this
     * will write a new pipeline to the cache file.
     * \param [in] shaders Shader keys
     * \param [in] state Graphics pipeline state
     * \param [in] format Render pass format
     */
    void addGraphicsPipeline(
      const DxvkStateCacheKey&              shaders,
      const DxvkGraphicsPipelineStateInfo&  state,
      const DxvkRenderPassFormat&           format);

    /**
     * Adds a compute pipeline to the cache
     * 
     * If the pipeline is not already cached, this
     * will write a new pipeline to the cache file.
     * \param [in] shaders Shader keys
     * \param [in] state Compute pipeline state
     */
    void addComputePipeline(
      const DxvkStateCacheKey&              shaders,
      const DxvkComputePipelineStateInfo&   state);

    /**
     * \brief Registers a newly compiled shader
     * 
     * Makes the shader available to the pipeline
     * compiler, and starts compiling all pipelines
     * for which all shaders become available.
     * \param [in] shader The shader to add
     */
    void registerShader(
      const Rc<DxvkShader>&                 shader);

    // NV-DXVK start: compile raytracing shaders on shader compilation threads
    /**
     * \brief Registers a set of raytracing shaders
     * 
     * Makes the shader available to the pipeline
     * compiler, and starts compiling all pipelines
     * for which all shaders become available.
     * \param [in] shaders The shaders to add
     */
    void registerRaytracingShaders(
      const DxvkRaytracingPipelineShaders& shaders);
    // NV-DXVK end
    
    /**
     * \brief Explicitly stops worker threads
     */
    void stopWorkerThreads();

    /**
     * \brief Checks whether compiler threads are busy
     * \returns \c true if we're compiling shaders
     */
    bool isCompilingShaders() {
      return m_workerBusy.load() > 0;
    }

  private:

    using WriterItem = DxvkStateCacheEntry;

    struct WorkerItem {
      DxvkGraphicsPipelineShaders gp;
      DxvkComputePipelineShaders  cp;

      // NV-DXVK start: compile rt shaders on shader compilation threads
      DxvkRaytracingPipelineShaders rt;
      // NV-DXVK end

      // NV-DXVK start: do not compile same shader multiple times
      size_t hash() const {
        // raytracing shader group hash is NOT guaranteed to be zero
        if (!rt.groups.empty()) {
          return rt.hash();
        }
        // note that one of these is guaranteed to be zero
        return cp.hash() ^ gp.hash();
      }
      // NV-DXVK end
    };

    DxvkPipelineManager*              m_pipeManager;
    DxvkRenderPassPool*               m_passManager;

    std::vector<DxvkStateCacheEntry>  m_entries;
    std::atomic<bool>                 m_stopThreads = { false };

    dxvk::mutex                       m_entryLock;

    std::unordered_multimap<
      DxvkStateCacheKey, size_t,
      DxvkHash, DxvkEq> m_entryMap;

    std::unordered_multimap<
      DxvkShaderKey, DxvkStateCacheKey,
      DxvkHash, DxvkEq> m_pipelineMap;
    
    std::unordered_map<
      DxvkShaderKey, Rc<DxvkShader>,
      DxvkHash, DxvkEq> m_shaderMap;

    dxvk::mutex                       m_workerLock;
    dxvk::condition_variable          m_workerCond;
    std::queue<WorkerItem>            m_workerQueue;
    // NV-DXVK start: do not compile same shader multiple times
    std::unordered_set<size_t>        m_workerItemsInFlight;  // stores hashes for work items in the queue
    // NV-DXVK end
    std::atomic<uint32_t>             m_workerBusy;
    std::vector<dxvk::thread>         m_workerThreads;

    dxvk::mutex                       m_writerLock;
    dxvk::condition_variable          m_writerCond;
    std::queue<WriterItem>            m_writerQueue;
    dxvk::thread                      m_writerThread;

    DxvkShaderKey getShaderKey(
      const Rc<DxvkShader>&           shader) const;

    bool getShaderByKey(
      const DxvkShaderKey&            key,
            Rc<DxvkShader>&           shader) const;
    
    void mapPipelineToEntry(
      const DxvkStateCacheKey&        key,
            size_t                    entryId);
    
    void mapShaderToPipeline(
      const DxvkShaderKey&            shader,
      const DxvkStateCacheKey&        key);

    void compilePipelines(
      const WorkerItem&               item);

    bool readCacheFile();

    bool readCacheHeader(
            std::istream&             stream,
            DxvkStateCacheHeader&     header) const;

    bool readCacheEntryV7(
            uint32_t                  version,
            std::istream&             stream, 
            DxvkStateCacheEntry&      entry) const;
    
    bool readCacheEntry(
            uint32_t                  version,
            std::istream&             stream, 
            DxvkStateCacheEntry&      entry) const;
    
    void writeCacheEntry(
            std::ostream&             stream, 
            DxvkStateCacheEntry&      entry) const;
    
    bool convertEntryV2(
            DxvkStateCacheEntryV4&    entry) const;
    
    bool convertEntryV4(
      const DxvkStateCacheEntryV4&    in,
            DxvkStateCacheEntryV6&    out) const;
    
    bool convertEntryV5(
      const DxvkStateCacheEntryV5&    in,
            DxvkStateCacheEntryV6&    out) const;
    
    bool convertEntryV6(
      const DxvkStateCacheEntryV6&    in,
            DxvkStateCacheEntry&      out) const;
    
    void workerFunc();

    void writerFunc();

    std::wstring getCacheFileName() const;
    
    std::string getCacheDir() const;

    static uint8_t packImageLayout(
            VkImageLayout             layout);

    static VkImageLayout unpackImageLayout(
            uint8_t                   layout);

    static bool validateRenderPassFormat(
      const DxvkRenderPassFormat&     format);

  };

}
