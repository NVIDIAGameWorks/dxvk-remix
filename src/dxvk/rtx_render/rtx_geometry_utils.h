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
}
