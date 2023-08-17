#include "dxvk_cmdlist.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"

namespace dxvk {
    
  DxvkCommandList::DxvkCommandList(DxvkDevice* device)
  : m_device        (device),
    m_vkd           (device->vkd()),
    m_vki           (device->instance()->vki()),
    m_cmdBuffersUsed(0),
    m_descriptorPoolTracker(device) {
    const auto& graphicsQueue = m_device->queues().graphics;
    const auto& transferQueue = m_device->queues().transfer;

    VkFenceCreateInfo fenceInfo;
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = 0;
    
    if (m_vkd->vkCreateFence(m_vkd->device(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to create fence");
    
    VkCommandPoolCreateInfo poolInfo;
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext            = nullptr;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueue.queueFamily;
    
    if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_graphicsPool) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to create graphics command pool");
    
    if (m_device->hasDedicatedTransferQueue()) {
      poolInfo.queueFamilyIndex = transferQueue.queueFamily;

      if (m_vkd->vkCreateCommandPool(m_vkd->device(), &poolInfo, nullptr, &m_transferPool) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to create transfer command pool");
    }
    
    VkCommandBufferAllocateInfo cmdInfoGfx;
    cmdInfoGfx.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfoGfx.pNext             = nullptr;
    cmdInfoGfx.commandPool       = m_graphicsPool;
    cmdInfoGfx.level             = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfoGfx.commandBufferCount = 1;
    
    VkCommandBufferAllocateInfo cmdInfoDma;
    cmdInfoDma.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfoDma.pNext             = nullptr;
    cmdInfoDma.commandPool       = m_transferPool ? m_transferPool : m_graphicsPool;
    cmdInfoDma.level             = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfoDma.commandBufferCount = 1;
    
    if (m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoGfx, &m_execBuffer) != VK_SUCCESS
     || m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoGfx, &m_initBuffer) != VK_SUCCESS
     || m_vkd->vkAllocateCommandBuffers(m_vkd->device(), &cmdInfoDma, &m_sdmaBuffer) != VK_SUCCESS)
      throw DxvkError("DxvkCommandList: Failed to allocate command buffer");
    
    if (m_device->hasDedicatedTransferQueue()) {
      VkSemaphoreCreateInfo semInfo;
      semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      semInfo.pNext = nullptr;
      semInfo.flags = 0;

      if (m_vkd->vkCreateSemaphore(m_vkd->device(), &semInfo, nullptr, &m_sdmaSemaphore) != VK_SUCCESS)
        throw DxvkError("DxvkCommandList: Failed to create semaphore");
    }
  }
  
  
  DxvkCommandList::~DxvkCommandList() {
    this->reset();

    m_vkd->vkDestroySemaphore(m_vkd->device(), m_sdmaSemaphore, nullptr);
    
    m_vkd->vkDestroyCommandPool(m_vkd->device(), m_graphicsPool, nullptr);
    m_vkd->vkDestroyCommandPool(m_vkd->device(), m_transferPool, nullptr);
    
    m_vkd->vkDestroyFence(m_vkd->device(), m_fence, nullptr);
  }

  // NV-DXVK start: DLFG integration
  void DxvkCommandList::addWaitSemaphore(VkSemaphore waitSemaphore, uint64_t waitSemaphoreValue) {
    assert(!m_additionalWaitSemaphore);
    m_additionalWaitSemaphore = waitSemaphore;
    m_additionalWaitSemaphoreValue = waitSemaphoreValue;
  }

  void DxvkCommandList::addSignalSemaphore(VkSemaphore signalSemaphore, uint64_t signalSemaphoreValue) {
    assert(!m_additionalSignalSemaphore);
    m_additionalSignalSemaphore = signalSemaphore;
    m_additionalSignalSemaphoreValue = signalSemaphoreValue;
  }
  // NV-DXVK end

  VkResult DxvkCommandList::submit(
          VkSemaphore     waitSemaphore,
          VkSemaphore     wakeSemaphore,
          uint64_t        waitSemaphoreValue,
          uint64_t        wakeSemaphoreValue) {
    const auto& graphics = m_device->queues().graphics;
    const auto& transfer = m_device->queues().transfer;

    DxvkQueueSubmission info = DxvkQueueSubmission();

    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::SdmaBuffer)) {
      info.cmdBuffers[info.cmdBufferCount++] = m_sdmaBuffer;

      if (m_device->hasDedicatedTransferQueue()) {
        info.wakeSync[info.wakeCount++] = m_sdmaSemaphore;
        VkResult status = submitToQueue(transfer.queueHandle, VK_NULL_HANDLE, info);

        if (status != VK_SUCCESS)
          return status;

        info = DxvkQueueSubmission();
        info.waitSync[info.waitCount] = m_sdmaSemaphore;
        info.waitMask[info.waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        info.waitCount += 1;
      }
    }

    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::InitBuffer))
      info.cmdBuffers[info.cmdBufferCount++] = m_initBuffer;
    if (m_cmdBuffersUsed.test(DxvkCmdBuffer::ExecBuffer))
      info.cmdBuffers[info.cmdBufferCount++] = m_execBuffer;
    
    if (waitSemaphore) {
      assert(info.waitCount < (sizeof(info.waitSync) / sizeof(info.waitSync[0])));
      info.waitSync[info.waitCount] = waitSemaphore;
      info.waitMask[info.waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      info.waitValue[info.waitCount] = waitSemaphoreValue;
      info.waitCount += 1;
    }

    if (wakeSemaphore) {
      assert(info.wakeCount < (sizeof(info.wakeSync) / sizeof(info.wakeSync[0])));
      info.wakeSync[info.wakeCount] = wakeSemaphore;
      info.wakeValue[info.wakeCount] = wakeSemaphoreValue;
      info.wakeCount++;
    }

    // NV-DXVK start: DLFG integration
    if (m_additionalWaitSemaphore) {
      assert(info.waitCount < (sizeof(info.waitSync) / sizeof(info.waitSync[0])));
      info.waitSync[info.waitCount] = m_additionalWaitSemaphore;
      info.waitMask[info.waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      info.waitValue[info.waitCount] = m_additionalWaitSemaphoreValue;
      info.waitCount += 1;

      m_additionalWaitSemaphore = nullptr;
      m_additionalWaitSemaphoreValue = uint64_t(-1);
    }
    if (m_additionalSignalSemaphore) {
      assert(info.wakeCount < (sizeof(info.wakeSync) / sizeof(info.wakeSync[0])));
      info.wakeSync[info.wakeCount] = m_additionalSignalSemaphore;
      info.wakeValue[info.wakeCount] = m_additionalSignalSemaphoreValue;
      info.wakeCount += 1;

      m_additionalSignalSemaphore = nullptr;
      m_additionalSignalSemaphoreValue = uint64_t(-1);
    }
    // NV-DXVK end

    return submitToQueue(graphics.queueHandle, m_fence, info);
  }
  
  
  VkResult DxvkCommandList::synchronize() {
    ScopedCpuProfileZone();
    VkResult status = VK_TIMEOUT;
    
    while (status == VK_TIMEOUT) {
      status = m_vkd->vkWaitForFences(
        m_vkd->device(), 1, &m_fence, VK_FALSE,
        1'000'000'000ull);
    }
    
    return status;
  }
  
  
  void DxvkCommandList::beginRecording() {
    VkCommandBufferBeginInfo info;
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext            = nullptr;
    info.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    info.pInheritanceInfo = nullptr;
    
    if ((m_graphicsPool && m_vkd->vkResetCommandPool(m_vkd->device(), m_graphicsPool, 0) != VK_SUCCESS)
     || (m_transferPool && m_vkd->vkResetCommandPool(m_vkd->device(), m_transferPool, 0) != VK_SUCCESS))
      Logger::err("DxvkCommandList: Failed to reset command buffer");
    
    if (m_vkd->vkBeginCommandBuffer(m_execBuffer, &info) != VK_SUCCESS
     || m_vkd->vkBeginCommandBuffer(m_initBuffer, &info) != VK_SUCCESS
     || m_vkd->vkBeginCommandBuffer(m_sdmaBuffer, &info) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to begin command buffer");
    
    if (m_vkd->vkResetFences(m_vkd->device(), 1, &m_fence) != VK_SUCCESS)
      Logger::err("DxvkCommandList: Failed to reset fence");
    
    // Unconditionally mark the exec buffer as used. There
    // is virtually no use case where this isn't correct.
    m_cmdBuffersUsed = DxvkCmdBuffer::ExecBuffer;
  }
  
  
  void DxvkCommandList::endRecording() {
    TracyVkCollect(m_device->queues().graphics.tracyCtx, m_execBuffer);

    if (m_vkd->vkEndCommandBuffer(m_execBuffer) != VK_SUCCESS
     || m_vkd->vkEndCommandBuffer(m_initBuffer) != VK_SUCCESS
     || m_vkd->vkEndCommandBuffer(m_sdmaBuffer) != VK_SUCCESS)
      Logger::err("DxvkCommandList::endRecording: Failed to record command buffer");
  }
  
  
  void DxvkCommandList::reset() {
    // Signal resources and events to
    // avoid stalling main thread
    m_signalTracker.reset();
    m_resources.reset();

    // Recycle heavy Vulkan objects
    m_descriptorPoolTracker.reset();

    // Return buffer memory slices
    m_bufferTracker.reset();

    // Return query and event handles
    m_gpuQueryTracker.reset();
    m_gpuEventTracker.reset();

    // Less important stuff
    m_statCounters.reset();
  }


  VkResult DxvkCommandList::submitToQueue(
          VkQueue               queue,
          VkFence               fence,
    const DxvkQueueSubmission&  info) {

    // NV-DXVK: DLFG integration
    VkTimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.pNext = nullptr;
    timelineInfo.waitSemaphoreValueCount = info.waitCount;
    timelineInfo.pWaitSemaphoreValues = info.waitValue;
    timelineInfo.signalSemaphoreValueCount = info.wakeCount;
    timelineInfo.pSignalSemaphoreValues = info.wakeValue;
    // NV-DXVK end
    
    VkSubmitInfo submitInfo;
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    // NV-DXVK: DLFG integration
    submitInfo.pNext                = &timelineInfo;
    // NV-DXVK end
    submitInfo.waitSemaphoreCount   = info.waitCount;
    submitInfo.pWaitSemaphores      = info.waitSync;
    submitInfo.pWaitDstStageMask    = info.waitMask;
    submitInfo.commandBufferCount   = info.cmdBufferCount;
    submitInfo.pCommandBuffers      = info.cmdBuffers;
    submitInfo.signalSemaphoreCount = info.wakeCount;
    submitInfo.pSignalSemaphores    = info.wakeSync;
    
    return m_vkd->vkQueueSubmit(queue, 1, &submitInfo, fence);
  }
  
  void DxvkCommandList::cmdBeginDebugUtilsLabel(VkDebugUtilsLabelEXT *pLabelInfo) {
    // NV-DXVK start: add debug names to VkImage objects
    m_vkd->vkCmdBeginDebugUtilsLabelEXT(m_execBuffer, pLabelInfo);
    // NV-DXVK end
  }

  void DxvkCommandList::cmdEndDebugUtilsLabel() {
    // NV-DXVK start: add debug names to VkImage objects
    m_vkd->vkCmdEndDebugUtilsLabelEXT(m_execBuffer);
    // NV-DXVK end
  }

  void DxvkCommandList::cmdInsertDebugUtilsLabel(VkDebugUtilsLabelEXT *pLabelInfo) {
    // NV-DXVK start: add debug names to VkImage objects
    m_vkd->vkCmdInsertDebugUtilsLabelEXT(m_execBuffer, pLabelInfo);
    // NV-DXVK end
  }
}
