/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include <algorithm>

#include "dxvk_device.h"
#include "dxvk_memory.h"

namespace dxvk {

DxvkMemoryStats& DxvkMemoryStats::operator=(const DxvkMemoryStats& other)
{
  memoryAllocated = other.memoryAllocated.load();
  memoryUsed = other.memoryUsed.load();
  applicationBuffers = other.applicationBuffers.load();
  applicationTextures = other.applicationTextures.load();
  rtxBuffers = other.rtxBuffers.load();
  rtxAccelerationStructures = other.rtxAccelerationStructures.load();
  rtxOpacityMicromaps = other.rtxOpacityMicromaps.load();
  rtxMaterialTextures = other.rtxMaterialTextures.load();
  rtxRenderTargets = other.rtxRenderTargets.load();

  return *this;
}

void DxvkMemoryStats::trackMemoryAssigned(Category category, VkDeviceSize size)
{
  switch (category) {
  case Category::AppBuffer:
    applicationBuffers += size;
    break;
  case Category::AppTexture:
    applicationTextures += size;
    break;
  case Category::RTXBuffer:
    rtxBuffers += size;
    break;
  case Category::RTXAccelerationStructure:
    rtxAccelerationStructures += size;
    break;
  case Category::RTXOpacityMicromap:
    rtxOpacityMicromaps += size;
    break;
  case Category::RTXMaterialTexture:
    rtxMaterialTextures += size;
    break;
  case Category::RTXRenderTarget:
    rtxRenderTargets += size;
    break;
        
  default:
    assert(!"unimplemented");
    break;
  }

  memoryUsed += size;
}

void DxvkMemoryStats::trackMemoryReleased(Category category, VkDeviceSize size)
{
  switch (category) {
  case Category::AppBuffer:
    applicationBuffers -= size;
    break;
  case Category::AppTexture:
    applicationTextures -= size;
    break;
  case Category::RTXBuffer:
    rtxBuffers -= size;
    break;
  case Category::RTXAccelerationStructure:
    rtxAccelerationStructures -= size;
    break;
  case Category::RTXOpacityMicromap:
    rtxOpacityMicromaps -= size;
    break;
  case Category::RTXMaterialTexture:
    rtxMaterialTextures -= size;
    break;
  case Category::RTXRenderTarget:
    rtxRenderTargets -= size;
    break;

  default:
    assert(!"unimplemented");
    break;
  }

  memoryUsed -= size;
}

void DxvkMemoryStats::trackMemoryAllocated(VkDeviceSize size)
{
  memoryAllocated += size;
}

void DxvkMemoryStats::trackMemoryFreed(VkDeviceSize size)
{
  memoryAllocated -= size;
}

VkDeviceSize DxvkMemoryStats::totalAllocated() const
{
  return memoryAllocated;
}

VkDeviceSize DxvkMemoryStats::totalUsed() const
{
  return memoryUsed;
}

VkDeviceSize DxvkMemoryStats::usedByCategory(Category category) const
{
  switch (category) {
  case Category::AppBuffer:
    return applicationBuffers;
  case Category::AppTexture:
    return applicationTextures;
  case Category::RTXBuffer:
    return rtxBuffers;
  case Category::RTXAccelerationStructure:
    return rtxAccelerationStructures;
  case Category::RTXOpacityMicromap:
    return rtxOpacityMicromaps;
  case Category::RTXMaterialTexture:
    return rtxMaterialTextures;
  case Category::RTXRenderTarget:
    return rtxRenderTargets;
  default:
    assert(!"unimplemented");
    return 0;
  }
}

static const std::map<DxvkMemoryStats::Category, const char *> categoryStringMap = {
  { DxvkMemoryStats::Category::AppBuffer, "AppBuffer" },
  { DxvkMemoryStats::Category::AppTexture, "AppTexture" },
  { DxvkMemoryStats::Category::RTXBuffer, "RTXBuffer" },
  { DxvkMemoryStats::Category::RTXAccelerationStructure, "RTXAccelerationStructure" },
  { DxvkMemoryStats::Category::RTXOpacityMicromap, "RTXOpacityMicromap" },
  { DxvkMemoryStats::Category::RTXMaterialTexture, "RTXMaterialTexture" },
  { DxvkMemoryStats::Category::RTXRenderTarget, "RTXRenderTarget" },
};

const char* DxvkMemoryStats::categoryToString(Category category) {
  return categoryStringMap.at(category);
}

DxvkMemory::DxvkMemory() { }
  DxvkMemory::DxvkMemory(
          DxvkMemoryAllocator*  alloc,
          DxvkMemoryChunk*      chunk,
          DxvkMemoryType*       type,
          VkDeviceMemory        memory,
          VkDeviceSize          offset,
          VkDeviceSize          length,
          void*                 mapPtr,
          DxvkMemoryStats::Category category)
  : m_alloc   (alloc),
    m_chunk   (chunk),
    m_type    (type),
    m_memory  (memory),
    m_offset  (offset),
    m_length  (length),
    m_mapPtr  (mapPtr),
    m_category (category) { }
  
  
  DxvkMemory::DxvkMemory(DxvkMemory&& other)
  : m_alloc   (std::exchange(other.m_alloc,  nullptr)),
    m_chunk   (std::exchange(other.m_chunk,  nullptr)),
    m_type    (std::exchange(other.m_type,   nullptr)),
    m_memory  (std::exchange(other.m_memory, VkDeviceMemory(VK_NULL_HANDLE))),
    m_offset  (std::exchange(other.m_offset, 0)),
    m_length  (std::exchange(other.m_length, 0)),
    m_mapPtr  (std::exchange(other.m_mapPtr, nullptr)),
    m_category (std::exchange(other.m_category, DxvkMemoryStats::Category::Invalid)) { }
  
  
  DxvkMemory& DxvkMemory::operator = (DxvkMemory&& other) {
    this->free();
    m_alloc   = std::exchange(other.m_alloc,  nullptr);
    m_chunk   = std::exchange(other.m_chunk,  nullptr);
    m_type    = std::exchange(other.m_type,   nullptr);
    m_memory  = std::exchange(other.m_memory, VkDeviceMemory(VK_NULL_HANDLE));
    m_offset  = std::exchange(other.m_offset, 0);
    m_length  = std::exchange(other.m_length, 0);
    m_mapPtr  = std::exchange(other.m_mapPtr, nullptr);
    m_category = std::exchange(other.m_category, DxvkMemoryStats::Category::Invalid);
    return *this;
  }
  
  
  DxvkMemory::~DxvkMemory() {
    this->free();
  }
  
  
  void DxvkMemory::free() {
    if (m_alloc != nullptr)
      m_alloc->free(*this);
  }
  

