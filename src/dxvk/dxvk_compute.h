#pragma once

#include <atomic>
#include <vector>

#include "dxvk_bind_mask.h"
#include "dxvk_graphics_state.h"
#include "dxvk_pipecache.h"
#include "dxvk_pipelayout.h"
#include "dxvk_resource.h"
#include "dxvk_shader.h"
#include "dxvk_stats.h"

namespace dxvk {
  
  class DxvkDevice;
  class DxvkPipelineManager;
  
  /**
   * \brief Shaders used in compute pipelines
   */
  struct DxvkComputePipelineShaders {
    Rc<DxvkShader> cs;
    // Note: This flag should be set for all Remix shaders for the most part not using spec constants.
    // This allows shaders to bypass needing to be seen and cached to disk on first run before they can be prewarmed in
    // subsequent runs by indicating that there will be no spec constant state that requires consideration.
    // If set to false, shaders may cause a blocking stall on first run on a clean system.
    bool forceNoSpecConstants = false;

    bool eq(const DxvkComputePipelineShaders& other) const {
      return cs == other.cs;
    }

    size_t hash() const {
      return DxvkShader::getHash(cs);
    }
  };


  /**
   * \brief Compute pipeline instance
   */
  class DxvkComputePipelineInstance {

  public:

    DxvkComputePipelineInstance()
    : m_stateVector (),
      m_pipeline    (VK_NULL_HANDLE) { }

    DxvkComputePipelineInstance(
      const DxvkComputePipelineStateInfo& state,
            VkPipeline                    pipe)
    : m_stateVector (state),
      m_pipeline    (pipe) { }

    /**
     * \brief Checks for matching pipeline state
     * 
     * \param [in] state Compute pipeline state
     * \returns \c true if the specialization is compatible
     */
    bool isCompatible(const DxvkComputePipelineStateInfo& state) const {
      return m_stateVector == state;
    }

    /**
     * \brief Retrieves pipeline
     * \returns The pipeline handle
     */
    VkPipeline pipeline() const {
      return m_pipeline;
    }

  private:

    DxvkComputePipelineStateInfo m_stateVector;
    VkPipeline                   m_pipeline;

  };
  
  
  /**
   * \brief Compute pipeline
   * 
   * Stores a compute pipeline object and the corresponding
   * pipeline layout. Unlike graphics pipelines, compute
   * pipelines do not need to be recompiled against any sort
   * of pipeline state.
   */
  class DxvkComputePipeline {
    
  public:
    
    DxvkComputePipeline(
            DxvkPipelineManager*        pipeMgr,
            DxvkComputePipelineShaders  shaders);

    ~DxvkComputePipeline();
    
    /**
     * \brief Shaders used by the pipeline
     * \returns Shaders used by the pipeline
     */
    const DxvkComputePipelineShaders& shaders() const {
      return m_shaders;
    }
    
    /**
     * \brief Pipeline layout
     * 
     * Stores the pipeline layout and the descriptor set
     * layout, as well as information on the resource
     * slots used by the pipeline.
     * \returns Pipeline layout
     */
    DxvkPipelineLayout* layout() const {
      return m_layout.ptr();
    }
    
    /**
     * \brief Retrieves pipeline handle
     * 
     * \param [in] state Pipeline state
     * \returns Pipeline handle
     */
    VkPipeline getPipelineHandle(
      const DxvkComputePipelineStateInfo& state);
    
    /**
     * \brief Compiles a pipeline
     * 
     * Asynchronously compiles the given pipeline
     * and stores the result for future use.
     * \param [in] state Pipeline state
     */
    void compilePipeline(
      const DxvkComputePipelineStateInfo& state);
    
  private:
    
    Rc<vk::DeviceFn>            m_vkd;
    DxvkPipelineManager*        m_pipeMgr;

    DxvkComputePipelineShaders  m_shaders;
    DxvkDescriptorSlotMapping   m_slotMapping;
    
    Rc<DxvkPipelineLayout>      m_layout;
    
    sync::Spinlock                           m_mutex;
    std::vector<DxvkComputePipelineInstance> m_pipelines;
    // Note: A special pipeline to be used when spec constants are known to not be in use to avoid
    // needing to rely on cached spec constant state to compile pipelines in advance.
    std::optional<DxvkComputePipelineInstance> m_noSpecConstantPipelines;
    
    DxvkComputePipelineInstance* createInstance(
      const DxvkComputePipelineStateInfo& state);
    
    DxvkComputePipelineInstance* findInstance(
      const DxvkComputePipelineStateInfo& state);
    
    VkPipeline createPipeline(
      const DxvkComputePipelineStateInfo& state) const;
    
    void destroyPipeline(
            VkPipeline                    pipeline);

    void writePipelineStateToCache(
      const DxvkComputePipelineStateInfo& state) const;
    
  };
  
}