#pragma once

#include <queue>

#include "dxvk_buffer.h"

namespace dxvk {
  
  class DxvkDevice;

  /**
   * \brief Staging data allocator
   *
   * Allocates buffer slices for resource uploads,
   * while trying to keep the number of allocations
   * but also the amount of allocated memory low.
   * 
   * Note that this is a copy of the old DxvkStagingDataAlloc structure,
   * which was removed in upstream (commit d262bebd9090)
   */
  class RtxStagingDataAlloc {
    constexpr static VkDeviceSize MaxBufferSize  = 1 << 25; // 32 MiB
    constexpr static uint32_t     MaxBufferCount = 2;
  public:

    RtxStagingDataAlloc(const Rc<DxvkDevice>& device,
                        const char* name,
                        const VkMemoryPropertyFlagBits memFlags = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                        const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TRANSFER_BIT,
                        const VkAccessFlags access = VK_ACCESS_TRANSFER_READ_BIT,
                        const VkDeviceSize bufferRequiredAlignmentOverride = 1);

    ~RtxStagingDataAlloc();

    /**
     * \brief Alloctaes a staging buffer slice
     * 
     * \param [in] align Alignment of the allocation
     * \param [in] size Size of the allocation
     * \returns Staging buffer slice
     */
    // Note: The alignment passed to this function is only used to align the allocation within the DXVK staging data buffer itself.
    // This means that the buffer's base address plus the offset returned in this slice may not be aligned to the desired alignment.
    // To mitigate this, ensure the buffer's memory requirements are modified before being allocated to have the maximum alignment the
    // staging data is expected to require. Usually the memory requirements will already hold the required alignment for the allocation,
    // but this is not always the case when alignment requirements come from how the buffer is actually used rather than its usage flags.
    DxvkBufferSlice alloc(VkDeviceSize align, VkDeviceSize size);

    /**
     * \brief Deletes all staging buffers
     * 
     * Destroys allocated buffers and
     * releases all buffer memory.
     */
    void trim();

  private:

    const VkMemoryPropertyFlagBits m_memoryFlags;
    const VkBufferUsageFlags m_usage;
    const VkPipelineStageFlags m_stages;
    const VkAccessFlags m_access;

    Rc<DxvkDevice>  m_device;
    Rc<DxvkBuffer>  m_buffer;
    VkDeviceSize    m_offset = 0;
    VkDeviceSize    m_bufferRequiredAlignmentOverride = 1;
    
    const char*     m_name = nullptr;

    std::queue<Rc<DxvkBuffer>> m_buffers;

    Rc<DxvkBuffer> createBuffer(VkDeviceSize size);
  };
}
