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

#include <cstring>
#include <unordered_set>

#include "dxvk_adapter.h"

#include "dxvk_device.h"
#include "dxvk_instance.h"

// NV-DXVK start: RTXIO
#include "rtx_render/rtx_io.h"
// NV-DXVK end:
// NV-DXVK start: Remix message box utilities
#include "rtx_render/rtx_env.h"
// NV-DXVK end:

namespace dxvk {

  const char* GpuVendorToString(DxvkGpuVendor vendor) {
    switch (vendor) {
      case DxvkGpuVendor::Amd: return "AMD";
      case DxvkGpuVendor::Nvidia: return "NVIDIA";
      case DxvkGpuVendor::Intel: return "Intel";
      default: return "Unknown";
    }
  }

  DxvkAdapter::DxvkAdapter(
    const Rc<vk::InstanceFn>& vki,
          VkPhysicalDevice    handle)
  : m_vki           (vki),
    m_handle        (handle) {
    this->initHeapAllocInfo();
    this->queryExtensions();
    this->queryDeviceInfo();
    this->queryDeviceFeatures();
    this->queryDeviceQueues();

    m_hasMemoryBudget = m_deviceExtensions.supports(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
  }


  DxvkAdapter::~DxvkAdapter() {

  }


  DxvkAdapterMemoryInfo DxvkAdapter::getMemoryHeapInfo() const {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT memBudget = { };
    memBudget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
    memBudget.pNext = nullptr;

    VkPhysicalDeviceMemoryProperties2 memProps = { };
    memProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    memProps.pNext = m_hasMemoryBudget ? &memBudget : nullptr;

    m_vki->vkGetPhysicalDeviceMemoryProperties2(m_handle, &memProps);

    DxvkAdapterMemoryInfo info = { };
    info.heapCount = memProps.memoryProperties.memoryHeapCount;

    for (uint32_t i = 0; i < info.heapCount; i++) {
      info.heaps[i].heapFlags = memProps.memoryProperties.memoryHeaps[i].flags;

      if (m_hasMemoryBudget) {
        info.heaps[i].memoryBudget    = memBudget.heapBudget[i];
        info.heaps[i].memoryAllocated = memBudget.heapUsage[i];
      } else {
        info.heaps[i].memoryBudget    = memProps.memoryProperties.memoryHeaps[i].size;
        info.heaps[i].memoryAllocated = m_heapAlloc[i].load();
      }
    }

    return info;
  }


  VkPhysicalDeviceMemoryProperties DxvkAdapter::memoryProperties() const {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    m_vki->vkGetPhysicalDeviceMemoryProperties(m_handle, &memoryProperties);
    return memoryProperties;
  }


  VkFormatProperties DxvkAdapter::formatProperties(VkFormat format) const {
    VkFormatProperties formatProperties;
    m_vki->vkGetPhysicalDeviceFormatProperties(m_handle, format, &formatProperties);
    return formatProperties;
  }


  VkResult DxvkAdapter::imageFormatProperties(
    VkFormat                  format,
    VkImageType               type,
    VkImageTiling             tiling,
    VkImageUsageFlags         usage,
    VkImageCreateFlags        flags,
    VkImageFormatProperties&  properties) const {
    return m_vki->vkGetPhysicalDeviceImageFormatProperties(
      m_handle, format, type, tiling, usage, flags, &properties);
  }


  DxvkAdapterQueueIndices DxvkAdapter::findQueueFamilies() const {
    uint32_t graphicsQueue = findQueueFamily(
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);

    uint32_t computeQueue = findQueueFamily(
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
      VK_QUEUE_COMPUTE_BIT);

    if (computeQueue == VK_QUEUE_FAMILY_IGNORED)
      computeQueue = graphicsQueue;

    uint32_t transferQueue = findQueueFamily(
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
      VK_QUEUE_TRANSFER_BIT);

    if (transferQueue == VK_QUEUE_FAMILY_IGNORED)
      transferQueue = computeQueue;

    DxvkAdapterQueueIndices queues;
    queues.graphics = graphicsQueue;
    queues.transfer = transferQueue;

    // NV-DXVK start: RTXIO
    uint32_t asyncComputeQueue = findQueueFamily(
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
      VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);

    if (asyncComputeQueue != VK_QUEUE_FAMILY_IGNORED &&
        asyncComputeQueue != graphicsQueue &&
        asyncComputeQueue != transferQueue) {
      queues.asyncCompute = asyncComputeQueue;
    }
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    uint32_t opticalFlowQueue = findQueueFamily(VK_QUEUE_OPTICAL_FLOW_BIT_NV, VK_QUEUE_OPTICAL_FLOW_BIT_NV);

    if (opticalFlowQueue != VK_QUEUE_FAMILY_IGNORED &&
        opticalFlowQueue != graphicsQueue &&
        opticalFlowQueue != asyncComputeQueue &&
        opticalFlowQueue != transferQueue) {
      queues.opticalFlow = opticalFlowQueue;
    }

    // xxxnsubtil: this doesn't actually check for present support, because we don't have a surface here!
    uint32_t presentQueue = findQueueFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
                                            VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);

    if (presentQueue != VK_QUEUE_FAMILY_IGNORED) {
      queues.present = presentQueue;
    }
    // NV_DXVK end

    return queues;
  }


