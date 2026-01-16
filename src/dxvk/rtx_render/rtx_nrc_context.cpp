/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_nrc_context.h"
#include "dxvk_device.h"

#include <sstream>
#include <string>
#include <mutex>
#include <array>

namespace dxvk {
  static const bool g_useCustomCPUMemoryAllocator = false;

  // Utility
  static void NRCLoggerCallback(const char* message, nrc::LogLevel logLevel) {
    static std::mutex loggerMutex;

    // Make the logging thread-safe
    loggerMutex.lock();
    {
      std::string rtxMessage = std::string("[RTX Neural Radiance Cache] ") + std::string(message);
      switch (logLevel) {
      case nrc::LogLevel::Debug:
        Logger::debug(rtxMessage);
        break;
      case nrc::LogLevel::Info:
        Logger::info(rtxMessage);
        break;
      case nrc::LogLevel::Warning:
        Logger::warn(rtxMessage);
        break;
      case nrc::LogLevel::Error:
        Logger::err(rtxMessage);
        break;
      }
    }
    loggerMutex.unlock();
  }

  static void NRCMemoryEventsCallback(
    nrc::MemoryEventType eventType,
    size_t size,
    const char* bufferName) {
    static std::mutex loggerMutex;

    // Make the logging thread-safe
    loggerMutex.lock();
    {
      std::string message = "NRC SDK Memory Stats: ";

      switch (eventType) {
      case nrc::MemoryEventType::Allocation:
        message += std::to_string(size) + " bytes allocated (" + bufferName + ")\n";
        break;
      case nrc::MemoryEventType::Deallocation:
        message += std::to_string(size) + " bytes deallocated (" + bufferName + ")\n";
        break;
      case nrc::MemoryEventType::MemoryStats:
        message += std::to_string(size) + " bytes currently allocated in total\n";
        break;
      }

#ifdef _DEBUG
      OutputDebugStringA(message.c_str());
#endif
    }
    loggerMutex.unlock();
  }

  static void* NRCCustomAllocatorCallback(const size_t bytes) {
    return static_cast<void*>(new char[bytes]);
  }

  static void NRCCustomDeallocatorCallback(
    void* pointer,
    const size_t bytes) {
    delete[] pointer;
  }

  const char* getNrcStatusErrorMessage(nrc::Status status) {
    switch (status) {
    default:
      assert(0 && "Unexpected value");
      return "unknown status code.";
    case nrc::Status::OK: return "OK.";
    case nrc::Status::SDKVersionMismatch: return "SDK version in the header file doesn't match library version - need to update header file?";
    case nrc::Status::AlreadyInitialized: return "You're trying to initialize NRC SDK multiple times, please deinitialize old instance first.";
    case nrc::Status::SDKNotInitialized: return "SDK was not yet initialized.";
    case nrc::Status::InternalError: return "Unexpected condition occured during processing, see error log for more information.";
    case nrc::Status::MemoryNotProvided: return "Memory allocation within SDK is disabled, but necessary memory was not provided.";
    case nrc::Status::OutOfMemory: return "There is insufficient memory to create the GPU resource.";
    case nrc::Status::AllocationFailed: return "Memory allocation failed.";
    case nrc::Status::ErrorParsingJSON: return "Provided JSON string is malformed.";
    case nrc::Status::WrongParameter: return "Parameter provided to the SDK API call was invalid.";
    case nrc::Status::UnsupportedDriver: return "Installed driver version is not supported.";
    case nrc::Status::UnsupportedHardware: return "GPU Device is not supported.";
    }
  }

  NrcContext::NrcContext(DxvkDevice& device, const Configuration& config)
    : CommonDeviceObject(&device), m_isDebugBufferRequired(config.debugBufferIsRequired) {
  }

  NrcContext::~NrcContext() {
    // Wait for idle to make sure none, if any, previous NRC resources are in use
    device()->waitForIdle();

    if (m_nrcContext) {
      nrc::Status status = nrc::vulkan::Context::Destroy(*m_nrcContext);
      m_nrcContext = nullptr;
      if (status != nrc::Status::OK) {
        ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] Failed to destroy NRC context. Reason: ", getNrcStatusErrorMessage(status))));
        return;
      }
    }

