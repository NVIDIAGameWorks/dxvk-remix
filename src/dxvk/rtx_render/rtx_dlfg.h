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

    void reset();
  private:
    DxvkDevice* m_device = nullptr;
    VkCommandPool m_cmdPool = nullptr;
    VkCommandBuffer m_cmdBuf = nullptr;

    // kMaxSemaphores ultimately depends on how many submits we do in the DLFG thread each frame
    // having too many semaphores isn't really much of a hit, but not having enough causes undefined behavior/crashes
    // addSignalSemaphore/addWaitSemaphore will assert if this value is too small
    static constexpr uint32_t kMaxSemaphores = 7;

    uint32_t m_numWaitSemaphores = 0;
    std::array<VkSemaphore, kMaxSemaphores> m_waitSemaphores;
    std::array<uint64_t, kMaxSemaphores> m_waitSemaphoreValues;
    uint32_t m_numSignalSemaphores = 0;
    std::array<VkSemaphore, kMaxSemaphores> m_signalSemaphores;
    std::array<uint64_t, kMaxSemaphores> m_signalSemaphoreValues;
    VkFence m_signalFence = nullptr;
    DxvkLifetimeTracker m_resources;
  };

  class DxvkDLFG;

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

    VkResult acquireNextImage(vk::PresenterSync& sync, uint32_t& index, bool) override;
    VkResult presentImage(std::atomic<VkResult>* status,
                          const DxvkPresentInfo& presentInfo,
                          const DxvkFrameInterpolationInfo& frameInterpolationInfo,
                          std::uint32_t acquiredImageIndex,
                          bool isDlfgPresenting,
                          VkSetPresentConfigNV* presentMetering) override;
    VkResult recreateSwapChain(const vk::PresenterDesc& desc) override;
    vk::PresenterInfo info() const override;

    // waits for all queued frames to be consumed
    void synchronize() override;

    int getPresentFrameCount() {
      return m_lastPresentFrameCount;
    }
  private:
    DxvkDevice* m_device;
    Rc<DxvkContext> m_ctx;

    // used by HUD to determine FPS, holds the total number of frames queued in the latest present call
    // initialized to 1 since during the first frame this will be queried before DLFG runs, so assume worst-case and let it update later
    int m_lastPresentFrameCount = 1;
    
    // the number of images requested by the app
    // the actual swapchain is sized to hold N interpolated frames + 1 rendered frame
    uint32_t m_appRequestedImageCount = 0;

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
      uint32_t acquiredImageIndex;
      DxvkPresentInfo present;
      DxvkFrameInterpolationInfo frameInterpolation;
    };

    WorkerThread m_presentThread;
    std::queue<PresentJob> m_presentQueue;

    struct PacerJob {
      uint32_t dlfgQueryIndex;
      VkFence lastCmdListFence;
      uint64_t semaphoreSignalValue;    // signal value for the first interpolated frame
      uint32_t interpolatedFrameCount;  // number of consecutive signals to emit, each one increments the signal value by 1
    };
    
    struct SwapchainImage {
      vk::PresenterImage image;
      vk::PresenterSync sync;
      uint32_t index;
    };

    WorkerThread m_pacerThread;
    std::queue<PacerJob> m_pacerQueue;

    std::atomic<VkResult> m_lastPresentStatus = VK_SUCCESS;

    DxvkDLFGCommandListArray m_dlfgCommandLists;
    DxvkDLFGCommandListArray m_blitCommandLists;
    DxvkDLFGCommandListArray m_presentPacingCommandLists;

    void synchronize(std::unique_lock<dxvk::mutex>& lock);
    bool swapchainAcquire(SwapchainImage& swapchainImage);
    bool interpolateFrame(DxvkDLFGCommandList* commandList, SwapchainImage& swapchainImage, const PresentJob& present, uint32_t interpolatedFrameIndex);
    void blitRenderedFrame(DxvkDLFGCommandList* commandList, SwapchainImage& renderedSwapchainImage, const PresentJob& present, bool frameInterpolated);
    bool submitPresent(SwapchainImage& image, const PresentJob& present, uint64_t pacerSemaphoreWaitValue, VkSetPresentConfigNV*
                       presentMetering);
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

    void dispatch(Rc<DxvkContext> ctx,
                  DxvkDLFGCommandList* commandList,
                  const RtCamera& camera,
                  Rc<DxvkImageView> outputImage,                       // VK_IMAGE_LAYOUT_GENERAL
                  Rc<DxvkImageView> colorBuffer,
                  Rc<DxvkImageView> primaryScreenSpaceMotionVector,    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                  Rc<DxvkImageView> primaryDepth,                      // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                  uint32_t interpolatedFrameIndex,                     // starts at 0
                  uint32_t interpolatedFrameCount,                     // total number of frames we will interpolate before the next rendered frame
                  bool resetHistory = false);

    Rc<RtxSemaphore>& getFrameEndSemaphore() {
      return m_dlfgFrameEndSemaphore;
    }

    uint64_t& frameEndSemaphoreValue() {
      return m_dlfgFrameEndSemaphoreValue;
    }

    Rc<DxvkDLFGTimestampQueryPool>& getDLFGQueryPool() {
      return m_queryPoolDLFG;
    }

    bool hasDLFGFailed() const {
      return m_hasDLFGFailed;
    }

    bool supportsPresentMetering() const;

    // returns the maximum number of interpolated frames we can generate on the current system
    uint32_t getMaxSupportedInterpolatedFrameCount();
    // returns the currently configured number of interpolated frames
    uint32_t getInterpolatedFrameCount();

    RTX_OPTION_ENV("rtx.dlfg", bool, enable, true, "RTX_DLFG_ENABLE", "Enables DLSS 3.0 frame generation which generates interpolated frames to increase framerate at the cost of slightly more latency."); // note: always use DxvkDevice::isDLFGEnabled() to check if DLFG is enabled, not this option directly
    RTX_OPTION("rtx.dlfg", uint32_t, maxInterpolatedFrames, 2, "For DLSS 4.0 frame generation, controls the number of interpolated frames for each rendered frame. Ignored for DLSS 3.0.");
    RTX_OPTION("rtx.dlfg", bool, enablePresentMetering, true, "Use hardware present metering for DLSS 4.0 frame generation instead of CPU pacing.");

  private:
    std::unique_ptr<NGXDLFGContext> m_dlfgContext = nullptr;
    std::atomic<bool> m_hasDLFGFailed = { false };
    uint32_t m_currentDisplaySize[2] = { 0,0 };
    bool m_contextDirty = true;
    
    DxvkDLFGCommandListArray m_dlfgEvalCommandLists;

    Rc<RtxSemaphore> m_dlfgFrameEndSemaphore = nullptr;
    uint64_t m_dlfgFrameEndSemaphoreValue = 0;

    // timestamp query pool for the DLFG pacer
    Rc<DxvkDLFGTimestampQueryPool> m_queryPoolDLFG;
  };
} // namespace dxvk
