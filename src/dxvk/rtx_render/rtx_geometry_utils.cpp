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

#include "rtx_geometry_utils.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include <rtx_shaders/gen_tri_list_index_buffer.h>
#include <rtx_shaders/gpu_skinning.h>
#include <rtx_shaders/view_model_correction.h>
#include <rtx_shaders/bake_opacity_micromap.h>
#include <rtx_shaders/decode_and_add_opacity.h>
#include <rtx_shaders/interleave_geometry.h>
#include "dxvk_scoped_annotation.h"

#include "rtx_context.h"
#include "rtx_options.h"

#include "rtx/pass/view_model/view_model_correction_binding_indices.h"
#include "rtx/pass/opacity_micromap/bake_opacity_micromap_binding_indices.h"
#include "rtx/pass/terrain_baking/decode_and_add_opacity_binding_indices.h"
#include "rtx/pass/gpu_skinning_binding_indices.h"
#include "rtx/pass/skinning.h"
#include "rtx/pass/gen_tri_list_index_buffer.h"
#include "rtx/pass/interleave_geometry_indices.h"
#include "rtx/pass/interleave_geometry.h"

namespace dxvk {
  static constexpr uint32_t kMaxInterleavedComponents = 3 + 3 + 2 + 1;

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class GenTriListIndicesShader : public ManagedShader {
      SHADER_SOURCE(GenTriListIndicesShader, VK_SHADER_STAGE_COMPUTE_BIT, gen_tri_list_index_buffer)

      PUSH_CONSTANTS(GenTriListArgs)

      BEGIN_PARAMETER()
        RW_STRUCTURED_BUFFER(GEN_TRILIST_BINDING_OUTPUT)
        STRUCTURED_BUFFER(GEN_TRILIST_BINDING_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(GenTriListIndicesShader);

    class SkinningShader : public ManagedShader {
      SHADER_SOURCE(SkinningShader, VK_SHADER_STAGE_COMPUTE_BIT, gpu_skinning)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(BINDING_SKINNING_CONSTANTS)
        RW_STRUCTURED_BUFFER(BINDING_POSITION_OUTPUT)
        STRUCTURED_BUFFER(BINDING_POSITION_INPUT)
        STRUCTURED_BUFFER(BINDING_BLEND_WEIGHT_INPUT)
        STRUCTURED_BUFFER(BINDING_BLEND_INDICES_INPUT)
        RW_STRUCTURED_BUFFER(BINDING_NORMAL_OUTPUT)
        STRUCTURED_BUFFER(BINDING_NORMAL_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(SkinningShader);

    class ViewModelCorrectionShader : public ManagedShader {
      SHADER_SOURCE(ViewModelCorrectionShader, VK_SHADER_STAGE_COMPUTE_BIT, view_model_correction)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(BINDING_VMC_CONSTANTS)
        RW_STRUCTURED_BUFFER(BINDING_VMC_POSITION_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(BINDING_VMC_NORMAL_INPUT_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(ViewModelCorrectionShader);

    class BakeOpacityMicromapShader : public ManagedShader {
      SHADER_SOURCE(BakeOpacityMicromapShader, VK_SHADER_STAGE_COMPUTE_BIT, bake_opacity_micromap)
      
      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        STRUCTURED_BUFFER(BINDING_BAKE_OPACITY_MICROMAP_TEXCOORD_INPUT) 
        SAMPLER2D(BINDING_BAKE_OPACITY_MICROMAP_OPACITY_INPUT)
        SAMPLER2D(BINDING_BAKE_OPACITY_MICROMAP_SECONDARY_OPACITY_INPUT)
        STRUCTURED_BUFFER(BINDING_BAKE_OPACITY_MICROMAP_BINDING_SURFACE_DATA_INPUT)
        CONSTANT_BUFFER(BINDING_BAKE_OPACITY_MICROMAP_CONSTANTS)
        RW_STRUCTURED_BUFFER(BINDING_BAKE_OPACITY_MICROMAP_ARRAY_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(BakeOpacityMicromapShader);

    class DecodeAndAddOpacityShader : public ManagedShader {
      SHADER_SOURCE(DecodeAndAddOpacityShader, VK_SHADER_STAGE_COMPUTE_BIT, decode_and_add_opacity)

      PUSH_CONSTANTS(DecodeAndAddOpacityArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_INPUT)
        TEXTURE2D(DECODE_AND_ADD_OPACITY_BINDING_ALBEDO_OPACITY_TEXTURE_INPUT)
        RW_TEXTURE2D(DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_OUTPUT)
        SAMPLER(DECODE_AND_ADD_OPACITY_BINDING_LINEAR_SAMPLER)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DecodeAndAddOpacityShader);

    class InterleaveGeometryShader : public ManagedShader {
      SHADER_SOURCE(InterleaveGeometryShader, VK_SHADER_STAGE_COMPUTE_BIT, interleave_geometry)

      PUSH_CONSTANTS(InterleaveGeometryArgs)

      BEGIN_PARAMETER()
      RW_STRUCTURED_BUFFER(INTERLEAVE_GEOMETRY_BINDING_OUTPUT)
      STRUCTURED_BUFFER(INTERLEAVE_GEOMETRY_BINDING_POSITION_INPUT)
      STRUCTURED_BUFFER(INTERLEAVE_GEOMETRY_BINDING_NORMAL_INPUT)
      STRUCTURED_BUFFER(INTERLEAVE_GEOMETRY_BINDING_TEXCOORD_INPUT)
      STRUCTURED_BUFFER(INTERLEAVE_GEOMETRY_BINDING_COLOR0_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(InterleaveGeometryShader);

    float calcUVTileSizeSqr(const Matrix4& objectToWorld, const uint8_t* pVertex, size_t vertexStride, const uint8_t* pTexcoord, size_t texcoordStride, uint32_t vertex1, uint32_t vertex2, uint32_t vertex3) {
      const Vector4 p1 = objectToWorld * Vector4(*reinterpret_cast<const Vector3* const>(pVertex + vertexStride * vertex1), 1.f);
      const Vector4 p2 = objectToWorld * Vector4(*reinterpret_cast<const Vector3* const>(pVertex + vertexStride * vertex2), 1.f);
      const Vector4 p3 = objectToWorld * Vector4(*reinterpret_cast<const Vector3* const>(pVertex + vertexStride * vertex3), 1.f);

      const Vector2& t1 = *reinterpret_cast<const Vector2* const>(pTexcoord + texcoordStride * vertex1);
      const Vector2& t2 = *reinterpret_cast<const Vector2* const>(pTexcoord + texcoordStride * vertex2);
      const Vector2& t3 = *reinterpret_cast<const Vector2* const>(pTexcoord + texcoordStride * vertex3);
      // UV tile size (squared)
      float len1Sqr = p1 != p2 ? lengthSqr(p1 - p2) / lengthSqr(t1 - t2) : 0.f;
      float len2Sqr = p1 != p3 ? lengthSqr(p1 - p3) / lengthSqr(t1 - t3) : 0.f;
      float len3Sqr = p2 != p3 ? lengthSqr(p2 - p3) / lengthSqr(t2 - t3) : 0.f;

      return std::max(len1Sqr, std::max(len2Sqr, len3Sqr));
    }


    float calcMaxUvTileSizeSqrIndexed(uint32_t indexCount, const Matrix4& objectToWorld, const uint8_t* pVertex, size_t vertexStride, const uint8_t* pTexcoord, size_t texcoordStride, const void* pIndexData, size_t indexStride) {
      float result = 0.f;
      if (indexStride == 2) {
        // 16 bit indices
        const uint16_t* pIndex = static_cast<const uint16_t*>(pIndexData);
        for (uint32_t i = 0; i < indexCount; i += 3) {
          uint32_t vertex1 = pIndex[i];
          uint32_t vertex2 = pIndex[i+1];
          uint32_t vertex3 = pIndex[i+2];

          result = std::max(result, calcUVTileSizeSqr(objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, vertex1, vertex2, vertex3));
        }
      } else if (indexStride == 4) {
        // 32 bit indices
        const uint32_t* pIndex = static_cast<const uint32_t*>(pIndexData);
        for (uint32_t i = 0; i + 2 < indexCount; i += 3) {
          uint32_t vertex1 = pIndex[i];
          uint32_t vertex2 = pIndex[i+1];
          uint32_t vertex3 = pIndex[i+2];

          result = std::max(result, calcUVTileSizeSqr(objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, vertex1, vertex2, vertex3));
        }
      } else {
        ONCE(Logger::err("calcMaxUvTileSizeSqrIndexed: invalid index stride"));
      }
      return result;
    }

    float calcMaxUvTileSizeSqrTriangles(uint32_t vertexCount, const Matrix4& objectToWorld, const uint8_t* pVertex, size_t vertexStride, const uint8_t* pTexcoord, size_t texcoordStride) {
      float result = 0.f;
      for (uint32_t i = 0; i < vertexCount; i += 3) {
        uint32_t vertex1 = i;
        uint32_t vertex2 = i+1;
        uint32_t vertex3 = i+2;

        result = std::max(result, calcUVTileSizeSqr(objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, vertex1, vertex2, vertex3));
      }
      return result;
    }

    float calcMaxUvTileSizeSqrTriangleStrip(uint32_t vertexCount, const Matrix4& objectToWorld, const uint8_t* pVertex, size_t vertexStride, const uint8_t* pTexcoord, size_t texcoordStride) {
      float result = 0.f;
      for (uint32_t i = 0; i + 2 < vertexCount; ++i) {
        uint32_t vertex1 = i;
        uint32_t vertex2 = i+1;
        uint32_t vertex3 = i+2;

        result = std::max(result, calcUVTileSizeSqr(objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, vertex1, vertex2, vertex3));
      }
      return result;
    }

    float calcMaxUvTileSizeSqrTriangleFan(uint32_t vertexCount, const Matrix4& objectToWorld, const uint8_t* pVertex, size_t vertexStride, const uint8_t* pTexcoord, size_t texcoordStride) {
      float result = 0.f;
      uint32_t vertex1 = 0;
      for (uint32_t i = 1; i + 1 < vertexCount; ++i) {
        uint32_t vertex2 = i+1;
        uint32_t vertex3 = i+2;

        result = std::max(result, calcUVTileSizeSqr(objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, vertex1, vertex2, vertex3));
      }
      return result;
    }
  }

  RtxGeometryUtils::RtxGeometryUtils(DxvkDevice* device) : CommonDeviceObject(device) {
    m_pCbData = std::make_unique<RtxStagingDataAlloc>(
      device,
      "RtxStagingDataAlloc: Geometry Utils CB",
      (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    m_skinningContext = device->createContext();
  }

  RtxGeometryUtils::~RtxGeometryUtils() { }

  void RtxGeometryUtils::onDestroy() {
    m_pCbData = nullptr;
    m_skinningContext = nullptr;
  }

  void RtxGeometryUtils::dispatchSkinning(const DrawCallState& drawCallState,
                                          const RaytraceGeometry& geo) {
    const Rc<DxvkContext>& ctx = m_skinningContext;
    // Create command list for the initial skinning dispatch (e.g. The first frame we get skinning mesh draw calls)
    if (ctx->getCommandList() == nullptr) {
      ctx->beginRecording(ctx->getDevice()->createCommandList());
    }

    ScopedGpuProfileZone(ctx, "performSkinning");

    const auto normalVertexFormat = drawCallState.getGeometryData().normalBuffer.vertexFormat();

    SkinningArgs params {};

    // Note: VK_FORMAT_R32_UINT assumed to be 32 bit spherical octahedral normals.
    assert(normalVertexFormat == VK_FORMAT_R32G32B32_SFLOAT || normalVertexFormat == VK_FORMAT_R32G32B32A32_SFLOAT || normalVertexFormat == VK_FORMAT_R32_UINT);
    assert(drawCallState.getGeometryData().blendWeightBuffer.defined());

    memcpy(&params.bones[0], &drawCallState.getSkinningState().pBoneMatrices[0], sizeof(Matrix4) * drawCallState.getSkinningState().numBones);

    params.dstPositionStride = geo.positionBuffer.stride();
    params.dstPositionOffset = geo.positionBuffer.offsetFromSlice();
    params.dstNormalStride = geo.normalBuffer.stride();
    params.dstNormalOffset = geo.normalBuffer.offsetFromSlice();
    
    params.srcPositionStride = drawCallState.getGeometryData().positionBuffer.stride();
    params.srcPositionOffset = drawCallState.getGeometryData().positionBuffer.offsetFromSlice();
    params.srcNormalStride = drawCallState.getGeometryData().normalBuffer.stride();
    params.srcNormalOffset = drawCallState.getGeometryData().normalBuffer.offsetFromSlice();

    params.blendWeightStride = drawCallState.getGeometryData().blendWeightBuffer.stride();
    params.blendWeightOffset = drawCallState.getGeometryData().blendWeightBuffer.offsetFromSlice();
    params.blendIndicesStride = drawCallState.getGeometryData().blendIndicesBuffer.stride();
    params.blendIndicesOffset = drawCallState.getGeometryData().blendIndicesBuffer.offsetFromSlice();

    params.numVertices = geo.vertexCount;
    params.useIndices = drawCallState.getGeometryData().blendIndicesBuffer.defined() ? 1 : 0;
    params.numBones = drawCallState.getGeometryData().numBonesPerVertex;
    params.useOctahedralNormals = normalVertexFormat == VK_FORMAT_R32_UINT ? 1 : 0;

    // If we don't have a mappable vertex buffer then we need to do this on the GPU
    bool mustUseGPU = drawCallState.getGeometryData().positionBuffer.mapPtr() == nullptr;

    // At some point, its more efficient to do these calculations on the GPU, this limit is somewhat arbitrary however, and might require better tuning...
    const uint32_t kNumVerticesToProcessOnCPU = 256;

    // Check we have appropriate CPU access
    const bool pendingGpuWrite = drawCallState.getGeometryData().positionBuffer.isPendingGpuWrite() ||
                                 drawCallState.getGeometryData().normalBuffer.isPendingGpuWrite() ||
                                 drawCallState.getGeometryData().blendWeightBuffer.isPendingGpuWrite() ||
                                 (drawCallState.getGeometryData().blendIndicesBuffer.defined() && drawCallState.getGeometryData().blendIndicesBuffer.isPendingGpuWrite());

    const bool useCPU = params.numVertices <= kNumVerticesToProcessOnCPU && !pendingGpuWrite && !mustUseGPU;

    if (!useCPU) {
      // Setting alignment to device limit minUniformBufferOffsetAlignment because the offset value should be its multiple.
      // See https://vulkan.lunarg.com/doc/view/1.2.189.2/windows/1.2-extensions/vkspec.html#VUID-VkWriteDescriptorSet-descriptorType-00327
      const auto& devInfo = ctx->getDevice()->properties().core.properties;
      VkDeviceSize alignment = devInfo.limits.minUniformBufferOffsetAlignment;

      DxvkBufferSlice cb = m_pCbData->alloc(alignment, sizeof(SkinningArgs));
      memcpy(cb.mapPtr(0), &params, sizeof(SkinningArgs));
      ctx->getCommandList()->trackResource<DxvkAccess::Write>(cb.buffer());

      ctx->bindResourceBuffer(BINDING_SKINNING_CONSTANTS, cb);
      ctx->bindResourceBuffer(BINDING_POSITION_OUTPUT, geo.positionBuffer);
      ctx->bindResourceBuffer(BINDING_POSITION_INPUT, drawCallState.getGeometryData().positionBuffer);
      ctx->bindResourceBuffer(BINDING_NORMAL_OUTPUT, geo.normalBuffer);
      ctx->bindResourceBuffer(BINDING_NORMAL_INPUT, drawCallState.getGeometryData().normalBuffer);
      ctx->bindResourceBuffer(BINDING_BLEND_WEIGHT_INPUT, drawCallState.getGeometryData().blendWeightBuffer);

      if (drawCallState.getGeometryData().blendIndicesBuffer.defined())
        ctx->bindResourceBuffer(BINDING_BLEND_INDICES_INPUT, drawCallState.getGeometryData().blendIndicesBuffer);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, SkinningShader::getShader());

      const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { params.numVertices, 1, 1 }, VkExtent3D { 128, 1, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(cb.buffer());
    } else {
      const float* srcPosition = reinterpret_cast<float*>(drawCallState.getGeometryData().positionBuffer.mapPtr(0));
      const float* srcNormal = reinterpret_cast<float*>(drawCallState.getGeometryData().normalBuffer.mapPtr(0));
      const float* srcBlendWeight = reinterpret_cast<float*>(drawCallState.getGeometryData().blendWeightBuffer.mapPtr(0));
      const uint8_t* srcBlendIndices = reinterpret_cast<uint8_t*>(drawCallState.getGeometryData().blendIndicesBuffer.mapPtr(0));

      // For CPU we are going to update a single entry at a time...
      params.dstPositionStride = 0;
      params.dstPositionOffset = 0;
      params.dstNormalStride = 0;
      params.dstNormalOffset = 0;

      float dstPosition[3];
      float dstNormal[3];

      for (uint32_t idx = 0; idx < params.numVertices; idx++) {
        skinning(idx, &dstPosition[0], &dstNormal[0], srcPosition, srcBlendWeight, srcBlendIndices, srcNormal, params);

        ctx->writeToBuffer(geo.positionBuffer.buffer(), geo.positionBuffer.offsetFromSlice() + idx * geo.positionBuffer.stride(), sizeof(dstPosition), &dstPosition[0]);
        ctx->writeToBuffer(geo.normalBuffer.buffer(), geo.normalBuffer.offsetFromSlice() + idx * geo.normalBuffer.stride(), sizeof(dstNormal), &dstNormal[0]);
      }
    }
    ++m_skinningCommands;
  }

  void RtxGeometryUtils::dispatchViewModelCorrection(
    Rc<DxvkContext> ctx,
    const RaytraceGeometry& geo,
    const Matrix4& positionTransform) const {

    // Fill out the arguments
    ViewModelCorrectionArgs args {};
    args.positionTransform = positionTransform;
    args.vectorTransform = transpose(inverse(positionTransform));
    args.positionStride = geo.positionBuffer.stride();
    args.positionOffset = geo.positionBuffer.offsetFromSlice();
    args.normalStride = geo.normalBuffer.defined() ? geo.normalBuffer.stride() : 0;
    args.normalOffset = geo.normalBuffer.defined() ? geo.normalBuffer.offsetFromSlice() : 0;
    args.numVertices = geo.vertexCount;

    // Upload the arguments into a buffer slice
    const auto& devInfo = ctx->getDevice()->properties().core.properties;
    VkDeviceSize alignment = devInfo.limits.minUniformBufferOffsetAlignment;

    DxvkBufferSlice cb = m_pCbData->alloc(alignment, sizeof(ViewModelCorrectionArgs));
    memcpy(cb.mapPtr(0), &args, sizeof(ViewModelCorrectionArgs));
    ctx->getCommandList()->trackResource<DxvkAccess::Write>(cb.buffer());

    // Bind other resources
    ctx->bindResourceBuffer(BINDING_VMC_CONSTANTS, cb);
    ctx->bindResourceBuffer(BINDING_VMC_POSITION_INPUT_OUTPUT, geo.positionBuffer);
    ctx->bindResourceBuffer(BINDING_VMC_NORMAL_INPUT_OUTPUT, geo.normalBuffer.defined() ? geo.normalBuffer : geo.positionBuffer);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ViewModelCorrectionShader::getShader());

    // Run the shader
    const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { args.numVertices, 1, 1 }, VkExtent3D { 128, 1, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Make sure the geom buffers are tracked for liveness
    ctx->getCommandList()->trackResource<DxvkAccess::Write>(geo.positionBuffer.buffer());
    if (geo.normalBuffer.defined())
      ctx->getCommandList()->trackResource<DxvkAccess::Write>(geo.normalBuffer.buffer());
  }

  // Calculates number of uTriangles to bake considering their triangle specific cost and an available budget.
  // Expects a bakeState with non-zero remaining micro triangles to be baked.
  // Returns values 1 or greater
  uint32_t RtxGeometryUtils::calculateNumMicroTrianglesToBake(
    const BakeOpacityMicromapState& bakeState,
    const BakeOpacityMicromapDesc& desc,
    // Alignment to which budget can be extended to if there are any remaining uTriangles to be baked in the last considered triangle
    const uint32_t allowedNumMicroTriangleAlignment,
    const float bakingWeightScale,
    // This budget is decreased by budget used up by the returned number of micro triangles to bake.
    // Expects a value 1 or greater
    uint32_t& availableBakingBudget) {

    uint32_t numMicroTrianglesToBake = 0;
    const uint32_t startTriangleIndex = bakeState.numMicroTrianglesBaked / desc.numMicroTrianglesPerTriangle;

    // Add uTriangles to bake from the remaining triangles in the geometry or until the baking budget limit is hit
    for (uint32_t triangleIndex = startTriangleIndex; triangleIndex < desc.numTriangles; triangleIndex++) {

      // Find number of uTriangles to bake for this triangle
      uint32_t numActiveMicroTriangles = desc.numMicroTrianglesPerTriangle;
      if (triangleIndex == startTriangleIndex && bakeState.numMicroTrianglesBaked > 0) {
        // Subtract previously baked uTriangles for this triangle
        numActiveMicroTriangles -= bakeState.numMicroTrianglesBaked 
                                 - startTriangleIndex * desc.numMicroTrianglesPerTriangle;
      }

      // Note: using floats below will result in some imprecisions, but the error should not 
      // make noticeable difference in the big picture and the floats are floor/ceil-ed such 
      // so as to not overshoot the budget

      // Calculate baking cost of a uTriangle for this triangle
      const float microTriangleCost = bakingWeightScale * 
        (1 +
         desc.numTexelsPerMicrotriangle[desc.triangleOffset + triangleIndex] * desc.costPerTexelTapPerMicroTriangleBudget);

      // Calculate baking cost of this triangle (i.e. including all of its remaining uTriangles that still need to be baked).
      // Note: take a ceil to overestimate rather than underestimate the cost
      const uint32_t weightedTriangleCost =
        static_cast<uint32_t>(
          std::min(
            ceil(numActiveMicroTriangles * microTriangleCost),
            static_cast<float>(UINT32_MAX)));

      // We have enough budget to bake uTriangles for (the rest of) the triangle
      if (weightedTriangleCost <= availableBakingBudget) {
        availableBakingBudget -= weightedTriangleCost;
        numMicroTrianglesToBake += numActiveMicroTriangles;
        continue;

      } else { // Not enough budget to bake all the uTriangles
        // Calculate how many uTriangles fit into the budget considering the alignment
        // Note: take a floor to underestimate number of uTriangles that fit
        const uint32_t maxNumMicroTrianglesWithinBakingBudgetAligned = 
          // Ensure aligning of values 1 or higher since 0 aligns with all values and thus would align to 0 which is undesired
          // as the current function's returned value is expected to be non 0
          align_safe(std::max(1u, static_cast<uint32_t>(floor(availableBakingBudget / microTriangleCost))), 
                     allowedNumMicroTriangleAlignment, 
                     UINT32_MAX);

        numMicroTrianglesToBake += std::min(numActiveMicroTriangles, maxNumMicroTrianglesWithinBakingBudgetAligned);

        // Simply nullify the budget, since it is too small for any other baking dispatch to be efficient
        availableBakingBudget = 0;

        break;
      }
    }

    return numMicroTrianglesToBake;
  }

  void RtxGeometryUtils::dispatchBakeOpacityMicromap(
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
    Rc<DxvkBuffer> opacityMicromapBuffer) const {

    // Init textures
    const TextureRef& opacityTexture = textures[albedoOpacityTextureIndex];
    const TextureRef* secondaryOpacityTexture = nullptr;
    if (secondaryAlbedoOpacityTextureIndex != kSurfaceMaterialInvalidTextureIndex)
      secondaryOpacityTexture = &textures[secondaryAlbedoOpacityTextureIndex];

    VkExtent3D opacityTextureResolution = opacityTexture.getImageView()->imageInfo().extent;

    // Fill out the arguments
    BakeOpacityMicromapArgs args {};
    size_t surfaceWriteOffset = 0;
    instance.surface.writeGPUData(&args.surface[0], surfaceWriteOffset);
    args.numTriangles = desc.numTriangles;
    args.numMicroTrianglesPerTriangle = desc.numMicroTrianglesPerTriangle;
    args.is2StateOMMFormat = desc.ommFormat == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    args.subdivisionLevel = desc.subdivisionLevel;
    args.texcoordOffset = geo.texcoordBuffer.offsetFromSlice();
    args.texcoordStride = geo.texcoordBuffer.stride();
    args.resolveTransparencyThreshold = desc.resolveTransparencyThreshold;
    args.resolveOpaquenessThreshold = desc.resolveOpaquenessThreshold;
    args.useConservativeEstimation = desc.useConservativeEstimation;
    args.isOpaqueMaterial = desc.materialType == MaterialDataType::Opaque;
    args.isRayPortalMaterial = desc.materialType == MaterialDataType::RayPortal;
    args.applyVertexAndTextureOperations = desc.applyVertexAndTextureOperations;
    args.numMicroTrianglesPerThread = args.is2StateOMMFormat ? 8 : 4;
    args.textureResolution = vec2 { static_cast<float>(opacityTextureResolution.width), static_cast<float>(opacityTextureResolution.height) };
    args.rcpTextureResolution = vec2 { 1.f / opacityTextureResolution.width, 1.f / opacityTextureResolution.height };
    args.conservativeEstimationMaxTexelTapsPerMicroTriangle = desc.conservativeEstimationMaxTexelTapsPerMicroTriangle;
    args.triangleOffset = desc.triangleOffset;

    // Init samplers
    Rc<DxvkSampler> opacitySampler;
    Rc<DxvkSampler> secondaryOpacitySampler;
    {
      const DxvkSamplerCreateInfo& samplerInfo = samplers[samplerIndex]->info();

      opacitySampler = device()->getCommon()->getResources().getSampler(
        VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        samplerInfo.addressModeU, samplerInfo.addressModeV, samplerInfo.addressModeW,
        samplerInfo.borderColor);

      if (secondaryOpacityTexture) {
        const DxvkSamplerCreateInfo& secondarySamplerInfo = samplers[secondarySamplerIndex]->info();
          secondaryOpacitySampler = device()->getCommon()->getResources().getSampler(
          VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
          secondarySamplerInfo.addressModeU, secondarySamplerInfo.addressModeV, secondarySamplerInfo.addressModeW,
          secondarySamplerInfo.borderColor);
      }
      else {
        secondaryOpacitySampler = opacitySampler;
      }
    }

    // Bind other resources
    ctx->bindResourceBuffer(BINDING_BAKE_OPACITY_MICROMAP_TEXCOORD_INPUT, geo.texcoordBuffer);
    ctx->bindResourceView(BINDING_BAKE_OPACITY_MICROMAP_OPACITY_INPUT, opacityTexture.getImageView(), nullptr);
    ctx->bindResourceSampler(BINDING_BAKE_OPACITY_MICROMAP_OPACITY_INPUT, opacitySampler);
    ctx->bindResourceView(BINDING_BAKE_OPACITY_MICROMAP_SECONDARY_OPACITY_INPUT,
                          secondaryOpacityTexture ? secondaryOpacityTexture->getImageView() : opacityTexture.getImageView(), nullptr);
    ctx->bindResourceSampler(BINDING_BAKE_OPACITY_MICROMAP_SECONDARY_OPACITY_INPUT, secondaryOpacitySampler);
    ctx->bindResourceBuffer(BINDING_BAKE_OPACITY_MICROMAP_BINDING_SURFACE_DATA_INPUT,
                            DxvkBufferSlice(device()->getCommon()->getSceneManager().getSurfaceBuffer()));
    ctx->bindResourceBuffer(BINDING_BAKE_OPACITY_MICROMAP_ARRAY_OUTPUT,
                            DxvkBufferSlice(opacityMicromapBuffer, 0, opacityMicromapBuffer->info().size));

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, BakeOpacityMicromapShader::getShader());

    if (!bakeState.initialized) {
      bakeState.numMicroTrianglesToBake = args.numTriangles * args.numMicroTrianglesPerTriangle;
      bakeState.numMicroTrianglesBaked = 0;
      bakeState.initialized = true;
    }

    const uint32_t numMicroTrianglesPerWord = args.is2StateOMMFormat ? 32 : 16;
    const uint32_t kNumMicroTrianglesPerComputeBlock = BAKE_OPACITY_MICROMAP_NUM_THREAD_PER_COMPUTE_BLOCK * args.numMicroTrianglesPerThread;
    const VkPhysicalDeviceLimits& limits = device()->properties().core.properties.limits;
    // Workgroup count limit can be high (i.e. 2 Billion), so avoid overflowing uint32_t limit 
    const uint32_t maxThreadsPerDispatch = std::min(limits.maxComputeWorkGroupCount[0], UINT32_MAX / kNumMicroTrianglesPerComputeBlock) *
                                           kNumMicroTrianglesPerComputeBlock;
    const uint32_t maxThreadsPerDispatchAligned = alignDown(maxThreadsPerDispatch, numMicroTrianglesPerWord); // Align down so as not to overshoot the limits

    // Baking cost increases with opacity texture resolution, so scale up the baking cost accordingly
    const float kResolutionWeight = 0.05f;  // Selected empirically
    const float minResolutionToScale = 128; // Selected empirically
    const float avgTextureResolution = 0.5f * (args.textureResolution.x + args.textureResolution.y);
    const float bakingWeightScale = 
      avgTextureResolution > minResolutionToScale 
      ? 1 + kResolutionWeight * avgTextureResolution / minResolutionToScale
      : 1;

    // Align number of microtriangles to bake up to how many are packed into a single word
    const uint32_t numMicroTrianglesAlignment = numMicroTrianglesPerWord;
    const uint32_t numMicroTrianglesToBake =
      calculateNumMicroTrianglesToBake(bakeState, desc, numMicroTrianglesAlignment, bakingWeightScale, availableBakingBudget);

    // Calculate per dispatch counts
    const uint32_t numThreads = numMicroTrianglesToBake / args.numMicroTrianglesPerThread;
    const uint32_t numThreadsPerDispatch = std::min(numThreads, maxThreadsPerDispatchAligned);
    const uint32_t numDispatches = dxvk::util::ceilDivide(numThreads, numThreadsPerDispatch);
    const uint32_t baseThreadIndexOffset = bakeState.numMicroTrianglesBaked / args.numMicroTrianglesPerThread;

    args.numActiveThreads = numThreadsPerDispatch;

    for (uint32_t i = 0; i < numDispatches; i++) {
      args.threadIndexOffset = i * numThreadsPerDispatch + baseThreadIndexOffset;

      // Upload the arguments into a buffer slice
      const auto& devInfo = ctx->getDevice()->properties().core.properties;
      DxvkBufferSlice cb = m_pCbData->alloc(devInfo.limits.minUniformBufferOffsetAlignment, sizeof(BakeOpacityMicromapArgs));
      memcpy(cb.mapPtr(0), &args, sizeof(BakeOpacityMicromapArgs));
      ctx->getCommandList()->trackResource<DxvkAccess::Write>(cb.buffer());

      // Bind other resources
      ctx->bindResourceBuffer(BINDING_BAKE_OPACITY_MICROMAP_CONSTANTS, cb);

      // Run the shader
      const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { numThreadsPerDispatch, 1, 1 }, VkExtent3D { BAKE_OPACITY_MICROMAP_NUM_THREAD_PER_COMPUTE_BLOCK, 1, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    bakeState.numMicroTrianglesBaked += numMicroTrianglesToBake;
    bakeState.numMicroTrianglesBakedInLastBake = numMicroTrianglesToBake;

    // Make sure the geom buffers are tracked for liveness
    ctx->getCommandList()->trackResource<DxvkAccess::Write>(opacityMicromapBuffer);
  }

  void RtxGeometryUtils::decodeAndAddOpacity(
      Rc<DxvkContext> ctx,
      const TextureRef& albedoOpacityTexture,
      const std::vector<TextureConversionInfo>& conversionInfos) {

    ScopedGpuProfileZone(ctx, "Decode And Add Opacity");

    Resources& resourceManager = ctx->getCommonObjects()->getResources();
    Rc<DxvkSampler> linearSampler = resourceManager.getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Bind resources
    ctx->bindResourceView(DECODE_AND_ADD_OPACITY_BINDING_ALBEDO_OPACITY_TEXTURE_INPUT, albedoOpacityTexture.getImageView(), nullptr);
    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DecodeAndAddOpacityShader::getShader());
    ctx->bindResourceSampler(DECODE_AND_ADD_OPACITY_BINDING_LINEAR_SAMPLER, linearSampler);
    
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    for (uint32_t i = 0; i < conversionInfos.size(); i++) {
      const TextureConversionInfo& conversionInfo = conversionInfos[i];

      // Bind resources
      ctx->bindResourceView(DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_INPUT, conversionInfo.sourceTexture->getImageView(), nullptr);
      ctx->bindResourceView(DECODE_AND_ADD_OPACITY_BINDING_TEXTURE_OUTPUT, conversionInfo.targetTexture.getImageView(), nullptr);

      // Fill out args
      DecodeAndAddOpacityArgs args {};
      args.textureType = conversionInfo.type;
      const VkExtent3D& extent = conversionInfo.targetTexture.getImageView()->imageInfo().extent;
      args.resolution = uint2(extent.width, extent.height);
      args.rcpResolution = float2(1.f / extent.width, 1.f / extent.height);
      args.normalIntensity = OpaqueMaterialOptions::normalIntensity();
      args.scale = conversionInfo.scale;
      args.offset = conversionInfo.offset;

      ctx->pushConstants(0, sizeof(args), &args);

      // Run the shader
      const VkExtent3D workgroups = util::computeBlockCount(extent, VkExtent3D { DECODE_AND_ADD_OPACITY_CS_DIMENSIONS });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }

  uint32_t RtxGeometryUtils::getOptimalTriangleListSize(const RasterGeometry& input) {
    const uint32_t primCount = (input.indexCount > 0) ? input.indexCount : input.vertexCount;
    assert(primCount > 0);
    switch (input.topology) {
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return primCount;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return (primCount - 2) * 3; // Conservative, assume no degenerates, no restart.  Actual returned in indexCountOut
    default:
      Logger::err("getTriangleListSize: unsupported topology");
      return 0;
    }
  }

  VkIndexType RtxGeometryUtils::getOptimalIndexFormat(uint32_t vertexCount) {
    assert(vertexCount > 0);
    return (vertexCount < 64 * 1024) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
  }

  bool RtxGeometryUtils::cacheIndexDataOnGPU(const Rc<DxvkContext>& ctx, const RasterGeometry& input, RaytraceGeometry& output) {
    ScopedCpuProfileZone();
    // Handle index buffer replacement - since the BVH builder does not support legacy primitive topology
    if (input.isTopologyRaytraceReady()) {
      ctx->copyBuffer(output.indexCacheBuffer, 0, input.indexBuffer.buffer(), input.indexBuffer.offset() + input.indexBuffer.offsetFromSlice(), input.indexCount * input.indexBuffer.stride());
    } else {
      return RtxGeometryUtils::generateTriangleList(ctx, input, output.indexCacheBuffer);
    }
    return true;
  }

  bool RtxGeometryUtils::generateTriangleList(const Rc<DxvkContext>& ctx, const RasterGeometry& input, Rc<DxvkBuffer> output) {
    ScopedCpuProfileZone();

    const uint32_t indexCount = getOptimalTriangleListSize(input);
    const uint32_t primIterCount = indexCount / 3;

    const VkIndexType indexBufferType = getOptimalIndexFormat(input.vertexCount);
    const uint32_t indexStride = (indexBufferType == VK_INDEX_TYPE_UINT16) ? 2 : 4;

    // TODO: Dont support 32-bit indices here yet
    if (indexBufferType != VK_INDEX_TYPE_UINT16 || (input.indexBuffer.defined() && input.indexBuffer.indexType() != VK_INDEX_TYPE_UINT16)) {
      ONCE(Logger::err("Not implemented yet, generating indices for a mesh which has 32-bit indices"));
      return false;
    }

    assert(output->info().size == align(indexCount * indexStride, CACHE_LINE_SIZE));

    // Prepare shader arguments
    GenTriListArgs pushArgs = { };
    pushArgs.firstIndex = 0;
    pushArgs.primCount = primIterCount;
    pushArgs.topology = (uint32_t) input.topology;
    pushArgs.useIndexBuffer = (input.indexBuffer.defined() && input.indexCount > 0) ? 1 : 0;
    pushArgs.minVertex = 0;
    pushArgs.maxVertex = input.vertexCount - 1;

    ctx->getCommonObjects()->metaGeometryUtils().dispatchGenTriList(ctx, pushArgs, DxvkBufferSlice(output), pushArgs.useIndexBuffer ? &input.indexBuffer : nullptr);

    if (indexCount % 3 != 0) {
      ONCE(Logger::err(str::format("Generating indices for a mesh which has non triangle topology: (indices%3) != 0, geometry hash = 0x", std::hex, input.getHashForRule(RtxOptions::geometryAssetHashRule()))));
      return false;
    }

    return true;
  }

  void RtxGeometryUtils::dispatchGenTriList(const Rc<DxvkContext>& ctx, const GenTriListArgs& cb, const DxvkBufferSlice& dstSlice, const RasterBuffer* srcBuffer) const {
    ScopedGpuProfileZone(ctx, "generateTriangleList");
    // At some point, its more efficient to do these calculations on the GPU, this limit is somewhat arbitrary however, and might require better tuning...
    const uint32_t kNumTrianglesToProcessOnCPU = 512;
    const bool useGPU = ((srcBuffer != nullptr) && (srcBuffer->isPendingGpuWrite())) || cb.primCount > kNumTrianglesToProcessOnCPU;

    if (useGPU) {
      ctx->bindResourceBuffer(GEN_TRILIST_BINDING_OUTPUT, dstSlice);

      if (srcBuffer != nullptr)
        ctx->bindResourceBuffer(GEN_TRILIST_BINDING_INPUT, *srcBuffer);

      ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

      ctx->pushConstants(0, sizeof(GenTriListArgs), &cb);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, GenTriListIndicesShader::getShader());

      const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { cb.primCount, 1, 1 }, VkExtent3D { 128, 1, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    } else {
      uint16_t dst[kNumTrianglesToProcessOnCPU * 3];

      const uint16_t* src = (cb.useIndexBuffer != 0) ? reinterpret_cast<uint16_t*>(srcBuffer->mapPtr()) : nullptr;

      for (uint32_t idx = 0; idx < cb.primCount; idx++) {
        generateIndices(idx, dst, src, cb);
      }

      ctx->writeToBuffer(dstSlice.buffer(), 0, cb.primCount * 3 * sizeof(uint16_t), dst);
    }
  }

  void RtxGeometryUtils::processGeometryBuffers(const InterleavedGeometryDescriptor& desc, RaytraceGeometry& output) {
    const DxvkBufferSlice targetSlice = DxvkBufferSlice(desc.buffer);

    output.positionBuffer = RaytraceBuffer(targetSlice, desc.positionOffset, desc.stride, VK_FORMAT_R32G32B32_SFLOAT);

    if (desc.hasNormals)
      output.normalBuffer = RaytraceBuffer(targetSlice, desc.normalOffset, desc.stride, VK_FORMAT_R32G32B32_SFLOAT);

    if (desc.hasTexcoord)
      output.texcoordBuffer = RaytraceBuffer(targetSlice, desc.texcoordOffset, desc.stride, VK_FORMAT_R32G32_SFLOAT);

    if (desc.hasColor0) 
      output.color0Buffer = RaytraceBuffer(targetSlice, desc.color0Offset, desc.stride, VK_FORMAT_B8G8R8A8_UNORM);
  }

  void RtxGeometryUtils::processGeometryBuffers(const RasterGeometry& input, RaytraceGeometry& output) {
    const DxvkBufferSlice slice = DxvkBufferSlice(output.historyBuffer[0]);

    output.positionBuffer = RaytraceBuffer(slice, input.positionBuffer.offsetFromSlice(), input.positionBuffer.stride(), input.positionBuffer.vertexFormat());

    if (input.normalBuffer.defined())
      output.normalBuffer = RaytraceBuffer(slice, input.normalBuffer.offsetFromSlice(), input.normalBuffer.stride(), input.normalBuffer.vertexFormat());

    if (input.texcoordBuffer.defined())
      output.texcoordBuffer = RaytraceBuffer(slice, input.texcoordBuffer.offsetFromSlice(), input.texcoordBuffer.stride(), input.texcoordBuffer.vertexFormat());

    if (input.color0Buffer.defined())
      output.color0Buffer = RaytraceBuffer(slice, input.color0Buffer.offsetFromSlice(), input.color0Buffer.stride(), input.color0Buffer.vertexFormat());
  }

  size_t RtxGeometryUtils::computeOptimalVertexStride(const RasterGeometry& input) {
    // Calculate stride
    size_t stride = sizeof(float) * 3; // position is the minimum

    if (input.normalBuffer.defined()) {
      stride += sizeof(float) * 3;
    }

    if (input.texcoordBuffer.defined()) {
      stride += sizeof(float) * 2;
    }

    if (input.color0Buffer.defined()) {
      stride += sizeof(uint32_t);
    }

    assert(stride <= kMaxInterleavedComponents * sizeof(float) && "Maximum number of interleaved components needs update.");

    return stride;
  }

  void RtxGeometryUtils::cacheVertexDataOnGPU(const Rc<DxvkContext>& ctx, const RasterGeometry& input, RaytraceGeometry& output) {
    ScopedCpuProfileZone();
    if (input.isVertexDataInterleaved() && input.areFormatsGpuFriendly()) {
      const size_t vertexBufferSize = input.vertexCount * input.positionBuffer.stride();
      ctx->copyBuffer(output.historyBuffer[0], 0, input.positionBuffer.buffer(), input.positionBuffer.offset(), vertexBufferSize);

      processGeometryBuffers(input, output);
    } else {
      RtxGeometryUtils::InterleavedGeometryDescriptor interleaveResult;
      interleaveResult.buffer = output.historyBuffer[0];

      ctx->getCommonObjects()->metaGeometryUtils().interleaveGeometry(ctx, input, interleaveResult);

      processGeometryBuffers(interleaveResult, output);
    }
  }

  void RtxGeometryUtils::interleaveGeometry(
    const Rc<DxvkContext>& ctx,
    const RasterGeometry& input,
    InterleavedGeometryDescriptor& output) const {
    ScopedGpuProfileZone(ctx, "interleaveGeometry");
    // Required
    assert(input.positionBuffer.defined());

    // Calculate stride
    output.stride = computeOptimalVertexStride(input);
    
    assert(output.buffer->info().size == align(output.stride * input.vertexCount, CACHE_LINE_SIZE));

    bool mustUseGPU = input.positionBuffer.isPendingGpuWrite() || input.positionBuffer.mapPtr() == nullptr;

    // Interleave vertex data
    InterleaveGeometryArgs args;
    assert(input.positionBuffer.offsetFromSlice() % 4 == 0);
    args.positionOffset = input.positionBuffer.offsetFromSlice() / 4;
    args.positionStride = input.positionBuffer.stride() / 4;
    args.positionFormat = input.positionBuffer.vertexFormat();
    if (!interleaver::formatConversionFloatSupported(args.positionFormat)) {
      ONCE(Logger::err(str::format("[rtx-interleaver] Unsupported position buffer format (", args.positionFormat, ")")));
      return;
    }
    args.hasNormals = input.normalBuffer.defined();
    if (args.hasNormals) {
      mustUseGPU |= input.normalBuffer.isPendingGpuWrite() || input.normalBuffer.mapPtr() == nullptr;
      assert(input.normalBuffer.offsetFromSlice() % 4 == 0);
      args.normalOffset = input.normalBuffer.offsetFromSlice() / 4;
      args.normalStride = input.normalBuffer.stride() / 4;
      args.normalFormat = input.normalBuffer.vertexFormat();
      if (!interleaver::formatConversionFloatSupported(args.normalFormat)) {
        ONCE(Logger::warn(str::format("[rtx-interleaver] Unsupported normal buffer format (", args.normalFormat, "), skipping normals")));
      }
    }
    args.hasTexcoord = input.texcoordBuffer.defined();
    if (args.hasTexcoord) {
      mustUseGPU |= input.texcoordBuffer.isPendingGpuWrite() || input.texcoordBuffer.mapPtr() == nullptr;
      assert(input.texcoordBuffer.offsetFromSlice() % 4 == 0);
      args.texcoordOffset = input.texcoordBuffer.offsetFromSlice() / 4;
      args.texcoordStride = input.texcoordBuffer.stride() / 4;
      args.texcoordFormat = input.texcoordBuffer.vertexFormat();
      if (!interleaver::formatConversionFloatSupported(args.texcoordFormat)) {
        ONCE(Logger::warn(str::format("[rtx-interleaver] Unsupported texcoord buffer format (", args.texcoordFormat, "), skipping texcoord")));
      }
    }
    args.hasColor0 = input.color0Buffer.defined();
    if (args.hasColor0) {
      mustUseGPU |= input.color0Buffer.isPendingGpuWrite() || input.color0Buffer.mapPtr() == nullptr;
      assert(input.color0Buffer.offsetFromSlice() % 4 == 0);
      args.color0Offset = input.color0Buffer.offsetFromSlice() / 4;
      args.color0Stride = input.color0Buffer.stride() / 4;
      args.color0Format = input.color0Buffer.vertexFormat();
      if (!interleaver::formatConversionUintSupported(args.color0Format)) {
        ONCE(Logger::warn(str::format("[rtx-interleaver] Unsupported color0 buffer format (", args.color0Format, "), skipping color0")));
      }
    }

    args.minVertexIndex = 0;
    assert(output.stride % 4 == 0);
    args.outputStride = output.stride / 4;
    args.vertexCount = input.vertexCount;

    const uint32_t kNumVerticesToProcessOnCPU = 1024;
    const bool useGPU = input.vertexCount > kNumVerticesToProcessOnCPU || mustUseGPU;

    if (useGPU) {
      ctx->bindResourceBuffer(INTERLEAVE_GEOMETRY_BINDING_OUTPUT, DxvkBufferSlice(output.buffer));

      ctx->bindResourceBuffer(INTERLEAVE_GEOMETRY_BINDING_POSITION_INPUT, input.positionBuffer);
      if (args.hasNormals)
        ctx->bindResourceBuffer(INTERLEAVE_GEOMETRY_BINDING_NORMAL_INPUT, input.normalBuffer);
      if (args.hasTexcoord)
        ctx->bindResourceBuffer(INTERLEAVE_GEOMETRY_BINDING_TEXCOORD_INPUT, input.texcoordBuffer);
      if (args.hasColor0)
        ctx->bindResourceBuffer(INTERLEAVE_GEOMETRY_BINDING_COLOR0_INPUT, input.color0Buffer);

      ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

      ctx->pushConstants(0, sizeof(InterleaveGeometryArgs), &args);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, InterleaveGeometryShader::getShader());

      const VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { input.vertexCount, 1, 1 }, VkExtent3D { 128, 1, 1 });
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    } else {
      float dst[kNumVerticesToProcessOnCPU * kMaxInterleavedComponents];

      GeometryBufferData inputData(input);

      // Don't need these in CPU path as GeometryBufferData handles the offset
      args.positionOffset = 0;
      args.normalOffset = 0;
      args.texcoordOffset = 0;
      args.color0Offset = 0;

      for (uint32_t i = 0; i < input.vertexCount; i++) {
        interleaver::interleave(i, dst, inputData.positionData, inputData.normalData, inputData.texcoordData, inputData.vertexColorData, args);
      }

      ctx->writeToBuffer(output.buffer, 0, input.vertexCount * output.stride, dst);
    }

    uint32_t offset = 0;

    output.positionOffset = offset;
    offset += sizeof(float) * 3;

    if (input.normalBuffer.defined()) {
      output.hasNormals = true;
      output.normalOffset = offset;
      offset += sizeof(float) * 3;
    }

    if (input.texcoordBuffer.defined()) {
      output.hasTexcoord = true;
      output.texcoordOffset = offset;
      offset += sizeof(float) * 2;
    }

    if (input.color0Buffer.defined()) {
      output.hasColor0 = true;
      output.color0Offset = offset;
      offset += sizeof(uint32_t);
    }
  }

  float RtxGeometryUtils::computeMaxUVTileSize(const RasterGeometry& input, const Matrix4& objectToWorld) {
    ScopedCpuProfileZone();

    const void* pVertexData = input.positionBuffer.mapPtr((size_t)input.positionBuffer.offsetFromSlice());
    const uint32_t vertexCount = input.vertexCount;
    const size_t vertexStride = input.positionBuffer.stride();
    
    const void* pTexcoordData = input.texcoordBuffer.mapPtr((size_t)input.texcoordBuffer.offsetFromSlice());
    const size_t texcoordStride = input.texcoordBuffer.stride();

    const void* pIndexData = input.indexBuffer.mapPtr((size_t)input.indexBuffer.offsetFromSlice());
    const uint32_t indexCount = input.indexCount;
    const size_t indexStride = input.indexBuffer.stride();

    if (pVertexData == nullptr || pTexcoordData == nullptr) {
      return NAN;
    }

    float maxUvTileSizeSqr = 0.f;
    const uint8_t* pVertex = static_cast<const uint8_t*>(pVertexData);
    const uint8_t* pTexcoord = static_cast<const uint8_t*>(pTexcoordData);
    switch (input.topology) {
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      if (input.indexCount > 0 && pIndexData != nullptr) {
        maxUvTileSizeSqr = calcMaxUvTileSizeSqrIndexed(indexCount, objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride, pIndexData, indexStride);
      } else {
        maxUvTileSizeSqr = calcMaxUvTileSizeSqrTriangles(vertexCount, objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride);
      }
      break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      maxUvTileSizeSqr = calcMaxUvTileSizeSqrTriangleStrip(vertexCount, objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride);
      break;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      maxUvTileSizeSqr = calcMaxUvTileSizeSqrTriangleFan(vertexCount, objectToWorld, pVertex, vertexStride, pTexcoord, texcoordStride);
      break;
    default:
      ONCE(Logger::err("computeMaxUVTileSize: unsupported topology"));
      return 0;
    }

    return std::sqrtf(maxUvTileSizeSqr);
  }
}
