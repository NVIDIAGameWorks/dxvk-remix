/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include <vector>

#include "../util/log/log.h"

#include "../util/util_error.h"
#include "../util/util_fps_limiter.h"
#include "../util/util_math.h"
#include "../util/util_string.h"

#include "vulkan_loader.h"

// NV-DXVK start: DLFG integration
namespace dxvk {
  struct DxvkPresentInfo;
  struct DxvkFrameInterpolationInfo;
};
// NV-DXVK end

namespace dxvk::vk {

  /**
   * \brief Presenter description
   * 
   * Contains the desired properties of
   * the swap chain. This is passed as
   * an input during swap chain creation.
   */
  struct PresenterDesc {
    VkExtent2D          imageExtent;
    uint32_t            imageCount;
    uint32_t            numFormats;
    VkSurfaceFormatKHR  formats[4];
    uint32_t            numPresentModes;
    VkPresentModeKHR    presentModes[4];
    VkFullScreenExclusiveEXT fullScreenExclusive;
  };

  /**
   * \brief Presenter properties
   * 
   * Contains the actual properties
   * of the underlying swap chain.
   */
  struct PresenterInfo {
    VkSurfaceFormatKHR  format;
    VkPresentModeKHR    presentMode;
    VkExtent2D          imageExtent;
    uint32_t            imageCount;
    // NV-DXVK start: App controlled FSE
    bool                appOwnedFSE;
    // NV-DXVK end
  };

  /**
   * \brief Presenter features
   */
  struct PresenterFeatures {
    bool                fullScreenExclusive : 1;
  };
  
  /**
   * \brief Adapter and queue
   */
  struct PresenterDevice {
    uint32_t            queueFamily = 0;
    VkQueue             queue       = VK_NULL_HANDLE;
    VkPhysicalDevice    adapter     = VK_NULL_HANDLE;
    PresenterFeatures   features    = { };
  };

  /**
   * \brief Swap image and view
   */
  struct PresenterImage {
    VkImage     image = VK_NULL_HANDLE;
    VkImageView view  = VK_NULL_HANDLE;
  };

  /**
   * \brief Presenter semaphores
   * 
   * Pair of semaphores used for acquire and present
   * operations, including the command buffers used
   * in between. Also stores a fence to signal on
   * image acquisition.
   */
  struct PresenterSync {
    VkSemaphore acquire;
    VkSemaphore present;
  };

  /**
   * \brief Vulkan presenter
   * 
   * Provides abstractions for some of the
   * more complicated aspects of Vulkan's
   * window system integration.
   */
  class Presenter : public RcObject {

  public:

    Presenter(
            HWND            window,
      const Rc<InstanceFn>& vki,
      const Rc<DeviceFn>&   vkd,
            PresenterDevice device,
      const PresenterDesc&  desc);

    // NV-DXVK start: DLFG integration
    virtual
      // NV-DXVK end
    ~Presenter();

    // NV-DXVK start: FSR FG support - Expose surface transfer for presenter switching
    /**
     * \brief Transfers surface from another presenter
     * 
     * Takes ownership of the surface from another presenter
     * to allow switching presenter types without recreating
     * the surface. The source presenter's surface will be
     * set to VK_NULL_HANDLE.
     * \param [in] other The presenter to take the surface from
     */
    void takeSurfaceFrom(Presenter* other);

    /**
     * \brief Gets the current surface handle
     * \returns The VkSurfaceKHR handle
     */
    VkSurfaceKHR getSurface() const {
      return m_surface;
    }

    /**
     * \brief Releases ownership of the surface
     * 
     * Returns the surface handle and sets internal handle to
     * VK_NULL_HANDLE so the destructor won't destroy it.
     * Caller is responsible for the surface lifetime.
     * \returns The VkSurfaceKHR handle (caller takes ownership)
     */
    VkSurfaceKHR releaseSurface() {
      VkSurfaceKHR surface = m_surface;
      m_surface = VK_NULL_HANDLE;
      return surface;
    }
    // NV-DXVK end

    /**
     * \brief Actual presenter info
     * \returns Swap chain properties
     */
    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    PresenterInfo info() const;

    /**
     * \brief Retrieves image by index
     * 
     * Can be used to create per-image objects.
     * \param [in] index Image index
     * \returns Image handle
     */
    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    PresenterImage getImage(
            uint32_t        index) const;

    // NV-DXVK start: DLFG integration
    /**
     * \brief Retrieves the current present wait semaphore
     *
     * Used when injecting work at present time from the
     * submit thread, for frame interpolation.
     */
    //VkSemaphore getCurrentPresentWaitSemaphore() const;
    // NV-DXVK end

    /**
     * \brief Acquires next image
     * 
     * Potentially blocks the calling thread.
     * If this returns an error, the swap chain
     * must be recreated and a new image must
     * be acquired before proceeding.
     * \param [out] sync Synchronization semaphores
     * \param [out] index Acquired image index
     * \returns Status of the operation
     */
    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    VkResult acquireNextImage(
            PresenterSync&  sync,
            uint32_t&       index,
            // NV-DXVK start: DLFG integration
            bool isDlfgPresenting = false
            // NV-DXVK end
      );
    
