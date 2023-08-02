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
#pragma once
#include "dxvk_device.h"
#include "dxvk_extension_provider.h"
#include "rtx_texture.h"
#include "rtx_semaphore.h"
#include "rtx_option.h"
#include "../util/util_singleton.h"

namespace dxvk {

  class DxvkInstance;

#ifdef WITH_RTXIO

  class RtxIoExtensionProvider : public DxvkExtensionProvider {
    const DxvkInstance* m_vkInstance = nullptr;

    VkPhysicalDevice getPhysicalDevice(uint32_t adapterId);

  public:

    std::string_view getName() {
      return "RTX IO";
    }

    DxvkNameSet getInstanceExtensions();
    DxvkNameSet getDeviceExtensions(uint32_t adapterId);
    bool getDeviceFeatures(VkPhysicalDevice device,
                           DxvkDeviceFeatures& features);

    void initInstanceExtensions() { }
    void initDeviceExtensions(const DxvkInstance* instance) {
      m_vkInstance = instance;
    }

    static RtxIoExtensionProvider s_instance;
  };

  class RtxIo : public Singleton<RtxIo> {
    // A "small" batch is a batch that has so little data
    // that it may show poor decoding throughput. We do
    // not want to dispatch these too frequently and would
    // prefer to accumulate more data if possible.
    // Calculated for 64 64KB tiles at ~1.66 ratio.
    static constexpr uint32_t kSmallBatchSize = ((64 * 65536) * 5) / 3;
    static constexpr uint32_t kSmallBatchPeriod = 10;
  public:
    typedef void* Handle;

    struct FileSource {
      Handle file;

      uint64_t offset;
      size_t size;

      bool isCompressed;
    };

    struct ImageDest {
      Rc<DxvkImage> image;
      uint16_t startSlice;
      uint16_t startMip;
      uint16_t count;
    };

    struct QueueGuard {
      explicit QueueGuard(const DxvkDeviceQueue& queue)
      : queue(queue) {
        RtxIo::get().lockQueue(queue);
      }

      ~QueueGuard() {
        RtxIo::get().unlockQueue(queue);
      }

      const DxvkDeviceQueue& queue;
    };

    bool initialize(DxvkDevice* device);
    void release();

    bool openFile(const char* filename, Handle* handle);
    bool closeFile(const Handle handle);

    bool enqueueWait(const Rc<RtxSemaphore>& sema, uint64_t value);
    uint64_t enqueueRead(const ImageDest& dst, const FileSource& src);
    bool enqueueSignal(const Rc<RtxSemaphore>& sema, uint64_t value);

    bool isComplete(uint64_t syncpt) const;

    bool flush(bool async);

    RTX_OPTION_ENV("rtx.io", bool, enabled, false, "DXVK_USE_RTXIO",
      "When this option is enabled the assets will be loaded (and optionally decompressed on GPU) using high "
      "performance RTX IO runtime. RTX IO must be enabled for loading compressed assets, but is not "
      "necessary for working with loose uncompressed assets.");

  private:
    static void OnEvent(uint32_t event, const void* data, void* userData);

    void lockQueue(const DxvkDeviceQueue& queue);
    void unlockQueue(const DxvkDeviceQueue& queue);

    void updateMemoryStats(int64_t memAdjustmentMB);
    void dumpStats() const;

    DxvkDevice* m_device;
    DxvkDeviceQueue m_workQueue;
    DxvkDeviceQueue m_transferQueue;
    dxvk::mutex m_flushMutex;

    bool m_isQueueDedicated = false;

    Handle m_rtxio;

    // TODO(iterentiev): implement real batching
    size_t m_sizeInFlight = 0;

    uint32_t m_lastFlushFrame = 0;

    RTX_OPTION("rtx.io", size_t, memoryBudgetMB, 256, "");
    RTX_OPTION("rtx.io", bool, useAsyncQueue, true, "");
    RTX_OPTION_ENV("rtx.io", bool, forceCpuDecoding, false, "DXVK_RTXIO_FORCE_CPU_DECODING",
      "Force CPU decoding in RTX IO.");
  };

#else

  class RtxIo {
  public:
    static inline bool enabled() {
      // Force disabled when not built with RTXIO
      return false;
    }
  };

#endif

}
