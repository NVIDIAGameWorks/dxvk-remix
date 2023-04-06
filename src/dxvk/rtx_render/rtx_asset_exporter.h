/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/sync/sync_signal.h"
#include "../util/rc/util_rc_ptr.h"
#include <atomic>
#include <future>
#include <mutex>


namespace dxvk {
  /// Filename can be KTX or DDS files
  class RtxContext;
  class DxvkDevice;
  class DxvkImage;
  class DxvkBuffer;
  class DxvkBufferSlice;

  class AssetExporter {
    Rc<sync::Fence> m_readbackSignal = nullptr;
    std::atomic<uint64_t> m_signalValue = 1;
    dxvk::mutex m_readbackSignalMutex;
    std::atomic<uint64_t> m_numExportsInFlight = 0;

  public:
    void waitForAllExportsToComplete(const float numSecsToWait = 10);
    void exportImage(Rc<DxvkDevice> device, Rc<RtxContext> ctx, const std::string& filename, Rc<DxvkImage> image, bool thumbnail = false);
    using BufferCallback = std::function<void(Rc<DxvkBuffer>)>;
    void exportBuffer(Rc<DxvkDevice> device, Rc<RtxContext> ctx, const DxvkBufferSlice& buffer, BufferCallback bufferCallback);
  };
} // namespace dxvk