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
#include <unordered_map>

#include "dxvk_compute.h"
#include "dxvk_graphics.h"

namespace dxvk {

  class DxvkStateCache;

  /**
   * \brief Pipeline count
   * 
   * Stores number of graphics and
   * compute pipelines, individually.
   */
  struct DxvkPipelineCount {
    uint32_t numGraphicsPipelines;
    uint32_t numComputePipelines;
  };
  
  
  /**
   * \brief Pipeline manager
   * 
   * Creates and stores graphics pipelines and compute
   * pipelines for each combination of shaders that is
   * used within the application. This is necessary
   * because DXVK does not expose the concept of shader
   * pipeline objects to the client API.
   */
  class DxvkPipelineManager {
    friend class DxvkComputePipeline;
    friend class DxvkGraphicsPipeline;
    friend class DxvkRaytracingPipeline;
  public:
    
    DxvkPipelineManager(
            DxvkDevice*         device,
            DxvkRenderPassPool* passManager);
    
    ~DxvkPipelineManager();
    
    /**
     * \brief Retrieves a compute pipeline object
     * 
     * If a pipeline for the given shader stage object
     * already exists, it will be returned. Otherwise,
     * a new pipeline will be created.
     * \param [in] shaders Shaders for the pipeline
     * \returns Compute pipeline object
     */
    DxvkComputePipeline* createComputePipeline(
      const DxvkComputePipelineShaders& shaders);
    
    /**
     * \brief Retrieves a graphics pipeline object
     * 
     * If a pipeline for the given shader stage objects
     * already exists, it will be returned. Otherwise,
     * a new pipeline will be created.
     * \param [in] shaders Shaders for the pipeline
     * \returns Graphics pipeline object
     */
    DxvkGraphicsPipeline* createGraphicsPipeline(
      const DxvkGraphicsPipelineShaders& shaders);
        
    /**
     * \brief Retrieves a raytracing pipeline object
     * 
     * If a pipeline for the given shader stage objects
     * already exists, it will be returned. Otherwise,
     * a new pipeline will be created.
     * \param [in] shaders Shaders for the pipeline
     * \returns Raytracing pipeline object
     */
    DxvkRaytracingPipeline* createRaytracingPipeline(
      const DxvkRaytracingPipelineShaders& shaders);

    /*
     * \brief Registers a shader
     * 
     * Starts compiling pipelines asynchronously
     * in case the state cache contains state
     * vectors for this shader.
     * \param [in] shader Newly compiled shader
     */
    void registerShader(
      const Rc<DxvkShader>&         shader);

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
     * \brief Retrieves total pipeline count
     * \returns Number of compute/graphics pipelines
     */
    DxvkPipelineCount getPipelineCount() const;

    /**
     * \brief Checks whether async compiler is busy
     * \returns \c true if shaders are being compiled
     */
    bool isCompilingShaders() const;

    /**
     * \brief Stops async compiler threads
     */
    void stopWorkerThreads() const;
    
  private:
    
    DxvkDevice*               m_device;
    Rc<DxvkPipelineCache>     m_cache;
    Rc<DxvkStateCache>        m_stateCache;

    std::atomic<uint32_t>     m_numComputePipelines  = { 0 };
    std::atomic<uint32_t>     m_numGraphicsPipelines = { 0 };
    
    dxvk::mutex m_mutex;
    
    std::unordered_map<
      DxvkComputePipelineShaders,
      DxvkComputePipeline,
      DxvkHash, DxvkEq> m_computePipelines;
    
    std::unordered_map<
      DxvkGraphicsPipelineShaders,
      DxvkGraphicsPipeline,
      DxvkHash, DxvkEq> m_graphicsPipelines;

    std::unordered_map<
      DxvkRaytracingPipelineShaders,
      DxvkRaytracingPipeline,
      DxvkHash, DxvkEq> m_raytracingPipelines;

  };
  
}