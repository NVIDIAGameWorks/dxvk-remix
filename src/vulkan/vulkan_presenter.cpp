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
#include "vulkan_presenter.h"
#include "../dxvk/dxvk_scoped_annotation.h"

#include "../dxvk/dxvk_format.h"
#include "../util/util_monitor.h"

namespace dxvk::vk {

  Presenter::Presenter(
          HWND            window,
    const Rc<InstanceFn>& vki,
    const Rc<DeviceFn>&   vkd,
          PresenterDevice device,
    const PresenterDesc&  desc)
  : m_vki(vki), m_vkd(vkd), m_device(device), m_window(window) {
    // As of Wine 5.9, winevulkan provides this extension, but does
    // not filter the pNext chain for VkSwapchainCreateInfoKHR properly
    // before passing it to the Linux sude, which breaks RenderDoc.
    if (m_device.features.fullScreenExclusive && ::GetModuleHandle("winevulkan.dll")) {
      Logger::warn("winevulkan detected, disabling exclusive fullscreen support");
      m_device.features.fullScreenExclusive = false;
    }

    if (createSurface() != VK_SUCCESS)
      throw DxvkError("Failed to create surface");

    if (recreateSwapChain(desc) != VK_SUCCESS)
      throw DxvkError("Failed to create swap chain");
  }

  
  Presenter::~Presenter() {
    destroySwapchain();
    destroySurface();
  }

  // NV-DXVK start: FSR FG support - Protected constructor with existing surface
  Presenter::Presenter(
          HWND            window,
    const Rc<InstanceFn>& vki,
    const Rc<DeviceFn>&   vkd,
          PresenterDevice device,
          VkSurfaceKHR    existingSurface)
  : m_vki(vki), m_vkd(vkd), m_device(device), m_window(window), m_surface(existingSurface) {
    // As of Wine 5.9, winevulkan provides this extension, but does
    // not filter the pNext chain for VkSwapchainCreateInfoKHR properly
    // before passing it to the Linux side, which breaks RenderDoc.
    if (m_device.features.fullScreenExclusive && ::GetModuleHandle("winevulkan.dll")) {
      Logger::warn("winevulkan detected, disabling exclusive fullscreen support");
      m_device.features.fullScreenExclusive = false;
    }
    // Note: derived class is responsible for calling recreateSwapChain
    // since the FFX swapchain proxy needs to be created first
  }

  void Presenter::takeSurfaceFrom(Presenter* other) {
    if (other && other->m_surface != VK_NULL_HANDLE) {
      // Take ownership of the surface
      m_surface = other->m_surface;
      m_window = other->m_window;
      // Clear the other presenter's surface so it doesn't destroy it
      other->m_surface = VK_NULL_HANDLE;
    }
  }
  // NV-DXVK end


  PresenterInfo Presenter::info() const {
    return m_info;
  }


  PresenterImage Presenter::getImage(uint32_t index) const {
    return m_images.at(index);
  }

  //VkSemaphore Presenter::getCurrentPresentWaitSemaphore() const {
  //  return m_semaphores.at(m_frameIndex).present;
  //}

