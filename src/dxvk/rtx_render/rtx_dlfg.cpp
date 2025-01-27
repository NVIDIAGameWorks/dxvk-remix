#include "dxvk_device.h"
#include "rtx_dlfg.h"

namespace dxvk {
  template<uint numBarriers>
  class DxvkDLFGImageBarrierSet {
  public:
    void addBarrier(VkImage image,
                    VkImageAspectFlags aspect,
                    VkAccessFlags srcAccess,
                    VkAccessFlags dstAccess,
                    VkImageLayout sourceLayout,
                    VkImageLayout targetLayout,
                    uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED) {
      assert(m_barrierCount < m_barriers.size());

      auto& b = m_barriers[m_barrierCount];

      VkImageSubresourceRange range;
      range.aspectMask = aspect;
      range.baseMipLevel = 0;
      range.levelCount = VK_REMAINING_MIP_LEVELS;
      range.baseArrayLayer = 0;
      range.layerCount = VK_REMAINING_ARRAY_LAYERS;

      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.pNext = nullptr;
      b.srcAccessMask = srcAccess;
      b.dstAccessMask = dstAccess;
      b.oldLayout = sourceLayout;
      b.newLayout = targetLayout;
      b.srcQueueFamilyIndex = srcQueueFamilyIndex;
      b.dstQueueFamilyIndex = dstQueueFamilyIndex;
      b.image = image;
      b.subresourceRange = range;
      b.subresourceRange.aspectMask = aspect;

      m_barrierCount++;
    };

    void record(DxvkDevice* device, DxvkDLFGCommandList& cmdList, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
      VkCommandBuffer cmdBuf = cmdList.getCmdBuffer();
      device->vkd()->vkCmdPipelineBarrier(cmdBuf,
                                          srcStage,
                                          dstStage,
                                          0,
                                          0, nullptr,
                                          0, nullptr,
                                          m_barrierCount, m_barriers.data());
      m_barrierCount = 0;
    }

  private:
    std::array<VkImageMemoryBarrier, numBarriers> m_barriers;
    uint32_t m_barrierCount = 0;
  };

  static void labelSemaphore(Rc<DxvkDevice>& m_device, VkSemaphore semaphore, const char* name) {
    if (m_device->vkd()->vkSetDebugUtilsObjectNameEXT) {
      VkDebugUtilsObjectNameInfoEXT nameInfo;
      nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
      nameInfo.pNext = nullptr;
      nameInfo.objectType = VK_OBJECT_TYPE_SEMAPHORE;
      nameInfo.objectHandle = (uint64_t) semaphore;
      nameInfo.pObjectName = name;
      m_device->vkd()->vkSetDebugUtilsObjectNameEXT(m_device->handle(), &nameInfo);
    }
  }

  void DxvkDLFGCommandList::submit() {
    ScopedCpuProfileZone();

    VkTimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.pNext = nullptr;
    timelineInfo.waitSemaphoreValueCount = m_numWaitSemaphores;
    timelineInfo.pWaitSemaphoreValues = m_waitSemaphoreValues.data();
    timelineInfo.signalSemaphoreValueCount = m_numSignalSemaphores;
    timelineInfo.pSignalSemaphoreValues = m_signalSemaphoreValues.data();

    VkPipelineStageFlags waitMask[kMaxSemaphores];
    for (uint32_t i = 0; i < kMaxSemaphores; i++) {
      waitMask[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = m_numWaitSemaphores;
    submitInfo.pWaitSemaphores = m_waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitMask;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBuf;
    submitInfo.signalSemaphoreCount = m_numSignalSemaphores;
    submitInfo.pSignalSemaphores = m_signalSemaphores.data();

    m_device->vkd()->vkQueueSubmit(m_device->queues().__DLFG_QUEUE.queueHandle, 1, &submitInfo, m_signalFence);
    // assert(m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle) == VK_SUCCESS);

    m_numWaitSemaphores = 0;
    m_numSignalSemaphores = 0;
    m_signalFence = nullptr;
  }

  DxvkDLFGPresenter::DxvkDLFGPresenter(Rc<DxvkDevice> device,
                                       Rc<DxvkContext> ctx,
                                       HWND window,
                                       const Rc<vk::InstanceFn>& vki,
                                       const Rc<vk::DeviceFn>& vkd,
                                       vk::PresenterDevice presenterDevice,
                                       const vk::PresenterDesc& desc)
    : vk::Presenter(window, vki, vkd, presenterDevice, desc)
    , m_device(device.ptr())
    , m_ctx(ctx)
    , m_backbufferIndex(0)
    , m_blitCommandLists(device.ptr(), 2)
    , m_presentPacingCommandLists(device.ptr(), 1)
    , m_dlfgBarrierCommandLists(device.ptr(), 2)
    , m_dlfgBeginSemaphore(RtxSemaphore::createBinary(device.ptr(), "DLFG begin"))
    , m_syncSemaphore(RtxSemaphore::createBinary(device.ptr(), "DLFG Sync"))
    , m_dlfgFrameEndSemaphore(m_device->getCommon()->metaDLFG().getFrameEndSemaphore())
    , m_dlfgPacerSemaphore(RtxSemaphore::createTimeline(m_device, "DLFG pacer CPU semaphore"))
    , m_dlfgPacerToPresentSemaphore(RtxSemaphore::createBinary(m_device, "DLFG pacer present semaphore")) {
    
    // vk::Presenter ctor calls into the base class implementation of recreateSwapchain and not our override,
    // so we need to create the backbuffers explicitly
    std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);
    assert(m_presentQueue.empty());
    createBackbuffers();

    m_presentThread.threadHandle = dxvk::thread([this]() { runPresentThread(); });
    m_pacerThread.threadHandle = dxvk::thread([this]() { runPacerThread(); });
  }

  DxvkDLFGPresenter::~DxvkDLFGPresenter() {
    if (m_presentThread.threadHandle.joinable()) {
      {
        std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);
        m_presentThread.stopped.store(true);
        m_presentThread.condWorkAvailable.notify_all();
      }

      m_presentThread.threadHandle.join();
    }

    if (m_pacerThread.threadHandle.joinable()) {
      {
        std::unique_lock<dxvk::mutex> lock(m_pacerThread.mutex);
        m_pacerThread.stopped.store(true);
        m_pacerThread.condWorkAvailable.notify_all();
      }

      m_pacerThread.threadHandle.join();
    }
  }

  vk::PresenterImage DxvkDLFGPresenter::getImage(uint32_t index) const {
    vk::PresenterImage ret;
    ret.image = m_backbufferImages[index]->handle();
    ret.view = m_backbufferViews[index]->handle();

    return ret;
  }

