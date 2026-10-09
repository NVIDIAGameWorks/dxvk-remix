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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rtx_graphics_preset.h"
#include "../dxvk_adapter.h"
#include "../dxvk_device_info.h"

namespace dxvk {

  enum class DLSSProfile : uint32_t;

  // Adapter facts used to choose automatic quality defaults. See documentation/RemixConfig.md, "RTX Spark".
  struct GpuHardwareInfo {
    uint32_t vendorId = 0;
    bool isIntegrated = false;
    // These are the NVAPI facts of the GPU that matches the Vulkan adapter LUID.
    // They are only queried for NVIDIA integrated GPUs.
    bool nvapiIntegrated = false;
    std::optional<uint32_t> nvapiArchitecture;
    // Logged for diagnostics only, because the core count differs between RTX Spark SKUs.
    std::optional<uint32_t> nvapiCoreCount;
    // Holds the size of the heap of the first memory type whose flags are exactly DEVICE_LOCAL.
    // Holds 0 if there is no such memory type.
    uint64_t exactDeviceLocalTypeHeapSize = 0;
    uint64_t deviceLocalHeapTotal = 0;
  };

  struct AutoPresetDecision {
    GraphicsPreset preset = GraphicsPreset::Low;
    bool lowMemoryGpu = false;
    bool applyNonNvidiaDefaults = false;
    std::vector<std::string> log;
  };

  constexpr GraphicsPreset kRtxSparkGraphicsPreset = GraphicsPreset::Medium;
  constexpr uint64_t kLowMemoryGpuThreshold = 8ull * 1024 * 1024 * 1024;

  bool isRtxSpark(const GpuHardwareInfo& info);

  // Returns why an NVIDIA integrated GPU is not RTX Spark.
  // Returns nullptr when it is RTX Spark.
  const char* describeRtxSparkMismatch(const GpuHardwareInfo& info);

  // nvidiaArch is only read for NVIDIA GPUs without an override that are not RTX Spark.
  AutoPresetDecision selectAutoPreset(const GpuHardwareInfo& info, std::optional<GraphicsPreset> gpuOverride, uint32_t nvidiaArch);

  std::optional<DLSSProfile> selectAutoDlssMode(std::optional<DLSSProfile> gpuOverride, bool isRtxSpark);

  class GpuProfile {
  public:
    // Collects adapter and NVAPI facts once during device initialization and logs each step.
    static void detect(const DxvkDeviceInfo& deviceInfo,
                       const VkPhysicalDeviceMemoryProperties& memoryProperties,
                       const DxvkAdapterMemoryInfo& memoryInfo);
    static const GpuHardwareInfo& getHardwareInfo();
    static bool isRtxSpark();
  };

}