  bool DxvkAdapter::checkFeatureSupport(const DxvkDeviceFeatures& required) const {
    return (m_deviceFeatures.core.features.robustBufferAccess
                || !required.core.features.robustBufferAccess)
        && (m_deviceFeatures.core.features.fullDrawIndexUint32
                || !required.core.features.fullDrawIndexUint32)
        && (m_deviceFeatures.core.features.imageCubeArray
                || !required.core.features.imageCubeArray)
        && (m_deviceFeatures.core.features.independentBlend
                || !required.core.features.independentBlend)
        && (m_deviceFeatures.core.features.geometryShader
                || !required.core.features.geometryShader)
        && (m_deviceFeatures.core.features.tessellationShader
                || !required.core.features.tessellationShader)
        && (m_deviceFeatures.core.features.sampleRateShading
                || !required.core.features.sampleRateShading)
        && (m_deviceFeatures.core.features.dualSrcBlend
                || !required.core.features.dualSrcBlend)
        && (m_deviceFeatures.core.features.logicOp
                || !required.core.features.logicOp)
        && (m_deviceFeatures.core.features.multiDrawIndirect
                || !required.core.features.multiDrawIndirect)
        && (m_deviceFeatures.core.features.drawIndirectFirstInstance
                || !required.core.features.drawIndirectFirstInstance)
        && (m_deviceFeatures.core.features.depthClamp
                || !required.core.features.depthClamp)
        && (m_deviceFeatures.core.features.depthBiasClamp
                || !required.core.features.depthBiasClamp)
        && (m_deviceFeatures.core.features.fillModeNonSolid
                || !required.core.features.fillModeNonSolid)
        && (m_deviceFeatures.core.features.depthBounds
                || !required.core.features.depthBounds)
        && (m_deviceFeatures.core.features.wideLines
                || !required.core.features.wideLines)
        && (m_deviceFeatures.core.features.largePoints
                || !required.core.features.largePoints)
        && (m_deviceFeatures.core.features.alphaToOne
                || !required.core.features.alphaToOne)
        && (m_deviceFeatures.core.features.multiViewport
                || !required.core.features.multiViewport)
        && (m_deviceFeatures.core.features.samplerAnisotropy
                || !required.core.features.samplerAnisotropy)
        && (m_deviceFeatures.core.features.textureCompressionETC2
                || !required.core.features.textureCompressionETC2)
        && (m_deviceFeatures.core.features.textureCompressionASTC_LDR
                || !required.core.features.textureCompressionASTC_LDR)
        && (m_deviceFeatures.core.features.textureCompressionBC
                || !required.core.features.textureCompressionBC)
        && (m_deviceFeatures.core.features.occlusionQueryPrecise
                || !required.core.features.occlusionQueryPrecise)
        && (m_deviceFeatures.core.features.pipelineStatisticsQuery
                || !required.core.features.pipelineStatisticsQuery)
        && (m_deviceFeatures.core.features.vertexPipelineStoresAndAtomics
                || !required.core.features.vertexPipelineStoresAndAtomics)
        && (m_deviceFeatures.core.features.fragmentStoresAndAtomics
                || !required.core.features.fragmentStoresAndAtomics)
        && (m_deviceFeatures.core.features.shaderTessellationAndGeometryPointSize
                || !required.core.features.shaderTessellationAndGeometryPointSize)
        && (m_deviceFeatures.core.features.shaderImageGatherExtended
                || !required.core.features.shaderImageGatherExtended)
        && (m_deviceFeatures.core.features.shaderStorageImageExtendedFormats
                || !required.core.features.shaderStorageImageExtendedFormats)
        && (m_deviceFeatures.core.features.shaderStorageImageMultisample
                || !required.core.features.shaderStorageImageMultisample)
        && (m_deviceFeatures.core.features.shaderStorageImageReadWithoutFormat
                || !required.core.features.shaderStorageImageReadWithoutFormat)
        && (m_deviceFeatures.core.features.shaderStorageImageWriteWithoutFormat
                || !required.core.features.shaderStorageImageWriteWithoutFormat)
        && (m_deviceFeatures.core.features.shaderUniformBufferArrayDynamicIndexing
                || !required.core.features.shaderUniformBufferArrayDynamicIndexing)
        && (m_deviceFeatures.core.features.shaderSampledImageArrayDynamicIndexing
                || !required.core.features.shaderSampledImageArrayDynamicIndexing)
        && (m_deviceFeatures.core.features.shaderStorageBufferArrayDynamicIndexing
                || !required.core.features.shaderStorageBufferArrayDynamicIndexing)
        && (m_deviceFeatures.core.features.shaderStorageImageArrayDynamicIndexing
                || !required.core.features.shaderStorageImageArrayDynamicIndexing)
        && (m_deviceFeatures.core.features.shaderClipDistance
                || !required.core.features.shaderClipDistance)
        && (m_deviceFeatures.core.features.shaderCullDistance
                || !required.core.features.shaderCullDistance)
        && (m_deviceFeatures.core.features.shaderFloat64
                || !required.core.features.shaderFloat64)
        && (m_deviceFeatures.core.features.shaderInt64
                || !required.core.features.shaderInt64)
        && (m_deviceFeatures.core.features.shaderInt16
                || !required.core.features.shaderInt16)
        && (m_deviceFeatures.core.features.shaderResourceResidency
                || !required.core.features.shaderResourceResidency)
        && (m_deviceFeatures.core.features.shaderResourceMinLod
                || !required.core.features.shaderResourceMinLod)
        && (m_deviceFeatures.core.features.sparseBinding
                || !required.core.features.sparseBinding)
        && (m_deviceFeatures.core.features.sparseResidencyBuffer
                || !required.core.features.sparseResidencyBuffer)
        && (m_deviceFeatures.core.features.sparseResidencyImage2D
                || !required.core.features.sparseResidencyImage2D)
        && (m_deviceFeatures.core.features.sparseResidencyImage3D
                || !required.core.features.sparseResidencyImage3D)
        && (m_deviceFeatures.core.features.sparseResidency2Samples
                || !required.core.features.sparseResidency2Samples)
        && (m_deviceFeatures.core.features.sparseResidency4Samples
                || !required.core.features.sparseResidency4Samples)
        && (m_deviceFeatures.core.features.sparseResidency8Samples
                || !required.core.features.sparseResidency8Samples)
        && (m_deviceFeatures.core.features.sparseResidency16Samples
                || !required.core.features.sparseResidency16Samples)
        && (m_deviceFeatures.core.features.sparseResidencyAliased
                || !required.core.features.sparseResidencyAliased)
        && (m_deviceFeatures.core.features.variableMultisampleRate
                || !required.core.features.variableMultisampleRate)
        && (m_deviceFeatures.core.features.inheritedQueries
                || !required.core.features.inheritedQueries)
        && (m_deviceFeatures.vulkan11Features.shaderDrawParameters
                || !required.vulkan11Features.shaderDrawParameters)
        && (m_deviceFeatures.vulkan12Features.hostQueryReset
                || !required.vulkan12Features.hostQueryReset)
        && (m_deviceFeatures.ext4444Formats.formatA4R4G4B4
                || !required.ext4444Formats.formatA4R4G4B4)
        && (m_deviceFeatures.ext4444Formats.formatA4B4G4R4
                || !required.ext4444Formats.formatA4B4G4R4)
        && (m_deviceFeatures.extCustomBorderColor.customBorderColors
                || !required.extCustomBorderColor.customBorderColors)
        && (m_deviceFeatures.extCustomBorderColor.customBorderColorWithoutFormat
                || !required.extCustomBorderColor.customBorderColorWithoutFormat)
        && (m_deviceFeatures.extDepthClipEnable.depthClipEnable
                || !required.extDepthClipEnable.depthClipEnable)
        && (m_deviceFeatures.extExtendedDynamicState.extendedDynamicState
                || !required.extExtendedDynamicState.extendedDynamicState)
        && (m_deviceFeatures.extMemoryPriority.memoryPriority
                || !required.extMemoryPriority.memoryPriority)
        && (m_deviceFeatures.extRobustness2.robustBufferAccess2
                || !required.extRobustness2.robustBufferAccess2)
        && (m_deviceFeatures.extRobustness2.robustImageAccess2
                || !required.extRobustness2.robustImageAccess2)
        && (m_deviceFeatures.extRobustness2.nullDescriptor
                || !required.extRobustness2.nullDescriptor)
        && (m_deviceFeatures.extTransformFeedback.transformFeedback
                || !required.extTransformFeedback.transformFeedback)
        && (m_deviceFeatures.extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor
                || !required.extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor)
        && (m_deviceFeatures.extVertexAttributeDivisor.vertexAttributeInstanceRateZeroDivisor
                || !required.extVertexAttributeDivisor.vertexAttributeInstanceRateZeroDivisor);
  }


