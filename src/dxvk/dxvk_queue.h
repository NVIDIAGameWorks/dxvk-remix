#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

#include "../util/thread.h"

#include "../vulkan/vulkan_presenter.h"

#include "dxvk_cmdlist.h"
#include "rtx_camera.h"

namespace dxvk {
  
  class DxvkDevice;

  /**
   * \brief Submission status
   * 
   * Stores the result of a queue
   * submission or a present call.
   */
  struct DxvkSubmitStatus {
    std::atomic<VkResult> result = { VK_SUCCESS };
  };


  /**
   * \brief Queue submission info
   * 
   * Stores parameters used to submit
   * a command buffer to the device.
   */
  struct DxvkSubmitInfo {
    Rc<DxvkCommandList> cmdList;
    VkSemaphore         waitSync;
    VkSemaphore         wakeSync;
  };
  
  
  /**
   * \brief Present info
   *
   * Stores parameters used to present
   * a swap chain image on the device.
   */
  struct DxvkPresentInfo {
    Rc<vk::Presenter>   presenter;
    uint64_t            cachedReflexFrameId;
    // Note: This flag is specifically used when the DXVK Queue should insert Reflex present
    // markers rather than the presenter currently in use. This is done because some presenters
    // (namely the DLFG Presenter) will insert their own Reflex markers due to having more complex
    // requirements.
    bool                insertReflexPresentMarkers;
  };


  // NV-DXVK start: DLFG integration
  /**
   * \brief Frame interpolation info
   *
   * Stores parameters used to run frame
   * interpolation in the submit queue at present time.
   */
  struct DxvkFrameInterpolationInfo {
    uint32_t          frameId;
    RtCamera          camera;
    Rc<DxvkImageView> motionVectors;
    VkImageLayout     motionVectorsLayout;
    Rc<DxvkImageView> depth;
    VkImageLayout     depthLayout;
    bool              resetHistory;
    
    bool valid() const {
      return motionVectors.ptr() &&
        depth.ptr();
    }

    void reset() {
      motionVectors = nullptr;
      depth = nullptr;
      resetHistory = false;
    }
  };
  // NV-DXVK end

  /**
   * \brief Submission queue entry
   */
  struct DxvkSubmitEntry {
    DxvkSubmitStatus*   status;
    DxvkSubmitInfo      submit;
    DxvkPresentInfo     present;
    // NV-DXVK start: DLFG integration
    // sent down to stash frame interpolation parameters before present
    DxvkFrameInterpolationInfo frameInterpolation;
    // NV-DXVK end
  };

  /**
   * \brief Submission queue
   */
  class DxvkSubmissionQueue {

  public:
    
    DxvkSubmissionQueue(DxvkDevice* device);
    ~DxvkSubmissionQueue();

    /**
     * \brief Number of pending submissions
     * 
     * A return value of 0 indicates
     * that the GPU is currently idle.
     * \returns Pending submission count
     */
    uint32_t pendingSubmissions() const {
      return m_pending.load();
    }

    /**
     * \brief Retrieves estimated GPU idle time
     *
     * This is a monotonically increasing counter
     * which can be evaluated periodically in order
     * to calculate the GPU load.
     * \returns Accumulated GPU idle time, in us
     */
    uint64_t gpuIdleTicks() const {
      return m_gpuIdle.load();
    }

    /**
     * \brief Retrieves last submission error
     * 
     * In case an error occured during asynchronous command
     * submission, it will be returned by this function.
     * \returns Last error from command submission
     */
    VkResult getLastError() const {
      return m_lastError.load();
    }
    
    /**
     * \brief Submits a command list asynchronously
     * 
     * Queues a command list for submission on the
     * dedicated submission thread. Use this to take
     * the submission overhead off the calling thread.
     * \param [in] submitInfo Submission parameters 
     */
    void submit(
            DxvkSubmitInfo      submitInfo);
    
    /**
     * \brief Presents an image synchronously
     *
     * Waits for queued command lists to be submitted
     * and then presents the current swap chain image
     * of the presenter. May stall the calling thread.
     * \param [in] present Present parameters
     * \returns Status of the operation
     */
    void present(
            DxvkPresentInfo     presentInfo,
            DxvkSubmitStatus*   status);

    // NV-DXVK start: DLFG integration
    /**
     * \brief Set up frame interpolation parameters for next present
     *
     * Used to send down all data required to do frame interpolation
     * at present time, except for the final output image.
     * If not called on a given frame, or called with an invalid input,
     * frame interpolation won't be done.
     */
    void setupFrameInterpolation(
      DxvkFrameInterpolationInfo frameInterpolationInfo);

    /**
     * \brief Checks if the next present will trigger frame interpolation
     * \returns true if frame interpolation is set up for the current frame
     */
    //bool frameInterpolationOnNextPresent();
    
    /**
     * \brief Does a busy-wait in the current thread
     */
    //void threadWaitUs(int64_t us);
    // NV-DXVK end

    /**
     * \brief Synchronizes with one queue submission
     * 
     * Waits for the result of the given submission
     * or present operation to become available.
     * \param [in,out] status Submission status
     */
    void synchronizeSubmission(
            DxvkSubmitStatus*   status);
    
    /**
     * \brief Synchronizes with queue submissions
     * 
     * Waits for all pending command lists to be
     * submitted to the GPU before returning.
     */
    void synchronize();

    /**
     * \brief Locks device queue
     *
     * Locks the mutex that protects the Vulkan queue
     * that DXVK uses for command buffer submission.
     * This is needed when the app submits its own
     * command buffers to the queue.
     */
    void lockDeviceQueue();

    /**
     * \brief Unlocks device queue
     *
     * Unlocks the mutex that protects the Vulkan
     * queue used for command buffer submission.
     */
    void unlockDeviceQueue();
  
  private:

    DxvkDevice*             m_device;

    std::atomic<VkResult>   m_lastError = { VK_SUCCESS };
    
    std::atomic<bool>       m_stopped = { false };
    std::atomic<uint32_t>   m_pending = { 0u };
    std::atomic<uint64_t>   m_gpuIdle = { 0ull };

    dxvk::mutex                 m_mutex;
    dxvk::mutex                 m_mutexQueue;
    
    dxvk::condition_variable    m_appendCond;
    dxvk::condition_variable    m_submitCond;
    dxvk::condition_variable    m_finishCond;

    std::queue<DxvkSubmitEntry> m_submitQueue;
    std::queue<DxvkSubmitEntry> m_finishQueue;

    dxvk::thread                m_submitThread;
    dxvk::thread                m_finishThread;

    VkResult submitToQueue(
      const DxvkSubmitInfo& submission);

    void submitCmdLists();

    void finishCmdLists();
    
    // NV-DXVK start: DLFG integration
    DxvkFrameInterpolationInfo m_currentFrameInterpolationData;
    // stash a reference to the last presenter object in case we need to flush
    // this is reset when we flush
    Rc<vk::Presenter> m_lastPresenter = nullptr;
    // NV-DXVK end
  };
}
