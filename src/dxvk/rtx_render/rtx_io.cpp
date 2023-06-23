/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_io.h"
#include "../../util/log/log.h"

#ifdef WITH_RTXIO

#include <rtxio/rtxioVulkan.h>

namespace dxvk {
  RtxIoExtensionProvider RtxIoExtensionProvider::s_instance;

  DxvkNameSet RtxIoExtensionProvider::getInstanceExtensions() {
    DxvkNameSet nameSet;
    uint32_t extensionCount;

    // Query the maximum number of extensions for RTX IO
    if (auto result = rtxioVulkanGetInstanceExtensions(nullptr,
                                                       &extensionCount)) {
      Logger::err(str::format("RTX IO instance extensions count query failed "
                              "with ", result));
      return nameSet;
    }

    // Allocate extension ptrs storage on stack
    const char** extensions =
      reinterpret_cast<const char**>(alloca(extensionCount * sizeof(char*)));

    // Query the extensions required for RTX IO
    if (auto result = rtxioVulkanGetInstanceExtensions(extensions,
                                                       &extensionCount)) {
      Logger::err(str::format("RTX IO instance extensions query failed "
                              "with ", result));
      return nameSet;
    }

    for (uint32_t n = 0; n < extensionCount; ++n) {
      nameSet.add(extensions[n]);
    }

    return nameSet;
  }

  VkPhysicalDevice RtxIoExtensionProvider::getPhysicalDevice(uint32_t adapterId) {
    if (m_vkInstance) {
      auto adapter = m_vkInstance->enumAdapters(adapterId);
      if (adapter != nullptr) {
        return adapter->handle();
      }
    }
    return VK_NULL_HANDLE;
  }

  DxvkNameSet RtxIoExtensionProvider::getDeviceExtensions(uint32_t adapterId) {
    DxvkNameSet nameSet;

    auto physicalDevice = getPhysicalDevice(adapterId);

    if (physicalDevice == VK_NULL_HANDLE) {
      return nameSet;
    }

    auto instance = const_cast<DxvkInstance*>(m_vkInstance)->handle();

    uint32_t extensionCount;
    // Query the maximum number of extensions for RTX IO
    if (auto result = rtxioVulkanGetDeviceExtensions(instance, physicalDevice,
                                                     nullptr, &extensionCount)) {
      Logger::err(str::format("RTX IO device extensions count query failed "
                              "with ", result));
      return nameSet;
    }

    // Allocate extension ptrs storage on stack
    const char** extensions =
      reinterpret_cast<const char**>(alloca(extensionCount * sizeof(char*)));

    // Query the extensions required for RTX IO
    if (auto result = rtxioVulkanGetDeviceExtensions(instance, physicalDevice,
                                                     extensions,
                                                     &extensionCount)) {
      Logger::err(str::format("RTX IO device extensions query failed "
                              "with ", result));
      return nameSet;
    }

    for (uint32_t n = 0; n < extensionCount; ++n) {
      nameSet.add(extensions[n]);
    }

    return nameSet;
  }

  bool RtxIoExtensionProvider::getDeviceFeatures(VkPhysicalDevice device,
                                                 DxvkDeviceFeatures& features) {
    if (device == VK_NULL_HANDLE) {
      return false;
    }

    auto instance = const_cast<DxvkInstance*>(m_vkInstance)->handle();

    // Patch the dxvk structures to make them right
    features.vulkan12Features.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.core.pNext = &features.vulkan12Features;

    // Query the features required for RTX IO
    if (auto result = rtxioVulkanGetPhysicalDeviceFeatures(instance,
                                                           device,
                                                           &features.core)) {
      Logger::err(str::format("RTX IO physical device features query failed "
                              "with ", result));
      return false;
    }

    return true;
  }

