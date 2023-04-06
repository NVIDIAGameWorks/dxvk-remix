#pragma once

#include <vector>

#include "dxvk_pipelayout.h"
#include "dxvk_shader.h"
#include "rtx_render/rtx_option.h"

namespace dxvk {

  class DxvkBuffer;
  class DxvkPipelineManager;

  struct DxvkRaytracingShaderGroup {
    Rc<DxvkShader> generalShader;
    Rc<DxvkShader> closestHitShader;
    Rc<DxvkShader> anyHitShader;
    Rc<DxvkShader> intersectionShader;
  };

  // Operators for DxvkRaytracingPipelineShaders::eq(...) implementation

  static bool operator==(const DxvkRaytracingShaderGroup& a, const DxvkRaytracingShaderGroup& b) {
    return a.generalShader == b.generalShader
      && a.closestHitShader == b.closestHitShader
      && a.anyHitShader == b.anyHitShader
      && a.intersectionShader == b.intersectionShader;
  }

  static bool operator!=(const DxvkRaytracingShaderGroup& a, const DxvkRaytracingShaderGroup& b) {
    return !(a == b);
  }

  /**
   * \brief Shaders used in raytracing pipelines
   */
  struct DxvkRaytracingPipelineShaders {
    // List of raytracing shader groups that contains all shaders for all 
    // RT groups (RGS, CHIT and MISS) in a raytracing pipeline state object (RTPSO).
    // Note regarding shader ordering:
    // - The shader order within the vector governs the shader record order 
    //   in a shader binding table (SBT) for each RT group separately.
    // - An RGS must be provided in the first group, and it defines the
    //   resource mappings for all shaders in the pipeline.
    // - The shaders for different RT groups can be sparsely interleaved.
    //   All that matters is the shader order within an SBT group.
    // - The order of all shaders within the vector defines a hash for RTPSO and 
    //   must stay consistent for frame to frame lookups. Changing it will result 
    //   in RTPSO recreation.
    std::vector<DxvkRaytracingShaderGroup> groups;
    VkPipelineCreateFlags pipelineFlags = 0;

    const char* debugName = nullptr;

    bool eq(const DxvkRaytracingPipelineShaders& other) const {
      return groups == other.groups && pipelineFlags == other.pipelineFlags;
    }

    size_t hash() const {

      if (!isHashCached) {
        DxvkHashState state;
        for (auto& group : groups) {
          state.add(DxvkShader::getHash(group.generalShader));
          state.add(DxvkShader::getHash(group.closestHitShader));
          state.add(DxvkShader::getHash(group.anyHitShader));
          state.add(DxvkShader::getHash(group.intersectionShader));
        }
        
        state.add(pipelineFlags);

        cachedHash = state;
        isHashCached = true;
      }

      return cachedHash;
    }

    void addGeneralShader(const Rc<DxvkShader>& shader) {
      groups.push_back({ shader, nullptr, nullptr, nullptr });
    }

    void addHitGroup(const Rc<DxvkShader>& closestHit, const Rc<DxvkShader>& anyHit, const Rc<DxvkShader>& intersection) {
      groups.push_back({ nullptr, closestHit, anyHit, intersection });
    }

  private:
    mutable size_t cachedHash = 0;
    mutable bool isHashCached = false;
  };

  class DxvkRaytracingPipeline {

  public:
    DxvkRaytracingPipeline(DxvkPipelineManager* pipeMgr, const DxvkRaytracingPipelineShaders& shaders);
    ~DxvkRaytracingPipeline();

    /**
     * \brief Retrieves pipeline handle
     * 
     * Compiles the pipeline in-place if it was
     * not compiled yet.
     * 
     * \returns Pipeline handle
     */
    VkPipeline getPipelineHandle();

    /**
     * \brief Compiles a pipeline
     *
     * Compiles the given pipeline potentially
     * asynchronously and stores the result for
     * future use.
     */
    void compilePipeline();

    const DxvkRaytracingPipelineShaders& shaders() const { return m_shaders; }

    DxvkPipelineLayout* layout() { return m_layout.ptr(); }

    VkStridedDeviceAddressRegionKHR   m_raygenShaderBindingTable{};
    VkStridedDeviceAddressRegionKHR   m_missShaderBindingTable{};
    VkStridedDeviceAddressRegionKHR   m_hitShaderBindingTable{};
    VkStridedDeviceAddressRegionKHR   m_callableShaderBindingTable{};

    static void releaseFinalizer();

  private:
    void createLayout();
    void createPipeline();
    void createShaderBindingTable();
    void releaseTmpResources();

    mutable dxvk::mutex               m_mutex;
    std::atomic_bool                  m_isCompiled;

    Rc<vk::DeviceFn>                  m_vkd;
    DxvkPipelineManager*              m_pipeMgr;
    Rc<DxvkBuffer>                    m_shaderBindingTableBuffer;

    std::vector<DxvkShaderModule>     m_shaderModules;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
    std::vector<VkPipelineShaderStageCreateInfo> m_stages;

    VkPipeline                        m_pipeline = VK_NULL_HANDLE;
    DxvkRaytracingPipelineShaders     m_shaders;
    Rc<DxvkPipelineLayout>            m_layout;

    RTX_OPTION("rtx.pipeline", bool, useDeferredOperations, true, "");
  };
}