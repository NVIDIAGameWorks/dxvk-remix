#include <cstring>

#include "d3d9_initializer.h"

namespace dxvk {

  D3D9Initializer::D3D9Initializer(
    const Rc<DxvkDevice>&             Device)
  : m_device(Device), m_context(m_device->createContext()) {
    m_context->beginRecording(
      m_device->createCommandList());
  }

  
  D3D9Initializer::~D3D9Initializer() {

  }


  void D3D9Initializer::Flush() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    if (m_transferCommands != 0)
      FlushInternal();
  }


  void D3D9Initializer::InitBuffer(
          D3D9CommonBuffer*  pBuffer) {
    VkMemoryPropertyFlags memFlags = pBuffer->GetBuffer<D3D9_COMMON_BUFFER_TYPE_REAL>()->memFlags();

    (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      ? InitHostVisibleBuffer(pBuffer->GetBufferSlice<D3D9_COMMON_BUFFER_TYPE_REAL>())
      : InitDeviceLocalBuffer(pBuffer->GetBufferSlice<D3D9_COMMON_BUFFER_TYPE_REAL>());

    if (pBuffer->GetMapMode() == D3D9_COMMON_BUFFER_MAP_MODE_BUFFER)
      InitHostVisibleBuffer(pBuffer->GetBufferSlice<D3D9_COMMON_BUFFER_TYPE_STAGING>());
  }
  

  void D3D9Initializer::InitTexture(
          D3D9CommonTexture* pTexture,
          void*              pInitialData) {    
    if (pTexture->GetMapMode() == D3D9_COMMON_TEXTURE_MAP_MODE_NONE)
      return;

    (pTexture->GetMapMode() == D3D9_COMMON_TEXTURE_MAP_MODE_BACKED)
      ? InitDeviceLocalTexture(pTexture)
      : InitHostVisibleTexture(pTexture, pInitialData);
  }


  void D3D9Initializer::InitDeviceLocalBuffer(
          DxvkBufferSlice    Slice) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    m_transferCommands += 1;

    m_context->clearBuffer(
      Slice.buffer(),
      Slice.offset(),
      // NV-DXVK start: Mitigation to fix validation errors
      // Hack: Use alignDown here as the clear length must be divisible by 4 but also less than the buffer size. A typical
      // align operation will align upwards which will make this length longer than the buffer's length, so alignDown is
      // used instead. This does have the effect of leaving up to 3 bytes of the end of the buffer non-zeroed, but given
      // D3D9 buffers are supposed to be initialized to undefined this is probably fine for the vast majority of games (only
      // games that incorrectly expect the buffer to be cleared and are actually touching these last few bytes will be affected,
      // which in practice shouldn't cause any problems).
      // Do note this hack can be removed once updating to a newer DXVK, as this fix has been integrated as part of this GitHub
      // issue: https://github.com/doitsujin/dxvk/issues/4641
      alignDown(Slice.length(), sizeof(uint32_t)),
      // NV-DXVK end
      0u);

    FlushImplicit();
  }


  void D3D9Initializer::InitHostVisibleBuffer(
          DxvkBufferSlice    Slice) {
    // If the buffer is mapped, we can write data directly
    // to the mapped memory region instead of doing it on
    // the GPU. Same goes for zero-initialization.
    std::memset(
      Slice.mapPtr(0), 0,
      Slice.length());
  }


  void D3D9Initializer::InitDeviceLocalTexture(
          D3D9CommonTexture* pTexture) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    auto InitImage = [&](Rc<DxvkImage> image) {
      if (image == nullptr)
        return;

      auto formatInfo = imageFormatInfo(image->info().format);

      m_transferCommands += 1;
      
      // While the Microsoft docs state that resource contents are
      // undefined if no initial data is provided, some applications
      // expect a resource to be pre-cleared. We can only do that
      // for non-compressed images, but that should be fine.
      VkImageSubresourceRange subresources;
      subresources.aspectMask     = formatInfo->aspectMask;
      subresources.baseMipLevel   = 0;
      subresources.levelCount     = image->info().mipLevels;
      subresources.baseArrayLayer = 0;
      subresources.layerCount     = image->info().numLayers;

      if (formatInfo->flags.test(DxvkFormatFlag::BlockCompressed)) {
        m_context->clearCompressedColorImage(image, subresources);
      } else {
        if (subresources.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
          VkClearColorValue value = { };

          m_context->clearColorImage(
            image, value, subresources);
        } else {
          VkClearDepthStencilValue value;
          value.depth   = 0.0f;
          value.stencil = 0;
          
          m_context->clearDepthStencilImage(
            image, value, subresources);
        }
      }
    };

    InitImage(pTexture->GetImage());

    FlushImplicit();
  }


  void D3D9Initializer::InitHostVisibleTexture(
          D3D9CommonTexture* pTexture,
          void*              pInitialData) {
    // If the buffer is mapped, we can write data directly
    // to the mapped memory region instead of doing it on
    // the GPU. Same goes for zero-initialization.
    const D3D9_COMMON_TEXTURE_DESC* desc = pTexture->Desc();
    for (uint32_t a = 0; a < desc->ArraySize; a++) {
      for (uint32_t m = 0; m < desc->MipLevels; m++) {
        uint32_t subresource = pTexture->CalcSubresource(a, m);
        DxvkBufferSliceHandle mapSlice  = pTexture->GetBuffer(subresource)->getSliceHandle();

        if (pInitialData != nullptr) {
          VkExtent3D mipExtent = pTexture->GetExtentMip(m);
          const DxvkFormatInfo* formatInfo = imageFormatInfo(pTexture->GetFormatMapping().FormatColor);
          VkExtent3D blockCount = util::computeBlockCount(mipExtent, formatInfo->blockSize);
          uint32_t pitch = blockCount.width * formatInfo->elementSize;
          uint32_t alignedPitch = align(pitch, 4);

          util::packImageData(
            mapSlice.mapPtr,
            pInitialData,
            pitch,
            pitch * blockCount.height,
            alignedPitch,
            alignedPitch * blockCount.height,
            D3D9CommonTexture::GetImageTypeFromResourceType(pTexture->GetType()),
            mipExtent,
            pTexture->Desc()->ArraySize,
            formatInfo,
            VK_IMAGE_ASPECT_COLOR_BIT);
        } else {
          std::memset(
            mapSlice.mapPtr, 0,
            mapSlice.length);
        }
      }
    }
  }


  void D3D9Initializer::FlushImplicit() {
    if (m_transferCommands > MaxTransferCommands
     || m_transferMemory   > MaxTransferMemory)
      FlushInternal();
  }


  void D3D9Initializer::FlushInternal() {
    m_context->flushCommandList();
    
    m_transferCommands = 0;
    m_transferMemory   = 0;
  }

}