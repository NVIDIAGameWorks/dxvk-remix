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
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <nvapi.h>

#include "../../../src/dxvk/rtx_render/rtx_gpu_overrides.h"
#include "../../../src/dxvk/rtx_render/rtx_gpu_profile.h"

#include "../../test_utils.h"

namespace dxvk {
  // Logger needed by some shared code used in this Unit Test.
  Logger Logger::s_instance("test_gpu_profile.log");

namespace gpu_profile_test {

  #define TEST_ASSERT(condition, message) \
    do { \
      if (!(condition)) { \
        std::ostringstream oss; \
        oss << "FAILED: " << __FUNCTION__ << " line " << __LINE__ << ": " << message; \
        throw DxvkError(oss.str()); \
      } \
    } while(0)

  constexpr uint32_t kNvidia = 0x10DE;
  constexpr uint32_t kAmd = 0x1002;
  constexpr uint64_t kGiB = 1024ull * 1024 * 1024;

  GpuHardwareInfo discreteNvidia(uint64_t vram) {
    GpuHardwareInfo info;
    info.vendorId = kNvidia;
    info.exactDeviceLocalTypeHeapSize = vram;
    info.deviceLocalHeapTotal = vram;
    return info;
  }

  GpuHardwareInfo spark(uint64_t deviceLocal) {
    GpuHardwareInfo info;
    info.vendorId = kNvidia;
    info.isIntegrated = true;
    info.nvapiIntegrated = true;
    info.nvapiArchitecture = NV_GPU_ARCHITECTURE_GB200;
    info.nvapiCoreCount = 5120;
    info.deviceLocalHeapTotal = deviceLocal;
    return info;
  }