  DxvkMemoryChunk::DxvkMemoryChunk(
          DxvkMemoryAllocator*  alloc,
          DxvkMemoryType*       type,
          DxvkDeviceMemory      memory,
          DxvkMemoryFlags       hints)
  : m_alloc(alloc), m_type(type), m_memory(memory), m_hints(hints) {
    // Mark the entire chunk as free
    m_freeList.push_back(FreeSlice { 0, memory.memSize });
  }
  
  
  DxvkMemoryChunk::~DxvkMemoryChunk() {
    // This call is technically not thread-safe, but it
    // doesn't need to be since we don't free chunks
    m_alloc->freeDeviceMemory(m_type, m_memory);
  }
  
  
  DxvkMemory DxvkMemoryChunk::alloc(
          VkMemoryPropertyFlags    flags,
          VkDeviceSize             size,
          VkDeviceSize             align,
          DxvkMemoryFlags          hints,
          DxvkMemoryStats::Category category) {
    // Property flags must be compatible. This could
    // be refined a bit in the future if necessary.
    if (m_memory.memFlags != flags || !checkHints(hints))
      return DxvkMemory();
    
    // If the chunk is full, return
    if (m_freeList.size() == 0)
      return DxvkMemory();
    
    // Select the slice to allocate from in a worst-fit
    // manner. This may help keep fragmentation low.
    auto bestSlice = m_freeList.begin();
    
    for (auto slice = m_freeList.begin(); slice != m_freeList.end(); slice++) {
      if (slice->length == size) {
        bestSlice = slice;
        break;
      } else if (slice->length > bestSlice->length) {
        bestSlice = slice;
      }
    }
    
    // We need to align the allocation to the requested alignment
    const VkDeviceSize sliceStart = bestSlice->offset;
    const VkDeviceSize sliceEnd   = bestSlice->offset + bestSlice->length;
    
    const VkDeviceSize allocStart = dxvk::align(sliceStart,        align);
    const VkDeviceSize allocEnd   = dxvk::align(allocStart + size, align);
    
    if (allocEnd > sliceEnd)
      return DxvkMemory();
    
    // We can use this slice, but we'll have to add
    // the unused parts of it back to the free list.
    m_freeList.erase(bestSlice);
    
    if (allocStart != sliceStart)
      m_freeList.push_back({ sliceStart, allocStart - sliceStart });
    
    if (allocEnd != sliceEnd)
      m_freeList.push_back({ allocEnd, sliceEnd - allocEnd });

    // NV-DXVK start:
    // Calculate the pointer to the mapped data, if any
    void* mapPtr = (m_memory.memPointer != nullptr) ? reinterpret_cast<char*>(m_memory.memPointer) + allocStart : nullptr;

    // Create the memory object with the aligned slice
    return DxvkMemory(m_alloc, this, m_type,
      m_memory.memHandle, allocStart, allocEnd - allocStart,
      mapPtr, category);
    // NV-DXVK end
  }
  
  
  void DxvkMemoryChunk::free(
          VkDeviceSize  offset,
          VkDeviceSize  length) {
    // Remove adjacent entries from the free list and then add
    // a new slice that covers all those entries. Without doing
    // so, the slice could not be reused for larger allocations.
    auto curr = m_freeList.begin();
    
    while (curr != m_freeList.end()) {
      if (curr->offset == offset + length) {
        length += curr->length;
        curr = m_freeList.erase(curr);
      } else if (curr->offset + curr->length == offset) {
        offset -= curr->length;
        length += curr->length;
        curr = m_freeList.erase(curr);
      } else {
        curr++;
      }
    }
    
    m_freeList.push_back({ offset, length });
  }
  
  
  bool DxvkMemoryChunk::isEmpty() const {
    return m_freeList.size() == 1
        && m_freeList[0].length == m_memory.memSize;
  }


