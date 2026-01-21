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

#include <optional>

#include "dxvk_device_info.h"
#include "dxvk_extensions.h"
#include "dxvk_include.h"

namespace dxvk {
  
  class DxvkDevice;
  class DxvkInstance;
  
  /**
   * \brief GPU vendors
   * Based on PCIe IDs.
   */
  enum class DxvkGpuVendor : uint16_t {
    Amd    = 0x1002,
    Nvidia = 0x10de,
    Intel  = 0x8086,
  };

  /**
   * \brief Adapter memory heap info
   * 
   * Stores info about a heap, and the amount
   * of memory allocated from it by the app.
   */
  struct DxvkAdapterMemoryHeapInfo {
    VkMemoryHeapFlags heapFlags;
    VkDeviceSize memoryBudget;
    VkDeviceSize memoryAllocated;
  };

  /**
   * \brief Adapter memory info
   * 
   * Stores properties and allocation
   * info of each available heap.
   */
  struct DxvkAdapterMemoryInfo {
    uint32_t                  heapCount;
    DxvkAdapterMemoryHeapInfo heaps[VK_MAX_MEMORY_HEAPS];
  };

  /**
   * \brief Retrieves queue indices
   */
  struct DxvkAdapterQueueIndices {
    // Note: All queue families initialized to ignored here as this is used to check for a queue family's
    // presence (and initializing these is easy to forget otherwise). Be sure to initialize any new additions
    // to ignored as well in the future.
    uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
    uint32_t transfer = VK_QUEUE_FAMILY_IGNORED;
    // NV-DXVK start: RTXIO
    uint32_t asyncCompute = VK_QUEUE_FAMILY_IGNORED;
    // NV-DXVK end
    // NV-DXVK start: DLFG integration
    uint32_t present = VK_QUEUE_FAMILY_IGNORED;
    // NV-DXVK end
    // NV-DXVK start: FSR FG integration
    uint32_t imageAcquire = VK_QUEUE_FAMILY_IGNORED;
    uint32_t fsrPresent = VK_QUEUE_FAMILY_IGNORED;  // Present queue for FSR FG (must support presentation)
    // NV-DXVK end
  };

  // NV-DXVK begin: General Queue Allocation Improvements
  /**
   * \brief Queue family and (clamped) queue indices within families for a single queue
   */
  struct DxvkAdapterQueueInfo {
    std::uint32_t queueFamilyIndex;
    std::uint32_t queueIndex;
  };

  /**
   * \brief Queue family and (clamped) queue indices within families for all queues
   */
  struct DxvkAdapterQueueInfos {
    // Note: Graphics/transfer queues are required, rest are optional.
    DxvkAdapterQueueInfo graphics{};
    DxvkAdapterQueueInfo transfer{};
    std::optional<DxvkAdapterQueueInfo> asyncCompute{};
    std::optional<DxvkAdapterQueueInfo> present{};
    // NV-DXVK start: FSR FG integration
    std::optional<DxvkAdapterQueueInfo> imageAcquire{};
    std::optional<DxvkAdapterQueueInfo> fsrPresent{};  // Present queue for FSR FG (must support presentation)
    // NV-DXVK end
  };
  // NV-DXVK end
  
  /**
   * \brief DXVK adapter
   * 
   * Corresponds to a physical device in Vulkan. Provides
   * all kinds of information about the device itself and
   * the supported feature set.
   */
  class DxvkAdapter : public RcObject {
    
  public:
    
    DxvkAdapter(
      const Rc<vk::InstanceFn>& vki,
            VkPhysicalDevice    handle);
    ~DxvkAdapter();
    
    /**
     * \brief Vulkan instance functions
     * \returns Vulkan instance functions
     */
    Rc<vk::InstanceFn> vki() const {
      return m_vki;
    }
    
    /**
     * \brief Physical device handle
     * \returns The adapter handle
     */
    VkPhysicalDevice handle() const {
      return m_handle;
    }
    
    /**
     * \brief Physical device properties
     * 
     * Returns a read-only reference to the core
     * properties of the Vulkan physical device.
     * \returns Physical device core properties
     */
    const VkPhysicalDeviceProperties& deviceProperties() const {
      return m_deviceInfo.core.properties;
    }

    /**
     * \brief Device info
     * 
     * Returns a read-only reference to the full
     * device info structure, including extended
     * properties.
     * \returns Device info struct
     */
    const DxvkDeviceInfo& devicePropertiesExt() const {
      return m_deviceInfo;
    }
    
    /**
     * \brief Supportred device features
     * 
     * Queries the supported device features.
     * \returns Device features
     */
    const DxvkDeviceFeatures& features() const {
      return m_deviceFeatures;
    }
    
    /**
     * \brief Retrieves memory heap info
     * 
     * Returns properties of all available memory heaps,
     * both device-local and non-local heaps, and the
     * amount of memory allocated from those heaps by
     * logical devices.
     * \returns Memory heap info
     */
    DxvkAdapterMemoryInfo getMemoryHeapInfo() const;
    
