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
#include <algorithm>
#include <chrono>
#include <random>
#include <utility>

#include "dxvk_memory_diagnostics.h"

#include "../util/log/log.h"
#include "../util/util_sentry.h"
#include "../util/util_string.h"

namespace dxvk {

  namespace {
    // Video memory exhaustion within this window counts as recent for crash dialogs and crash reports.
    constexpr int64_t kRecentExhaustionWindowMs = 10000;

    // Failed vkAllocateMemory calls are logged at most once per this interval.
    constexpr int64_t kDriverFailureLogIntervalMs = 10000;

    const char* categoryName(DxvkMemoryStats::Category category) {
      return category == DxvkMemoryStats::Category::Invalid
        ? "Invalid"
        : DxvkMemoryStats::categoryToString(category);
    }
  }

  DxvkMemoryDiagnostics::DxvkMemoryDiagnostics()
  : DxvkMemoryDiagnostics(sentry::setTag, sentry::setContext) { }

  DxvkMemoryDiagnostics::DxvkMemoryDiagnostics(PublishValue setTag, PublishValue setContext)
  : m_setTag(std::move(setTag)), m_setContext(std::move(setContext)) { }

  DxvkMemoryDiagnostics& DxvkMemoryDiagnostics::get() {
    static DxvkMemoryDiagnostics s_instance;
    return s_instance;
  }

