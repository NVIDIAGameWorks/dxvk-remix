#include "dxvk_device.h"
#include "dxvk_staging.h"

namespace dxvk {
  
  DxvkStagingDataAlloc::DxvkStagingDataAlloc(const Rc<DxvkDevice>& device, const VkMemoryPropertyFlagBits memFlags, const VkBufferUsageFlags usageFlags, const VkPipelineStageFlags stages, const VkAccessFlags access)
    : m_device(device) 
	  , m_memoryFlags(memFlags)
    , m_usage(usageFlags)
    , m_stages(stages)
    , m_access(access)
  {

  }


  DxvkStagingDataAlloc::~DxvkStagingDataAlloc() {

  }

  DxvkBufferSlice DxvkStagingDataAlloc::alloc(VkDeviceSize align, VkDeviceSize size) {
    ScopedCpuProfileZone();

    if (size > MaxBufferSize)
      return DxvkBufferSlice(createBuffer(size));
    
    if (m_buffer == nullptr)
      m_buffer = createBuffer(MaxBufferSize);
    
    // Acceleration structure API accepts a VA, which DXVK doesnt recognize as "in use"
    if (!m_buffer->isInUse() && (m_usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) == 0)
      m_offset = 0;
    
    m_offset = dxvk::align(m_offset, align);

    if (m_offset + size > MaxBufferSize) {
      m_offset = 0;

      if (m_buffers.size() < MaxBufferCount)
        m_buffers.push(std::move(m_buffer));

      if (!m_buffers.front()->isInUse() && (m_usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) == 0) {
        m_buffer = std::move(m_buffers.front());
        m_buffers.pop();
      } else {
        m_buffer = createBuffer(MaxBufferSize);
      }
    }

    DxvkBufferSlice slice(m_buffer, m_offset, size);
    m_offset = dxvk::align(m_offset + size, align);
    return slice;
  }


  void DxvkStagingDataAlloc::trim() {
    m_buffer = nullptr;
    m_offset = 0;

    while (!m_buffers.empty())
      m_buffers.pop();
  }

  Rc<DxvkBuffer> DxvkStagingDataAlloc::createBuffer(VkDeviceSize size) {
    DxvkBufferCreateInfo info;
    info.size = size;
    info.access = m_access;
    info.stages = m_stages;
    info.usage = m_usage;

    return m_device->createBuffer(info, m_memoryFlags, DxvkMemoryStats::Category::AppBuffer);
  }
 
}
