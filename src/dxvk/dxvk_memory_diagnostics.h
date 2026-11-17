/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "dxvk_memory.h"
#include "rtx_render/rtx_option.h"

namespace dxvk {

  // Records allocations that fell back from video memory to system memory, and allocations that failed,
  // so that logs, the HUD and crash reports can show memory pressure. Also injects allocation failures for testing.
  // The state is process-wide, since a process normally has one device.
  class DxvkMemoryDiagnostics {
  public:
    // What the allocator learned while trying one allocation, across all of its retries.
    struct AllocationEvidence {
      // A device-local heap was out of memory.
      // Either the driver returned VK_ERROR_OUT_OF_DEVICE_MEMORY for a device-local memory type, or the injected heap limit refused the allocation.
      bool videoMemoryExhausted = false;
      bool injectedLimitReached = false;
      // A heap budget other than the injected limit refused an attempt.
      bool budgetRefused = false;
      bool mapFailed = false;
      // The last error vkAllocateMemory returned, or VK_SUCCESS when it returned none.
      VkResult lastDriverResult = VK_SUCCESS;
    };

    struct FaultInjection {
      RTX_OPTION_ARGS("rtx.memory.faultInjection", uint32_t, failCategoryMask, 0,
                      "A bit mask of memory categories whose allocations are made to fail, for testing how failures are handled. Bit N selects category N:\n"
                      "1: App Buffer, 2: App Texture, 3: RTX Replacement Geometry, 4: RTX Buffer, 5: RTX Acceleration Structure, 6: RTX Opacity Micromap, 7: RTX Material Texture, 8: RTX Render Target.\n"
                      "0 disables injected failures.",
                      args.flags = RtxOptionFlags::NoSave);
      RTX_OPTION_ARGS("rtx.memory.faultInjection", uint32_t, failAfterCount, 0,
                      "The number of allocations in the categories selected by failCategoryMask that succeed before injected failures begin.",
                      args.flags = RtxOptionFlags::NoSave);
      RTX_OPTION_ARGS("rtx.memory.faultInjection", float, failProbability, 1.0f,
                      "The probability, from 0 to 1, that an allocation in the categories selected by failCategoryMask fails once injected failures have begun.",
                      args.minValue = 0.0f, args.maxValue = 1.0f, args.flags = RtxOptionFlags::NoSave);
      RTX_OPTION_ARGS("rtx.memory.faultInjection", uint32_t, deviceLocalHeapLimitMB, 0,
                      "Limits how much memory Remix allocates from each device-local heap, in MB, to simulate a GPU with less video memory.\n"
                      "Allocations past the limit fall back to system memory where they can. 0 disables the limit.",
                      args.flags = RtxOptionFlags::NoSave);
      RTX_OPTION_ARGS("rtx.memory.faultInjection", bool, simulateDriverFailure, false,
                      "Makes allocations past deviceLocalHeapLimitMB fail as if vkAllocateMemory returned VK_ERROR_OUT_OF_DEVICE_MEMORY,\n"
                      "rather than being refused before the driver is called. This exercises the handling of driver allocation failures.",
                      args.flags = RtxOptionFlags::NoSave);
    };

    using PublishValue = std::function<void(const std::string&, const std::string&)>;

    DxvkMemoryDiagnostics(PublishValue setTag, PublishValue setContext);

    static DxvkMemoryDiagnostics& get();

    // Returns milliseconds since an arbitrary epoch, the time base for the functions below that take a time.
    static int64_t nowMs();

    // Returns true when fault injection makes this allocation fail.
    bool injectFailure(DxvkMemoryStats::Category category);

    // Returns the limit fault injection places on a device-local heap.
    // Returns 0 when there is no limit.
    static VkDeviceSize injectedHeapLimit();

    // Records that an allocation fell back or failed because a device-local heap was out of memory.
    void recordVideoMemoryExhaustion(int64_t timeMs);

    // Records an allocation that asked for video memory and received system memory.
    // The first fallback in each category is logged with the report formatReport returns.
    bool recordFallback(DxvkMemoryStats::Category category, VkDeviceSize size, const AllocationEvidence& evidence,
                        const std::function<std::string()>& formatReport);

    // Records an allocation that failed after every fallback, and logs the report formatReport returns.
    void recordFailure(DxvkMemoryStats::Category category, VkDeviceSize size, VkMemoryPropertyFlags flags,
                       const AllocationEvidence& evidence, bool injected, const std::function<std::string()>& formatReport);

    // Records a failed vkAllocateMemory call. These repeat across an allocation's retries, so they are logged at most once per interval.
    // Returns true when this failure was logged.
    bool recordDriverFailure(uint32_t memoryTypeId, uint32_t heapId, VkDeviceSize size, VkResult result, int64_t timeMs);

    // The fallback and failure counts are running totals since the process started.
    uint32_t getFallbackCount(DxvkMemoryStats::Category category) const;
    VkDeviceSize getFallbackBytes(DxvkMemoryStats::Category category) const;
    uint32_t getTotalFallbackCount() const;
    uint32_t getFailureCount() const { return m_failureCount; }
    uint32_t getDriverFailureCount() const { return m_driverFailureCount; }

    // Returns true when video memory ran out within the recent window before timeMs.
    // Exhaustion recorded after timeMs does not count.
    bool hadVideoMemoryExhaustion(int64_t timeMs) const;

    // Returns the fallback counts, the failures and the last driver failure as text, for logs and crash reports.
    std::string formatSummary() const;

    // Publishes the summary to crash reporting, so that a later crash carries it.
    void publishToCrashReporting(const std::string& heapReport);

  private:
    struct DriverFailure {
      VkResult result = VK_SUCCESS;
      VkDeviceSize size = 0;
      uint32_t memoryTypeId = 0;
      uint32_t heapId = 0;
    };

    DxvkMemoryDiagnostics();

    static std::string formatDriverFailure(const DriverFailure& failure);
    static std::string formatEvidence(const AllocationEvidence& evidence);

    // Running totals since the process started. Freeing a demoted allocation does not reduce them.
    std::array<std::atomic<uint32_t>, DxvkMemoryStats::Category::Count> m_fallbackCounts = {};
    std::array<std::atomic<VkDeviceSize>, DxvkMemoryStats::Category::Count> m_fallbackBytes = {};
    std::atomic<uint32_t> m_failureCount = 0;
    std::atomic<uint32_t> m_injectionCandidates = 0;

    // Milliseconds from nowMs(), or 0 when video memory has not run out.
    std::atomic<int64_t> m_lastExhaustionTimeMs = 0;
    std::atomic<bool> m_videoMemoryEverExhausted = false;

    std::atomic<uint32_t> m_driverFailureCount = 0;
    std::atomic<uint32_t> m_unloggedDriverFailures = 0;
    std::atomic<int64_t> m_nextDriverFailureLogTimeMs = 0;

    mutable dxvk::mutex m_failureMutex;
    std::string m_lastFailure;
    DriverFailure m_lastDriverFailure;

    dxvk::mutex m_crashReportingMutex;
    const PublishValue m_setTag;
    const PublishValue m_setContext;
  };

  // Records once whether video memory ran out shortly before a GPU crash was detected.
  // The first capture wins, and capture() returns only once the result is available,
  // so every queue thread that handles the crash reports the same result.
  class DxvkGpuCrashMemorySnapshot {
  public:
    void capture(int64_t detectionTimeMs);

    // Must be called after capture() on the same thread.
    bool videoMemoryExhausted() const { return m_videoMemoryExhausted; }

  private:
    std::once_flag m_once;
    bool m_videoMemoryExhausted = false;
  };

}