  VkResult DxvkDLFGPresenter::acquireNextImage(vk::PresenterSync& sync, uint32_t& index) {
    ScopedCpuProfileZone();

    VkResult lastStatus = m_lastPresentStatus;
    if (lastStatus != VK_SUCCESS) {
      return lastStatus;
    }

    m_backbufferIndex = (m_backbufferIndex + 1) % m_info.imageCount;

    // stall until the image is available
    {
      std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);
      m_presentThread.condWorkConsumed.wait(lock, [this] { return m_backbufferInFlight[m_backbufferIndex] == false; });
    }

    index = m_backbufferIndex;
    sync.acquire = m_backbufferAcquireSemaphores[m_backbufferIndex]->handle();
    sync.present = m_backbufferPresentSemaphores[m_backbufferIndex]->handle();

    return VK_SUCCESS;
  }

  VkResult DxvkDLFGPresenter::presentImage(std::atomic<VkResult>* status,
                                           const DxvkPresentInfo& presentInfo,
                                           const DxvkFrameInterpolationInfo& frameInterpolationInfo,
                                           std::uint32_t acquiredImageIndex) {
    VkResult lastStatus = m_lastPresentStatus;
    if (lastStatus != VK_SUCCESS) {
      *status = lastStatus;
      return lastStatus;
    }

    *status = VK_EVENT_SET;

    {
      std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);

      assert(m_backbufferInFlight[acquiredImageIndex] == false);
      m_backbufferInFlight[acquiredImageIndex] = true;

      m_presentQueue.push({ status, acquiredImageIndex, presentInfo, frameInterpolationInfo });

      m_presentThread.condWorkAvailable.notify_all();
    }

    return VK_EVENT_SET;
  }

  VkResult DxvkDLFGPresenter::recreateSwapChain(const vk::PresenterDesc& desc) {
    std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);
    synchronize(lock);

    VkResult res = vk::Presenter::recreateSwapChain(desc);
    if (res != VK_SUCCESS) {
      return res;
    }

    createBackbuffers();
    return res;
  }

  void DxvkDLFGPresenter::createBackbuffers() {
    // note: assumes queue is idle and m_presentThread.mutex is locked
    assert(m_presentQueue.empty());

    DxvkImageCreateInfo info;
    info.type             = VK_IMAGE_TYPE_2D;
    info.format           = m_info.format.format;
    info.flags            = 0;
    info.sampleCount      = VK_SAMPLE_COUNT_1_BIT;
    info.extent           = { m_info.imageExtent.width, m_info.imageExtent.height, 1 };
    info.numLayers        = 1;
    info.mipLevels        = 1;
    info.usage            = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.stages           = 0;
    info.access           = 0;
    info.tiling           = VK_IMAGE_TILING_OPTIMAL;
    info.layout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    info.shared           = VK_FALSE;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format       = m_info.format.format;
    viewInfo.usage        = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    viewInfo.aspect       = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel     = 0;
    viewInfo.numLevels    = 1;
    viewInfo.minLayer     = 0;
    viewInfo.numLayers    = 1;

    m_backbufferImages.resize(m_info.imageCount);
    m_backbufferViews.resize(m_info.imageCount);
    m_backbufferAcquireSemaphores.resize(m_info.imageCount);
    m_backbufferPresentSemaphores.resize(m_info.imageCount);
    m_backbufferInFlight.resize(m_info.imageCount);
    
    Rc<DxvkDLFGCommandList> dummyCmdList = new DxvkDLFGCommandList(m_device);
    dummyCmdList->beginRecording();
    
    for (uint32_t i = 0; i < m_info.imageCount; i++) {
      m_backbufferImages[i] = m_device->createImage(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::RTXRenderTarget, "DLFG backbuffer");
      m_backbufferViews[i] = m_device->createImageView(m_backbufferImages[i], viewInfo);

      char buf[32];
      snprintf(buf, sizeof(buf), "backbuffer acquire %d", i);
      m_backbufferAcquireSemaphores[i] = RtxSemaphore::createBinary(m_device, buf);

      snprintf(buf, sizeof(buf), "backbuffer present %d", i);
      m_backbufferPresentSemaphores[i] = RtxSemaphore::createBinary(m_device, buf);

      m_backbufferInFlight[i] = false;

      // we just created the images, so acquire semaphores need to be signaled
      dummyCmdList->addSignalSemaphore(m_backbufferAcquireSemaphores[i]->handle());
    }

    dummyCmdList->endRecording();

#if !__DLFG_USE_GRAPHICS_QUEUE
    dummyCmdList->submit();
    m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle);
#else
    // submitting to the graphics queue here means we're racing with the submit thread;
    // we'll wait on unsignaled semaphores for the first imageCount frames instead, which works fine in practice on Windows
