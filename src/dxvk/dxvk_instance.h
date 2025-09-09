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
#pragma once

#include "../util/config/config.h"

#include "dxvk_adapter.h"
#include "dxvk_device_filter.h"
#include "dxvk_extension_provider.h"
#include "dxvk_options.h"

namespace dxvk {
  
  /**
   * \brief DXVK instance
   * 
   * Manages a Vulkan instance and stores a list
   * of adapters. This also provides methods for
   * device creation.
   */
  class DxvkInstance : public RcObject {
    
  public:
    
    DxvkInstance();
    ~DxvkInstance();
    
    /**
     * \brief Vulkan instance functions
     * \returns Vulkan instance functions
     */
    Rc<vk::InstanceFn> vki() const {
      return m_vki;
    }
    
    /**
     * \brief Vulkan instance handle
     * \returns The instance handle
     */
    VkInstance handle() {
      return m_vki->instance();
    }

     /**
     * \brief Number of adapters
     * 
     * \returns The number of adapters
     */
    size_t adapterCount() {
      return m_adapters.size();
    }
    
    /**
     * \brief Retrieves an adapter
     * 
     * Note that the adapter does not hold
     * a hard reference to the instance.
     * \param [in] index Adapter index
     * \returns The adapter, or \c nullptr.
     */
    Rc<DxvkAdapter> enumAdapters(
            uint32_t      index) const;
    
    /**
     * \brief Finds adapter by LUID
     * 
     * \param [in] luid Pointer to LUID
     * \returns Matching adapter, if any
     */
    Rc<DxvkAdapter> findAdapterByLuid(
      const void*         luid) const;
    
    /**
     * \brief Finds adapter by device IDs
     * 
     * \param [in] vendorId Vendor ID
     * \param [in] deviceId Device ID
     * \returns Matching adapter, if any
     */
    Rc<DxvkAdapter> findAdapterByDeviceId(
            uint16_t      vendorId,
            uint16_t      deviceId) const;
    
    /**
     * \brief Retrieves configuration options
     * 
     * The configuration set contains user-defined
     * options as well as app-specific options.
     * \returns Configuration options
     */
    const Config& config() const {
      return m_config;
    }

    /**
     * \brief DXVK options
     * \returns DXVK options
     */
    const DxvkOptions& options() const {
      return m_options;
    }

    /**
     * \brief Enabled instance extensions
     * \returns Enabled instance extensions
     */
    const DxvkInstanceExtensions& extensions() const {
      return m_extensions;
    }

  private:
    Config              m_config;
    DxvkOptions         m_options;
    // NV-DXVK start: Integrate Aftermath
    bool                m_aftermathEnabled = false;
    // NV-DXVK end

    Rc<vk::LibraryFn>       m_vkl;
    Rc<vk::InstanceFn>      m_vki;
    DxvkInstanceExtensions  m_extensions;
    // NV-DXVK start: use EXT_debug_utils
    VkDebugUtilsMessengerEXT m_debugUtilsMessenger = nullptr;
    // NV-DXVK end

    std::vector<DxvkExtensionProvider*> m_extProviders;
    std::vector<Rc<DxvkAdapter>> m_adapters;
    
    VkInstance createInstance();

    std::vector<Rc<DxvkAdapter>> queryAdapters();
    
    static void logNameList(const DxvkNameList& names);
    
    // NV-DXVK start: Custom config loading/logging
    std::array<Config,Config::Type_kSize> m_confs;
    void initConfigs();
    template<Config::Type type>
    void initConfig();
    // NV-DXVK end 
  };
  
}
