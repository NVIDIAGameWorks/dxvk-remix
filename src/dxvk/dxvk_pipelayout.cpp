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
#include <cstring>

#include "dxvk_descriptor.h"
#include "dxvk_limits.h"
#include "dxvk_pipelayout.h"

namespace dxvk {
  
  DxvkDescriptorSlotMapping:: DxvkDescriptorSlotMapping() { }
  DxvkDescriptorSlotMapping::~DxvkDescriptorSlotMapping() { }
  
  
  void DxvkDescriptorSlotMapping::defineSlot(
          VkShaderStageFlagBits stage,
    const DxvkResourceSlot&     desc) {
    uint32_t bindingId = this->getBindingId(desc.slot);
    
    if (bindingId != InvalidBinding) {
      m_descriptorSlots[bindingId].stages |= stage;
      m_descriptorSlots[bindingId].access |= desc.access;
    } else {
      DxvkDescriptorSlot slotInfo;
      slotInfo.slot   = desc.slot;
      slotInfo.type   = desc.type;
      slotInfo.view   = desc.view;
      slotInfo.stages = stage;
      slotInfo.access = desc.access;
      slotInfo.count  = desc.count;
      slotInfo.flags  = desc.flags;
      m_descriptorSlots.push_back(slotInfo);
    }
  }


