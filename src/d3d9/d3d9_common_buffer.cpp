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
#include "d3d9_common_buffer.h"
#include "d3d9_device.h"
#include "d3d9_util.h"

#include "../dxvk/dxvk_scoped_annotation.h"


namespace dxvk {
  D3D9CommonBuffer::D3D9CommonBuffer(
          D3D9DeviceEx*      pDevice,
    const D3D9_BUFFER_DESC*  pDesc) 
    : m_parent ( pDevice ), m_desc ( *pDesc ) {
    m_buffer = CreateBuffer();
    if (GetMapMode() == D3D9_COMMON_BUFFER_MAP_MODE_BUFFER)
      m_stagingBuffer = CreateStagingBuffer();

    m_sliceHandle = GetMapBuffer()->getSliceHandle();

    if (m_desc.Pool != D3DPOOL_DEFAULT)
      m_dirtyRange = D3D9Range(0, m_desc.Size);
  }


  HRESULT D3D9CommonBuffer::Lock(
          UINT   OffsetToLock,
          UINT   SizeToLock,
          void** ppbData,
          DWORD  Flags) {

    return m_parent->LockBuffer(
      this,
      OffsetToLock,
      SizeToLock,
      ppbData,
      Flags);
  }
  
  HRESULT D3D9CommonBuffer::Unlock() {
    ScopedCpuProfileZone();

    return m_parent->UnlockBuffer(this);
  }


  HRESULT D3D9CommonBuffer::ValidateBufferProperties(const D3D9_BUFFER_DESC* pDesc) {
    if (pDesc->Size == 0)
      return D3DERR_INVALIDCALL;

    return D3D_OK;
  }


  void D3D9CommonBuffer::PreLoad() {
    if (IsPoolManaged(m_desc.Pool)) {
      auto lock = m_parent->LockDevice();

      if (NeedsUpload())
        m_parent->FlushBuffer(this);
    }
  }

  Rc<DxvkBuffer> D3D9CommonBuffer::CreateBuffer() const {
    DxvkBufferCreateInfo  info;
    info.size = m_desc.Size;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.access = VK_ACCESS_TRANSFER_READ_BIT;

    VkMemoryPropertyFlags memoryFlags = 0;

    if (m_desc.Type == D3DRTYPE_VERTEXBUFFER) {
      info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      info.stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
      info.access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
      memoryFlags |=  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      if (m_parent->SupportsSWVP()) {
        info.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.stages |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        info.access |= VK_ACCESS_SHADER_WRITE_BIT;
      }
    }
    else if (m_desc.Type == D3DRTYPE_INDEXBUFFER) {
      info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
      info.stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
      info.access |= VK_ACCESS_INDEX_READ_BIT;
      memoryFlags |=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    if (GetMapMode() == D3D9_COMMON_BUFFER_MAP_MODE_DIRECT) {
      info.stages |= VK_PIPELINE_STAGE_HOST_BIT;
      info.access |= VK_ACCESS_HOST_WRITE_BIT;

      if (!(m_desc.Usage & D3DUSAGE_WRITEONLY))
        info.access |= VK_ACCESS_HOST_READ_BIT;

      memoryFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_CACHED_BIT
        | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else {
      info.access |= VK_ACCESS_TRANSFER_WRITE_BIT;
      memoryFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    // NV-DXVK start: force app geometry data into host memory
    if (m_parent->GetOptions()->hostMemoryForGeometry) {
      memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    // NV-DXVK end

    if (memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT && m_parent->GetOptions()->apitraceMode) {
      memoryFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    return m_parent->GetDXVKDevice()->createBuffer(info, memoryFlags, DxvkMemoryStats::Category::AppBuffer);
  }


  Rc<DxvkBuffer> D3D9CommonBuffer::CreateStagingBuffer() const {
    DxvkBufferCreateInfo  info;
    info.size   = m_desc.Size;
    info.stages = VK_PIPELINE_STAGE_HOST_BIT
                | VK_PIPELINE_STAGE_TRANSFER_BIT;

    info.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    info.access = VK_ACCESS_HOST_WRITE_BIT
                | VK_ACCESS_TRANSFER_READ_BIT;

    if (!(m_desc.Usage & D3DUSAGE_WRITEONLY))
      info.access |= VK_ACCESS_HOST_READ_BIT;

    VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    if (m_parent->GetOptions()->apitraceMode)
      memoryFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    return m_parent->GetDXVKDevice()->createBuffer(info, memoryFlags, DxvkMemoryStats::Category::AppBuffer);
  }

}