  VkResult Presenter::acquireNextImage(PresenterSync& sync, uint32_t& index,
                                       // NV-DXVK start: DLFG integration
                                       bool isDlfgPresenting
                                       // NV-DXVK end
                                       ) {
    ScopedCpuProfileZone();

    sync = m_semaphores.at(m_frameIndex);

    // NV-DXVK start: DLFG integration
    if (isDlfgPresenting) {
      // DLFG manages swapchain images directly and can have more than one acquire outstanding at a time
      m_acquireStatus = m_vkd->vkAcquireNextImageKHR(m_vkd->device(),
                                                     m_swapchain,
                                                     std::numeric_limits<uint64_t>::max(),
                                                     sync.acquire,
                                                     VK_NULL_HANDLE,
                                                     &index);
      assert(m_acquireStatus != VK_NOT_READY);
    } else {
      // Don't acquire more than one image at a time
      if (m_acquireStatus == VK_NOT_READY) {
        m_acquireStatus = m_vkd->vkAcquireNextImageKHR(m_vkd->device(),
          m_swapchain, std::numeric_limits<uint64_t>::max(),
          sync.acquire, VK_NULL_HANDLE, &m_imageIndex);
      }
    }

    if (m_acquireStatus == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
      acquireFullscreenExclusive();

    if (m_acquireStatus != VK_SUCCESS && m_acquireStatus != VK_SUBOPTIMAL_KHR)
      return m_acquireStatus;

    if (!isDlfgPresenting) {
      index = m_imageIndex;
    }

    return m_acquireStatus;
  }

  VkResult Presenter::presentImage(
    // NV-DXVK start: DLFG integration
    std::atomic<VkResult>*,
    const DxvkPresentInfo&,
    const DxvkFrameInterpolationInfo&,
    std::uint32_t imageIndex,
    bool isDlfgPresenting,
    VkSetPresentConfigNV* presentMetering
    // NV-DXVK end
  ) {
    ScopedCpuProfileZone();
    // NV-DXVK start: DLFG integration
    PresenterSync sync;
    
    sync = m_semaphores.at(m_frameIndex);
    // NV-DXVK end

    VkPresentInfoKHR info;
    info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.pNext              = presentMetering;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores    = &sync.present;
    info.swapchainCount     = 1;
    info.pSwapchains        = &m_swapchain;
    // NV-DXVK start: DLFG integration
    if (isDlfgPresenting) {
      info.pImageIndices = &imageIndex;
    } else {
      info.pImageIndices = &m_imageIndex;
    }
    // NV-DXVK end
    info.pResults           = nullptr;

    VkResult status = m_vkd->vkQueuePresentKHR(m_device.queue, &info);

    if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR)
      return status;

    // NV-DXVK start: App Controlled FSE
    if (status == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
      acquireFullscreenExclusive();
    // NV-DXVK end

    // NV-DXVK start: DLFG integration
    if (!isDlfgPresenting) {
    // NV-DXVK end
      // Try to acquire next image already, in order to hide
      // potential delays from the application thread.
      m_frameIndex += 1;
      m_frameIndex %= m_semaphores.size();

      sync = m_semaphores.at(m_frameIndex);

      m_acquireStatus = m_vkd->vkAcquireNextImageKHR(m_vkd->device(),
        m_swapchain, std::numeric_limits<uint64_t>::max(),
        sync.acquire, VK_NULL_HANDLE, &m_imageIndex);
    }

    bool vsync = m_info.presentMode == VK_PRESENT_MODE_FIFO_KHR
              || m_info.presentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;

    m_fpsLimiter.delay(vsync);
    return status;
  }

  
  VkResult Presenter::recreateSwapChain(const PresenterDesc& desc) {
    if (m_swapchain)
      destroySwapchain();

    // Query surface capabilities. Some properties might
    // have changed, including the size limits and supported
    // present modes, so we'll just query everything again.
    VkSurfaceCapabilitiesKHR        caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   modes;

    VkResult status;
    
    if ((status = m_vki->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_device.adapter, m_surface, &caps)) != VK_SUCCESS) {
      if (status == VK_ERROR_SURFACE_LOST_KHR) {
        // Recreate the surface and try again.
        if (m_surface)
          destroySurface();
        if ((status = createSurface()) != VK_SUCCESS)
          return status;
        status = m_vki->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            m_device.adapter, m_surface, &caps);
      }
      if (status != VK_SUCCESS)
        return status;
    }

    if ((status = getSupportedFormats(formats, desc)) != VK_SUCCESS)
      return status;

    if ((status = getSupportedPresentModes(modes, desc)) != VK_SUCCESS)
      return status;

    // Select actual swap chain properties and create swap chain
    m_info.format       = pickFormat(formats.size(), formats.data(), desc.numFormats, desc.formats);
    m_info.presentMode  = pickPresentMode(modes.size(), modes.data(), desc.numPresentModes, desc.presentModes);
    m_info.imageExtent  = pickImageExtent(caps, desc.imageExtent);
    m_info.imageCount   = pickImageCount(caps, m_info.presentMode, desc.imageCount);

    // NV-DXVK start: App controlled FSE
    m_info.appOwnedFSE  = m_device.features.fullScreenExclusive && (desc.fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT);
    // NV-DXVK end

