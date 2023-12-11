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

#include "../util/config/config.h"

namespace dxvk {

  struct DxvkOptions {
    DxvkOptions() { }
    DxvkOptions(const Config& config);

    /// Enable state cache
    bool enableStateCache;

    /// Number of compiler threads
    /// when using the state cache
    int32_t numCompilerThreads;

    /// Shader-related options
    Tristate useRawSsbo;

    /// Workaround for NVIDIA driver bug 3114283
    Tristate shrinkNvidiaHvvHeap;

    /// HUD elements
    std::string hud;

    /// Throttle presents, causes a fixed CPU delay after present if >0 (value in ms)
    int32_t presentThrottleDelay = 0;

    // NV-DXVK start: Integrate Aftermath
    bool enableAftermath;
    bool enableAftermathResourceTracking;
    // NV-DXVK end

    // NV-DXVK start: early submit heuristics for memcpy work
    uint32_t memcpyKickoffThreshold;
    // NV-DXVK end

    // NV-DXVK start: tell the user they cant run Remix
    uint32_t nvidiaMinDriver;
    uint32_t nvidiaLinuxMinDriver;
    // NV-DXVK end

    // NV-DXVK start: configurable memory allocation chunk sizes
    uint32_t deviceLocalMemoryChunkSizeMB;
    uint32_t otherMemoryChunkSizeMB;
    // NV-DXVK end
  };

}
