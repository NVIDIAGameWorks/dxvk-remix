#include "dxvk_buffer.h"
#include "dxvk_device.h"

namespace dxvk {
  
  DxvkBuffer::DxvkBuffer(
          DxvkDevice*           device,
    const DxvkBufferCreateInfo& createInfo,
          VkMemoryPropertyFlags memoryType)
  : m_device        (device),
    m_info          (createInfo),
    m_memFlags      (memoryType) {
    // Align physical buffer slices to 256 bytes, which guarantees
    // that we don't violate any Vulkan alignment requirements
    m_physSliceLength = createInfo.size;
    m_physSliceStride = align(createInfo.size, 256);
    
    // Allocate a single buffer slice
    m_physSlice = this->allocPhysicalBuffer(1)
      ->slice(0, m_physSliceStride);
  }


  DxvkBuffer::~DxvkBuffer() {
    
  }
  
  
  DxvkPhysicalBufferSlice DxvkBuffer::allocPhysicalSlice() {
    std::unique_lock<sync::Spinlock> freeLock(m_freeMutex);
    
    // If no slices are available, swap the two free lists.
    if (m_freeSlices.size() == 0) {
      std::unique_lock<sync::Spinlock> swapLock(m_swapMutex);
      std::swap(m_freeSlices, m_nextSlices);
    }
      
    // If there are still no slices available, create a new
    // physical buffer and add all slices to the free list.
    if (m_freeSlices.size() == 0) {
      std::unique_lock<sync::Spinlock> swapLock(m_swapMutex);
      m_physBuffer = this->allocPhysicalBuffer(m_physSliceCount);
      
      for (uint32_t i = 0; i < m_physSliceCount; i++) {
        m_freeSlices.push_back(m_physBuffer->slice(
          m_physSliceStride * i,
          m_physSliceLength));
      }
      
      m_physSliceCount *= 2;
    }
    
    // Take the first slice from the queue
    DxvkPhysicalBufferSlice result = std::move(m_freeSlices.back());
    m_freeSlices.pop_back();
    return result;
  }
  
  
  void DxvkBuffer::freePhysicalSlice(const DxvkPhysicalBufferSlice& slice) {
    // Add slice to a separate free list to reduce lock contention.
    std::unique_lock<sync::Spinlock> swapLock(m_swapMutex);

    // Discard slices allocated from other physical buffers.
    // This may make descriptor set binding more efficient.
    if (m_physBuffer->handle() == slice.handle())
      m_nextSlices.push_back(slice);
  }
  
  
  Rc<DxvkPhysicalBuffer> DxvkBuffer::allocPhysicalBuffer(VkDeviceSize sliceCount) const {
    DxvkBufferCreateInfo createInfo = m_info;
    createInfo.size = sliceCount * m_physSliceStride;
    
    return m_device->allocPhysicalBuffer(createInfo, m_memFlags);
  }
  
  
  DxvkBufferView::DxvkBufferView(
    const Rc<vk::DeviceFn>&         vkd,
    const Rc<DxvkBuffer>&           buffer,
    const DxvkBufferViewCreateInfo& info)
  : m_vkd(vkd), m_info(info), m_buffer(buffer),
    m_bufferSlice (m_buffer->getSliceHandle()),
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


  void DxvkBufferView::updateBufferView() {
    if (m_views.empty())
      m_views.insert({ m_bufferSlice, m_bufferView });
    
    m_bufferSlice = m_buffer->getSliceHandle();
    auto entry = m_views.find(m_bufferSlice);
    
    if (entry != m_views.end()) {
      m_bufferView = entry->second;
    } else {
      m_bufferView = createBufferView(m_bufferSlice);
      m_views.insert({ m_bufferSlice, m_bufferView });
    }
  }
  
  
  DxvkBufferTracker:: DxvkBufferTracker() { }
  DxvkBufferTracker::~DxvkBufferTracker() { }
  
  
  void DxvkBufferTracker::freeBufferSlice(
    const Rc<DxvkBuffer>&           buffer,
    const DxvkPhysicalBufferSlice&  slice) {
    m_entries.push_back({ buffer, slice });
  }
  
  
  void DxvkBufferTracker::reset() {
    for (const auto& e : m_entries)
      e.buffer->freePhysicalSlice(e.slice);
      
    m_entries.clear();
  }
  
}