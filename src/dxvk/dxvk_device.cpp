/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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


namespace dxvk {
  
  DxvkDevice::DxvkDevice(
    const Rc<DxvkInstance>&         instance,
    const Rc<DxvkAdapter>&          adapter,
    const Rc<vk::DeviceFn>&         vkd,
    const DxvkDeviceExtensions&     extensions,
    const DxvkDeviceFeatures&       features)
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
    auto queueFamilies = m_adapter->findQueueFamilies();
    m_queues.graphics = getQueue(queueFamilies.graphics, 0);
    m_queues.transfer = getQueue(queueFamilies.transfer, 0);

    // NV-DXVK start: RTXIO
    if (queueFamilies.asyncCompute != VK_QUEUE_FAMILY_IGNORED) {
      m_queues.asyncCompute = getQueue(queueFamilies.asyncCompute, 0);
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
                                                          m_vkd->vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                                                          m_vkd->vkGetCalibratedTimestampsEXT);
    TracyVkContextName(m_queues.graphics.tracyCtx, "Graphics Queue", strlen("Graphics Queue"));
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
    TracyVkDestroy(m_queues.graphics.tracyCtx);
    m_vkd->vkDestroyCommandPool(m_vkd->device(), m_queues.graphics.tracyPool, nullptr);
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
          DxvkMemoryStats::Category category) {
    return new DxvkBuffer(this, createInfo, m_objects.memoryManager(), memoryType, category);
  }


  // NV-DXVK start: implement acceleration structures
  Rc<DxvkAccelStructure> DxvkDevice::createAccelStructure(
      const DxvkBufferCreateInfo& createInfo,
            VkMemoryPropertyFlags memoryType,
            VkAccelerationStructureTypeKHR accelType) {
    return new DxvkAccelStructure(this, createInfo, m_objects.memoryManager(), memoryType, accelType);
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
    return new DxvkImage(m_vkd, createInfo, m_objects.memoryManager(), memoryType, category, name);
// NV-DXVK end
  }
  
  
  Rc<DxvkImage> DxvkDevice::createImageFromVkImage(
    const DxvkImageCreateInfo&  createInfo,
          VkImage               image) {
    return new DxvkImage(m_vkd, createInfo, image);
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


  void DxvkDevice::registerShader(const Rc<DxvkShader>& shader) {
    m_objects.pipelineManager().registerShader(shader);
  }
  
  
  void DxvkDevice::presentImage(
    std::uint64_t                   cachedReflexFrameId,
    const Rc<vk::Presenter>&        presenter,
          DxvkSubmitStatus*         status) {
    ScopedCpuProfileZone();
    
    status->result = VK_NOT_READY;

    // NV-DXVK start: Integrate Reflex

    // Note: End rendering now that presentation is desired to be queued up. This presentImage call is done on the
    // same CS thread that rendering was started on so this should be consistent with when a frame starts versus ends.
    // Additionally, it is possible that this could be called without a matching startRendering call for this frame due
    // to all the early outs injectRtx does, but Reflex should be able to deal with missing markers on a given frame.
    // If this becomes a problem in the future then we may need to handle adding in missing end markers in our own Reflex
    // integration somehow.
    m_objects.metaReflex().endRendering(cachedReflexFrameId);

    DxvkPresentInfo presentInfo;
    presentInfo.presenter = presenter;
    presentInfo.cachedReflexFrameId = cachedReflexFrameId;
    m_submissionQueue.present(presentInfo, status);
    
    {
      std::lock_guard<sync::Spinlock> statLock(m_statLock);
      m_statCounters.addCtr(DxvkStatCounter::QueuePresentCount, 1); // Increase getCurrentFrameId()
    }
    // NV-DXVK end
  }
  

  void DxvkDevice::submitCommandList(
    const Rc<DxvkCommandList>&      commandList,
          VkSemaphore               waitSync,
          VkSemaphore               wakeSync) {
    ScopedCpuProfileZone();
    DxvkSubmitInfo submitInfo;
    submitInfo.cmdList  = commandList;
    submitInfo.waitSync = waitSync;
    submitInfo.wakeSync = wakeSync;
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
  
  
  void DxvkDevice::waitForIdle() {
    ScopedCpuProfileZone();
    this->lockSubmission();
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
  
}
