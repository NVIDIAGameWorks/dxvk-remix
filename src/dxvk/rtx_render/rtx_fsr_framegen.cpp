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

namespace dxvk {

  // Helper to access config
  static ffx::ConfigureDescFrameGeneration& asFrameGenConfig(void* cfg) {
    return *reinterpret_cast<ffx::ConfigureDescFrameGeneration*>(cfg);
  }

  // Static callback for frame generation dispatch  
  // pUserCtx is the ffx::Context value (void*) for the frame gen context
  static ffxReturnCode_t frameGenCallback(ffxDispatchDescFrameGeneration* params, void* pUserCtx) {
    // pUserCtx IS the context (void*), pass its address as required by ffxDispatch
    Logger::debug(str::format("FSR FG: Frame generation callback invoked for frame ", params->frameID));
    ffxContext ctx = static_cast<ffxContext>(pUserCtx);
    ffxReturnCode_t result = ffxDispatch(&ctx, &params->header);
    Logger::debug(str::format("FSR FG: Frame generation dispatch result: ", static_cast<int>(result)));
    return result;
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

    // Validate that we have all 4 separate queues required by FFX SDK
    bool hasAsyncCompute = queues.asyncCompute.queueHandle != VK_NULL_HANDLE &&
                           queues.asyncCompute.queueHandle != queues.graphics.queueHandle;
    bool hasFsrPresent = queues.fsrPresent.queueHandle != VK_NULL_HANDLE &&
                         queues.fsrPresent.queueHandle != queues.graphics.queueHandle;
    bool hasImageAcquire = queues.imageAcquire.queueHandle != VK_NULL_HANDLE &&
                           queues.imageAcquire.queueHandle != queues.graphics.queueHandle;

    if (!hasAsyncCompute || !hasFsrPresent || !hasImageAcquire) {
      Logger::err(str::format("FSR FG: Cannot create swapchain proxy - AMD FFX SDK requires 4 SEPARATE queues"));
      Logger::err(str::format("  hasAsyncCompute=", hasAsyncCompute, ", hasFsrPresent=", hasFsrPresent, ", hasImageAcquire=", hasImageAcquire));
      Logger::err(str::format("  Your GPU may not have enough hardware queues for FSR 3 Frame Generation"));
      return;
    }

    // Verify all 4 queues are actually unique
    VkQueue gameQueue = queues.graphics.queueHandle;
    VkQueue asyncQueue = queues.asyncCompute.queueHandle;
    VkQueue presentQueue = queues.fsrPresent.queueHandle;
    VkQueue acquireQueue = queues.imageAcquire.queueHandle;

    if (gameQueue == asyncQueue || gameQueue == presentQueue || gameQueue == acquireQueue ||
        asyncQueue == presentQueue || asyncQueue == acquireQueue || presentQueue == acquireQueue) {
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
    createSwapChainDesc.gameQueue.queue = queues.graphics.queueHandle;
    createSwapChainDesc.gameQueue.familyIndex = queues.graphics.queueFamily;
    createSwapChainDesc.gameQueue.submitFunc = nullptr;

    // Async compute queue - MUST be a separate queue from graphics
    createSwapChainDesc.asyncComputeQueue.queue = queues.asyncCompute.queueHandle;
    createSwapChainDesc.asyncComputeQueue.familyIndex = queues.asyncCompute.queueFamily;
    createSwapChainDesc.asyncComputeQueue.submitFunc = nullptr;

    // Present queue - MUST be a separate queue supporting presentation
    createSwapChainDesc.presentQueue.queue = queues.fsrPresent.queueHandle;
    createSwapChainDesc.presentQueue.familyIndex = queues.fsrPresent.queueFamily;
    createSwapChainDesc.presentQueue.submitFunc = nullptr;

    // Image acquire queue - MUST be a separate queue
    createSwapChainDesc.imageAcquireQueue.queue = queues.imageAcquire.queueHandle;
    createSwapChainDesc.imageAcquireQueue.familyIndex = queues.imageAcquire.queueFamily;
    createSwapChainDesc.imageAcquireQueue.submitFunc = nullptr;

    Logger::info(str::format("FSR FG: Using 4 separate queues for FFX SDK"));

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
      ffx::DestroyContext(swapChainCtx);
      m_swapChainContext = nullptr;
      return;
    }

    m_replacedAcquireNextImageKHR = replacementFunctions.pOutAcquireNextImageKHR;
    m_replacedQueuePresentKHR = replacementFunctions.pOutQueuePresentKHR;
    m_replacedGetSwapchainImagesKHR = replacementFunctions.pOutGetSwapchainImagesKHR;

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
    if (!m_swapchainContextCreated) {
      Logger::err("FSR FG: Cannot create frame gen context - swapchain proxy not created");
      return;
    }

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
      
      m_replacedAcquireNextImageKHR = nullptr;
      m_replacedQueuePresentKHR = nullptr;
      m_replacedGetSwapchainImagesKHR = nullptr;
      m_swapchainSize = { 0, 0 };
      
      Logger::info("FSR FG: Swapchain context destroyed");
    }