    /**
     * \brief Presents current image
     * 
     * Presents the current image. If this returns
     * an error, the swap chain must be recreated,
     * but do not present before acquiring an image.
     *
     * DLFG integration: this may result VK_EVENT_SET if present was queued for execution in a separate thread,
     * status will be updated once the corresponding present operation has landed
     *
     * \param [in] waitSemaphore Semaphore to wait on before present (if not present, uses the internal present semaphore)
     * \returns Status of the operation
     */
    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    VkResult presentImage(
      // NV-DXVK start: DLFG integration
      // xxxnsubtil: these are never used by vk::Presenter, only the DLFG presenter needs them --- split this method!
      std::atomic<VkResult>* presentStatus,
      const DxvkPresentInfo& presentInfo,
      const DxvkFrameInterpolationInfo& frameInterpolationInfo,
      // used by vk::Presenter if not -1
      std::uint32_t acquiredImageIndex = std::uint32_t(-1),
      bool isDlfgPresenting = false,
      VkSetPresentConfigNV* presentMetering = nullptr
      // NV-DXVK end
    );
    
    /**
     * \brief Changes presenter properties
     * 
     * Recreates the swap chain immediately. Note that
     * no swap chain resources must be in use by the
     * GPU at the time this is called.
     * \param [in] desc Swap chain description
     */
    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    VkResult recreateSwapChain(
      const PresenterDesc&  desc);

    /**
     * \brief Changes maximum frame rate
     *
     * \param [in] frameRate Target frame rate. Set
     *    to 0 in order to disable the limiter.
     */
    void setFrameRateLimit(double frameRate);

    /**
     * \brief Notifies frame rate limiter about the display refresh rate
     *
     * Used to dynamically disable the frame rate limiter in case
     * vertical synchronization is used and the target frame rate
     * roughly equals the display's refresh rate.
     * \param [in] refresnRate Current refresh rate
     */
    void setFrameRateLimiterRefreshRate(double refreshRate);

    /**
     * \brief Checks whether a Vulkan swap chain exists
     *
     * On Windows, there are situations where we cannot create
     * a swap chain as the surface size can reach zero, and no
     * presentation can be performed.
     * \returns \c true if the presenter has a swap chain.
     */
    bool hasSwapChain() const {
      return m_swapchain;
    }

    /**
     * \brief Gets the Vulkan swap chain handle
     *
     * Used by FSR Frame Generation to wrap the swapchain.
     * \returns The VkSwapchainKHR handle.
     */
    VkSwapchainKHR getSwapChain() const {
      return m_swapchain;
    }

    /**
     * \brief Acquires FSE
     *
     * When using app-controlled FSE, this function acquires the
     * FSE monitor context.
     */
    VkResult acquireFullscreenExclusive();

    /**
     * \brief Acquires FSE
     *
     * When using app-controlled FSE, this function releases the
     * FSE monitor context.
     */
    VkResult releaseFullscreenExclusive();

    // NV-DXVK start: Global window handle accessor
    HWND getWindowHandle() const {
      return m_window;
    }
    // NV-DXVK end

  // NV-DXVK start: DLFG integration
    virtual void synchronize() { }

  protected:
  // NV-DXVK end

    // NV-DXVK start: FSR FG support - Protected constructor for derived classes
    /**
     * \brief Protected constructor for derived classes
     * 
     * Creates a presenter using an existing surface, allowing
     * derived classes to take ownership of another presenter's
     * surface without recreating it. This avoids VK_ERROR_NATIVE_WINDOW_IN_USE_KHR
     * when switching presenter types at runtime.
     */
    Presenter(
            HWND            window,
      const Rc<InstanceFn>& vki,
      const Rc<DeviceFn>&   vkd,
            PresenterDevice device,
            VkSurfaceKHR    existingSurface);
    // NV-DXVK end

    Rc<InstanceFn>    m_vki;
    Rc<DeviceFn>      m_vkd;

    PresenterDevice   m_device;
    PresenterInfo     m_info;

    HWND              m_window      = nullptr;
    VkSurfaceKHR      m_surface     = VK_NULL_HANDLE;
    VkSwapchainKHR    m_swapchain   = VK_NULL_HANDLE;

    std::vector<PresenterImage> m_images;
    std::vector<PresenterSync>  m_semaphores;

    uint32_t m_imageIndex = 0;
    uint32_t m_frameIndex = 0;

    VkResult m_acquireStatus = VK_NOT_READY;

    FpsLimiter m_fpsLimiter;

    VkResult getSupportedFormats(
            std::vector<VkSurfaceFormatKHR>& formats,
      const PresenterDesc&            desc);
    
    VkResult getSupportedPresentModes(
            std::vector<VkPresentModeKHR>& modes,
      const PresenterDesc&            desc);
    
    VkResult getSwapImages(
            std::vector<VkImage>&     images);
    
    VkSurfaceFormatKHR pickFormat(
            uint32_t                  numSupported,
      const VkSurfaceFormatKHR*       pSupported,
            uint32_t                  numDesired,
      const VkSurfaceFormatKHR*       pDesired);

    VkPresentModeKHR pickPresentMode(
            uint32_t                  numSupported,
      const VkPresentModeKHR*         pSupported,
            uint32_t                  numDesired,
      const VkPresentModeKHR*         pDesired);

    VkExtent2D pickImageExtent(
      const VkSurfaceCapabilitiesKHR& caps,
            VkExtent2D                desired);

    // NV-DXVK start: DLFG integration
    virtual
    // NV-DXVK end
    uint32_t pickImageCount(
      const VkSurfaceCapabilitiesKHR& caps,
            VkPresentModeKHR          presentMode,
            uint32_t                  desired);

    VkResult createSurface();

    void destroySwapchain();

    void destroySurface();
  };

}
