/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_fsr_framegen.h"
#include "rtx_camera.h"
#include "../dxvk_device.h"
#include "../dxvk_context.h"
#include "../dxvk_cmdlist.h"

// FFX SDK includes - only in cpp file
#undef FFX_API_ENTRY
#define FFX_API_ENTRY __declspec(dllimport)

#pragma warning(push)
#pragma warning(disable: 4005)

#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_framegeneration.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>
#include <ffx_api/vk/ffx_api_vk.h>

#pragma warning(pop)
#undef FFX_API_ENTRY
#define FFX_API_ENTRY

#include <vector>
#include <mutex>
#include <map>
#include <unordered_map>

namespace dxvk {

  namespace {
    struct FsrPresentColorOverrideEntry {
      Rc<DxvkImage> image;
      FfxApiResource resource = FfxApiResource({});
    };

    std::mutex g_fsrFrameGenCallbackStateMutex;
    std::unordered_map<ffxContext, std::map<uint64_t, FsrPresentColorOverrideEntry>> g_fsrFrameGenPresentColorOverride;

    FfxApiResource createFfxResourceFromImage(const Rc<DxvkImage>& image, uint32_t state, uint32_t additionalUsages = FFX_API_RESOURCE_USAGE_READ_ONLY) {
      if (image == nullptr)
        return FfxApiResource({});

      VkImageCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      createInfo.imageType = VK_IMAGE_TYPE_2D;
      createInfo.format = image->info().format;
      createInfo.extent = image->info().extent;
      createInfo.mipLevels = image->info().mipLevels;
      createInfo.arrayLayers = image->info().numLayers;
      createInfo.samples = image->info().sampleCount;
      createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      createInfo.usage = image->info().usage;
      createInfo.flags = image->info().flags;

      FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(
        image->handle(),
        createInfo,
        additionalUsages);

      return ffxApiGetResourceVK(
        reinterpret_cast<void*>(image->handle()),
        desc,
        state);
    }
  }

  // Helper to access config
  static ffx::ConfigureDescFrameGeneration& asFrameGenConfig(void* cfg) {
    return *reinterpret_cast<ffx::ConfigureDescFrameGeneration*>(cfg);
  }

  static VkSwapchainCreateInfoKHR buildSwapchainCreateInfoFromPresenter(
    const vk::PresenterInfo& info,
    VkSurfaceKHR surface,
    VkSwapchainKHR oldSwapchain)
  {
    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.pNext = nullptr;
    swapInfo.flags = 0;
    swapInfo.surface = surface;
    swapInfo.minImageCount = info.imageCount;
    swapInfo.imageFormat = info.format.format;
    swapInfo.imageColorSpace = info.format.colorSpace;
    swapInfo.imageExtent = info.imageExtent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                        | VK_IMAGE_USAGE_STORAGE_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.queueFamilyIndexCount = 0;
    swapInfo.pQueueFamilyIndices = nullptr;
    swapInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = info.presentMode;
    swapInfo.clipped = VK_TRUE;
    swapInfo.oldSwapchain = oldSwapchain;
    return swapInfo;
  }

  // Static callback for frame generation dispatch  
  // pUserCtx is the ffx::Context value (void*) for the frame gen context
  static ffxReturnCode_t frameGenCallback(ffxDispatchDescFrameGeneration* params, void* pUserCtx) {
    // pUserCtx IS the context (void*), pass its address as required by ffxDispatch
    ffxContext ctx = static_cast<ffxContext>(pUserCtx);

    bool hasPathTracedOverride = false;
    uint64_t selectedFrameId = 0;

    {
      std::lock_guard<std::mutex> lock(g_fsrFrameGenCallbackStateMutex);
      auto ctxIt = g_fsrFrameGenPresentColorOverride.find(ctx);
      if (ctxIt != g_fsrFrameGenPresentColorOverride.end()) {
        auto& perFrame = ctxIt->second;
        auto frameIt = perFrame.find(params->frameID);

        // If exact match is unavailable, prefer the newest prepared frame that is
        // not newer than the callback frame ID.
        if (frameIt == perFrame.end() && !perFrame.empty()) {
          auto upper = perFrame.upper_bound(params->frameID);
          if (upper != perFrame.begin())
            frameIt = std::prev(upper);
        }

        if (frameIt != perFrame.end()) {
          selectedFrameId = frameIt->first;

          // Refuse large frame deltas since they produce severe FG artifacts.
          const uint64_t maxAcceptedDelta = 2;
          if (params->frameID >= selectedFrameId && (params->frameID - selectedFrameId) > maxAcceptedDelta) {
            Logger::warn(str::format(
              "FSR FG: Rejecting stale presentColor override. callbackFrame=", params->frameID,
              " selectedFrame=", selectedFrameId,
              " delta=", (params->frameID - selectedFrameId)));
            return FFX_API_RETURN_ERROR_PARAMETER;
          }

          const auto& entry = frameIt->second;
          if (entry.image != nullptr && entry.resource.resource != nullptr) {
            params->presentColor = entry.resource;
            hasPathTracedOverride = true;
          }

          // Consume up to and including the used frame to avoid stale buildup.
          perFrame.erase(perFrame.begin(), std::next(frameIt));
        }
      }
    }

    // If no explicit override is bound for this frame, allow the SDK-provided
    // presentColor path (proxy swapchain path) to proceed so frame generation
    // does not stall.
    if (!hasPathTracedOverride) {
      if (params->presentColor.resource == nullptr) {
        Logger::warn(str::format("FSR FG: Missing presentColor for frame ", params->frameID, ", skipping generation dispatch"));
        return FFX_API_RETURN_ERROR_PARAMETER;
      }
    }

    return ffxDispatch(&ctx, &params->header);
  }

  DxvkFSRFrameGen::DxvkFSRFrameGen(DxvkDevice* device)
    : CommonDeviceObject(device) {
    // Allocate frame gen config
    m_frameGenConfig = new ffx::ConfigureDescFrameGeneration{};
    Logger::info("FSR FG: DxvkFSRFrameGen created");
  }

  void DxvkFSRFrameGen::onDestroy() {
    Logger::info("FSR FG: onDestroy called");
    destroyContexts();
    
    if (m_frameGenConfig) {
      delete reinterpret_cast<ffx::ConfigureDescFrameGeneration*>(m_frameGenConfig);
      m_frameGenConfig = nullptr;
    }
  }