    // Shutdown the NRC Library
    nrc::vulkan::Shutdown();
  }

  bool NrcContext::checkIsSupported(const DxvkDevice* device) {
    assert((s_hasCheckedNrcSupport || device) && "The function has to be called with a valid device pointer for the first call.");
    if (s_hasCheckedNrcSupport || !device) {
      return s_isNrcSupported;
    }

    s_isNrcSupported = true;

    struct ExtensionSupportCapability {
      const char* name;
      const bool isSupported;
    };

    // Checks if all required extensions are supported in the capabilities vector
    auto checkSupport = [&](const uint32_t numRequiredExtensions, const char* const*& requiredExtensions, const auto& caps) {
      // Check all required extensions
      for (uint32_t i = 0; i < numRequiredExtensions; i++) {
        const char* extension = requiredExtensions[i];
        // Check if an extension is active in the capabilities vector
        uint32_t iCap = 0;
        for (; iCap < caps.size(); iCap++) {
          const ExtensionSupportCapability& cap = caps[iCap];
          if (strcmp(extension, cap.name) == 0) {
            if (!cap.isSupported) {
              Logger::err(str::format("[RTX Neural Radiance Cache] Required extension not supported: ", cap.name));
              s_isNrcSupported = false;
            }
            break;
          }
        }

        // Unhandled extension
        if (iCap > caps.size()) {
          assert(0 && "Uknown extension requested");
          Logger::err(str::format("[RTX Neural Radiance Cache] Unknown extension requested: ", extension));
          s_isNrcSupported = false;
        }
      }
    };

    const DxvkInstance& instance = *device->instance().ptr();

    const std::array extensionCaps{
      ExtensionSupportCapability{ "VK_NVX_binary_import", device->extensions().nvxBinaryImport },
      ExtensionSupportCapability{ "VK_NVX_image_view_handle", device->extensions().nvxImageViewHandle },
      ExtensionSupportCapability{ "VK_EXT_memory_budget", device->extensions().extMemoryBudget },
      ExtensionSupportCapability{ "VK_KHR_buffer_device_address", device->extensions().khrBufferDeviceAddress },
      ExtensionSupportCapability{ "VK_EXT_scalar_block_layout", static_cast<bool>(device->features().vulkan12Features.scalarBlockLayout) },
      ExtensionSupportCapability{ "VK_KHR_get_physical_device_properties2", instance.extensions().khrDeviceProperties2 },
      ExtensionSupportCapability{ "VK_KHR_uniform_buffer_standard_layout", static_cast<bool>(device->features().vulkan12Features.uniformBufferStandardLayout) },
    };

    const char* const* requiredExtensions;
    checkSupport(nrc::vulkan::GetVulkanDeviceExtensions(requiredExtensions), requiredExtensions, extensionCaps);
    checkSupport(nrc::vulkan::GetVulkanInstanceExtensions(requiredExtensions), requiredExtensions, extensionCaps);
    checkSupport(nrc::vulkan::GetVulkanDeviceFeatures(requiredExtensions), requiredExtensions, extensionCaps);

    // Check against driver version requirements
    if (s_isNrcSupported) {
      const VkPhysicalDeviceProperties& deviceProperties = device->adapter()->deviceProperties();

      if (deviceProperties.vendorID != static_cast<uint32_t>(DxvkGpuVendor::Nvidia)) {
        s_isNrcSupported = false;
      } else {
        // Need 565.90+ driver to support CUDA runtime in NRC SDK
        const uint32_t nrcMinSupportedMajor = 565;
        const uint32_t nrcMinSupportedMinor = 90;

        const uint32_t nrcMinSupportedDriver = VK_MAKE_API_VERSION(0, nrcMinSupportedMajor, nrcMinSupportedMinor, 0);
        const uint32_t currentDriverVersion = deviceProperties.driverVersion;

        if (currentDriverVersion < nrcMinSupportedDriver) {

          const std::string currentDriverVersionString = getDriverVersionString(currentDriverVersion);
          Logger::info(str::format("[RTX Neural Radiance Cache] Incompatible driver installed:\n"
                                  "\tInstalled: ", currentDriverVersionString.c_str(), "\n",
                                  "\tRequired: ", nrcMinSupportedMajor, ".", nrcMinSupportedMinor, "+"));
          s_isNrcSupported = false;
        }
      }
    }

    Logger::info(str::format("[RTX info] Neural Radiance Cache: ", s_isNrcSupported ? "supported" : "not supported"));
    s_hasCheckedNrcSupport = true;

    return s_isNrcSupported;
  }

  nrc::Status NrcContext::initialize() {
    
    m_nrcContextSettings = nrc::ContextSettings {};

    nrc::GlobalSettings globalSettings;

    // First, set logger callbacks for catching messages from NRC
    globalSettings.loggerFn = &NRCLoggerCallback;
    globalSettings.memoryLoggerFn = &NRCMemoryEventsCallback;

    // Optionally, use custom CPU memory provided by the user application
    if (g_useCustomCPUMemoryAllocator) {
      globalSettings.allocatorFn = &NRCCustomAllocatorCallback;
      globalSettings.deallocatorFn = &NRCCustomDeallocatorCallback;
    }

    // Set to false, we allocate gpu memory
    globalSettings.enableGPUMemoryAllocation = false;

    // Only enable debug buffers in development and not production
    globalSettings.enableDebugBuffers = m_isDebugBufferRequired;
    globalSettings.maxNumFramesInFlight = kMaxFramesInFlight;

    globalSettings.depsDirectoryPath = !NrcCtxOptions::cudaDllDepsDirectoryPath().empty() ? NrcCtxOptions::cudaDllDepsDirectoryPath().c_str() : nullptr;

    // Initialize the NRC Library
    nrc::Status status = nrc::vulkan::Initialize(globalSettings);
    if (status != nrc::Status::OK) {
      ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] Failed to initialize NRC. Reason: ", getNrcStatusErrorMessage(status))));
      return status;
    }

    // Create an NRC Context
    VkDevice nativeDevice = m_device->handle();
    VkPhysicalDevice nativeGPU = m_device->adapter()->handle();
    VkInstance apiInstance = m_device->instance()->handle();

    if (nativeDevice != nullptr && nativeGPU != nullptr) {
      assert(m_nrcContext == nullptr);
      status = nrc::vulkan::Context::Create(nativeDevice, nativeGPU, apiInstance, m_nrcContext);
      if (status != nrc::Status::OK) {
        ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] Failed to create NRC context. Reason: ", getNrcStatusErrorMessage(status))));
        return status;
      }
    }

    return nrc::Status::OK;
  }

  Rc<DxvkBuffer>& NrcContext::getBuffer(nrc::BufferIdx nrcResourceType) {
    return m_buffers[static_cast<size_t>(nrcResourceType)];
  }

  DxvkBufferSlice NrcContext::getBufferSlice(
    DxvkContext& ctx,
    nrc::BufferIdx nrcResourceType) {
    Rc<DxvkBuffer>& buffer = m_buffers[static_cast<size_t>(nrcResourceType)];

    if (buffer != nullptr) {
      ctx.getCommandList()->trackResource<DxvkAccess::Read>(buffer);
      return DxvkBufferSlice(buffer);
    }
    else {
      return DxvkBufferSlice(nullptr, 0, 0);
    }
  }

  VkBufferMemoryBarrier NrcContext::createVkBufferMemoryBarrier(
    nrc::BufferIdx bufferIndex,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask) {

    VkBufferMemoryBarrier barrier;
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext = nullptr;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.srcQueueFamilyIndex = 0;
    barrier.dstQueueFamilyIndex = 0;
    barrier.offset = 0;
    barrier.buffer = m_nrcBuffers[bufferIndex].resource;
    barrier.size = m_nrcBuffers[bufferIndex].allocatedSize;

    return barrier;
  }

  bool NrcContext::onFrameBegin(
    DxvkContext& ctx,
    const nrc::ContextSettings& config,
    const nrc::FrameSettings& frameSettings,
    bool* hasCacheBeenReset) {

    if (config != m_nrcContextSettings) {
      // Configuration has changed
      m_nrcContextSettings = config;

      const bool forceAllocate = false;

      // Ensure our buffers are valid for the new configuration.
      // If so, pass them to the NRC Context
      allocateOrCheckAllResources(forceAllocate);

      // [REMIX-3810] WAR to clean the input buffers after their creation to avoid occasional corruption
      // when changing resolutions
      // Clear generated buffers
      {
        clearBuffer(ctx, nrc::BufferIdx::Counter, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::QueryPathInfo, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::TrainingPathInfo, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::TrainingPathVertices, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::TrainingRadiance, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::TrainingRadianceParams, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::QueryRadiance, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        clearBuffer(ctx, nrc::BufferIdx::QueryRadianceParams, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
        if (isDebugBufferRequired()) {
          clearBuffer(ctx, nrc::BufferIdx::DebugTrainingPathInfo, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }
      }

      // Configure network
      nrc::Status status = m_nrcContext->Configure(m_nrcContextSettings, &m_nrcBuffers,
        NrcCtxOptions::enableCustomNetworkConfig()
        ? "CustomNetworkConfig.json"
        : nullptr);

      if (status != nrc::Status::OK) {
        Logger::err(str::format("[RTX Neural Radiance Cache] Configure call failed. Reason: ", getNrcStatusErrorMessage(status)));
        return false;
      } else {
        Logger::debug(str::format("[RTX Neural Radiance Cache] Configure call succeeded."));
      }

      m_nrcContextSettings.requestReset = false;

      // Assume NRC cache got reset on Configure() since it often does when the config doesn't match
      *hasCacheBeenReset = true;
    } else {
      *hasCacheBeenReset = false;
    }

    VkCommandBuffer cmdBuffer = ctx.getCommandList()->getCmdBuffer(dxvk::DxvkCmdBuffer::ExecBuffer);
    if (cmdBuffer) {
      nrc::Status status = m_nrcContext->BeginFrame(cmdBuffer, frameSettings);
      if (status != nrc::Status::OK) {
        ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] BeginFrame call failed. Reason: ", getNrcStatusErrorMessage(status))));
        return false;
      }
    }

    return true;
  }

  void NrcContext::endFrame() {
    const auto nativeCmdQueue = m_device->queues().graphics.queueHandle;

    // Note: Unlike the rest of NRC's operations which submit to a DXVK command buffer we provide to it, the EndFrame call takes a Vulkan queue directly
    // so that it can submit a fence to track the end of the frame on the CPU properly. This is a problem however as DXVK submits to queues on
    // the dxvk-submit thread while this end frame call is done on the dxvk-cs thread, and Vulkan requires host access to queue submission to be
    // externally synchronized, which this code was not doing previously. This caused the queue to be submitted to on multiple threads at once in
    // rare cases causing potential issues.
    //
    // To fix this, we call DXVK's lockSubmissionUnsynchronized function which ensures only one thread is submitting to the queue at a time via a mutex.
    // Do note that unlike the usual lockSubmission function this function variant does not synchronize the calling thread with previously queued submissions
    // (done by lockSubmission by blocking until the dxvk-submit thread's own queue of work is empty). This is not ideal since the rest of NRC is using DXVK
    // command buffers and it may be important to ensure that all this NRC work queued for submission is submitted to Vulkan before this EndFrame fence is
    // inserted, as otherwise NRC may think the end of the frame is in the wrong location and potentially try to free a resource while it is in use. The main
    // risk with synchronizing however is that the synchronize function also synchronizes with DLSS-FG which may cause performance issues, though a variant of that
    // function could be made if simply synchronizing with dxvk-submit is desirable but not DLSS-FG. Additionally if the dxvk-submit thread takes significant
    // amounts of time to submit work to Vulkan synchronizing with that thread in general may cause a performance impact due to stalling any work on the dxvk-cs
    // thread. In practice though queue submission is generally fairly fast and the dxvk-submit thread is idle more often than not. In fact during Remix's end
    // frame phase the dxvk-submit thread is typically not doing anything which is likely why this bug was so rare to begin with, as this submission only
    // overlaps with actual work on the dxvk-submit thread very rarely.
    //
    // In conclusion:
    // - Locking submission here ensures proper Vulkan host synchronization of vkQueueSubmit between NRC's EndFrame call and the DXVK's dxvk-submit thread.
    // - An "unsynchronized" version of this locking function is used to avoid potential performance regressions with DLSS-FG, even though it is probably
    //   proper to be ensuring this EndFrame queue submission is done after all work on the dxvk-submit thread has been processed, so this may need to be
    //   changed in the future (this code should not add any new bugs though as it was never synchronized to begin with).
    // - NRC should ideally change this API to avoid doing submissions internally to avoid the need for us to lock submissions or synchronize with the
    //   dxvk-submit thread to begin with as this will never be ideal. If anything it should allow us to submit the fence ourselves on the dxvk-submit
    //   thread and pass the fence in directly or something.
    m_device->lockSubmissionUnsynchronized();
    const nrc::Status status = m_nrcContext->EndFrame(nativeCmdQueue);
    m_device->unlockSubmission();

    if (status != nrc::Status::OK) {
      ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] EndFrame call failed. Reason: ", getNrcStatusErrorMessage(status))));
    }
  }

  float NrcContext::queryAndTrain(
    DxvkContext& ctx,
    bool calculateTrainingLoss) {
    VkCommandBuffer cmdBuffer = ctx.getCommandList()->getCmdBuffer(dxvk::DxvkCmdBuffer::ExecBuffer);
   
    float trainingLoss = 0.0f;
    nrc::Status status = m_nrcContext->QueryAndTrain(cmdBuffer, (calculateTrainingLoss ? &trainingLoss : nullptr));
    if (status != nrc::Status::OK) {
      ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] QueryAndTrain call failed. Reason: ", getNrcStatusErrorMessage(status))));
    }

    return trainingLoss;
  }

  void NrcContext::resolve(
    DxvkContext& ctx,
    const Rc<DxvkImageView>& outputImage) {
    VkCommandBuffer cmdBuffer = ctx.getCommandList()->getCmdBuffer(dxvk::DxvkCmdBuffer::ExecBuffer);
    nrc::Status status = m_nrcContext->Resolve(cmdBuffer, outputImage->handle());
    if (status != nrc::Status::OK) {
      ONCE(Logger::err(str::format("[RTX Neural Radiance Cache] BeginFrame call failed. Reason: ", getNrcStatusErrorMessage(status))));
    }
  }

  bool NrcContext::isDebugBufferRequired() const     {
    return m_isDebugBufferRequired;
  }

  VkDeviceSize NrcContext::getCurrentMemoryConsumption() const {
    VkDeviceSize totalAllocatedMemory = 0;

    for (const nrc::vulkan::BufferInfo& buffer : m_nrcBuffers.buffers) {
      totalAllocatedMemory += buffer.allocatedSize;
    }
    
    return totalAllocatedMemory;
  }

  void NrcContext::populateShaderConstants(NrcConstants& outConstants) const {
    m_nrcContext->PopulateShaderConstants(outConstants);
  }

  void NrcContext::tryReallocateBuffer(
    Rc<DxvkBuffer>& buffer, 
    nrc::vulkan::BufferInfo& bufferInfo, 
    const nrc::AllocationInfo& allocationInfo) {
    VkDeviceSize bufferSize = allocationInfo.elementSize * allocationInfo.elementCount;

    // Size hasn't changed, early exit
    if (buffer != nullptr && buffer->info().size == bufferSize) {
      return;
    }

    buffer = nullptr;
    bufferInfo = {};

    if (bufferSize == 0) {
      return;
    }

    // Set up buffer create info
    DxvkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    {
      bufferCreateInfo.usage = m_nrcContext->GetBufferUsageFlags(allocationInfo);
      bufferCreateInfo.stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;   // ToDo - should narrow it down?
      // Note: Transfer bit needed for fill operations used within NRC.
      bufferCreateInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      bufferCreateInfo.size = bufferSize;

      if (allocationInfo.allowUAV) {
        bufferCreateInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      }

      // For clear support
      // ToDo limit it to if (allocationInfo.useReadbackHeap) like it's done in NRC sdk
      bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    // Create a new buffer
    buffer = m_device->createBuffer(bufferCreateInfo, memoryFlags, DxvkMemoryStats::Category::RTXBuffer, "NRC buffer");

    // Fill out buffer info
    bufferInfo.resource = buffer->getSliceHandle().handle;
    bufferInfo.allocatedSize = buffer->getSliceHandle().length;
    bufferInfo.allocatedOffset = buffer->getSliceHandle().offset;
    bufferInfo.memory = buffer->getBufferHandle().memory.memory();
    bufferInfo.deviceAddress = buffer->getDeviceAddress();
  }

  void NrcContext::allocateOrCheckAllResources(bool forceAllocate) {
    nrc::BuffersAllocationInfo bufferAllocations;
    nrc::vulkan::Context::GetBuffersAllocationInfo(m_nrcContextSettings, bufferAllocations);

    for (uint32_t i = 0; i < static_cast<uint32_t>(nrc::BufferIdx::Count); i++) {
      if (forceAllocate) {
        m_buffers[i] = nullptr;
      }

      tryReallocateBuffer(m_buffers[i], m_nrcBuffers[static_cast<nrc::BufferIdx>(i)], bufferAllocations[static_cast<nrc::BufferIdx>(i)]);
    }
  }

  void NrcContext::clearBuffer(
    DxvkContext& ctx,
    nrc::BufferIdx nrcResourceType,
    VkPipelineStageFlagBits dstStageMask,
    VkAccessFlags dstAccessMask) {
    Rc<DxvkBuffer>& buffer = m_buffers[static_cast<uint32_t>(nrcResourceType)];

    if (buffer != nullptr) {
      ctx.clearBuffer(buffer, 0, buffer->info().size, 0);
    }
  }
}