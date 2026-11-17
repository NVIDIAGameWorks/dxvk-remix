/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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
#include <array>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>

#include "../dxvk_context.h"
#include "../dxvk_device.h"
#include "../../util/util_global_time.h"

#include "rtx_context.h"
#include "rtx_gpu_resident_scene.h"
#include "rtx_gpu_resident_scene_nee.h"
#include "rtx_materials.h"
#include "rtx_option_manager.h"
#include "rtx_options.h"
#include "rtx_scene_manager.h"
#include "rtx_texture_manager.h"
#include "rtx_types.h"
#include "rtx_upload_pages.h"
#include "../shaders/rtx/pass/instance_definitions.h"

namespace dxvk {

  namespace {

    // Every mesh surface takes at least two bindless buffer slots, whose 16-bit indices cap the useful BLAS count below this.
    constexpr uint32_t kMaxBlasCount = 65535;

    // Options set the surface and material capacities, because their CPU shadows and GPU tables are allocated in full up front.
    // A capacity may equal the shader sentinel, because the largest slot index is one less than the capacity.
    uint32_t readSurfaceCapacity() {
      return std::clamp<uint32_t>(SceneManager::gpuResidentSceneMaxSurfaceCount(), 1, SURFACE_INDEX_INVALID);
    }
    uint32_t readMaterialCapacity() {
      return std::clamp<uint32_t>(SceneManager::gpuResidentSceneMaxMaterialCount(), 1, MATERIAL_INDEX_MAX_VALUE);
    }


    Rc<DxvkBuffer> createGpuBuffer(
        DxvkDevice& device,
        size_t size,
        VkBufferUsageFlags usage,
        const char* name,
        VkDeviceSize requiredAlignment = 1
    ) {
      DxvkBufferCreateInfo info {};
      {
        info.size = size;
        info.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
                    | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                    | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
        info.requiredAlignmentOverride = requiredAlignment;
      }
      return device.createBuffer(
        info,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        DxvkMemoryStats::Category::RTXBuffer,
        name
      );
    }
    Rc<DxvkBuffer> growBuffer(
        DxvkDevice& device,
        Rc<DxvkBuffer> buffer,
        size_t size,
        VkBufferUsageFlags usage,
        const char* name,
        VkDeviceSize requiredAlignment = 1
    ) {
      ScopedCpuProfileZoneN("Remix API Ensure Buffer");
      const size_t allocationSize = align(std::max<size_t>(size, 16), kBufferAlignment);
      if (buffer != nullptr
          && buffer->info().size >= allocationSize
          && buffer->info().requiredAlignmentOverride % requiredAlignment == 0) {
        return buffer;
      }
      return createGpuBuffer(
        device,
        allocationSize,
        usage,
        name,
        requiredAlignment
      );
    }


    bool isMirrorTransform(const Matrix4& matrix) {
      const Vector3 x(matrix[0].data);
      const Vector3 y(matrix[1].data);
      const Vector3 z(matrix[2].data);
      return dot(cross(x, y), z) < 0;
    }


    enum SurfaceChange : uint8_t {
      SurfaceGpu = 1 << 0, SurfaceTlas = 1 << 1,
    };
    // Holds each dirty ID once, with the change bits of all its marks combined.
    template<typename Id>
    struct DirtyQueue {
      struct Entry { Id id; uint8_t bits; };

      std::vector<Entry> entries;
      std::vector<uint32_t> locations;
      uint8_t allBits = 0;
      void mark(Id id, uint8_t bits = 1) {
        if (!id) {
          return;
        }
        if (id.m_arrayIndex >= locations.size()) {
          locations.resize(size_t(id.m_arrayIndex) + 1);
        }
        allBits |= bits;

        const uint32_t location = locations[id.m_arrayIndex];
        if (location != 0 && entries[location - 1].id == id) {
          entries[location - 1].bits |= bits;
          return;
        }

        locations[id.m_arrayIndex] = uint32_t(entries.size()) + 1;
        entries.push_back({ id, bits });
      }

      bool empty() const { return entries.empty(); }

      void clear() {
        for (const Entry& entry : entries) { locations[entry.id.m_arrayIndex] = 0; }
        entries.clear();
        allBits = 0;
      }
    };


    struct MaterialRecord {
      std::optional<MaterialData> source;
      std::optional<RtSurfaceMaterial> gpuMaterial;
      std::optional<RtSubsurfaceMaterial> gpuSubsurface;
    };
    using PackedMaterial = std::array<unsigned char, kSurfaceMaterialGPUSize>;


    // A mesh is a chain of BLAS records, one per mesh surface, linked through next.
    struct BlasRecord {
      RaytraceGeometry geometry;
      MaterialId material;
      BlasId next;
      Rc<DxvkAccelStructure> accelStructure;
      bool ready = false;
    };


    // An instance is a chain of surface records, one per mesh surface, linked through next.
    struct SurfaceRecord {
      BlasId blas;
      SurfaceId next;
      Matrix4 objectToWorld;
      Matrix4 previousObjectToWorld;
      Matrix4 textureTransform;
      CategoryFlags categories;
      ObjectPickingValue objectPickingValue = 0;
      CameraType::Enum cameraType = CameraType::Main;
      remixapi_InstanceInfoBlendEXT blend {};
      bool doubleSided = false;
      bool hasBlend = false;
      bool frameLocal = false;
    };
    using PackedSurface = std::array<unsigned char, kSurfaceGPUSize>;


    SurfaceRecord makeSurfaceRecord(
        SurfaceRecord record,
        BlasId blas,
        const ExternalDrawState& state,
        const remixapi_InstanceInfoBlendEXT* blend,
        bool frameLocal
    ) {
      const bool isNew = !record.blas;
      record.blas = blas;
      record.objectToWorld = state.drawCall.getTransformData().objectToWorld;
      // The previous transform of an existing record advances only at frame end, so that several updates in one frame
      // keep the motion from the last rendered transform.
      if (isNew) {
        record.previousObjectToWorld = record.objectToWorld;
      }
      record.textureTransform = state.drawCall.getTransformData().textureTransform;
      record.categories = state.categories;
      record.objectPickingValue = state.drawCall.drawCallID;
      record.cameraType = state.cameraType;
      record.doubleSided = state.doubleSided;
      record.hasBlend = blend != nullptr;
      record.blend = blend != nullptr ? *blend : remixapi_InstanceInfoBlendEXT {};
      record.blend.pNext = nullptr;
      record.frameLocal = frameLocal;
      return record;
    }