  bool DxvkFSRFrameGen::supportsFSRFrameGen() {
    // FSR 3 Frame Generation requires:
    // 1. Vulkan 1.1+ with VK_KHR_swapchain
    // 2. AMD or other GPU with compute capability
    // 3. FFX SDK libraries available
    
    // For now, return true and let runtime initialization fail gracefully
    // if requirements aren't met
    return true;
  }


  void DxvkFSRFrameGen::setDisplaySize(uint2 displaySize) {
    if (m_displaySize.x != displaySize.x || m_displaySize.y != displaySize.y) {
      m_displaySize = displaySize;
      // Proxy swapchain path is optional; keep swapchain size in sync with presenter size
      // so generationRect remains valid when proxy hooks are bypassed.
      m_swapchainSize = displaySize;
      Logger::info(str::format("FSR FG: Display size set to ", displaySize.x, "x", displaySize.y));
      
      // If contexts already exist, we need to recreate them for the new size
      if (m_frameGenContextCreated) {
        // Mark for recreation on next frame
        // The actual recreation happens in prepareFrameGeneration
      }
    }
  }

  void DxvkFSRFrameGen::createSwapchainProxy(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkSwapchainKHR* pSwapchain,
    const VkSwapchainCreateInfoKHR* pCreateInfo)
  {
    if (m_swapchainContextCreated) {
      Logger::warn("FSR FG: Swapchain proxy already created");
      return;
    }

    Logger::info("FSR FG: Creating swapchain proxy...");
      m_asyncWorkloadsSupported = false;
    const VkSwapchainKHR originalSwapchain = pSwapchain ? *pSwapchain : VK_NULL_HANDLE;

    // Store the swapchain size for use in generationRect
    m_swapchainSize = { pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height };

    // Get queues from device
    const DxvkDeviceQueueSet& queues = m_device->queues();

    // Debug: Log all input parameters
    Logger::info(str::format("FSR FG: physicalDevice=", reinterpret_cast<uintptr_t>(physicalDevice)));
    Logger::info(str::format("FSR FG: device=", reinterpret_cast<uintptr_t>(device)));
    Logger::info(str::format("FSR FG: pSwapchain=", reinterpret_cast<uintptr_t>(pSwapchain), 
      ", *pSwapchain=", reinterpret_cast<uintptr_t>(pSwapchain ? *pSwapchain : VK_NULL_HANDLE)));
    Logger::info(str::format("FSR FG: createInfo surface=", reinterpret_cast<uintptr_t>(pCreateInfo->surface),
      ", imageExtent=", pCreateInfo->imageExtent.width, "x", pCreateInfo->imageExtent.height,
      ", format=", static_cast<int>(pCreateInfo->imageFormat)));
    // AMD FidelityFX SDK 1.1.4 requires ALL 4 QUEUES TO BE SEPARATE (different VkQueue handles)
    // If any queues are the same, init() returns VK_ERROR_INITIALIZATION_FAILED (error code 3)
    // Log available queues for debugging
    Logger::info(str::format("FSR FG: Available queues:"));
    Logger::info(str::format("  graphics: queue=", reinterpret_cast<uintptr_t>(queues.graphics.queueHandle), ", family=", queues.graphics.queueFamily));
    Logger::info(str::format("  asyncCompute: queue=", reinterpret_cast<uintptr_t>(queues.asyncCompute.queueHandle), ", family=", queues.asyncCompute.queueFamily));
    Logger::info(str::format("  present: queue=", reinterpret_cast<uintptr_t>(queues.present.queueHandle), ", family=", queues.present.queueFamily));
    Logger::info(str::format("  fsrPresent: queue=", reinterpret_cast<uintptr_t>(queues.fsrPresent.queueHandle), ", family=", queues.fsrPresent.queueFamily));
    Logger::info(str::format("  imageAcquire: queue=", reinterpret_cast<uintptr_t>(queues.imageAcquire.queueHandle), ", family=", queues.imageAcquire.queueFamily));
    Logger::info(str::format("  transfer: queue=", reinterpret_cast<uintptr_t>(queues.transfer.queueHandle), ", family=", queues.transfer.queueFamily));

    auto queueCollides = [](VkQueue a, VkQueue b) {
      return a != VK_NULL_HANDLE && b != VK_NULL_HANDLE && a == b;
    };

    auto queueSupportsPresent = [&](const DxvkDeviceQueue& queueInfo) {
      if (queueInfo.queueHandle == VK_NULL_HANDLE)
        return false;

      VkBool32 supportsPresent = VK_FALSE;
      VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
        physicalDevice,
        queueInfo.queueFamily,
        pCreateInfo->surface,
        &supportsPresent);

      if (result != VK_SUCCESS)
        return false;

      return supportsPresent == VK_TRUE;
    };

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    auto queueFamilyHasFlags = [&](uint32_t familyIndex, VkQueueFlags requiredFlags) {
      if (familyIndex >= queueFamilies.size())
        return false;

      return (queueFamilies[familyIndex].queueFlags & requiredFlags) == requiredFlags;
    };

    const DxvkDeviceQueue gameQueueInfo = queues.graphics;
    DxvkDeviceQueue presentQueueInfo;
    DxvkDeviceQueue imageAcquireQueueInfo;
    DxvkDeviceQueue asyncQueueInfo;

    const DxvkDeviceQueue* presentCandidates[] = {
      &queues.fsrPresent,
      &queues.present,
      &queues.transfer,
      &queues.asyncCompute,
    };

    for (const DxvkDeviceQueue* candidate : presentCandidates) {
      if (candidate->queueHandle == VK_NULL_HANDLE)
        continue;

      if (queueCollides(candidate->queueHandle, gameQueueInfo.queueHandle))
        continue;

      if (!queueSupportsPresent(*candidate))
        continue;

      presentQueueInfo = *candidate;
      break;
    }

    if (presentQueueInfo.queueHandle == VK_NULL_HANDLE) {
      Logger::err("FSR FG: Cannot create swapchain proxy - no dedicated present-capable queue available");
      Logger::err("FSR FG: Falling back to non-frame-generation present path");
      return;
    }

