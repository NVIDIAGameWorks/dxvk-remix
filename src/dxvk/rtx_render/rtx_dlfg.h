#pragma once

#include <cstdint>

#include "../dxvk_include.h"
#include "rtx_resources.h"
#include "../../vulkan/vulkan_presenter.h"
#include "../dxvk_queue.h"

namespace dxvk {
  class NGXDLFGContext;
  class DxvkBarrierSet;
  class RtxContext;
  class DxvkDevice;
  class RtxSemaphore;
  class RtxFence;

  class DxvkDLFGPresenter;

  struct DxvkDeviceQueue;

  namespace vk {
    struct PresenterDevice;
    struct PresenterDesc;
  }

  // utility class to run a lambda on scope exit
  class DxvkDLFGScopeGuard {
  public:
    DxvkDLFGScopeGuard(std::function<void()> f)
      : m_function(f) { }

    DxvkDLFGScopeGuard(const DxvkDLFGScopeGuard&) = delete;

    ~DxvkDLFGScopeGuard() {
      m_function();
    }

  private:
    std::function<void()> m_function;
  };

  class DxvkDLFGCommandList : public RcObject {
  public:
    DxvkDLFGCommandList(DxvkDevice* device);
    ~DxvkDLFGCommandList();

    void beginRecording();
    void endRecording();

    VkCommandBuffer getCmdBuffer() const {
      return m_cmdBuf;
    }

    void addWaitSemaphore(VkSemaphore sem, uint64_t value = uint64_t(-1));
    void addSignalSemaphore(VkSemaphore sem, uint64_t value = uint64_t(-1));
    void setSignalFence(VkFence fence);
    VkFence getSignalFence() {
      assert(m_signalFence != nullptr);
      return m_signalFence;
    }
    
    // note: always submits to present queue
    void submit();

    template<DxvkAccess Access>
    void trackResource(Rc<DxvkResource> rc) {
      m_resources.trackResource<Access>(std::move(rc));
    }

    void reset() { m_resources.reset(); }
  private:
    DxvkDevice* m_device = nullptr;
    VkCommandPool m_cmdPool = nullptr;
    VkCommandBuffer m_cmdBuf = nullptr;

    static constexpr uint32_t kMaxSemaphores = 4;
    uint32_t m_numWaitSemaphores = 0;
    std::array<VkSemaphore, kMaxSemaphores> m_waitSemaphores;
    std::array<uint64_t, kMaxSemaphores> m_waitSemaphoreValues;
    uint32_t m_numSignalSemaphores = 0;
    std::array<VkSemaphore, kMaxSemaphores> m_signalSemaphores;
    std::array<uint64_t, kMaxSemaphores> m_signalSemaphoreValues;
    VkFence m_signalFence = nullptr;
    DxvkLifetimeTracker m_resources;
  };

  class DxvkDLFGCommandListArray {
  public:
    DxvkDLFGCommandListArray(DxvkDevice* device, uint32_t numCmdLists);
    DxvkDLFGCommandList* nextCmdList();

    void resizeCommandList(uint32_t numCmdLists) {
      m_commandLists.resize(numCmdLists);
      m_fences.resize(numCmdLists);
    }

    size_t size() const {
      return m_commandLists.size();
    }

  private:
    DxvkDevice* m_device;
    std::vector<Rc<DxvkDLFGCommandList>> m_commandLists;
    std::vector<Rc<RtxFence>> m_fences;
    uint32_t m_currentCommandListIndex = 0;
  };

  class DxvkDLFGPresenter : public vk::Presenter {
  public:
    DxvkDLFGPresenter(Rc<DxvkDevice> device, 
                      Rc<DxvkContext> ctx,
                      HWND window,
                      const Rc<vk::InstanceFn>& vki,
                      const Rc<vk::DeviceFn>& vkd,
                      vk::PresenterDevice presenterDevice,
                      const vk::PresenterDesc& desc);

    ~DxvkDLFGPresenter();

    vk::PresenterImage getImage(uint32_t index) const override;

