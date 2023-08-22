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

#include <NRD.h>

#include "dxvk_context.h"
#include "rtx_denoise.h"
#include "rtx_nrd_settings.h"
#include "rtx_scene_manager.h"

namespace dxvk {
  class NRDContext : public CommonDeviceObject {

  public:
    NRDContext(DxvkDevice* device, DenoiserType type);
    ~NRDContext();

    void onDestroy();

    void dispatch(
      Rc<DxvkContext> ctx,
      DxvkBarrierSet& barriers,
      const SceneManager& sceneManager,
      const Resources::RaytracingOutput& rtOutput,
      const DxvkDenoise::Input& inputs,
      const DxvkDenoise::Output& outputs);

    void showImguiSettings();
    NrdArgs getNrdArgs() const;
    bool isReferenceDenoiserEnabled() const;

    const NrdSettings& getNrdSettings() const;
    void setNrdSettings(const NrdSettings& refSettings);

  private:
    void setSettingsOnInitialization(const DxvkDevice* device);
    void updateRuntimeSettings(const DxvkDenoise::Input& inputs);

    void prepareResources(
      Rc<DxvkContext> ctx,
      const Resources::RaytracingOutput& rtOutput);

    void createResources(Rc<DxvkContext> ctx, const Resources::RaytracingOutput& rtOutput);
    void createPipelines();

    const Resources::Resource* getTexture(
      const nrd::ResourceDesc& resource,
      const DxvkDenoise::Input& inputs,
      const DxvkDenoise::Output& outputs);

    void destroyResources();
    void destroyPipelines();

    void updateNRDSettings(
      const SceneManager& sceneManager,
      const DxvkDenoise::Input& inputs,
      const Resources::RaytracingOutput& rtOutput);

    void updateAdaptiveScaling(const VkExtent3D& renderSize);
    
    struct ComputePipeline
    {
      static constexpr uint32_t kInvalidIndex = UINT32_MAX;

      VkPipeline pipeline = VK_NULL_HANDLE;
      VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
      VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

      // Static sampler bindings are first
      std::vector<VkDescriptorSetLayoutBinding> bindings;
      uint32_t constantBufferIndex = kInvalidIndex;
      uint32_t resourcesStartIndex = kInvalidIndex;

      Rc<DxvkSampler> linearSampler;
    };

    VkPipelineLayout createPipelineLayout(VkDescriptorSetLayout dsetLayout);
    VkPipeline createPipeline(
      const nrd::ComputeShaderDesc& nrdCS,
      const nrd::PipelineDesc& nrdPipelineDesc,
      const VkPipelineLayout& pipelineLayout);

    Rc<vk::DeviceFn> m_vkd;
    DenoiserType m_type;

    // Settings
    NrdSettings m_settings;
    nrd::Method m_method = nrd::Method::MAX_NUM;
    bool m_resetResources = true;
    nrd::Denoiser* m_denoiser = nullptr;

    // Resources
    using Resource = Resources::Resource;
    std::vector<Resource> m_permanentTex;
    std::vector<std::shared_ptr<Resource>> m_transientTex;
    Resource m_validationTex;

    using SharedTransientPool = std::unordered_map<size_t, std::weak_ptr<Resource>>;
    inline static SharedTransientPool m_sharedTransientTex; // share these between all NRD instances

    std::vector<Rc<DxvkSampler>> m_staticSamplers;

    // Pipelines
    std::vector<ComputePipeline> m_computePipelines;
    std::unique_ptr<DxvkStagingDataAlloc> m_cbData;
  };
} // namespace dxvk
