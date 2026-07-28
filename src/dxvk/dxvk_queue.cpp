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
#include "dxvk_device.h"
#include "dxvk_queue.h"
#include "dxvk_scoped_annotation.h"

#include "NvLowLatencyVk.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"

#include "../util/util_env.h"
// NV-DXVK start: Shared crash-report exit code / dialog helpers
#include "../util/util_crash_report.h"
#include "rtx_render/rtx_bridge_message_channel.h"
// NV-DXVK end
#include "../util/util_sentry.h"

namespace dxvk {
  
  DxvkSubmissionQueue::DxvkSubmissionQueue(DxvkDevice* device)
  : m_device(device),
    m_submitThread([this] () { submitCmdLists(); }),
    m_finishThread([this] () { finishCmdLists(); }) {
  }
  
  
  DxvkSubmissionQueue::~DxvkSubmissionQueue() {
    { std::unique_lock<dxvk::mutex> lock(m_mutex);
      m_stopped.store(true);
    }
    
    m_appendCond.notify_all();
    m_submitCond.notify_all();

    m_submitThread.join();
    m_finishThread.join();
  }
  
  
  void DxvkSubmissionQueue::submit(DxvkSubmitInfo submitInfo) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_finishCond.wait(lock, [this] {
      return m_submitQueue.size() + m_finishQueue.size() <= MaxNumQueuedCommandBuffers;
    });

    DxvkSubmitEntry entry = { };
    entry.submit = std::move(submitInfo);

    m_pending += 1;
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }


  void DxvkSubmissionQueue::present(DxvkPresentInfo presentInfo, DxvkSubmitStatus* status) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    DxvkSubmitEntry entry = { };
    entry.status  = status;
    entry.present = std::move(presentInfo);
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }


// NV-DXVK begin: DLFG integration
  void DxvkSubmissionQueue::setupFrameInterpolation(DxvkFrameInterpolationInfo frameInterpolationInfo) {
    ZoneScoped;
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    DxvkSubmitEntry entry = { };
    entry.frameInterpolation = std::move(frameInterpolationInfo);
    m_submitQueue.push(std::move(entry));
    m_appendCond.notify_all();
  }
