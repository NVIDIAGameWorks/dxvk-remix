/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "dxvk_device.h"
#include "dxvk_instance.h"
#include "rtx_render/rtx_context.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_ray_reconstruction.h"
#include "rtx_render/rtx_texture_manager.h"
#include "rtx_render/rtx_neural_radiance_cache.h"
#include "rtx_render/rtx_rtxdi_rayquery.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "rtx_render/rtx_composite.h"
#include "rtx_render/rtx_debug_view.h"
#include "rtx_render/rtx_xess.h"
#include "rtx_render/rtx_fsr.h"


namespace dxvk {
  
  DxvkDevice::DxvkDevice(
    const Rc<vk::InstanceFn>&       vki,
    const Rc<DxvkInstance>&         instance,
    const Rc<DxvkAdapter>&          adapter,
    const Rc<vk::DeviceFn>&         vkd,
    const DxvkDeviceExtensions&     extensions,
    const DxvkDeviceFeatures&       features,
    const DxvkAdapterQueueInfos&    adapterQueueInfos)
  : m_options           (instance->options()),
    m_instance          (instance),
    m_adapter           (adapter),
    m_vkd               (vkd),
    m_extensions        (extensions),
    m_features          (features),
    m_properties        (adapter->devicePropertiesExt()),
    m_perfHints         (getPerfHints()),
    m_objects           (this),
    m_submissionQueue   (this) {
    // NV-DXVK start: DLFG + RTXIO
    // Get desired queues from the device

    m_queues.graphics = getQueue(adapterQueueInfos.graphics.queueFamilyIndex, adapterQueueInfos.graphics.queueIndex);
    m_queues.transfer = getQueue(adapterQueueInfos.transfer.queueFamilyIndex, adapterQueueInfos.transfer.queueIndex);

    if (adapterQueueInfos.asyncCompute.has_value()) {
      m_queues.asyncCompute = getQueue(adapterQueueInfos.asyncCompute->queueFamilyIndex, adapterQueueInfos.asyncCompute->queueIndex);
    }

    if (adapterQueueInfos.present.has_value()) {
      m_queues.present = getQueue(adapterQueueInfos.present->queueFamilyIndex, adapterQueueInfos.present->queueIndex);
    }

    // NV-DXVK start: FSR FG integration
    if (adapterQueueInfos.imageAcquire.has_value()) {
      m_queues.imageAcquire = getQueue(adapterQueueInfos.imageAcquire->queueFamilyIndex, adapterQueueInfos.imageAcquire->queueIndex);
    }
    
    if (adapterQueueInfos.fsrPresent.has_value()) {
      m_queues.fsrPresent = getQueue(adapterQueueInfos.fsrPresent->queueFamilyIndex, adapterQueueInfos.fsrPresent->queueIndex);
    }
    // NV-DXVK end

    if (__DLFG_QUEUE_INFO_CHECK(adapterQueueInfos)) {
      // Note: When DLFG is active a separate queue is used for out of band rendering/presentation, so it should be marked accordingly.
      // Additionally, we do not mark the out of band render queue here as apparently it should only be marked when the out of band rendering
      // is sequential to application work, whereas out DLFG rendering work is overlapped with application rendering work.
      // Todo: Should we be calling this even when DLFG is disabled? Probably shouldn't matter since no OOB Presents
      // will be used in such a case, but something to consider if Reflex behaves weirdly when DLFG is disabled.
      m_objects.metaReflex().markOutOfBandPresentQueue(m_queues.__DLFG_QUEUE.queueHandle);
    }
    // NV-DXVK end

 #ifdef TRACY_ENABLE
    VkCommandPoolCreateInfo poolInfo;
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queues.graphics.queueFamily;

    if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_queues.graphics.tracyPool) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to create graphics command pool");

    VkCommandBufferAllocateInfo cmdInfoTracy;
    cmdInfoTracy.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfoTracy.pNext = nullptr;
    cmdInfoTracy.commandPool = m_queues.graphics.tracyPool;
    cmdInfoTracy.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfoTracy.commandBufferCount = 1;

    if (m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoTracy, &m_queues.graphics.tracyCmdList) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to allocate command buffer");

    m_queues.graphics.tracyCtx = TracyVkContextCalibrated(m_adapter->handle(),
                                                          m_vkd->device(),
                                                          m_queues.graphics.queueHandle,
                                                          m_queues.graphics.tracyCmdList,
                                                          vki->vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                                                          m_vkd->vkGetCalibratedTimestampsEXT);
    TracyVkContextName(m_queues.graphics.tracyCtx, "Graphics Queue", strlen("Graphics Queue"));