  void DxvkAdapter::enableExtensions(const DxvkNameSet& extensions) {
    m_extraExtensions.merge(extensions);
  }

  const std::string getDriverVersionString(const uint32_t version) {
    std::string driverStr = str::format(VK_VERSION_MAJOR(version), ".", std::setfill('0'), std::setw(2), VK_VERSION_MINOR(version));
    if (VK_VERSION_PATCH(version) != 0)
      driverStr += str::format(".", std::setfill('0'), std::setw(2), VK_VERSION_PATCH(version));
    return driverStr;
  }

  Rc<DxvkDevice> DxvkAdapter::createDevice(
    const Rc<DxvkInstance>&   instance,
          DxvkDeviceFeatures  enabledFeatures) {
    DxvkDeviceExtensions devExtensions;

    std::array<DxvkExt*, 42> devExtensionList = {{
      &devExtensions.amdMemoryOverallocationBehaviour,
      &devExtensions.amdShaderFragmentMask,
      &devExtensions.ext4444Formats,
      &devExtensions.extConservativeRasterization,
      &devExtensions.extCustomBorderColor,
      &devExtensions.extDepthClipEnable,
      &devExtensions.extExtendedDynamicState,
      &devExtensions.extFullScreenExclusive,
      &devExtensions.extMemoryBudget,
      &devExtensions.extMemoryPriority,
      &devExtensions.extRobustness2,
      &devExtensions.extShaderDemoteToHelperInvocation,
      &devExtensions.extShaderStencilExport,
      &devExtensions.extShaderViewportIndexLayer,
      &devExtensions.extTransformFeedback,
      &devExtensions.extVertexAttributeDivisor,
      &devExtensions.khrBufferDeviceAddress,
      &devExtensions.khrCreateRenderPass2,
      &devExtensions.khrDepthStencilResolve,
      &devExtensions.khrDrawIndirectCount,
      &devExtensions.khrDriverProperties,
      &devExtensions.khrExternalMemoryWin32,
      &devExtensions.khrImageFormatList,
      &devExtensions.khrSamplerMirrorClampToEdge,
      &devExtensions.khrShaderFloatControls,
      &devExtensions.khrSwapchain,
#ifdef _WIN64
      &devExtensions.khrDeferredHostOperations,
      &devExtensions.khrAccelerationStructure,
      &devExtensions.khrRayQueries,
#endif
      &devExtensions.khrRayTracingPipeline,
      &devExtensions.khrPipelineLibrary,
      &devExtensions.khrPushDescriptor,
      &devExtensions.khrShaderInt8Float16Types,
      &devExtensions.nvRayTracingInvocationReorder,
      &devExtensions.khrSynchronization2,
      &devExtensions.extOpacityMicromap,
      &devExtensions.nvLowLatency,
      &devExtensions.nvxBinaryImport,
      &devExtensions.nvxImageViewHandle,
      &devExtensions.khrExternalMemory,
      &devExtensions.khrExternalSemaphore,
      &devExtensions.khrExternalSemaphoreWin32,
    }};

    // Only enable Cuda interop extensions in 64-bit builds in
    // order to avoid potential driver or address space issues.
    // VK_KHR_buffer_device_address is expensive on some drivers.
    bool enableCudaInterop = !env::is32BitHostPlatform() &&
      m_deviceExtensions.supports(devExtensions.nvxBinaryImport.name()) &&
      m_deviceExtensions.supports(devExtensions.nvxImageViewHandle.name()) &&
      m_deviceFeatures.khrBufferDeviceAddress.bufferDeviceAddress;

    if (enableCudaInterop) {
      devExtensions.nvxBinaryImport.setMode(DxvkExtMode::Optional);
      devExtensions.nvxImageViewHandle.setMode(DxvkExtMode::Optional);
      devExtensions.khrBufferDeviceAddress.setMode(DxvkExtMode::Optional);

      enabledFeatures.khrBufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
    }

    DxvkNameSet extensionsEnabled;

    if (!m_deviceExtensions.enableExtensions(
          devExtensionList.size(),
          devExtensionList.data(),
          extensionsEnabled)) {
      // NV-DXVK start: Check against extension requirements for DXVK and Remix to run
      Logger::err("Unable to find all required Vulkan GPU extensions for device creation.");

      messageBox("Your GPU doesn't support the required features to run RTX Remix.  See the *_d3d9.log for what features your GPU doesn't support.  The game will exit now.", "RTX Remix - GPU Feature Error!", MB_OK);
      // NV-DXVK end

      throw DxvkError("DxvkAdapter: Failed to create device");
    }

    // NV-DXVK start: Integrate Aftermath extensions
    if (instance->options().enableAftermath) {
      std::array devAftermathExtensions = {
        &devExtensions.nvDeviceDiagnostics,
        &devExtensions.nvDeviceDiagnosticCheckpoints,
      };

      m_deviceExtensions.enableExtensions(
        devAftermathExtensions.size(),
        devAftermathExtensions.data(),
        extensionsEnabled);
    }
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    // enable DLFG extensions if available
    std::array devDlfgExtensions = {
      &devExtensions.khrMaintenance4,
      &devExtensions.nvOpticalFlow,
      &devExtensions.extCalibratedTimestamps,
    };

    m_deviceExtensions.enableExtensions(
      devDlfgExtensions.size(),
      devDlfgExtensions.data(),
      extensionsEnabled);
    // NV-DXVK end

    // Enable additional extensions if necessary
    extensionsEnabled.merge(m_extraExtensions);
    DxvkNameList extensionNameList = extensionsEnabled.toNameList();

    // Enable additional device features if supported

    enabledFeatures.vulkan12Features.drawIndirectCount = m_deviceFeatures.vulkan12Features.drawIndirectCount;
    enabledFeatures.vulkan12Features.samplerMirrorClampToEdge = m_deviceFeatures.vulkan12Features.samplerMirrorClampToEdge;

    enabledFeatures.extExtendedDynamicState.extendedDynamicState = m_deviceFeatures.extExtendedDynamicState.extendedDynamicState;

    enabledFeatures.ext4444Formats.formatA4B4G4R4 = m_deviceFeatures.ext4444Formats.formatA4B4G4R4;
    enabledFeatures.ext4444Formats.formatA4R4G4B4 = m_deviceFeatures.ext4444Formats.formatA4R4G4B4;

    // Enable RTX device features if supported

    enabledFeatures.core.features.shaderInt16 = m_deviceFeatures.core.features.shaderInt16;
    enabledFeatures.vulkan11Features.storageBuffer16BitAccess = m_deviceFeatures.vulkan11Features.storageBuffer16BitAccess;
    enabledFeatures.vulkan11Features.uniformAndStorageBuffer16BitAccess = m_deviceFeatures.vulkan11Features.uniformAndStorageBuffer16BitAccess;
    enabledFeatures.vulkan12Features.bufferDeviceAddress = m_deviceFeatures.vulkan12Features.bufferDeviceAddress;
    enabledFeatures.vulkan12Features.descriptorIndexing = m_deviceFeatures.vulkan12Features.descriptorIndexing;
    enabledFeatures.vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = m_deviceFeatures.vulkan12Features.descriptorBindingSampledImageUpdateAfterBind;
    enabledFeatures.vulkan12Features.runtimeDescriptorArray = m_deviceFeatures.vulkan12Features.runtimeDescriptorArray;
    enabledFeatures.vulkan12Features.descriptorBindingPartiallyBound = m_deviceFeatures.vulkan12Features.descriptorBindingPartiallyBound;
    enabledFeatures.vulkan12Features.shaderStorageBufferArrayNonUniformIndexing = m_deviceFeatures.vulkan12Features.shaderStorageBufferArrayNonUniformIndexing;
    enabledFeatures.vulkan12Features.shaderSampledImageArrayNonUniformIndexing = m_deviceFeatures.vulkan12Features.shaderSampledImageArrayNonUniformIndexing;
    enabledFeatures.vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = m_deviceFeatures.vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind;
    enabledFeatures.vulkan12Features.descriptorBindingVariableDescriptorCount = m_deviceFeatures.vulkan12Features.descriptorBindingVariableDescriptorCount;
    enabledFeatures.vulkan12Features.shaderInt8 = m_deviceFeatures.vulkan12Features.shaderInt8;
    enabledFeatures.vulkan12Features.shaderFloat16 = m_deviceFeatures.vulkan12Features.shaderFloat16;
    enabledFeatures.vulkan12Features.uniformAndStorageBuffer8BitAccess = m_deviceFeatures.vulkan12Features.uniformAndStorageBuffer8BitAccess;
    enabledFeatures.khrAccelerationStructureFeatures.accelerationStructure = m_deviceFeatures.khrAccelerationStructureFeatures.accelerationStructure;
    enabledFeatures.khrRayQueryFeatures.rayQuery = m_deviceFeatures.khrRayQueryFeatures.rayQuery;
    enabledFeatures.khrDeviceRayTracingPipelineFeatures.rayTracingPipeline = m_deviceFeatures.khrDeviceRayTracingPipelineFeatures.rayTracingPipeline;

    enabledFeatures.vulkan12Features.shaderInt8 = VK_TRUE;
    enabledFeatures.vulkan12Features.storageBuffer8BitAccess = VK_TRUE;
    enabledFeatures.vulkan12Features.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    enabledFeatures.vulkan12Features.timelineSemaphore = VK_TRUE;

    // NV-DXVK start: RTXIO
#ifdef WITH_RTXIO
    if (RtxIo::enabled()) {
      // Reset the extension provider to adapter's Vulkan instance first since client app
      // may have probbed another Vulkan instance in the process and so latched it inside
      // extension provider singleton.
      RtxIoExtensionProvider::s_instance.initDeviceExtensions(instance.ptr());
      if (!RtxIoExtensionProvider::s_instance.getDeviceFeatures(m_handle, enabledFeatures)) {
        Logger::err("Physical device does not support features required to enable RTX IO.");
        throw DxvkError("DxvkAdapter: Failed to create device");
      }
    }
#endif
    // NV-DXVK end:

    // Create pNext chain for additional device features
    enabledFeatures.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;

    // NV-DXVK start: RTXIO
    // Preserve pNext chain from RTXIO
    std::exchange(enabledFeatures.core.pNext, enabledFeatures.vulkan12Features.pNext);
    // NV-DXVK end:

    enabledFeatures.vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    enabledFeatures.vulkan11Features.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.vulkan11Features);

