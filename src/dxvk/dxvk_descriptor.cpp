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
#include "dxvk_descriptor.h"
#include "dxvk_device.h"
#include "dxvk_image.h"
#include "dxvk_buffer.h"

namespace dxvk {

  VkWriteDescriptorSet DxvkDescriptor::texture(const VkDescriptorSet& set, const VkDescriptorImageInfo& in, const VkDescriptorType t, const uint32_t bindingIdx)
  {
    VkWriteDescriptorSet desc;
    desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    desc.pNext = nullptr;
    desc.dstSet = set;
    desc.dstBinding = bindingIdx;
    desc.dstArrayElement = 0;
    desc.descriptorCount = 1;
    desc.descriptorType = t;
    desc.pImageInfo = &in;
    desc.pBufferInfo = nullptr;
    desc.pTexelBufferView = nullptr;
    return desc;
  }

  VkWriteDescriptorSet DxvkDescriptor::texture(const VkDescriptorSet& set, VkDescriptorImageInfo* stagingInfo, const DxvkImageView& imageView, const VkDescriptorType t, const uint32_t bindingIdx, const VkSampler sampler)
  {
    stagingInfo->sampler = sampler;
    stagingInfo->imageView = imageView.handle();
    stagingInfo->imageLayout = imageView.imageInfo().layout;

    return texture(set, *stagingInfo, t, bindingIdx);
  };

  VkWriteDescriptorSet DxvkDescriptor::buffer(const VkDescriptorSet& set, const VkDescriptorBufferInfo& in, const VkDescriptorType t, const uint32_t bindingIdx)
  {
      VkWriteDescriptorSet desc;
      desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      desc.pNext = nullptr;
      desc.dstSet = set;
      desc.dstBinding = bindingIdx;
      desc.dstArrayElement = 0;
      desc.descriptorCount = 1;
      desc.descriptorType = t;
      desc.pImageInfo = nullptr;
      desc.pBufferInfo = &in;
      desc.pTexelBufferView = nullptr;
      return desc;
  }

  VkWriteDescriptorSet DxvkDescriptor::buffer(const VkDescriptorSet& set, VkDescriptorBufferInfo* stagingInfo, const DxvkBuffer& bufferView, const VkDescriptorType t, const uint32_t bindingIdx)
  {
    auto surfaceBufferSlice = bufferView.getSliceHandle();

    stagingInfo->buffer = surfaceBufferSlice.handle;
    stagingInfo->offset = surfaceBufferSlice.offset;
    stagingInfo->range = surfaceBufferSlice.length;

    return buffer(set, *stagingInfo, t, bindingIdx);
  };

  
// NV-DXVK start: use EXT_debug_utils
  DxvkDescriptorPool::DxvkDescriptorPool(const Rc<vk::InstanceFn>& vki, const Rc<vk::DeviceFn>& vkd)
    : m_vkd(vkd)
    , m_vki(vki) {
// NV-DXVK end
    const uint32_t maxSets = 8192;

    std::array<VkDescriptorPoolSize, 10> pools = { {
      { VK_DESCRIPTOR_TYPE_SAMPLER,                     maxSets * 2  },
      { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,               maxSets * 2  },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,               maxSets / 64 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,              maxSets * 4  },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,              maxSets * 1  },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,        maxSets * 1  },
      { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,        maxSets / 64 },
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,      maxSets * 1  },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,      maxSets },
      { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,  10 } } };

    VkDescriptorPoolCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.maxSets = maxSets;
    info.poolSizeCount = pools.size();
    info.pPoolSizes = pools.data();

    if (m_vkd->vkCreateDescriptorPool(m_vkd->device(), &info, nullptr, &m_pool) != VK_SUCCESS)
      throw DxvkError("DxvkDescriptorPool: Failed to create descriptor pool");
  }

  // NV-DXVK start: Adding global bindless resources
  DxvkDescriptorPool::DxvkDescriptorPool(const Rc<vk::InstanceFn>& vki, const Rc<vk::DeviceFn>& vkd, const VkDescriptorPoolCreateInfo& info)
    : m_vki(vki)
    , m_vkd(vkd) {

    if (m_vkd->vkCreateDescriptorPool(m_vkd->device(), &info, nullptr, &m_pool) != VK_SUCCESS)
      throw DxvkError("DxvkDescriptorPool: Failed to create descriptor pool");
  }
  // NV-DXVK end

  DxvkDescriptorPool::~DxvkDescriptorPool() {
    m_vkd->vkDestroyDescriptorPool(
      m_vkd->device(), m_pool, nullptr);
  }

  // NV-DXVK start: Adding global bindless resources
  VkDescriptorSet DxvkDescriptorPool::alloc(VkDescriptorSetLayout layout, const char *name) {
    VkDescriptorSetAllocateInfo info;
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.pNext = nullptr;
    info.descriptorPool = m_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (m_vkd->vkAllocateDescriptorSets(m_vkd->device(), &info, &set) != VK_SUCCESS)
      return VK_NULL_HANDLE;

    if (name && m_vkd->vkSetDebugUtilsObjectNameEXT) {
      VkDebugUtilsObjectNameInfoEXT nameInfo;
      nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
      nameInfo.pNext = nullptr;
      nameInfo.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET;
      nameInfo.objectHandle = (uint64_t) set;
      nameInfo.pObjectName = name;
      m_vkd->vkSetDebugUtilsObjectNameEXT(m_vkd->device(), &nameInfo);
    }

    return set;
  }
  // NV-DXVK end

  void DxvkDescriptorPool::reset() {
    m_vkd->vkResetDescriptorPool(
      m_vkd->device(), m_pool, 0);
  }

  DxvkDescriptorPoolTracker::DxvkDescriptorPoolTracker(DxvkDevice* device)
    : m_device(device) {

  }


  DxvkDescriptorPoolTracker::~DxvkDescriptorPoolTracker() {

  }


  void DxvkDescriptorPoolTracker::trackDescriptorPool(Rc<DxvkDescriptorPool> pool) {
    m_pools.push_back(std::move(pool));
  }


  void DxvkDescriptorPoolTracker::reset() {
    for (const auto& pool : m_pools) {
      pool->reset();
      m_device->recycleDescriptorPool(pool);
    }

    m_pools.clear();
  }
  
}