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
#pragma once

#include "dxvk_include.h"
#include "dxvk_context.h"
#include "dxvk_pipemanager.h"

#include "../util/util_matrix.h"
#include "rtx/pass/gen_tri_list_index_buffer_indices.h"
#include "rtx_types.h"

struct SkinningArgs;

namespace dxvk {

  class DxvkDevice;
  class DxvkContext;

  /**
  * \brief Geometry utility shaders and related objects
  *
  * Creates the shaders, pipeline layouts, and
  * compute pipelines that are going to be used
  * for geometry operations.
  */
  class RtxGeometryUtils {
    std::unique_ptr<DxvkStagingDataAlloc> m_pCbData;

  public:
    RtxGeometryUtils(DxvkDevice* pDevice);
    ~RtxGeometryUtils();

    /**
     * \brief Currently we only support these texcoord formats...
     */
    static bool isTexcoordFormatValid(VkFormat format) {
      return format == VK_FORMAT_R32G32B32A32_SFLOAT || format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R32G32_SFLOAT;
    }

    /**
     * \brief Execute a compute shader to perform skinning
     */
    void dispatchSkinning(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      const DrawCallState& drawCallState, const RaytraceGeometry& geo) const;

    /**
     * \brief Execute a compute shader to perform view model perspective correction
     */
    void dispatchViewModelCorrection(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      const RaytraceGeometry& geo,
      const Matrix4& positionTransform) const;

    struct BakeOpacityMicromapDesc {
      uint8_t subdivisionLevel;
      uint32_t numMicroTrianglesPerTriangle;
      VkOpacityMicromapFormatEXT ommFormat;
      uint32_t surfaceIndex;
      RtSurfaceMaterialType materialType;
      bool applyVertexAndTextureOperations;
      bool useConservativeEstimation;
      uint32_t conservativeEstimationMaxTexelTapsPerMicroTriangle;
      uint32_t maxNumMicroTrianglesToBake;
      uint32_t numTriangles;
      uint32_t triangleOffset;
      float resolveTransparencyThreshold;  // Anything smaller or equal is transparent
      float resolveOpaquenessThreshold;    // Anything greater or equal is opaque
    };

    struct BakeOpacityMicromapState {
      bool initialized = false;
      uint32_t numTriangles;
      uint32_t numMicroTrianglesToBake;
      uint32_t numMicroTrianglesBaked;
      uint32_t numMicroTrianglesBakedInLastBake;
    };

    /**
     * \brief Execute a compute shader to bake opacity micromap for the input geometry
     *        Note: bakeState.numMicroTrianglesBakedInLastBake can be greater than desc.maxNumMicroTrianglesToBake due 
     *              internal alignments
     */
    void dispatchBakeOpacityMicromap(
      Rc<DxvkDevice> device,
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> ctx,
      const RaytraceGeometry& geo, 
      const TextureRef& opacityTexture,
      const TextureRef* secondaryOpacityTexture,
      const BakeOpacityMicromapDesc& desc,
      BakeOpacityMicromapState& bakeState,
      Rc<DxvkBuffer> opacityMicromapBuffer) const;

    struct InterleavedGeometryDescriptor {
      Rc<DxvkBuffer> buffer = nullptr;
      uint32_t stride = 0;
      uint32_t positionOffset = 0;
      bool hasNormals = false;
      uint32_t normalOffset = 0;
      bool hasTexcoord = false;
      uint32_t texcoordOffset = 0;
      bool hasColor0 = false;
      uint32_t color0Offset = 0;
    };

    // Helpers for promoting Geometry Snapshots from raster pipeline to Geometry Data for RT pipeline
    // Index related:
    static uint32_t getOptimalTriangleListSize(const RasterGeometry& input);
    static VkIndexType getOptimalIndexFormat(uint32_t vertexCount);
    static bool cacheIndexDataOnGPU(const Rc<DxvkContext>& ctx, const RasterGeometry& input, RaytraceGeometry& output);
    static bool generateTriangleList(const Rc<DxvkContext>& ctx, const RasterGeometry& input, Rc<DxvkBuffer> output);
    // Vertex related:
    static void processGeometryBuffers(const InterleavedGeometryDescriptor& desc, RaytraceGeometry& output);
    static void processGeometryBuffers(const RasterGeometry& input, RaytraceGeometry& output);
    static size_t computeOptimalVertexStride(const RasterGeometry& input);
    static void cacheVertexDataOnGPU(const Rc<DxvkContext>& ctx, const RasterGeometry& input, RaytraceGeometry& output);

    /**
     * \brief Execute a compute shader to generate a triangle list from arbitrary topologies
     */
    void dispatchGenTriList(const Rc<DxvkContext>& ctx, const GenTriListArgs& args, const DxvkBufferSlice& dst, const RasterBuffer* srcBuffer) const;
    
    /**
      * \brief Execute a compute shader to interleave vertex data into a single buffer
      */
    void interleaveGeometry(
      const Rc<DxvkContext>& ctx,
      const RasterGeometry& input,
      InterleavedGeometryDescriptor& output) const;
  };

  // A helper class to do small in-band buffer updates. Should be used instead of
  // DxvkContext::updateBuffer() when update size is guaranteed to be smaller than 64KB.
  //
  // Note: DxvkContext::updateBuffer() should be avoided because under certain conditions
  // it may replace the buffer and may also use the "init" command buffer and so may fail to
  // sync properly. updateBuffer() also uses a staging copy in case if update is larger
  // than 4096 bytes.
  inline constexpr size_t kMaxInbandUpdateSize = 64 * 1024;
  template<typename T, size_t MaxUpdateCount = kMaxInbandUpdateSize / sizeof(T)>
  class RtxInbandBufferUpdate {
    static_assert(MaxUpdateCount * sizeof(T) < kMaxInbandUpdateSize,
                  "Vulkan cannot update more than 64KB in-band!");

    const DxvkBufferSlice& m_bufferSlice;

    T m_data[MaxUpdateCount];
    size_t m_updateSize;

  public:
    RtxInbandBufferUpdate(const DxvkBufferSlice& bufferSlice, size_t updateCount)
    : m_bufferSlice(bufferSlice)
    // Note: dxvk buffers guaranteed to be at least 4-bytes aligned so
    // we do not have to check for that and may enforce 4-byte update size
    // alignment.
    , m_updateSize(align(updateCount * sizeof(T), 4)) {
#ifdef REMIX_DEVELOPMENT
      if ((bufferSlice.offset() & 3) != 0) {
        throw DxvkError("In-band update offset must be a multiple of 4.");
      }
      if (updateCount * sizeof(T) > m_bufferSlice.length()) {
        throw DxvkError("Refusing to update a buffer past slice bounds.");
      }
#endif
    }

    void commit(const Rc<DxvkContext>& ctx) {
      auto cmd = ctx->getCommandList();
      auto sliceHandle = m_bufferSlice.getSliceHandle();

      cmd->cmdUpdateBuffer(
        DxvkCmdBuffer::ExecBuffer,
        sliceHandle.handle,
        sliceHandle.offset,
        m_updateSize,
        m_data);

      cmd->trackResource<DxvkAccess::Write>(m_bufferSlice.buffer());

      ctx->emitMemoryBarrier(0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        m_bufferSlice.buffer()->info().stages,
        m_bufferSlice.buffer()->info().access);
    }

    T* data() {
      return m_data + 0;
    }
  };
}