    enabledFeatures.vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enabledFeatures.vulkan12Features.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.vulkan12Features);

#ifdef _WIN64
    if (devExtensions.khrAccelerationStructure) {
      enabledFeatures.khrAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
      enabledFeatures.khrAccelerationStructureFeatures.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.khrAccelerationStructureFeatures);
    }

    if (devExtensions.khrRayQueries) {
      enabledFeatures.khrRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
      enabledFeatures.khrRayQueryFeatures.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.khrRayQueryFeatures);
    }
#endif

    if (devExtensions.khrRayTracingPipeline) {
        enabledFeatures.khrDeviceRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        enabledFeatures.khrDeviceRayTracingPipelineFeatures.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.khrDeviceRayTracingPipelineFeatures);
    }

    if (devExtensions.ext4444Formats) {
      enabledFeatures.ext4444Formats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT;
      enabledFeatures.ext4444Formats.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.ext4444Formats);
    }

    if (devExtensions.extCustomBorderColor) {
      enabledFeatures.extCustomBorderColor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
      enabledFeatures.extCustomBorderColor.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extCustomBorderColor);
    }

    if (devExtensions.extDepthClipEnable) {
      enabledFeatures.extDepthClipEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
      enabledFeatures.extDepthClipEnable.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extDepthClipEnable);
    }

    if (devExtensions.extExtendedDynamicState) {
      enabledFeatures.extExtendedDynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
      enabledFeatures.extExtendedDynamicState.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extExtendedDynamicState);
    }

    if (devExtensions.extMemoryPriority) {
      enabledFeatures.extMemoryPriority.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
      enabledFeatures.extMemoryPriority.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extMemoryPriority);
    }

    if (devExtensions.extShaderDemoteToHelperInvocation) {
      enabledFeatures.extShaderDemoteToHelperInvocation.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
      enabledFeatures.extShaderDemoteToHelperInvocation.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extShaderDemoteToHelperInvocation);
    }

    if (devExtensions.extRobustness2) {
      enabledFeatures.extRobustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
      enabledFeatures.extRobustness2.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extRobustness2);
    }

    if (devExtensions.extTransformFeedback) {
      enabledFeatures.extTransformFeedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
      enabledFeatures.extTransformFeedback.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extTransformFeedback);
    }

    if (devExtensions.extVertexAttributeDivisor.revision() >= 3) {
      enabledFeatures.extVertexAttributeDivisor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
      enabledFeatures.extVertexAttributeDivisor.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.extVertexAttributeDivisor);
    }

    // NV-DXVK start: Integrate Aftermath
    if (devExtensions.nvDeviceDiagnostics && instance->options().enableAftermath) {
      enabledFeatures.nvDeviceDiagnosticsConfig.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV;
      enabledFeatures.nvDeviceDiagnosticsConfig.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.nvDeviceDiagnosticsConfig);
      enabledFeatures.nvDeviceDiagnosticsConfig.diagnosticsConfig = VK_TRUE;
    }
    // NV-DXVK end

    // NV-DXVK start: opacity micromap
    if (devExtensions.khrSynchronization2) {
      enabledFeatures.khrSynchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
      enabledFeatures.khrSynchronization2.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.khrSynchronization2);
      enabledFeatures.khrSynchronization2.synchronization2 = VK_TRUE;
    }
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    if (devExtensions.nvOpticalFlow) {
      enabledFeatures.nvOpticalFlow.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV;
      enabledFeatures.nvOpticalFlow.pNext = std::exchange(enabledFeatures.core.pNext, &enabledFeatures.nvOpticalFlow);
      enabledFeatures.nvOpticalFlow.opticalFlow = VK_TRUE;
    }
    // NV-DXVK end

    // NV-DXVK start: Moved logging to where it is on more recent DXVK to properly show enabled features, also added more information to be logged
    // (Still needs driver version from latest DXVK though at the time of writing this, but we can wait on that since it needs larger changes)

    // Log GPU information, extensions and enabled features

    // Note: Split out from the other formatting operation to not have these hex modifiers affect other printed values. Not very effecient
    // but C++'s formatting being stateful like this is annoying to work with otherwise.
    const auto hexFormattedString = str::format(
      std::setfill('0'), std::setw(4), std::hex,
      "\n  Device ID:       : 0x", m_deviceInfo.core.properties.deviceID,
      "\n  Vendor ID:       : 0x", m_deviceInfo.core.properties.vendorID
    );

    Logger::info(str::format("Device properties:"
      "\n  Device name:     : ", m_deviceInfo.core.properties.deviceName,
      // Note: This cast is fine to do as values will be safely represented in this fixed-width enum, see:
      // https://stackoverflow.com/a/55404179
      // Additionally, the function to convert to string will properly handle cases where the vendor is unknown.
      "\n  Vendor name:     : ", GpuVendorToString(static_cast<DxvkGpuVendor>(m_deviceInfo.core.properties.vendorID)),
      hexFormattedString,
      "\n  Driver version   : ",
        VK_VERSION_MAJOR(m_deviceInfo.core.properties.driverVersion), ".",
        VK_VERSION_MINOR(m_deviceInfo.core.properties.driverVersion), ".",
        VK_VERSION_PATCH(m_deviceInfo.core.properties.driverVersion)));

    Logger::info("Enabled device extensions:");
    this->logNameList(extensionNameList);
    this->logFeatures(enabledFeatures);

    // NV-DXVK end

    // NV-DXVK start: Check against set driver version minimums requires for Remix to run
    // Note: This vendor/driver version check could be done much sooner, but we do it here instead just before device creation or anything else
    // substantial with Vulkan takes place so that the device info, extensions and enabled features can be printed out first (just to ensure
    // users get a bit more info in the log if the driver version check fails).
    if (m_deviceInfo.core.properties.vendorID == static_cast<uint32_t>(DxvkGpuVendor::Nvidia)) {
      const auto driverVersion = m_deviceInfo.core.properties.driverVersion;
      const auto minDriverVersion = ::GetModuleHandle("winevulkan.dll") ? instance->options().nvidiaLinuxMinDriver : instance->options().nvidiaMinDriver;

      if (driverVersion < minDriverVersion) {
        const auto currentDriverVersionString = getDriverVersionString(driverVersion);
        const auto minimumDriverVersionString = getDriverVersionString(minDriverVersion);

        // Note: Error logging done before message box to ensure it is always logged (as the process will block on the message box call and may be terminated
        // before the user interacts with the message box to continue execution).
        Logger::err(str::format(
          "Current NVIDIA Graphics Driver version (", currentDriverVersionString, ") is lower than the minimum required version (", minimumDriverVersionString, "). "
          "Please update your to the latest version for RTX Remix to function properly."));

        const std::string minDriverCheckDialogMessage = str::format(
          "Your GPU driver needs to be updated before running this game with RTX Remix. Please update the NVIDIA Graphics Driver to the latest version. The game will exit now.\n\n"
          "\tCurrently installed: ", currentDriverVersionString, "\n",
          "\tRequired minimum: ", minimumDriverVersionString);

        messageBox(minDriverCheckDialogMessage.c_str(), "RTX Remix - Driver Compatibility Error!", MB_OK);

        throw DxvkError("DxvkAdapter: Failed to create device");
      }
    }
    // NV-DXVK end

    // Report the desired overallocation behaviour to the driver
    VkDeviceMemoryOverallocationCreateInfoAMD overallocInfo;
    overallocInfo.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD;
    overallocInfo.pNext = nullptr;
    overallocInfo.overallocationBehavior = VK_MEMORY_OVERALLOCATION_BEHAVIOR_ALLOWED_AMD;

    // NV-DXVK start: Integrate Aftermath
    VkDeviceDiagnosticsConfigCreateInfoNV deviceDiag;
    deviceDiag.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
    deviceDiag.pNext = nullptr;
    deviceDiag.flags = VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV | VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_ERROR_REPORTING_BIT_NV;
    if (instance->options().enableAftermathResourceTracking) {
      deviceDiag.flags |= VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV;
    }
    // NV-DXVK end

    // Create the requested queues
    uint32_t numQueuePriorities = 0;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;

    // NV-DXVK begin: DLFG integration + RTXIO
    std::map<uint32_t, uint32_t> queueCounts; // maps queue family indices to a count of queues for that family

    DxvkAdapterQueueIndices queueFamilies = findQueueFamilies();
    numQueuePriorities = std::max(numQueuePriorities, ++queueCounts[queueFamilies.graphics]);
    numQueuePriorities = std::max(numQueuePriorities, ++queueCounts[queueFamilies.transfer]);

    if (queueFamilies.asyncCompute != VK_QUEUE_FAMILY_IGNORED) {
      numQueuePriorities = std::max(numQueuePriorities, ++queueCounts[queueFamilies.asyncCompute]);
    }

    if (queueFamilies.opticalFlow != VK_QUEUE_FAMILY_IGNORED) {
      numQueuePriorities = std::max(numQueuePriorities, ++queueCounts[queueFamilies.opticalFlow]);
    }

    if (queueFamilies.present != VK_QUEUE_FAMILY_IGNORED) {
      numQueuePriorities = std::max(numQueuePriorities, ++queueCounts[queueFamilies.present]);
    }

    std::vector<float> queuePriorities(numQueuePriorities);
    std::fill(queuePriorities.begin(), queuePriorities.end(), 1.0f);

    this->logQueueFamilies(queueFamilies);

    for (auto queueInfo : queueCounts) {
      uint32_t queueFamily = queueInfo.first;
      uint32_t queueCount = queueInfo.second;
      
      VkDeviceQueueCreateInfo graphicsQueue;
      graphicsQueue.sType             = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      graphicsQueue.pNext             = nullptr;
      graphicsQueue.flags             = 0;
      graphicsQueue.queueFamilyIndex  = queueFamily;
      graphicsQueue.queueCount        = queueCount;
      graphicsQueue.pQueuePriorities  = queuePriorities.data();
      queueInfos.push_back(graphicsQueue);
    }
    // NV-DXVK end

    VkDeviceCreateInfo info;
    info.sType                      = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext                      = enabledFeatures.core.pNext;
    info.flags                      = 0;
    info.queueCreateInfoCount       = queueInfos.size();
    info.pQueueCreateInfos          = queueInfos.data();
    info.enabledLayerCount          = 0;
    info.ppEnabledLayerNames        = nullptr;
    info.enabledExtensionCount      = extensionNameList.count();
    info.ppEnabledExtensionNames    = extensionNameList.names();
    info.pEnabledFeatures           = &enabledFeatures.core.features;

    if (devExtensions.amdMemoryOverallocationBehaviour)
      overallocInfo.pNext = std::exchange(info.pNext, &overallocInfo);

    // NV-DXVK start: Integrate Aftermath
    if (devExtensions.nvDeviceDiagnostics && instance->options().enableAftermath)
      deviceDiag.pNext = std::exchange(info.pNext, &deviceDiag);
    // NV-DXVK end

    VkDevice device = VK_NULL_HANDLE;
    VkResult vr = m_vki->vkCreateDevice(m_handle, &info, nullptr, &device);

    if (vr != VK_SUCCESS && enableCudaInterop) {
      // Enabling certain Vulkan extensions can cause device creation to fail on
      // Nvidia drivers if a certain kernel module isn't loaded, but we cannot know
      // that in advance since the extensions are reported as supported anyway.
      Logger::err("DxvkAdapter: Failed to create device, retrying without CUDA interop extensions");

      extensionsEnabled.disableExtension(devExtensions.khrBufferDeviceAddress);
      extensionsEnabled.disableExtension(devExtensions.nvxBinaryImport);
      extensionsEnabled.disableExtension(devExtensions.nvxImageViewHandle);

      enabledFeatures.khrBufferDeviceAddress.bufferDeviceAddress = VK_FALSE;

      vk::removeStructFromPNextChain(&enabledFeatures.core.pNext,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR);

      extensionNameList = extensionsEnabled.toNameList();
      info.enabledExtensionCount      = extensionNameList.count();
      info.ppEnabledExtensionNames    = extensionNameList.names();

      vr = m_vki->vkCreateDevice(m_handle, &info, nullptr, &device);
    }

    if (vr != VK_SUCCESS)
      throw DxvkError("DxvkAdapter: Failed to create device");

    Rc<DxvkDevice> result = new DxvkDevice(instance, this,
      new vk::DeviceFn(true, m_vki->instance(), device),
      devExtensions, enabledFeatures);
    result->initResources();
    return result;
  }


  void DxvkAdapter::notifyHeapMemoryAlloc(
          uint32_t            heap,
          VkDeviceSize        bytes) {
    if (!m_hasMemoryBudget)
      m_heapAlloc[heap] += bytes;
  }


  void DxvkAdapter::notifyHeapMemoryFree(
          uint32_t            heap,
          VkDeviceSize        bytes) {
    if (!m_hasMemoryBudget)
      m_heapAlloc[heap] -= bytes;
  }


  bool DxvkAdapter::matchesDriver(
          DxvkGpuVendor       vendor,
          VkDriverIdKHR       driver,
          uint32_t            minVer,
          uint32_t            maxVer) const {
    bool driverMatches = m_deviceInfo.khrDeviceDriverProperties.driverID
      ? driver == m_deviceInfo.khrDeviceDriverProperties.driverID
      : vendor == DxvkGpuVendor(m_deviceInfo.core.properties.vendorID);

    if (minVer) driverMatches &= m_deviceInfo.core.properties.driverVersion >= minVer;
    if (maxVer) driverMatches &= m_deviceInfo.core.properties.driverVersion <  maxVer;

    return driverMatches;
  }


  void DxvkAdapter::logAdapterInfo() const {
    VkPhysicalDeviceProperties deviceInfo = this->deviceProperties();
    VkPhysicalDeviceMemoryProperties memoryInfo = this->memoryProperties();

    Logger::info(str::format(deviceInfo.deviceName, ":"));
    Logger::info(str::format("  Driver: ",
      VK_VERSION_MAJOR(deviceInfo.driverVersion), ".",
      VK_VERSION_MINOR(deviceInfo.driverVersion), ".",
      VK_VERSION_PATCH(deviceInfo.driverVersion)));
    Logger::info(str::format("  Vulkan: ",
      VK_VERSION_MAJOR(deviceInfo.apiVersion), ".",
      VK_VERSION_MINOR(deviceInfo.apiVersion), ".",
      VK_VERSION_PATCH(deviceInfo.apiVersion)));

    for (uint32_t i = 0; i < memoryInfo.memoryHeapCount; i++) {
      constexpr VkDeviceSize mib = 1024 * 1024;

      Logger::info(str::format("  Memory Heap[", i, "]: "));
      Logger::info(str::format("    Size: ", memoryInfo.memoryHeaps[i].size / mib, " MiB"));
      Logger::info(str::format("    Flags: ", "0x", std::hex, memoryInfo.memoryHeaps[i].flags));

      for (uint32_t j = 0; j < memoryInfo.memoryTypeCount; j++) {
        if (memoryInfo.memoryTypes[j].heapIndex == i) {
          Logger::info(str::format(
            "    Memory Type[", j, "]: ",
            "Property Flags = ", "0x", std::hex, memoryInfo.memoryTypes[j].propertyFlags));
        }
      }
    }
  }
  
  bool DxvkAdapter::isUnifiedMemoryArchitecture() const {
    auto memory = this->memoryProperties();
    bool result = true;

    for (uint32_t i = 0; i < memory.memoryHeapCount; i++)
      result &= memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    return result;
  }
  
  
  void DxvkAdapter::initHeapAllocInfo() {
    for (uint32_t i = 0; i < m_heapAlloc.size(); i++)
      m_heapAlloc[i] = 0;
  }


  void DxvkAdapter::queryExtensions() {
    m_deviceExtensions = DxvkNameSet::enumDeviceExtensions(m_vki, m_handle);
  }


  void DxvkAdapter::queryDeviceInfo() {
    m_deviceInfo = DxvkDeviceInfo();
    m_deviceInfo.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    m_deviceInfo.core.pNext = nullptr;

    // Query info now so that we have basic device properties available
    m_vki->vkGetPhysicalDeviceProperties2(m_handle, &m_deviceInfo.core);

    m_deviceInfo.coreDeviceId.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    m_deviceInfo.coreDeviceId.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.coreDeviceId);

    m_deviceInfo.coreSubgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    m_deviceInfo.coreSubgroup.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.coreSubgroup);

    if (m_deviceExtensions.supports(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME)) {
      m_deviceInfo.extConservativeRasterization.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
      m_deviceInfo.extConservativeRasterization.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extConservativeRasterization);
    }

    if (m_deviceExtensions.supports(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME)) {
      m_deviceInfo.extCustomBorderColor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT;
      m_deviceInfo.extCustomBorderColor.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extCustomBorderColor);
    }

    if (m_deviceExtensions.supports(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
      m_deviceInfo.extRobustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT;
      m_deviceInfo.extRobustness2.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extRobustness2);
    }

    if (m_deviceExtensions.supports(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME)) {
      m_deviceInfo.extTransformFeedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT;
      m_deviceInfo.extTransformFeedback.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extTransformFeedback);
    }

    if (m_deviceExtensions.supports(VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME)) {
      m_deviceInfo.extVertexAttributeDivisor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT;
      m_deviceInfo.extVertexAttributeDivisor.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extVertexAttributeDivisor);
    }

    if (m_deviceExtensions.supports(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME)) {
      m_deviceInfo.khrDepthStencilResolve.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES_KHR;
      m_deviceInfo.khrDepthStencilResolve.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.khrDepthStencilResolve);
    }

    if (m_deviceExtensions.supports(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
      m_deviceInfo.khrDeviceDriverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
      m_deviceInfo.khrDeviceDriverProperties.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.khrDeviceDriverProperties);
    }

    if (m_deviceExtensions.supports(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME)) {
      m_deviceInfo.khrShaderFloatControls.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
      m_deviceInfo.khrShaderFloatControls.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.khrShaderFloatControls);
    }

    if (m_deviceExtensions.supports(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
        m_deviceInfo.khrDeviceRayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        m_deviceInfo.khrDeviceRayTracingPipelineProperties.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.khrDeviceRayTracingPipelineProperties);
    }

    if (m_deviceExtensions.supports(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
      m_deviceInfo.khrDeviceAccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
      m_deviceInfo.khrDeviceAccelerationStructureProperties.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.khrDeviceAccelerationStructureProperties);
    }

    if (m_deviceExtensions.supports(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)) {
      m_deviceInfo.extOpacityMicromapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT;
      m_deviceInfo.extOpacityMicromapProperties.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.extOpacityMicromapProperties);
    }

    if (m_deviceExtensions.supports(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)) {
      m_deviceInfo.nvRayTracingInvocationReorderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_NV;
      m_deviceInfo.nvRayTracingInvocationReorderProperties.pNext = std::exchange(m_deviceInfo.core.pNext, &m_deviceInfo.nvRayTracingInvocationReorderProperties);
    }

    // Query full device properties for all enabled extensions
    m_vki->vkGetPhysicalDeviceProperties2(m_handle, &m_deviceInfo.core);
    
    // Some drivers reports the driver version in a slightly different format
    switch (m_deviceInfo.khrDeviceDriverProperties.driverID) {
      case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
        m_deviceInfo.core.properties.driverVersion = VK_MAKE_VERSION(
          (m_deviceInfo.core.properties.driverVersion >> 22) & 0x3ff,
          (m_deviceInfo.core.properties.driverVersion >> 14) & 0x0ff,
          (m_deviceInfo.core.properties.driverVersion >>  6) & 0x0ff);
        break;

      case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
        m_deviceInfo.core.properties.driverVersion = VK_MAKE_VERSION(
          m_deviceInfo.core.properties.driverVersion >> 14,
          m_deviceInfo.core.properties.driverVersion & 0x3fff, 0);
        break;

      default:;
    }
  }

  void DxvkAdapter::queryDeviceFeatures() {
    m_deviceFeatures = DxvkDeviceFeatures();
    m_deviceFeatures.core.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    m_deviceFeatures.core.pNext = nullptr;

    m_deviceFeatures.vulkan11Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    m_deviceFeatures.vulkan11Features.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.vulkan11Features);

    m_deviceFeatures.vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    m_deviceFeatures.vulkan12Features.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.vulkan12Features);

    if (m_deviceExtensions.supports(VK_EXT_4444_FORMATS_EXTENSION_NAME)) {
      m_deviceFeatures.ext4444Formats.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT;
      m_deviceFeatures.ext4444Formats.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.ext4444Formats);
    }

    if (m_deviceExtensions.supports(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME)) {
      m_deviceFeatures.extCustomBorderColor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
      m_deviceFeatures.extCustomBorderColor.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extCustomBorderColor);
    }

    if (m_deviceExtensions.supports(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)) {
      m_deviceFeatures.extDepthClipEnable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
      m_deviceFeatures.extDepthClipEnable.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extDepthClipEnable);
    }

    if (m_deviceExtensions.supports(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) {
      m_deviceFeatures.extExtendedDynamicState.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
      m_deviceFeatures.extExtendedDynamicState.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extExtendedDynamicState);
    }

    if (m_deviceExtensions.supports(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME)) {
      m_deviceFeatures.extMemoryPriority.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
      m_deviceFeatures.extMemoryPriority.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extMemoryPriority);
    }

    if (m_deviceExtensions.supports(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME)) {
      m_deviceFeatures.extRobustness2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
      m_deviceFeatures.extRobustness2.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extRobustness2);
    }

    if (m_deviceExtensions.supports(VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME)) {
      m_deviceFeatures.extShaderDemoteToHelperInvocation.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
      m_deviceFeatures.extShaderDemoteToHelperInvocation.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extShaderDemoteToHelperInvocation);
    }

    if (m_deviceExtensions.supports(VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME)) {
      m_deviceFeatures.extTransformFeedback.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
      m_deviceFeatures.extTransformFeedback.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extTransformFeedback);
    }

    if (m_deviceExtensions.supports(VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME) >= 3) {
      m_deviceFeatures.extVertexAttributeDivisor.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
      m_deviceFeatures.extVertexAttributeDivisor.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.extVertexAttributeDivisor);
    }

    if (m_deviceExtensions.supports(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
      m_deviceFeatures.khrRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
      m_deviceFeatures.khrRayQueryFeatures.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.khrRayQueryFeatures);
    }

    if (m_deviceExtensions.supports(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)) {
      m_deviceFeatures.khrAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
      m_deviceFeatures.khrAccelerationStructureFeatures.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.khrAccelerationStructureFeatures);
    }

    if (m_deviceExtensions.supports(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
      m_deviceFeatures.khrDeviceRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
      m_deviceFeatures.khrDeviceRayTracingPipelineFeatures.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.khrDeviceRayTracingPipelineFeatures);
    }

    if (m_deviceExtensions.supports(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)) {
      m_deviceFeatures.khrBufferDeviceAddress.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
      m_deviceFeatures.khrBufferDeviceAddress.pNext = std::exchange(m_deviceFeatures.core.pNext, &m_deviceFeatures.khrBufferDeviceAddress);
    }

    m_vki->vkGetPhysicalDeviceFeatures2(m_handle, &m_deviceFeatures.core);
  }


  void DxvkAdapter::queryDeviceQueues() {
    uint32_t numQueueFamilies = 0;
    m_vki->vkGetPhysicalDeviceQueueFamilyProperties(
      m_handle, &numQueueFamilies, nullptr);

    m_queueFamilies.resize(numQueueFamilies);
    m_vki->vkGetPhysicalDeviceQueueFamilyProperties(
      m_handle, &numQueueFamilies, m_queueFamilies.data());
  }


  uint32_t DxvkAdapter::findQueueFamily(
          VkQueueFlags          mask,
          VkQueueFlags          flags) const {
    for (uint32_t i = 0; i < m_queueFamilies.size(); i++) {
      if ((m_queueFamilies[i].queueFlags & mask) == flags)
        return i;
    }

    return VK_QUEUE_FAMILY_IGNORED;
  }


  void DxvkAdapter::logNameList(const DxvkNameList& names) {
    for (uint32_t i = 0; i < names.count(); i++)
      Logger::info(str::format("  ", names.name(i)));
  }


  void DxvkAdapter::logFeatures(const DxvkDeviceFeatures& features) {
    Logger::info(str::format("Device features:",
      "\n  robustBufferAccess                     : ", features.core.features.robustBufferAccess ? "1" : "0",
      "\n  fullDrawIndexUint32                    : ", features.core.features.fullDrawIndexUint32 ? "1" : "0",
      "\n  imageCubeArray                         : ", features.core.features.imageCubeArray ? "1" : "0",
      "\n  independentBlend                       : ", features.core.features.independentBlend ? "1" : "0",
      "\n  geometryShader                         : ", features.core.features.geometryShader ? "1" : "0",
      "\n  tessellationShader                     : ", features.core.features.tessellationShader ? "1" : "0",
      "\n  sampleRateShading                      : ", features.core.features.sampleRateShading ? "1" : "0",
      "\n  dualSrcBlend                           : ", features.core.features.dualSrcBlend ? "1" : "0",
      "\n  logicOp                                : ", features.core.features.logicOp ? "1" : "0",
      "\n  multiDrawIndirect                      : ", features.core.features.multiDrawIndirect ? "1" : "0",
      "\n  drawIndirectFirstInstance              : ", features.core.features.drawIndirectFirstInstance ? "1" : "0",
      "\n  depthClamp                             : ", features.core.features.depthClamp ? "1" : "0",
      "\n  depthBiasClamp                         : ", features.core.features.depthBiasClamp ? "1" : "0",
      "\n  fillModeNonSolid                       : ", features.core.features.fillModeNonSolid ? "1" : "0",
      "\n  depthBounds                            : ", features.core.features.depthBounds ? "1" : "0",
      "\n  multiViewport                          : ", features.core.features.multiViewport ? "1" : "0",
      "\n  samplerAnisotropy                      : ", features.core.features.samplerAnisotropy ? "1" : "0",
      "\n  textureCompressionBC                   : ", features.core.features.textureCompressionBC ? "1" : "0",
      "\n  occlusionQueryPrecise                  : ", features.core.features.occlusionQueryPrecise ? "1" : "0",
      "\n  pipelineStatisticsQuery                : ", features.core.features.pipelineStatisticsQuery ? "1" : "0",
      "\n  vertexPipelineStoresAndAtomics         : ", features.core.features.vertexPipelineStoresAndAtomics ? "1" : "0",
      "\n  fragmentStoresAndAtomics               : ", features.core.features.fragmentStoresAndAtomics ? "1" : "0",
      "\n  shaderImageGatherExtended              : ", features.core.features.shaderImageGatherExtended ? "1" : "0",
      "\n  shaderStorageImageExtendedFormats      : ", features.core.features.shaderStorageImageExtendedFormats ? "1" : "0",
      "\n  shaderStorageImageReadWithoutFormat    : ", features.core.features.shaderStorageImageReadWithoutFormat ? "1" : "0",
      "\n  shaderStorageImageWriteWithoutFormat   : ", features.core.features.shaderStorageImageWriteWithoutFormat ? "1" : "0",
      "\n  shaderClipDistance                     : ", features.core.features.shaderClipDistance ? "1" : "0",
      "\n  shaderCullDistance                     : ", features.core.features.shaderCullDistance ? "1" : "0",
      "\n  shaderFloat64                          : ", features.core.features.shaderFloat64 ? "1" : "0",
      "\n  shaderInt64                            : ", features.core.features.shaderInt64 ? "1" : "0",
      "\n  variableMultisampleRate                : ", features.core.features.variableMultisampleRate ? "1" : "0",
      "\n  hostQueryReset                         : ", features.vulkan12Features.hostQueryReset ? "1" : "0",
      "\n", VK_EXT_4444_FORMATS_EXTENSION_NAME,
      "\n  formatA4R4G4B4                         : ", features.ext4444Formats.formatA4R4G4B4 ? "1" : "0",
      "\n  formatA4B4G4R4                         : ", features.ext4444Formats.formatA4B4G4R4 ? "1" : "0",
      "\n", VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
      "\n  customBorderColors                     : ", features.extCustomBorderColor.customBorderColors ? "1" : "0",
      "\n  customBorderColorWithoutFormat         : ", features.extCustomBorderColor.customBorderColorWithoutFormat ? "1" : "0",
      "\n", VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
      "\n  depthClipEnable                        : ", features.extDepthClipEnable.depthClipEnable ? "1" : "0",
      "\n", VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
      "\n  extendedDynamicState                   : ", features.extExtendedDynamicState.extendedDynamicState ? "1" : "0",
      "\n", VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
      "\n  memoryPriority                         : ", features.extMemoryPriority.memoryPriority ? "1" : "0",
      "\n", VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
      "\n  robustBufferAccess2                    : ", features.extRobustness2.robustBufferAccess2 ? "1" : "0",
      "\n  robustImageAccess2                     : ", features.extRobustness2.robustImageAccess2 ? "1" : "0",
      "\n  nullDescriptor                         : ", features.extRobustness2.nullDescriptor ? "1" : "0",
      "\n", VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
      "\n  shaderDemoteToHelperInvocation         : ", features.extShaderDemoteToHelperInvocation.shaderDemoteToHelperInvocation ? "1" : "0",
      "\n", VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME,
      "\n  transformFeedback                      : ", features.extTransformFeedback.transformFeedback ? "1" : "0",
      "\n  geometryStreams                        : ", features.extTransformFeedback.geometryStreams ? "1" : "0",
      "\n", VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
      "\n  vertexAttributeInstanceRateDivisor     : ", features.extVertexAttributeDivisor.vertexAttributeInstanceRateDivisor ? "1" : "0",
      "\n  vertexAttributeInstanceRateZeroDivisor : ", features.extVertexAttributeDivisor.vertexAttributeInstanceRateZeroDivisor ? "1" : "0",
      "\n", VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
      "\n  bufferDeviceAddress                    : ", features.khrBufferDeviceAddress.bufferDeviceAddress,
      // NV-DXVK begin: DLFG integration
      "\n", VK_NV_OPTICAL_FLOW_EXTENSION_NAME,
      "\n  nvOpticalFlow                            : ", features.nvOpticalFlow.opticalFlow));
      // NV-DXVK end
  }


  void DxvkAdapter::logQueueFamilies(const DxvkAdapterQueueIndices& queues) {
    Logger::info(str::format("Queue families:",
      "\n  Graphics : ", queues.graphics,
      "\n  Transfer : ", queues.transfer));
    // NV-DXVK start: RTXIO
    if (queues.asyncCompute != VK_QUEUE_FAMILY_IGNORED) {
      Logger::info(str::format("  Async Compute : ", queues.asyncCompute));
    }
    // NV-DXVK end
    // NV-DXVK start: DLFG integration
    if (queues.opticalFlow != VK_QUEUE_FAMILY_IGNORED) {
      Logger::info(str::format("  Optical flow : ", queues.opticalFlow));
    }

    if (queues.present != VK_QUEUE_FAMILY_IGNORED) {
      Logger::info(str::format("  Present : ", queues.present));
    }
    // NV-DXVK end
  }

}
