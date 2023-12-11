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
#include "dxvk_buffer.h"
#include "dxvk_device.h"

#include <algorithm>
#include <numeric>

namespace dxvk {
  
  DxvkBuffer::DxvkBuffer(
          DxvkDevice*           device,
    const DxvkBufferCreateInfo& createInfo,
          DxvkMemoryAllocator&  memAlloc,
          VkMemoryPropertyFlags memFlags,
          DxvkMemoryStats::Category category)
  : m_device        (device),
    m_info          (createInfo),
    m_memAlloc      (&memAlloc),
    m_memFlags      (memFlags),
    m_category      (category) {
    // Align slices so that we don't violate any alignment
    // requirements imposed by the Vulkan device/driver
    VkDeviceSize sliceAlignment = computeSliceAlignment();
    m_physSliceLength = createInfo.size;
    m_physSliceStride = align(createInfo.size, sliceAlignment);
    m_physSliceCount  = std::max<VkDeviceSize>(1, 256 / m_physSliceStride);

    // Limit size of multi-slice buffers to reduce fragmentation
    constexpr VkDeviceSize MaxBufferSize = 256 << 10;

    m_physSliceMaxCount = MaxBufferSize >= m_physSliceStride
      ? MaxBufferSize / m_physSliceStride
      : 1;

    // Allocate the initial set of buffer slices
    m_buffer = allocBuffer(m_physSliceCount, category);

    DxvkBufferSliceHandle slice;
    slice.handle = m_buffer.buffer; 
    slice.offset = 0;
    slice.length = m_physSliceLength;
    slice.mapPtr = m_buffer.memory.mapPtr(0);

    m_physSlice = slice;
    m_lazyAlloc = m_physSliceCount > 1;
  }


  DxvkBuffer::~DxvkBuffer() {
    // NV-DXVK start: buffer clones for orphaned slices
    if (m_parent != nullptr) {
      // Clones own nothing. Bail out.
      return;
    }
    // NV-DXVK end

    const auto& vkd = m_device->vkd();

    for (const auto& buffer : m_buffers)
      vkd->vkDestroyBuffer(vkd->device(), buffer.buffer, nullptr);
    vkd->vkDestroyBuffer(vkd->device(), m_buffer.buffer, nullptr);
  }
  

  VkDeviceAddress DxvkBuffer::getDeviceAddress() {
    const auto& vkd = m_device->vkd();

    if (m_deviceAddress == 0) {
      VkBufferDeviceAddressInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
      bufferInfo.buffer = m_physSlice.handle;
      m_deviceAddress = vkd->vkGetBufferDeviceAddress(vkd->device(), &bufferInfo);
    }
    return m_deviceAddress;
  }

