/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software and to permit persons to whom the
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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dxvk {

  // A shader with in-flight work at the time of the crash, not necessarily the one that caused it.
  struct AftermathActiveShaderInfo {
    std::string type; // e.g. "fragment", "compute"
    std::string name; // as passed to registerAftermathShader()
  };

  // The specific GPU resource a page fault touched, when Aftermath was able to identify one.
  struct AftermathPageFaultResourceInfo {
    uint64_t gpuVa = 0;
    uint64_t size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t mipLevels = 0;
    uint32_t format = 0; // VkFormat
    bool isBufferHeap = false;
    bool isStaticTextureHeap = false;
    bool isRenderTargetOrDepthStencilViewHeap = false;
    bool isPlacedResource = false;
    bool wasDestroyed = false; // Set when this looks like a use-after-free.
    uint32_t createDestroyTickCount = 0;
  };

  struct AftermathCrashInfo {
    // Crash classification, e.g. "PageFault", "Timeout" (see describeAftermathDeviceStatus()).
    // Excludes per-crash specifics so crashes sharing a root cause group into one Sentry issue.
    std::string reason;

    bool adapterReset = false;
    bool engineReset = false;

    // The following are only set when reason is a page fault; empty/default otherwise.
    std::string pageFaultAccessType; // "read", "write", or "atomic"
    std::string pageFaultType;       // "address translation error" or "illegal access"
    std::string pageFaultEngine;     // e.g. "graphics", "copy engine"
    std::string pageFaultClient;     // hardware unit, e.g. "texture processing cluster"
    uint64_t pageFaultingGpuVA = 0;
    std::vector<AftermathPageFaultResourceInfo> pageFaultResourceInfo;

    // Deduplicated, in Aftermath's own order, which carries no meaning. GPU work is pipelined, so
    // most of these are unrelated to the crash.
    std::vector<AftermathActiveShaderInfo> activeShaders;

    // Shaders with no registry entry.
    uint32_t unregisteredShaderCount = 0;
    uint32_t driverInternalShaderCount = 0;
  };

  // Disabled by default so builds without Aftermath skip the per-shader hashing cost. Call from
  // DxvkInstance once GFSDK_Aftermath_EnableGpuCrashDumps succeeds.
  void setAftermathShaderRegistrationEnabled(bool enabled);

  // Maps a shader module's Aftermath hash to debugName, so a crash dump can name it. Call once per
  // module, right after creating it. No-op until setAftermathShaderRegistrationEnabled(true).
  //
  // Coverage is not total: everything via DxvkShaderModule (graphics, compute and ray tracing) and
  // NRDContext::createPipeline register. The DXVK meta shaders (blit, clear, copy, pack, resolve)
  // call vkCreateShaderModule directly, so they only reach unregisteredShaderCount.
  void registerAftermathShader(const uint32_t* spirvCode, size_t spirvSizeBytes, const std::string& debugName);

  // Classifies a raw crash dump without needing shader debug symbols.
  AftermathCrashInfo decodeAftermathCrashInfo(const void* pGpuCrashDump, uint32_t gpuCrashDumpSize);

}