    if (!m_info.imageExtent.width || !m_info.imageExtent.height) {
      m_info.imageCount = 0;
      m_info.format     = { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      return VK_SUCCESS;
    }

    // NV-DXVK start: App controlled FSE
    VkSurfaceFullScreenExclusiveInfoEXT fullScreenInfo;
    fullScreenInfo.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    fullScreenInfo.pNext = nullptr;
    fullScreenInfo.fullScreenExclusive = desc.fullScreenExclusive;

    VkSurfaceFullScreenExclusiveWin32InfoEXT fullScreenInfoWin32;
    if (m_info.appOwnedFSE) {
      fullScreenInfoWin32.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
      fullScreenInfoWin32.pNext = nullptr;
      fullScreenInfoWin32.hmonitor = GetDefaultMonitor();
      fullScreenInfo.pNext = &fullScreenInfoWin32;
    }
    // NV-DXVK end

    VkSwapchainCreateInfoKHR swapInfo;
    swapInfo.sType                  = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.pNext                  = nullptr;
    swapInfo.flags                  = 0;
    swapInfo.surface                = m_surface;
    swapInfo.minImageCount          = m_info.imageCount;
    swapInfo.imageFormat            = m_info.format.format;
    swapInfo.imageColorSpace        = m_info.format.colorSpace;
    swapInfo.imageExtent            = m_info.imageExtent;
    swapInfo.imageArrayLayers       = 1;
    swapInfo.imageUsage             = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                    | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    // NV-DXVK start: Add storage bit for Frameview because it runs computer shader
                                    | VK_IMAGE_USAGE_STORAGE_BIT;
    // NV-DXVK end
    swapInfo.imageSharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.queueFamilyIndexCount  = 0;
    swapInfo.pQueueFamilyIndices    = nullptr;
    swapInfo.preTransform           = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapInfo.compositeAlpha         = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode            = m_info.presentMode;
    swapInfo.clipped                = VK_TRUE;
    swapInfo.oldSwapchain           = m_swapchain;

    if (m_device.features.fullScreenExclusive)
      swapInfo.pNext = &fullScreenInfo;

    Logger::info(str::format(
      "Presenter: Actual swap chain properties:"
      "\n  Format:       ", m_info.format.format,
      "\n  Present mode: ", m_info.presentMode,
      "\n  Buffer size:  ", m_info.imageExtent.width, "x", m_info.imageExtent.height,
      "\n  Image count:  ", m_info.imageCount,
      "\n  Exclusive FS: ", desc.fullScreenExclusive));
    
    if ((status = m_vkd->vkCreateSwapchainKHR(m_vkd->device(), &swapInfo, nullptr, &m_swapchain)) != VK_SUCCESS) {

      const auto errString(str::format("Presenter: vkCreateSwapchainKHR failed, error code: ", status));

      if (swapInfo.pNext) {
        Logger::warn(errString);
        Logger::info("Presenter: retrying to create swap chain without Exclusive FS");

        m_info.appOwnedFSE = false;
        swapInfo.pNext = nullptr;

        if ((status = m_vkd->vkCreateSwapchainKHR(m_vkd->device(), &swapInfo, nullptr, &m_swapchain)) != VK_SUCCESS) {
          Logger::err(str::format("Presenter: vkCreateSwapchainKHR failed again, error code: ", status, ". Giving up."));

          return status;
        }
      }
      else {
        Logger::err(errString);
        return status;
      }
    }

    // NV-DXVK start: App Controlled FSE
    acquireFullscreenExclusive();
    // NV-DXVK end

    // Acquire images and create views
    std::vector<VkImage> images;

    if ((status = getSwapImages(images)) != VK_SUCCESS)
      return status;
    
    // Update actual image count
    m_info.imageCount = images.size();
    m_images.resize(m_info.imageCount);

    for (uint32_t i = 0; i < m_info.imageCount; i++) {
      m_images[i].image = images[i];

      VkImageViewCreateInfo viewInfo;
      viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.pNext    = nullptr;
      viewInfo.flags    = 0;
      viewInfo.image    = images[i];
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format   = m_info.format.format;
      viewInfo.components = VkComponentMapping {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
      viewInfo.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, 1, 0, 1 };
      
      if ((status = m_vkd->vkCreateImageView(m_vkd->device(),
          &viewInfo, nullptr, &m_images[i].view)) != VK_SUCCESS)
        return status;
    }

    // Create one set of semaphores per swap image
    m_semaphores.resize(m_info.imageCount);

    for (uint32_t i = 0; i < m_semaphores.size(); i++) {
      VkSemaphoreCreateInfo semInfo;
      semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      semInfo.pNext = nullptr;
      semInfo.flags = 0;

      if ((status = m_vkd->vkCreateSemaphore(m_vkd->device(),
          &semInfo, nullptr, &m_semaphores[i].acquire)) != VK_SUCCESS)
        return status;

      if ((status = m_vkd->vkCreateSemaphore(m_vkd->device(),
          &semInfo, nullptr, &m_semaphores[i].present)) != VK_SUCCESS)
        return status;

      // NV-DXVK start: add debug names to VkImage objects
      if (m_vkd->vkSetDebugUtilsObjectNameEXT) {
        VkDebugUtilsObjectNameInfoEXT nameInfo;
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.pNext = nullptr;
        nameInfo.objectType = VK_OBJECT_TYPE_SEMAPHORE;
        nameInfo.objectHandle = (uint64_t) m_semaphores[i].acquire;
        nameInfo.pObjectName = "Presenter: acquire semaphore";
        m_vkd->vkSetDebugUtilsObjectNameEXT(m_vkd->device(), &nameInfo);
        
        nameInfo.objectHandle = (uint64_t) m_semaphores[i].present;
        nameInfo.pObjectName = "Presenter: present semaphore";
        m_vkd->vkSetDebugUtilsObjectNameEXT(m_vkd->device(), &nameInfo);
      }
      // NV-DXVK end
    }
    
    // Invalidate indices
    m_imageIndex = 0;
    m_frameIndex = 0;
    m_acquireStatus = VK_NOT_READY;
    return VK_SUCCESS;
  }