    /**
     * \brief Memory properties
     * 
     * Queries the memory types and memory heaps of
     * the device. This is useful for memory allocators.
     * \returns Device memory properties
     */
    VkPhysicalDeviceMemoryProperties memoryProperties() const;

    /**
     * \brief Queries format support
     * 
     * \param [in] format The format to query
     * \returns Format support info
     */
    VkFormatProperties formatProperties(
      VkFormat format) const;
    
    /**
     * \brief Queries image format support
     * 
     * \param [in] format Format to query
     * \param [in] type Image type
     * \param [in] tiling Image tiling
     * \param [in] usage Image usage flags
     * \param [in] flags Image create flags
     * \param [out] properties Format properties
     * \returns \c VK_SUCCESS or \c VK_ERROR_FORMAT_NOT_SUPPORTED
     */
    VkResult imageFormatProperties(
      VkFormat                  format,
      VkImageType               type,
      VkImageTiling             tiling,
      VkImageUsageFlags         usage,
      VkImageCreateFlags        flags,
      VkImageFormatProperties&  properties) const;
    
    /**
     * \brief Retrieves queue family indices
     * \returns Indices for all queue families
     */
    DxvkAdapterQueueIndices findQueueFamilies() const;

    /**
     * \brief Tests whether all required features are supported
     * 
     * \param [in] features Required device features
     * \returns \c true if all features are supported
     */
    bool checkFeatureSupport(
      const DxvkDeviceFeatures& required) const;
    
    /**
     * \brief Enables extensions for this adapter
     *
     * When creating a device, all extensions that
     * are added using this method will be enabled
     * in addition to the ones required by DXVK.
     * This is used for OpenVR support.
     */
    void enableExtensions(
      const DxvkNameSet&        extensions);
    
    /**
     * \brief Creates a DXVK device
     * 
     * Creates a logical device for this adapter.
     * \param [in] instance Parent instance
     * \param [in] enabledFeatures Device features
     * \returns Device handle
     */
    Rc<DxvkDevice> createDevice(
      const Rc<DxvkInstance>&   instance,
            DxvkDeviceFeatures  enabledFeatures);
    
    /**
     * \brief Registers memory allocation
     * 
     * Updates memory alloc info accordingly.
     * \param [in] heap Memory heap index
     * \param [in] bytes Allocation size
     */
    void notifyHeapMemoryAlloc(
            uint32_t            heap,
            VkDeviceSize        bytes);
    
    /**
     * \brief Registers memory deallocation
     * 
     * Updates memory alloc info accordingly.
     * \param [in] heap Memory heap index
     * \param [in] bytes Allocation size
     */
    void notifyHeapMemoryFree(
            uint32_t            heap,
            VkDeviceSize        bytes);
    
    /**
     * \brief Tests if the driver matches certain criteria
     *
     * \param [in] vendor GPU vendor
     * \param [in] driver Driver. Ignored when the
     *    driver properties extension is not supported.
     * \param [in] minVer Match versions starting with this one
     * \param [in] maxVer Match versions lower than this one
     * \returns \c True if the driver matches these criteria
     */
    bool matchesDriver(
            DxvkGpuVendor       vendor,
            VkDriverIdKHR       driver,
            uint32_t            minVer,
            uint32_t            maxVer) const;
    
    /**
     * \brief Logs DXVK adapter info
     * 
     * May be useful for bug reports
     * and general troubleshooting.
     */
    void logAdapterInfo() const;
    
    /**
     * \brief Checks whether this is a UMA system
     *
     * Basically tests whether all heaps are device-local.
     * Can be used for various optimizations in client APIs.
     * \returns \c true if the system has unified memory.
     */
    bool isUnifiedMemoryArchitecture() const;
    
  private:
    
    Rc<vk::InstanceFn>  m_vki;
    VkPhysicalDevice    m_handle;

    DxvkNameSet         m_extraExtensions;
    DxvkNameSet         m_deviceExtensions;
    DxvkDeviceInfo      m_deviceInfo;
    DxvkDeviceFeatures  m_deviceFeatures;

    bool                m_hasMemoryBudget;
    
    std::vector<VkQueueFamilyProperties> m_queueFamilies;

    std::array<std::atomic<VkDeviceSize>, VK_MAX_MEMORY_HEAPS> m_heapAlloc;

    void initHeapAllocInfo();
    void queryExtensions();
    void queryDeviceInfo();
    void queryDeviceFeatures();
    void queryDeviceQueues();

    uint32_t findQueueFamily(
            VkQueueFlags          mask,
            VkQueueFlags          flags
      ) const;
    
    static void logNameList(const DxvkNameList& names);
    static void logFeatures(const DxvkDeviceFeatures& features);
    static void logQueueFamilies(const DxvkAdapterQueueIndices& queues);
    
  };

  const std::string getDriverVersionString(const uint32_t version);  
}