  void DxvkDescriptorSlotMapping::definePushConstRange(
          VkShaderStageFlagBits stage,
          uint32_t              offset,
          uint32_t              size) {
    m_pushConstRange.stageFlags |= stage;
    m_pushConstRange.size = std::max(
      m_pushConstRange.size, offset + size);
  }
  
  
  uint32_t DxvkDescriptorSlotMapping::getBindingId(uint32_t slot) const {
    // This won't win a performance competition, but the number
    // of bindings used by a shader is usually much smaller than
    // the number of resource slots available to the system.
    for (uint32_t i = 0; i < m_descriptorSlots.size(); i++) {
      if (m_descriptorSlots[i].slot == slot)
        return i;
    }
    
    return InvalidBinding;
  }
  
  
  void DxvkDescriptorSlotMapping::makeDescriptorsDynamic(
          uint32_t              uniformBuffers,
          uint32_t              storageBuffers) {
    if (this->countDescriptors(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) <= uniformBuffers)
      this->replaceDescriptors(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
  }


  uint32_t DxvkDescriptorSlotMapping::countDescriptors(
          VkDescriptorType      type) const {
    uint32_t count = 0;

    for (const auto& slot : m_descriptorSlots)
      count += slot.type == type ? 1 : 0;
    
    return count;
  }


  void DxvkDescriptorSlotMapping::replaceDescriptors(
          VkDescriptorType      oldType,
          VkDescriptorType      newType) {
    for (auto& slot : m_descriptorSlots) {
      if (slot.type == oldType)
        slot.type = newType;
    }
  }


  DxvkPipelineLayout::DxvkPipelineLayout(
    const Rc<vk::DeviceFn>&   vkd,
    const DxvkDescriptorSlotMapping& slotMapping,
          VkPipelineBindPoint pipelineBindPoint,
    // NV-DXVK start: descriptor set(s)
          std::vector<VkDescriptorSetLayout> extraLayouts)
    // NV-DXVK end
  : m_vkd           (vkd),
    m_pushConstRange(slotMapping.pushConstRange()),
    m_bindingSlots  (slotMapping.bindingCount()),
    m_hasExtraLayouts (extraLayouts.size() > 0) {

    auto bindingCount = slotMapping.bindingCount();
    auto bindingInfos = slotMapping.bindingInfos();
    
    if (bindingCount > MaxNumActiveBindings)
      throw DxvkError(str::format("Too many active bindings in pipeline layout (", bindingCount, ")"));
    
    for (uint32_t i = 0; i < bindingCount; i++)
      m_bindingSlots[i] = bindingInfos[i];
    
    std::vector<VkDescriptorSetLayoutBinding>    bindings(bindingCount);
    std::vector<VkDescriptorUpdateTemplateEntry> tEntries(bindingCount);
    std::vector<VkDescriptorBindingFlags>        flags(bindingCount);
    bool hasFlags = false;
    
    for (uint32_t i = 0; i < bindingCount; i++) {
      bindings[i].binding            = i;
      bindings[i].descriptorType     = bindingInfos[i].type;
      bindings[i].descriptorCount    = bindingInfos[i].count;
      bindings[i].stageFlags         = bindingInfos[i].stages;
      bindings[i].pImmutableSamplers = nullptr;
      
      tEntries[i].dstBinding      = i;
      tEntries[i].dstArrayElement = 0;
      tEntries[i].descriptorCount = 1; // Do not use update template for bindless textures/buffer arrays
      tEntries[i].descriptorType  = bindingInfos[i].type;
      tEntries[i].offset          = sizeof(DxvkDescriptorInfo) * i;
      tEntries[i].stride          = 0;

      flags[i] = bindingInfos[i].flags;
      hasFlags |= (bindingInfos[i].flags != 0);

      if (bindingInfos[i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
        m_dynamicSlots.push_back(i);
      
      // NV-DXVK start: Replace unsuitable Flags set function with new boolean setting function
      setDescriptorType(bindingInfos[i].type);
      // NV-DXVK end
    }
    
    // Create descriptor set layout. We do not need to
    // create one if there are no active resource bindings.
    if (bindingCount > 0) {
      VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
      bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
      bindingFlags.bindingCount = bindingCount;
      bindingFlags.pBindingFlags = flags.data();

      VkDescriptorSetLayoutCreateInfo dsetInfo;
      dsetInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      dsetInfo.pNext        = hasFlags ? &bindingFlags : nullptr;
      dsetInfo.flags        = 0;
      dsetInfo.bindingCount = bindings.size();
      dsetInfo.pBindings    = bindings.data();
      
      if (m_vkd->vkCreateDescriptorSetLayout(m_vkd->device(),
            &dsetInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        throw DxvkError("DxvkPipelineLayout: Failed to create descriptor set layout");
    }
    
    // NV-DXVK start: descriptor set(s)
    std::vector<VkDescriptorSetLayout> descSetLayouts;

    if(bindingCount > 0)
      descSetLayouts.push_back(m_descriptorSetLayout);

    // NV-DXVK start: descriptor set(s)
    for (auto&& layout : extraLayouts)
      descSetLayouts.push_back(layout);
    // NV-DXVK end
 
    // Create pipeline layout with the given descriptor set layout
    VkPipelineLayoutCreateInfo pipeInfo;
    pipeInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeInfo.pNext                  = nullptr;
    pipeInfo.flags                  = 0;
    // NV-DXVK start: descriptor set(s)
    pipeInfo.setLayoutCount         = descSetLayouts.size();
    pipeInfo.pSetLayouts            = descSetLayouts.data();
    // NV-DXVK end
    pipeInfo.pushConstantRangeCount = 0;
    pipeInfo.pPushConstantRanges    = nullptr;

    if (m_pushConstRange.size) {
      pipeInfo.pushConstantRangeCount = 1;
      pipeInfo.pPushConstantRanges    = &m_pushConstRange;
    }
    
    if (m_vkd->vkCreatePipelineLayout(m_vkd->device(),
        &pipeInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
      m_vkd->vkDestroyDescriptorSetLayout(m_vkd->device(), m_descriptorSetLayout, nullptr);
      throw DxvkError("DxvkPipelineLayout: Failed to create pipeline layout");
    }
    
    // Create descriptor update template. If there are no active
    // resource bindings, there won't be any descriptors to update.
    if (bindingCount > 0) {
      VkDescriptorUpdateTemplateCreateInfo templateInfo;
      templateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
      templateInfo.pNext = nullptr;
      templateInfo.flags = 0;
      templateInfo.descriptorUpdateEntryCount = tEntries.size();
      templateInfo.pDescriptorUpdateEntries   = tEntries.data();
      templateInfo.templateType               = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
      templateInfo.descriptorSetLayout        = m_descriptorSetLayout;
      templateInfo.pipelineBindPoint          = pipelineBindPoint;
      templateInfo.pipelineLayout             = m_pipelineLayout;
      templateInfo.set                        = 0;
      
      if (m_vkd->vkCreateDescriptorUpdateTemplate(
          m_vkd->device(), &templateInfo, nullptr, &m_descriptorTemplate) != VK_SUCCESS) {
        m_vkd->vkDestroyDescriptorSetLayout(m_vkd->device(), m_descriptorSetLayout, nullptr);
        m_vkd->vkDestroyPipelineLayout(m_vkd->device(), m_pipelineLayout, nullptr);
        throw DxvkError("DxvkPipelineLayout: Failed to create descriptor update template");
      }
    }
  }
  
  
  DxvkPipelineLayout::~DxvkPipelineLayout() {
    m_vkd->vkDestroyDescriptorUpdateTemplate(
      m_vkd->device(), m_descriptorTemplate, nullptr);
    
    m_vkd->vkDestroyPipelineLayout(
      m_vkd->device(), m_pipelineLayout, nullptr);
    
    m_vkd->vkDestroyDescriptorSetLayout(
      m_vkd->device(), m_descriptorSetLayout, nullptr);
  }
  
}