  void Presenter::setFrameRateLimit(double frameRate) {
    m_fpsLimiter.setTargetFrameRate(frameRate);
  }


  void Presenter::setFrameRateLimiterRefreshRate(double refreshRate) {
    m_fpsLimiter.setDisplayRefreshRate(refreshRate);
  }


  VkResult Presenter::getSupportedFormats(std::vector<VkSurfaceFormatKHR>& formats, const PresenterDesc& desc) {
    uint32_t numFormats = 0;

    VkSurfaceFullScreenExclusiveInfoEXT fullScreenInfo;
    fullScreenInfo.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    fullScreenInfo.pNext = nullptr;
    fullScreenInfo.fullScreenExclusive = desc.fullScreenExclusive;

    VkSurfaceFullScreenExclusiveWin32InfoEXT fullScreenInfoWin32;

    if (desc.fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT) {
      fullScreenInfoWin32.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
      fullScreenInfoWin32.pNext = nullptr;
      fullScreenInfoWin32.hmonitor = GetDefaultMonitor();
      fullScreenInfo.pNext = &fullScreenInfoWin32;
    }

    VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo;
    surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
    surfaceInfo.pNext = &fullScreenInfo;
    surfaceInfo.surface = m_surface;

    VkResult status;
    
    if (m_device.features.fullScreenExclusive) {
      status = m_vki->vkGetPhysicalDeviceSurfaceFormats2KHR(
        m_device.adapter, &surfaceInfo, &numFormats, nullptr);
    } else {
      status = m_vki->vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_device.adapter, m_surface, &numFormats, nullptr);
    }

    if (status != VK_SUCCESS)
      return status;
    
    formats.resize(numFormats);

