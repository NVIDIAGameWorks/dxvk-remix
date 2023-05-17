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
   */
  class DxvkStagingDataAlloc {
    constexpr static VkDeviceSize MaxBufferSize  = 1 << 25; // 32 MiB
    constexpr static uint32_t     MaxBufferCount = 2;
  public:

    DxvkStagingDataAlloc(const Rc<DxvkDevice>& device,
                         const VkMemoryPropertyFlagBits memFlags = (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                         const VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         const VkAccessFlags access = VK_ACCESS_TRANSFER_READ_BIT,
    // NV-DXVK start: Add alignment override functionality.
                         const VkDeviceSize bufferRequiredAlignmentOverride = 1);
    // NV-DXVK end

    ~DxvkStagingDataAlloc();

    /**
     * \brief Alloctaes a staging buffer slice
     * 
     * \param [in] align Alignment of the allocation
     * \param [in] size Size of the allocation
     * \returns Staging buffer slice
     */
    // NV-DXVK start: Add important usage note.
    // Note: The alignment passed to this function is only used to align the allocation within the DXVK staging data buffer itself.
    // This means that the buffer's base address plus the offset returned in this slice may not be aligned to the desired alignment.
    // To mitigate this, ensure the buffer's memory requirements are modified before being allocated to have the maximum alignment the
    // staging data is expected to require. Usually the memory requirements will already hold the required alignment for the allocation,
    // but this is not always the case when alignment requirements come from how the buffer is actually used rather than its usage flags.
    // NV-DXVK end
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
    // NV-DXVK start: Add alignment override functionality.
    VkDeviceSize    m_bufferRequiredAlignmentOverride = 1;
    // NV-DXVK end

    std::queue<Rc<DxvkBuffer>> m_buffers;

    Rc<DxvkBuffer> createBuffer(VkDeviceSize size);
  };
  
}