  bool logContains(const AutoPresetDecision& decision, const std::string& text) {
    for (const std::string& line : decision.log) {
      if (line.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  void test_sparkDetection() {
    TEST_ASSERT(isRtxSpark(spark(128 * kGiB)), "Spark");
    TEST_ASSERT(describeRtxSparkMismatch(spark(128 * kGiB)) == nullptr, "Spark has no mismatch");
    TEST_ASSERT(!isRtxSpark(discreteNvidia(16 * kGiB)), "discrete GPU");

    // Core count differs per SKU and is not part of detection.
    GpuHardwareInfo info = spark(128 * kGiB);
    info.nvapiCoreCount = 6144;
    TEST_ASSERT(isRtxSpark(info), "6144 cores");
    info.nvapiCoreCount.reset();
    TEST_ASSERT(isRtxSpark(info), "core count query failed");

    info = spark(128 * kGiB);
    info.nvapiIntegrated = false;
    info.nvapiArchitecture.reset();
    info.nvapiCoreCount.reset();
    TEST_ASSERT(!isRtxSpark(info), "NVAPI unavailable or no LUID match");
    TEST_ASSERT(std::string(describeRtxSparkMismatch(info)) == "NVAPI did not confirm an integrated GPU", "NVAPI unavailable reason");

    // Vulkan reports integrated but NVAPI reports a discrete GPU.
    info = spark(128 * kGiB);
    info.nvapiIntegrated = false;
    TEST_ASSERT(!isRtxSpark(info), "NVAPI reports a discrete GPU");

    info = spark(128 * kGiB);
    info.nvapiArchitecture.reset();
    TEST_ASSERT(!isRtxSpark(info), "architecture query failed");
    TEST_ASSERT(std::string(describeRtxSparkMismatch(info)) == "NVAPI architecture unknown", "unknown architecture reason");

    const uint32_t otherArchitectures[] = { NV_GPU_ARCHITECTURE_AD100, NV_GPU_ARCHITECTURE_GB200 + 0x10u, static_cast<uint32_t>(NV_GPU_ARCHITECTURE_T4X) };
    for (uint32_t architecture : otherArchitectures) {
      info = spark(128 * kGiB);
      info.nvapiArchitecture = architecture;
      TEST_ASSERT(!isRtxSpark(info), "architecture 0x" << std::hex << architecture);
      TEST_ASSERT(std::string(describeRtxSparkMismatch(info)) == "not a Blackwell GPU", "architecture reason");
    }

    // NVIDIA laptop GPUs, including Blackwell ones, are discrete.
    info = spark(16 * kGiB);
    info.isIntegrated = false;
    TEST_ASSERT(!isRtxSpark(info), "Vulkan reports a discrete GPU");

    info = spark(128 * kGiB);
    info.vendorId = kAmd;
    TEST_ASSERT(!isRtxSpark(info), "non-NVIDIA integrated GPU");

    // A carve-out layout (one device-local heap plus a host heap) is still RTX Spark.
    info = spark(8 * kGiB);
    info.exactDeviceLocalTypeHeapSize = 8 * kGiB;
    TEST_ASSERT(isRtxSpark(info), "carve-out layout");
  }

  // This is the memory layout from the first RTX Spark log.
  // Heap 0 is 49140 MiB device-local and heap 1 is 7543 MiB host memory.
  void test_sparkObservedLayout() {
    GpuHardwareInfo info = spark(49140ull * 1024 * 1024);
    info.exactDeviceLocalTypeHeapSize = 49140ull * 1024 * 1024;
    TEST_ASSERT(isRtxSpark(info), "observed Spark");
    const AutoPresetDecision decision = selectAutoPreset(info, std::nullopt, NV_GPU_ARCHITECTURE_GB200);
    TEST_ASSERT(decision.preset == GraphicsPreset::Medium && !decision.lowMemoryGpu, "observed Spark defaults");
    TEST_ASSERT(!logContains(decision, "Blackwell architecture detected"), "discrete ladder skipped");
  }

  void test_discretePresets() {
    struct Case { uint32_t arch; GraphicsPreset preset; const char* log; };
    const Case cases[] = {
      { NV_GPU_ARCHITECTURE_TU100 - 1, GraphicsPreset::Low, "without HW RTX support" },
      { NV_GPU_ARCHITECTURE_TU100, GraphicsPreset::Low, "Turing" },
      { NV_GPU_ARCHITECTURE_GA100 - 1, GraphicsPreset::Low, "Turing" },
      { NV_GPU_ARCHITECTURE_GA100, GraphicsPreset::Medium, "Ampere" },
      { NV_GPU_ARCHITECTURE_AD100, GraphicsPreset::High, "Ada" },
      { NV_GPU_ARCHITECTURE_GB200, GraphicsPreset::Ultra, "Blackwell" },
    };
    for (const Case& c : cases) {
      const AutoPresetDecision decision = selectAutoPreset(discreteNvidia(16 * kGiB), std::nullopt, c.arch);
      TEST_ASSERT(decision.preset == c.preset, "arch 0x" << std::hex << c.arch);
      TEST_ASSERT(logContains(decision, c.log), "log for arch 0x" << std::hex << c.arch);
      TEST_ASSERT(!decision.lowMemoryGpu && !decision.applyNonNvidiaDefaults, "large discrete GPU flags");
    }

    // The architecture query falls back to Turing when NVAPI fails.
    TEST_ASSERT(selectAutoPreset(discreteNvidia(16 * kGiB), std::nullopt, NV_GPU_ARCHITECTURE_TU100).preset == GraphicsPreset::Low, "Turing fallback");

    // 8 GiB threshold is inclusive and demotes by one step, never below Low and never above Medium.
    AutoPresetDecision decision = selectAutoPreset(discreteNvidia(kLowMemoryGpuThreshold), std::nullopt, NV_GPU_ARCHITECTURE_GB200);
    TEST_ASSERT(decision.preset == GraphicsPreset::Medium && decision.lowMemoryGpu, "Ultra at 8 GiB demotes to Medium");
    TEST_ASSERT(logContains(decision, "lowering quality setting"), "demotion log");
    decision = selectAutoPreset(discreteNvidia(kLowMemoryGpuThreshold + 1), std::nullopt, NV_GPU_ARCHITECTURE_GB200);
    TEST_ASSERT(decision.preset == GraphicsPreset::Ultra && !decision.lowMemoryGpu, "one byte over 8 GiB");
    decision = selectAutoPreset(discreteNvidia(8 * kGiB), std::nullopt, NV_GPU_ARCHITECTURE_AD100);
    TEST_ASSERT(decision.preset == GraphicsPreset::Medium, "Ada at 8 GiB demotes to Medium");
    decision = selectAutoPreset(discreteNvidia(8 * kGiB), std::nullopt, NV_GPU_ARCHITECTURE_TU100);
    TEST_ASSERT(decision.preset == GraphicsPreset::Low, "Turing at 8 GiB stays Low");

    // No memory type is exactly DEVICE_LOCAL, as in ReBAR-only layouts.
    // The legacy rule then sees 0 bytes.
    GpuHardwareInfo info = discreteNvidia(0);
    info.deviceLocalHeapTotal = 16 * kGiB;
    decision = selectAutoPreset(info, std::nullopt, NV_GPU_ARCHITECTURE_GB200);
    TEST_ASSERT(decision.lowMemoryGpu && decision.preset == GraphicsPreset::Medium, "no exact device-local type");
  }

  void test_overridesAndOtherVendors() {
    AutoPresetDecision decision = selectAutoPreset(discreteNvidia(4 * kGiB), GraphicsPreset::Ultra, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::Ultra, "override prevents demotion");
    TEST_ASSERT(decision.lowMemoryGpu, "override keeps memory-based low memory mode");
    TEST_ASSERT(logContains(decision, "keeping the matched graphics preset override"), "override log");

    GpuHardwareInfo amd = discreteNvidia(16 * kGiB);
    amd.vendorId = kAmd;
    decision = selectAutoPreset(amd, std::nullopt, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::Low && decision.applyNonNvidiaDefaults, "non-NVIDIA default");
    TEST_ASSERT(logContains(decision, "Non-NVIDIA architecture detected"), "non-NVIDIA log");
    decision = selectAutoPreset(amd, GraphicsPreset::High, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::High && decision.applyNonNvidiaDefaults, "non-NVIDIA with override keeps side effects");
  }

  void test_sparkPresets() {
    AutoPresetDecision decision = selectAutoPreset(spark(128 * kGiB), std::nullopt, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::Medium && !decision.lowMemoryGpu, "Spark default");
    TEST_ASSERT(logContains(decision, "RTX Spark detected"), "Spark log");

    decision = selectAutoPreset(spark(128 * kGiB), GraphicsPreset::High, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::High, "per-GPU override wins on Spark");

    decision = selectAutoPreset(spark(8 * kGiB), std::nullopt, 0);
    TEST_ASSERT(decision.preset == GraphicsPreset::Medium && decision.lowMemoryGpu, "8 GiB carve-out enables low memory mode");
    decision = selectAutoPreset(spark(8 * kGiB - 64 * 1024 * 1024), std::nullopt, 0);
    TEST_ASSERT(decision.lowMemoryGpu, "slightly under 8 GiB");
    decision = selectAutoPreset(spark(8 * kGiB + 1), std::nullopt, 0);
    TEST_ASSERT(!decision.lowMemoryGpu, "one byte over 8 GiB");
  }

  void test_dlssMode() {
    const std::optional<DLSSProfile> quality = GpuOverrides::parseDlssMode("Quality");
    TEST_ASSERT(!selectAutoDlssMode(std::nullopt, false), "no default off Spark");
    TEST_ASSERT(std::string(GpuOverrides::dlssModeName(*selectAutoDlssMode(std::nullopt, true))) == "Performance", "Spark default");
    TEST_ASSERT(selectAutoDlssMode(quality, true) == quality, "per-GPU override wins on Spark");
    TEST_ASSERT(selectAutoDlssMode(quality, false) == quality, "per-GPU override elsewhere");
  }

  void test_hardwareCollectionWithoutLuid() {
    const uint32_t vendors[] = { kNvidia, kAmd };
    const bool exactTypeCases[] = { true, false };

    for (uint32_t vendorId : vendors) {
      for (bool hasExactType : exactTypeCases) {
        DxvkDeviceInfo deviceInfo = { };
        deviceInfo.core.properties.vendorID = vendorId;
        deviceInfo.core.properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        deviceInfo.coreDeviceId.deviceLUIDValid = vendorId == kNvidia ? VK_FALSE : VK_TRUE;

        VkPhysicalDeviceMemoryProperties memoryProperties = { };
        memoryProperties.memoryHeapCount = 3;
        memoryProperties.memoryHeaps[0] = { 4 * kGiB, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        memoryProperties.memoryHeaps[1] = { 64 * kGiB, 0 };
        memoryProperties.memoryHeaps[2] = { 8 * kGiB, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };

        memoryProperties.memoryTypeCount = 4;
        memoryProperties.memoryTypes[0] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
        if (!hasExactType) {
          memoryProperties.memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        }
        memoryProperties.memoryTypes[1] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0 };
        memoryProperties.memoryTypes[2] = { VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 1 };
        memoryProperties.memoryTypes[3] = { VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 2 };

        DxvkAdapterMemoryInfo memoryInfo = { };
        memoryInfo.heapCount = 3;
        memoryInfo.heaps[0].memoryBudget = kGiB;
        memoryInfo.heaps[2].memoryBudget = kGiB;

        GpuProfile::detect(deviceInfo, memoryProperties, memoryInfo);
        const GpuHardwareInfo& info = GpuProfile::getHardwareInfo();

        TEST_ASSERT(info.vendorId == vendorId && info.isIntegrated, "Vulkan facts collected");
        TEST_ASSERT(info.exactDeviceLocalTypeHeapSize == (hasExactType ? 4 * kGiB : 0), "first exact device-local type or zero");
        TEST_ASSERT(info.deviceLocalHeapTotal == 12 * kGiB, "local heaps counted once, without host heaps or budgets");
        TEST_ASSERT(!info.nvapiIntegrated && !info.nvapiArchitecture.has_value() && !info.nvapiCoreCount.has_value(), "NVAPI facts remain empty");
        TEST_ASSERT(!GpuProfile::isRtxSpark() && !isRtxSpark(info), "unconfirmed hardware is not Spark");

        if (vendorId == kNvidia) {
          const char* pMismatch = describeRtxSparkMismatch(info);
          TEST_ASSERT(pMismatch != nullptr && std::string(pMismatch) == "NVAPI did not confirm an integrated GPU", "invalid LUID mismatch reason");
        }
      }
    }
  }

  void runAllTests() {
    std::cout << "Running GPU profile tests" << std::endl;
    test_sparkDetection();
    test_sparkObservedLayout();
    test_discretePresets();
    test_overridesAndOtherVendors();
    test_sparkPresets();
    test_dlssMode();
    test_hardwareCollectionWithoutLuid();
    std::cout << "All GPU profile tests PASSED" << std::endl;
  }

}  // namespace gpu_profile_test
}  // namespace dxvk

int main() {
  try {
    dxvk::gpu_profile_test::runAllTests();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& e) {
    std::cerr << "TEST FAILED with exception: " << e.what() << std::endl;
    return -1;
  }
  return 0;
}
