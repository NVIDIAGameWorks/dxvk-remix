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

#include "../dxvk_device.h"
#include "../dxvk_context.h"
#include "rtx_scene_manager.h"
#include "rtx_resources.h"
#include "rtx_bindless_resource_manager.h"

#include "../shaders/rtx/pass/common_binding_indices.h"
#include "../dxvk_descriptor.h"

namespace dxvk {

  BindlessResourceManager::BindlessResourceManager(DxvkDevice* device)
  : CommonDeviceObject(device) { 
    for (int i = 0; i < kMaxFramesInFlight; i++) {
      m_tables[Table::Textures][i].reset(new BindlessTable(this));
      m_tables[Table::Buffers][i].reset(new BindlessTable(this));
      m_tables[Table::Samplers][i].reset(new BindlessTable(this));
    }

    createGlobalBindlessDescPool();
  }

  const Rc<vk::DeviceFn> BindlessResourceManager::BindlessTable::vkd() const {
    return m_pManager->m_device->vkd();
  }

  VkDescriptorSet BindlessResourceManager::getGlobalBindlessTableSet(Table type) const {
    if (m_frameLastUpdated != m_device->getCurrentFrameId())
      throw DxvkError("Getting bindless table before it's been updated for this frame!!");

    return m_tables[type][currentIdx()]->bindlessDescSet;
  }

  template<VkDescriptorType Type, typename T, typename U>
  void BindlessResourceManager::createDescriptorSet(const Rc<DxvkContext>& ctx, const std::vector<U>& engineObjects, const T& dummyDescriptor) {
    const size_t numDescriptors = std::max((size_t) 1, engineObjects.size()); // Must always leave 1 to have a valid binding set
    assert(numDescriptors <= kMaxBindlessResources);

    std::vector<T> descriptorInfos(numDescriptors);
    descriptorInfos[0] = dummyDescriptor; // we set the first descriptor to be a dummy (size is always at least 1) and overwrite it if there are valid engine objects

    uint32_t idx = 0;
    for (auto&& engineObject : engineObjects) {
      descriptorInfos[idx] = dummyDescriptor;

      if constexpr (Type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
        DxvkImageView* imageView = engineObject.getImageView();
        if (imageView != nullptr) {
          descriptorInfos[idx].sampler = nullptr;
          descriptorInfos[idx].imageView = imageView->handle();
          descriptorInfos[idx].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(imageView);
        }
      } else if constexpr (Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        if (engineObject.defined()) {
          descriptorInfos[idx] = engineObject.getDescriptor().buffer;
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(engineObject.buffer());
        }
      } else if constexpr (Type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        if (engineObject != nullptr) {
          descriptorInfos[idx].sampler = engineObject->handle();
          descriptorInfos[idx].imageView = nullptr;
        }
      } else {
        static_assert(Type != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || Type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || Type != VK_DESCRIPTOR_TYPE_SAMPLER, "Support for this descriptor type has not been implemented yet.");
        return;
      }

      ++idx;
    }

    VkWriteDescriptorSet descWrites;
    memset(&descWrites, 0, sizeof(descWrites));
    descWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites.descriptorCount = numDescriptors;
    descWrites.descriptorType = Type;

    if constexpr (std::is_same_v<T, VkDescriptorImageInfo>) {
      descWrites.pImageInfo = &descriptorInfos[0];
    } else if constexpr (std::is_same_v<T, VkDescriptorBufferInfo>) {
      descWrites.pBufferInfo = &descriptorInfos[0];
    }

    switch (Type) {
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      m_tables[Table::Textures][currentIdx()]->updateDescriptors(descWrites);
      break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      m_tables[Table::Buffers][currentIdx()]->updateDescriptors(descWrites);
      break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
      m_tables[Table::Samplers][currentIdx()]->updateDescriptors(descWrites);
      break;
    default:
      break;
    }
  }

