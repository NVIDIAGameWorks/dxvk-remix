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
#pragma once

#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx_common_object.h"

#include "../dxvk_format.h"
#include "../dxvk_include.h"
#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"
#include "../../vulkan/vulkan_presenter.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <atomic>

// No FFX forward declarations here - FFX's ffxContext is "typedef void*"
// We use void* directly in this header to avoid conflicts

namespace dxvk {

  class DxvkDevice;
  class DxvkContext;
  class DxvkBarrierSet;
  class RtCamera;
  class DxvkImageView;
  class DxvkFSRFGPresenter;

  namespace vk {
    struct PresenterDevice;
    struct PresenterDesc;
  }

  /**
   * \brief FSR 3 Frame Generation
   * 
   * AMD FidelityFX Frame Generation implementation
   */
  class DxvkFSRFrameGen : public CommonDeviceObject {
    friend class DxvkFSRFGPresenter;

  public:

    explicit DxvkFSRFrameGen(DxvkDevice* device);
    virtual void onDestroy();

    static bool supportsFSRFrameGen();
    
    void setDisplaySize(uint2 displaySize);

    /**
     * \brief Configure frame generation for the current frame
     * 
     * Must be called each frame before prepareFrameGeneration.
     * Sets up swapchain, frame ID, enabled state, and HUDLessColor.
     * 
     * \param backbuffer The rendered backbuffer (composite output) for HUDLessColor
     */
    void configureFrameGeneration(
      VkSwapchainKHR swapchain,
      uint64_t frameId,
      bool enabled);

    /**
     * \brief Dispatch the frame generation prepare pass
     * 
     * Records commands to prepare data for frame interpolation.
     * Call during the main rendering command buffer after compositing.
     */
    void prepareFrameGeneration(
      Rc<DxvkContext> ctx,
      DxvkBarrierSet& barriers,
      const RtCamera& camera,
      Rc<DxvkImageView> motionVectors,
      Rc<DxvkImageView> depth,
      bool resetHistory,
      float deltaTimeMs);

    /**
     * \brief Wait for all presents to complete
     * 
     * Must be called before resize or shutdown.
     */
    void waitForPresents();

    /**
     * \brief Check if frame generation is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * \brief Get swapchain context (for presenter)
     */
    void* getSwapChainContext() const { return m_swapChainContext; }

    /**
     * \brief Check if swapchain context is created
     */
    bool isSwapchainContextCreated() const { return m_swapchainContextCreated; }

    /**
     * \brief Get replaced Vulkan functions
     */
    PFN_vkAcquireNextImageKHR getReplacedAcquireNextImageKHR() const { return m_replacedAcquireNextImageKHR; }
    PFN_vkQueuePresentKHR getReplacedQueuePresentKHR() const { return m_replacedQueuePresentKHR; }
    PFN_vkGetSwapchainImagesKHR getReplacedGetSwapchainImagesKHR() const { return m_replacedGetSwapchainImagesKHR; }

    // RTX_OPTION accessors
    RTX_OPTION_ENV("rtx.fsrfg", bool, enable, false, "RTX_FSRFG_ENABLE", 
      "Enables FSR 3 frame generation which generates interpolated frames to increase framerate.");

    /**
     * \brief Create swapchain proxy context
     * 
     * Creates FFX swapchain context which intercepts Vulkan swapchain calls.
     * Must be called during swapchain creation.
     */
    void createSwapchainProxy(
      VkPhysicalDevice physicalDevice,
      VkDevice device,
      VkSwapchainKHR* pSwapchain,
      const VkSwapchainCreateInfoKHR* pCreateInfo);

    /**
     * \brief Create frame generation context
     * 
     * Creates FFX frame generation context for interpolation.
     * Must be called after swapchain proxy is created.
     */
    void createFrameGenContext(
      VkPhysicalDevice physicalDevice,
      VkDevice device,
      uint32_t displayWidth,
      uint32_t displayHeight,
      VkFormat backBufferFormat);

  private:
    void destroyContexts();

    // FFX contexts - using void* (FFX's ffxContext is typedef void*)
    void* m_swapChainContext = nullptr;
    void* m_frameGenContext = nullptr;
    
    // Frame generation config stored as opaque data (allocated in cpp)
    void* m_frameGenConfig = nullptr;

    // State tracking
    bool m_initialized = false;
    bool m_swapchainContextCreated = false;
    bool m_frameGenContextCreated = false;
    bool m_frameGenEnabled = false;
    
    uint2 m_displaySize = { 0, 0 };      // Render resolution (input to upscaler)
    uint2 m_swapchainSize = { 0, 0 };    // Swapchain/output resolution (for generationRect)
    uint64_t m_frameId = 0;

    // Replaced Vulkan functions from swapchain proxy
    PFN_vkAcquireNextImageKHR m_replacedAcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR m_replacedQueuePresentKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR m_replacedGetSwapchainImagesKHR = nullptr;
    
    // Swapchain handle (set by presenter)
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    
  public:
    void setSwapchain(VkSwapchainKHR swapchain) { m_swapchain = swapchain; }
    VkSwapchainKHR getSwapchain() const { return m_swapchain; }
  };

  /**
   * \brief FSR 3 Frame Generation Presenter
   * 
   * Custom presenter that uses FFX swapchain proxy for FSR 3 Frame Generation.
   * Similar to DxvkDLFGPresenter but uses AMD's FidelityFX SDK.
   */
  class DxvkFSRFGPresenter : public vk::Presenter {
  public:
    // NV-DXVK start: FSR FG support - Accept existing surface for runtime switching
    DxvkFSRFGPresenter(
      Rc<DxvkDevice> device,
      Rc<DxvkContext> ctx,
      HWND window,
      const Rc<vk::InstanceFn>& vki,
      const Rc<vk::DeviceFn>& vkd,
      vk::PresenterDevice presenterDevice,
      const vk::PresenterDesc& desc,
      VkSurfaceKHR existingSurface = VK_NULL_HANDLE);
    // NV-DXVK end

    ~DxvkFSRFGPresenter();

    VkResult acquireNextImage(vk::PresenterSync& sync, uint32_t& index, bool isDlfgPresenting) override;
    VkResult presentImage(
      std::atomic<VkResult>* status,
      const DxvkPresentInfo& presentInfo,
      const DxvkFrameInterpolationInfo& frameInterpolationInfo,
      std::uint32_t acquiredImageIndex,
      bool isDlfgPresenting,
      VkSetPresentConfigNV* presentMetering) override;
    VkResult recreateSwapChain(const vk::PresenterDesc& desc) override;
    
    // Override to use FFX SDK's replacement vkGetSwapchainImagesKHR which returns proxy images
    VkResult getSwapImages(std::vector<VkImage>& images);

  private:
    void createFFXContexts();
    void updateSwapchainImagesToProxy(DxvkFSRFrameGen& fsrFrameGen, VkFormat format);
    
    DxvkDevice* m_device;
    Rc<DxvkContext> m_ctx;
    VkSwapchainKHR m_fsrSwapchain = VK_NULL_HANDLE;
    bool m_contextCreated = false;
    bool m_recreatingSwapchain = false;  // Guard against re-entrant recreation
    VkExtent2D m_currentExtent = { 0, 0 };  // Track current swapchain size
    vk::PresenterDesc m_cachedDesc = {};
  };

} // namespace dxvk