    if (m_queues.present.queueHandle) {
      poolInfo.queueFamilyIndex = m_queues.present.queueFamily;

      if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_queues.present.tracyPool) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to create present command pool");

      cmdInfoTracy.commandPool = m_queues.present.tracyPool;

      if (m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoTracy, &m_queues.present.tracyCmdList) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to allocate command buffer");

      m_queues.present.tracyCtx = TracyVkContextCalibrated(m_adapter->handle(),
                                                           m_vkd->device(),
                                                           m_queues.present.queueHandle,
                                                           m_queues.present.tracyCmdList,
                                                           vki->vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                                                           m_vkd->vkGetCalibratedTimestampsEXT);
      TracyVkContextName(m_queues.present.tracyCtx, "Present Queue", strlen("Present Queue"));
    }
#endif
  }

  
  
  DxvkDevice::~DxvkDevice() {
    // Wait for all pending Vulkan commands to be
    // executed before we destroy any resources.
    this->waitForIdle();

    // NV-DXVK start: RTX initializer
    m_objects.getRtxInitializer().release();
    // NV-DXVK end

#ifdef TRACY_ENABLE
    if (m_queues.graphics.tracyCtx) {
      TracyVkDestroy(m_queues.graphics.tracyCtx);
    }

    if (m_queues.graphics.tracyPool) {
      m_vkd->vkDestroyCommandPool(m_vkd->device(), m_queues.graphics.tracyPool, nullptr);
    }

    if (m_queues.present.queueHandle) {
      if (m_queues.present.tracyCtx) {
        TracyVkDestroy(m_queues.present.tracyCtx);
      }
      if (m_queues.present.tracyPool) {
        m_vkd->vkDestroyCommandPool(m_vkd->device(), m_queues.present.tracyPool, nullptr);
      }
    }
#endif
    // Stop workers explicitly in order to prevent
    // access to structures that are being destroyed.
    m_objects.pipelineManager().stopWorkerThreads();
  }


  bool DxvkDevice::isUnifiedMemoryArchitecture() const {
    return m_adapter->isUnifiedMemoryArchitecture();
  }


  DxvkFramebufferSize DxvkDevice::getDefaultFramebufferSize() const {
    return DxvkFramebufferSize {
      m_properties.core.properties.limits.maxFramebufferWidth,
      m_properties.core.properties.limits.maxFramebufferHeight,
      m_properties.core.properties.limits.maxFramebufferLayers };
  }


  VkPipelineStageFlags DxvkDevice::getShaderPipelineStages() const {
    VkPipelineStageFlags result = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    
    if (m_features.core.features.geometryShader)
      result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    
    if (m_features.core.features.tessellationShader) {
      result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT
             |  VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    }

    return result;
  }


  DxvkDeviceOptions DxvkDevice::options() const {
    DxvkDeviceOptions options;
    options.maxNumDynamicUniformBuffers = m_properties.core.properties.limits.maxDescriptorSetUniformBuffersDynamic;
    options.maxNumDynamicStorageBuffers = m_properties.core.properties.limits.maxDescriptorSetStorageBuffersDynamic;
    return options;
  }
  
  
  Rc<DxvkCommandList> DxvkDevice::createCommandList() {
    Rc<DxvkCommandList> cmdList = m_recycledCommandLists.retrieveObject();
    
    if (cmdList == nullptr)
      cmdList = new DxvkCommandList(this);
    
    return cmdList;
  }


  Rc<DxvkDescriptorPool> DxvkDevice::createDescriptorPool() {
    Rc<DxvkDescriptorPool> pool = m_recycledDescriptorPools.retrieveObject();

    if (pool == nullptr)
      // NV-DXVK start: use EXT_debug_utils
      pool = new DxvkDescriptorPool(m_instance->vki(), m_vkd);
      // NV-DXVK end
    
    return pool;
  }
  
  
  Rc<DxvkContext> DxvkDevice::createContext() {
    return new DxvkContext(this);
  }

  Rc<RtxContext> DxvkDevice::createRtxContext() {
    return new RtxContext(this);
  }


  Rc<DxvkGpuEvent> DxvkDevice::createGpuEvent() {
    return new DxvkGpuEvent(m_vkd);
  }


  Rc<DxvkGpuQuery> DxvkDevice::createGpuQuery(
          VkQueryType           type,
          VkQueryControlFlags   flags,
          uint32_t              index) {
    return new DxvkGpuQuery(m_vkd, type, flags, index);
  }
  
  
  Rc<DxvkFramebuffer> DxvkDevice::createFramebuffer(
    const DxvkFramebufferInfo&  info) {
    return new DxvkFramebuffer(m_vkd, info);
  }
  
  
  Rc<DxvkBuffer> DxvkDevice::createBuffer(
    const DxvkBufferCreateInfo& createInfo,
          VkMemoryPropertyFlags memoryType,
          DxvkMemoryStats::Category category,
// NV-DXVK start: add debug names to VkBuffer objects
          const char* name) {
    return new DxvkBuffer(this, createInfo, m_objects.memoryManager(), memoryType, category, name);
// NV-DXVK end
  }


  // NV-DXVK start: implement acceleration structures
  Rc<DxvkAccelStructure> DxvkDevice::createAccelStructure(
      const DxvkBufferCreateInfo& createInfo,
            VkMemoryPropertyFlags memoryType,
            VkAccelerationStructureTypeKHR accelType,
      const char* name) {
    return new DxvkAccelStructure(this, createInfo, m_objects.memoryManager(), memoryType, accelType, name);
  }
  // NV-DXVK end
  
  Rc<DxvkBufferView> DxvkDevice::createBufferView(
    const Rc<DxvkBuffer>&           buffer,
    const DxvkBufferViewCreateInfo& createInfo) {
    return new DxvkBufferView(m_vkd, buffer, createInfo);
  }
  
  
  Rc<DxvkImage> DxvkDevice::createImage(
    const DxvkImageCreateInfo&  createInfo,
          VkMemoryPropertyFlags memoryType,
          DxvkMemoryStats::Category category,
// NV-DXVK start: add debug names to VkImage objects
          const char *name) {
    return new DxvkImage(this, createInfo, m_objects.memoryManager(), memoryType, category, name);
// NV-DXVK end
  }
  
  
  Rc<DxvkImage> DxvkDevice::createImageFromVkImage(
    const DxvkImageCreateInfo&  createInfo,
          VkImage               image) {
    return new DxvkImage(this, createInfo, image);
  }
  
  Rc<DxvkImageView> DxvkDevice::createImageView(
    const Rc<DxvkImage>&            image,
    const DxvkImageViewCreateInfo&  createInfo) {
    return new DxvkImageView(m_vkd, image, createInfo);
  }
  

  Rc<DxvkSampler> DxvkDevice::createSampler(
    const DxvkSamplerCreateInfo&  createInfo) {
    return new DxvkSampler(this, createInfo);
  }
  
  
  Rc<DxvkShader> DxvkDevice::createShader(
          VkShaderStageFlagBits     stage,
          uint32_t                  slotCount,
    const DxvkResourceSlot*         slotInfos,
    const DxvkInterfaceSlots&       iface,
    const SpirvCodeBuffer&          code) {
    return new DxvkShader(stage,
      slotCount, slotInfos, iface, code,
      DxvkShaderOptions(),
      DxvkShaderConstData());
  }
  
  
  DxvkStatCounters DxvkDevice::getStatCounters() {
    DxvkPipelineCount pipe = m_objects.pipelineManager().getPipelineCount();
    
    DxvkStatCounters result;
    result.setCtr(DxvkStatCounter::PipeCountGraphics, pipe.numGraphicsPipelines);
    result.setCtr(DxvkStatCounter::PipeCountCompute,  pipe.numComputePipelines);
    result.setCtr(DxvkStatCounter::PipeCompilerBusy,  m_objects.pipelineManager().isCompilingShaders());
    result.setCtr(DxvkStatCounter::GpuIdleTicks,      m_submissionQueue.gpuIdleTicks());

    std::lock_guard<sync::Spinlock> lock(m_statLock);
    result.merge(m_statCounters);
    return result;
  }
  
  
  DxvkMemoryStats DxvkDevice::getMemoryStats(uint32_t heap) {
    return m_objects.memoryManager().getMemoryStats(heap);
  }


  uint32_t DxvkDevice::getCurrentFrameId() const {
    // NV-DXVK start
    // ToDo: avoid returning kInvalidFrameIndex
    // NV-DXVK end
    return m_statCounters.getCtr(DxvkStatCounter::QueuePresentCount);
  }
  
  
  void DxvkDevice::initResources() {
    m_objects.dummyResources().clearResources(this);

    // NV-DXVK start: RTX initializer
    m_objects.getRtxInitializer().initialize();
    // NV-DXVK end
  }