    VkResult acquireNextImage(vk::PresenterSync& sync, uint32_t& index) override;
    VkResult presentImage(std::atomic<VkResult>* status, const DxvkPresentInfo& presentInfo, const DxvkFrameInterpolationInfo& frameInterpolationInfo) override;
    VkResult recreateSwapChain(const vk::PresenterDesc& desc) override;

    // waits for all queued frames to be consumed
    void synchronize() override;

    static int getPresentFrameCount() {
      return 2;
    }
  private:
    DxvkDevice* m_device;
    Rc<DxvkContext> m_ctx;

    // xxxnsubtil: try multiple swapchain acquires (2x acquire, 2x present) instead of using extra memory + blits
    std::vector<Rc<DxvkImage>> m_backbufferImages;
    std::vector<Rc<DxvkImageView>> m_backbufferViews;
    std::vector<Rc<RtxSemaphore>> m_backbufferAcquireSemaphores;
    std::vector<Rc<RtxSemaphore>> m_backbufferPresentSemaphores;
    std::vector<bool> m_backbufferInFlight;
    uint32_t m_backbufferIndex;

    // owned by DLFG context, signaled to frameId when DLFG work is complete each frame
    Rc<RtxSemaphore> m_dlfgFrameEndSemaphore;

    std::vector<Rc<DxvkImage>> m_swapchainImages;
    std::vector<Rc<DxvkImageView>> m_swapchainImageViews;
    std::vector<VkImageLayout> m_swapchainImageLayouts;

    struct WorkerThread {
      dxvk::thread threadHandle;
      dxvk::mutex mutex;
      std::atomic<bool> stopped = { false };
      dxvk::condition_variable condWorkConsumed;
      dxvk::condition_variable condWorkAvailable;
    };
    
    struct PresentJob {
      std::atomic<VkResult>* status;
      uint32_t m_backbufferIndex;
      DxvkPresentInfo present;
      DxvkFrameInterpolationInfo frameInterpolation;
    };

    WorkerThread m_presentThread;
    std::queue<PresentJob> m_presentQueue;

    struct PacerJob {
      uint32_t dlfgQueryIndex;
      VkFence lastCmdListFence;
      uint64_t semaphoreSignalValue;
    };
    
    WorkerThread m_pacerThread;
    std::queue<PacerJob> m_pacerQueue;

    std::atomic<VkResult> m_lastPresentStatus = VK_SUCCESS;

    DxvkDLFGCommandListArray m_dlfgBarrierCommandLists;
    DxvkDLFGCommandListArray m_blitCommandLists;
    DxvkDLFGCommandListArray m_presentPacingCommandLists;
    Rc<RtxSemaphore> m_syncSemaphore;
    Rc<RtxSemaphore> m_dlfgBeginSemaphore;

    void synchronize(std::unique_lock<dxvk::mutex>& lock);
    void runPresentThread();
    void runPacerThread();
    void createBackbuffers();
    
    Rc<RtxSemaphore> m_dlfgPacerSemaphore = nullptr;
    uint64_t m_dlfgPacerSemaphoreValue = 0;
    Rc<RtxSemaphore> m_dlfgPacerToPresentSemaphore = nullptr;
  };

  class DxvkDLFGTimestampQueryPool : public RcObject {
  public:
    DxvkDLFGTimestampQueryPool(DxvkDevice* device, const uint32_t numQueries);
    ~DxvkDLFGTimestampQueryPool();

    // returns the query slot index used
    uint32_t writeTimestamp(VkCommandBuffer cmdList, VkPipelineStageFlagBits stage);
    bool readTimestamp(uint64_t* queryResult, uint32_t queryIndex);

    VkQueryPool handle() {
      return m_queryPool;
    }
    
  private:
    Rc<DxvkDevice> m_device = nullptr;
    VkQueryPool m_queryPool = nullptr;
    const uint32_t m_queryPoolSize = 0;
    uint32_t m_nextQueryIndex = 0;
  };

  class DxvkDLFG : public CommonDeviceObject {
  public:
    explicit DxvkDLFG(DxvkDevice* device);
    virtual void onDestroy();

