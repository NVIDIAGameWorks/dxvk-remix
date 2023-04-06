/*
* Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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

#include <vector>

#include "dxvk_include.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkImageView;
  class DxvkBuffer;
  
  /**
   * \brief Descriptor info
   * 
   * Stores information that is required to
   * update a single resource descriptor.
   */
  union DxvkDescriptorInfo {
    VkDescriptorImageInfo  image;
    VkDescriptorBufferInfo buffer;
    VkBufferView           texelBuffer;
    VkAccelerationStructureKHR accelerationStructure;
  };
  
  
  /**
   * \brief Descriptor pool
   * 
   * Wrapper around a Vulkan descriptor pool that
   * descriptor sets can be allocated from.
   */
  class DxvkDescriptorPool : public RcObject {
    
  public:
    
    // NV-DXVK start: use EXT_debug_utils
    DxvkDescriptorPool(
      const Rc<vk::InstanceFn>& vki,
      const Rc<vk::DeviceFn>& vkd
    );
    // NV-DXVK end

    // NV-DXVK start: Adding global bindless resources
    DxvkDescriptorPool(
      const Rc<vk::InstanceFn>& vki,
      const Rc<vk::DeviceFn>& vkd,
      const VkDescriptorPoolCreateInfo& info);
    // NV-DXVK end

    ~DxvkDescriptorPool();
    
    // NV-DXVK start: Adding global bindless resources
    /**
     * \brief Allocates a descriptor set
     * 
     * \param [in] layout Descriptor set layout
     * \returns The descriptor set
     */
    VkDescriptorSet alloc(VkDescriptorSetLayout layout, const char *name);
    // NV-DXVK end

    /**
     * \brief Resets descriptor set allocator
     * 
     * Destroys all descriptor sets and
     * resets the Vulkan descriptor pools.
     */
    void reset();
    
  private:
    // NV-DXVK start: use EXT_debug_utils
    Rc<vk::InstanceFn> m_vki;
    // NV-DXVK end
    Rc<vk::DeviceFn> m_vkd;
    VkDescriptorPool m_pool;
    
  };


  /**
   * \brief Descriptor pool tracker
   * 
   * Tracks descriptor pools that are either full
   * or no longer needed by the DXVK context. The
   * command list will reset and recycle all pools
   * once it has completed execution on the GPU.
   */
  class DxvkDescriptorPoolTracker {

  public:

    DxvkDescriptorPoolTracker(DxvkDevice* device);
    ~DxvkDescriptorPoolTracker();

    /**
     * \brief Adds a descriptor pool to track
     * \param [in] pool The descriptor pool
     */
    void trackDescriptorPool(Rc<DxvkDescriptorPool> pool);
    
    /**
     * \brief Resets event tracker
     * 
     * Resets all tracked descriptor pools
     * and returns them to the device.
     */
    void reset();

  private:

    DxvkDevice* m_device;

    std::vector<Rc<DxvkDescriptorPool>> m_pools;

  };

  struct DxvkDescriptor
  {
    static VkWriteDescriptorSet buffer(const VkDescriptorSet& set, const VkDescriptorBufferInfo& in, const VkDescriptorType t, const uint32_t bindingIdx);
    static VkWriteDescriptorSet buffer(const VkDescriptorSet& set, VkDescriptorBufferInfo* stagingInfo, const DxvkBuffer& bufferView, const VkDescriptorType t, const uint32_t bindingIdx);
    static VkWriteDescriptorSet texture(const VkDescriptorSet& set, const VkDescriptorImageInfo& in, const VkDescriptorType t, const uint32_t bindingIdx);
    static VkWriteDescriptorSet texture(const VkDescriptorSet& set, VkDescriptorImageInfo* stagingInfo, const DxvkImageView& imageView, const VkDescriptorType t, const uint32_t bindingIdx, VkSampler sampler = VK_NULL_HANDLE);
  };
  
}