  void RtxIo::updateMemoryStats(int64_t memAdjustmentMB) {
    if (m_device == nullptr) {
      return;
    }

    const auto category = DxvkMemoryStats::Category::RTXBuffer;
    const int64_t size = abs(memAdjustmentMB * 1024 * 1024) / 2;

    auto& heaps = m_device->getCommon()->memoryManager().getMemoryHeaps();

    // Update device local memory stats
    for (auto& heap : heaps) {
      if (0 != (heap.properties.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
        if (memAdjustmentMB > 0) {
          heap.stats.trackMemoryAssigned(category, size);
        } else {
          heap.stats.trackMemoryReleased(category, size);
        }
        break;
      }
    }

    // Update host memory stats
    for (auto& heap : heaps) {
      if (0 == (heap.properties.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) {
        if (memAdjustmentMB > 0) {
          heap.stats.trackMemoryAssigned(category, size);
        } else {
          heap.stats.trackMemoryReleased(category, size);
        }
        break;
      }
    }
  }
  
  void RtxIo::OnEvent(uint32_t event, const void* data, void* userData) {
    auto rtxio = static_cast<RtxIo*>(userData);

    switch (event) {
    case RTXIO_VK_WORK_QUEUE_ACCESS_BEGIN:
      rtxio->lockQueue(rtxio->m_workQueue);
      break;
    case RTXIO_VK_WORK_QUEUE_ACCESS_END:
      rtxio->unlockQueue(rtxio->m_workQueue);
      break;
    case RTXIO_VK_TRANSFER_QUEUE_ACCESS_BEGIN:
      rtxio->lockQueue(rtxio->m_transferQueue);
      break;
    case RTXIO_VK_TRANSFER_QUEUE_ACCESS_END:
      rtxio->unlockQueue(rtxio->m_transferQueue);
      break;
    }
  }

  void RtxIo::lockQueue(const DxvkDeviceQueue& queue) {
    if (m_device == nullptr) {
      return;
    }

    // TODO(iterentiev): at the moment using a global lock via lockSubmission()
    // which drains the submission queue. We may avoid that with finer grained
    // access to the queues and QueueGuard() class.
    if (!m_isQueueDedicated) {
      m_device->lockSubmission();
    }
  }

  void RtxIo::unlockQueue(const DxvkDeviceQueue& queue) {
    if (m_device == nullptr) {
      return;
    }

    if (!m_isQueueDedicated) {
      m_device->unlockSubmission();
    }
  }

  bool RtxIo::initialize(DxvkDevice* device) {
    const auto& queues = device->queues();

    if (useAsyncQueue() && queues.asyncCompute.queueHandle != VK_NULL_HANDLE) {
      // Use async compute queue where possible
      m_workQueue = queues.asyncCompute;
      m_isQueueDedicated = true;
    } else {
      // Run on graphics queue otherwise
      m_workQueue = queues.graphics;
      m_isQueueDedicated = false;
    }

    RTXIOVkDevice vkDevice;
    vkDevice.instance = device->instance()->handle();
    vkDevice.physicalDevice = device->adapter()->handle();
    vkDevice.device = device->handle();
    vkDevice.workQueueFamily = m_workQueue.queueFamily;
    vkDevice.workQueueIdx = m_workQueue.queueIndex;
    vkDevice.transferQueueFamily = RTXIO_VK_NO_QUEUE;

    RTXIOInstanceDesc instanceDesc{ RTXIO_VERSION };
    instanceDesc.flags = RTXIO_VULKAN + RTXIO_SCHEDULE_BULK;
    instanceDesc.memoryBudget = memoryBudgetMB() * 1024 * 1024;
    instanceDesc.queueCapacity = 2048;
    instanceDesc.shareDevice = &vkDevice;

    if (!m_isQueueDedicated) {
      instanceDesc.flags += RTXIO_VULKAN_SHARED_QUEUE;
      instanceDesc.onEvent = OnEvent;
      instanceDesc.eventUserData = this;
    }

    if (auto result = rtxioCreate(&instanceDesc, &m_rtxio)) {
      throw DxvkError(str::format("RTX IO creation failed with: ", result));
    }

    m_device = device;

    updateMemoryStats(memoryBudgetMB());

    return true;
  }

  void RtxIo::release() {
    if (m_rtxio == nullptr) {
      return;
    }

    if (auto result = rtxioRelease(m_rtxio)) {
      Logger::err(str::format("RTX IO release failed with: ", result));
    }

    m_rtxio = nullptr;
    updateMemoryStats(-static_cast<int64_t>(memoryBudgetMB()));

    m_device = nullptr;
  }

  bool RtxIo::openFile(const char* filename, Handle* handle) {
    std::lock_guard<dxvk::mutex> _(m_flushMutex);

    if (auto result = rtxioOpenFile(m_rtxio, filename, handle)) {
      Logger::err(str::format("RTX IO open file ", filename, " failed with ",
                              result));
      return false;
    }
    return true;
  }

  bool RtxIo::closeFile(const Handle handle) {
    if (auto result = rtxioCloseFile(m_rtxio, handle)) {
      Logger::err(str::format("RTX IO close file failed with ", result));
      return false;
    }
    return true;
  }

  bool RtxIo::enqueueWait(const Rc<RtxSemaphore>& sema, uint64_t value) {
    auto vkSema = sema->handle();
    if (auto result = rtxioEnqueueWait(m_rtxio, RTXIO_WAIT_BEFORE_COPY,
                                       &vkSema, value)) {
      Logger::err(str::format("RTX IO wait enqueue failed with ", result));
      return false;
    }
    return true;
  }

  uint64_t RtxIo::enqueueRead(const ImageDest& dst, const FileSource& src) {
    assert(dst.image != nullptr && "Image is a nullptr");
    assert(dst.image->handle() != VK_NULL_HANDLE && "Image handle is null");

    RTXIOUpdateRequest req;

    req.source.type = RTXIO_SRC_FILE;
    req.source.compression = src.isCompressed ?
      RTXIO_COMPRESSION_GDEFLATE_1_0 : RTXIO_COMPRESSION_NONE;
    req.source.flags = 0;
    req.source.encryptionContext = 0;
    req.source.file = src.file;
    req.source.offset = src.offset;
    req.source.size = src.size;

    // Add slack to account for disk sector overread and alignment
    const size_t sizeWithSlack = src.size + 4096 + kRtxIoDataAlignment;

    const auto& imageInfo = dst.image->info();

    RTXIOVkImage vkImage;
    vkImage.image = dst.image->handle();
    vkImage.type = imageInfo.type;
    vkImage.format = imageInfo.format;
    vkImage.extent = imageInfo.extent;
    vkImage.mipLevels = imageInfo.mipLevels;
    vkImage.arrayLayers = imageInfo.numLayers;

    req.destination.type = RTXIO_DST_SUBRESOURCE_RANGE;
    req.destination.flags = RTXIO_DST_STATE_READ_OPTIMAL;
    req.destination.resource = &vkImage;
    req.destination.subresourceRange.first =
      rtxioVulkanGetSubresourceIndex(dst.startMip, dst.startSlice, dst.count);
    req.destination.subresourceRange.count = dst.count;

    // Although RTXIO enqueue API is thread-safe, it may interfere
    // with RTXIO flushes which at the moment are not guaranteed to happen
    // serially with the enqueues. Hold flush lock.
    std::lock_guard<dxvk::mutex> _(m_flushMutex);

    if (auto result = rtxioEnqueueUpdateRequests(m_rtxio, 0, 1, &req)) {
      Logger::err(str::format("RTX IO request enqueue failed with ", result));
      return 0;
    }

    uint64_t currentTopPt;
    rtxioGetTimelineValue(m_rtxio, RTXIO_PIPELINE_STAGE_TOP, &currentTopPt);

    m_sizeInFlight += sizeWithSlack;

    return currentTopPt + 1;
  }

  bool RtxIo::isComplete(uint64_t syncpt) const {
    uint64_t currentBottomPt;
    rtxioGetTimelineValue(m_rtxio, RTXIO_PIPELINE_STAGE_BOTTOM, &currentBottomPt);
    return currentBottomPt >= syncpt;
  }

  bool RtxIo::enqueueSignal(const Rc<RtxSemaphore>& sema, uint64_t value) {
    auto vkSema = sema->handle();
    if (auto result = rtxioEnqueueSignal(m_rtxio, &vkSema, value)) {
      Logger::err(str::format("RTX IO signal enqueue failed with ", result));
      return false;
    }
    return true;
  }

  bool RtxIo::flush(bool async) {
    ScopedCpuProfileZone();

    if (async) {
      const uint32_t framesSinceFlush = m_device->getCurrentFrameId() -
        m_lastFlushFrame;

      // Check if there's too little data and we're
      // dispatching it too fast.
      if (m_sizeInFlight == 0 || (m_sizeInFlight < kSmallBatchSize &&
          framesSinceFlush < kSmallBatchPeriod)) {
        return false;
      }
    }

    // RTXIO flush is not thread-safe and may happen from multiple
    // locations in dxvk environment. Hold a lock.
    std::lock_guard<dxvk::mutex> _(m_flushMutex);

    if (m_sizeInFlight == 0) {
      return true;
    }

    if (auto result = rtxioFlush(m_rtxio, !async)) {
      Logger::err(str::format("RTX IO flush failed with ", result));
      return false;
    }

#ifdef DEBUG
    Logger::info(str::format("RTXIO Dispatched: ", m_sizeInFlight));
#endif

    m_sizeInFlight = 0;
    m_lastFlushFrame = m_device->getCurrentFrameId();

#ifdef DEBUG
    dumpStats();
#endif

    return true;
  }

  void RtxIo::dumpStats() const {
    uint64_t uploadTimeUs, flushTimeUs, copyTimeUs, readbackTimeUs, cmdBuffTimeUs, ioTimeUs,
      executionTimeUs;

    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_UPLOAD_TIME_US, 0, &uploadTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_DECODE_TIME_US, 0, &executionTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_FLUSH_TIME_US, 0, &flushTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_COPY_TIME_US, 0, &copyTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_READBACK_TIME_US, 0, &readbackTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_CMD_BUFF_TIME_US, 0, &cmdBuffTimeUs);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_IO_TIME_US, 0, &ioTimeUs);

    uint64_t totalOutBytes, totalInBytes;
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_TOTAL_INPUT_BYTES, 0, &totalInBytes);
    rtxioGetCounter(m_rtxio, RTXIO_COUNTER_TOTAL_OUTPUT_BYTES, 0, &totalOutBytes);

    Logger::info(str::format("RTXIO In: ", totalInBytes, ", Out: ", totalOutBytes));

    Logger::info(str::format("GPU upload time ", uploadTimeUs, " us, upload throughput ",
                             static_cast<double>(totalInBytes) / uploadTimeUs, " MB/s"));
    Logger::info(str::format("GPU decode time ", executionTimeUs, " us, decode throughput ",
                             static_cast<double>(totalOutBytes) / executionTimeUs, " MB/s"));
    Logger::info(str::format("GPU copy time ", copyTimeUs, " us, copy throughput ",
                             static_cast<double>(totalOutBytes) / copyTimeUs, " MB/s"));

    Logger::info(str::format("IO time ", ioTimeUs));
    Logger::info(str::format("Flush time ", flushTimeUs));
    Logger::info(str::format("Cmd buffer build time ", cmdBuffTimeUs));
  }
}
#endif