    m_initialized = false;
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

    // Use callback mode for frame generation dispatch
    config.frameGenerationCallback = frameGenCallback;
    config.frameGenerationCallbackUserContext = m_frameGenContext;

    // Let FFX handle presentation internally
    // When presentCallback is nullptr, FFX VK swapchain proxy handles the copy internally
    config.presentCallback = nullptr;
    config.presentCallbackUserContext = nullptr;
    
    // HUDLessColor is for UI extraction - comparing against backbuffer to identify UI elements.
    // We don't use separate UI rendering, so leave this empty.
    // The SDK will use the proxy swapchain images directly as the interpolation source.
    config.HUDLessColor = FfxApiResource({});

    config.flags = 0;
    config.allowAsyncWorkloads = true;
    config.onlyPresentGenerated = false;

    // Set generation rect to full swapchain/output size (not render size)
    config.generationRect.left = 0;
    config.generationRect.top = 0;
    config.generationRect.width = m_swapchainSize.x;
    config.generationRect.height = m_swapchainSize.y;

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
    bool resetHistory,
    float deltaTimeMs)
  {
    if (!m_frameGenContextCreated) {
      return;
    }
    
    // Configure frame generation each frame (enables it and sets frame ID)
    // This must be called before the prepare dispatch
    if (m_swapchain != VK_NULL_HANDLE) {
      // Increment frame ID
      m_frameId++;
      configureFrameGeneration(m_swapchain, m_frameId, true);
    }
    
    if (!m_frameGenEnabled) {
      return;
    }

    // Get command buffer from context
    VkCommandBuffer cmdBuffer = ctx->getCmdBuffer(DxvkCmdBuffer::ExecBuffer);
    if (cmdBuffer == VK_NULL_HANDLE) {
      Logger::warn("FSR FG: No command buffer available for prepareFrameGeneration");
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
    prepareDesc.frameID = m_frameId;
    prepareDesc.flags = 0;
    prepareDesc.renderSize = { renderWidth, renderHeight };
    prepareDesc.jitterOffset = { jitter[0], jitter[1] };  // Same jitter as FSR upscaler
    prepareDesc.motionVectorScale = { 1.0f, 1.0f }; // Motion vectors already in pixel units (like DLSS)
    prepareDesc.frameTimeDelta = deltaTimeMs;
    prepareDesc.cameraNear = cameraNear;
    prepareDesc.cameraFar = cameraFar;
    prepareDesc.cameraFovAngleVertical = fovY;
    prepareDesc.viewSpaceToMetersFactor = 1.0f;
    prepareDesc.depth = createFfxResource(depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_DEPTHTARGET);
    prepareDesc.motionVectors = createFfxResource(motionVectors, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ, FFX_API_RESOURCE_USAGE_READ_ONLY);
    prepareDesc.unused_reset = resetHistory;
    
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

    Logger::debug(str::format("FSR FG: Dispatching prepare pass for frame ", m_frameId));
    ffx::ReturnCode retCode = ffx::Dispatch(frameGenCtx, prepareDesc);
    if (retCode != ffx::ReturnCode::Ok) {
      Logger::err(str::format("FSR FG: Failed to dispatch prepare pass: ", static_cast<int>(retCode)));
    } else {
      Logger::debug("FSR FG: Prepare dispatch successful");
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
    
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    VkPhysicalDevice physDevice = m_device->adapter()->handle();
    VkDevice vkDevice = m_device->handle();
    VkFormat backBufferFormat = m_cachedDesc.formats[0].format;
    
    // Create the swapchain proxy context using the swapchain created by base class
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = m_surface;
    swapchainCreateInfo.minImageCount = m_cachedDesc.imageCount;
    swapchainCreateInfo.imageFormat = backBufferFormat;
    swapchainCreateInfo.imageExtent = m_cachedDesc.imageExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Use IMMEDIATE mode for FSR FG - VSync is handled internally by the FSR FG pacing
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    swapchainCreateInfo.clipped = VK_TRUE;
    // Don't pass old swapchain - FFX SDK manages swapchain lifecycle
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
    
    // Create swapchain proxy - this replaces the swapchain
    fsrFrameGen.createSwapchainProxy(
      physDevice,
      vkDevice,
      &m_swapchain,
      &swapchainCreateInfo);
    
    // CRITICAL: After creating the swapchain proxy, the base class still has image views 
    // pointing to the REAL swapchain images. We need to replace them with PROXY images.
    updateSwapchainImagesToProxy(fsrFrameGen, backBufferFormat);
    
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
    }
  }

  VkResult DxvkFSRFGPresenter::acquireNextImage(
    vk::PresenterSync& sync, 
    uint32_t& index, 
    bool isDlfgPresenting) 
  {
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    // Only use FFX replaced functions if FSR FG is enabled AND contexts are created
    if (DxvkFSRFrameGen::enable() && m_contextCreated) {
      PFN_vkAcquireNextImageKHR replacedAcquire = fsrFrameGen.getReplacedAcquireNextImageKHR();
      if (replacedAcquire && fsrFrameGen.isSwapchainContextCreated()) {
        sync = m_semaphores.at(m_frameIndex);
        
        VkResult result = replacedAcquire(
          m_vkd->device(),
          m_swapchain,
          std::numeric_limits<uint64_t>::max(),
          sync.acquire,
          VK_NULL_HANDLE,
          &index);
        
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
          m_frameIndex = (m_frameIndex + 1) % m_semaphores.size();
        }
        
        return result;
      }
    }
    
    // Fall back to base implementation when disabled or contexts not created
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
    
    // Check if FSR FG is enabled
    if (DxvkFSRFrameGen::enable()) {
      // Lazily create FFX contexts when first enabled
      if (!m_contextCreated) {
        createFFXContexts();
      }
      
      // If contexts are ready, use FFX replaced functions
      if (m_contextCreated) {
        PFN_vkQueuePresentKHR replacedPresent = fsrFrameGen.getReplacedQueuePresentKHR();
        if (replacedPresent && fsrFrameGen.isSwapchainContextCreated()) {
          // Get present semaphore from base class's semaphore array
          vk::PresenterSync sync = m_semaphores.at(m_frameIndex);
          
          VkPresentInfoKHR info = {};
          info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
          info.waitSemaphoreCount = 1;
          info.pWaitSemaphores = &sync.present;
          info.swapchainCount = 1;
          info.pSwapchains = &m_swapchain;
          info.pImageIndices = &acquiredImageIndex;
          
          // Use queue from base class's device (vk::Presenter::m_device is PresenterDevice)
          Logger::debug(str::format("FSR FG: Presenting image index ", acquiredImageIndex, " via FFX replacement function"));
          VkResult result = replacedPresent(vk::Presenter::m_device.queue, &info);
          
          Logger::debug(str::format("FSR FG: Present result: ", static_cast<int>(result)));
          
          if (status) {
            status->store(result);
          }
          
          return result;
        }
      }
    }
    
    // Fall back to base implementation when disabled or contexts not ready
    return vk::Presenter::presentImage(status, presentInfo, frameInterpolationInfo, 
                                        acquiredImageIndex, isDlfgPresenting, presentMetering);
  }

  VkResult DxvkFSRFGPresenter::getSwapImages(std::vector<VkImage>& images) {
    auto& fsrFrameGen = m_device->getCommon()->metaFSRFrameGen();
    
    // Use FFX replaced function if FSR FG is enabled AND the swapchain proxy is created
    // Note: During initial swapchain creation, the base class calls getSwapImages() 
    // BEFORE we create the swapchain proxy, so we must check isSwapchainContextCreated()
    if (DxvkFSRFrameGen::enable() && fsrFrameGen.isSwapchainContextCreated()) {
      PFN_vkGetSwapchainImagesKHR replacedGetSwapchainImages = fsrFrameGen.getReplacedGetSwapchainImagesKHR();
      if (replacedGetSwapchainImages) {
        // Use the FFX SDK's replacement function which returns PROXY images
        uint32_t imageCount = 0;
        
        VkResult status = replacedGetSwapchainImages(
          m_vkd->device(), m_swapchain, &imageCount, nullptr);
        
        if (status != VK_SUCCESS)
          return status;
        
        images.resize(imageCount);
        
        status = replacedGetSwapchainImages(
          m_vkd->device(), m_swapchain, &imageCount, images.data());
        
        Logger::info(str::format("FSR FG: getSwapImages returning ", imageCount, " PROXY images via FFX replacement function"));
        return status;
      }
    }
    
    // Fall back to base implementation when disabled or replacement function not available
    Logger::info("FSR FG: getSwapImages using base implementation (REAL swapchain images)");
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
    
    // Save the old swapchain proxy handle before destroying contexts
    VkSwapchainKHR oldSwapchainProxy = m_swapchain;
    
    // Destroy old FFX contexts - this releases the swapchain proxy
    fsrFrameGen.destroyContexts();
    m_contextCreated = false;
    
    // Clear the swapchain handle since FFX destroyed the proxy
    // This prevents base class from trying to destroy an already-destroyed handle
    m_swapchain = VK_NULL_HANDLE;
    
    // Now call base implementation to create a new normal swapchain
    VkResult result = vk::Presenter::recreateSwapChain(desc);
    if (result != VK_SUCCESS) {
      Logger::err(str::format("FSR FG Presenter: Base swapchain recreation failed: ", static_cast<int>(result)));
      m_recreatingSwapchain = false;
      return result;
    }
    
    // Update display size
    fsrFrameGen.setDisplaySize({ desc.imageExtent.width, desc.imageExtent.height });
    
    // Re-create FFX contexts with the new swapchain
    VkPhysicalDevice physDevice = m_device->adapter()->handle();
    VkDevice vkDevice = m_device->handle();
    VkFormat backBufferFormat = desc.formats[0].format;
    
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = m_surface;
    swapchainCreateInfo.minImageCount = desc.imageCount;
    swapchainCreateInfo.imageFormat = backBufferFormat;
    swapchainCreateInfo.imageExtent = desc.imageExtent;
    swapchainCreateInfo.imageArrayLayers = 1;
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Use IMMEDIATE mode for FSR FG - VSync is handled internally by the FSR FG pacing
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    swapchainCreateInfo.clipped = VK_TRUE;
    // Don't pass old swapchain - FFX SDK manages swapchain lifecycle
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
    
    // Create swapchain proxy - this replaces the swapchain
    fsrFrameGen.createSwapchainProxy(
      physDevice,
      vkDevice,
      &m_swapchain,
      &swapchainCreateInfo);
    
    // CRITICAL: After creating the swapchain proxy, the base class still has image views 
    // pointing to the REAL swapchain images. We need to replace them with PROXY images.
    updateSwapchainImagesToProxy(fsrFrameGen, backBufferFormat);
    
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
    }
    
    m_recreatingSwapchain = false;
    return result;
  }

} // namespace dxvk