  bool DxvkMemoryChunk::isCompatible(const Rc<DxvkMemoryChunk>& other) const {
    return other->m_memory.memFlags == m_memory.memFlags && other->m_hints == m_hints;
  }


  bool DxvkMemoryChunk::checkHints(DxvkMemoryFlags hints) const {
    DxvkMemoryFlags mask(
      DxvkMemoryFlag::Small,
      DxvkMemoryFlag::GpuReadable,
      DxvkMemoryFlag::GpuWritable);

    if (hints.test(DxvkMemoryFlag::IgnoreConstraints))
      mask = DxvkMemoryFlags();

    return (m_hints & mask) == (hints & mask);
  }


  DxvkMemoryAllocator::DxvkMemoryAllocator(const DxvkDevice* device)
  : m_vkd             (device->vkd()),
    m_device          (device),
    m_devProps        (device->adapter()->deviceProperties()),
    m_memProps        (device->adapter()->memoryProperties()) {
    for (uint32_t i = 0; i < m_memProps.memoryHeapCount; i++) {
      m_memHeaps[i].properties = m_memProps.memoryHeaps[i];
      m_memHeaps[i].budget     = 0;

      /* Target 80% of a heap on systems where we want
       * to avoid oversubscribing memory heaps */
      if ((m_memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
       && (m_device->isUnifiedMemoryArchitecture()))
        m_memHeaps[i].budget = (8 * m_memProps.memoryHeaps[i].size) / 10;
    }
    
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++) {
      m_memTypes[i].heap       = &m_memHeaps[m_memProps.memoryTypes[i].heapIndex];
      m_memTypes[i].heapId     = m_memProps.memoryTypes[i].heapIndex;
      m_memTypes[i].memType    = m_memProps.memoryTypes[i];
      m_memTypes[i].memTypeId  = i;
    }

    /* Check what kind of heap the HVV memory type is on, if any. If the
     * HVV memory type is on the largest device-local heap, we either have
     * an UMA system or an RBAR-enabled system. Otherwise, there will likely
     * be a separate, smaller heap for it. */
    VkDeviceSize largestDeviceLocalHeap = 0;

    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++) {
      if (m_memTypes[i].memType.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
        largestDeviceLocalHeap = std::max(largestDeviceLocalHeap, m_memTypes[i].heap->properties.size);
    }

    /* Work around an issue on Nvidia drivers where using the entire
     * device_local | host_visible heap can cause crashes or slowdowns */
    if (m_device->properties().core.properties.vendorID == uint16_t(DxvkGpuVendor::Nvidia)) {
      bool shrinkNvidiaHvvHeap = device->adapter()->matchesDriver(DxvkGpuVendor::Nvidia,
        VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR, 0, VK_MAKE_VERSION(465, 0, 0));

      applyTristate(shrinkNvidiaHvvHeap, device->config().shrinkNvidiaHvvHeap);

      if (shrinkNvidiaHvvHeap) {
        for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++) {
          VkMemoryPropertyFlags hvvFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

          if ((m_memTypes[i].memType.propertyFlags & hvvFlags) == hvvFlags
           && (m_memTypes[i].heap->properties.size < largestDeviceLocalHeap))
            m_memTypes[i].heap->budget = 32 << 20;
        }
      }
    }
  }
  
  
  DxvkMemoryAllocator::~DxvkMemoryAllocator() {
    
  }
  
  
  DxvkMemory DxvkMemoryAllocator::alloc(
    const VkMemoryRequirements*             req,
    const VkMemoryDedicatedRequirements&    dedAllocReq,
    const VkMemoryDedicatedAllocateInfo&    dedAllocInfo,
          VkMemoryPropertyFlags             flags,
          DxvkMemoryFlags                   hints,
          DxvkMemoryStats::Category         category) {
    ScopedCpuProfileZone();

    // NV-DXVK start: Allocation mutex removal
    // Note: The mutex here in DXVK has been removed in favor of the per-memory type mutex in tryAllocFromType.
    // NV-DXVK end

    // Keep small allocations together to avoid fragmenting
    // chunks for larger resources with lots of small gaps,
    // as well as resources with potentially weird lifetimes
    if (req->size <= SmallAllocationThreshold) {
      hints.set(DxvkMemoryFlag::Small);
      hints.clr(DxvkMemoryFlag::GpuWritable, DxvkMemoryFlag::GpuReadable);
    }

    // Ignore all hints for host-visible allocations since they
    // usually don't make much sense for those resources
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      hints = DxvkMemoryFlags();

    // Try to allocate from a memory type which supports the given flags exactly
    auto dedAllocPtr = dedAllocReq.prefersDedicatedAllocation ? &dedAllocInfo : nullptr;
    DxvkMemory result = this->tryAlloc(req, dedAllocPtr, flags, hints, category);

    // If the first attempt failed, try ignoring the dedicated allocation
    if (!result && dedAllocPtr && !dedAllocReq.requiresDedicatedAllocation) {
      result = this->tryAlloc(req, nullptr, flags, hints, category);
      dedAllocPtr = nullptr;
    }

    // Retry without the hint constraints
    if (!result) {
      hints.set(DxvkMemoryFlag::IgnoreConstraints);
      result = this->tryAlloc(req, nullptr, flags, hints, category);
    }

    // If that still didn't work, probe slower memory types as well
    VkMemoryPropertyFlags optFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                   | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    VkMemoryPropertyFlags remFlags = 0;
    
    while (!result && (flags & optFlags)) {
      remFlags |= optFlags & (0 - optFlags); // Note: 0 - x is a more well defined version of -x for unsigned values
      optFlags &= ~remFlags;

      result = this->tryAlloc(req, dedAllocPtr, flags & ~remFlags, hints, category);
    }
    
    if (!result) {
      DxvkAdapterMemoryInfo memHeapInfo = m_device->adapter()->getMemoryHeapInfo();

      Logger::err(str::format(
        "DxvkMemoryAllocator: Memory allocation failed",
        "\n  Size:      ", req->size,
        "\n  Alignment: ", req->alignment,
        "\n  Mem property flags: ", "0x", std::hex, flags,
        "\n  Mem types: ", "0x", std::hex, req->memoryTypeBits));

      for (uint32_t i = 0; i < m_memProps.memoryHeapCount; i++) {
        Logger::err(str::format("Heap ", i, ": ",
          (m_memHeaps[i].stats.totalAllocated() >> 20), " MB allocated, ",
          (m_memHeaps[i].stats.totalUsed() >> 20), " MB used, ",
          m_device->extensions().extMemoryBudget
            ? str::format(
                (memHeapInfo.heaps[i].memoryAllocated >> 20), " MB allocated (driver), ",
                (memHeapInfo.heaps[i].memoryBudget    >> 20), " MB budget (driver), ",
                (m_memHeaps[i].properties.size        >> 20), " MB total")
            : str::format(
                (m_memHeaps[i].properties.size        >> 20), " MB total")));
      }

      throw DxvkError("DxvkMemoryAllocator: Memory allocation failed");
    }
    
    return result;
  }
  
  //// NV-DXVK start: Free unused memory
  void DxvkMemoryAllocator::freeUnusedChunks() {
    for (auto& heap : m_memHeaps) {
      freeEmptyChunks(&heap);
    }
  }
  //// NV-DXVK end

  DxvkMemory DxvkMemoryAllocator::tryAlloc(
    const VkMemoryRequirements*             req,
    const VkMemoryDedicatedAllocateInfo*    dedAllocInfo,
          VkMemoryPropertyFlags             flags,
          DxvkMemoryFlags                   hints,
          DxvkMemoryStats::Category         category) {
    DxvkMemory result;

    for (uint32_t i = 0; i < m_memProps.memoryTypeCount && !result; i++) {
      const bool supported = (req->memoryTypeBits & (1u << i)) != 0;
      const bool adequate  = (m_memTypes[i].memType.propertyFlags & flags) == flags;
      
      if (supported && adequate) {
        result = this->tryAllocFromType(&m_memTypes[i],
                                        flags, req->size, req->alignment, hints, dedAllocInfo, category);
      }
    }
    
    return result;
  }
  
  
  DxvkMemory DxvkMemoryAllocator::tryAllocFromType(
          DxvkMemoryType*                   type,
          VkMemoryPropertyFlags             flags,
          VkDeviceSize                      size,
          VkDeviceSize                      align,
          DxvkMemoryFlags                   hints,
    const VkMemoryDedicatedAllocateInfo*    dedAllocInfo,
          DxvkMemoryStats::Category         category
          ) {
    // NV-DXVK start: use a per-memory-type mutex
    std::lock_guard<dxvk::mutex> lock(type->mutex);
    // NV-DXVK end

    // Prevent unnecessary external host memory fragmentation
    bool isDeviceLocal = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
  
    VkDeviceSize chunkSize = pickChunkSize(type->memTypeId, hints);

    DxvkMemory memory;

    if (size >= chunkSize || dedAllocInfo) {
      if (this->shouldFreeEmptyChunks(type->heap, size)) {
        // NV-DXVK start: use a per-memory-type mutex
        type->mutex.unlock();
        this->freeEmptyChunks(type->heap);
        type->mutex.lock();
        // NV-DXVK end
      }

      DxvkDeviceMemory devMem = this->tryAllocDeviceMemory(
        type, flags, size, hints, dedAllocInfo, category);

      if (devMem.memHandle != VK_NULL_HANDLE)
        memory = DxvkMemory(this, nullptr, type, devMem.memHandle, 0, size, devMem.memPointer, category);
    } else {
      for (uint32_t i = 0; i < type->chunks.size() && !memory; i++)
        memory = type->chunks[i]->alloc(flags, size, align, hints, category);
      
      if (!memory) {
        DxvkDeviceMemory devMem;

        if (this->shouldFreeEmptyChunks(type->heap, chunkSize)) {
          // NV-DXVK start: use a per-memory-type mutex
          type->mutex.unlock();
          this->freeEmptyChunks(type->heap);
          type->mutex.lock();
          // NV-DXVK end
        }

        for (uint32_t i = 0; i < 6 && (chunkSize >> i) >= size && !devMem.memHandle; i++)
          devMem = tryAllocDeviceMemory(type, flags, chunkSize >> i, hints, nullptr, category);

        if (devMem.memHandle) {
          Rc<DxvkMemoryChunk> chunk = new DxvkMemoryChunk(this, type, devMem, hints);
          memory = chunk->alloc(flags, size, align, hints, category);

          type->chunks.push_back(std::move(chunk));
        }
      }
    }

    if (memory)
      type->heap->stats.trackMemoryAssigned(category, memory.m_length);

    return memory;
  }
  
  
  DxvkDeviceMemory DxvkMemoryAllocator::tryAllocDeviceMemory(
          DxvkMemoryType*                   type,
          VkMemoryPropertyFlags             flags,
          VkDeviceSize                      size,
          DxvkMemoryFlags                   hints,
    const VkMemoryDedicatedAllocateInfo*    dedAllocInfo,
          DxvkMemoryStats::Category         category) {
    ScopedCpuProfileZone();
    bool useMemoryPriority = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                          && (m_device->features().extMemoryPriority.memoryPriority);
    
    if (type->heap->budget && type->heap->stats.totalAllocated() + size > type->heap->budget)
      return DxvkDeviceMemory();

    float priority = 0.0f;

    if (hints.test(DxvkMemoryFlag::GpuReadable))
      priority = 0.5f;
    if (hints.test(DxvkMemoryFlag::GpuWritable))
      priority = 1.0f;

    DxvkDeviceMemory result;
    result.memSize  = size;
    result.memFlags = flags;
    result.priority = priority;

    VkMemoryAllocateFlagsInfo allocateFlagsInfo;
    allocateFlagsInfo.sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocateFlagsInfo.pNext      = dedAllocInfo;
    // NV-DXVK begin: use device address bit for allocations
    // dxvk-remix requires buffer device addresses on some allocations; setting this bit
    // is essentially free, so we set it unconditionally to avoid having to plumb it through
    allocateFlagsInfo.flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    // NV-DXVK end
    allocateFlagsInfo.deviceMask = 0;

    VkMemoryPriorityAllocateInfoEXT prio;
    prio.sType            = VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT;
    prio.pNext            = &allocateFlagsInfo;
    prio.priority         = priority;

    VkMemoryAllocateInfo info;
    info.sType            = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.pNext            = useMemoryPriority ? &prio : prio.pNext;
    info.allocationSize   = size;
    info.memoryTypeIndex  = type->memTypeId;

    if (m_vkd->vkAllocateMemory(m_vkd->device(), &info, nullptr, &result.memHandle) != VK_SUCCESS)
      return DxvkDeviceMemory();
    
    if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      VkResult status = m_vkd->vkMapMemory(m_vkd->device(), result.memHandle, 0, VK_WHOLE_SIZE, 0, &result.memPointer);

      if (status != VK_SUCCESS) {
        Logger::err(str::format("DxvkMemoryAllocator: Mapping memory failed with ", status));
        m_vkd->vkFreeMemory(m_vkd->device(), result.memHandle, nullptr);
        return DxvkDeviceMemory();
      }
    }

    type->heap->stats.trackMemoryAllocated(size);
    m_device->adapter()->notifyHeapMemoryAlloc(type->heapId, size);
    return result;
  }


  void DxvkMemoryAllocator::free(
    const DxvkMemory&           memory) {
    // NV-DXVK start: use a per-memory-type mutex
    std::lock_guard<dxvk::mutex> lock(memory.m_type->mutex);
    // NV-DXVK end
    memory.m_type->heap->stats.trackMemoryReleased(memory.m_category, memory.m_length);

    if (memory.m_chunk != nullptr) {
      this->freeChunkMemory(
        memory.m_type,
        memory.m_chunk,
        memory.m_offset,
        memory.m_length);
    } else {
      DxvkDeviceMemory devMem;
      devMem.memHandle  = memory.m_memory;
      devMem.memPointer = nullptr;
      devMem.memSize    = memory.m_length;
      this->freeDeviceMemory(memory.m_type, devMem);
    }
  }

  
  void DxvkMemoryAllocator::freeChunkMemory(
          DxvkMemoryType*       type,
          DxvkMemoryChunk*      chunk,
          VkDeviceSize          offset,
          VkDeviceSize          length) {
    chunk->free(offset, length);

    if (chunk->isEmpty()) {
      Rc<DxvkMemoryChunk> chunkRef = chunk;

      // Free the chunk if we have to, or at least put it at the end of
      // the list so that chunks that are already in use and cannot be
      // freed are prioritized for allocations to reduce memory pressure.
      type->chunks.erase(std::remove(type->chunks.begin(), type->chunks.end(), chunkRef));

      if (!this->shouldFreeChunk(type, chunkRef))
        type->chunks.push_back(std::move(chunkRef));
    }
  }
  

  void DxvkMemoryAllocator::freeDeviceMemory(
          DxvkMemoryType*       type,
          DxvkDeviceMemory      memory) {
    m_vkd->vkFreeMemory(m_vkd->device(), memory.memHandle, nullptr);
    type->heap->stats.trackMemoryFreed(memory.memSize);
    m_device->adapter()->notifyHeapMemoryFree(type->heapId, memory.memSize);
  }


  VkDeviceSize DxvkMemoryAllocator::pickChunkSize(uint32_t memTypeId, DxvkMemoryFlags hints) const {
    VkMemoryType type = m_memProps.memoryTypes[memTypeId];
    VkMemoryHeap heap = m_memProps.memoryHeaps[type.heapIndex];
    
    // NV-DXVK start: configurable memory allocation chunk sizes
    const DxvkOptions& options = m_device->instance()->options();
    const bool isDeviceLocal = (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    VkDeviceSize chunkSize = (isDeviceLocal ? options.deviceLocalMemoryChunkSizeMB : options.otherMemoryChunkSizeMB) << 20;
    // NV-DXVK end

    if (hints.test(DxvkMemoryFlag::Small))
      chunkSize = 16 << 20;

    // Try to waste a bit less system memory especially in
    // 32-bit applications due to address space constraints
    if (env::is32BitHostPlatform()) {
      if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        chunkSize = 16 << 20;
    }

    // Reduce the chunk size on small heaps so
    // we can at least fit in 15 allocations
    while (chunkSize * 15 > heap.size)
      chunkSize >>= 1;

    return chunkSize;
  }


  bool DxvkMemoryAllocator::shouldFreeChunk(
    const DxvkMemoryType*       type,
    const Rc<DxvkMemoryChunk>&  chunk) const {
    // Under memory pressure, we should start freeing everything.
    if (this->shouldFreeEmptyChunks(type->heap, 0))
      return true;

    // Even if we have enough memory to spare, only keep
    // one chunk of each type around to save memory.
    for (const auto& c : type->chunks) {
      if (c != chunk && c->isEmpty() && c->isCompatible(chunk))
        return true;
    }

    return false;
  }


  bool DxvkMemoryAllocator::shouldFreeEmptyChunks(
    const DxvkMemoryHeap*       heap,
          VkDeviceSize          allocationSize) const {
    VkDeviceSize budget = heap->budget;

    if (!budget)
      budget = (heap->properties.size * 4) / 5;

    return heap->stats.totalAllocated() + allocationSize > budget;
  }


  void DxvkMemoryAllocator::freeEmptyChunks(
    const DxvkMemoryHeap*       heap) {
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; i++) {
      DxvkMemoryType* type = &m_memTypes[i];
      
      if (type->heap != heap)
        continue;

      // NV-DXVK start: use a per-memory-type mutex
      std::lock_guard<dxvk::mutex> lock(type->mutex);
      // NV-DXVK end

      type->chunks.erase(
        std::remove_if(type->chunks.begin(), type->chunks.end(),
          [] (const Rc<DxvkMemoryChunk>& chunk) { return chunk->isEmpty(); }),
        type->chunks.end());
    }
  }

}