#endif
    
    m_backbufferIndex = 0;

    // if we're here, the swapchain was just (re)created
    // create image/view wrappers and mark all swapchain images as undefined so we transition them properly
    m_swapchainImages.resize(m_info.imageCount);
    m_swapchainImageViews.resize(m_info.imageCount);
    m_swapchainImageLayouts.resize(m_info.imageCount);

    // these need to match the swapchain usage bits
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    viewInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    
    for (uint32_t i = 0; i < m_info.imageCount; i++) {
      vk::PresenterImage swapImage = vk::Presenter::getImage(i);

      m_swapchainImages[i] = new DxvkImage(m_device, info, swapImage.image);
      m_swapchainImageViews[i] = new DxvkImageView(m_device->vkd(), m_swapchainImages[i], viewInfo);
      m_swapchainImageLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
  }

  void DxvkDLFGPresenter::synchronize() {
    std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);
    synchronize(lock);
  }

  void DxvkDLFGPresenter::synchronize(std::unique_lock<dxvk::mutex>& lock) {
    m_presentThread.condWorkConsumed.wait(lock, [this] { return m_presentQueue.empty(); });
    m_pacerThread.condWorkConsumed.wait(lock, [this] { return m_pacerQueue.empty(); });
  }

  void DxvkDLFGPresenter::runPresentThread() {
    ScopedCpuProfileZone();
    env::setThreadName("dxvk-dlfg-present");

    std::unique_lock<dxvk::mutex> lock(m_presentThread.mutex);

    DxvkDLFG& dlfg = m_device->getCommon()->metaDLFG();
    Rc<DxvkDLFGTimestampQueryPool> queryPoolDLFG = dlfg.getDLFGQueryPool();
    
    while (!m_presentThread.stopped.load()) {
      {
        ScopedCpuProfileZoneN("DLFG queue: wait");
        m_presentThread.condWorkAvailable.wait(lock, [this] { return m_presentThread.stopped.load() || !m_presentQueue.empty(); });
      }

      if (m_presentThread.stopped.load()) {
        // idle the queue here to ensure we can destroy objects if needed
        m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle);
        return;
      }

      PresentJob present = std::move(m_presentQueue.front());

      DxvkDLFGScopeGuard signalWorkConsumed([&]() {
        // m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle);
        present.status->store(m_lastPresentStatus);

        assert(m_backbufferInFlight[present.acquiredImageIndex] == true);
        m_backbufferInFlight[present.acquiredImageIndex] = false;

        m_presentQueue.pop();
        m_presentThread.condWorkConsumed.notify_all();
      });

      // if we have an error condition that hasn't been cleared yet, drop frames until recreateSwapchain is called
      if (m_lastPresentStatus != VK_SUCCESS) {
        continue;
      }

      PacerJob pacer;
      
      // acquire the next image from the VK swapchain
      vk::PresenterImage swapchainImage;
      vk::PresenterSync swapchainSync;
      uint32_t swapchainIndex;

      m_lastPresentStatus = vk::Presenter::acquireNextImage(swapchainSync, swapchainIndex);
      if (m_lastPresentStatus != VK_SUCCESS) {
        // got an error, bail until it's handled
        // xxxnsubtil: may need to signal the frame end semaphore here
        continue;
      }

      assert(swapchainIndex < m_blitCommandLists.size());
      swapchainImage = vk::Presenter::getImage(swapchainIndex);

      DxvkDLFGCommandList* m_cmdList = nullptr;
      DxvkDLFGImageBarrierSet<4> barriers;

      VkSemaphore backbufferWaitSemaphore = m_backbufferPresentSemaphores[present.acquiredImageIndex]->handle();

      // if true, present only interpolated frames (for debugging)
      constexpr bool kSkipRealFrames = false;
      // if true, skip interpolated frames (debugging)
      constexpr bool kSkipInterpolatedFrames = false;
      // if true, disable the pacer semaphore wait
      constexpr bool kSkipPacerSemaphoreWait = false;
      
      const auto& reflex = m_ctx->getCommonObjects()->metaReflex();

      if (present.frameInterpolation.valid() && !kSkipInterpolatedFrames) {
        ScopedCpuProfileZoneN("DLFG queue: interpolate");

        reflex.beginOutOfBandRendering(present.present.cachedReflexFrameId);
        
        // pre-DLFG barriers
        // xxxnsubtil: missing queue transfers here
        //
        // the VK spec requires a queue ownership transfer barrier when switching an image created with
        // VK_SHARING_MODE_EXCLUSIVE (which is all of them in dxvk) between queues; not doing so is not
        // a spec violation, but it allows the driver to leave the image contents undefined on the target
        // queue
        //
        // we colud do the present side queue transfer here, but it needs a corresponding release from graphics,
        // which we can not do on this thread; doing it in dxvk-cs requires teaching dxvk about queues, which
        // is a wider change, and doing only one half of the queue transfer results in VL errors
        //
        // we can also get around this by using VK_SHARING_MODE_CONCURRENT, but that requires queues be set up
        // in DxvkDevice before any of the implicit singleton objects that hold images are constructed (as
        // we need to specify all the queue families up front when creating the image), which is also a wider change
        //
        // for now, SHARING_MODE_EXCLUSIVE + no queue transfer barriers works fine

        m_cmdList = m_dlfgBarrierCommandLists.nextCmdList();
        {
          ScopedGpuProfileZone_Present(m_device, m_cmdList->getCmdBuffer(), "DLFG pre-eval barriers");

          barriers.addBarrier(m_swapchainImages[swapchainIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_NONE,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              m_swapchainImageLayouts[swapchainIndex],
                              VK_IMAGE_LAYOUT_GENERAL);

          barriers.addBarrier(m_backbufferImages[present.acquiredImageIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_NONE,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

          barriers.addBarrier(present.frameInterpolation.motionVectors->image()->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_NONE,
                              VK_ACCESS_SHADER_READ_BIT,
                              present.frameInterpolation.motionVectorsLayout,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

          barriers.addBarrier(present.frameInterpolation.depth->image()->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_NONE,
                              VK_ACCESS_SHADER_READ_BIT,
                              present.frameInterpolation.depthLayout,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

          barriers.record(m_device, *m_cmdList, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        
        m_cmdList->endRecording();
        m_cmdList->addWaitSemaphore(backbufferWaitSemaphore);
        backbufferWaitSemaphore = nullptr;
        m_cmdList->addWaitSemaphore(swapchainSync.acquire);
        m_cmdList->addSignalSemaphore(m_dlfgBeginSemaphore->handle());
        m_cmdList->submit();  // xxxnsubtil: skip this submit!

        // run DLFG to populate the swapchain image
        VkSemaphore dlfgCompleteSemaphore = dlfg.dispatch(m_ctx,
                                                          present.frameInterpolation.camera,
                                                          m_swapchainImageViews[swapchainIndex],
                                                          m_dlfgBeginSemaphore->handle(),
                                                          m_backbufferViews[present.acquiredImageIndex],
                                                          present.frameInterpolation.motionVectors,
                                                          present.frameInterpolation.depth,
                                                          false);

        // xxxnsubtil: get cmdlist from DLFG dispatch and reuse
        m_cmdList = m_dlfgBarrierCommandLists.nextCmdList();

        {
          ScopedGpuProfileZone_Present(m_device, m_cmdList->getCmdBuffer(), "DLFG post-eval barriers");

          barriers.addBarrier(m_swapchainImages[swapchainIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              VK_ACCESS_MEMORY_READ_BIT,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

          barriers.addBarrier(m_backbufferImages[present.acquiredImageIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

          barriers.addBarrier(present.frameInterpolation.motionVectors->image()->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              present.frameInterpolation.motionVectorsLayout);

          barriers.addBarrier(present.frameInterpolation.depth->image()->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_SHADER_READ_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              present.frameInterpolation.depthLayout);

          barriers.record(m_device, *m_cmdList, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        pacer.dlfgQueryIndex = queryPoolDLFG->writeTimestamp(m_cmdList->getCmdBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        m_cmdList->endRecording();
        m_cmdList->addWaitSemaphore(dlfgCompleteSemaphore);
        m_cmdList->addSignalSemaphore(swapchainSync.present);
        if (kSkipRealFrames) {
          // if we're skipping real frames, then signal the acquire and frame ID semaphores here
          m_cmdList->addSignalSemaphore(m_backbufferAcquireSemaphores[present.acquiredImageIndex]->handle());

          // skip signaling if the semaphore is already at the value we are signaling to, since the VK spec forbids this (yes, really)
          // this happens after init where the value is already 0 on frame 0, and may also happen during wraparound
          // (CS thread detects this and idles instead)
          if (dlfg.frameEndSemaphoreValue() != uint64_t(present.frameInterpolation.frameId)) {
            m_cmdList->addSignalSemaphore(m_dlfgFrameEndSemaphore->handle(), uint64_t(present.frameInterpolation.frameId));
            dlfg.frameEndSemaphoreValue() = uint64_t(present.frameInterpolation.frameId);
          }
        }
        
        // pacer thread will do a CPU wait on this command list before signaling the semaphore below
        pacer.lastCmdListFence = m_cmdList->getSignalFence();
        m_cmdList->submit();

        reflex.endOutOfBandRendering(present.present.cachedReflexFrameId);

        m_swapchainImageLayouts[swapchainIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        //assert(m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle) == VK_SUCCESS);
        
        // present the interpolated frame
        reflex.beginOutOfBandPresent(present.present.cachedReflexFrameId);
        m_lastPresentStatus = vk::Presenter::presentImage(present.status, present.present, present.frameInterpolation, present.acquiredImageIndex);
        reflex.endOutOfBandPresent(present.present.cachedReflexFrameId);

        if (m_lastPresentStatus != VK_SUCCESS) {
          // got an error, bail until it's handled
          continue;
        }

        if (kSkipRealFrames) {
          // if we're skipping real frames, restart the loop
          continue;
        }

        // now acquire again to present the real frame below
        m_lastPresentStatus = vk::Presenter::acquireNextImage(swapchainSync, swapchainIndex);
        if (m_lastPresentStatus != VK_SUCCESS) {
          // got an error, bail until it's handled
          continue;
        }

        assert(swapchainIndex < m_blitCommandLists.size());
        swapchainImage = vk::Presenter::getImage(swapchainIndex);
      }

      if (kSkipRealFrames) {
        continue;
      }

      {
        // set up pre-blit barriers
        m_cmdList = m_blitCommandLists.nextCmdList();

        {
          ScopedGpuProfileZone_Present(m_device, m_cmdList->getCmdBuffer(), "DLFG real frame blit barriers");

          barriers.addBarrier(m_backbufferImages[present.acquiredImageIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

          barriers.addBarrier(swapchainImage.image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_MEMORY_READ_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              m_swapchainImageLayouts[present.acquiredImageIndex],
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

          barriers.record(m_device, *m_cmdList, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

          VkImageCopy copy = {
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            { 0, 0, 0 },
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            { 0, 0, 0 },
            { m_info.imageExtent.width, m_info.imageExtent.height, 1 }
          };
          
          m_device->vkd()->vkCmdCopyImage(m_cmdList->getCmdBuffer(),
                                          m_backbufferImages[present.acquiredImageIndex]->handle(),
                                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                          swapchainImage.image,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          1, &copy);

          barriers.addBarrier(m_backbufferImages[present.acquiredImageIndex]->handle(),
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

          barriers.addBarrier(swapchainImage.image,
                              VK_IMAGE_ASPECT_COLOR_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_MEMORY_READ_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

          m_swapchainImageLayouts[present.acquiredImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

          barriers.record(m_device, *m_cmdList, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }

        m_cmdList->endRecording();

        // blit needs to wait on:
        //   * the present semaphore on the backbuffer we're reading, if we haven't waited on that before (app will signal)
        //   * the VK swapchain acquire semaphore
        // ... and must signal:
        //   * the acquire semaphore on the backbuffer we're reading (app will wait on this on acquire)
        //   * the VK swapchain present semaphore
        //   * the frame end semaphore
        m_cmdList->addWaitSemaphore(backbufferWaitSemaphore); // if we ran interpolation, this will be null (which is a no-op)
        m_cmdList->addWaitSemaphore(swapchainSync.acquire);
        m_cmdList->addSignalSemaphore(m_backbufferAcquireSemaphores[present.acquiredImageIndex]->handle());
        if (!present.frameInterpolation.valid() || kSkipPacerSemaphoreWait) {
          m_cmdList->addSignalSemaphore(swapchainSync.present);
        }

        // skip signaling if the semaphore is already at the value we are signaling to, since the VK spec forbids this (yes, really)
        // this happens after init where the value is already 0 on frame 0, and may also happen during wraparound
        // (CS thread detects this and idles instead)
        if (present.frameInterpolation.valid() &&
            dlfg.frameEndSemaphoreValue() != uint64_t(present.frameInterpolation.frameId)) {
          m_cmdList->addSignalSemaphore(m_dlfgFrameEndSemaphore->handle(), uint64_t(present.frameInterpolation.frameId));
          dlfg.frameEndSemaphoreValue() = uint64_t(present.frameInterpolation.frameId);
        }

        m_cmdList->submit();

        if (present.frameInterpolation.valid() && !kSkipPacerSemaphoreWait) {
          // inject a command list that waits on the pacer semaphore and signals the present semaphore
          // this will cause the present below to wait on this timeline semaphore, which the pacer thread will signal from the CPU
          m_cmdList = m_presentPacingCommandLists.nextCmdList();
          m_cmdList->endRecording();
          m_cmdList->addWaitSemaphore(m_dlfgPacerSemaphore->handle(), m_dlfgPacerSemaphoreValue + 1);
          m_cmdList->addSignalSemaphore(swapchainSync.present);
          m_cmdList->submit();
        }

        // kick off the pacer job for this frame
        // we have to do this before present, since VK overlays may assume
        // it's safe to idle the queue during present, which would otherwise cause
        // the GPU to get stuck waiting on the pacer job
        if (present.frameInterpolation.valid()) {
          assert(pacer.lastCmdListFence != nullptr);
          pacer.semaphoreSignalValue = ++m_dlfgPacerSemaphoreValue;
          {
            std::unique_lock<dxvk::mutex> lock(m_pacerThread.mutex);
            m_pacerQueue.push(pacer);
            m_pacerThread.condWorkAvailable.notify_all();
          }
        }

        // present the image (note: args unused)
#if !__DLFG_REFLEX_WORKAROUND
        reflex.beginPresentation(present.present.cachedReflexFrameId);
#else
        // this marker is a workaround based on feedback from driver/FrameView --- we mark both presents as OOB presents in this thread,
        // and we mark the code that queues presents from the app to the present thread as the plain present
        // see comments around __DLFG_REFLEX_WORKAROUND define in rtx_ngx_wrapper.h for more details
        reflex.beginOutOfBandPresent(present.present.cachedReflexFrameId);
#endif
        m_lastPresentStatus = vk::Presenter::presentImage(present.status, present.present, present.frameInterpolation, present.acquiredImageIndex);
#if !__DLFG_REFLEX_WORKAROUND
        reflex.endPresentation(present.present.cachedReflexFrameId);
#else
        reflex.endOutOfBandPresent(present.present.cachedReflexFrameId);
#endif
      }
    }
  }

  void DxvkDLFGPresenter::runPacerThread() {
    ScopedCpuProfileZone();
    env::setThreadName("dxvk-dlfg-pacer");

    std::unique_lock<dxvk::mutex> lock(m_pacerThread.mutex);

    Rc<DxvkDLFGTimestampQueryPool> queryPoolDLFG = m_device->getCommon()->metaDLFG().getDLFGQueryPool();

    VkPhysicalDeviceLimits limits = m_device->adapter()->deviceProperties().limits;
    const double nsPerGpuIncrement = limits.timestampPeriod;

    const int64_t qpcIncrementsPerSecond = high_resolution_clock::getFrequency();
    const double qpcIncrementsPerNs = double(qpcIncrementsPerSecond) / 1000000000.0;
    const double nsPerQpcIncrement = 1000000000.0 / double(qpcIncrementsPerSecond);

    uint64_t referenceTimestampGpu = 0;
    uint64_t referenceTimestampQpc = 0;
    uint64_t referenceMaxDeviation = 0;

    auto signalPresentSemaphore = [&](uint64_t value) {
      VkSemaphoreSignalInfo info;
      info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
      info.pNext = nullptr;
      info.semaphore = m_dlfgPacerSemaphore->handle();
      info.value = value;
      VkResult res = m_device->vkd()->vkSignalSemaphore(m_device->handle(), &info);
      if (res != VK_SUCCESS) {
        Logger::err("DxvkDLFGPresenter::runPacerThread: vkSignalSemaphore failed");
      }
    };

    auto calibrateTimestamps = [&]() {
      VkCalibratedTimestampInfoEXT info[2] = {
        {
          VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
          nullptr,
          VK_TIME_DOMAIN_DEVICE_EXT,
        }, {
          VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT,
          nullptr,
          VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT,
        }
      };

      uint64_t timestamps[2];
      VkResult res;
      res = m_device->vkd()->vkGetCalibratedTimestampsEXT(m_device->handle(),
                                                          2,
                                                          info,
                                                          timestamps, &referenceMaxDeviation);
      if (res != VK_SUCCESS) {
        throw DxvkError("DxvkDLFGPresenter::runPacerThread(): vkGetCalibratedTimestampsEXT failed");
      }

      referenceTimestampGpu = timestamps[0];
      referenceTimestampQpc = timestamps[1];
    };

    auto qpcTicksToNs = [&](int64_t ticks) -> double {
      return ticks * nsPerQpcIncrement;
    };

    auto nsToQpcTicks = [&](double ns) -> uint64_t {
      return uint64_t(ns * qpcIncrementsPerNs);
    };

    auto gpuTicksToNs = [&](uint64_t ticks) -> double {
      return ticks * nsPerGpuIncrement;
    };

    auto nsToMs = [](double ns) -> double {
      return ns / 1000000.0;
    };

    auto gpuTicksToQpc = [&](uint64_t gpuTicks) -> int64_t {
      double deltaToReferenceNs;
      int64_t deltaToReferenceQpcTicks;
      
      if (gpuTicks > referenceTimestampGpu) {
        deltaToReferenceNs = gpuTicksToNs(gpuTicks - referenceTimestampGpu);
        deltaToReferenceQpcTicks = nsToQpcTicks(deltaToReferenceNs);
        return referenceTimestampQpc + deltaToReferenceQpcTicks;
      } else {
        deltaToReferenceNs = gpuTicksToNs(referenceTimestampGpu - gpuTicks);
        deltaToReferenceQpcTicks = nsToQpcTicks(deltaToReferenceNs);
        return referenceTimestampQpc - deltaToReferenceQpcTicks;
      }
    };

    uint64_t lastFrameDlfgEndGpuTicks = 0;

    while (!m_pacerThread.stopped.load()) {
      {
        ScopedCpuProfileZoneN("DLFG pacer: wait");
        m_pacerThread.condWorkAvailable.wait(lock, [this] { return m_pacerThread.stopped.load() || !m_pacerQueue.empty(); });
      }

      if (m_pacerThread.stopped.load()) {
        break;
      }

      PacerJob pacer = std::move(m_pacerQueue.front());
      lock.unlock();

      uint64_t dlfgTimestamp = 0;
      bool pacerActive = true;

      // wait on the GPU and read back timestamp query
      {
        ScopedCpuProfileZoneN("DLFG pacer: query readback");

        // instead of using the WAIT bit for GetQueryPoolResults, wait on a fence that resolves after the query results
        // this ensures we can timeout if the queries don't resolve, instead of hanging forever
        VkResult res = m_device->vkd()->vkWaitForFences(m_device->handle(), 1, &pacer.lastCmdListFence, VK_TRUE, 1000000000);

        if (res != VK_SUCCESS) {
          Logger::warn("DLFG pacer: fence timed out");
          lastFrameDlfgEndGpuTicks = 0;
          pacerActive = false;
        } else {
          pacerActive = queryPoolDLFG->readTimestamp(&dlfgTimestamp, pacer.dlfgQueryIndex);

          if (lastFrameDlfgEndGpuTicks == 0) {
            lastFrameDlfgEndGpuTicks = dlfgTimestamp;
            // no data for previous frame available, do not pace this frame
            pacerActive = false;
          }
        }
      }

      if (pacerActive) {
        {
          ScopedCpuProfileZoneN("DLFG pacer: timestamp calibration");
          calibrateTimestamps();
        }

        const double frameToFrame = nsToMs(gpuTicksToNs(dlfgTimestamp - lastFrameDlfgEndGpuTicks));
        ProfilerPlotValue("DLFG pacer: frame-to-frame time (ms)", frameToFrame);

        // this determines the maximum amount of time we're willing to sleep, as a backstop in case something goes wrong
        constexpr double kMinOutputFPS = 20;
        constexpr double kMaxFrameTimeMs = 2000.0 / kMinOutputFPS;

        // skip the pacer logic if the timestamps don't make sense
        if (frameToFrame > 0.0 && frameToFrame < kMaxFrameTimeMs) {
          // time the present to land at the halfway point between the two DLFG interpolated frames
          const uint64_t frameTimeGpuTicks = dlfgTimestamp - lastFrameDlfgEndGpuTicks;
          uint64_t deltaGpuPresentTicks = frameTimeGpuTicks / 2;

          // convert the GPU timestamp to a CPU timestamp
          const int64_t targetQpcPresentTicks = gpuTicksToQpc(dlfgTimestamp + deltaGpuPresentTicks);

          const double deltaPresentNs = gpuTicksToNs(deltaGpuPresentTicks);
          const double deltaPresentQpcNs = qpcTicksToNs(targetQpcPresentTicks - high_resolution_clock::getCounter());
          ProfilerPlotValue("DLFG pacer: measured GPU sleep time (ms)", nsToMs(deltaPresentNs));
          ProfilerPlotValue("DLFG pacer: remaining CPU sleep time (ms)", nsToMs(deltaPresentQpcNs));

          // ignore sleeps longer than kMaxFrameTimeMs in case something goes wrong with the math above
          if (deltaPresentQpcNs < kMaxFrameTimeMs * 1e9) {
            ScopedCpuProfileZoneN("DLFG pacer: sleep");
            while (high_resolution_clock::getCounter() < targetQpcPresentTicks) {
              _mm_pause();
            }
          } else {
            pacerActive = false;
          }

          lastFrameDlfgEndGpuTicks = dlfgTimestamp;
        } else {
          // timings don't make sense, reset history
          lastFrameDlfgEndGpuTicks = 0;
          pacerActive = false;
        }
      }

      ProfilerPlotValueI64("DLFG pacer: active", pacerActive ? 1 : 0);

      // signal the present semaphore
      {
        ScopedCpuProfileZoneN("DLFG pacer: signal semaphore");
        signalPresentSemaphore(pacer.semaphoreSignalValue);
      }

      lock.lock();
      m_pacerQueue.pop();
      m_pacerThread.condWorkConsumed.notify_all();
    }

    // release all pending frames in the queue before leaving
    assert(lock.owns_lock());

    while (m_pacerQueue.size()) {
      PacerJob pacer = std::move(m_pacerQueue.front());
      signalPresentSemaphore(pacer.semaphoreSignalValue);

      m_pacerQueue.pop();
      m_pacerThread.condWorkConsumed.notify_all();
    }
  }

  DxvkDLFGCommandList::DxvkDLFGCommandList(DxvkDevice* device)
    : m_device(device) {
    VkCommandPoolCreateInfo poolInfo;
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_device->queues().__DLFG_QUEUE.queueFamily;

    if (m_device->vkd()->vkCreateCommandPool(m_device->handle(), &poolInfo, nullptr, &m_cmdPool) != VK_SUCCESS) {
      throw DxvkError("DxvkDLFGCommandList: failed to create command pool");
    }

    VkCommandBufferAllocateInfo cmdInfo;
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.pNext = nullptr;
    cmdInfo.commandPool = m_cmdPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    
    if (m_device->vkd()->vkAllocateCommandBuffers(m_device->handle(), &cmdInfo, &m_cmdBuf) != VK_SUCCESS) {
      throw DxvkError("DxvkDLFGCommandList: failed to create command list");
    }
  }

  DxvkDLFGCommandList::~DxvkDLFGCommandList() {
    m_resources.reset();
    if (m_cmdPool) {
      m_device->vkd()->vkDestroyCommandPool(m_device->handle(), m_cmdPool, nullptr);
      m_cmdPool = nullptr;
      m_cmdBuf = nullptr;
    }
  }

  void DxvkDLFGCommandList::beginRecording() {
    assert(m_cmdPool);
    assert(m_cmdBuf);
    
    VkCommandBufferBeginInfo info;
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    info.pInheritanceInfo = nullptr;

    if (m_device->vkd()->vkBeginCommandBuffer(m_cmdBuf, &info) != VK_SUCCESS) {
      Logger::err("DxvkDLFGCommandList::beginRecording: vkBeginCommandBuffer failed");
    }
  }

  void DxvkDLFGCommandList::endRecording() {
    TracyVkCollect(m_device->queues().__DLFG_QUEUE.tracyCtx, m_cmdBuf);

    if (m_device->vkd()->vkEndCommandBuffer(m_cmdBuf) != VK_SUCCESS) {
      Logger::err("DxvkDLFGCommandList::endRecording: vkEndCommandBuffer failed");
    }
  }

  void DxvkDLFGCommandList::addWaitSemaphore(VkSemaphore sem, uint64_t value) {
    assert(m_numWaitSemaphores < kMaxSemaphores);

    if (sem) {
      m_waitSemaphores[m_numWaitSemaphores] = sem;
      m_waitSemaphoreValues[m_numWaitSemaphores] = value;
      ++m_numWaitSemaphores;
    }
  }

  void DxvkDLFGCommandList::addSignalSemaphore(VkSemaphore sem, uint64_t value) {
    assert(m_numSignalSemaphores < kMaxSemaphores);

    if (sem) {
      m_signalSemaphores[m_numSignalSemaphores] = sem;
      m_signalSemaphoreValues[m_numSignalSemaphores] = value;
      ++m_numSignalSemaphores;
    }
  }

  void DxvkDLFGCommandList::setSignalFence(VkFence fence) {
    assert(!m_signalFence);
    assert(fence);
    m_signalFence = fence;
  }

  DxvkDLFGCommandListArray::DxvkDLFGCommandListArray(DxvkDevice* device, uint32_t numCmdLists)
    : m_device(device) {
    m_commandLists.resize(dxvk::kMaxFramesInFlight * numCmdLists);
    m_fences.resize(dxvk::kMaxFramesInFlight * numCmdLists);
  }

  DxvkDLFGCommandList *DxvkDLFGCommandListArray::nextCmdList() {
    ScopedCpuProfileZone();

    // note: we can't create this in the ctor as our parent object is constructed before the VK device is created
    if (m_commandLists[m_currentCommandListIndex] == nullptr) {
      m_commandLists[m_currentCommandListIndex] = new DxvkDLFGCommandList(m_device);
      m_fences[m_currentCommandListIndex] = new RtxFence(m_device);
    }

    DxvkDLFGCommandList *ret = m_commandLists[m_currentCommandListIndex].ptr();
    VkFence fence = m_fences[m_currentCommandListIndex]->handle();
    assert(fence);
    VkResult res = m_device->vkd()->vkWaitForFences(m_device->handle(), 1, &fence, VK_TRUE, 1'000'000'000ull);
    if (res != VK_SUCCESS) {
      throw DxvkError("DxvkDLFGCommandListArray::nextCmdList: vkWaitForFences failed");
    }

    res = m_device->vkd()->vkResetFences(m_device->handle(), 1, &fence);
    assert(res == VK_SUCCESS);
    
    ret->reset();
    ret->setSignalFence(m_fences[m_currentCommandListIndex]->handle());
    ret->beginRecording();
    
    m_currentCommandListIndex = (m_currentCommandListIndex + 1) % m_commandLists.size();

    return ret;
  }

  DxvkDLFGTimestampQueryPool::DxvkDLFGTimestampQueryPool(DxvkDevice* device, const uint32_t numQueries)
    : m_device(device)
    , m_queryPoolSize(numQueries) {

    VkQueryPoolCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = numQueries;
    info.pipelineStatistics = 0;

    VkResult res;
    res = m_device->vkd()->vkCreateQueryPool(m_device->handle(), &info, nullptr, &m_queryPool);
    if (res != VK_SUCCESS) {
      throw DxvkError("DxvkDLFGTimestampQueryPool: vkCreateQueryPool failed");
    }

    m_device->vkd()->vkResetQueryPool(m_device->handle(), m_queryPool, 0, numQueries);
  }

  DxvkDLFGTimestampQueryPool::~DxvkDLFGTimestampQueryPool() {
    if (m_queryPool) {
      m_device->vkd()->vkDestroyQueryPool(m_device->handle(), m_queryPool, nullptr);
      m_queryPool = nullptr;
    }
  }

  uint32_t DxvkDLFGTimestampQueryPool::writeTimestamp(VkCommandBuffer cmdList, VkPipelineStageFlagBits stage) {
    assert(m_queryPool);

    uint32_t idx = m_nextQueryIndex;
    m_device->vkd()->vkCmdResetQueryPool(cmdList, m_queryPool, idx, 1);
    m_device->vkd()->vkCmdWriteTimestamp(cmdList, stage, m_queryPool, idx);

    m_nextQueryIndex = (m_nextQueryIndex + 1) % m_queryPoolSize;
    return idx;
  }

  bool DxvkDLFGTimestampQueryPool::readTimestamp(uint64_t* queryResult, uint32_t queryIndex) {
    VkResult res;

    res = m_device->vkd()->vkGetQueryPoolResults(m_device->handle(),
                                                 m_queryPool,
                                                 queryIndex,
                                                 1,
                                                 sizeof(uint64_t),
                                                 queryResult,
                                                 sizeof(uint64_t),
                                                 VK_QUERY_RESULT_64_BIT);

    if (res != VK_SUCCESS) {
      return false;
    }

    return true;
  }

  void createTimelineSyncObjectsCallback(void* appContext,
                                         void** syncObjSignal,        // N1
                                         uint64_t syncObjSignalValue,
                                         void** syncObjWait,          // N4
                                         uint64_t syncObjWaitValue) {
    DxvkDLFG* dlfg = (DxvkDLFG*) appContext;
    dlfg->createTimelineSyncObjects(*reinterpret_cast<VkSemaphore*>(syncObjSignal), syncObjSignalValue,
                                    *reinterpret_cast<VkSemaphore*>(syncObjWait), syncObjWaitValue);
  }

  void syncSignalCallback(void* appContext,
                          void** cmdList,
                          void* syncObjSignal, // N1
                          uint64_t syncObjSignalValue) {
    DxvkDLFG* dlfg = (DxvkDLFG*) appContext;
    dlfg->syncSignal(*reinterpret_cast<VkCommandBuffer*>(cmdList),
                     reinterpret_cast<VkSemaphore>(syncObjSignal), syncObjSignalValue);
  }

  void syncWaitCallback(void* appContext,
                        void** cmdList,
                        void* syncObjWait,  // N4
                        uint64_t syncObjWaitValue,
                        int waitCpu,
                        void* __unused_syncObjSignal, // always null
                        uint64_t syncObjSignalValue) {
    DxvkDLFG* dlfg = (DxvkDLFG*) appContext;
    dlfg->syncWait(*reinterpret_cast<VkCommandBuffer*>(cmdList), 
                   reinterpret_cast<VkSemaphore>(syncObjWait), syncObjWaitValue, waitCpu, __unused_syncObjSignal, syncObjSignalValue);
  }

  void syncFlushCallback(void* appContext,
                         void** cmdList,
                         void* syncObjSignal,
                         uint64_t syncObjSignalValue,
                         int waitCpu) {
    DxvkDLFG* dlfg = (DxvkDLFG*) appContext;
    dlfg->syncFlush(*reinterpret_cast<VkCommandBuffer*>(cmdList),
                    reinterpret_cast<VkSemaphore>(syncObjSignal), syncObjSignalValue, waitCpu);
  }

  DxvkDLFG::DxvkDLFG(DxvkDevice* device)
    : CommonDeviceObject(device)
    // xxxnsubtil: use swapchain frame count here
    , m_dlfgEvalCommandLists(device, 1)
    , m_dlfgInternalAsyncOFACommandLists(device, 1)
    , m_dlfgInternalPostOFACommandLists(device, 1)
    , m_dlfgDummyWaitOnSem2CommandLists(device, 2)
    , m_dlfgDummyWaitOnSem4CommandLists(device, 2)
    , m_dlfgFrameEndSemaphore(RtxSemaphore::createTimeline(device, "DLFG frame end"))
    , m_dlfgFinishedSemaphore(RtxSemaphore::createBinary(device, "DLFG finished")) {

    m_queryPoolDLFG = new DxvkDLFGTimestampQueryPool(m_device, kMaxFramesInFlight);
    m_dlfgSemaphores.s2 = RtxSemaphore::createBinary(device, "DLFG semaphore 2");
    m_dlfgSemaphores.s3 = RtxSemaphore::createBinary(device, "DLFG semaphore 3");
  }

  void DxvkDLFG::onDestroy() {
    m_queryPoolDLFG = nullptr;
  }

  bool DxvkDLFG::supportsDLFG() {
    return m_device->getCommon()->metaNGXContext().supportsDLFG();
  }

  const std::string& DxvkDLFG::getDLFGNotSupportedReason() {
    return m_device->getCommon()->metaNGXContext().getDLFGNotSupportedReason();
  }

  void DxvkDLFG::setDisplaySize(uint2 displaySize) {
    if (m_currentDisplaySize[0] != displaySize.x ||
        m_currentDisplaySize[1] != displaySize.y) {
      m_currentDisplaySize[0] = displaySize.x;
      m_currentDisplaySize[1] = displaySize.y;
      m_contextDirty = true;
    }
  }

  VkSemaphore DxvkDLFG::dispatch(Rc<DxvkContext> ctx,
                                 const RtCamera& camera,
                                 Rc<DxvkImageView> outputImage,                       // VK_IMAGE_LAYOUT_GENERAL
                                 VkSemaphore outputImageSemaphore,
                                 Rc<DxvkImageView> colorBuffer,                       // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 Rc<DxvkImageView> primaryScreenSpaceMotionVector,    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 Rc<DxvkImageView> primaryDepth,                      // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 bool resetHistory) {
    ScopedCpuProfileZone();

    if (!m_dlfgContext) {
      m_dlfgContext = m_device->getCommon()->metaNGXContext().createDLFGContext();
      m_contextDirty = true;
    }
    
    if (m_contextDirty) {
      if (m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle) != VK_SUCCESS) {
        Logger::err("DxvkDLFG::dispatch: vkQueueWaitIdle failed");
      }
    }

    m_outputImageSemaphore = outputImageSemaphore;
    m_currentCommandList = m_dlfgEvalCommandLists.nextCmdList();

    if (m_contextDirty) {
      assert(m_dlfgContext != nullptr);

      m_dlfgContext->releaseNGXFeature();
      m_dlfgContext->initialize(ctx,
                                m_currentCommandList->getCmdBuffer(),
                                m_currentDisplaySize,
                                outputImage->info().format,
                                createTimelineSyncObjectsCallback,
                                syncSignalCallback,
                                syncWaitCallback,
                                syncFlushCallback,
                                this);
      m_contextDirty = false;
    }

    m_dlfgSemaphores.s2SignaledThisFrame = false;
    NGXDLFGContext::EvaluateResult res;

    m_currentCommandList->trackResource<DxvkAccess::Write>(outputImage);
    m_currentCommandList->trackResource<DxvkAccess::Read>(colorBuffer);
    m_currentCommandList->trackResource<DxvkAccess::Read>(primaryScreenSpaceMotionVector);
    m_currentCommandList->trackResource<DxvkAccess::Read>(primaryDepth);

    // Conditionally wait for the command queue to be cleared.
    // In some cases DLFG needs to wait for the command queue to be cleared before it proceeds.
    // For example, the resolution is changed.
    do {
      // xxxnsubtil: can't do this because DLFG will submit cmdlists behind our back
      //ScopedGpuProfileZone_Present(m_device, *m_currentCommandList, "DLFG evaluate");

      assert(m_dlfgContext != nullptr);

      res = m_dlfgContext->evaluate(Rc<DxvkContext>(ctx.ptr()),
                                                    m_currentCommandList->getCmdBuffer(),
                                                    outputImage,
                                                    colorBuffer,
                                                    primaryScreenSpaceMotionVector,
                                                    primaryDepth,
                                                    camera,
                                                    Vector2(1.0f, 1.0f),
                                                    resetHistory);

      switch (res) {
      case NGXDLFGContext::EvaluateResult::Failure:
        Logger::err("NGX DLFG evaluate failed");
        m_hasDLFGFailed = true;
        break;

      case NGXDLFGContext::EvaluateResult::Success:
        break;

      case NGXDLFGContext::EvaluateResult::NeedWaitIdle:
        // DLFG wants WFI, go idle and prepare to call Evaluate again
        {
          //Logger::warn("DLFG wants to idle");
          m_currentCommandList->endRecording();
          m_currentCommandList->addWaitSemaphore(m_outputImageSemaphore);
          m_currentCommandList->submit();
          m_outputImageSemaphore = nullptr;

          VkResult vkres;
          vkres = m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle);
          assert(vkres == VK_SUCCESS);

          // we need to grab a new cmdList to make sure we have a fence associated with it (the submit above resets the fence)
          m_currentCommandList = m_dlfgEvalCommandLists.nextCmdList();
        }

        break;
      }
    } while (res == NGXDLFGContext::EvaluateResult::NeedWaitIdle);

    // this is now CL3 (post-processing after OFA), must wait on N2

    m_currentCommandList->endRecording();
    if (m_dlfgSemaphores.s2SignaledThisFrame)
      m_currentCommandList->addWaitSemaphore(m_dlfgSemaphores.s2->handle());
    m_currentCommandList->addWaitSemaphore(m_outputImageSemaphore); // may be null if OFA is off
    m_currentCommandList->addSignalSemaphore(m_dlfgFinishedSemaphore->handle());
    m_currentCommandList->submit();

    m_currentCommandList = nullptr;
    m_outputImageSemaphore = nullptr;

    return m_dlfgFinishedSemaphore->handle();
  }

  void DxvkDLFG::createTimelineSyncObjects(VkSemaphore& syncObjSignal,        // N1
                                           uint64_t syncObjSignalValue,
                                           VkSemaphore& syncObjWait,          // N4
                                           uint64_t syncObjWaitValue) {
    ScopedCpuProfileZone();

    m_dlfgSemaphores.s1 = RtxSemaphore::createTimeline(m_device, "DLFG semaphore 1", syncObjSignalValue, true);
    m_dlfgSemaphores.s1Value = syncObjSignalValue;
    
    m_dlfgSemaphores.s4 = RtxSemaphore::createTimeline(m_device, "DLFG semaphore 4", syncObjWaitValue, true);
    m_dlfgSemaphores.s4Value = syncObjWaitValue;
    
    syncObjSignal = m_dlfgSemaphores.s1->handle();
    syncObjWait = m_dlfgSemaphores.s4->handle();
  }

  void DxvkDLFG::syncSignal(VkCommandBuffer& cmdList,
                            VkSemaphore syncObjSignal, // N1
                            uint64_t syncObjSignalValue) {
    ScopedCpuProfileZone();

    VkSemaphore syncSem = syncObjSignal;
    assert(syncSem == m_dlfgSemaphores.s1->handle());
    assert(cmdList == m_currentCommandList->getCmdBuffer());
    m_currentCommandList->endRecording();
    // wait on N0, submit CL1, signal N1
    m_currentCommandList->addWaitSemaphore(m_outputImageSemaphore);
    m_currentCommandList->addSignalSemaphore(m_dlfgSemaphores.s1->handle(), syncObjSignalValue);
    m_currentCommandList->submit();

    m_outputImageSemaphore = nullptr;
    m_dlfgSemaphores.s1Value = syncObjSignalValue;

    m_currentCommandList = m_dlfgInternalAsyncOFACommandLists.nextCmdList();
    cmdList = m_currentCommandList->getCmdBuffer();
  }

  void DxvkDLFG::syncWait(VkCommandBuffer& cmdList,
                          VkSemaphore syncObjWait,  // N4
                          uint64_t syncObjWaitValue,
                          int waitCpu,
                          void* __unused_syncObjSignal, // always null
                          uint64_t /* syncObjSignalValue */) {
    ScopedCpuProfileZone();

    assert(waitCpu == 0);
    assert(__unused_syncObjSignal == nullptr);
    assert(syncObjWait);
    assert(syncObjWait == m_dlfgSemaphores.s4->handle());
    m_dlfgSemaphores.s4Value = syncObjWaitValue;

    // wait on N1, execute CL2, signal N2
    assert(cmdList == m_currentCommandList->getCmdBuffer());
    m_currentCommandList->endRecording();

    if (m_dlfgSemaphores.s2SignaledThisFrame) {
      // xxxnsubtil: refactor DXVK submit interface to allow merging this into the next submit
      DxvkDLFGCommandList* dummyCmdList = m_dlfgDummyWaitOnSem2CommandLists.nextCmdList();
      dummyCmdList->endRecording();
      dummyCmdList->addWaitSemaphore(m_dlfgSemaphores.s2->handle());
      dummyCmdList->submit();
    }

    m_currentCommandList->addWaitSemaphore(m_dlfgSemaphores.s1->handle(), m_dlfgSemaphores.s1Value);
    m_currentCommandList->addSignalSemaphore(m_dlfgSemaphores.s2->handle());
    m_currentCommandList->submit();
    m_dlfgSemaphores.s2SignaledThisFrame = true;

    // wait on N4
    {
      // xxxnsubtil: this can now be merged with the previous submit!
      DxvkDLFGCommandList* dummyCmdList = m_dlfgDummyWaitOnSem4CommandLists.nextCmdList();
      dummyCmdList->endRecording();

      dummyCmdList->addWaitSemaphore(m_dlfgSemaphores.s4->handle(), syncObjWaitValue);
      dummyCmdList->submit();
    }

    // return CL3
    m_currentCommandList = m_dlfgInternalPostOFACommandLists.nextCmdList();
    cmdList = m_currentCommandList->getCmdBuffer();
  }


  void DxvkDLFG::syncFlush(VkCommandBuffer& cmdList,
                           VkSemaphore syncObjSignal,
                           uint64_t syncObjSignalValue,
                           int waitCpu) {
    VkResult res = m_device->vkd()->vkQueueWaitIdle(m_device->queues().__DLFG_QUEUE.queueHandle);
    assert(res == VK_SUCCESS);
  }
} // namespace dxvk