    if (m_device.features.fullScreenExclusive) {
      std::vector<VkSurfaceFormat2KHR> tmpFormats(numFormats, 
        { VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR, nullptr, VkSurfaceFormatKHR() });

      status = m_vki->vkGetPhysicalDeviceSurfaceFormats2KHR(
        m_device.adapter, &surfaceInfo, &numFormats, tmpFormats.data());

      for (uint32_t i = 0; i < numFormats; i++)
        formats[i] = tmpFormats[i].surfaceFormat;
    } else {
      status = m_vki->vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_device.adapter, m_surface, &numFormats, formats.data());
    }

    return status;
  }

  
  VkResult Presenter::getSupportedPresentModes(std::vector<VkPresentModeKHR>& modes, const PresenterDesc& desc) {
    uint32_t numModes = 0;

    VkSurfaceFullScreenExclusiveInfoEXT fullScreenInfo;
    fullScreenInfo.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
    fullScreenInfo.pNext = nullptr;
    fullScreenInfo.fullScreenExclusive = desc.fullScreenExclusive;
    
    VkSurfaceFullScreenExclusiveWin32InfoEXT fullScreenInfoWin32;

    if (desc.fullScreenExclusive == VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT) {
      fullScreenInfoWin32.sType = VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
      fullScreenInfoWin32.pNext = nullptr;
      fullScreenInfoWin32.hmonitor = GetDefaultMonitor();
      fullScreenInfo.pNext = &fullScreenInfoWin32;
    }

    VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo;
    surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
    surfaceInfo.pNext = &fullScreenInfo;
    surfaceInfo.surface = m_surface;

    VkResult status;

    if (m_device.features.fullScreenExclusive) {
      status = m_vki->vkGetPhysicalDeviceSurfacePresentModes2EXT(
        m_device.adapter, &surfaceInfo, &numModes, nullptr);
    } else {
      status = m_vki->vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_device.adapter, m_surface, &numModes, nullptr);
    }

    if (status != VK_SUCCESS)
      return status;
    
    modes.resize(numModes);

    if (m_device.features.fullScreenExclusive) {
      status = m_vki->vkGetPhysicalDeviceSurfacePresentModes2EXT(
        m_device.adapter, &surfaceInfo, &numModes, modes.data());
    } else {
      status = m_vki->vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_device.adapter, m_surface, &numModes, modes.data());
    }

    return status;
  }


  VkResult Presenter::getSwapImages(std::vector<VkImage>& images) {
    uint32_t imageCount = 0;

    VkResult status = m_vkd->vkGetSwapchainImagesKHR(
      m_vkd->device(), m_swapchain, &imageCount, nullptr);
    
    if (status != VK_SUCCESS)
      return status;
    
    images.resize(imageCount);

    return m_vkd->vkGetSwapchainImagesKHR(
      m_vkd->device(), m_swapchain, &imageCount, images.data());
  }


  VkSurfaceFormatKHR Presenter::pickFormat(
          uint32_t                  numSupported,
    const VkSurfaceFormatKHR*       pSupported,
          uint32_t                  numDesired,
    const VkSurfaceFormatKHR*       pDesired) {
    if (numDesired > 0) {
      // If the implementation allows us to freely choose
      // the format, we'll just use the preferred format.
      if (numSupported == 1 && pSupported[0].format == VK_FORMAT_UNDEFINED)
        return pDesired[0];
      
      // If the preferred format is explicitly listed in
      // the array of supported surface formats, use it
      for (uint32_t i = 0; i < numDesired; i++) {
        for (uint32_t j = 0; j < numSupported; j++) {
          if (pSupported[j].format     == pDesired[i].format
           && pSupported[j].colorSpace == pDesired[i].colorSpace)
            return pSupported[j];
        }
      }

      // If that didn't work, we'll fall back to a format
      // which has similar properties to the preferred one
      DxvkFormatFlags prefFlags = imageFormatInfo(pDesired[0].format)->flags;

      for (uint32_t j = 0; j < numSupported; j++) {
        auto currFlags = imageFormatInfo(pSupported[j].format)->flags;

        if ((currFlags & DxvkFormatFlag::ColorSpaceSrgb)
         == (prefFlags & DxvkFormatFlag::ColorSpaceSrgb))
          return pSupported[j];
      }
    }
    
    // Otherwise, fall back to the first supported format
    return pSupported[0];
  }


  VkPresentModeKHR Presenter::pickPresentMode(
          uint32_t                  numSupported,
    const VkPresentModeKHR*         pSupported,
          uint32_t                  numDesired,
    const VkPresentModeKHR*         pDesired) {
    // Just pick the first desired and supported mode
    for (uint32_t i = 0; i < numDesired; i++) {
      for (uint32_t j = 0; j < numSupported; j++) {
        if (pSupported[j] == pDesired[i])
          return pSupported[j];
      }
    }
    
    // Guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
  }


  VkExtent2D Presenter::pickImageExtent(
    const VkSurfaceCapabilitiesKHR& caps,
          VkExtent2D                desired) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
      return caps.currentExtent;
    
    VkExtent2D actual;
    actual.width  = clamp(desired.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    actual.height = clamp(desired.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
  }


  uint32_t Presenter::pickImageCount(
    const VkSurfaceCapabilitiesKHR& caps,
          VkPresentModeKHR          presentMode,
          uint32_t                  desired) {
    uint32_t count = caps.minImageCount;
    
    if (presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR)
      count = caps.minImageCount + 1;
    
    if (count < desired)
      count = desired;
    
    if (count > caps.maxImageCount && caps.maxImageCount != 0)
      count = caps.maxImageCount;
    
    return count;
  }


  VkResult Presenter::createSurface() {
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(
      GetWindowLongPtr(m_window, GWLP_HINSTANCE));
    
    VkWin32SurfaceCreateInfoKHR info;
    info.sType      = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.pNext      = nullptr;
    info.flags      = 0;
    info.hinstance  = instance;
    info.hwnd       = m_window;
    
    VkResult status = m_vki->vkCreateWin32SurfaceKHR(
      m_vki->instance(), &info, nullptr, &m_surface);
    
    if (status != VK_SUCCESS)
      return status;
    
    VkBool32 supportStatus = VK_FALSE;

    if ((status = m_vki->vkGetPhysicalDeviceSurfaceSupportKHR(m_device.adapter,
        m_device.queueFamily, m_surface, &supportStatus)) != VK_SUCCESS)
      return status;
    
    if (!supportStatus) {
      m_vki->vkDestroySurfaceKHR(m_vki->instance(), m_surface, nullptr);
      return VK_ERROR_OUT_OF_HOST_MEMORY; // just abuse this
    }

    return VK_SUCCESS;
  }


  void Presenter::destroySwapchain() {
    releaseFullscreenExclusive();

    for (const auto& img : m_images)
      m_vkd->vkDestroyImageView(m_vkd->device(), img.view, nullptr);
    
    for (const auto& sem : m_semaphores) {
      m_vkd->vkDestroySemaphore(m_vkd->device(), sem.acquire, nullptr);
      m_vkd->vkDestroySemaphore(m_vkd->device(), sem.present, nullptr);
    }

    m_vkd->vkDestroySwapchainKHR(m_vkd->device(), m_swapchain, nullptr);

    m_images.clear();
    m_semaphores.clear();

    m_swapchain = VK_NULL_HANDLE;
  }


  void Presenter::destroySurface() {
    m_vki->vkDestroySurfaceKHR(m_vki->instance(), m_surface, nullptr);
  }

  // NV-DXVK start: App Controlled FSE
  VkResult Presenter::acquireFullscreenExclusive() {
    if (!m_info.appOwnedFSE)
      return VK_SUCCESS;

    if (!m_swapchain)
      return VK_ERROR_UNKNOWN;

    VkResult result = m_vkd->vkAcquireFullScreenExclusiveModeEXT(m_vkd->device(), m_swapchain);

    // Already acquired?
    if (result == VK_ERROR_INITIALIZATION_FAILED)
      return VK_SUCCESS;

    if (result == VK_SUCCESS) {
      Logger::debug("Acquired Fullscreen Exclusive");
    } else {
      Logger::warn("Fullscreen exclusive failed to acquire"); // This is not the end of the world.
    }

    return result;
  }

  VkResult Presenter::releaseFullscreenExclusive() {
    if (!m_info.appOwnedFSE)
      return VK_SUCCESS;

    if (!m_swapchain)
      return VK_ERROR_UNKNOWN;

    VkResult result = m_vkd->vkReleaseFullScreenExclusiveModeEXT(m_vkd->device(), m_swapchain);

    // Already released?
    if (result == VK_ERROR_INITIALIZATION_FAILED)
      return VK_SUCCESS;

    if (result == VK_SUCCESS) {
      Logger::debug("Released Fullscreen Exclusive");
    } else {
      Logger::err("Fullscreen exclusive failed to release"); // This is bad.
    }

    return result;
  }
  // NV-DXVK end
}