  void BindlessResourceManager::prepareSceneData(const Rc<DxvkContext> ctx, const std::vector<TextureRef>& rtTextures, const std::vector<RaytraceBuffer>& rtBuffers, const std::vector<Rc<DxvkSampler>>& samplers) {
    ScopedCpuProfileZone();
    if (m_frameLastUpdated == m_device->getCurrentFrameId()) {
      Logger::debug("Updating bindless tables multiple times per frame...");
      return;
    }

    // Increment
    m_globalBindlessDescSetIdx = nextIdx();

    const VkDescriptorImageInfo dummyImage = m_device->getCommon()->dummyResources().imageViewDescriptor(VK_IMAGE_VIEW_TYPE_2D, true);
    const VkDescriptorBufferInfo dummyBuffer = m_device->getCommon()->dummyResources().bufferDescriptor();
    const VkDescriptorImageInfo dummySampler = m_device->getCommon()->dummyResources().samplerDescriptor();

    createDescriptorSet<VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE>(ctx, rtTextures, dummyImage);
    createDescriptorSet<VK_DESCRIPTOR_TYPE_STORAGE_BUFFER>(ctx, rtBuffers, dummyBuffer);
    createDescriptorSet<VK_DESCRIPTOR_TYPE_SAMPLER>(ctx, samplers, dummySampler);

    m_frameLastUpdated = m_device->getCurrentFrameId();
  }

  BindlessResourceManager::BindlessTable::~BindlessTable() {
    if (layout != VK_NULL_HANDLE) {
      vkd()->vkDestroyDescriptorSetLayout(vkd()->device(), layout, nullptr);
    }
  }

  void BindlessResourceManager::BindlessTable::createLayout(const VkDescriptorType type) {
    assert(bindlessDescSet == nullptr); // can't update the layout if we already allocated a descriptor

    static const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

    VkDescriptorSetLayoutBinding binding;
    binding.descriptorType = type;
    binding.descriptorCount = kMaxBindlessResources;
    binding.binding = 0; // Tables always bound at 0
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT |
                          VK_SHADER_STAGE_RAYGEN_BIT_KHR | 
                          VK_SHADER_STAGE_ANY_HIT_BIT_KHR | 
                          VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | 
                          VK_SHADER_STAGE_INTERSECTION_BIT_KHR | 
                          VK_SHADER_STAGE_CALLABLE_BIT_KHR | 
                          VK_SHADER_STAGE_MISS_BIT_KHR;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    layoutInfo.flags = 0;

    VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, nullptr };
    extendedInfo.bindingCount = 1;
    extendedInfo.pBindingFlags = &flags;

    layoutInfo.pNext = &extendedInfo;

    if (vkd()->vkCreateDescriptorSetLayout(m_pManager->m_device->vkd()->device(), &layoutInfo, nullptr, &layout) != VK_SUCCESS)
      throw DxvkError("BindlessTable: Failed to create descriptor set layout");
  }

  void BindlessResourceManager::BindlessTable::updateDescriptors(VkWriteDescriptorSet set) {
    if (bindlessDescSet == nullptr) {
      // Allocate the descriptor set
      bindlessDescSet = m_pManager->m_globalBindlessPool[m_pManager->currentIdx()]->alloc(layout, "bindless descriptor set");
      if (bindlessDescSet == nullptr) {
        Logger::err(str::format("BindlessTable: failed to allocate a descriptor set for ", set.descriptorCount, " ",
                                (set.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) ? "buffers" : "textures"));
        return;
      }
    }

    // Update the write descriptor with our set
    set.dstSet = bindlessDescSet;

    // Do the write
    vkd()->vkUpdateDescriptorSets(vkd()->device(), 1, &set, 0, nullptr);
  }

  void BindlessResourceManager::createGlobalBindlessDescPool() {
    // Create bindless descriptor pool
    static std::array<VkDescriptorPoolSize, Table::Count> pools = { {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          kMaxBindlessResources * kMaxFramesInFlight },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         kMaxBindlessResources * kMaxFramesInFlight },
        { VK_DESCRIPTOR_TYPE_SAMPLER,                kMaxBindlessResources * kMaxFramesInFlight }
    } };

    VkDescriptorPoolCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.maxSets = pools.size() * kMaxFramesInFlight;
    info.poolSizeCount = pools.size();
    info.pPoolSizes = pools.data();

    // Create the global pool
    for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
      m_globalBindlessPool[i] = new DxvkDescriptorPool(m_device->instance()->vki(), m_device->vkd(), info);
      m_tables[Table::Textures][i]->createLayout(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      m_tables[Table::Buffers][i]->createLayout(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      m_tables[Table::Samplers][i]->createLayout(VK_DESCRIPTOR_TYPE_SAMPLER);
    }
  }

} // namespace dxvk 