  DxvkBufferHandle DxvkBuffer::allocBuffer(VkDeviceSize sliceCount, DxvkMemoryStats::Category category) const {
    const auto& vkd = m_device->vkd();

    const bool isAccelerationStructure = m_info.usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    VkBufferCreateInfo info;
    info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.pNext                 = nullptr;
    info.flags                 = 0;
    info.size                  = m_physSliceStride * sliceCount;
    info.usage                 = m_info.usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if (!isAccelerationStructure && m_device->features().vulkan12Features.bufferDeviceAddress)
    {
      info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    if (info.usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
    {
      info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    info.sharingMode           = VK_SHARING_MODE_EXCLUSIVE;
    info.queueFamilyIndexCount = 0;
    info.pQueueFamilyIndices   = nullptr;
    
    DxvkBufferHandle handle;

    if (vkd->vkCreateBuffer(vkd->device(),
          &info, nullptr, &handle.buffer) != VK_SUCCESS) {
      throw DxvkError(str::format(
        "DxvkBuffer: Failed to create buffer:"
        "\n  size:  ", info.size,
        "\n  usage: ", info.usage));
    }

    VkMemoryDedicatedRequirements dedicatedRequirements;
    dedicatedRequirements.sType                       = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    dedicatedRequirements.pNext                       = VK_NULL_HANDLE;
    dedicatedRequirements.prefersDedicatedAllocation  = VK_FALSE;
    dedicatedRequirements.requiresDedicatedAllocation = VK_FALSE;
    
    VkMemoryRequirements2 memReq;
    memReq.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    memReq.pNext = &dedicatedRequirements;
    
    VkBufferMemoryRequirementsInfo2 memReqInfo;
    memReqInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
    memReqInfo.buffer = handle.buffer;
    memReqInfo.pNext  = VK_NULL_HANDLE;
    
    VkMemoryDedicatedAllocateInfo dedMemoryAllocInfo;
    dedMemoryAllocInfo.sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedMemoryAllocInfo.pNext  = VK_NULL_HANDLE;
    dedMemoryAllocInfo.buffer = handle.buffer;
    dedMemoryAllocInfo.image  = VK_NULL_HANDLE;

    vkd->vkGetBufferMemoryRequirements2(
       vkd->device(), &memReqInfo, &memReq);

    // NV-DXVK start: Increase memory requirement alignment based on override requirement.
    // Note: This increase in alignment is safe to do as long as the override alignment is less than or equal to the maximum alignment
    // required by the Vulkan spec (since raw device memory allocation will only gaurentee alignment in such cases even if custom
    // sub-allocating logic can handle greater alignments).
    memReq.memoryRequirements.alignment = std::lcm(memReq.memoryRequirements.alignment, m_info.requiredAlignmentOverride);
    // NV-DXVK end

    // xxxnsubtil: avoid bad interaction with DxvkStagingDataAlloc
    // when dedicated allocations are used, the implicit memory recycling in DxvkStagingDataAlloc goes away for larger buffers,
    // which are often used for BVH builds; dedicated is not very meaningful for buffers, so ignore the hint if
    // dedicated memory is not strictly required
    if (!dedicatedRequirements.requiresDedicatedAllocation) {
      dedicatedRequirements.prefersDedicatedAllocation = VK_FALSE;
    }

    // Use high memory priority for GPU-writable resources
    bool isGpuWritable = (m_info.access & (
      VK_ACCESS_SHADER_WRITE_BIT |
      VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT)) != 0;
    
    DxvkMemoryFlags hints(DxvkMemoryFlag::GpuReadable);

    if (isGpuWritable)
      hints.set(DxvkMemoryFlag::GpuWritable);

    // Ask driver whether we should be using a dedicated allocation
    handle.memory = m_memAlloc->alloc(&memReq.memoryRequirements,
      dedicatedRequirements, dedMemoryAllocInfo, m_memFlags, hints, category);
    
    if (vkd->vkBindBufferMemory(vkd->device(), handle.buffer,
        handle.memory.memory(), handle.memory.offset()) != VK_SUCCESS)
      throw DxvkError("DxvkBuffer: Failed to bind device memory");
    
    return handle;
  }


  VkDeviceSize DxvkBuffer::computeSliceAlignment() const {
    const auto& devInfo = m_device->properties().core.properties;

    VkDeviceSize result = sizeof(uint32_t);

    if (m_info.usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
      result = std::max(result, devInfo.limits.minUniformBufferOffsetAlignment);

    if (m_info.usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
      result = std::max(result, devInfo.limits.minStorageBufferOffsetAlignment);

    if (m_info.usage & (VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
      result = std::max(result, devInfo.limits.minTexelBufferOffsetAlignment);
      result = std::max(result, VkDeviceSize(16));
    }

    if (m_info.usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
     && m_info.size > (devInfo.limits.optimalBufferCopyOffsetAlignment / 2))
      result = std::max(result, devInfo.limits.optimalBufferCopyOffsetAlignment);

    // For some reason, Warhammer Chaosbane breaks otherwise
    if (m_info.usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
      result = std::max(result, VkDeviceSize(256));

    if (m_memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      result = std::max(result, devInfo.limits.nonCoherentAtomSize);
      result = std::max(result, VkDeviceSize(64));
    }

    return result;
  }

  // NV-DXVK start: buffer clones for orphaned slices
  DxvkBuffer::DxvkBuffer(DxvkBuffer& parent) 
    : m_device   (parent.m_device),
      m_info     (parent.m_info),
      m_memAlloc (parent.m_memAlloc),
      m_memFlags (parent.m_memFlags),
      m_category (parent.m_category) {
    m_buffer.buffer = parent.m_buffer.buffer;

    m_physSlice = parent.m_physSlice;
    m_vertexStride = parent.m_vertexStride;

    m_parent = &parent;
  }

  Rc<DxvkBuffer> DxvkBuffer::clone() {
    if (m_parent != nullptr) {
      throw DxvkError("Refusing to clone a clone!");
    }
    return new DxvkBuffer(*this);
  }
  // NV-DXVK end


  
  DxvkBufferView::DxvkBufferView(
    const Rc<vk::DeviceFn>&         vkd,
    const Rc<DxvkBuffer>&           buffer,
    const DxvkBufferViewCreateInfo& info)
  : m_vkd(vkd), m_info(info), m_buffer(buffer),
    m_bufferSlice (getSliceHandle()),
    m_bufferView  (createBufferView(m_bufferSlice)) {
    
  }
  
  
  DxvkBufferView::~DxvkBufferView() {
    if (m_views.empty()) {
      m_vkd->vkDestroyBufferView(
        m_vkd->device(), m_bufferView, nullptr);
    } else {
      for (const auto& pair : m_views) {
        m_vkd->vkDestroyBufferView(
          m_vkd->device(), pair.second, nullptr);
      }
    }
  }
  
  
  VkBufferView DxvkBufferView::createBufferView(
    const DxvkBufferSliceHandle& slice) {
    VkBufferViewCreateInfo viewInfo;
    viewInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
    viewInfo.pNext  = nullptr;
    viewInfo.flags  = 0;
    viewInfo.buffer = slice.handle;
    viewInfo.format = m_info.format;
    viewInfo.offset = slice.offset;
    viewInfo.range  = slice.length;
    
    VkBufferView result = VK_NULL_HANDLE;

    if (m_vkd->vkCreateBufferView(m_vkd->device(),
          &viewInfo, nullptr, &result) != VK_SUCCESS) {
      throw DxvkError(str::format(
        "DxvkBufferView: Failed to create buffer view:",
        "\n  Offset: ", viewInfo.offset,
        "\n  Range:  ", viewInfo.range,
        "\n  Format: ", viewInfo.format));
    }

    return result;
  }


  void DxvkBufferView::updateBufferView(
    const DxvkBufferSliceHandle& slice) {
    if (m_views.empty())
      m_views.insert({ m_bufferSlice, m_bufferView });
     
    m_bufferSlice = slice;
    
    auto entry = m_views.find(slice);
    if (entry != m_views.end()) {
      m_bufferView = entry->second;
    } else {
      m_bufferView = createBufferView(m_bufferSlice);
      m_views.insert({ m_bufferSlice, m_bufferView });
    }
  }
  
  // NV-DXVK start: implement acceleration structures
  DxvkAccelStructure::DxvkAccelStructure(
        DxvkDevice* device,
  const DxvkBufferCreateInfo& createInfo,
        DxvkMemoryAllocator& memAlloc,
        VkMemoryPropertyFlags memFlags,
        VkAccelerationStructureTypeKHR accelType)
    : DxvkBuffer(device, createInfo, memAlloc, memFlags, DxvkMemoryStats::Category::RTXAccelerationStructure) {

    VkAccelerationStructureCreateInfoKHR accelCreateInfo {};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.pNext = nullptr;
    accelCreateInfo.createFlags = 0;
    accelCreateInfo.buffer = DxvkBuffer::getBufferRaw();
    accelCreateInfo.offset = 0;
    accelCreateInfo.size = createInfo.size;
    accelCreateInfo.type = accelType;

    if (m_device->vkd()->vkCreateAccelerationStructureKHR(m_device->handle(), &accelCreateInfo, nullptr, &accelStructureRef) != VK_SUCCESS) {
      throw DxvkError(str::format(
        "DxvkAccelStructure: Failed to create acceleration structure:"
        "\n  size:  ", accelCreateInfo.size,
        "\n  type: ", accelType));
    }
  }

  DxvkAccelStructure::~DxvkAccelStructure() {
    if (accelStructureRef != VK_NULL_HANDLE) {
      const auto& vkd = m_device->vkd();
      vkd->vkDestroyAccelerationStructureKHR(m_device->handle(), accelStructureRef, nullptr);
    }
  }

  VkDeviceAddress DxvkAccelStructure::getAccelDeviceAddress() const {
    VkAccelerationStructureDeviceAddressInfoKHR deviceAddressInfo {};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    deviceAddressInfo.accelerationStructure = accelStructureRef;
    return m_device->vkd()->vkGetAccelerationStructureDeviceAddressKHR(m_device->handle(), &deviceAddressInfo);
  }
  // NV-DXVK end

  DxvkBufferTracker:: DxvkBufferTracker() { }
  DxvkBufferTracker::~DxvkBufferTracker() { }
  
  
  void DxvkBufferTracker::reset() {
    std::sort(m_entries.begin(), m_entries.end(),
      [] (const Entry& a, const Entry& b) {
        return a.slice.handle < b.slice.handle;
      });

    for (const auto& e : m_entries)
      e.buffer->freeSlice(e.slice);
      
    m_entries.clear();
  }
  
}