    bool supportsDLFG();
    const std::string& getDLFGNotSupportedReason();
    
    void setDisplaySize(uint2 displaySize);

    VkSemaphore dispatch(Rc<DxvkContext> ctx,
                         const RtCamera& camera,
                         Rc<DxvkImageView> outputImage,                       // VK_IMAGE_LAYOUT_GENERAL
                         VkSemaphore outputImageSemaphore,                    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         Rc<DxvkImageView> colorBuffer,
                         Rc<DxvkImageView> primaryScreenSpaceMotionVector,    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         Rc<DxvkImageView> primaryDepth,                      // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         bool resetHistory = false);

    Rc<RtxSemaphore>& getFrameEndSemaphore() {
      return m_dlfgFrameEndSemaphore;
    }

    uint64_t& frameEndSemaphoreValue() {
      return m_dlfgFrameEndSemaphoreValue;
    }

    // DLFG callbacks
    void createTimelineSyncObjects(VkSemaphore& syncObjSignal,
                                   uint64_t syncObjSignalValue,
                                   VkSemaphore& syncObjWait,
                                   uint64_t syncObjWaitValue);

    void syncSignal(VkCommandBuffer& cmdList,
                    VkSemaphore syncObjSignal,
                    uint64_t syncObjSignalValue);

    void syncWait(VkCommandBuffer& cmdList,
                  VkSemaphore syncObjWait,
                  uint64_t syncObjWaitValue,
                  int waitCpu,
                  void* syncObjSignal,
                  uint64_t syncObjSignalValue);

    void syncFlush(VkCommandBuffer& cmdList,
                   VkSemaphore syncObjSignal,
                   uint64_t syncObjSignalValue,
                   int waitCpu);

    Rc<DxvkDLFGTimestampQueryPool>& getDLFGQueryPool() {
      return m_queryPoolDLFG;
    }
    
    RW_RTX_OPTION("rtx.dlfg", bool, enable, true, "Enables DLSS 3.0 frame generation which generates interpolated frames to increase framerate at the cost of slightly more latency."); // note: always use DxvkDevice::isDLFGEnabled() to check if DLFG is enabled, not this option directly
  
  private:
    std::unique_ptr<NGXDLFGContext> m_dlfgContext = nullptr;
    uint32_t m_currentDisplaySize[2] = { 0,0 };
    bool m_contextDirty = true;

    // binary semaphore, signaled after all DLFG and related synchronization work is done
    Rc<RtxSemaphore> m_dlfgFinishedSemaphore;
    
    DxvkDLFGCommandListArray m_dlfgEvalCommandLists;              // CL1
    DxvkDLFGCommandListArray m_dlfgInternalAsyncOFACommandLists;  // CL2
    DxvkDLFGCommandListArray m_dlfgInternalPostOFACommandLists;   // CL3
    DxvkDLFGCommandListArray m_dlfgDummyWaitOnSem2CommandLists;
    DxvkDLFGCommandListArray m_dlfgDummyWaitOnSem4CommandLists;
    
    struct DLFGInternalSemaphores {
      Rc<RtxSemaphore> s1 = nullptr;    // timeline
      uint64_t s1Value = uint64_t(-1);  // last signaled value
      Rc<RtxSemaphore> s2 = nullptr;    // binary
      bool s2SignaledThisFrame = false; // this may or may not happen, so we may or may not need to wait on this...
      Rc<RtxSemaphore> s3 = nullptr;    // binary
      Rc<RtxSemaphore> s4 = nullptr;    // timeline
      uint64_t s4Value = uint64_t(-1);  // last signaled value
    } m_dlfgSemaphores;
    
    DxvkDLFGCommandList* m_currentCommandList = nullptr;

    VkSemaphore m_outputImageSemaphore = nullptr;

    Rc<RtxSemaphore> m_dlfgFrameEndSemaphore = nullptr;
    uint64_t m_dlfgFrameEndSemaphoreValue = 0;

    // timestamp query pool for the DLFG pacer
    Rc<DxvkDLFGTimestampQueryPool> m_queryPoolDLFG;
  };
} // namespace dxvk
