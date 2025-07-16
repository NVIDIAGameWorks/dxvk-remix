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
#include "rtx/pass/terrain_baking/decode_and_add_opacity_binding_indices.h"
#include "rtx_types.h"
#include "rtx_common_object.h"
#include "rtx_staging.h"

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
  class RtxGeometryUtils : public CommonDeviceObject {
    std::unique_ptr<RtxStagingDataAlloc> m_pCbData;
    Rc<DxvkContext> m_skinningContext;
    uint32_t m_skinningCommands = 0;

  public:
    explicit RtxGeometryUtils(DxvkDevice* pDevice);
    ~RtxGeometryUtils();

    /**
     * \brief Called before destruction
     */
    void onDestroy();

    /**
     * \brief Currently we only support these texcoord formats...
     */
    static bool isTexcoordFormatValid(VkFormat format) {
      return format == VK_FORMAT_R32G32B32A32_SFLOAT || format == VK_FORMAT_R32G32B32_SFLOAT || format == VK_FORMAT_R32G32_SFLOAT;
    }

    /**
     * \brief Execute a compute shader to perform skinning
     */
    void dispatchSkinning(const DrawCallState& drawCallState, const RaytraceGeometry& geo);

    /**
     * \brief Execute a compute shader to perform view model perspective correction
     */
    void dispatchViewModelCorrection(
      Rc<DxvkContext> ctx,
      const RaytraceGeometry& geo,
      const Matrix4& positionTransform) const;

    struct BakeOpacityMicromapDesc {
      uint8_t subdivisionLevel;
      uint32_t numMicroTrianglesPerTriangle;
      VkOpacityMicromapFormatEXT ommFormat;
      uint32_t surfaceIndex;
      MaterialDataType materialType;
      bool applyVertexAndTextureOperations;
      bool useConservativeEstimation;
      uint32_t conservativeEstimationMaxTexelTapsPerMicroTriangle;
      uint32_t numTriangles;
      uint32_t triangleOffset;
      float resolveTransparencyThreshold;  // Anything smaller or equal is transparent
      float resolveOpaquenessThreshold;    // Anything greater or equal is opaque
      
      float costPerTexelTapPerMicroTriangleBudget;
      const std::vector<uint16_t>& numTexelsPerMicrotriangle;

      BakeOpacityMicromapDesc(const std::vector<uint16_t>& _numTexelsPerMicrotriangle)
        : numTexelsPerMicrotriangle(_numTexelsPerMicrotriangle) { }
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
     */
    void dispatchBakeOpacityMicromap(
      Rc<DxvkContext> ctx,
      const RtInstance& instance,
      const RaytraceGeometry& geo,
      const std::vector<TextureRef>& textures,
      const std::vector<Rc<DxvkSampler>>& samplers,
      const uint32_t albedoOpacityTextureIndex,
      const uint32_t samplerIndex,
      const uint32_t secondaryAlbedoOpacityTextureIndex,
      const uint32_t secondarySamplerIndex,
      const BakeOpacityMicromapDesc& desc,
      BakeOpacityMicromapState& bakeState,
      uint32_t& availableBakingBudget,
      Rc<DxvkBuffer> opacityMicromapBuffer) const;

    struct TextureConversionInfo {
      ReplacementMaterialTextureType::Enum type = ReplacementMaterialTextureType::Count;
      const TextureRef* sourceTexture = nullptr;
      Rc<DxvkImageView> sourceView = nullptr;
      TextureRef targetTexture;
      float scale = 1.f;
      float offset = 0.f;
    };

    void decodeAndAddOpacity(
      Rc<DxvkContext> ctx,
      const TextureRef& colorOpacityTexture,
      const std::vector<TextureConversionInfo>& conversionInfos);

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
    
    // Calculate the maximum UV tile size (i.e. minimum UV density) of a draw call.
    static float computeMaxUVTileSize(const RasterGeometry& input, const Matrix4& objectToWorld);

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

    inline void flushCommandList() {
      if (m_skinningContext->getCommandList() != nullptr && m_skinningCommands > 0) {
        m_skinningContext->flushCommandList();
      }
    }

  private:
    static uint32_t calculateNumMicroTrianglesToBake(const BakeOpacityMicromapState& bakeState, const BakeOpacityMicromapDesc& desc, const uint32_t allowedNumMicroTriangleAlignment, const float bakingWeightScale, uint32_t& availableBakingBudget);
  };
}