    struct BlendState {
      BlendType type;
      bool inverted;
    };
    std::optional<BlendState> makeBlendState(const remixapi_InstanceInfoBlendEXT& blend) {
      if (!blend.alphaBlendEnabled
          || VkBlendOp(blend.colorBlendOp) != VK_BLEND_OP_ADD) {
        return std::nullopt;
      }

      const VkBlendFactor src = VkBlendFactor(blend.srcColorBlendFactor);
      const VkBlendFactor dst = VkBlendFactor(blend.dstColorBlendFactor);
      if (src == VK_BLEND_FACTOR_ONE && dst == VK_BLEND_FACTOR_ZERO) {
        return std::nullopt;
      }
      // The mapping matches InstanceManager::calculateAlphaState.
      const bool translateEmissive = RtxOptions::enableEmissiveBlendModeTranslation();
      if (src == VK_BLEND_FACTOR_ONE
          && dst == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA) {
        const bool premultiplied =
          (VkBlendOp(blend.alphaBlendOp) == VK_BLEND_OP_ADD
            && VkBlendFactor(blend.srcAlphaBlendFactor) == VK_BLEND_FACTOR_ONE
            && VkBlendFactor(blend.dstAlphaBlendFactor)
              == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
          || !(blend.writeMask & VK_COLOR_COMPONENT_A_BIT);
        if (premultiplied) {
          return BlendState { translateEmissive ? BlendType::kAlphaEmissive : BlendType::kAlpha, false };
        }
        return BlendState { translateEmissive ? BlendType::kReverseAlphaEmissive : BlendType::kReverseAlpha, true };
      }

      // The second type applies when rtx.enableEmissiveBlendModeTranslation is off.
      struct Mapping {
        VkBlendFactor src;
        VkBlendFactor dst;
        BlendType type;
        BlendType nonEmissiveType;
        bool inverted;
      };
      static constexpr Mapping kBlendMappings[] = {
        { VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, BlendType::kAlpha, BlendType::kAlpha, false },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA, BlendType::kAlpha, BlendType::kAlpha, true },
        { VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE, BlendType::kAlphaEmissive, BlendType::kAlpha, false },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE, BlendType::kAlphaEmissive, BlendType::kAlpha, true },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, BlendType::kReverseAlphaEmissive, BlendType::kReverseAlpha, false },
        { VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, BlendType::kColor, BlendType::kColor, false },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_COLOR, BlendType::kColor, BlendType::kColor, true },
        { VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE, BlendType::kColorEmissive, BlendType::kColor, false },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE, BlendType::kColorEmissive, BlendType::kColor, true },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, BlendType::kReverseColorEmissive, BlendType::kReverseColor, false },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, BlendType::kColorEmissive, BlendType::kColor, false },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, BlendType::kEmissive, BlendType::kColor, false },
        { VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, BlendType::kMultiplicative, BlendType::kMultiplicative, false },
        { VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_SRC_COLOR, BlendType::kMultiplicative, BlendType::kMultiplicative, false },
        { VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR, BlendType::kDoubleMultiplicative, BlendType::kDoubleMultiplicative, false },
      };
      for (const Mapping& mapping : kBlendMappings) {
        if (src == mapping.src && dst == mapping.dst) {
          return BlendState { translateEmissive ? mapping.type : mapping.nonEmissiveType, mapping.inverted };
        }
      }
      return std::nullopt;
    }


    RtSurface::AlphaState makeAlphaState(
        const SurfaceRecord& surface,
        const MaterialData& material
    ) {
      RtSurface::AlphaState state;
      if (material.getType() != MaterialDataType::Opaque) {
        return state;
      }

      const auto& opaque = material.getOpaqueMaterialData();
      bool blending = false;
      if (surface.categories.test(InstanceCategories::AlphaBlendToCutout)) {
        state.alphaTestType = AlphaTestType::kGreater;
        state.alphaTestReferenceValue =
          static_cast<uint8_t>(RtxOptions::forceCutoutAlpha() * 255.0);
      } else if (opaque.getUseLegacyAlphaState()) {
        if (surface.hasBlend) {
          if (surface.blend.alphaTestEnabled) {
            state.alphaTestType = AlphaTestType(surface.blend.alphaTestCompareOp);
            state.alphaTestReferenceValue = surface.blend.alphaTestReferenceValue;
          }
          if (const auto blend = makeBlendState(surface.blend)) {
            state.blendType = blend->type;
            state.invertedBlend = blend->inverted;
            blending = true;
          }
        }
      } else {
        state.alphaTestType = opaque.getAlphaTestType();
        state.alphaTestReferenceValue = opaque.getAlphaTestReferenceValue();
        blending = opaque.getBlendEnabled();
        state.blendType = opaque.getBlendType();
        state.invertedBlend = opaque.getInvertedBlend();
      }

      state.isBlendingDisabled = !blending;
      state.isFullyOpaque = !blending
        && state.alphaTestType == AlphaTestType::kAlways;
      state.emissiveBlend = blending && isBlendTypeEmissive(state.blendType);
      state.isParticle = blending
        && surface.categories.test(InstanceCategories::Particle);
      state.isDecal = blending
        && surface.categories.any(DECAL_CATEGORY_FLAGS);
      return state;
    }


    PackedSurface packSurface(
        const SurfaceRecord& record,
        const BlasRecord& blas,
        const MaterialRecord& material,
        uint32_t surfaceIndex
    ) {
      const RaytraceGeometry& geometry = blas.geometry;
      RtSurface surface;
      surface.positionBufferIndex = geometry.positionBufferIndex;
      surface.positionOffset = geometry.positionBuffer.offsetFromSlice();
      surface.positionStride = geometry.positionBuffer.stride();
      surface.previousPositionBufferIndex = geometry.previousPositionBufferIndex;
      surface.normalBufferIndex = geometry.normalBufferIndex;
      surface.normalOffset = geometry.normalBuffer.offsetFromSlice();
      surface.normalStride = geometry.normalBuffer.stride();
      surface.normalFormat = geometry.normalBuffer.vertexFormat();
      surface.texcoordBufferIndex = geometry.texcoordBufferIndex;
      surface.texcoordOffset = geometry.texcoordBuffer.offsetFromSlice();
      surface.texcoordStride = geometry.texcoordBuffer.stride();
      surface.indexBufferIndex = geometry.indexBufferIndex;
      surface.indexStride = geometry.indexBuffer.stride();
      surface.color0BufferIndex = geometry.color0BufferIndex;
      surface.color0Offset = geometry.color0Buffer.offsetFromSlice();
      surface.color0Stride = geometry.color0Buffer.stride();
      surface.objectToWorld = record.objectToWorld;
      surface.prevObjectToWorld = record.previousObjectToWorld;
      surface.normalObjectToWorld = transpose(inverse(Matrix3(record.objectToWorld)));
      surface.textureTransform = record.textureTransform;
      surface.objectPickingValue = record.objectPickingValue;
      surface.associatedGeometryHash =
        geometry.hashes[HashComponents::GeometryDescriptor];
      surface.isStatic = record.objectToWorld == record.previousObjectToWorld;
      surface.alphaState = makeAlphaState(record, *material.source);
      surface.isMotionBlurMaskOut =
        record.categories.test(InstanceCategories::IgnoreMotionBlur);
      surface.isAnimatedWater =
        record.categories.test(InstanceCategories::AnimatedWater);
      // InstanceCategories::IgnoreAlphaChannel is not supported. The GPU material is cached per material,
      // so a per-surface OPAQUE_SURFACE_MATERIAL_FLAG_IGNORE_ALPHA_CHANNEL bit has no place to go.
      if (record.hasBlend) {
        surface.textureColorArg1Source =
          RtTextureArgSource(record.blend.textureColorArg1Source);
        surface.textureColorArg2Source =
          RtTextureArgSource(record.blend.textureColorArg2Source);
        surface.textureColorOperation =
          DxvkRtTextureOperation(record.blend.textureColorOperation);
        surface.textureAlphaArg1Source =
          RtTextureArgSource(record.blend.textureAlphaArg1Source);
        surface.textureAlphaArg2Source =
          RtTextureArgSource(record.blend.textureAlphaArg2Source);
        surface.textureAlphaOperation =
          DxvkRtTextureOperation(record.blend.textureAlphaOperation);
        surface.tFactor = record.blend.tFactor;
        surface.isTextureFactorBlend = record.blend.isTextureFactorBlend;
        surface.isVertexColorBakedLighting =
          bool(record.blend.isVertexColorBakedLighting);
      }
      material.source->getSpriteSheetData(
        surface.spriteSheetRows,
        surface.spriteSheetCols,
        surface.spriteSheetFPS
      );

      // The material table is indexed by material slot, not by surface.
      const uint32_t gpuMaterialIndex = blas.material.m_arrayIndex;
      PackedSurface result {};
      size_t offset = 0;
      surface.writeGPUData(
        result.data(),
        offset,
        surfaceIndex,
        GlobalTime::get().absoluteTimeUs(),
        gpuMaterialIndex
      );
      return result;
    }


    PackedMaterial packMaterial(const RtSurfaceMaterial& material, uint32_t materialIndex) {
      PackedMaterial result {};
      size_t offset = 0;
      material.writeGPUData(result.data(), offset, materialIndex);
      return result;
    }
    PackedMaterial packMaterialExtension(const RtSubsurfaceMaterial& material) {
      PackedMaterial result {};
      size_t offset = 0;
      material.writeGPUData(result.data(), offset);
      return result;
    }


    void retainMaterialTextures(RtxTextureManager& textureManager, const MaterialRecord& material) {
      const auto retain = [&](uint32_t textureIndex) { textureManager.retainTexture(textureIndex); };
      if (material.gpuMaterial.has_value()) {
        material.gpuMaterial->forEachTextureIndex(retain);
      }
      if (material.gpuSubsurface.has_value()) {
        material.gpuSubsurface->forEachTextureIndex(retain);
      }
    }
    void releaseMaterialTextures(RtxTextureManager& textureManager, const MaterialRecord& material) {
      const auto release = [&](uint32_t textureIndex) { textureManager.releaseTexture(textureIndex); };
      if (material.gpuMaterial.has_value()) {
        material.gpuMaterial->forEachTextureIndex(release);
      }
      if (material.gpuSubsurface.has_value()) {
        material.gpuSubsurface->forEachTextureIndex(release);
      }
    }


    // Records where a surface sits in one TLAS instance buffer.
    // The previous generation and BLAS address decide whether the surface keeps its history across a rebuild.
    struct TlasSlot {
      uint32_t generation = 0;
      uint32_t index = ~0u;
      uint32_t previousGeneration = 0;
      VkDeviceAddress previousOpaqueBlas = 0;
      Tlas::Type type = Tlas::Count;
    };
    struct TlasInstances {
      Rc<DxvkBuffer> buffer;
      std::vector<VkAccelerationStructureInstanceKHR> values;
      std::vector<SurfaceId> surfaces;
      std::vector<Rc<DxvkAccelStructure>> owners;
      UploadPages<sizeof(VkAccelerationStructureInstanceKHR)> uploads;
      bool rebuild = true;

      void markDirty(uint32_t index) {
        uploads.markSlot(index);
      }
    };


    template<typename Table, size_t SlotSize>
    void writeGpuSlot(
        Table& table,
        uint32_t index,
        const std::array<unsigned char, SlotSize>& value
    ) {
      std::memcpy(
        table.shadow.data() + size_t(index) * SlotSize,
        value.data(),
        SlotSize
      );
      table.uploads.markSlot(index);
    }


    // Holds the slots freed in one frame.
    // They return to their arrays once the GPU event after that frame's work signals.
    struct FreeBatch {
      Rc<DxvkGpuEvent> event;
      std::vector<BlasId> blases;
      std::vector<MaterialId> materials;
      std::vector<SurfaceId> surfaces;

      bool empty() const {
        return blases.empty() && materials.empty() && surfaces.empty();
      }
    };


    struct BlasState {
      GenerationArray<BlasId, BlasRecord> entries { kMaxBlasCount };
      DirtyQueue<BlasId> dirty;
    };
    struct MaterialState {
      explicit MaterialState(uint32_t capacity)
        : entries(capacity)
        , shadow(size_t(capacity) * kSurfaceMaterialGPUSize)
        , uploads(capacity)
        , extensionShadow(size_t(capacity) * kSurfaceMaterialGPUSize)
        , extensionUploads(capacity) {
      }

      GenerationArray<MaterialId, MaterialRecord> entries;
      DirtyQueue<MaterialId> dirty;

      Rc<DxvkBuffer> gpuBuffer;
      std::vector<unsigned char> shadow;
      UploadPages<kSurfaceMaterialGPUSize> uploads;

      Rc<DxvkBuffer> extensionBuffer;
      std::vector<unsigned char> extensionShadow;
      UploadPages<kSurfaceMaterialGPUSize> extensionUploads;

      MaterialId cameraMedium;
    };
    struct SurfaceState {
      explicit SurfaceState(uint32_t capacity)
        : entries(capacity)
        , shadow(size_t(capacity) * kSurfaceGPUSize)
        , uploads(capacity)
        , mappingShadow(capacity, SURFACE_INDEX_INVALID)
        , mappingUploads(capacity) {
      }

      GenerationArray<SurfaceId, SurfaceRecord> entries;
      DirtyQueue<SurfaceId> dirty;

      Rc<DxvkBuffer> gpuBuffer;
      std::vector<unsigned char> shadow;
      UploadPages<kSurfaceGPUSize> uploads;

      Rc<DxvkBuffer> mappingBuffer;
      std::vector<uint32_t> mappingShadow;
      UploadPages<sizeof(uint32_t)> mappingUploads;

      std::vector<SurfaceId> frameLocal;
      std::vector<SurfaceId> pendingHistory;
    };
    struct FreeState {
      FreeBatch pending;
      std::deque<FreeBatch> batches;
    };
    struct SceneState {
      SceneState(uint32_t surfaceCapacity, uint32_t materialCapacity)
        : materials(materialCapacity)
        , surfaces(surfaceCapacity) {
      }

      mutable std::mutex mutex;
      BlasState blases;
      MaterialState materials;
      SurfaceState surfaces;
      FreeState frees;
    };


    bool releaseBlasChain(SceneState& sceneState, BlasId id) {
      const size_t firstReleased = sceneState.frees.pending.blases.size();
      while (id) {
        if (sceneState.blases.entries.lget(id) != nullptr) {
          sceneState.blases.entries.freeId(id);
        }
        BlasRecord* record = sceneState.blases.entries.processFree(id);
        if (record == nullptr) {
          break;
        }
        sceneState.frees.pending.blases.push_back(id);
        id = record->next;
      }

      const auto released = sceneState.frees.pending.blases.begin() + firstReleased;
      if (released == sceneState.frees.pending.blases.end()) {
        return false;
      }
      for (uint32_t i = 0; i < sceneState.surfaces.entries.capacity(); i++) {
        const SurfaceId surfaceId = sceneState.surfaces.entries.liveIdAt(i);
        const SurfaceRecord* surface = sceneState.surfaces.entries.lget(surfaceId);
        if (surface != nullptr
            && std::find(released, sceneState.frees.pending.blases.end(), surface->blas)
              != sceneState.frees.pending.blases.end()) {
          sceneState.surfaces.dirty.mark(surfaceId, SurfaceGpu | SurfaceTlas);
        }
      }
      return true;
    }


    bool releaseSurfaceChain(SceneState& sceneState, SurfaceId id) {
      bool released = false;
      while (id) {
        if (sceneState.surfaces.entries.lget(id) != nullptr) {
          sceneState.surfaces.entries.freeId(id);
        }
        SurfaceRecord* record = sceneState.surfaces.entries.processFree(id);
        if (record == nullptr) {
          break;
        }
        const SurfaceId next = record->next;
        sceneState.surfaces.dirty.mark(id, SurfaceGpu | SurfaceTlas);
        sceneState.frees.pending.surfaces.push_back(id);
        released = true;
        id = next;
      }
      return released;
    }

    // Points the surface chain starting at root at the BLAS chain of a mesh, one surface per BLAS.
    // Grows the surface chain as needed and releases the surfaces left over from a longer chain.
    bool setSurfaceChain(
        SceneState& sceneState,
        SurfaceId root,
        BlasId blas,
        const ExternalDrawState& state,
        const remixapi_InstanceInfoBlendEXT* blend,
        bool frameLocal
    ) {
      if (state.drawCall.getSkinningState().numBones != 0) {
        ONCE(Logger::warn("GPU-resident scene renders skinned meshes in bind pose."));
      }
      if (!state.gpuInstancingTransforms.empty()) {
        ONCE(Logger::warn("GPU-resident scene ignores GPU instance arrays."));
      }
      if (state.optionalParticleDesc.has_value()) {
        ONCE(Logger::warn("GPU-resident scene renders particles as their base triangles."));
      }
      if (state.categories.test(InstanceCategories::Sky)) {
        ONCE(Logger::warn("GPU-resident scene does not render Sky-category instances yet."));
      }
      SurfaceId surface = root;
      SurfaceId previous;
      while (BlasRecord* blasRecord = sceneState.blases.entries.lget(blas)) {
        if (!surface) {
          surface = sceneState.surfaces.entries.allocId();
          if (!surface) {
            ONCE(Logger::err("GPU-resident scene exceeded its fixed surface capacity."));
            return false;
          }
          sceneState.surfaces.entries.lget(previous)->next = surface;
        }

        SurfaceRecord* surfaceRecord = sceneState.surfaces.entries.lget(surface);
        if (surfaceRecord == nullptr) {
          return false;
        }
        const bool existed = bool(surfaceRecord->blas);
        *surfaceRecord = makeSurfaceRecord(
          std::move(*surfaceRecord),
          blas,
          state,
          blend,
          frameLocal
        );
        sceneState.surfaces.dirty.mark(surface, SurfaceGpu | SurfaceTlas);
        if (existed && !frameLocal) {
          sceneState.surfaces.pendingHistory.push_back(surface);
        }
        previous = surface;
        surface = surfaceRecord->next;
        blas = blasRecord->next;
      }

      if (blas || !previous) {
        return false;
      }
      sceneState.surfaces.entries.lget(previous)->next = {};
      releaseSurfaceChain(sceneState, surface);
      return true;
    }


    struct AccelerationState {
      Rc<DxvkBuffer> scratch;
      Tlas tlas[Tlas::Count];
      TlasInstances instances[Tlas::Count];
      // Acceleration structures contain raw device addresses of their child acceleration structures.
      // Keep the complete transitive owner sets paired with both opaque TLAS generations,
      // including across frames where no TLAS rebuild is necessary.
      std::vector<Rc<DxvkAccelStructure>> currentOpaqueBlasOwners;
      std::vector<Rc<DxvkAccelStructure>> previousOpaqueBlasOwners;
      std::vector<TlasSlot> slots;
      std::vector<TlasSlot> sssSlots;
      std::vector<SurfaceId> pendingMappings;
      // A TLAS stores raw BLAS device addresses and does not retain their Rc objects.
      // Owners removed from any TLAS type wait here until the command list that rebuilds that TLAS tracks them,
      // which also orders their release after earlier in-flight traversal.
      std::vector<Rc<DxvkAccelStructure>> retiredBlasOwners;
    };


    void finishFreeBatch(
        SceneState& sceneState,
        DxvkDevice& device,
        SceneManager& sceneManager,
        FreeBatch& batch
    ) {
      auto& textureManager =
        device.getCommon()->getTextureManager();
      for (SurfaceId id : batch.surfaces) {
        sceneState.surfaces.entries.completeFree(id);
      }
      for (MaterialId id : batch.materials) {
        MaterialRecord material = sceneState.materials.entries.completeFree(id);
        releaseMaterialTextures(textureManager, material);
      }
      for (BlasId id : batch.blases) {
        BlasRecord blas = sceneState.blases.entries.completeFree(id);
        if (blas.ready) {
          sceneManager.releaseRemixApiGeometry(blas.geometry);
        }
      }
    }
    void finishFrees(
        SceneState& sceneState,
        DxvkDevice& device,
        SceneManager& sceneManager
    ) {
      while (!sceneState.frees.batches.empty()
          && sceneState.frees.batches.front().event->test()
            == DxvkGpuEventStatus::Signaled) {
        FreeBatch& batch = sceneState.frees.batches.front();
        finishFreeBatch(sceneState, device, sceneManager, batch);
        sceneState.frees.batches.pop_front();
      }
    }
    void submitFrees(
        SceneState& sceneState,
        DxvkDevice& device,
        const Rc<RtxContext>& ctx
    ) {
      FreeBatch batch = std::move(sceneState.frees.pending);
      sceneState.frees.pending = {};
      if (batch.empty()) {
        return;
      }

      batch.event = device.createGpuEvent();
      ctx->signalGpuEvent(batch.event);
      sceneState.frees.batches.push_back(std::move(batch));
    }

  }  // namespace

  struct GpuResidentScene::Data {
    explicit Data(DxvkDevice* pDevice)
      : device(pDevice)
      , sceneState(readSurfaceCapacity(), readMaterialCapacity())
      , neeScene(*pDevice, sceneState.surfaces.entries.capacity()) {
      const uint32_t surfaceCapacity = sceneState.surfaces.entries.capacity();
      const uint32_t materialCapacity = sceneState.materials.entries.capacity();
      Logger::info(str::format("GPU-resident scene allocated ", surfaceCapacity, " instance surface slots and ", materialCapacity, " material slots."));

      constexpr VkBufferUsageFlags kShaderUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

      sceneState.surfaces.gpuBuffer = createGpuBuffer(
        *device,
        size_t(surfaceCapacity) * kSurfaceGPUSize,
        kShaderUsage,
        "Remix API Surface Buffer"
      );
      sceneState.materials.gpuBuffer = createGpuBuffer(
        *device,
        size_t(materialCapacity) * kSurfaceMaterialGPUSize,
        kShaderUsage,
        "Remix API Surface Material Buffer"
      );
      sceneState.materials.extensionBuffer = createGpuBuffer(
        *device,
        size_t(materialCapacity) * kSurfaceMaterialGPUSize,
        kShaderUsage,
        "Remix API Surface Material Extension Buffer"
      );
      sceneState.surfaces.mappingBuffer = createGpuBuffer(
        *device,
        size_t(surfaceCapacity) * sizeof(uint32_t),
        kShaderUsage,
        "Remix API Surface Mapping Buffer"
      );

      constexpr const char* kTlasInstanceBufferNames[Tlas::Count] = {
        "Remix API Opaque TLAS Instances",
        "Remix API Unordered TLAS Instances",
        "Remix API SSS TLAS Instances",
      };
      acceleration.slots.resize(surfaceCapacity);
      acceleration.sssSlots.resize(surfaceCapacity);
      for (size_t type = 0; type < Tlas::Count; type++) {
        TlasInstances& instances = acceleration.instances[type];
        instances.uploads = UploadPages<sizeof(VkAccelerationStructureInstanceKHR)>(surfaceCapacity);
        instances.buffer = createGpuBuffer(
          *device,
          size_t(surfaceCapacity)
            * sizeof(VkAccelerationStructureInstanceKHR),
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
          kTlasInstanceBufferNames[type]
        );
        instances.values.reserve(surfaceCapacity);
        instances.surfaces.reserve(surfaceCapacity);
        instances.owners.reserve(surfaceCapacity);
      }
    }


    DxvkDevice* device;
    SceneState sceneState;
    Rc<DxvkBuffer> emptyBuffer;
    bool resourcesPrepared = false;
    AccelerationState acceleration;
    GpuResidentSceneNee neeScene;
  };


  GpuResidentScene::GpuResidentScene(DxvkDevice* device)
    : m_data(std::make_unique<Data>(device)) {
  }
  GpuResidentScene::~GpuResidentScene() = default;


  MaterialId gpuResidentSceneAllocMaterialId(GpuResidentScene& scene) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    const MaterialId id = sceneState.materials.entries.allocId();
    if (!id) {
      ONCE(Logger::err("GPU-resident scene exceeded its fixed material capacity."));
    }
    return id;
  }
  bool gpuResidentSceneFreeMaterialId(GpuResidentScene& scene, MaterialId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return sceneState.materials.entries.freeId(id);
  }
  bool gpuResidentSceneSetMaterial(
      GpuResidentScene& scene,
      SceneManager&,
      MaterialId id,
      MaterialData&& material
  ) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    MaterialRecord* record = sceneState.materials.entries.lset(id);
    if (record == nullptr) {
      return false;
    }

    if (material.getType() == MaterialDataType::RayPortal) {
      ONCE(Logger::warn("GPU-resident scene renders portal materials as default opaque."));
      material = MaterialData {};
    }
    {
      Vector2 scrollSpeed;
      Vector2 scale;
      Vector2 offset;
      float rotation;
      material.getTextureTransformData(scrollSpeed, scale, offset, rotation);
      if (scrollSpeed != Vector2(0.f) || scale != Vector2(1.f) || offset != Vector2(0.f) || rotation != 0.f) {
        ONCE(Logger::warn("GPU-resident scene ignores material texture transforms."));
      }
    }

    record->source.emplace(std::move(material));
    sceneState.materials.dirty.mark(id);
    return true;
  }
  bool gpuResidentSceneUpdateMaterial(
      GpuResidentScene& scene,
      MaterialId id,
      MaterialData&& material,
      bool& isCameraMediumMaterial
  ) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    isCameraMediumMaterial = false;

    MaterialRecord* record = sceneState.materials.entries.lget(id);
    if (record == nullptr || !record->source.has_value()) {
      ONCE(Logger::warn("UpdateMaterial: material handle not found."));
      return false;
    }

    const MaterialData& previous = *record->source;
    uint8_t previousRows, previousCols, previousFps;
    uint8_t rows, cols, fps;
    previous.getSpriteSheetData(previousRows, previousCols, previousFps);
    material.getSpriteSheetData(rows, cols, fps);

    // These fields feed the surface table or the TLAS instance flags, which only a SetMaterial refreshes.
    bool affectsScene = previous.getType() != material.getType()
      || previousRows != rows || previousCols != cols || previousFps != fps;
    if (!affectsScene && material.getType() == MaterialDataType::Opaque) {
      const OpaqueMaterialData& previousOpaque = previous.getOpaqueMaterialData();
      const OpaqueMaterialData& opaque = material.getOpaqueMaterialData();
      affectsScene = previousOpaque.getUseLegacyAlphaState() != opaque.getUseLegacyAlphaState()
        || previousOpaque.getBlendEnabled() != opaque.getBlendEnabled()
        || previousOpaque.getBlendType() != opaque.getBlendType()
        || previousOpaque.getInvertedBlend() != opaque.getInvertedBlend()
        || previousOpaque.getAlphaTestType() != opaque.getAlphaTestType()
        || previousOpaque.getAlphaTestReferenceValue() != opaque.getAlphaTestReferenceValue()
        || previousOpaque.getSubsurfaceDiffusionProfile() != opaque.getSubsurfaceDiffusionProfile();
    }
    if (affectsScene) {
      ONCE(Logger::warn("UpdateMaterial: updates affecting surfaces or acceleration structures are not supported."));
      return false;
    }

    record->source.emplace(std::move(material));
    sceneState.materials.dirty.mark(id);
    isCameraMediumMaterial = sceneState.materials.cameraMedium == id;
    return true;
  }
  bool gpuResidentSceneReleaseMaterial(
      GpuResidentScene& scene,
      MaterialId id,
      bool& wasCameraMediumMaterial
  ) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    wasCameraMediumMaterial = false;
    MaterialRecord* record = sceneState.materials.entries.processFree(id);
    if (record == nullptr) {
      return false;
    }
    for (uint32_t i = 0; i < sceneState.surfaces.entries.capacity(); i++) {
      const SurfaceId surfaceId = sceneState.surfaces.entries.liveIdAt(i);
      const SurfaceRecord* surface = sceneState.surfaces.entries.lget(surfaceId);
      const BlasRecord* blas = surface ? sceneState.blases.entries.lget(surface->blas) : nullptr;
      if (blas != nullptr && blas->material == id) {
        sceneState.surfaces.dirty.mark(surfaceId, SurfaceGpu | SurfaceTlas);
      }
    }
    if (sceneState.materials.cameraMedium == id) {
      sceneState.materials.cameraMedium = {};
      wasCameraMediumMaterial = true;
    }
    sceneState.materials.dirty.mark(id);
    sceneState.frees.pending.materials.push_back(id);
    return true;
  }


  BlasId gpuResidentSceneAllocMeshId(GpuResidentScene& scene) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    const BlasId id = sceneState.blases.entries.allocId();
    if (!id) {
      ONCE(Logger::err("GPU-resident scene exceeded its fixed BLAS capacity."));
    }
    return id;
  }
  bool gpuResidentSceneFreeMeshId(GpuResidentScene& scene, BlasId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return sceneState.blases.entries.freeId(id);
  }


  bool gpuResidentSceneSetMesh(
      GpuResidentScene& scene,
      SceneManager& sceneManager,
      DxvkContext* ctx,
      BlasId id,
      std::vector<RasterGeometry>&& surfaces
  ) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    if (surfaces.empty()) {
      return false;
    }
    if (sceneState.blases.entries.lset(id) == nullptr) {
      return false;
    }

    BlasId current = id;
    BlasId previous;
    for (RasterGeometry& surface : surfaces) {
      if (!current) {
        current = sceneState.blases.entries.allocId();
        if (!current) {
          ONCE(Logger::err("GPU-resident scene exceeded its fixed BLAS capacity; mesh was truncated."));
          break;
        }
      }
      BlasRecord& record = *sceneState.blases.entries.lget(current);
      if (BlasRecord* previousRecord = sceneState.blases.entries.lget(previous)) {
        previousRecord->next = current;
      }

      record.material = unpackGenerationArrayId<MaterialId>(
        surface.externalMaterial
      );
      record.accelStructure = nullptr;
      record.ready = sceneManager.prepareRemixApiGeometry(
        ctx,
        std::move(surface),
        record.geometry
      );
      if (record.ready) {
        sceneState.blases.dirty.mark(current);
      }
      previous = current;
      current = {};
    }
    return true;
  }
  bool gpuResidentSceneReleaseMesh(GpuResidentScene& scene, BlasId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return releaseBlasChain(sceneState, id);
  }


  SurfaceId gpuResidentSceneAllocInstanceId(GpuResidentScene& scene) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    SurfaceId id = sceneState.surfaces.entries.allocId();
    if (!id) {
      ONCE(Logger::err("GPU-resident scene exceeded its fixed surface capacity."));
    }
    return id;
  }
  bool gpuResidentSceneFreeInstanceId(GpuResidentScene& scene, SurfaceId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return sceneState.surfaces.entries.freeId(id);
  }
  bool gpuResidentSceneSetInstance(
      GpuResidentScene& scene,
      SurfaceId id,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  ) {
    const BlasId blas = unpackGenerationArrayId<BlasId>(state.mesh);
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    if (sceneState.surfaces.entries.lset(id) == nullptr) {
      return false;
    }
    if (sceneState.blases.entries.lget(blas) == nullptr) {
      return false;
    }
    return setSurfaceChain(sceneState, id, blas, state, blend, false);
  }
  void gpuResidentSceneAddFrameLocalInstance(
      GpuResidentScene& scene,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  ) {
    const BlasId blas = unpackGenerationArrayId<BlasId>(state.mesh);
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    if (sceneState.blases.entries.lget(blas) == nullptr) {
      return;
    }

    SurfaceId root = sceneState.surfaces.entries.allocId();
    if (!root) {
      ONCE(Logger::err("GPU-resident scene exceeded its fixed surface capacity."));
      return;
    }
    if (!setSurfaceChain(sceneState, root, blas, state, blend, true)) {
      releaseSurfaceChain(sceneState, root);
      return;
    }
    sceneState.surfaces.frameLocal.push_back(root);
  }
  bool gpuResidentSceneUpdateInstance(
      GpuResidentScene& scene,
      SurfaceId id,
      ExternalDrawState&& state,
      const remixapi_InstanceInfoBlendEXT* blend
  ) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    SurfaceRecord* record = sceneState.surfaces.entries.lget(id);
    if (record == nullptr || record->frameLocal) {
      return false;
    }
    const BlasId blas = unpackGenerationArrayId<BlasId>(state.mesh);
    return sceneState.blases.entries.lget(blas) != nullptr
        && setSurfaceChain(sceneState, id, blas, state, blend, false);
  }
  bool gpuResidentSceneReleaseInstance(GpuResidentScene& scene, SurfaceId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return releaseSurfaceChain(sceneState, id);
  }
  void gpuResidentSceneEndFrame(GpuResidentScene& scene, SceneManager& sceneManager) {
    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    std::lock_guard lock { sceneState.mutex };
    finishFrees(sceneState, *data.device, sceneManager);
    for (SurfaceId id : sceneState.surfaces.frameLocal) {
      releaseSurfaceChain(sceneState, id);
    }
    sceneState.surfaces.frameLocal.clear();

    // An updated surface keeps its previous transform for one frame, which gives it motion vectors.
    // From the next frame on it is static again, unless it is updated again.
    for (SurfaceId id : sceneState.surfaces.pendingHistory) {
      SurfaceRecord* record = sceneState.surfaces.entries.lget(id);
      if (record != nullptr
          && record->previousObjectToWorld != record->objectToWorld) {
        record->previousObjectToWorld = record->objectToWorld;
        sceneState.surfaces.dirty.mark(id, SurfaceGpu);
      }
    }
    sceneState.surfaces.pendingHistory.clear();
  }


  void gpuResidentSceneResolveDeferredFrees(
      GpuResidentScene& scene,
      SceneManager& sceneManager,
      const std::function<void()>& synchronize
  ) {
    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    {
      std::lock_guard lock { sceneState.mutex };
      if (sceneState.frees.pending.empty()
          && sceneState.frees.batches.empty()) {
        return;
      }
    }

    synchronize();

    bool sceneEmpty;
    {
      std::lock_guard lock { sceneState.mutex };
      finishFreeBatch(
        sceneState, *data.device, sceneManager, sceneState.frees.pending);
      for (FreeBatch& batch : sceneState.frees.batches) {
        finishFreeBatch(sceneState, *data.device, sceneManager, batch);
      }
      sceneState.frees.pending = {};
      sceneState.frees.batches.clear();
      sceneEmpty = sceneState.surfaces.entries.empty()
          && sceneState.materials.entries.empty()
          && sceneState.blases.entries.empty();
    }

    if (sceneEmpty) {
      sceneManager.requestTextureVramFree();
    } else {
      sceneManager.requestVramCompaction();
    }
  }


  void gpuResidentScenePrepareTextureCacheClear(GpuResidentScene& scene) {
    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    std::lock_guard lock { sceneState.mutex };
    auto& textureManager = data.device->getCommon()->getTextureManager();
    sceneState.materials.entries.forEachAllocated(
      [&](MaterialId id, MaterialRecord& material) {
        releaseMaterialTextures(textureManager, material);
        material.gpuMaterial.reset();
        material.gpuSubsurface.reset();
        if (id && material.source.has_value()) {
          sceneState.materials.dirty.mark(id);
        }
      });
  }


  namespace {

    void removeTlasSlot(
        AccelerationState& acceleration,
        std::vector<TlasSlot>& slots,
        uint32_t surfaceIndex
    ) {
      TlasSlot& slot = slots[surfaceIndex];
      TlasInstances& instances = acceleration.instances[slot.type];
      const uint32_t index = slot.index;
      const uint32_t last = uint32_t(instances.values.size() - 1);
      acceleration.retiredBlasOwners.push_back(
        std::move(instances.owners[index])
      );
      if (index != last) {
        instances.values[index] = instances.values[last];
        instances.surfaces[index] = instances.surfaces[last];
        instances.owners[index] = std::move(instances.owners[last]);
        slots[instances.surfaces[index].m_arrayIndex].index = index;
        instances.markDirty(index);
      }
      instances.values.pop_back();
      instances.surfaces.pop_back();
      instances.owners.pop_back();
      const uint32_t previousGeneration = slot.previousGeneration;
      const VkDeviceAddress previousOpaqueBlas = slot.previousOpaqueBlas;
      slot = {};
      slot.previousGeneration = previousGeneration;
      slot.previousOpaqueBlas = previousOpaqueBlas;
      instances.rebuild = true;
    }


    // Adds, moves, updates or removes the surface's instance in the TLAS of the given type.
    // Tlas::Count removes the instance.
    void applyTlasSlot(
        const SceneState& sceneState,
        AccelerationState& acceleration,
        std::vector<TlasSlot>& slots,
        SurfaceId surfaceId,
        BlasId blasId,
        const VkAccelerationStructureInstanceKHR& instance,
        Tlas::Type type
    ) {
      const SurfaceRecord* surface = sceneState.surfaces.entries.lget(surfaceId);
      TlasSlot& slot = slots[surfaceId.m_arrayIndex];
      if (slot.generation != 0
          && slot.generation != surfaceId.m_generation) {
        if (surface == nullptr) {
          return;
        }
        removeTlasSlot(acceleration, slots, surfaceId.m_arrayIndex);
      }
      if (slot.generation != 0 && type != slot.type) {
        removeTlasSlot(acceleration, slots, surfaceId.m_arrayIndex);
      }
      if (type == Tlas::Count) {
        return;
      }

      const Rc<DxvkAccelStructure>& owner =
        sceneState.blases.entries.lget(blasId)->accelStructure;
      TlasInstances& instances = acceleration.instances[type];
      if (slot.generation == 0) {
        slot.generation = surfaceId.m_generation;
        slot.index = uint32_t(instances.values.size());
        slot.type = type;
        instances.values.push_back(instance);
        instances.surfaces.push_back(surfaceId);
        instances.owners.push_back(owner);
        instances.markDirty(slot.index);
        instances.rebuild = true;
        return;
      }

      const bool ownerChanged = instances.owners[slot.index] != owner;
      if (!ownerChanged && std::memcmp(&instances.values[slot.index],
            &instance, sizeof(instance)) == 0) {
        return;
      }
      if (ownerChanged) {
        acceleration.retiredBlasOwners.push_back(
          std::move(instances.owners[slot.index])
        );
        instances.owners[slot.index] = owner;
      }
      instances.values[slot.index] = instance;
      instances.markDirty(slot.index);
      instances.rebuild = true;
    }

    void applyTlasSurface(
        const SceneState& sceneState,
        AccelerationState& acceleration,
        SurfaceId id
    ) {
      BlasId blasId;
      VkAccelerationStructureInstanceKHR instance {};
      Tlas::Type type = Tlas::Count;
      bool sss = false;
      const SurfaceRecord* surface = sceneState.surfaces.entries.lget(id);
      const BlasRecord* blas = surface != nullptr
        ? sceneState.blases.entries.lget(surface->blas)
        : nullptr;
      const MaterialRecord* material = blas != nullptr
        ? sceneState.materials.entries.lget(blas->material)
        : nullptr;
      if (surface != nullptr && blas != nullptr
          && blas->accelStructure != nullptr && material != nullptr
          && material->gpuMaterial.has_value()
          && !surface->categories.any(
            InstanceCategories::Ignore,
            InstanceCategories::Hidden,
            InstanceCategories::Sky)) {
        blasId = surface->blas;
        const Matrix4 transform = transpose(surface->objectToWorld);
        const uint32_t materialType = uint32_t(material->gpuMaterial->getType());
        {
          std::memcpy(
            &instance.transform,
            &transform,
            sizeof(instance.transform)
          );
          instance.instanceShaderBindingTableRecordOffset = 0;
          instance.accelerationStructureReference = blas->accelStructure->getAccelDeviceAddress();
          instance.flags = (blas->geometry.frontFace == VK_FRONT_FACE_CLOCKWISE
                              ? VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR
                              : 0U);
          instance.instanceCustomIndex = id.m_arrayIndex | (materialType << CUSTOM_INDEX_MATERIAL_TYPE_BIT);
        }
        if (isMirrorTransform(surface->objectToWorld)) {
          instance.flags ^= VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR;
        }
        if (surface->doubleSided) {
          instance.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        }

        const RtSurface::AlphaState alpha =
          makeAlphaState(*surface, *material->source);
        instance.flags |= alpha.isFullyOpaque
          ? VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR
          : VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

        const bool unordered = !alpha.isBlendingDisabled
          || surface->categories.any(
            InstanceCategories::Particle,
            InstanceCategories::Beam,
            DECAL_CATEGORY_FLAGS
          );
        const bool isPlayerModel =
          surface->categories.test(InstanceCategories::ThirdPersonPlayerModel);

        if (surface->cameraType == CameraType::ViewModel) {
          // Viewmodel perspective correction and portal clones are not implemented.
          instance.mask = OBJECT_MASK_VIEWMODEL;
          instance.instanceCustomIndex |= CUSTOM_INDEX_IS_VIEW_MODEL;
          type = Tlas::Opaque;
        } else if (isPlayerModel) {
          instance.mask = OBJECT_MASK_PLAYER_MODEL;
          type = Tlas::Opaque;
        } else if (unordered && RtxOptions::enableSeparateUnorderedApproximations()) {
          instance.mask = OBJECT_MASK_UNORDERED_ALL_BLENDED;
          instance.flags &= ~VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
          instance.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
          type = Tlas::Unordered;
        } else if (material->gpuMaterial->getType() == RtSurfaceMaterialType::Translucent) {
          instance.mask = OBJECT_MASK_TRANSLUCENT;
          type = Tlas::Opaque;
        } else {
          instance.mask = alpha.isBlendingDisabled
            ? OBJECT_MASK_OPAQUE
            : OBJECT_MASK_ALPHA_BLEND;
          type = Tlas::Opaque;
        }
        sss = type == Tlas::Opaque
          && RtxOptions::SubsurfaceScattering::enableDiffusionProfile()
          && material->gpuSubsurface.has_value()
          && material->gpuSubsurface->getSubsurfaceRadiusScale() > 0.0f;
      }

      applyTlasSlot(sceneState, acceleration, acceleration.slots,
        id, blasId, instance, type
      );
      applyTlasSlot(sceneState, acceleration, acceleration.sssSlots,
        id, blasId, instance, sss ? Tlas::SSS : Tlas::Count
      );
    }


    // Builds one PREFER_FAST_BUILD BLAS per dirty mesh surface whose geometry and material are ready.
    void buildDirtyBlases(
        DxvkDevice& device,
        SceneState& sceneState,
        const Rc<RtxContext>& ctx,
        DxvkBarrierSet& execBarriers,
        Rc<DxvkBuffer>& scratch
    ) {
      ScopedGpuProfileZone(ctx, "Remix API Build BLASes");
      if (sceneState.blases.dirty.empty()) {
        return;
      }

      const VkDeviceSize scratchAlignment = device.properties()
        .khrDeviceAccelerationStructureProperties
        .minAccelerationStructureScratchOffsetAlignment;
      const size_t dirtyBlasCount = sceneState.blases.dirty.entries.size();
      std::vector<VkAccelerationStructureGeometryKHR> geometries;
      std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildInfos;
      std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRanges;
      geometries.reserve(dirtyBlasCount);
      buildInfos.reserve(dirtyBlasCount);
      buildRanges.reserve(dirtyBlasCount);
      VkDeviceSize totalScratchSize = 0;

      for (const auto& dirty : sceneState.blases.dirty.entries) {
        BlasRecord* record = sceneState.blases.entries.lget(dirty.id);
        const MaterialRecord* material = record != nullptr
          ? sceneState.materials.entries.lget(record->material)
          : nullptr;
        if (record == nullptr || !record->ready || material == nullptr
            || !material->gpuMaterial.has_value()) {
          continue;
        }

        const RaytraceGeometry& input = record->geometry;
        geometries.emplace_back();
        VkAccelerationStructureGeometryKHR& geometry = geometries.back();
        {
          geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
          geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
          geometry.flags = material->gpuMaterial->getType() == RtSurfaceMaterialType::Opaque
            ? VK_GEOMETRY_OPAQUE_BIT_KHR
            : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        }
        auto& triangles = geometry.geometry.triangles;
        {
          triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
          triangles.vertexData.deviceAddress = input.positionBuffer.getDeviceAddress() + input.positionBuffer.offsetFromSlice();
          triangles.vertexStride = input.positionBuffer.stride();
          triangles.vertexFormat = input.positionBuffer.vertexFormat();
          triangles.maxVertex = input.vertexCount - 1;
          triangles.indexData.deviceAddress = input.usesIndices() ? input.indexBuffer.getDeviceAddress() : 0;
          triangles.indexType = input.usesIndices() ? input.indexBuffer.indexType() : VK_INDEX_TYPE_NONE_KHR;
        }

        buildInfos.emplace_back();
        VkAccelerationStructureBuildGeometryInfoKHR& buildInfo = buildInfos.back();
        {
          buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
          buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
          buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
          buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
          buildInfo.geometryCount = 1;
          buildInfo.pGeometries = &geometry;
        }

        const uint32_t primitiveCount = input.calculatePrimitiveCount();
        VkAccelerationStructureBuildSizesInfoKHR sizes {};
        {
          sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        }
        device.vkd()->vkGetAccelerationStructureBuildSizesKHR(
          device.handle(),
          VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
          &buildInfo,
          &primitiveCount,
          &sizes
        );

        const VkBufferUsageFlags asUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        DxvkBufferCreateInfo asInfo {};
        {
          asInfo.size = sizes.accelerationStructureSize;
          asInfo.usage = asUsage;
          asInfo.stages = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
          asInfo.access = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        }
        record->accelStructure = device.createAccelStructure(
          asInfo,
          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
          VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
          "Remix API BLAS"
        );
        buildInfo.dstAccelerationStructure =
          record->accelStructure->getAccelStructure();
        buildInfo.scratchData.deviceAddress = totalScratchSize;
        totalScratchSize += align(
          sizes.buildScratchSize + scratchAlignment,
          scratchAlignment
        );
        buildRanges.emplace_back();
        VkAccelerationStructureBuildRangeInfoKHR& range = buildRanges.back();
        {
          range.primitiveCount = primitiveCount;
          range.primitiveOffset = 0;
          range.firstVertex = 0;
          range.transformOffset = 0;
        }
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(
          record->accelStructure
        );
        ctx->getCommandList()->trackResource<DxvkAccess::Read>(
          input.positionBuffer.buffer()
        );
        auto prepareInput = [&](const RaytraceBuffer& buffer) {
          const Rc<DxvkBuffer>& resource = buffer.buffer();
          execBarriers.accessBuffer(
            resource->getSliceHandle(),
            resource->info().stages,
            resource->info().access,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_SHADER_READ_BIT
          );
        };
        prepareInput(input.positionBuffer);
        if (input.usesIndices()) {
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(
            input.indexBuffer.buffer()
          );
          prepareInput(input.indexBuffer);
        }
      }

      if (!buildInfos.empty()) {
        scratch = growBuffer(
          device,
          std::move(scratch),
          totalScratchSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          "Remix API AS Scratch",
          scratchAlignment
        );
        constexpr VkDeviceSize kScratchWarningSize = 32ull * 1024 * 1024;
        if (scratch->info().size > kScratchWarningSize) {
          ONCE(
            Logger::warn(
              str::format(
                "Remix API AS scratch buffer exceeds 32 MiB (",
                scratch->info().size / (1024 * 1024),
                " MiB)."
              )
            )
          );
        }

        const VkDeviceAddress scratchAddress =
          scratch->getDeviceAddress();
        assert(scratchAddress % scratchAlignment == 0);
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> pBuildRanges;
        pBuildRanges.reserve(buildRanges.size());
        for (size_t i = 0; i < buildInfos.size(); i++) {
          buildInfos[i].scratchData.deviceAddress += scratchAddress;
          pBuildRanges.push_back(&buildRanges[i]);
        }

        execBarriers.recordCommands(ctx->getCommandList());
        ctx->vkCmdBuildAccelerationStructuresKHR(
          uint32_t(buildInfos.size()),
          buildInfos.data(),
          pBuildRanges.data()
        );
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(
          scratch
        );
        ctx->emitMemoryBarrier(
          0,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
          VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
            | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        );
      }
      sceneState.blases.dirty.clear();
    }


    // Rewrites the TLAS instances of surfaces with TLAS changes.
    // When the opaque TLAS is rebuilt, it also refreshes the previous to current surface mapping of those surfaces.
    // A surface maps to itself only if it kept its slot generation and opaque BLAS since the previous rebuild.
    void applyTlasChanges(SceneState& sceneState, AccelerationState& acceleration) {
      ScopedCpuProfileZoneN("Remix API Compose TLAS Instances");
      if (!(sceneState.surfaces.dirty.allBits & SurfaceTlas)) {
        return;
      }
      for (const auto& dirty : sceneState.surfaces.dirty.entries) {
        if (dirty.bits & SurfaceTlas) {
          applyTlasSurface(sceneState, acceleration, dirty.id);
        }
      }
      if (acceleration.instances[Tlas::Opaque].rebuild) {
        std::vector<SurfaceId> mapping = std::move(acceleration.pendingMappings);
        for (const auto& dirty : sceneState.surfaces.dirty.entries) {
          if (dirty.bits & SurfaceTlas) {
            mapping.push_back(dirty.id);
          }
        }
        std::sort(
          mapping.begin(),
          mapping.end(),
          [](SurfaceId a, SurfaceId b) {
            return a.m_arrayIndex < b.m_arrayIndex;
          }
        );
        uint32_t previous = ~0u;
        for (SurfaceId id : mapping) {
          const uint32_t i = id.m_arrayIndex;
          if (i == previous) {
            continue;
          }
          previous = i;
          TlasSlot& slot = acceleration.slots[i];
          const uint32_t generation = slot.generation;
          const VkDeviceAddress opaqueBlas = generation != 0
              && slot.type == Tlas::Opaque
            ? acceleration.instances[Tlas::Opaque]
                .values[slot.index].accelerationStructureReference
            : 0;
          const bool hasPreviousSurface = generation != 0
              && generation == slot.previousGeneration
              && opaqueBlas != 0
              && opaqueBlas == slot.previousOpaqueBlas;
          const uint32_t value = hasPreviousSurface
            ? i : SURFACE_INDEX_INVALID;
          if (sceneState.surfaces.mappingShadow[i] != value) {
            sceneState.surfaces.mappingShadow[i] = value;
            sceneState.surfaces.mappingUploads.markSlot(i);
          }
          if (!hasPreviousSurface && opaqueBlas != 0) {
            acceleration.pendingMappings.push_back({ i, generation });
          }
          slot.previousGeneration = generation;
          slot.previousOpaqueBlas = opaqueBlas;
        }
      }
    }
    // Maps the surfaces that got a new slot or BLAS at the last opaque rebuild to themselves from the following frame on.
    void advancePendingMappings(SceneState& sceneState, AccelerationState& acceleration) {
      for (SurfaceId id : acceleration.pendingMappings) {
        if (id.m_arrayIndex >= acceleration.slots.size()) {
          continue;
        }

        const TlasSlot& slot = acceleration.slots[id.m_arrayIndex];
        const TlasInstances& opaque =
          acceleration.instances[Tlas::Opaque];
        const bool valid = sceneState.surfaces.entries.lget(id) != nullptr
            && slot.generation == id.m_generation
            && slot.type == Tlas::Opaque
            && slot.index < opaque.values.size()
            && slot.previousOpaqueBlas != 0
            && opaque.values[slot.index].accelerationStructureReference
                == slot.previousOpaqueBlas;
        const uint32_t value = valid
          ? id.m_arrayIndex : SURFACE_INDEX_INVALID;
        if (sceneState.surfaces.mappingShadow[id.m_arrayIndex] != value) {
          sceneState.surfaces.mappingShadow[id.m_arrayIndex] = value;
          sceneState.surfaces.mappingUploads.markSlot(id.m_arrayIndex);
        }
      }
      acceleration.pendingMappings.clear();
    }


    void applyMaterialChanges(
        SceneState& sceneState,
        SceneManager& sceneManager,
        RtxTextureManager& textureManager
    ) {
      if (sceneState.materials.dirty.empty()) {
        return;
      }
      ScopedCpuProfileZoneN("GPU Resident Scene Apply Material Changes");
      for (const auto& dirty : sceneState.materials.dirty.entries) {
        MaterialRecord* material = sceneState.materials.entries.lget(dirty.id);
        if (material != nullptr) {
          releaseMaterialTextures(textureManager, *material);
          material->gpuMaterial.reset();
          material->gpuSubsurface.reset();
          if (material->source.has_value()) {
            material->gpuMaterial.emplace(
              sceneManager.createRemixApiSurfaceMaterial(
                *material->source,
                dirty.id.m_arrayIndex,
                material->gpuSubsurface
              )
            );
            retainMaterialTextures(textureManager, *material);
          }
        }

        const PackedMaterial packed =
          material != nullptr && material->gpuMaterial.has_value()
            ? packMaterial(*material->gpuMaterial, dirty.id.m_arrayIndex)
            : PackedMaterial {};
        writeGpuSlot(sceneState.materials, dirty.id.m_arrayIndex, packed);

        const PackedMaterial extension =
          material != nullptr && material->gpuSubsurface.has_value()
            ? packMaterialExtension(*material->gpuSubsurface)
            : PackedMaterial {};
        std::memcpy(
          sceneState.materials.extensionShadow.data()
            + size_t(dirty.id.m_arrayIndex) * kSurfaceMaterialGPUSize,
          extension.data(),
          extension.size()
        );
        sceneState.materials.extensionUploads.markSlot(dirty.id.m_arrayIndex);
      }
    }


    void applySurfaceChanges(SceneState& sceneState, GpuResidentSceneNee& neeScene) {
      ScopedCpuProfileZoneN("GPU Resident Scene Apply Surface Changes");
      for (const auto& dirty : sceneState.surfaces.dirty.entries) {
        if (!(dirty.bits & SurfaceGpu)) {
          continue;
        }
        const SurfaceRecord* surface = sceneState.surfaces.entries.lget(dirty.id);
        const BlasRecord* blas = surface != nullptr
          ? sceneState.blases.entries.lget(surface->blas)
          : nullptr;
        const MaterialRecord* material = blas != nullptr
          ? sceneState.materials.entries.lget(blas->material)
          : nullptr;
        PackedSurface packedSurface {};
        const bool active = surface != nullptr && blas != nullptr
            && blas->accelStructure != nullptr && material != nullptr
            && material->gpuMaterial.has_value();
        neeScene.setPrimitiveCount(
          dirty.id.m_arrayIndex,
          active ? blas->geometry.calculatePrimitiveCount() : 0
        );
        if (active) {
          packedSurface = packSurface(
            *surface,
            *blas,
            *material,
            dirty.id.m_arrayIndex
          );
        }
        writeGpuSlot(
          sceneState.surfaces,
          dirty.id.m_arrayIndex,
          packedSurface
        );
      }
    }


    template<size_t SlotSize, size_t PageSize>
    void uploadPages(
        const Rc<RtxContext>& ctx,
        const Rc<DxvkBuffer>& buffer,
        const void* data,
        UploadPages<SlotSize, PageSize>& pages,
        size_t usedSlotCount
    ) {
      const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(data);
      pages.forEachRegion(
        usedSlotCount,
        [&](size_t offset, size_t size) {
          ctx->writeToBuffer(buffer, offset, size, bytes + offset);
        }
      );
      pages.clear();
    }
    void uploadSceneTables(
        DxvkDevice& device,
        SceneState& sceneState,
        Rc<DxvkBuffer>& emptyBuffer,
        const Rc<RtxContext>& ctx
    ) {
      ScopedGpuProfileZone(ctx, "Remix API Upload Scene Buffers");

      const bool initializeBuffers = emptyBuffer == nullptr;
      if (initializeBuffers) {
        constexpr VkBufferUsageFlags kShaderUsage =
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        emptyBuffer = createGpuBuffer(
          device,
          16,
          kShaderUsage,
          "Remix API Empty Scene Buffer"
        );
        ctx->clearBuffer(emptyBuffer, 0, emptyBuffer->info().size, 0);
      }

      uploadPages(
        ctx,
        sceneState.surfaces.gpuBuffer,
        sceneState.surfaces.shadow.data(),
        sceneState.surfaces.uploads,
        sceneState.surfaces.entries.capacity()
      );
      uploadPages(
        ctx,
        sceneState.materials.gpuBuffer,
        sceneState.materials.shadow.data(),
        sceneState.materials.uploads,
        sceneState.materials.entries.capacity()
      );
      uploadPages(
        ctx,
        sceneState.materials.extensionBuffer,
        sceneState.materials.extensionShadow.data(),
        sceneState.materials.extensionUploads,
        sceneState.materials.entries.capacity()
      );

      if (initializeBuffers) {
        ctx->writeToBuffer(
          sceneState.surfaces.mappingBuffer,
          0,
          sceneState.surfaces.mappingShadow.size() * sizeof(uint32_t),
          sceneState.surfaces.mappingShadow.data()
        );
        sceneState.surfaces.mappingUploads.clear();
      } else {
        uploadPages(
          ctx,
          sceneState.surfaces.mappingBuffer,
          sceneState.surfaces.mappingShadow.data(),
          sceneState.surfaces.mappingUploads,
          sceneState.surfaces.entries.capacity()
        );
      }

      sceneState.materials.dirty.clear();
      sceneState.surfaces.dirty.clear();
    }


    void buildDirtyTlas(
        DxvkDevice& device,
        AccelerationState& acceleration,
        const Rc<RtxContext>& ctx
    ) {
      ScopedGpuProfileZone(ctx, "Remix API Build TLASes");
      const bool noDirtyTlas = std::none_of(
        std::begin(acceleration.instances),
        std::end(acceleration.instances),
        [](const TlasInstances& instances) { return instances.rebuild; }
      );
      if (noDirtyTlas) {
        return;
      }
      for (size_t type = 0; type < Tlas::Count; type++) {
        TlasInstances& instances = acceleration.instances[type];
        if (!instances.rebuild) {
          continue;
        }
        uploadPages(
          ctx,
          instances.buffer,
          instances.values.data(),
          instances.uploads,
          instances.values.size()
        );
      }
      ctx->emitMemoryBarrier(
        0,
        VK_PIPELINE_STAGE_TRANSFER_BIT
          | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
          | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
          | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT
          | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
          | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
          | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
          | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        VK_ACCESS_SHADER_READ_BIT
          | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
          | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
      );

      TlasInstances& opaqueInstances = acceleration.instances[Tlas::Opaque];
      if (opaqueInstances.rebuild) {
        // The previous TLAS becomes the build destination, so its BLASes are retired through this command list.
        for (Rc<DxvkAccelStructure>& blas
            : acceleration.previousOpaqueBlasOwners) {
          acceleration.retiredBlasOwners.push_back(std::move(blas));
        }

        // The owner sets follow the TLAS swap below.
        acceleration.previousOpaqueBlasOwners =
          std::move(acceleration.currentOpaqueBlasOwners);
        acceleration.currentOpaqueBlasOwners = opaqueInstances.owners;
      }

      for (const TlasInstances& instances : acceleration.instances) {
        for (const Rc<DxvkAccelStructure>& blas : instances.owners) {
          ctx->getCommandList()->trackResource<DxvkAccess::Read>(blas);
        }
      }
      // The command list now keeps the retired owners alive until the GPU has finished with them.
      for (const Rc<DxvkAccelStructure>& blas : acceleration.retiredBlasOwners) {
        ctx->getCommandList()->trackResource<DxvkAccess::Read>(blas);
      }
      acceleration.retiredBlasOwners.clear();

      const VkDeviceSize scratchAlignment = device.properties()
        .khrDeviceAccelerationStructureProperties
        .minAccelerationStructureScratchOffsetAlignment;
      static constexpr const char* kTlasNames[Tlas::Count] = {
        "Remix API Opaque TLAS",
        "Remix API Unordered TLAS",
        "Remix API SSS TLAS",
      };

      std::array<VkAccelerationStructureBuildSizesInfoKHR, Tlas::Count>
        buildSizes {};
      std::array<VkDeviceSize, Tlas::Count> scratchOffsets {};
      VkDeviceSize totalScratchSize = 0;

      // Reserve a distinct scratch range for every TLAS build recorded by this command list.
      // Vulkan does not implicitly order acceleration structure builds,
      // so sharing one scratch address would create a write-after-write hazard between the opaque, unordered and SSS builds.
      for (size_t type = 0; type < Tlas::Count; type++) {
        const TlasInstances& instances = acceleration.instances[type];
        if (!instances.rebuild) {
          continue;
        }

        VkAccelerationStructureGeometryInstancesDataKHR instanceData {};
        {
          instanceData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
          instanceData.data.deviceAddress = instances.buffer->getDeviceAddress();
        }

        VkAccelerationStructureGeometryKHR geometry {};
        {
          geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
          geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
          geometry.geometry.instances = instanceData;
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo {};
        {
          buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
          buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
          buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
          buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
          buildInfo.geometryCount = 1;
          buildInfo.pGeometries = &geometry;
        }

        const uint32_t instanceCount = uint32_t(instances.values.size());
        VkAccelerationStructureBuildSizesInfoKHR& sizes = buildSizes[type];
        {
          sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        }
        device.vkd()->vkGetAccelerationStructureBuildSizesKHR(
          device.handle(),
          VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
          &buildInfo,
          &instanceCount,
          &sizes
        );

        scratchOffsets[type] = totalScratchSize;
        totalScratchSize += align(
          sizes.buildScratchSize + scratchAlignment,
          scratchAlignment
        );
      }

      acceleration.scratch = growBuffer(
        device,
        std::move(acceleration.scratch),
        totalScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        "Remix API AS Scratch",
        scratchAlignment
      );
      const VkDeviceAddress scratchAddress =
        acceleration.scratch->getDeviceAddress();
      assert(scratchAddress % scratchAlignment == 0);

      for (size_t type = 0; type < Tlas::Count; type++) {
        ScopedGpuProfileZone(ctx, "Remix API TLAS");
        TlasInstances& instances = acceleration.instances[type];
        if (!instances.rebuild) {
          continue;
        }

        VkAccelerationStructureGeometryInstancesDataKHR instanceData {};
        {
          instanceData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
          instanceData.data.deviceAddress = instances.buffer->getDeviceAddress();
        }

        VkAccelerationStructureGeometryKHR geometry {};
        {
          geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
          geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
          geometry.geometry.instances = instanceData;
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo {};
        {
          buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
          buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
          buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
          buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
          buildInfo.geometryCount = 1;
          buildInfo.pGeometries = &geometry;
        }

        const uint32_t instanceCount = uint32_t(instances.values.size());
        const VkAccelerationStructureBuildSizesInfoKHR& sizes =
          buildSizes[type];

        Tlas& tlas = acceleration.tlas[type];
        if (type == Tlas::Opaque) {
          std::swap(tlas.accelStructure, tlas.previousAccelStructure);
        }
        if (tlas.accelStructure == nullptr
            || tlas.accelStructure->info().size
              < sizes.accelerationStructureSize) {
          const VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
          const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
          const VkAccessFlags access = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
            | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
          DxvkBufferCreateInfo info {};
          {
            info.size = sizes.accelerationStructureSize;
            info.usage = usage;
            info.stages = stages;
            info.access = access;
          }
          tlas.accelStructure = device.createAccelStructure(
            info,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            kTlasNames[type]
          );
        }

        buildInfo.dstAccelerationStructure =
          tlas.accelStructure->getAccelStructure();
        buildInfo.scratchData.deviceAddress =
          scratchAddress + scratchOffsets[type];

        VkAccelerationStructureBuildRangeInfoKHR range {};
        {
          range.primitiveCount = instanceCount;
          range.primitiveOffset = 0;
          range.firstVertex = 0;
          range.transformOffset = 0;
        }
        const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
        ctx->vkCmdBuildAccelerationStructuresKHR(
          1,
          &buildInfo,
          &pRange
        );
        ctx->getCommandList()->trackResource<DxvkAccess::Read>(
          instances.buffer
        );
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(
          tlas.accelStructure
        );
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(
          acceleration.scratch
        );
        ctx->emitMemoryBarrier(
          0,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
          VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
          VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
            | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        );
        instances.rebuild = false;
      }
    }


    void prepareResources(GpuResidentScene::Data& data, SceneManager& sceneManager) {
      if (data.resourcesPrepared) {
        return;
      }

      SceneState& sceneState = data.sceneState;
      if (RtxOptionManager::isDrawcallTranslationInvalid()) {
        for (uint32_t i = 0; i < sceneState.materials.entries.capacity(); i++) {
          const MaterialId materialId = sceneState.materials.entries.liveIdAt(i);
          if (materialId) {
            sceneState.materials.dirty.mark(materialId);
          }
        }
      }

      auto& textureManager =
        data.device->getCommon()->getTextureManager();
      applyMaterialChanges(sceneState, sceneManager, textureManager);
      data.resourcesPrepared = true;
    }

  }  // namespace


  bool gpuResidentSceneSetCameraMediumMaterial(GpuResidentScene& scene, MaterialId id) {
    SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    if (!id) {
      const bool changed = bool(sceneState.materials.cameraMedium);
      sceneState.materials.cameraMedium = {};
      return changed;
    }
    const MaterialRecord* material = sceneState.materials.entries.lget(id);
    if (material == nullptr || !material->source.has_value()) {
      Logger::warn("SetCameraMediumMaterial: material handle not found");
      return false;
    }
    if (material->source->getType() != MaterialDataType::Translucent) {
      Logger::warn("SetCameraMediumMaterial: material must be translucent");
      return false;
    }
    const bool changed = sceneState.materials.cameraMedium != id;
    sceneState.materials.cameraMedium = id;
    return changed;
  }
  uint32_t gpuResidentSceneGetCameraMediumMaterialIndex(const GpuResidentScene& scene) {
    const SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return sceneState.materials.entries.lget(sceneState.materials.cameraMedium) != nullptr
      ? sceneState.materials.cameraMedium.m_arrayIndex
      : SURFACE_INDEX_INVALID;
  }


  // Shaders index the surface table by slot, so the count covers every slot, including empty ones.
  uint32_t gpuResidentSceneGetSurfaceCount(const GpuResidentScene& scene) {
    return scene.m_data->sceneState.surfaces.entries.capacity();
  }
  bool gpuResidentSceneHasRenderableSurfaces(const GpuResidentScene& scene) {
    const SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return std::any_of(
      std::begin(scene.m_data->acceleration.instances),
      std::end(scene.m_data->acceleration.instances),
      [](const TlasInstances& instances) { return !instances.values.empty(); }
    );
  }
  bool gpuResidentSceneHasPreviousScene(const GpuResidentScene& scene) {
    const SceneState& sceneState = scene.m_data->sceneState;
    std::lock_guard lock { sceneState.mutex };
    return scene.m_data->acceleration.tlas[Tlas::Opaque].previousAccelStructure != nullptr;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetSurfaceBuffer(const GpuResidentScene& scene) {
    return scene.m_data->sceneState.surfaces.gpuBuffer;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetMaterialBuffer(const GpuResidentScene& scene) {
    return scene.m_data->sceneState.materials.gpuBuffer;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetMaterialExtensionBuffer(const GpuResidentScene& scene) {
    return scene.m_data->sceneState.materials.extensionBuffer;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetSurfaceMappingBuffer(const GpuResidentScene& scene) {
    return scene.m_data->sceneState.surfaces.mappingBuffer;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetEmptyBuffer(const GpuResidentScene& scene) {
    return scene.m_data->emptyBuffer;
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetCurrentPrimitiveIDPrefixSumBuffer(const GpuResidentScene& scene) {
    return scene.m_data->neeScene.currentBuffer();
  }
  const Rc<DxvkBuffer>& gpuResidentSceneGetPreviousPrimitiveIDPrefixSumBuffer(const GpuResidentScene& scene) {
    return scene.m_data->neeScene.previousBuffer();
  }


  void gpuResidentScenePrepareResources(
      GpuResidentScene& scene,
      SceneManager& sceneManager
  ) {
    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    std::lock_guard lock { sceneState.mutex };
    prepareResources(data, sceneManager);
  }


  void gpuResidentSceneOnDestroy(GpuResidentScene& scene) {
    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    std::lock_guard lock { sceneState.mutex };
    SceneManager& sceneManager =
      data.device->getCommon()->getSceneManager();
    auto& textureManager = data.device->getCommon()->getTextureManager();
    finishFreeBatch(
      sceneState,
      *data.device,
      sceneManager,
      sceneState.frees.pending
    );
    for (FreeBatch& batch : sceneState.frees.batches) {
      finishFreeBatch(sceneState, *data.device, sceneManager, batch);
    }
    sceneState.frees.pending = {};
    sceneState.frees.batches.clear();
    for (uint32_t i = 0; i < sceneState.materials.entries.capacity(); i++) {
      MaterialRecord* material =
        sceneState.materials.entries.lget(sceneState.materials.entries.liveIdAt(i));
      if (material != nullptr) {
        releaseMaterialTextures(textureManager, *material);
        material->gpuMaterial.reset();
        material->gpuSubsurface.reset();
      }
    }
    for (uint32_t i = 0; i < sceneState.blases.entries.capacity(); i++) {
      BlasRecord* blas = sceneState.blases.entries.lget(sceneState.blases.entries.liveIdAt(i));
      if (blas != nullptr && blas->ready) {
        sceneManager.releaseRemixApiGeometry(blas->geometry);
        blas->ready = false;
      }
    }
    sceneState.surfaces.dirty.clear();
    sceneState.materials.dirty.clear();
    sceneState.blases.dirty.clear();
    sceneState.surfaces.frameLocal.clear();
    sceneState.surfaces.pendingHistory.clear();
    sceneState.surfaces.entries.clear();
    sceneState.materials.entries.clear();
    sceneState.blases.entries.clear();
    data.resourcesPrepared = false;
  }


  void gpuResidentScenePrepare(
      GpuResidentScene& scene,
      const Rc<RtxContext>& ctx,
      DxvkBarrierSet& execBarriers
  ) {
    ScopedGpuProfileZone(ctx, "GPU Resident Scene Prepare");

    GpuResidentScene::Data& data = *scene.m_data;
    SceneState& sceneState = data.sceneState;
    AccelerationState& acceleration = data.acceleration;

    std::lock_guard lock { sceneState.mutex };
    finishFrees(sceneState, *data.device, ctx->getSceneManager());

    // Callers that skipped prepareSceneResources before the texture upload get it here.
    if (!data.resourcesPrepared) {
      prepareResources(data, ctx->getSceneManager());
    }

    buildDirtyBlases(
      *data.device,
      sceneState,
      ctx,
      execBarriers,
      acceleration.scratch
    );
    advancePendingMappings(sceneState, acceleration);
    const bool rebuildTlas = std::any_of(
      std::begin(acceleration.instances),
      std::end(acceleration.instances),
      [](const TlasInstances& instances) { return instances.rebuild; }
    );
    if (sceneState.materials.dirty.empty()
        && sceneState.surfaces.dirty.empty()
        && !rebuildTlas
        && sceneState.surfaces.mappingUploads.empty()
        && data.emptyBuffer != nullptr) {
      data.neeScene.upload(ctx);
      submitFrees(sceneState, *data.device, ctx);
      acceleration.scratch = nullptr;
      data.resourcesPrepared = false;
      return;
    }

    applySurfaceChanges(sceneState, data.neeScene);
    data.neeScene.upload(ctx);
    applyTlasChanges(sceneState, acceleration);
    uploadSceneTables(*data.device, sceneState, data.emptyBuffer, ctx);
    buildDirtyTlas(*data.device, acceleration, ctx);

    for (size_t type = 0; type < Tlas::Count; type++) {
      ctx->getResourceManager().getTLAS(Tlas::Type(type)) =
        acceleration.tlas[type];
    }
    ctx->emitMemoryBarrier(
      0,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
      VK_ACCESS_SHADER_READ_BIT
    );
    submitFrees(sceneState, *data.device, ctx);
    acceleration.scratch = nullptr;
    data.resourcesPrepared = false;
  }

}  // namespace dxvk