// NV-DXVK start
  void DxvkDevice::registerShader(const Rc<DxvkShader>& shader, bool isRemixShader) {
    m_objects.pipelineManager().registerShader(shader, isRemixShader);
  }
// NV-DXVK end
  
  
  void DxvkDevice::presentImage(
    std::uint64_t                   cachedReflexFrameId,
    bool                            insertReflexPresentMarkers,
    std::uint32_t                   cachedAcquiredImageIndex,
    const Rc<vk::Presenter>&        presenter,
          DxvkSubmitStatus*         status
          ) {
    ScopedCpuProfileZone();
    
    status->result = VK_NOT_READY;

    // NV-DXVK start: Integrate Reflex

    DxvkPresentInfo presentInfo;
    presentInfo.presenter = presenter;
    presentInfo.cachedReflexFrameId = cachedReflexFrameId;
    presentInfo.insertReflexPresentMarkers = insertReflexPresentMarkers;
    presentInfo.cachedAcquiredImageIndex = cachedAcquiredImageIndex;

    m_submissionQueue.present(presentInfo, status);

    incrementPresentCount();

    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    if (m_presenterInFlight.ptr() != presenter.ptr()) {
      // if we're switching presenters, synchronize the old one to make sure nothing stays in flight
      // to ensure the correctness of reference count for presenter object
      synchronizePresenter();

      // stash the presenter object so we can synchronize with it if needed
      m_presenterInFlight = presenter;
    }
    // NV-DXVK end

    // NV-DXVK start: Global window handle accessor
    m_objects.setWindowHandle(presenter->getWindowHandle());
    // NV-DXVK end
  }

  void DxvkDevice::incrementPresentCount() {
    std::lock_guard<sync::Spinlock> statLock(m_statLock);
    m_statCounters.addCtr(DxvkStatCounter::QueuePresentCount, 1); // Increase getCurrentFrameId()
  }

  // NV-DXVK start: DLFG integration
  void DxvkDevice::setupFrameInterpolation(DxvkFrameInterpolationInfo parameters) {
    m_submissionQueue.setupFrameInterpolation(parameters);
  }
  // NV-DXVK end

  void DxvkDevice::submitCommandList(
    const Rc<DxvkCommandList>&      commandList,
          VkSemaphore               waitSync,
          VkSemaphore               wakeSync,
          bool                      insertReflexRenderMarkers /*= false*/,
          uint64_t                  cachedReflexFrameId /*= 0*/) {
    ScopedCpuProfileZone();
    DxvkSubmitInfo submitInfo;
    submitInfo.cmdList  = commandList;
    submitInfo.waitSync = waitSync;
    submitInfo.wakeSync = wakeSync;
    submitInfo.insertReflexRenderMarkers = insertReflexRenderMarkers;
    submitInfo.cachedReflexFrameId = cachedReflexFrameId;
    m_submissionQueue.submit(submitInfo);

    std::lock_guard<sync::Spinlock> statLock(m_statLock);
    m_statCounters.merge(commandList->statCounters());
    m_statCounters.addCtr(DxvkStatCounter::QueueSubmitCount, 1);
  }
  
  
  VkResult DxvkDevice::waitForSubmission(DxvkSubmitStatus* status) {
    VkResult result = status->result.load();

    if (result == VK_NOT_READY) {
      m_submissionQueue.synchronizeSubmission(status);
      result = status->result.load();
    }

    return result;
  }


  void DxvkDevice::waitForResource(const Rc<DxvkResource>& resource, DxvkAccess access) {
    if (resource->isInUse(access)) {
      auto t0 = dxvk::high_resolution_clock::now();

      m_submissionQueue.synchronizeUntil([resource, access] {
        return !resource->isInUse(access);
      });

      auto t1 = dxvk::high_resolution_clock::now();
      auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

      std::lock_guard<sync::Spinlock> lock(m_statLock);
      m_statCounters.addCtr(DxvkStatCounter::GpuSyncCount, 1);
      m_statCounters.addCtr(DxvkStatCounter::GpuSyncTicks, us.count());
    }
  }
  
  
  void DxvkDevice::waitForIdle() {
    ScopedCpuProfileZone();
    this->lockSubmission();
    
    // NV-DXVK start: DLFG integration
    // idle DLFG so we can call vkDeviceWaitIdle safely
    synchronizePresenter();
    // NV-DXVK end

    if (m_vkd->vkDeviceWaitIdle(m_vkd->device()) != VK_SUCCESS)
      Logger::err("DxvkDevice: waitForIdle: Operation failed");
    this->unlockSubmission();
  }
  
  
  DxvkDevicePerfHints DxvkDevice::getPerfHints() {
    DxvkDevicePerfHints hints;
    hints.preferFbDepthStencilCopy = m_extensions.extShaderStencilExport
      && (m_adapter->matchesDriver(DxvkGpuVendor::Amd, VK_DRIVER_ID_MESA_RADV_KHR, 0, 0)
       || m_adapter->matchesDriver(DxvkGpuVendor::Amd, VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR, 0, 0)
       || m_adapter->matchesDriver(DxvkGpuVendor::Amd, VK_DRIVER_ID_AMD_PROPRIETARY_KHR, 0, 0));
    hints.preferFbResolve = m_extensions.amdShaderFragmentMask
      && (m_adapter->matchesDriver(DxvkGpuVendor::Amd, VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR, 0, 0)
       || m_adapter->matchesDriver(DxvkGpuVendor::Amd, VK_DRIVER_ID_AMD_PROPRIETARY_KHR, 0, 0));
    return hints;
  }


  void DxvkDevice::recycleCommandList(const Rc<DxvkCommandList>& cmdList) {
    m_recycledCommandLists.returnObject(cmdList);
  }
  

  void DxvkDevice::recycleDescriptorPool(const Rc<DxvkDescriptorPool>& pool) {
    m_recycledDescriptorPools.returnObject(pool);
  }


  DxvkDeviceQueue DxvkDevice::getQueue(
          uint32_t                family,
          uint32_t                index) const {
    VkQueue queue = VK_NULL_HANDLE;
    m_vkd->vkGetDeviceQueue(m_vkd->device(), family, index, &queue);
    return DxvkDeviceQueue { queue, family, index };
  }

  DxvkObjects::DxvkObjects(DxvkDevice* device)
    : m_device(device),
    m_memoryManager(device),
    m_renderPassPool(device),
    m_pipelineManager(device, &m_renderPassPool),
    m_eventPool(device),
    m_queryPool(device),
    m_sceneManager(device),
    m_rtResources(device),
    m_rtInitializer(device),
    m_textureManager { std::make_unique<RtxTextureManager>(device) },
    m_imgui(device),
    m_dummyResources(device),
    m_globalVolumetrics(device),
    m_pathtracerGbuffer(device),
    m_rtxdiRayQuery(device),
    m_restirgiRayQuery(device),
    m_pathtracerIntegrateDirect(device),
    m_pathtracerIntegrateIndirect(device),
    m_demodulate(device),
    m_neeCache(device),
    m_neuralRadianceCache(device),
    m_primaryDirectLightDenoiser(device, DenoiserType::DirectLight),
    m_primaryIndirectLightDenoiser(device, DenoiserType::IndirectLight),
    m_primaryCombinedLightDenoiser(device, DenoiserType::DirectAndIndirectLight),
    m_secondaryCombinedLightDenoiser(device, DenoiserType::Secondaries),
    m_ngxContext(device),
    m_dlfg(device),
    m_referenceDenoiserSecondLobe0(device, DenoiserType::Reference),
    m_referenceDenoiserSecondLobe1(device, DenoiserType::Reference),
    m_referenceDenoiserSecondLobe2(device, DenoiserType::Reference),
    m_dlss(device),
    m_rayReconstruction(device),
    m_nis(device),
    m_taa(device),
    m_xess(device),
    m_fsr(device),
    m_fsrFrameGen(device),
    m_composite(device),
    m_debug_view(device),
    m_autoExposure(device),
    m_toneMapping(device),
    m_localToneMapping(device),
    m_bloom(device),
    m_geometryUtils(device),
    m_imageUtils(device),
    m_postFx(device),
    m_capturer(new GameCapturer(device, m_sceneManager, m_exporter.get())),
    m_lastKnownWindowHandle((HWND) 0) { }

  RtxTextureManager& DxvkObjects::getTextureManager() {
    return *m_textureManager;
  }

  void DxvkObjects::onDestroy() {
    getRtxInitializer().onDestroy();

    metaGeometryUtils().onDestroy();
    getSceneManager().onDestroy();

    m_primaryDirectLightDenoiser.get().onDestroy();
    m_primaryIndirectLightDenoiser.get().onDestroy();
    m_primaryCombinedLightDenoiser.get().onDestroy();
    m_secondaryCombinedLightDenoiser.get().onDestroy();
    m_referenceDenoiserSecondLobe0.get().onDestroy();
    m_referenceDenoiserSecondLobe1.get().onDestroy();
    m_referenceDenoiserSecondLobe2.get().onDestroy();
    m_rayReconstruction.get().onDestroy();
    m_dlss.get().onDestroy();
    m_dlfg.get().onDestroy();
    m_fsrFrameGen.get().onDestroy();
  }
}