    const DxvkDeviceQueue* imageAcquireCandidates[] = {
      &queues.imageAcquire,
      &queues.transfer,
      &queues.present,
      &queues.asyncCompute,
    };

    for (const DxvkDeviceQueue* candidate : imageAcquireCandidates) {
      if (candidate->queueHandle == VK_NULL_HANDLE)
        continue;

      if (queueCollides(candidate->queueHandle, gameQueueInfo.queueHandle))
        continue;

      if (queueCollides(candidate->queueHandle, presentQueueInfo.queueHandle))
        continue;

      imageAcquireQueueInfo = *candidate;
      break;
    }

    if (imageAcquireQueueInfo.queueHandle == VK_NULL_HANDLE) {
      Logger::err("FSR FG: Cannot create swapchain proxy - no dedicated image acquire queue available");
      Logger::err("FSR FG: Falling back to non-frame-generation present path");
      return;
    }

    const DxvkDeviceQueue* asyncCandidates[] = {
      &queues.asyncCompute,
      &queues.transfer,
      &queues.present,
      &queues.imageAcquire,
    };

    for (const DxvkDeviceQueue* candidate : asyncCandidates) {
      if (candidate->queueHandle == VK_NULL_HANDLE)
        continue;

      if (queueCollides(candidate->queueHandle, gameQueueInfo.queueHandle))
        continue;

      if (queueCollides(candidate->queueHandle, presentQueueInfo.queueHandle))
        continue;

      if (queueCollides(candidate->queueHandle, imageAcquireQueueInfo.queueHandle))
        continue;

      if (!queueFamilyHasFlags(candidate->queueFamily, VK_QUEUE_COMPUTE_BIT))
        continue;

      asyncQueueInfo = *candidate;
      break;
    }

    if (asyncQueueInfo.queueHandle == VK_NULL_HANDLE) {
      Logger::warn("FSR FG: Dedicated async compute queue unavailable, running without async workloads");
      asyncQueueInfo.queueFamily = gameQueueInfo.queueFamily;
    }

    VkQueue gameQueue = gameQueueInfo.queueHandle;
    VkQueue asyncQueue = asyncQueueInfo.queueHandle;
    VkQueue presentQueue = presentQueueInfo.queueHandle;
    VkQueue acquireQueue = imageAcquireQueueInfo.queueHandle;

    if (queueCollides(gameQueue, asyncQueue) ||
        queueCollides(gameQueue, presentQueue) ||
        queueCollides(gameQueue, acquireQueue) ||
        queueCollides(asyncQueue, presentQueue) ||
        queueCollides(asyncQueue, acquireQueue) ||
        queueCollides(presentQueue, acquireQueue)) {
      Logger::err(str::format("FSR FG: Queue collision detected - all 4 queues must be unique!"));
      Logger::err(str::format("  game=", reinterpret_cast<uintptr_t>(gameQueue),
        ", async=", reinterpret_cast<uintptr_t>(asyncQueue),
        ", present=", reinterpret_cast<uintptr_t>(presentQueue),
        ", acquire=", reinterpret_cast<uintptr_t>(acquireQueue)));
      return;
    }

    ffx::CreateContextDescFrameGenerationSwapChainVK createSwapChainDesc{};
    createSwapChainDesc.physicalDevice = physicalDevice;
    createSwapChainDesc.device = device;
    createSwapChainDesc.swapchain = pSwapchain;
    createSwapChainDesc.createInfo = *pCreateInfo;

    // Game queue - main graphics queue
    createSwapChainDesc.gameQueue.queue = gameQueueInfo.queueHandle;
    createSwapChainDesc.gameQueue.familyIndex = gameQueueInfo.queueFamily;
    createSwapChainDesc.gameQueue.submitFunc = nullptr;

    // Async compute queue can be null if async workloads are disabled.
    createSwapChainDesc.asyncComputeQueue.queue = asyncQueueInfo.queueHandle;
    createSwapChainDesc.asyncComputeQueue.familyIndex = asyncQueueInfo.queueFamily;
    createSwapChainDesc.asyncComputeQueue.submitFunc = nullptr;

    // Present queue must be dedicated and support this swapchain surface.
    createSwapChainDesc.presentQueue.queue = presentQueueInfo.queueHandle;
    createSwapChainDesc.presentQueue.familyIndex = presentQueueInfo.queueFamily;
    createSwapChainDesc.presentQueue.submitFunc = nullptr;

    // Image acquire queue must be dedicated from game and present.
    createSwapChainDesc.imageAcquireQueue.queue = imageAcquireQueueInfo.queueHandle;
    createSwapChainDesc.imageAcquireQueue.familyIndex = imageAcquireQueueInfo.queueFamily;
    createSwapChainDesc.imageAcquireQueue.submitFunc = nullptr;

    Logger::info(str::format("FSR FG: Using queues for FFX SDK: game=", gameQueueInfo.queueFamily,
      ", present=", presentQueueInfo.queueFamily,
      ", imageAcquire=", imageAcquireQueueInfo.queueFamily,
      ", async=", asyncQueueInfo.queueFamily,
      ", asyncEnabled=", asyncQueueInfo.queueHandle != VK_NULL_HANDLE));

    // m_swapChainContext is void* which IS ffx::Context (typedef void*)
    // CreateContext takes a reference to fill in the context handle
    ffx::Context& swapChainCtx = *reinterpret_cast<ffx::Context*>(&m_swapChainContext);
    
