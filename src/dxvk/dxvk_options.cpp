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
#include "dxvk_options.h"
#include "vulkan/vulkan_core.h"

namespace dxvk {

  DxvkOptions::DxvkOptions(const Config& config) {
    enableStateCache      = config.getOption<bool>    ("dxvk.enableStateCache",       true);
    numCompilerThreads    = config.getOption<int32_t> ("dxvk.numCompilerThreads",     0);
    useRawSsbo            = config.getOption<Tristate>("dxvk.useRawSsbo",             Tristate::Auto);
    shrinkNvidiaHvvHeap   = config.getOption<Tristate>("dxvk.shrinkNvidiaHvvHeap",    Tristate::Auto);
    hud                   = config.getOption<std::string>("dxvk.hud", "");

    // NV-DXVK start: Integrate Aftermath
    enableAftermath = config.getOption<bool>("dxvk.enableAftermath", false);
    enableAftermathResourceTracking = config.getOption<bool>("dxvk.enableAftermathResourceTracking", false);
    // NV-DXVK end

    // NV-DXVK start: early submit heuristics for memcpy work
    memcpyKickoffThreshold = config.getOption<uint32_t>("dxvk.memcpyKickoffThreshold", 16 * 1024 * 1024);
    // NV-DXVK end

    // NV-DXVK start: tell the user they cant run Remix
    float nvidiaMinDriverFloat = config.getOption<float>("dxvk.nvidiaMinDriver", 536.67f);
    float nvidiaGfnMinDriverFloat = config.getOption<float>("dxvk.nvidiaGfnMinDriver", 527.01f);
    float nvidiaLinuxMinDriverFloat = config.getOption<float>("dxvk.nvidiaLinuxMinDriver", 525.60f);

    // Convert human readable version from settings to proper version number
    float major = 0;
    long minor = 0;
    // Desktop Windows
    minor = std::lround(std::modf(nvidiaMinDriverFloat, &major) * 100);
    nvidiaMinDriver = VK_MAKE_API_VERSION(0, major, minor, 0);    

    // Desktop Linux (via Proton)
    minor = std::lround(std::modf(nvidiaLinuxMinDriverFloat, &major) * 100);
    nvidiaLinuxMinDriver = VK_MAKE_API_VERSION(0, major, minor, 0);
    // NV-DXVK end
    
    // NV-DXVK start: configurable memory allocation chunk sizes
    deviceLocalMemoryChunkSizeMB = config.getOption<uint32_t>("dxvk.deviceLocalMemoryChunkSizeMB", 320);
    otherMemoryChunkSizeMB = config.getOption<uint32_t>("dxvk.otherMemoryChunkSizeMB", 128);
    // NV-DXVK end
  }

}