// NV-DXVK end

  void DxvkSubmissionQueue::synchronizeSubmission(
          DxvkSubmitStatus*   status) {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_submitCond.wait(lock, [status] {
      return status->result.load() != VK_NOT_READY;
    });
  }


  void DxvkSubmissionQueue::synchronize() {
    ScopedCpuProfileZone();
    std::unique_lock<dxvk::mutex> lock(m_mutex);

    m_submitCond.wait(lock, [this] {
      return m_submitQueue.empty();
    });

    // NV-DXVK start: DLFG integration
    if (m_lastPresenter != nullptr) {
      m_lastPresenter->synchronize();
      m_lastPresenter = nullptr;
    }
    // NV-DXVK end
  }


  void DxvkSubmissionQueue::lockDeviceQueue() {
    ScopedCpuProfileZone();
    m_mutexQueue.lock();
  }


  void DxvkSubmissionQueue::unlockDeviceQueue() {
    ScopedCpuProfileZone();
    m_mutexQueue.unlock();
  }

  // NV-DXVK start: GPU crash diagnostics
  void DxvkSubmissionQueue::onGpuCrash(const char* reason) {
    if (m_gpuCrashHandled.exchange(true)) {
      return;
    }

    // The submission queue is destroyed and its threads are joined before the
    // device's common objects, so the scene manager and recorder are alive here.
    m_device->getCommon()->getSceneManager().getAccelManager().dumpCrashState(reason);

    if (m_device->config().enableAftermath) {
      // Stall the pending exception until Aftermath has finished writing or reports an error.
      constexpr uint32_t kTimeoutPreventionLimit = 5000;
      constexpr uint32_t kTimeoutPerTry = 100;
      uint32_t elapsedTime = 0;
      GFSDK_Aftermath_CrashDump_Status aftermathStatus = GFSDK_Aftermath_CrashDump_Status_NotStarted;

      while (elapsedTime < kTimeoutPreventionLimit) {
        GFSDK_Aftermath_GetCrashDumpStatus(&aftermathStatus);

        if (aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Finished
         || aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Unknown) {
          break;
        }

        Sleep(kTimeoutPerTry);
        elapsedTime += kTimeoutPerTry;
      }
    }
  }
  // NV-DXVK end

  void DxvkSubmissionQueue::submitCmdLists() {
    env::setThreadName("dxvk-submit");

    std::unique_lock<dxvk::mutex> lock(m_mutex);

    while (!m_stopped.load()) {
      m_appendCond.wait(lock, [this] {
        return m_stopped.load() || !m_submitQueue.empty();
      });
      
      if (m_stopped.load())
        return;

      ScopedCpuProfileZone();

      DxvkSubmitEntry entry = std::move(m_submitQueue.front());
      lock.unlock();
      
      // Submit command buffer to device
      VkResult status = VK_NOT_READY;

      if (m_lastError != VK_ERROR_DEVICE_LOST) {
        // NV-DXVK start: Rename lock to lockQueue to avoid shadowing other mutex
        std::lock_guard<dxvk::mutex> lockQueue(m_mutexQueue);
        // NV-DXVK end

          // NV-DXVK start: Reflex render submit
        const auto& reflex = m_device->getCommon()->metaReflex();
        // NV-DXVK end

        if (entry.submit.cmdList != nullptr) {
          // When using Reflex with Remix, we need to wrap the queue submit for the injectRTX rendering
          // work with the reflex render_submit markers.  This is because in Remix we essentially
          // have one large cmd list of work (inject rtx) and we want the Reflex timing to prioritize
          // this work for best latency reduction while minimizing performance impact.  So we tag the submit
          // upstream (RtxContext) which contains the injectRTX call as the one we want to wrap with Reflex markers.
          // NV-DXVK start: Reflex render submit
          if (entry.submit.insertReflexRenderMarkers) {
            reflex.beginRendering(entry.submit.cachedReflexFrameId);
          }

          status = entry.submit.cmdList->submit(
            entry.submit.waitSync,
            entry.submit.wakeSync);

          if (entry.submit.insertReflexRenderMarkers) {
            reflex.endRendering(entry.submit.cachedReflexFrameId);
          }
          // NV-DXVK end
        }
        // NV-DXVK start: DLFG integration
        else if (entry.frameInterpolation.valid()) {
          // stash frame interpolation data for next present call
          m_currentFrameInterpolationData = entry.frameInterpolation;
        }
        else if (entry.present.presenter != nullptr) {
          m_lastPresenter = entry.present.presenter;

          // NV-DXVK start: Reflex present start
          const auto insertReflexPresentMarkers = entry.present.insertReflexPresentMarkers;
          const auto cachedReflexFrameId = entry.present.cachedReflexFrameId;

          // Note: Only insert Reflex Present markers around the Presenter's present call if requested.
          if (insertReflexPresentMarkers) {
            reflex.beginPresentation(cachedReflexFrameId);
          }
          // NV-DXVK end

          // NV-DXVK start: DLFG acquired image information retrieval
          const auto cachedAcquiredImageIndex = entry.present.cachedAcquiredImageIndex;
          // NV-DXVK end

          // m_device->vkd()->vkQueueWaitIdle(m_device->queues().graphics.queueHandle);
          status = entry.present.presenter->presentImage(&entry.status->result, entry.present, m_currentFrameInterpolationData, cachedAcquiredImageIndex);
          // if both submit and DLFG+present run on the same queue, then we need to wait for present to avoid racing on the queue
#if __DLFG_USE_GRAPHICS_QUEUE
          entry.present.presenter->synchronize();
#endif

          // NV-DXVK start: Reflex present end
          // Note: Only insert Reflex Present markers around the Presenter's present call if requested.
          if (insertReflexPresentMarkers) {
            reflex.endPresentation(cachedReflexFrameId);
          }
          // NV-DXVK end

          m_currentFrameInterpolationData.reset();

          const auto presentThrottleDelay = m_device->config().presentThrottleDelay;

          if (presentThrottleDelay > 0) {
            ScopedCpuProfileZoneN("Present Throttle Delay Sleep");

            Sleep(presentThrottleDelay);
          }
        }
      } else {
        // Don't submit anything after device loss
        // so that drivers get a chance to recover
        status = VK_ERROR_DEVICE_LOST;
      }

      if (entry.status)
        // NV-DXVK start: DLFG integration
        // if we queued for interpolation, then don't touch the output status here; DLFG presenter thread will update it (and may have already done so)
        if (status != VK_EVENT_SET) {
          entry.status->result = status;
        }
        // NV-DXVK end

      // On success, pass it on to the queue thread
      lock = std::unique_lock<dxvk::mutex>(m_mutex);

      if (status == VK_SUCCESS) {
        if (entry.submit.cmdList != nullptr)
          m_finishQueue.push(std::move(entry));
      } else if (status == VK_ERROR_DEVICE_LOST || entry.submit.cmdList != nullptr) {
        Logger::err(str::format("DxvkSubmissionQueue: Command submission failed: ", status));
        m_lastError = status;

        // NV-DXVK start: GPU crash diagnostics and handling on device loss
        // Aftermath only produces a GPU crash dump for device loss. Other
        // submission failures retain their existing handling without a stall.
        if (status == VK_ERROR_DEVICE_LOST) {
          onGpuCrash("queue submission");
          handleDeviceLost();
        } else {
          waitForAftermathDump();
          m_device->waitForIdle();
        }
        // NV-DXVK end
      }

      m_submitQueue.pop();
      m_submitCond.notify_all();
    }
  }
  
  
  void DxvkSubmissionQueue::finishCmdLists() {
    env::setThreadName("dxvk-queue");

    while (!m_stopped.load()) {
      std::unique_lock<dxvk::mutex> lock(m_mutex);

      if (m_finishQueue.empty()) {
        auto t0 = dxvk::high_resolution_clock::now();

        m_submitCond.wait(lock, [this] {
          return m_stopped.load() || !m_finishQueue.empty();
        });

        auto t1 = dxvk::high_resolution_clock::now();
        m_gpuIdle += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
      }

      if (m_stopped.load())
        return;

      ScopedCpuProfileZone();
      
      DxvkSubmitEntry entry = std::move(m_finishQueue.front());
      lock.unlock();
      
      VkResult status = m_lastError.load();
      
      if (status != VK_ERROR_DEVICE_LOST)
        status = entry.submit.cmdList->synchronize();
      
      if (status != VK_SUCCESS) {
        Logger::err(str::format("DxvkSubmissionQueue: Failed to sync fence: ", status));
        m_lastError = status;

        // NV-DXVK start: GPU crash diagnostics and handling on device loss
        if (status == VK_ERROR_DEVICE_LOST) {
          onGpuCrash("fence synchronization");
          handleDeviceLost();
        } else {
          m_device->waitForIdle();
        }
        // NV-DXVK end
      }

      // Release resources and signal events, then immediately wake
      // up any thread that's currently waiting on a resource in
      // order to reduce delays as much as possible.
      entry.submit.cmdList->notifyObjects();

      lock.lock();
      m_pending -= 1;

      m_finishQueue.pop();
      m_finishCond.notify_all();
      lock.unlock();

      // Free the command list and associated objects now
      entry.submit.cmdList->reset();
      m_device->recycleCommandList(entry.submit.cmdList);
    }
  }

  // NV-DXVK start: GPU crash handling (Aftermath dump wait + device-loss reporting)
  void DxvkSubmissionQueue::waitForAftermathDump() {
    if (!m_device->config().enableAftermath) {
      return;
    }

    // Stall the pending exception until Aftermath has finished writing (or hits some error).
    uint32_t counter = 0;
    GFSDK_Aftermath_CrashDump_Status aftermathStatus = GFSDK_Aftermath_CrashDump_Status_NotStarted;

    static const uint32_t kTimeoutPreventionLimit = 5000;

    while (counter < kTimeoutPreventionLimit) {
      GFSDK_Aftermath_GetCrashDumpStatus(&aftermathStatus);

      if (aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Finished || aftermathStatus == GFSDK_Aftermath_CrashDump_Status_Unknown) {
        break; // Our dump was written
      }

      static const uint32_t kTimeoutPerTry = 100;
      Sleep(kTimeoutPerTry);
      counter += kTimeoutPerTry;
    }
  }

  void DxvkSubmissionQueue::handleDeviceLost() {
    // Both the submit and finish threads can observe device loss; ensure only the first one performs
    // crash handling. Any other thread blocks here until the process is terminated below.
    static std::atomic<bool> s_handlingDeviceLoss(false);
    if (s_handlingDeviceLoss.exchange(true)) {
      while (true) {
        Sleep(1000);
      }
    }

    // Notify the bridge client immediately so it can disable the Windows ghost dialog and
    // prepare the crash dialog window before the slow Sentry capture/upload below.
    BridgeMessageChannel::get().send(kGpuCrashNotifyMsgName, 0, 0);

    waitForAftermathDump();
    // Capture/upload the GPU crash (no-op if Sentry is inactive) before tearing down.
    sentry::processPendingGpuCrashReports();
    Logger::err("DxvkSubmissionQueue: Exiting after GPU device loss");
    // Signal the GPU-crash exit code so the bridge can distinguish this from a CPU crash.
    // sentry::shutdown() is intentionally omitted here: sentry_close() blocks for several seconds
    // waiting for a network flush, but at this point crash data is already captured and cached
    // locally — the bridge client will trigger upload after the user grants consent. The
    // out-of-process crashpad_handler.exe handles any remaining cleanup independently.
    env::killProcess(kRemixGpuCrashExitCode);
  }
  // NV-DXVK end
}