    ffx::ReturnCode retCode = ffx::CreateContext(swapChainCtx, nullptr, createSwapChainDesc);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to create swapchain context: ", static_cast<int>(retCode)));
      return;
    }

    // Query replacement Vulkan functions
    ffx::QueryDescSwapchainReplacementFunctionsVK replacementFunctions{};
    retCode = ffx::Query(swapChainCtx, replacementFunctions);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to query replacement functions: ", static_cast<int>(retCode)));

      // Roll back the swapchain handle before destroying the context.
      // Without this, the caller may retain a proxy handle that was just destroyed.
      if (pSwapchain)
        *pSwapchain = originalSwapchain;

      ffx::DestroyContext(swapChainCtx);
      m_swapChainContext = nullptr;
      return;
    }

    m_replacedAcquireNextImageKHR = replacementFunctions.pOutAcquireNextImageKHR;
    m_replacedQueuePresentKHR = replacementFunctions.pOutQueuePresentKHR;
    m_replacedGetSwapchainImagesKHR = replacementFunctions.pOutGetSwapchainImagesKHR;

    m_asyncWorkloadsSupported = asyncQueueInfo.queueHandle != VK_NULL_HANDLE;
    m_swapchainContextCreated = true;
    Logger::info("FSR FG: Swapchain proxy created successfully");
  }

  void DxvkFSRFrameGen::createFrameGenContext(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t displayWidth,
    uint32_t displayHeight,
    VkFormat backBufferFormat)
  {
    if (m_frameGenContextCreated) {
      Logger::warn("FSR FG: Frame gen context already created");
      return;
    }

    Logger::info(str::format("FSR FG: Creating frame gen context ", displayWidth, "x", displayHeight));

    // Create backend for Vulkan
    ffx::CreateBackendVKDesc backendDesc{};
    backendDesc.vkDevice = device;
    backendDesc.vkPhysicalDevice = physicalDevice;
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    // Create frame generation context
    ffx::CreateContextDescFrameGeneration createFg{};
    createFg.displaySize = { displayWidth, displayHeight };
    createFg.maxRenderSize = { displayWidth, displayHeight };

    // Flags: HDR, inverted depth (common in Remix)
    createFg.flags = 0;
    createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
    createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
    // Primary screen-space motion vectors are generated from jittered projections, so let FG cancel jitter internally.
    createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (m_asyncWorkloadsSupported)
      createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    // Map VkFormat to FfxSurfaceFormat
    createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    switch (backBufferFormat) {
      case VK_FORMAT_R8G8B8A8_UNORM:
      case VK_FORMAT_R8G8B8A8_SRGB:
        createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        break;
      case VK_FORMAT_B8G8R8A8_UNORM:
      case VK_FORMAT_B8G8R8A8_SRGB:
        createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
        break;
      case VK_FORMAT_R16G16B16A16_SFLOAT:
        createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        break;
      case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
        break;
      default:
        Logger::warn(str::format("FSR FG: Unknown VkFormat ", static_cast<int>(backBufferFormat)));
        break;
    }

    // m_frameGenContext is void* which IS ffx::Context (typedef void*)
    ffx::Context& frameGenCtx = *reinterpret_cast<ffx::Context*>(&m_frameGenContext);
    
    ffx::ReturnCode retCode = ffx::CreateContext(frameGenCtx, nullptr, createFg, backendDesc);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to create frame gen context: ", static_cast<int>(retCode)));
      return;
    }

    m_frameGenContextCreated = true;
    m_initialized = true;
    Logger::info("FSR FG: Frame gen context created successfully");
  }

  void DxvkFSRFrameGen::destroyContexts() {
    if (m_frameGenContextCreated && m_frameGenContext) {
      ffx::Context& frameGenCtx = *reinterpret_cast<ffx::Context*>(&m_frameGenContext);
      ffx::ConfigureDescFrameGeneration& config = asFrameGenConfig(m_frameGenConfig);

      {
        std::lock_guard<std::mutex> lock(g_fsrFrameGenCallbackStateMutex);
        g_fsrFrameGenPresentColorOverride.erase(static_cast<ffxContext>(m_frameGenContext));
      }
      
      // Disable frame gen before destroying
      config.frameGenerationEnabled = false;
      config.presentCallback = nullptr;
      config.HUDLessColor = FfxApiResource({});
      ffx::Configure(frameGenCtx, config);

      ffx::DestroyContext(frameGenCtx);
      m_frameGenContext = nullptr;
      m_frameGenContextCreated = false;
      Logger::info("FSR FG: Frame gen context destroyed");
    }

    if (m_swapchainContextCreated && m_swapChainContext) {
      ffx::Context& swapChainCtx = *reinterpret_cast<ffx::Context*>(&m_swapChainContext);
      ffx::DestroyContext(swapChainCtx);
      m_swapChainContext = nullptr;
      m_swapchainContextCreated = false;
      m_asyncWorkloadsSupported = false;
      
      m_replacedAcquireNextImageKHR = nullptr;
      m_replacedQueuePresentKHR = nullptr;
      m_replacedGetSwapchainImagesKHR = nullptr;
      m_swapchainSize = { 0, 0 };
      
      Logger::info("FSR FG: Swapchain context destroyed");
    }

    m_initialized = false;
    m_presentColorSource = nullptr;
    m_preparedForPresent.store(false, std::memory_order_release);
  }

  void DxvkFSRFrameGen::setFrameGenerationPresentColorSource(const Rc<DxvkImage>& presentColorSource) {
    m_presentColorSource = presentColorSource;
  }

  bool DxvkFSRFrameGen::registerPresentColorOverrideForFrame(const Rc<DxvkImage>& presentColorImage, uint64_t frameId) {
    if (!m_frameGenContextCreated || !m_frameGenContext || presentColorImage == nullptr)
      return false;

    const ffxContext ctx = static_cast<ffxContext>(m_frameGenContext);
    const FfxApiResource resource = createFfxResourceFromImage(
      presentColorImage,
      FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ,
      FFX_API_RESOURCE_USAGE_READ_ONLY);

    if (resource.resource == nullptr)
      return false;

    std::lock_guard<std::mutex> lock(g_fsrFrameGenCallbackStateMutex);
    FsrPresentColorOverrideEntry entry;
    entry.image = presentColorImage;
    entry.resource = resource;
    auto& perFrame = g_fsrFrameGenPresentColorOverride[ctx];
    perFrame[frameId] = std::move(entry);

    // Keep only a small recent window to avoid unbounded growth.
    if (perFrame.size() > 8) {
      const uint64_t minFrameToKeep = frameId > 4 ? frameId - 4 : 0;
      for (auto it = perFrame.begin(); it != perFrame.end(); ) {
        if (it->first < minFrameToKeep)
          it = perFrame.erase(it);
        else
          ++it;
      }
    }

    return true;
  }

  void DxvkFSRFrameGen::configureFrameGeneration(
    VkSwapchainKHR swapchain,
    uint64_t frameId,
    bool enabled)
  {
    if (!m_frameGenContextCreated) {
      return;
    }

    m_frameId = frameId;
    m_frameGenEnabled = enabled;

    ffx::Context& frameGenCtx = *reinterpret_cast<ffx::Context*>(&m_frameGenContext);
    ffx::ConfigureDescFrameGeneration& config = asFrameGenConfig(m_frameGenConfig);

    config.frameGenerationEnabled = enabled;
    config.swapChain = swapchain;
    config.frameID = frameId;

    // Keep callback path active whenever frame generation is enabled.
    // The callback may use explicit path-traced overrides or fall back to
    // proxy-provided presentColor depending on availability.
    if (enabled) {
      config.frameGenerationCallback = frameGenCallback;
      config.frameGenerationCallbackUserContext = m_frameGenContext;
    } else {
      config.frameGenerationCallback = nullptr;
      config.frameGenerationCallbackUserContext = nullptr;
    }

    // Let FFX handle presentation internally
    // When presentCallback is nullptr, FFX VK swapchain proxy handles the copy internally
    config.presentCallback = nullptr;
    config.presentCallbackUserContext = nullptr;
    
    // Keep FFX color path pinned to Remix-provided path-traced output.
    // This avoids SDK-side fallback to proxy swapchain color when HUDLessColor is unset.
    config.HUDLessColor = createFfxResourceFromImage(
      m_presentColorSource,
      FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ,
      FFX_API_RESOURCE_USAGE_READ_ONLY);

    config.flags = m_swapchainContextCreated
      ? 0
      : FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    config.allowAsyncWorkloads = m_asyncWorkloadsSupported;
    config.onlyPresentGenerated = false;

    // Set generation rect to full swapchain/output size (not render size)
    const uint32_t outputWidth = m_swapchainSize.x != 0 ? m_swapchainSize.x : m_displaySize.x;
    const uint32_t outputHeight = m_swapchainSize.y != 0 ? m_swapchainSize.y : m_displaySize.y;

    config.generationRect.left = 0;
    config.generationRect.top = 0;
    config.generationRect.width = outputWidth;
    config.generationRect.height = outputHeight;

    ffx::ReturnCode retCode = ffx::Configure(frameGenCtx, config);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to configure frame gen: ", static_cast<int>(retCode)));
    }
  }

  void DxvkFSRFrameGen::prepareFrameGeneration(
    Rc<DxvkContext> ctx,
    DxvkBarrierSet& barriers,
    const RtCamera& camera,
    Rc<DxvkImageView> motionVectors,
    Rc<DxvkImageView> depth,
    bool viewModelActive,
    bool resetHistory,
    uint64_t frameId,
    float deltaTimeMs)
  {
    if (!m_frameGenContextCreated) {
      return;
    }

    m_preparedForPresent.store(false, std::memory_order_release);

    if (motionVectors == nullptr || depth == nullptr) {
      Logger::warn("FSR FG: Skipping prepare pass - motion vectors or depth is missing");
      if (m_swapchain != VK_NULL_HANDLE)
        configureFrameGeneration(m_swapchain, m_frameId, false);
      return;
    }

    // Get command buffer from context
    VkCommandBuffer cmdBuffer = ctx->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    if (cmdBuffer == VK_NULL_HANDLE) {
      Logger::warn("FSR FG: No command buffer available for prepareFrameGeneration");
      if (m_swapchain != VK_NULL_HANDLE)
        configureFrameGeneration(m_swapchain, m_frameId, false);
      return;
    }
    
    // Configure frame generation each frame (enables it and sets frame ID)
    // This must be called before the prepare dispatch
    const uint64_t fgFrameId = frameId != 0 ? frameId : (m_frameId + 1);
    m_frameId = fgFrameId;

    if (m_swapchain != VK_NULL_HANDLE) {
      if (!m_swapchainContextCreated) {
        const bool hasPathTracedPresentColor = registerPresentColorOverrideForFrame(m_presentColorSource, fgFrameId);
        if (!hasPathTracedPresentColor) {
          Logger::warn("FSR FG: Skipping prepare pass - path-traced present color source is missing");
          configureFrameGeneration(m_swapchain, fgFrameId, false);
          return;
        }
      }

      configureFrameGeneration(m_swapchain, fgFrameId, true);
    }
    
    if (!m_frameGenEnabled) {
      return;
    }

    // Helper function to create FFX resource from Vulkan image using SDK helper
    auto createFfxResource = [](Rc<DxvkImageView> view, uint32_t state, uint32_t additionalUsages = FFX_API_RESOURCE_USAGE_READ_ONLY) -> FfxApiResource {
      if (view == nullptr) {
        return FfxApiResource({});
      }
      
      // Build a VkImageCreateInfo from the image info for the SDK helper
      VkImageCreateInfo createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      createInfo.imageType = VK_IMAGE_TYPE_2D;
      createInfo.format = view->info().format;
      createInfo.extent.width = view->imageInfo().extent.width;
      createInfo.extent.height = view->imageInfo().extent.height;
      createInfo.extent.depth = 1;
      createInfo.mipLevels = 1;
      createInfo.arrayLayers = 1;
      createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      createInfo.usage = view->image()->info().usage;
      createInfo.flags = view->image()->info().flags;
      
      FfxApiResourceDescription desc = ffxApiGetImageResourceDescriptionVK(view->image()->handle(), createInfo, additionalUsages);
      return ffxApiGetResourceVK(reinterpret_cast<void*>(view->image()->handle()), desc, state);
    };

    // Get camera info
    float cameraNear = camera.getNearPlane();
    float cameraFar = camera.getFarPlane();
    float fovY = camera.getFov();
    
    // Get jitter - FSR expects negative jitter offset
    float jitter[2];
    camera.getJittering(jitter);
    
    // Get render size from motion vectors
    uint32_t renderWidth = motionVectors->imageInfo().extent.width;
    uint32_t renderHeight = motionVectors->imageInfo().extent.height;

    // Dispatch the prepare pass
    ffx::Context& frameGenCtx = *reinterpret_cast<ffx::Context*>(&m_frameGenContext);
    
    ffx::DispatchDescFrameGenerationPrepare prepareDesc{};
    prepareDesc.commandList = cmdBuffer;
    prepareDesc.frameID = fgFrameId;
    prepareDesc.flags = 0;
    prepareDesc.renderSize = { renderWidth, renderHeight };
    prepareDesc.jitterOffset = viewModelActive
      ? decltype(prepareDesc.jitterOffset){ 0.0f, 0.0f }
      : decltype(prepareDesc.jitterOffset){ jitter[0], jitter[1] };  // Same jitter as FSR upscaler
    // View-model motion is often from a different camera and can cause severe FG reprojection trails.
    // On those frames, suppress MV contribution so FG relies on non-MV cues instead of mixed-camera vectors.
    prepareDesc.motionVectorScale = viewModelActive
      ? decltype(prepareDesc.motionVectorScale){ 0.0f, 0.0f }
      : decltype(prepareDesc.motionVectorScale){ 1.0f, 1.0f }; // Motion vectors already in pixel units (like DLSS)
    prepareDesc.frameTimeDelta = deltaTimeMs;
    prepareDesc.cameraNear = cameraNear;
    prepareDesc.cameraFar = cameraFar;
    prepareDesc.cameraFovAngleVertical = fovY;
    prepareDesc.viewSpaceToMetersFactor = 1.0f;
    prepareDesc.depth = createFfxResource(depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_DEPTHTARGET);
    prepareDesc.motionVectors = createFfxResource(motionVectors, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    // View-model animation frequently violates the single-camera assumption used by FG reprojection.
    // Reset FG history on those frames to reduce persistent first-person trails.
    prepareDesc.unused_reset = resetHistory || viewModelActive;
    
    // Add camera info for better quality (FSR 3.1.4+)
    ffx::DispatchDescFrameGenerationPrepareCameraInfo cameraInfo{};
    Vector3 cameraPos = camera.getPosition(false);
    Vector3 cameraUp = camera.getUp(false);
    Vector3 cameraRight = camera.getRight(false);
    Vector3 cameraForward = camera.getDirection(false);
    cameraInfo.cameraPosition[0] = cameraPos.x;
    cameraInfo.cameraPosition[1] = cameraPos.y;
    cameraInfo.cameraPosition[2] = cameraPos.z;
    cameraInfo.cameraUp[0] = cameraUp.x;
    cameraInfo.cameraUp[1] = cameraUp.y;
    cameraInfo.cameraUp[2] = cameraUp.z;
    cameraInfo.cameraRight[0] = cameraRight.x;
    cameraInfo.cameraRight[1] = cameraRight.y;
    cameraInfo.cameraRight[2] = cameraRight.z;
    cameraInfo.cameraForward[0] = cameraForward.x;
    cameraInfo.cameraForward[1] = cameraForward.y;
    cameraInfo.cameraForward[2] = cameraForward.z;

    // Link camera info to prepare descriptor
    prepareDesc.header.pNext = &cameraInfo.header;

    Logger::debug(str::format("FSR FG: Dispatching prepare pass for frame ", fgFrameId));
    ffx::ReturnCode retCode = ffx::Dispatch(frameGenCtx, prepareDesc);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to dispatch prepare pass: ", static_cast<int>(retCode)));
      if (m_swapchain != VK_NULL_HANDLE)
        configureFrameGeneration(m_swapchain, m_frameId, false);
    } else {
      Logger::debug("FSR FG: Prepare dispatch successful");
      m_preparedForPresent.store(true, std::memory_order_release);
    }
  }

  void DxvkFSRFrameGen::waitForPresents() {
    if (!m_swapchainContextCreated || !m_swapChainContext) {
      return;
    }

    ffx::Context& swapChainCtx = *reinterpret_cast<ffx::Context*>(&m_swapChainContext);
    ffx::DispatchDescFrameGenerationSwapChainWaitForPresentsVK waitDesc{};
    ffx::ReturnCode retCode = ffx::Dispatch(swapChainCtx, waitDesc);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::warn(str::format("FSR FG: Wait for presents failed: ", static_cast<int>(retCode)));
    }
  }

  // ============================================================================
  // DxvkFSRFGPresenter implementation
  // ============================================================================

  DxvkFSRFGPresenter::DxvkFSRFGPresenter(
    Rc<DxvkDevice> device,
    Rc<DxvkContext> ctx,
    HWND window,
    const Rc<vk::InstanceFn>& vki,
    const Rc<vk::DeviceFn>& vkd,
    vk::PresenterDevice presenterDevice,
    const vk::PresenterDesc& desc,
    VkSurfaceKHR existingSurface)
    // NV-DXVK start: FSR FG support - Use protected constructor with existing surface
    : vk::Presenter(window, vki, vkd, presenterDevice, existingSurface)
    // NV-DXVK end
    , m_device(device.ptr())
    , m_ctx(ctx)
  {
    Logger::info("FSR FG: Creating FSR FG Presenter");

    // If no surface was provided, create one
    if (m_surface == VK_NULL_HANDLE) {
      Logger::info("FSR FG: Creating new surface (first time initialization)");
      if (createSurface() != VK_SUCCESS) {
        throw DxvkError("FSR FG: Failed to create surface");
      }
    } else {
      Logger::info("FSR FG: Reusing existing surface from previous presenter");
    }

    // Get access to the FSR Frame Gen component
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    // Set display size for later use
    fsrFrameGen.setDisplaySize({ desc.imageExtent.width, desc.imageExtent.height });
    
    // Store desc for lazy context creation
    m_cachedDesc = desc;

    // Create the initial swapchain using our own recreateSwapChain
    // (The protected constructor only sets the surface, doesn't create swapchain)
    if (recreateSwapChain(desc) != VK_SUCCESS)
      throw DxvkError("FSR FG: Failed to create initial swap chain");

    // DON'T create FFX contexts here - defer until FSR FG is actually enabled
    // This prevents the FFX swapchain proxy from interfering when FSR FG is disabled
    Logger::info(str::format("FSR FG Presenter: Created (contexts deferred until enabled) for ", 
      desc.imageExtent.width, "x", desc.imageExtent.height));
  }

  DxvkFSRFGPresenter::~DxvkFSRFGPresenter() {
    Logger::info("FSR FG: Destroying FSR FG Presenter");
    
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    fsrFrameGen.waitForPresents();
    fsrFrameGen.destroyContexts();

    // The proxy swapchain is owned by FFX contexts; clear handle so base presenter
    // teardown does not try to destroy an already-destroyed swapchain.
    m_swapchain = VK_NULL_HANDLE;
    m_contextCreated = false;
  }

  void DxvkFSRFGPresenter::updateSwapchainImagesToProxy(DxvkFSRFrameGen& fsrFrameGen, VkFormat format) {
    // Get the FFX replacement function to retrieve PROXY images
    PFN_vkGetSwapchainImagesKHR replacedGetSwapchainImages = fsrFrameGen.getReplacedGetSwapchainImagesKHR();
    if (!replacedGetSwapchainImages) {
      Logger::err("FSR FG: No replacement vkGetSwapchainImagesKHR available");
      return;
    }
    
    // Destroy old image views (they point to REAL swapchain images, not PROXY images)
    for (auto& img : m_images) {
      if (img.view != VK_NULL_HANDLE) {
        m_vkd->vkDestroyImageView(m_vkd->device(), img.view, nullptr);
        img.view = VK_NULL_HANDLE;
      }
    }
    
    // Get PROXY images from FFX SDK
    std::vector<VkImage> proxyImages;
    uint32_t imageCount = 0;
    VkResult status = replacedGetSwapchainImages(m_vkd->device(), m_swapchain, &imageCount, nullptr);
    if (status != VK_SUCCESS) {
      Logger::err(str::format("FSR FG: Failed to get proxy image count: ", static_cast<int>(status)));
      return;
    }
    
    proxyImages.resize(imageCount);
    status = replacedGetSwapchainImages(m_vkd->device(), m_swapchain, &imageCount, proxyImages.data());
    if (status != VK_SUCCESS) {
      Logger::err(str::format("FSR FG: Failed to get proxy images: ", static_cast<int>(status)));
      return;
    }
    
    // Resize m_images to match proxy image count
    m_images.resize(imageCount);
    m_info.imageCount = imageCount;
    
    // Create new image views for PROXY images
    for (uint32_t i = 0; i < imageCount; i++) {
      m_images[i].image = proxyImages[i];
      
      VkImageViewCreateInfo viewInfo = {};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.pNext = nullptr;
      viewInfo.flags = 0;
      viewInfo.image = proxyImages[i];
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = format;
      viewInfo.components = VkComponentMapping {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
      viewInfo.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, 1, 0, 1 };
      
      VkResult viewResult = m_vkd->vkCreateImageView(m_vkd->device(), &viewInfo, nullptr, &m_images[i].view);
      if (viewResult != VK_SUCCESS) {
        Logger::err(str::format("FSR FG: Failed to create image view for proxy image ", i, ": ", static_cast<int>(viewResult)));
      }
    }
    
    Logger::info(str::format("FSR FG: Updated ", imageCount, " image views to use PROXY images"));
  }

  void DxvkFSRFGPresenter::createFFXContexts() {
    Logger::info("FSR FG Presenter: Creating FFX contexts (lazy initialization)");

    if (::GetModuleHandleA("winevulkan.dll") != nullptr) {
      Logger::warn("FSR FG Presenter: winevulkan detected, disabling FSR FG for this session to avoid present deadlocks");
      m_disableFsrFgForSession = true;
      m_contextCreated = false;
      return;
    }
    
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    VkPhysicalDevice physDevice = m_device->adapter()->handle();
    VkDevice vkDevice = m_device->handle();
    // Frame generation source is Remix path-traced final output, which is
    // allocated as R16G16B16A16_SFLOAT in resource setup.
    VkFormat backBufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkSwapchainCreateInfoKHR swapInfo = buildSwapchainCreateInfoFromPresenter(
      m_info,
      m_surface,
      m_swapchain);

    // Wrap swapchain with FFX replacement functions so generated frames can be presented.
    fsrFrameGen.createSwapchainProxy(
      physDevice,
      vkDevice,
      &m_swapchain,
      &swapInfo);

    if (fsrFrameGen.isSwapchainContextCreated()) {
      updateSwapchainImagesToProxy(fsrFrameGen, m_info.format.format);
    } else {
      Logger::warn("FSR FG Presenter: Swapchain proxy unavailable, FG presentation may be disabled");
    }
    
    // Create the frame generation context
    fsrFrameGen.createFrameGenContext(
      physDevice,
      vkDevice,
      m_cachedDesc.imageExtent.width,
      m_cachedDesc.imageExtent.height,
      backBufferFormat);
    
    if (fsrFrameGen.isInitialized()) {
      m_contextCreated = true;
      fsrFrameGen.setSwapchain(m_swapchain);
      Logger::info(str::format("FSR FG Presenter: FFX contexts created successfully for ",
        m_cachedDesc.imageExtent.width, "x", m_cachedDesc.imageExtent.height,
        " format=", static_cast<int>(backBufferFormat)));
    } else {
      Logger::err("FSR FG Presenter: Failed to create FFX contexts");
      m_disableFsrFgForSession = true;
      m_contextCreated = false;
    }
  }

  VkResult DxvkFSRFGPresenter::acquireNextImage(
    vk::PresenterSync& sync, 
    uint32_t& index, 
    bool isDlfgPresenting) 
  {
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();

    if (m_contextCreated) {
      PFN_vkAcquireNextImageKHR replacedAcquire = fsrFrameGen.getReplacedAcquireNextImageKHR();
      if (replacedAcquire != nullptr) {
        sync = m_semaphores.at(m_frameIndex);

        if (m_acquireStatus == VK_NOT_READY) {
          m_acquireStatus = replacedAcquire(
            m_vkd->device(),
            m_swapchain,
            std::numeric_limits<uint64_t>::max(),
            sync.acquire,
            VK_NULL_HANDLE,
            &m_imageIndex);
        }

        if (m_acquireStatus != VK_SUCCESS && m_acquireStatus != VK_SUBOPTIMAL_KHR)
          return m_acquireStatus;

        index = m_imageIndex;
        return m_acquireStatus;
      }
    }

    return vk::Presenter::acquireNextImage(sync, index, isDlfgPresenting);
  }

  VkResult DxvkFSRFGPresenter::presentImage(
    std::atomic<VkResult>* status,
    const DxvkPresentInfo& presentInfo,
    const DxvkFrameInterpolationInfo& frameInterpolationInfo,
    std::uint32_t acquiredImageIndex,
    bool isDlfgPresenting,
    VkSetPresentConfigNV* presentMetering) 
  {
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    const bool fgEnabled = DxvkFSRFrameGen::enable() && !m_disableFsrFgForSession;
    
    // Lazily create FFX contexts when first enabled.
    if (fgEnabled) {
      if (!m_contextCreated) {
        createFFXContexts();
      }
    }

    if (m_contextCreated) {
      const bool preparedForPresent = fsrFrameGen.consumePreparedFrameForPresent();

      // Only disable from presenter path. Enable/configure happens in prepare with FG-local frame ID.
      if (fsrFrameGen.isInitialized() && (!fgEnabled || !preparedForPresent)) {
        fsrFrameGen.configureFrameGeneration(m_swapchain, fsrFrameGen.getFrameGenFrameId(), false);
      }
    }
    
    if (m_contextCreated) {
      PFN_vkQueuePresentKHR replacedPresent = fsrFrameGen.getReplacedQueuePresentKHR();
      if (replacedPresent != nullptr) {
        vk::PresenterSync sync = m_semaphores.at(m_frameIndex);

        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.pNext = presentMetering;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &sync.present;
        info.swapchainCount = 1;
        info.pSwapchains = &m_swapchain;
        info.pImageIndices = &m_imageIndex;
        info.pResults = nullptr;

        VkResult presentResult = replacedPresent(m_device->queues().present.queueHandle, &info);
        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
          return presentResult;

        m_frameIndex = (m_frameIndex + 1) % m_semaphores.size();
        vk::PresenterSync nextSync = m_semaphores.at(m_frameIndex);

        PFN_vkAcquireNextImageKHR replacedAcquire = fsrFrameGen.getReplacedAcquireNextImageKHR();
        if (replacedAcquire != nullptr) {
          m_acquireStatus = replacedAcquire(
            m_vkd->device(),
            m_swapchain,
            std::numeric_limits<uint64_t>::max(),
            nextSync.acquire,
            VK_NULL_HANDLE,
            &m_imageIndex);
        } else {
          m_acquireStatus = VK_NOT_READY;
        }

        return presentResult;
      }
    }

    return vk::Presenter::presentImage(status, presentInfo, frameInterpolationInfo,
      acquiredImageIndex, isDlfgPresenting, presentMetering);
  }

  VkResult DxvkFSRFGPresenter::getSwapImages(std::vector<VkImage>& images) {
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();

    if (m_contextCreated) {
      PFN_vkGetSwapchainImagesKHR replacedGetSwapchainImages = fsrFrameGen.getReplacedGetSwapchainImagesKHR();
      if (replacedGetSwapchainImages != nullptr) {
        uint32_t imageCount = 0;
        VkResult status = replacedGetSwapchainImages(m_vkd->device(), m_swapchain, &imageCount, nullptr);
        if (status != VK_SUCCESS)
          return status;

        images.resize(imageCount);
        return replacedGetSwapchainImages(m_vkd->device(), m_swapchain, &imageCount, images.data());
      }
    }

    return vk::Presenter::getSwapImages(images);
  }

  VkResult DxvkFSRFGPresenter::recreateSwapChain(const vk::PresenterDesc& desc) {
    // Guard against re-entrant calls during recreation
    if (m_recreatingSwapchain) {
      Logger::warn("FSR FG Presenter: Skipping re-entrant recreateSwapChain call");
      return VK_SUCCESS;
    }
    
    // Skip if size hasn't changed and we already have valid contexts
    if (m_currentExtent.width == desc.imageExtent.width && 
        m_currentExtent.height == desc.imageExtent.height &&
        m_contextCreated && m_swapchain != VK_NULL_HANDLE) {
      Logger::info(str::format("FSR FG Presenter: Skipping redundant recreation for ",
        desc.imageExtent.width, "x", desc.imageExtent.height));
      return VK_SUCCESS;
    }
    
    m_recreatingSwapchain = true;
    Logger::info(str::format("FSR FG Presenter: Recreating swapchain ", 
      desc.imageExtent.width, "x", desc.imageExtent.height));
    
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    // Wait for any pending presents
    fsrFrameGen.waitForPresents();
    
    // Destroy old FFX contexts - this releases the swapchain proxy
    fsrFrameGen.destroyContexts();
    m_contextCreated = false;
    
    // Now call base implementation to create a new normal swapchain
    VkResult result = vk::Presenter::recreateSwapChain(desc);
    if (result != VK_SUCCESS) {
      Logger::err(str::format("FSR FG Presenter: Base swapchain recreation failed: ", static_cast<int>(result)));
      m_recreatingSwapchain = false;
      return result;
    }
    
    // Update display size
    fsrFrameGen.setDisplaySize({ desc.imageExtent.width, desc.imageExtent.height });
    
    // Re-wrap swapchain and re-create frame generation context with new display params.
    VkPhysicalDevice physDevice = m_device->adapter()->handle();
    VkDevice vkDevice = m_device->handle();
    // Keep frame generation context format aligned with path-traced source.
    VkFormat backBufferFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    VkSwapchainCreateInfoKHR swapInfo = buildSwapchainCreateInfoFromPresenter(
      m_info,
      m_surface,
      m_swapchain);

    fsrFrameGen.createSwapchainProxy(
      physDevice,
      vkDevice,
      &m_swapchain,
      &swapInfo);

    if (fsrFrameGen.isSwapchainContextCreated()) {
      updateSwapchainImagesToProxy(fsrFrameGen, m_info.format.format);
    }
    
    // Create the frame generation context
    fsrFrameGen.createFrameGenContext(
      physDevice,
      vkDevice,
      desc.imageExtent.width,
      desc.imageExtent.height,
      backBufferFormat);
    
    if (fsrFrameGen.isInitialized()) {
      m_contextCreated = true;
      fsrFrameGen.setSwapchain(m_swapchain);
      m_currentExtent = desc.imageExtent;
      Logger::info(str::format("FSR FG Presenter: Recreated successfully for ",
        desc.imageExtent.width, "x", desc.imageExtent.height));
    } else {
      Logger::err("FSR FG Presenter: Failed to recreate FSR FG contexts");
      m_disableFsrFgForSession = true;
      m_contextCreated = false;
    }
    
    m_recreatingSwapchain = false;
    return result;
  }

} // namespace dxvk