  int64_t DxvkMemoryDiagnostics::nowMs() {
    // 0 marks "never" in the timestamps below, so the clock starts at 1.
    return std::max<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count(), 1);
  }

  bool DxvkMemoryDiagnostics::injectFailure(DxvkMemoryStats::Category category) {
    const uint32_t categoryMask = FaultInjection::failCategoryMask();

    if ((categoryMask & (1u << category)) == 0) {
      return false;
    }

    if (m_injectionCandidates.fetch_add(1) < FaultInjection::failAfterCount()) {
      return false;
    }

    const float probability = FaultInjection::failProbability();

    if (probability >= 1.0f) {
      return true;
    }

    thread_local std::minstd_rand s_random(std::random_device {}());
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(s_random) < probability;
  }

  VkDeviceSize DxvkMemoryDiagnostics::injectedHeapLimit() {
    return VkDeviceSize(FaultInjection::deviceLocalHeapLimitMB()) << 20;
  }

  bool DxvkMemoryDiagnostics::recordFallback(DxvkMemoryStats::Category category, VkDeviceSize size, const AllocationEvidence& evidence,
                                             const std::function<std::string()>& formatReport) {
    const uint32_t previousCount = m_fallbackCounts[category]++;
    m_fallbackBytes[category] += size;

    if (previousCount != 0) {
      return false;
    }

    const std::string heapReport = formatReport();
    Logger::warn(str::format(
      "DxvkMemoryAllocator: An allocation of ", size, " bytes in category ", categoryName(category),
      " asked for video memory and was placed in system memory. This can reduce performance.", formatEvidence(evidence), "\n",
      "Further fallbacks in this category are counted but not logged. Memory state:\n", heapReport));
    publishToCrashReporting(heapReport);
    return true;
  }

  void DxvkMemoryDiagnostics::recordFailure(DxvkMemoryStats::Category category, VkDeviceSize size, VkMemoryPropertyFlags flags,
                                            const AllocationEvidence& evidence, bool injected, const std::function<std::string()>& formatReport) {
    const std::string failure = str::format(
      injected ? "An injected failure of an allocation of " : "An allocation of ",
      size, " bytes in category ", categoryName(category),
      " with memory property flags 0x", std::hex, flags, std::dec, " failed.", formatEvidence(evidence));

    {
      std::lock_guard<dxvk::mutex> lock(m_failureMutex);
      m_lastFailure = failure;
    }

    m_failureCount++;

    // The report includes the failure recorded above.
    const std::string heapReport = formatReport();
    Logger::err(str::format("DxvkMemoryAllocator: ", failure, " Memory state:\n", heapReport));
    publishToCrashReporting(heapReport);
  }

  bool DxvkMemoryDiagnostics::recordDriverFailure(uint32_t memoryTypeId, uint32_t heapId, VkDeviceSize size, VkResult result, int64_t timeMs) {
    m_driverFailureCount++;
    m_unloggedDriverFailures++;

    {
      std::lock_guard<dxvk::mutex> lock(m_failureMutex);
      m_lastDriverFailure = { result, size, memoryTypeId, heapId };
    }

    // Only the thread that moves the next log time forward logs, so concurrent failures produce one message.
    int64_t nextLogTimeMs = m_nextDriverFailureLogTimeMs;

    if (timeMs < nextLogTimeMs
     || !m_nextDriverFailureLogTimeMs.compare_exchange_strong(nextLogTimeMs, timeMs + kDriverFailureLogIntervalMs)) {
      return false;
    }

    const uint32_t unloggedCount = m_unloggedDriverFailures.exchange(0);
    const DriverFailure failure = { result, size, memoryTypeId, heapId };
    Logger::warn(str::format(
      "DxvkMemoryAllocator: vkAllocateMemory failed with ", formatDriverFailure(failure), ".",
      unloggedCount > 1 ? str::format(" ", unloggedCount - 1, " earlier failures were not logged.") : std::string(),
      " These failures are logged at most every ", kDriverFailureLogIntervalMs / 1000, " s,",
      " and the memory report counts them all."));
    return true;
  }

  uint32_t DxvkMemoryDiagnostics::getFallbackCount(DxvkMemoryStats::Category category) const {
    return m_fallbackCounts[category];
  }

  VkDeviceSize DxvkMemoryDiagnostics::getFallbackBytes(DxvkMemoryStats::Category category) const {
    return m_fallbackBytes[category];
  }

  uint32_t DxvkMemoryDiagnostics::getTotalFallbackCount() const {
    uint32_t total = 0;

    for (const std::atomic<uint32_t>& count : m_fallbackCounts) {
      total += count;
    }

    return total;
  }

  bool DxvkMemoryDiagnostics::hadVideoMemoryExhaustion(int64_t timeMs) const {
    const int64_t lastExhaustionTimeMs = m_lastExhaustionTimeMs;
    return lastExhaustionTimeMs != 0
        && lastExhaustionTimeMs <= timeMs
        && timeMs - lastExhaustionTimeMs < kRecentExhaustionWindowMs;
  }

  std::string DxvkMemoryDiagnostics::formatSummary() const {
    std::string summary;

    if (getTotalFallbackCount() == 0) {
      summary = "System memory fallbacks: none\n";
    } else {
      summary = "System memory fallbacks:\n";

      for (uint32_t category = DxvkMemoryStats::Category::First; category <= DxvkMemoryStats::Category::Last; category++) {
        const uint32_t count = m_fallbackCounts[category];

        if (count != 0) {
          summary += str::format("  ", categoryName(DxvkMemoryStats::Category(category)), ": ",
                                 count, " allocations, ", m_fallbackBytes[category] >> 20, " MB\n");
        }
      }
    }

    summary += str::format("Video memory exhausted: ", m_videoMemoryEverExhausted ? "yes" : "no", "\n");
    summary += str::format("Allocation failures: ", m_failureCount.load(), "\n");

    std::lock_guard<dxvk::mutex> lock(m_failureMutex);

    if (!m_lastFailure.empty()) {
      summary += str::format("Last allocation failure: ", m_lastFailure, "\n");
    }

    const uint32_t driverFailureCount = m_driverFailureCount;

    if (driverFailureCount != 0) {
      summary += str::format("vkAllocateMemory failures: ", driverFailureCount, ". The last returned ", formatDriverFailure(m_lastDriverFailure), ".\n");
    }

    return summary;
  }

  void DxvkMemoryDiagnostics::publishToCrashReporting(const std::string& heapReport) {
    std::lock_guard<dxvk::mutex> lock(m_crashReportingMutex);
    m_setTag("memory_fallback", getTotalFallbackCount() != 0 ? "true" : "false");
    m_setTag("memory_alloc_failed", m_failureCount != 0 ? "true" : "false");
    m_setTag("memory_vram_exhausted", m_videoMemoryEverExhausted ? "true" : "false");
    m_setContext("memory", heapReport);
  }

  void DxvkMemoryDiagnostics::recordVideoMemoryExhaustion(int64_t timeMs) {
    m_lastExhaustionTimeMs = timeMs;

    if (!m_videoMemoryEverExhausted.exchange(true)) {
      std::lock_guard<dxvk::mutex> lock(m_crashReportingMutex);
      m_setTag("memory_vram_exhausted", "true");
    }
  }

  std::string DxvkMemoryDiagnostics::formatDriverFailure(const DriverFailure& failure) {
    return str::format(failure.result, " for ", failure.size, " bytes from memory type ", failure.memoryTypeId, " on heap ", failure.heapId);
  }

  std::string DxvkMemoryDiagnostics::formatEvidence(const AllocationEvidence& evidence) {
    std::string text;

    if (evidence.injectedLimitReached) {
      text += " The injected device-local heap limit was reached.";
    } else if (evidence.videoMemoryExhausted) {
      text += " A device-local heap was out of memory.";
    }

    if (evidence.lastDriverResult != VK_SUCCESS) {
      text += str::format(" The last vkAllocateMemory error was ", evidence.lastDriverResult, ".");
    }

    if (evidence.budgetRefused) {
      text += " A heap budget refused an attempt.";
    }

    if (evidence.mapFailed) {
      text += " Mapping host-visible memory failed.";
    }

    return text;
  }

  void DxvkGpuCrashMemorySnapshot::capture(int64_t detectionTimeMs) {
    std::call_once(m_once, [this, detectionTimeMs] {
      m_videoMemoryExhausted = DxvkMemoryDiagnostics::get().hadVideoMemoryExhaustion(detectionTimeMs);
    });
  }

}
