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

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/dxvk_device.h"
#include "../../../src/dxvk/dxvk_instance.h"
#include "../../../src/dxvk/dxvk_memory_diagnostics.h"

namespace dxvk {

  Logger Logger::s_instance("test_memory_diagnostics.log");

  namespace {

    // The default suite shares process-wide state. Allocator scenarios run in separate processes.
    DxvkMemoryDiagnostics& diagnostics() {
      return DxvkMemoryDiagnostics::get();
    }

    std::string emptyReport() {
      return std::string();
    }

    void require(bool condition, const char* pMessage) {
      if (!condition) {
        throw DxvkError(pMessage);
      }
    }

    class AllocationProbe {
    public:
      AllocationProbe(const Rc<vk::DeviceFn>& pVkd, VkResult firstAllocationResult, bool failMapping)
      : m_pVkd(pVkd), m_pAllocateMemory(pVkd->vkAllocateMemory), m_pMapMemory(pVkd->vkMapMemory),
        m_threadId(std::this_thread::get_id()), m_firstAllocationResult(firstAllocationResult), m_failMapping(failMapping) {
        s_pActiveProbe = this;
        m_pVkd->vkAllocateMemory = allocateMemory;
        m_pVkd->vkMapMemory = mapMemory;
      }

      ~AllocationProbe() {
        m_pVkd->vkAllocateMemory = m_pAllocateMemory;
        m_pVkd->vkMapMemory = m_pMapMemory;
        s_pActiveProbe = nullptr;
      }

      const std::vector<VkDeviceSize>& allocationSizes() const { return m_allocationSizes; }
      uint32_t mappingCount() const { return m_mappingCount; }
      bool exhaustedBeforeSuccess() const { return m_exhaustedBeforeSuccess; }

    private:
      static VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice device, const VkMemoryAllocateInfo* pInfo,
                                                           const VkAllocationCallbacks* pCallbacks, VkDeviceMemory* pMemory) {
        AllocationProbe& probe = *s_pActiveProbe;
        if (std::this_thread::get_id() == probe.m_threadId) {
          probe.m_allocationSizes.push_back(pInfo->allocationSize);
          if (probe.m_allocationSizes.size() == 1 && probe.m_firstAllocationResult != VK_SUCCESS) {
            *pMemory = VK_NULL_HANDLE;
            return probe.m_firstAllocationResult;
          }
          probe.m_exhaustedBeforeSuccess = diagnostics().hadVideoMemoryExhaustion(DxvkMemoryDiagnostics::nowMs());
        }
        return probe.m_pAllocateMemory(device, pInfo, pCallbacks, pMemory);
      }

      static VKAPI_ATTR VkResult VKAPI_CALL mapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                                      VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
        AllocationProbe& probe = *s_pActiveProbe;
        if (std::this_thread::get_id() == probe.m_threadId) {
          probe.m_mappingCount++;
          if (probe.m_failMapping && probe.m_mappingCount == 1) {
            return VK_ERROR_MEMORY_MAP_FAILED;
          }
        }
        return probe.m_pMapMemory(device, memory, offset, size, flags, ppData);
      }

