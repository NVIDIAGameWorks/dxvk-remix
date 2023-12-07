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

  void BindlessResourceManager::prepareSceneData(const Rc<DxvkContext> ctx, const std::vector<TextureRef>& rtTextures, const std::vector<RaytraceBuffer>& rtBuffers, const std::vector<Rc<DxvkSampler>>& samplers) {
    ScopedCpuProfileZone();
    if (m_frameLastUpdated == m_device->getCurrentFrameId()) {
      Logger::debug("Updating bindless tables multiple times per frame...");
      return;
    }

    // Increment
    m_globalBindlessDescSetIdx = nextIdx();

    // Textures
    {
      std::vector<VkDescriptorImageInfo> imageInfo(rtTextures.size());

      uint32_t idx = 0;
      for (auto&& texRef : rtTextures) {
        DxvkImageView* imageView = texRef.getImageView();

        if (imageView != nullptr) {
          imageInfo[idx].sampler = nullptr;
          imageInfo[idx].imageView = imageView->handle();
          imageInfo[idx].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(imageView);
        } else {
          imageInfo[idx] = m_device->getCommon()->dummyResources().imageViewDescriptor(VK_IMAGE_VIEW_TYPE_2D, true);
        }

        ++idx;
      }

      assert(idx <= kMaxBindlessResources);

      if (idx > 0) {
        VkWriteDescriptorSet descWrites;
        descWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrites.pNext = nullptr;
        descWrites.dstSet = 0;//  This will be filled in by the BindlessTable 
        descWrites.dstBinding = 0;
        descWrites.dstArrayElement = 0;
        descWrites.descriptorCount = idx;
        descWrites.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descWrites.pImageInfo = &imageInfo[0];
        descWrites.pBufferInfo = nullptr;
        descWrites.pTexelBufferView = nullptr;
        m_tables[Table::Textures][currentIdx()]->updateDescriptors(descWrites);
      }
    }

    // Buffers
    {
      uint32_t idx = 0;
      std::vector<VkDescriptorBufferInfo> bufferInfo(rtBuffers.size());
      for (auto&& bufRef : rtBuffers) {
        if (bufRef.defined()) {
          bufferInfo[idx] = bufRef.getDescriptor().buffer;
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(bufRef.buffer());
        } else {
          bufferInfo[idx] = m_device->getCommon()->dummyResources().bufferDescriptor();
        }

        ++idx;
      }

      assert(idx <= kMaxBindlessResources);

      if (idx > 0) {
        VkWriteDescriptorSet descWrites;
        descWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrites.pNext = nullptr;
        descWrites.dstSet = 0;//  This will be filled in by the BindlessTable 
        descWrites.dstBinding = 0;
        descWrites.dstArrayElement = 0;
        descWrites.descriptorCount = idx;
        descWrites.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descWrites.pImageInfo = nullptr;
        descWrites.pBufferInfo = &bufferInfo[0];
        descWrites.pTexelBufferView = nullptr;
        m_tables[Table::Buffers][currentIdx()]->updateDescriptors(descWrites);
      }
    }

    // Samplers
    {
      std::vector<VkDescriptorImageInfo> imageInfo(samplers.size());

      uint32_t idx = 0;
      for (auto&& sampler : samplers) {
        if (sampler != nullptr) {
          imageInfo[idx].sampler = sampler->handle();
          imageInfo[idx].imageView = nullptr;
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(sampler);
        } else {
          imageInfo[idx] = m_device->getCommon()->dummyResources().samplerDescriptor();
        }

        ++idx;
      }

      assert(idx <= kMaxBindlessSamplers);

      if (idx > 0) {
        VkWriteDescriptorSet descWrites;
        descWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrites.pNext = nullptr;
        descWrites.dstSet = 0;//  This will be filled in by the BindlessTable 
        descWrites.dstBinding = 0;
        descWrites.dstArrayElement = 0;
        descWrites.descriptorCount = idx;
        descWrites.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        descWrites.pImageInfo = &imageInfo[0];
        descWrites.pBufferInfo = nullptr;
        descWrites.pTexelBufferView = nullptr;
        m_tables[Table::Samplers][currentIdx()]->updateDescriptors(descWrites);
      }
    }

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

  void BindlessResourceManager::BindlessTable::updateDescriptors(VkWriteDescriptorSet& set) {
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
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxBindlessResources * kMaxFramesInFlight },
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