      inline static AllocationProbe* s_pActiveProbe = nullptr;
      Rc<vk::DeviceFn> m_pVkd;
      PFN_vkAllocateMemory m_pAllocateMemory;
      PFN_vkMapMemory m_pMapMemory;
      std::thread::id m_threadId;
      VkResult m_firstAllocationResult;
      bool m_failMapping;
      std::vector<VkDeviceSize> m_allocationSizes;
      uint32_t m_mappingCount = 0;
      bool m_exhaustedBeforeSuccess = false;
    };

    int testAllocatorRetry(const std::string& scenario) {
      const bool driverFailure = scenario == "driver";
      const bool injectedLimit = scenario == "injected-limit" || scenario == "simulated-driver";
      const bool simulatedDriver = scenario == "simulated-driver";
      const bool hostFailure = scenario == "host";
      const bool mappingFailure = scenario == "mapping";
      const bool budgetRefusal = scenario == "budget";
      require(driverFailure || injectedLimit || hostFailure || mappingFailure || budgetRefusal, "Unknown allocator scenario");

      const Rc<DxvkInstance> pInstance = new DxvkInstance;
      const Rc<DxvkAdapter> pAdapter = pInstance->enumAdapters(0);
      if (pAdapter == nullptr) {
        std::cout << "SKIPPED: No Vulkan adapter for allocator retry tests." << std::endl;
        return 77;
      }
      const Rc<DxvkDevice> pDevice = pAdapter->createDevice(pInstance, pAdapter->features());

      require(diagnostics().getTotalFallbackCount() == 0 && diagnostics().getFailureCount() == 0
              && diagnostics().getDriverFailureCount() == 0,
              "Allocator regressions require a fresh process without initialization failures");
      require(diagnostics().formatSummary().find("Video memory exhausted: no") != std::string::npos,
              "Allocator regression did not start with clean exhaustion state");

      const VkMemoryPropertyFlags flags = hostFailure
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
        : mappingFailure
          ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
          : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      const VkPhysicalDeviceMemoryProperties properties = pAdapter->memoryProperties();
      uint32_t memoryTypeId = properties.memoryTypeCount;
      for (uint32_t typeIndex = 0; typeIndex < properties.memoryTypeCount; typeIndex++) {
        const VkMemoryType& type = properties.memoryTypes[typeIndex];
        const VkMemoryHeap& heap = properties.memoryHeaps[type.heapIndex];
        if ((type.propertyFlags & flags) == flags && heap.size >= (VkDeviceSize(16) << 20) * 15
            && (!hostFailure || !(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))) {
          memoryTypeId = typeIndex;
          break;
        }
      }
      if (memoryTypeId == properties.memoryTypeCount) {
        std::cout << "SKIPPED: No suitable memory type for " << scenario << "." << std::endl;
        return 77;
      }

      using FaultInjection = DxvkMemoryDiagnostics::FaultInjection;
      const RtxOptionLayer* pLayer = RtxOptionLayer::getEnvironmentLayer();
      FaultInjection::failCategoryMaskObject().setImmediately(0, pLayer);
      FaultInjection::deviceLocalHeapLimitMBObject().setImmediately(injectedLimit ? 8 : 0, pLayer);
      FaultInjection::simulateDriverFailureObject().setImmediately(simulatedDriver, pLayer);

      const VkResult firstResult = driverFailure
        ? VK_ERROR_OUT_OF_DEVICE_MEMORY
        : hostFailure ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
      AllocationProbe probe(pDevice->vkd(), firstResult, mappingFailure);
      DxvkMemoryAllocator allocator(pDevice.ptr());
      const uint32_t heapId = properties.memoryTypes[memoryTypeId].heapIndex;
      VkDeviceSize initialChunkSize = VkDeviceSize(16) << 20;
      if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        const bool deviceLocalType = (properties.memoryTypes[memoryTypeId].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        initialChunkSize = VkDeviceSize(deviceLocalType
          ? pInstance->options().deviceLocalMemoryChunkSizeMB
          : pInstance->options().otherMemoryChunkSizeMB) << 20;
        while (initialChunkSize * 15 > properties.memoryHeaps[heapId].size) {
          initialChunkSize >>= 1;
        }
      }
      const VkDeviceSize retryChunkSize = initialChunkSize >> 1;
      allocator.getMemoryHeaps()[heapId].budget = budgetRefusal ? retryChunkSize : 0;

      VkMemoryRequirements requirements = {};
      requirements.size = 64 << 10;
      requirements.alignment = 256;
      requirements.memoryTypeBits = 1u << memoryTypeId;
      VkMemoryDedicatedRequirements dedicatedRequirements = {};
      dedicatedRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
      VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
      dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;

      const int64_t attemptTimeMs = DxvkMemoryDiagnostics::nowMs();
      DxvkMemory memory = allocator.alloc(&requirements, dedicatedRequirements, dedicatedInfo, flags,
                                          DxvkMemoryFlags(), DxvkMemoryStats::Category::AppBuffer);
      require(memory && (memory.propertyFlags() & flags) == flags, "The smaller allocation did not succeed on the requested memory type");
      require(allocator.getMemoryStats(heapId).totalAllocated() == retryChunkSize, "The retry did not allocate the next smaller chunk");
      const bool refusedBeforeDriver = injectedLimit || budgetRefusal;
      require(probe.allocationSizes().size() == (refusedBeforeDriver ? 1 : 2), "Unexpected number of driver allocation attempts");
      require(probe.allocationSizes().front() == (refusedBeforeDriver ? retryChunkSize : initialChunkSize),
              "The allocator did not start with the expected chunk size");
      require(probe.allocationSizes().back() == retryChunkSize, "The next smaller driver allocation did not succeed");
      require(!mappingFailure || probe.mappingCount() == 2, "The mapping failure did not retry the smaller allocation");

      // A heap that refuses a larger chunk is not out of memory when a smaller chunk then succeeds.
      require(!probe.exhaustedBeforeSuccess(), "A refused larger chunk counted as video memory exhaustion");
      DxvkMemory subsequent = allocator.alloc(&requirements, dedicatedRequirements, dedicatedInfo, flags,
                                              DxvkMemoryFlags(), DxvkMemoryStats::Category::AppBuffer);
      require(subsequent, "A subsequent suballocation failed");
      require(!memory.wasDemoted() && !subsequent.wasDemoted(), "A same-type allocation was marked as demoted");
      require(diagnostics().getDriverFailureCount() == (driverFailure || simulatedDriver || hostFailure ? 1u : 0u),
              "Unexpected driver failure count after a successful retry");
      require(diagnostics().getTotalFallbackCount() == 0, "A successful same-type retry counted as a system-memory fallback");
      require(diagnostics().getFailureCount() == 0, "A successful retry counted as a terminal allocation failure");
      require(diagnostics().formatSummary().find("Video memory exhausted: no") != std::string::npos,
              "The memory summary reports exhaustion after a successful retry");

      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(attemptTimeMs + 1000);
      require(!snapshot.videoMemoryExhausted(), "Device loss after a successful retry was classified as video memory exhaustion");
      return 0;
    }

    // An allocation that the injected heap limit pushes into system memory counts as video memory running out.
    int testAllocatorFallback(bool simulateDriverFailure) {
      const Rc<DxvkInstance> pInstance = new DxvkInstance;
      const Rc<DxvkAdapter> pAdapter = pInstance->enumAdapters(0);
      if (pAdapter == nullptr) {
        std::cout << "SKIPPED: No Vulkan adapter for allocator fallback tests." << std::endl;
        return 77;
      }
      const Rc<DxvkDevice> pDevice = pAdapter->createDevice(pInstance, pAdapter->features());

      require(diagnostics().getTotalFallbackCount() == 0 && diagnostics().getFailureCount() == 0,
              "Allocator regressions require a fresh process without initialization failures");

      const VkPhysicalDeviceMemoryProperties properties = pAdapter->memoryProperties();
      uint32_t deviceLocalTypeId = properties.memoryTypeCount;
      uint32_t systemTypeId = properties.memoryTypeCount;
      for (uint32_t typeIndex = 0; typeIndex < properties.memoryTypeCount; typeIndex++) {
        const VkMemoryType& type = properties.memoryTypes[typeIndex];
        const bool deviceLocalHeap = (properties.memoryHeaps[type.heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        if (deviceLocalTypeId == properties.memoryTypeCount && deviceLocalHeap
            && (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
          deviceLocalTypeId = typeIndex;
        }
        if (systemTypeId == properties.memoryTypeCount && !deviceLocalHeap
            && (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
          systemTypeId = typeIndex;
        }
      }
      if (deviceLocalTypeId == properties.memoryTypeCount || systemTypeId == properties.memoryTypeCount) {
        std::cout << "SKIPPED: The adapter has no separate device-local and system memory types." << std::endl;
        return 77;
      }

      using FaultInjection = DxvkMemoryDiagnostics::FaultInjection;
      const RtxOptionLayer* pLayer = RtxOptionLayer::getEnvironmentLayer();
      FaultInjection::failCategoryMaskObject().setImmediately(0, pLayer);
      FaultInjection::deviceLocalHeapLimitMBObject().setImmediately(8, pLayer);
      FaultInjection::simulateDriverFailureObject().setImmediately(simulateDriverFailure, pLayer);

      DxvkMemoryAllocator allocator(pDevice.ptr());
      allocator.getMemoryHeaps()[properties.memoryTypes[deviceLocalTypeId].heapIndex].budget = 0;

      // The allocation is larger than the injected limit, so every device-local chunk size is refused.
      VkMemoryRequirements requirements = {};
      requirements.size = VkDeviceSize(32) << 20;
      requirements.alignment = 256;
      requirements.memoryTypeBits = (1u << deviceLocalTypeId) | (1u << systemTypeId);
      VkMemoryDedicatedRequirements dedicatedRequirements = {};
      dedicatedRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
      VkMemoryDedicatedAllocateInfo dedicatedInfo = {};
      dedicatedInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;

      const int64_t attemptTimeMs = DxvkMemoryDiagnostics::nowMs();
      DxvkMemory memory = allocator.alloc(&requirements, dedicatedRequirements, dedicatedInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                          DxvkMemoryFlags(), DxvkMemoryStats::Category::AppBuffer);
      require(memory && !(memory.propertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "The allocation did not fall back to system memory");
      require(memory.wasDemoted(), "The fallback was not marked as demoted");
      require(diagnostics().getFallbackCount(DxvkMemoryStats::Category::AppBuffer) == 1, "The fallback was not counted");
      require(diagnostics().formatSummary().find("Video memory exhausted: yes") != std::string::npos,
              "The memory summary omits the exhaustion that caused the fallback");

      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(attemptTimeMs + 8000);
      require(snapshot.videoMemoryExhausted(), "Device loss eight seconds after the fallback was not classified as recent");
      return 0;
    }

    // A host-only allocation failure must be recorded, but must not count as video memory running out.
    void testHostFailureIsNotVideoMemoryExhaustion() {
      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.lastDriverResult = VK_ERROR_OUT_OF_HOST_MEMORY;

      diagnostics().recordFailure(DxvkMemoryStats::Category::AppBuffer, 1 << 20,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                  evidence, false, emptyReport);

      const int64_t detectionTimeMs = DxvkMemoryDiagnostics::nowMs() + 1000;
      require(!diagnostics().hadVideoMemoryExhaustion(detectionTimeMs), "A host-only failure counted as video memory exhaustion");

      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(detectionTimeMs);
      require(!snapshot.videoMemoryExhausted(), "A GPU crash after a host-only failure was classified as video memory exhaustion");

      require(diagnostics().getFailureCount() == 1, "The host-only failure was not counted");

      const std::string summary = diagnostics().formatSummary();
      require(summary.find("VK_ERROR_OUT_OF_HOST_MEMORY") != std::string::npos, "The memory summary omits the host failure's result");
      require(summary.find("Video memory exhausted: no") != std::string::npos, "The memory summary reports video memory exhaustion");
    }

    void testMappingFailureIsNotVideoMemoryExhaustion() {
      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.mapFailed = true;

      diagnostics().recordFailure(DxvkMemoryStats::Category::AppBuffer, 1 << 20,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                  evidence, false, emptyReport);

      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(DxvkMemoryDiagnostics::nowMs() + 1000);
      require(!snapshot.videoMemoryExhausted(), "A mapping failure counted as video memory exhaustion");
      require(diagnostics().getFailureCount() == 2, "The mapping failure was not counted");
      require(diagnostics().getDriverFailureCount() == 0, "A mapping failure counted as a driver allocation failure");
      require(diagnostics().formatSummary().find("Mapping host-visible memory failed") != std::string::npos,
              "The memory summary omits the mapping failure");
    }

    void testOutcomePreservesExhaustionTime() {
      constexpr int64_t kExhaustionTimeMs = 1000;
      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.videoMemoryExhausted = true;
      diagnostics().recordVideoMemoryExhaustion(kExhaustionTimeMs);

      diagnostics().recordFallback(DxvkMemoryStats::Category::AppBuffer, 1 << 20, evidence, emptyReport);
      require(diagnostics().hadVideoMemoryExhaustion(kExhaustionTimeMs), "A fallback refreshed the attempt timestamp");
      require(!diagnostics().hadVideoMemoryExhaustion(kExhaustionTimeMs + 10000), "A fallback extended the exhaustion window");

      diagnostics().recordFailure(DxvkMemoryStats::Category::AppBuffer, 1 << 20,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, evidence, false, emptyReport);
      require(diagnostics().hadVideoMemoryExhaustion(kExhaustionTimeMs), "A terminal failure refreshed the attempt timestamp");
      require(!diagnostics().hadVideoMemoryExhaustion(kExhaustionTimeMs + 10000), "A terminal failure extended the exhaustion window");
      require(diagnostics().getTotalFallbackCount() == 1, "The fallback was not counted once");
      require(diagnostics().getFailureCount() == 1, "The terminal failure was not counted once");
    }

    void testExhaustionTelemetryAfterFallback() {
      std::unordered_map<std::string, std::string> tags;
      std::string memoryContext;
      uint32_t exhaustionPublicationCount = 0;
      DxvkMemoryDiagnostics memoryDiagnostics(
        [&](const std::string& key, const std::string& value) {
          tags[key] = value;
          if (key == "memory_vram_exhausted") {
            exhaustionPublicationCount++;
          }
        },
        [&](const std::string& key, const std::string& value) {
          require(key == "memory", "Diagnostics published an unexpected context");
          memoryContext = value;
        });

      uint32_t reportCount = 0;
      const auto formatReport = [&] {
        reportCount++;
        return memoryDiagnostics.formatSummary();
      };

      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.budgetRefused = true;
      uint32_t warningCount = memoryDiagnostics.recordFallback(DxvkMemoryStats::Category::AppBuffer, 1 << 20, evidence, formatReport);
      require(tags.at("memory_vram_exhausted") == "false", "A budget-only fallback published exhaustion");
      require(tags.at("memory_fallback") == "true", "The fallback tag was not published");
      require(tags.at("memory_alloc_failed") == "false", "A fallback published a terminal allocation failure");
      const std::string firstReport = memoryContext;

      constexpr int64_t kExhaustionTimeMs = 1000;
      memoryDiagnostics.recordVideoMemoryExhaustion(kExhaustionTimeMs);
      require(tags.at("memory_vram_exhausted") == "true", "The first exhaustion transition did not update the tag");
      evidence.videoMemoryExhausted = true;
      evidence.injectedLimitReached = true;
      warningCount += memoryDiagnostics.recordFallback(DxvkMemoryStats::Category::AppBuffer, 1 << 20, evidence, formatReport);
      require(tags.at("memory_vram_exhausted") == "true", "A subsequent fallback left the exhaustion tag stale");

      memoryDiagnostics.recordVideoMemoryExhaustion(kExhaustionTimeMs + 1);
      warningCount += memoryDiagnostics.recordFallback(DxvkMemoryStats::Category::AppBuffer, 1 << 20, evidence, formatReport);
      require(exhaustionPublicationCount == 2, "Repeated exhaustion republished the tag instead of only the first transition");
      require(warningCount == 1, "Fallbacks in one category did not emit exactly one warning");
      require(reportCount == 1, "Repeated fallbacks formatted additional memory reports");
      require(memoryContext == firstReport, "The transition unexpectedly replaced the full memory report");
      require(memoryDiagnostics.getTotalFallbackCount() == 3, "Repeated fallbacks were not all counted");
      require(memoryDiagnostics.getFailureCount() == 0, "Fallbacks counted as terminal allocation failures");

      require(memoryDiagnostics.hadVideoMemoryExhaustion(kExhaustionTimeMs + 8000), "Exhaustion was not recent at detection");
      require(!memoryDiagnostics.hadVideoMemoryExhaustion(kExhaustionTimeMs + 11000), "Exhaustion did not expire during the simulated wait");
      tags["memory_fallback"] = "false";
      tags["memory_alloc_failed"] = "true";
      tags["memory_vram_exhausted"] = "false";
      tags["memory_vram_exhausted_recent"] = "true";
      const std::string crashReport = memoryDiagnostics.formatSummary();
      memoryDiagnostics.publishToCrashReporting(crashReport);
      require(tags.at("memory_fallback") == "true", "Crash publication did not refresh the fallback tag");
      require(tags.at("memory_alloc_failed") == "false", "Crash publication did not refresh the allocation failure tag");
      require(tags.at("memory_vram_exhausted") == "true", "Crash publication did not refresh cumulative exhaustion");
      require(tags.at("memory_vram_exhausted_recent") == "true", "Crash publication replaced the detection-time classification");
      require(memoryContext == crashReport, "Crash publication did not refresh the memory context");

      tags["memory_vram_exhausted_recent"] = "false";
      memoryDiagnostics.publishToCrashReporting(crashReport);
      require(tags.at("memory_vram_exhausted_recent") == "false", "Cumulative exhaustion overrode a negative detection-time classification");
    }

    // A fallback refused only by a heap's own soft budget is counted, but is not video memory running out.
    void testBudgetFallbackIsNotVideoMemoryExhaustion() {
      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.budgetRefused = true;

      diagnostics().recordFallback(DxvkMemoryStats::Category::AppBuffer, 1 << 16, evidence, emptyReport);

      require(diagnostics().getFallbackCount(DxvkMemoryStats::Category::AppBuffer) == 1, "The fallback was not counted");
      require(!diagnostics().hadVideoMemoryExhaustion(DxvkMemoryDiagnostics::nowMs()), "A budget fallback counted as video memory exhaustion");
    }

    // Pressure at t=0, device loss detected at t=8 s, and Aftermath finishing at t=11 s must still classify as recent,
    // whichever queue thread reaches crash reporting.
    void testSnapshotTakenAtDetection() {
      const int64_t pressureTimeMs = DxvkMemoryDiagnostics::nowMs();

      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.videoMemoryExhausted = true;
      evidence.injectedLimitReached = true;
      diagnostics().recordVideoMemoryExhaustion(pressureTimeMs);
      diagnostics().recordFallback(DxvkMemoryStats::Category::RTXAccelerationStructure, 1 << 20, evidence, emptyReport);

      const int64_t detectionTimeMs = pressureTimeMs + 8000;
      const int64_t aftermathDoneTimeMs = pressureTimeMs + 11000;

      // Classifying when Aftermath finishes is the bug the snapshot avoids.
      require(!diagnostics().hadVideoMemoryExhaustion(aftermathDoneTimeMs), "The 10 s window did not expire as expected");

      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(detectionTimeMs);
      require(snapshot.videoMemoryExhausted(), "Pressure 8 s before detection was not classified as recent");

      // The other queue thread reaches crash reporting after the Aftermath wait. It must see the detection-time result.
      bool otherThreadResult = false;
      std::thread otherThread([&] {
        snapshot.capture(aftermathDoneTimeMs);
        otherThreadResult = snapshot.videoMemoryExhausted();
      });
      otherThread.join();
      require(otherThreadResult, "A later capture on another thread replaced the detection-time result");
    }

    // Threads that capture concurrently, at different times, must all see the same result.
    void testConcurrentCapturesAgree() {
      const int64_t baseTimeMs = DxvkMemoryDiagnostics::nowMs();

      for (int iteration = 0; iteration < 50; iteration++) {
        DxvkGpuCrashMemorySnapshot snapshot;
        std::vector<std::thread> threads;
        std::vector<int> results(8, -1);

        for (int i = 0; i < 8; i++) {
          // Half of the threads capture inside the window and half outside, so the first capture decides the result.
          const int64_t timeMs = baseTimeMs + (i % 2 ? 1000 : 20000);
          threads.emplace_back([&snapshot, &results, i, timeMs] {
            snapshot.capture(timeMs);
            results[i] = snapshot.videoMemoryExhausted() ? 1 : 0;
          });
        }

        for (std::thread& thread : threads) {
          thread.join();
        }

        for (int result : results) {
          require(result == results[0], "Concurrent captures returned different results");
        }
      }
    }

    // Exhaustion recorded after device loss was detected, such as during the Aftermath wait, must not count.
    // Must run before anything else marks video memory as exhausted.
    void testExhaustionAfterDetectionIgnored() {
      DxvkGpuCrashMemorySnapshot snapshot;
      snapshot.capture(DxvkMemoryDiagnostics::nowMs());
      require(!snapshot.videoMemoryExhausted(), "Video memory was exhausted before any exhaustion was recorded");

      DxvkMemoryDiagnostics::AllocationEvidence evidence;
      evidence.videoMemoryExhausted = true;
      diagnostics().recordVideoMemoryExhaustion(DxvkMemoryDiagnostics::nowMs());
      diagnostics().recordFallback(DxvkMemoryStats::Category::RTXRenderTarget, 1 << 20, evidence, emptyReport);

      // The other queue thread reaches crash reporting after the exhaustion was recorded.
      bool otherThreadResult = true;
      std::thread otherThread([&] {
        snapshot.capture(DxvkMemoryDiagnostics::nowMs());
        otherThreadResult = snapshot.videoMemoryExhausted();
      });
      otherThread.join();
      require(!otherThreadResult, "Exhaustion recorded after detection was classified as preceding the crash");
    }

    // Repeated driver failures, as during sustained fallback, are logged at most once per interval but all counted.
    void testDriverFailureLoggingIsBounded() {
      const uint32_t initialCount = diagnostics().getDriverFailureCount();
      const int64_t startTimeMs = 1000;
      uint32_t loggedCount = 0;

      for (int i = 0; i < 1000; i++) {
        if (diagnostics().recordDriverFailure(1, 0, 1 << 20, VK_ERROR_OUT_OF_DEVICE_MEMORY, startTimeMs + i)) {
          loggedCount++;
        }
      }

      require(loggedCount == 1, "Driver failures within one interval were logged more than once");

      require(diagnostics().recordDriverFailure(1, 0, 1 << 20, VK_ERROR_OUT_OF_DEVICE_MEMORY, startTimeMs + 10000),
              "A driver failure after the interval was not logged");
      require(diagnostics().getDriverFailureCount() == initialCount + 1001, "Driver failures were not all counted");

      const std::string summary = diagnostics().formatSummary();
      require(summary.find("vkAllocateMemory failures: 1001") != std::string::npos, "The memory summary omits the driver failure count");
    }
  }
}

int main(int argc, char** ppArgv) {
  try {
    if (argc == 3 && std::string(ppArgv[1]) == "--allocator-retry") {
      return dxvk::testAllocatorRetry(ppArgv[2]);
    }
    if (argc == 2 && std::string(ppArgv[1]) == "--allocator-fallback") {
      return dxvk::testAllocatorFallback(false);
    }
    if (argc == 2 && std::string(ppArgv[1]) == "--allocator-fallback-driver") {
      return dxvk::testAllocatorFallback(true);
    }
    if (argc == 2 && std::string(ppArgv[1]) == "--outcome-timestamp") {
      dxvk::testOutcomePreservesExhaustionTime();
      return 0;
    }
    if (argc == 2 && std::string(ppArgv[1]) == "--exhaustion-telemetry") {
      dxvk::testExhaustionTelemetryAfterFallback();
      return 0;
    }
    dxvk::require(argc == 1, "Unknown test arguments");

    // The negative cases run first, before anything marks video memory as exhausted.
    dxvk::testHostFailureIsNotVideoMemoryExhaustion();
    dxvk::testMappingFailureIsNotVideoMemoryExhaustion();
    dxvk::testBudgetFallbackIsNotVideoMemoryExhaustion();
    dxvk::testExhaustionAfterDetectionIgnored();
    dxvk::testSnapshotTakenAtDetection();
    dxvk::testConcurrentCapturesAgree();
    dxvk::testDriverFailureLoggingIsBounded();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED with exception: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All memory diagnostics tests passed." << std::endl;
  return 0;
}
