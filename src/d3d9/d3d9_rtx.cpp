#pragma once

#include "d3d9_include.h"
#include "d3d9_state.h"

#include "d3d9_util.h"
#include "d3d9_buffer.h"

#include "d3d9_rtx.h"
#include "d3d9_device.h"

#include "../util/util_fastops.h"
#include "../util/util_math.h"
#include "d3d9_rtx_utils.h"
#include "d3d9_texture.h"

namespace dxvk {
  static const bool s_isDxvkResolutionEnvVarSet = (env::getEnvVar("DXVK_RESOLUTION_WIDTH") != "") || (env::getEnvVar("DXVK_RESOLUTION_HEIGHT") != "");
  
  // We only look at RT 0 currently.
  const uint32_t kRenderTargetIndex = 0;

  #define CATEGORIES_REQUIRE_DRAW_CALL     InstanceCategories::Sky, InstanceCategories::Terrain
  #define CATEGORIES_REQUIRE_GEOMETRY_COPY InstanceCategories::Terrain, InstanceCategories::WorldUI

  D3D9Rtx::D3D9Rtx(D3D9DeviceEx* d3d9Device, bool enableDrawCallConversion)
    : m_rtStagingData(d3d9Device->GetDXVKDevice(), (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    , m_vertexCaptureData(d3d9Device->GetDXVKDevice(), 
                          (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), 
                          (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
                          (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT),
                          VK_ACCESS_TRANSFER_READ_BIT)
    , m_parent(d3d9Device)
    , m_enableDrawCallConversion(enableDrawCallConversion)
    , m_pGeometryWorkers(enableDrawCallConversion ? std::make_unique<GeometryProcessor>(popcnt_uint8(D3D9Rtx::kAllThreads), "geometry-processing") : nullptr) {

    // Add space for 256 objects skinned with 256 bones each.
    m_stagedBones.resize(256 * 256);
  }

  void D3D9Rtx::Initialize() {
    m_vsVertexCaptureData = m_parent->CreateConstantBuffer(false,
                                        sizeof(D3D9RtxVertexCaptureData),
                                        DxsoProgramType::VertexShader,
                                        DxsoConstantBuffers::VSVertexCaptureData);

    // Get constant buffer bindings from D3D9
    m_parent->EmitCs([vertexCaptureCB = m_vsVertexCaptureData](DxvkContext* ctx) {
      const uint32_t vsFixedFunctionConstants = computeResourceSlotId(DxsoProgramType::VertexShader, DxsoBindingType::ConstantBuffer, DxsoConstantBuffers::VSFixedFunction);
      const uint32_t psSharedStateConstants = computeResourceSlotId(DxsoProgramType::PixelShader, DxsoBindingType::ConstantBuffer, DxsoConstantBuffers::PSShared);
      static_cast<RtxContext*>(ctx)->setConstantBuffers(vsFixedFunctionConstants, psSharedStateConstants, vertexCaptureCB);
    });
  }

  const Direct3DState9& D3D9Rtx::d3d9State() const {
    return *m_parent->GetRawState();
  }

  template<typename T>
  void D3D9Rtx::copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex) {
    ScopedCpuProfileZone();

    assert(indexCount >= 3);

    // Find min/max index
    {
      ScopedCpuProfileZoneN("Find min/max");

      fast::findMinMax<T>(indexCount, pIndices, minIndex, maxIndex);
    }

    // Modify the indices if the min index is non-zero
    {
      ScopedCpuProfileZoneN("Copy indices");

      if (minIndex != 0) {
        fast::copySubtract<T>(pIndicesDst, pIndices, indexCount, (T) minIndex);
      } else {
        memcpy(pIndicesDst, pIndices, sizeof(T) * indexCount);
      }
    }
  }

  template<typename T>
  DxvkBufferSlice D3D9Rtx::processIndexBuffer(const uint32_t indexCount, const uint32_t startIndex, const DxvkBufferSliceHandle& indexSlice, uint32_t& minIndex, uint32_t& maxIndex) {
    ScopedCpuProfileZone();

    const uint32_t indexStride = sizeof(T);
    const size_t numIndexBytes = indexCount * indexStride;
    const size_t indexOffset = indexStride * startIndex;

    // Get our slice of the staging ring buffer
    const DxvkBufferSlice& stagingSlice = m_rtStagingData.alloc(CACHE_LINE_SIZE, numIndexBytes);

    // Acquire prevents the staging allocator from re-using this memory
    stagingSlice.buffer()->acquire(DxvkAccess::Read);

    const uint8_t* pBaseIndex = (uint8_t*) indexSlice.mapPtr + indexOffset;

    T* pIndices = (T*) pBaseIndex;
    T* pIndicesDst = (T*) stagingSlice.mapPtr(0);
    copyIndices<T>(indexCount, pIndicesDst, pIndices, minIndex, maxIndex);

    return stagingSlice;
  }

  void D3D9Rtx::prepareVertexCapture(const int vertexIndexOffset) {
    ScopedCpuProfileZone();

    struct CapturedVertex {
      Vector4 position;
      Vector4 texcoord0;
      Vector4 normal0;
    };

    auto BoundShaderHas = [&](const D3D9CommonShader* shader, DxsoUsage usage, bool inOut)-> bool {
      if (shader == nullptr)
        return false;

      const auto& sgn = inOut ? shader->GetIsgn() : shader->GetOsgn();
      for (uint32_t i = 0; i < sgn.elemCount; i++) {
        const auto& decl = sgn.elems[i];
        if (decl.semantic.usageIndex == 0 && decl.semantic.usage == usage)
          return true;
      }
      return false;
    };

    // Get common shaders to query what data we can capture
    const D3D9CommonShader* vertexShader = d3d9State().vertexShader.ptr() != nullptr ? d3d9State().vertexShader->GetCommonShader() : nullptr;

    RasterGeometry& geoData = m_activeDrawCallState.geometryData;

    // Known stride for vertex capture buffers
    const uint32_t stride = sizeof(CapturedVertex);
    const size_t vertexCaptureDataSize = align(geoData.vertexCount * stride, CACHE_LINE_SIZE);

    DxvkBufferSlice slice = m_vertexCaptureData.alloc(CACHE_LINE_SIZE, vertexCaptureDataSize);
    slice.buffer()->acquire(DxvkAccess::Write);

    geoData.positionBuffer = RasterBuffer(slice, 0, stride, VK_FORMAT_R32G32B32A32_SFLOAT);
    assert(geoData.positionBuffer.offset() % 4 == 0);

    // Did we have a texcoord buffer bound for this draw?  Note, we currently get texcoord from the vertex shader output 
    if (BoundShaderHas(vertexShader, DxsoUsage::Texcoord, false) && (!geoData.texcoordBuffer.defined() || !RtxGeometryUtils::isTexcoordFormatValid(geoData.texcoordBuffer.vertexFormat()))) {
      // Known offset for vertex capture buffers
      const uint32_t texcoordOffset = offsetof(CapturedVertex, texcoord0);
      geoData.texcoordBuffer = RasterBuffer(slice, texcoordOffset, stride, VK_FORMAT_R32G32_SFLOAT);
      assert(geoData.texcoordBuffer.offset() % 4 == 0);
    }

    // Check if we should/can get normals.  We don't see a lot of games sending normals to pixel shader, so we must capture from the IA output (or Vertex input)
    if (BoundShaderHas(vertexShader, DxsoUsage::Normal, true) && useVertexCapturedNormals()) {
      const uint32_t normalOffset = offsetof(CapturedVertex, normal0);
      geoData.normalBuffer = RasterBuffer(slice, normalOffset, stride, VK_FORMAT_R32G32B32_SFLOAT);
      assert(geoData.normalBuffer.offset() % 4 == 0);
    } else {
      geoData.normalBuffer = RasterBuffer();
    }

    auto constants = m_vsVertexCaptureData->allocSlice();

    // NOTE: May be better to move reverse transformation to end of frame, because this won't work if there hasnt been a FF draw this frame to scrape the matrix from...
    const Matrix4& ObjectToProjection = m_activeDrawCallState.transformData.viewToProjection * m_activeDrawCallState.transformData.worldToView * m_activeDrawCallState.transformData.objectToWorld;

    // Set constants required for vertex shader injection
    D3D9RtxVertexCaptureData& data = *(D3D9RtxVertexCaptureData*) constants.mapPtr;
    // Apply an inverse transform to get positions in object space (what renderer expects)
    data.projectionToWorld = inverse(ObjectToProjection);
    data.normalTransform = m_activeDrawCallState.transformData.objectToWorld;
    data.baseVertex = (uint32_t)std::max(0, vertexIndexOffset);

    m_parent->EmitCs([cVertexDataSlice = slice,
                      cConstantBuffer = m_vsVertexCaptureData,
                      cConstants = constants](DxvkContext* ctx) {
      // Bind the new constants to buffer
      ctx->invalidateBuffer(cConstantBuffer, cConstants);

      // Invalidate rest of the members
      // customWorldToProjection is not invalidated as its use is controlled by D3D9SpecConstantId::CustomVertexTransformEnabled being enabled
      ctx->bindResourceBuffer(getVertexCaptureBufferSlot(), cVertexDataSlice);
    });
  }

  void D3D9Rtx::processVertices(const VertexContext vertexContext[caps::MaxStreams], int vertexIndexOffset, RasterGeometry& geoData) {
    DxvkBufferSlice streamCopies[caps::MaxStreams] {};

    // Process vertex buffers from CPU
    for (const auto& element : d3d9State().vertexDecl->GetElements()) {
      // Get vertex context
      const VertexContext& ctx = vertexContext[element.Stream];

      if (ctx.mappedSlice.handle == VK_NULL_HANDLE)
        continue;

      ScopedCpuProfileZoneN("Process Vertices");
      const int32_t vertexOffset = ctx.offset + ctx.stride * vertexIndexOffset;
      const uint32_t numVertexBytes = ctx.stride * geoData.vertexCount;

      // Validating index data here, vertexCount and vertexIndexOffset accounts for the min/max indices
      if (RtxOptions::Get()->getValidateCPUIndexData()) {
        if (ctx.mappedSlice.length < vertexOffset + numVertexBytes) {
          throw DxvkError("Invalid draw call");
        }
      }

      // TODO: Simplify this by refactoring RasterGeometry to contain an array of RasterBuffer's
      RasterBuffer* targetBuffer = nullptr;
      switch (element.Usage) {
      case D3DDECLUSAGE_POSITIONT:
      case D3DDECLUSAGE_POSITION:
        if (element.UsageIndex == 0)
          targetBuffer = &geoData.positionBuffer;
        break;
      case D3DDECLUSAGE_BLENDWEIGHT:
        if (element.UsageIndex == 0)
          targetBuffer = &geoData.blendWeightBuffer;
        break;
      case D3DDECLUSAGE_BLENDINDICES:
        if (element.UsageIndex == 0)
          targetBuffer = &geoData.blendIndicesBuffer;
        break;
      case D3DDECLUSAGE_NORMAL:
        if (element.UsageIndex == 0)
          targetBuffer = &geoData.normalBuffer;
        break;
      case D3DDECLUSAGE_TEXCOORD:
        if (m_texcoordIndex <= MAXD3DDECLUSAGEINDEX && element.UsageIndex == m_texcoordIndex)
          targetBuffer = &geoData.texcoordBuffer;
        break;
      case D3DDECLUSAGE_COLOR:
        if (element.UsageIndex == 0 &&
            !lookupHash(RtxOptions::ignoreBakedLightingTextures(), m_activeDrawCallState.materialData.colorTextures[0].getImageHash())) {
          targetBuffer = &geoData.color0Buffer;
        }
        break;
      }

      if (targetBuffer != nullptr) {
        assert(!targetBuffer->defined());

        // Only do once for each stream
        if (!streamCopies[element.Stream].defined()) {
          // Deep clonning a buffer object is not cheap (320 bytes to copy and other work). Set a min-size threshold.
          const uint32_t kMinSizeToClone = 512;

          // Check if buffer is actualy a d3d9 orphan
          const bool isOrphan = !(ctx.buffer.getSliceHandle() == ctx.mappedSlice);
          const bool canUseBuffer = ctx.canUseBuffer && m_forceGeometryCopy == false;

          if (canUseBuffer && !isOrphan) {
            // Use the buffer directly if it is not an orphan
            if (ctx.pVBO != nullptr && ctx.pVBO->NeedsUpload())
              m_parent->FlushBuffer(ctx.pVBO);

            streamCopies[element.Stream] = ctx.buffer.subSlice(vertexOffset, numVertexBytes);
          } else if (canUseBuffer && numVertexBytes > kMinSizeToClone) {
            // Create a clone for the orphaned physical slice
            auto clone = ctx.buffer.buffer()->clone();
            clone->rename(ctx.mappedSlice);
            streamCopies[element.Stream] = DxvkBufferSlice(clone, ctx.buffer.offset() + vertexOffset, numVertexBytes);
          } else {
            streamCopies[element.Stream] = m_rtStagingData.alloc(CACHE_LINE_SIZE, numVertexBytes);

            // Acquire prevents the staging allocator from re-using this memory
            streamCopies[element.Stream].buffer()->acquire(DxvkAccess::Read);

            memcpy(streamCopies[element.Stream].mapPtr(0), (uint8_t*) ctx.mappedSlice.mapPtr + vertexOffset, numVertexBytes);
          }
        }

        *targetBuffer = RasterBuffer(streamCopies[element.Stream], element.Offset, ctx.stride, DecodeDecltype(D3DDECLTYPE(element.Type)));
        assert(targetBuffer->offset() % 4 == 0);
      }
    }
  }

  bool D3D9Rtx::processRenderState() {
    DrawCallTransforms& transformData = m_activeDrawCallState.transformData;

    // When games use vertex shaders, the object to world transforms can be unreliable, and so we can ignore them.
    const bool useObjectToWorldTransform = !m_parent->UseProgrammableVS() || (m_parent->UseProgrammableVS() && useVertexCapture() && useWorldMatricesForShaders());
    transformData.objectToWorld = useObjectToWorldTransform ? d3d9State().transforms[GetTransformIndex(D3DTS_WORLD)] : Matrix4();

    transformData.worldToView = d3d9State().transforms[GetTransformIndex(D3DTS_VIEW)];
    transformData.viewToProjection = d3d9State().transforms[GetTransformIndex(D3DTS_PROJECTION)];
    transformData.objectToView = transformData.worldToView * transformData.objectToWorld;

    // Some games pass invalid matrices which D3D9 apparently doesnt care about.
    // since we'll be doing inversions and other matrix operations, we need to 
    // sanitize those or there be nans.
    transformData.sanitize();

    if (m_flags.test(D3D9RtxFlag::DirtyClipPlanes)) {
      m_flags.clr(D3D9RtxFlag::DirtyClipPlanes);

      // Find one truly enabled clip plane because we don't support more than one
      transformData.enableClipPlane = false;
      if (d3d9State().renderStates[D3DRS_CLIPPLANEENABLE] != 0) {
        for (int i = 0; i < caps::MaxClipPlanes; ++i) {
          // Check the enable bit
          if ((d3d9State().renderStates[D3DRS_CLIPPLANEENABLE] & (1 << i)) == 0)
            continue;

          // Make sure that the plane equation is not degenerate
          const Vector4 plane = Vector4(d3d9State().clipPlanes[i].coeff);
          if (lengthSqr(plane.xyz()) > 0.f) {
            if (transformData.enableClipPlane) {
              ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Using more than 1 user clip plane is not supported.")));
              break;
            }

            transformData.enableClipPlane = true;
            transformData.clipPlane = plane;
          }
        }
      }
    }

    if (m_flags.test(D3D9RtxFlag::DirtyLights)) {
      m_flags.clr(D3D9RtxFlag::DirtyLights);

      std::vector<D3DLIGHT9> activeLightsRT;
      uint32_t lightIdx = 0;
      for (auto idx : d3d9State().enabledLightIndices) {
        if (idx == UINT32_MAX)
          continue;
        activeLightsRT.push_back(d3d9State().lights[idx].value());
      }

      m_parent->EmitCs([activeLightsRT, lightIdx](DxvkContext* ctx) {
          static_cast<RtxContext*>(ctx)->addLights(activeLightsRT.data(), activeLightsRT.size());
        });
    }

    // Stencil state is important to Remix
    m_activeDrawCallState.stencilEnabled = d3d9State().renderStates[D3DRS_STENCILENABLE];

    // Process textures
    if (m_parent->UseProgrammablePS()) {
      return processTextures<false>();
    } else {
      return processTextures<true>();
    }
  }

  D3D9Rtx::DrawCallType D3D9Rtx::makeDrawCallType(const DrawContext& drawContext) {
    // Track the drawcall index so we can use it in rtx_context
    m_activeDrawCallState.drawCallID = m_drawCallID++;

    if (m_drawCallID < (uint32_t)RtxOptions::Get()->getDrawCallRange().x ||
        m_drawCallID > (uint32_t)RtxOptions::Get()->getDrawCallRange().y) {
      return { RtxGeometryStatus::Ignored, false };
    }

    if (m_parent->UseProgrammableVS() && !useVertexCapture()) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipping draw call with shader usage as vertex capture is not enabled."));
      return { RtxGeometryStatus::Ignored, false };
    }

    if (drawContext.PrimitiveCount == 0) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, primitive count was 0."));
      return { RtxGeometryStatus::Ignored, false };
    }

    // Only certain draw calls are worth raytracing
    if (!isPrimitiveSupported(drawContext.PrimitiveType)) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to raytrace an unsupported primitive topology [", drawContext.PrimitiveType, "]. Ignoring.")));
      return { RtxGeometryStatus::Ignored, false };
    }

    if (!RtxOptions::Get()->isAlphaTestEnabled() && m_parent->IsAlphaTestEnabled()) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Raytracing an alpha-tested draw call when alpha-tested objects disabled in RT. Ignoring.")));
      return { RtxGeometryStatus::Ignored, false };
    }

    if (!RtxOptions::Get()->isAlphaBlendEnabled() && d3d9State().renderStates[D3DRS_ALPHABLENDENABLE]) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Raytracing an alpha-blended draw call when alpha-blended objects disabled in RT. Ignoring.")));
      return { RtxGeometryStatus::Ignored, false };
    }
    
    if (m_activeOcclusionQueries > 0) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to raytrace an occlusion query. Ignoring.")));
      return { RtxGeometryStatus::Rasterized, false };
    }

    if (d3d9State().renderTargets[kRenderTargetIndex] == nullptr) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, as no color render target bound."));
      return { RtxGeometryStatus::Ignored, false };
    }

    constexpr DWORD rgbWriteMask = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
    if ((d3d9State().renderStates[ColorWriteIndex(kRenderTargetIndex)] & rgbWriteMask) != rgbWriteMask) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, colour write disabled."));
      return { RtxGeometryStatus::Ignored, false };
    }

    // Ensure present parameters for the swapchain have been cached
    // Note: This assumes that ResetSwapChain has been called at some point before this call, typically done after creating a swapchain.
    assert(m_activePresentParams.has_value());

    // Attempt to detect shadow mask draws and ignore them
    // Conditions: non-textured flood-fill draws into a small quad render target
    if (((d3d9State().textureStages[0][D3DTSS_COLOROP] == D3DTOP_SELECTARG1 && d3d9State().textureStages[0][D3DTSS_COLORARG1] != D3DTA_TEXTURE) ||
         (d3d9State().textureStages[0][D3DTSS_COLOROP] == D3DTOP_SELECTARG2 && d3d9State().textureStages[0][D3DTSS_COLORARG2] != D3DTA_TEXTURE))) {
      const auto& rtExt = d3d9State().renderTargets[kRenderTargetIndex]->GetSurfaceExtent();
      // If rt is a quad at least 4 times smaller than backbuffer and the format is invalid format, then it is likely a shadow mask
      if (rtExt.width == rtExt.height && rtExt.width < m_activePresentParams->BackBufferWidth / 4 &&
          Resources::getFormatCompatibilityCategoryIndex(d3d9State().renderTargets[kRenderTargetIndex]->GetImageView(false)->imageInfo().format) == Resources::kInvalidFormatCompatibilityCategoryIndex) {
        ONCE(Logger::info("[RTX-Compatibility-Info] Skipped shadow mask drawcall."));
        return { RtxGeometryStatus::Ignored, false };
      }
    }

    if (!s_isDxvkResolutionEnvVarSet) {
      // NOTE: This can fail when setting DXVK_RESOLUTION_WIDTH or HEIGHT
      const bool isPrimary = isRenderTargetPrimary(*m_activePresentParams, d3d9State().renderTargets[kRenderTargetIndex]->GetCommonTexture()->Desc());

      if (!isPrimary) {
        ONCE(Logger::info("[RTX-Compatibility-Info] Found a draw call to a non-primary render target. Falling back to rasterization"));
        return { RtxGeometryStatus::Rasterized, false };
      }
    }

    // Detect stencil shadow draws and ignore them
    // Conditions: passingthrough stencil is enabled with increment or decrement z-fail action
    if (d3d9State().renderStates[D3DRS_STENCILENABLE] == TRUE &&
        d3d9State().renderStates[D3DRS_STENCILFUNC] == D3DCMP_ALWAYS &&
        (d3d9State().renderStates[D3DRS_STENCILZFAIL] == D3DSTENCILOP_DECR || d3d9State().renderStates[D3DRS_STENCILZFAIL] == D3DSTENCILOP_INCR ||
         d3d9State().renderStates[D3DRS_STENCILZFAIL] == D3DSTENCILOP_DECRSAT || d3d9State().renderStates[D3DRS_STENCILZFAIL] == D3DSTENCILOP_INCRSAT) &&
        d3d9State().renderStates[D3DRS_ZWRITEENABLE] == FALSE) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped stencil shadow drawcall."));
      return { RtxGeometryStatus::Ignored, false };
    }

    // Check UI only to the primary render target
    if (isRenderingUI()) {
      return {
        RtxGeometryStatus::Rasterized,
        true, // UI rendering detected => trigger RTX injection
      };
    }

    // TODO(REMIX-760): Support reverse engineering pre-transformed vertices
    if (d3d9State().vertexDecl != nullptr) {
      if (d3d9State().vertexDecl->TestFlag(D3D9VertexDeclFlag::HasPositionT)) {
        ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, using pre-transformed vertices which isn't currently supported."));
        return { RtxGeometryStatus::Rasterized, false };
      }
    }

    return { RtxGeometryStatus::RayTraced, false };
  }

  bool D3D9Rtx::checkBoundTextureCategory(const fast_unordered_set& textureCategory) const {
    const uint32_t usedSamplerMask = m_parent->m_psShaderMasks.samplerMask | m_parent->m_vsShaderMasks.samplerMask;
    const uint32_t usedTextureMask = m_parent->m_activeTextures & usedSamplerMask;
    for (uint32_t idx : bit::BitMask(usedTextureMask)) {
      if (!d3d9State().textures[idx])
        continue;

      auto texture = GetCommonTexture(d3d9State().textures[idx]);

      const XXH64_hash_t texHash = texture->GetSampleView(false)->image()->getHash();
      if (textureCategory.find(texHash) != textureCategory.end()) {
        return true;
      }
    }

    return false;
  }

  bool D3D9Rtx::isRenderingUI() {
    if (!m_parent->UseProgrammableVS() && orthographicIsUI()) {
      // Here we assume drawcalls with an orthographic projection are UI calls (as this pattern is common, and we can't raytrace these objects).
      const bool isOrthographic = (d3d9State().transforms[GetTransformIndex(D3DTS_PROJECTION)][3][3] == 1.0f);
      const bool zWriteEnabled = d3d9State().renderStates[D3DRS_ZWRITEENABLE];
      if (isOrthographic && !zWriteEnabled) {
        return true;
      }
    }

    // Check if UI texture bound
    return checkBoundTextureCategory(RtxOptions::uiTextures());
  }

  D3D9Rtx::PrepareDrawType D3D9Rtx::internalPrepareDraw(const IndexContext& indexContext, const VertexContext vertexContext[caps::MaxStreams], const DrawContext& drawContext) {
    ScopedCpuProfileZone();

    // RTX was injected => treat everything else as rasterized 
    if (m_rtxInjectTriggered) {
      return { !RtxOptions::Get()->skipDrawCallsPostRTXInjection(), false };
    }

    auto [status, triggerRtxInjection] = makeDrawCallType(drawContext);

    // When raytracing is enabled we want to completely remove the ignored drawcalls from further processing as early as possible
    const bool processIgnoredDraws = !RtxOptions::Get()->enableRaytracing();

    if (status == RtxGeometryStatus::Ignored) {
      return { processIgnoredDraws, false };
    }

    if (triggerRtxInjection) {
      // Bind all resources required for this drawcall to context first (i.e. render targets)
      m_parent->PrepareDraw(drawContext.PrimitiveType);

      triggerInjectRTX();

      m_rtxInjectTriggered = true;
      return { true, false };
    }

    if (status == RtxGeometryStatus::Rasterized) {
      return { true, false };
    }

    assert(status == RtxGeometryStatus::RayTraced);

    m_forceGeometryCopy = RtxOptions::Get()->useBuffersDirectly() == false;
    m_forceGeometryCopy |= m_parent->GetOptions()->allowDiscard == false;

    // The packet we'll send to RtxContext with information about geometry
    RasterGeometry& geoData = m_activeDrawCallState.geometryData;
    geoData = {};
    geoData.cullMode = DecodeCullMode(D3DCULL(d3d9State().renderStates[D3DRS_CULLMODE]));
    geoData.frontFace = VK_FRONT_FACE_CLOCKWISE;
    geoData.topology = DecodeInputAssemblyState(drawContext.PrimitiveType).primitiveTopology;

    // This can be negative!!
    int vertexIndexOffset = drawContext.BaseVertexIndex;

    // Process index buffer
    uint32_t minIndex = 0, maxIndex = 0;
    if (indexContext.indexType != VK_INDEX_TYPE_NONE_KHR) {
      geoData.indexCount = GetVertexCount(drawContext.PrimitiveType, drawContext.PrimitiveCount);

      if (indexContext.indexType == VK_INDEX_TYPE_UINT16)
        geoData.indexBuffer = RasterBuffer(processIndexBuffer<uint16_t>(geoData.indexCount, drawContext.StartIndex, indexContext.indexBuffer, minIndex, maxIndex), 0, 2, indexContext.indexType);
      else
        geoData.indexBuffer = RasterBuffer(processIndexBuffer<uint32_t>(geoData.indexCount, drawContext.StartIndex, indexContext.indexBuffer, minIndex, maxIndex), 0, 4, indexContext.indexType);

      // Unlikely, but invalid
      if (maxIndex == minIndex) {
        ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, no triangles detected in index buffer."));
        return { processIgnoredDraws, false };
      }

      geoData.vertexCount = maxIndex - minIndex + 1;
      vertexIndexOffset += minIndex;
    } else {
      geoData.vertexCount = GetVertexCount(drawContext.PrimitiveType, drawContext.PrimitiveCount);
    }

    if (geoData.vertexCount == 0) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, no vertices detected."));
      return { processIgnoredDraws, false };
    }

    m_activeDrawCallState.categories = 0;
    m_activeDrawCallState.materialData = {};

    // Fetch all the legacy state (colour modes, alpha test, etc...)
    setLegacyMaterialState(m_parent, m_parent->m_alphaSwizzleRTs & (1 << kRenderTargetIndex), m_activeDrawCallState.materialData);

    // Fetch fog state 
    setFogState(m_parent, m_activeDrawCallState.fogState);

    // Fetch all the render state and send it to rtx context (textures, transforms, etc.)
    if (!processRenderState()) {
      return { processIgnoredDraws, false };
    }

    // Max offseted index value within a buffer slice that geoData contains
    const uint32_t maxOffsetedIndex = maxIndex - minIndex;

    // Copy all the vertices into a staging buffer.  Assign fields of the geoData structure.
    processVertices(vertexContext, vertexIndexOffset, geoData);
    geoData.futureGeometryHashes = computeHash(geoData, maxOffsetedIndex);
    geoData.futureBoundingBox = computeAxisAlignedBoundingBox(geoData);
    
    // Process skinning data
    m_activeDrawCallState.futureSkinningData = processSkinning(geoData);

    // Hash material data
    m_activeDrawCallState.materialData.updateCachedHash();

    // For shader based drawcalls we also want to capture the vertex shader output
    const bool needVertexCapture = m_parent->UseProgrammableVS() && useVertexCapture();
    if (needVertexCapture) {
      prepareVertexCapture(vertexIndexOffset);
    }

    m_activeDrawCallState.usesVertexShader = m_parent->UseProgrammableVS();
    m_activeDrawCallState.usesPixelShader = m_parent->UseProgrammablePS();

    if (m_activeDrawCallState.usesVertexShader) {
      m_activeDrawCallState.programmableVertexShaderInfo = d3d9State().vertexShader->GetCommonShader()->GetInfo();
    }
    
    if (m_activeDrawCallState.usesPixelShader) {
      m_activeDrawCallState.programmablePixelShaderInfo = d3d9State().pixelShader->GetCommonShader()->GetInfo();
    }
    
    m_activeDrawCallState.cameraType = CameraType::Unknown;

    m_activeDrawCallState.minZ = std::clamp(d3d9State().viewport.MinZ, 0.0f, 1.0f);
    m_activeDrawCallState.maxZ = std::clamp(d3d9State().viewport.MaxZ, 0.0f, 1.0f);

    m_activeDrawCallState.zWriteEnable = d3d9State().renderStates[D3DRS_ZWRITEENABLE];
    m_activeDrawCallState.alphaBlendEnable = d3d9State().renderStates[D3DRS_ALPHABLENDENABLE];
    m_activeDrawCallState.zEnable = d3d9State().renderStates[D3DRS_ZENABLE] == D3DZB_TRUE;
    
    // Now that the DrawCallState is complete, we can use heuristics for detection
    m_activeDrawCallState.setupCategoriesForHeuristics();

    // Note: when skybox geometries are defined, we don't know if we will or won't need the draw call ahead of time (requires camera data)
    const bool preserveOriginalDraw =
      status == RtxGeometryStatus::Rasterized ||
      needVertexCapture ||
      !RtxOptions::skyBoxGeometries().empty() ||
      m_activeDrawCallState.testCategoryFlags(CATEGORIES_REQUIRE_DRAW_CALL);

    return { preserveOriginalDraw, true };
  }

  void D3D9Rtx::triggerInjectRTX() {
    // Flush any pending game and RTX work
    m_parent->Flush();

    // Send command to inject RTX
    m_parent->EmitCs([cReflexFrameId = GetReflexFrameId()](DxvkContext* ctx) {
      static_cast<RtxContext*>(ctx)->injectRTX(cReflexFrameId);
    });
  }

  void D3D9Rtx::CommitGeometryToRT(const DrawContext& drawContext) {
    ScopedCpuProfileZone();
    auto drawInfo = m_parent->GenerateDrawInfo(drawContext.PrimitiveType, drawContext.PrimitiveCount, m_parent->GetInstanceCount());

    DrawParameters params;
    params.instanceCount = drawInfo.instanceCount;
    params.vertexOffset = drawContext.BaseVertexIndex;
    params.firstIndex = drawContext.StartIndex;
    // DXVK overloads the vertexCount/indexCount in DrawInfo
    if (drawContext.Indexed) {
      params.indexCount = drawInfo.vertexCount; 
    } else {
      params.vertexCount = drawInfo.vertexCount;
    }

    submitActiveDrawCallState();

    m_parent->EmitCs([params, this](DxvkContext* ctx) {
      assert(dynamic_cast<RtxContext*>(ctx));
      DrawCallState drawCallState;
      if (m_drawCallStateQueue.pop(drawCallState)) {
        static_cast<RtxContext*>(ctx)->commitGeometryToRT(params, drawCallState);
      }
    });
  }

  void D3D9Rtx::submitActiveDrawCallState() {
    // We must be prepared for `push` failing here, this can happen, since we're pushing to a circular buffer, which 
    //  may not have room for new entries.  In such cases, we trust that the consumer thread will make space for us, and
    //  so we may just need to wait a little bit.
    while (!m_drawCallStateQueue.push(std::move(m_activeDrawCallState))) {
      Sleep(0);
    }
  }

  Future<SkinningData> D3D9Rtx::processSkinning(const RasterGeometry& geoData) {
    ScopedCpuProfileZone();

    static const auto kEmptySkinningFuture = Future<SkinningData>();

    if (m_parent->UseProgrammableVS()) {
      return kEmptySkinningFuture;
    }

    // Some games set vertex blend without enough data to actually do the blending, handle that logic below.

    const bool hasBlendWeight = d3d9State().vertexDecl != nullptr ? d3d9State().vertexDecl->TestFlag(D3D9VertexDeclFlag::HasBlendWeight) : false;
    const bool hasBlendIndices = d3d9State().vertexDecl != nullptr ? d3d9State().vertexDecl->TestFlag(D3D9VertexDeclFlag::HasBlendIndices) : false;
    const bool indexedVertexBlend = hasBlendIndices && d3d9State().renderStates[D3DRS_INDEXEDVERTEXBLENDENABLE];

    if (d3d9State().renderStates[D3DRS_VERTEXBLEND] == D3DVBF_DISABLE) {
      return kEmptySkinningFuture;
    }

    if (d3d9State().renderStates[D3DRS_VERTEXBLEND] != D3DVBF_0WEIGHTS) {
      if (!hasBlendWeight) {
        return kEmptySkinningFuture;
      }
    } else if (!indexedVertexBlend) {
      return kEmptySkinningFuture;
    }

    // We actually have skinning data now, process it!

    uint32_t numBonesPerVertex = 0;
    switch (d3d9State().renderStates[D3DRS_VERTEXBLEND]) {
    case D3DVBF_0WEIGHTS: numBonesPerVertex = 1; break;
    case D3DVBF_1WEIGHTS: numBonesPerVertex = 2; break;
    case D3DVBF_2WEIGHTS: numBonesPerVertex = 3; break;
    case D3DVBF_3WEIGHTS: numBonesPerVertex = 4; break;
    }

    const uint32_t vertexCount = geoData.vertexCount;

    HashQuery blendIndices;
    // Analyze the vertex data and find the min and max bone indices used in this mesh.
    // The min index is used to detect a case when vertex blend is enabled but there is just one bone used in the mesh,
    // so we can drop the skinning pass. That is processed in RtxContext::commitGeometryToRT(...)
    if (indexedVertexBlend && geoData.blendIndicesBuffer.defined()) {
      auto& buffer = geoData.blendIndicesBuffer;

      blendIndices.pBase = (uint8_t*) buffer.mapPtr(buffer.offsetFromSlice());
      blendIndices.elementSize = imageFormatInfo(buffer.vertexFormat())->elementSize;
      blendIndices.stride = buffer.stride();
      blendIndices.size = blendIndices.stride * vertexCount;
      blendIndices.ref = buffer.buffer().ptr();

      // Acquire prevents the staging allocator from re-using this memory
      blendIndices.ref->acquire(DxvkAccess::Read);
      // Make sure we hold on to this reference while the hashing is in flight
      blendIndices.ref->incRef();
    } else {
      blendIndices.ref = nullptr;
    }

    // Copy bones up to the max bone we have registered so far.
    const uint32_t maxBone = m_maxBone > 0 ? m_maxBone : 255;
    const uint32_t startBoneTransform = GetTransformIndex(D3DTS_WORLDMATRIX(0));

    if (m_stagedBonesCount + maxBone >= m_stagedBones.size()) {
      throw DxvkError("Bones temp storage is too small.");
    }

    Matrix4* boneMatrices = m_stagedBones.data() + m_stagedBonesCount;
    memcpy(boneMatrices, d3d9State().transforms.data() + startBoneTransform, sizeof(Matrix4)*(maxBone + 1));
    m_stagedBonesCount += maxBone + 1;

    return m_pGeometryWorkers->Schedule([boneMatrices, blendIndices, numBonesPerVertex, vertexCount]()->SkinningData {
      ScopedCpuProfileZone();
      uint32_t numBones = numBonesPerVertex;

      int minBoneIndex = 0;
      if (blendIndices.ref) {
        const uint8_t* pBlendIndices = blendIndices.pBase;
        // Find out how many bone indices are specified for each vertex.
        // This is needed to find out the min bone index and ignore the padding zeroes.
        int maxBoneIndex = -1;
        if (!getMinMaxBoneIndices(pBlendIndices, blendIndices.stride, vertexCount, numBonesPerVertex, minBoneIndex, maxBoneIndex)) {
          minBoneIndex = 0;
          maxBoneIndex = 0;
        }
        numBones = maxBoneIndex + 1;

        // Release this memory back to the staging allocator
        blendIndices.ref->release(DxvkAccess::Read);
        blendIndices.ref->decRef();
      }

      // Pass bone data to RT back-end

      SkinningData skinningData;
      skinningData.pBoneMatrices.reserve(numBones);

      for (uint32_t n = 0; n < numBones; n++) {
        skinningData.pBoneMatrices.push_back(boneMatrices[n]);
      }

      skinningData.minBoneIndex = minBoneIndex;
      skinningData.numBones = numBones;
      skinningData.numBonesPerVertex = numBonesPerVertex;
      skinningData.computeHash(); // Computes the hash and stores it in the skinningData itself

      return skinningData;
    });
  }

  template<bool FixedFunction>
  bool D3D9Rtx::processTextures() {
    // We don't support full legacy materials in fixed function mode yet..
    // This implementation finds the most relevant textures bound from the
    // following criteria:
    //   - Texture actually bound (and used) by stage
    //   - First N textures bound to a specific texcoord index
    //   - Prefer lowest texcoord index
    // In non-fixed function (shaders), take the first N textures.

    // Used args for a given operation.
    auto ArgsMask = [](DWORD Op) {
      switch (Op) {
      case D3DTOP_DISABLE:
        return 0b000u; // No Args
      case D3DTOP_SELECTARG1:
      case D3DTOP_PREMODULATE:
        return 0b010u; // Arg 1
      case D3DTOP_SELECTARG2:
        return 0b100u; // Arg 2
      case D3DTOP_MULTIPLYADD:
      case D3DTOP_LERP:
        return 0b111u; // Arg 0, 1, 2
      default:
        return 0b110u; // Arg 1, 2
      }
    };

    // Currently we only support 2 textures
    constexpr uint32_t NumTexcoordBins = FixedFunction ? (D3DDP_MAXTEXCOORD * LegacyMaterialData::kMaxSupportedTextures) : LegacyMaterialData::kMaxSupportedTextures;

    bool useStageTextureFactorBlending = true;
    bool useMultipleStageTextureFactorBlending = false;

    // Build a mapping of texcoord indices to stage
    const uint8_t kInvalidStage = 0xFF;
    uint8_t texcoordIndexToStage[NumTexcoordBins];
    if constexpr (FixedFunction) {
      memset(&texcoordIndexToStage[0], kInvalidStage, sizeof(texcoordIndexToStage));
      for (uint32_t stage = 0; stage < caps::TextureStageCount; stage++) {
        auto isTextureFactorBlendingEnabled = [](const auto& textureStageStates) -> bool {
          const auto colorOp = textureStageStates[DXVK_TSS_COLOROP];
          const auto alphaOp = textureStageStates[DXVK_TSS_ALPHAOP];
          return (textureStageStates[DXVK_TSS_COLORARG1] == D3DTA_TFACTOR ||
                  textureStageStates[DXVK_TSS_COLORARG2] == D3DTA_TFACTOR ||
                  textureStageStates[DXVK_TSS_ALPHAARG1] == D3DTA_TFACTOR ||
                  textureStageStates[DXVK_TSS_ALPHAARG2] == D3DTA_TFACTOR) &&
                 (colorOp == D3DTOP_MODULATE || colorOp == D3DTOP_MODULATE2X || colorOp == D3DTOP_MODULATE4X ||
                  alphaOp == D3DTOP_MODULATE || alphaOp == D3DTOP_MODULATE2X || alphaOp == D3DTOP_MODULATE4X);
        };

        // Support texture factor blending besides the first stage. Currently, we only support 1 additional stage tFactor blending.
        // Note: If the tFactor is disabled for current texture (useStageTextureFactorBlending) then we should ignore the multiple stage tFactor blendings.
        bool isCurrentStageTextureFactorBlendingEnabled = false;
        if (useStageTextureFactorBlending &&
            RtxOptions::Get()->enableMultiStageTextureFactorBlending() &&
            stage != 0 &&
            isTextureFactorBlendingEnabled(d3d9State().textureStages[stage])) {
          isCurrentStageTextureFactorBlendingEnabled = true;
          useMultipleStageTextureFactorBlending = true;
        }

        if (d3d9State().textures[stage] == nullptr)
          continue;

        const auto& data = d3d9State().textureStages[stage];

        // Subsequent stages do not occur if this is true.
        if (data[DXVK_TSS_COLOROP] == D3DTOP_DISABLE)
          break;

        const uint32_t argsMask = ArgsMask(data[DXVK_TSS_COLOROP]) | ArgsMask(data[DXVK_TSS_ALPHAOP]);
        const uint32_t texMask = ((data[DXVK_TSS_COLORARG0] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) || ((data[DXVK_TSS_ALPHAARG0] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? 0b001 : 0
          | ((data[DXVK_TSS_COLORARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) || ((data[DXVK_TSS_ALPHAARG1] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? 0b010 : 0
          | ((data[DXVK_TSS_COLORARG2] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) || ((data[DXVK_TSS_ALPHAARG2] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? 0b100 : 0;

        // Is texture used?
        if ((argsMask & texMask) == 0)
          continue;

        D3D9CommonTexture* texture = GetCommonTexture(d3d9State().textures[stage]);

        // Remix can only handle 2D textures - no volumes.
        if (texture->GetType() != D3DRTYPE_TEXTURE && (!allowCubemaps() || texture->GetType() != D3DRTYPE_CUBETEXTURE)) {
          continue;
        }

        const XXH64_hash_t texHash = texture->GetSampleView(true)->image()->getHash();

        // Currently we only support regular textures, skip lightmaps.
        if (lookupHash(RtxOptions::lightmapTextures(), texHash)) {
          continue;
        }

        // Allow for two stage candidates per texcoord index
        const uint32_t texcoordIndex = data[DXVK_TSS_TEXCOORDINDEX] & 0b111;
        const uint32_t candidateIndex = texcoordIndex * LegacyMaterialData::kMaxSupportedTextures;
        const uint32_t subIndex = (texcoordIndexToStage[candidateIndex] == kInvalidStage) ? 0 : 1;

        // Don't override if candidate exists
        if (texcoordIndexToStage[candidateIndex + subIndex] == kInvalidStage)
          texcoordIndexToStage[candidateIndex + subIndex] = stage;

        // Check if texture factor blending is enabled for the first stage
        if (useStageTextureFactorBlending && stage == 0) {
          isCurrentStageTextureFactorBlendingEnabled = isTextureFactorBlendingEnabled(d3d9State().textureStages[stage]);
        }

        // Check if texture factor blending is enabled
        if (isCurrentStageTextureFactorBlendingEnabled &&
            lookupHash(RtxOptions::ignoreBakedLightingTextures(), texHash)) {
          useStageTextureFactorBlending = false;
          useMultipleStageTextureFactorBlending = false;
        }
      }
    }

    // Find the ideal textures for raytracing, initialize the data to invalid (out of range) to unbind unused textures
    uint32_t firstStage = 0;
    for (uint32_t idx = 0, textureID = 0; idx < NumTexcoordBins && textureID < LegacyMaterialData::kMaxSupportedTextures; idx++) {
      const uint8_t stage = FixedFunction ? texcoordIndexToStage[idx] : textureID;
      if (stage == kInvalidStage || d3d9State().textures[stage] == nullptr)
        continue;

      D3D9CommonTexture* pTexInfo = GetCommonTexture(d3d9State().textures[stage]);
      assert(pTexInfo != nullptr);

      // Send the texture stage state for first texture slot (or 0th stage if no texture)
      if (textureID == 0) {
        // ColorTexture2 is optional and currently only used as RayPortal material, the material type will be checked in the submitDrawState.
        // So we don't use it to check valid drawcall or not here.
        if (pTexInfo->GetImage()->getHash() == kEmptyHash) {
          ONCE(Logger::info("[RTX-Compatibility-Info] Texture 0 without valid hash detected, skipping drawcall."));
          return false;
        }

        if (FixedFunction) {
          firstStage = stage;
        }
      }

      D3D9SamplerKey key = m_parent->CreateSamplerKey(stage);
      XXH64_hash_t samplerHash = XXH3_64bits(&key, sizeof(key));

      Rc<DxvkSampler> sampler;
      auto samplerIt = m_samplerCache.find(samplerHash);
      if (samplerIt != m_samplerCache.end()) {
        sampler = samplerIt->second;
      } else {
        const auto samplerInfo = m_parent->DecodeSamplerKey(key);
        sampler = m_parent->GetDXVKDevice()->createSampler(samplerInfo);
        m_samplerCache.insert(std::make_pair(samplerHash, sampler));
      }

      // Cache the slot we want to bind
      const bool srgb = d3d9State().samplerStates[stage][D3DSAMP_SRGBTEXTURE] & 0x1;
      m_activeDrawCallState.materialData.colorTextures[textureID] = TextureRef(pTexInfo->GetSampleView(srgb));
      m_activeDrawCallState.materialData.samplers[textureID] = sampler;

      auto shaderSampler = RemapStateSamplerShader(stage);
      m_activeDrawCallState.materialData.colorTextureSlot[textureID] = computeResourceSlotId(shaderSampler.first, DxsoBindingType::Image, uint32_t(shaderSampler.second));

      ++textureID;
    }

    // Update the drawcall state with texture stage info
    setTextureStageState(d3d9State(), firstStage, useStageTextureFactorBlending, useMultipleStageTextureFactorBlending,
                         m_activeDrawCallState.materialData, m_activeDrawCallState.transformData);

    if (d3d9State().textures[firstStage]) {
      m_activeDrawCallState.setupCategoriesForTexture();

      // Check if an ignore texture is bound
      if (m_activeDrawCallState.getCategoryFlags().test(InstanceCategories::Ignore)) {
        return false;
      }

      if (!m_forceGeometryCopy && RtxOptions::alwaysCopyDecalGeometries()) {
        // Only poke decal hashes when option is enabled.
        m_forceGeometryCopy |= m_activeDrawCallState.testCategoryFlags(CATEGORIES_REQUIRE_GEOMETRY_COPY);
      }
    }

    m_texcoordIndex = d3d9State().textureStages[firstStage][DXVK_TSS_TEXCOORDINDEX];

    return true;
  }

  D3D9Rtx::PrepareDrawType D3D9Rtx::PrepareDrawGeometryForRT(const bool indexed, const DrawContext& context) {
    if (!RtxOptions::Get()->enableRaytracing() || !m_enableDrawCallConversion)
      return { true, false };

    m_parent->PrepareTextures();

    IndexContext indices;
    if (indexed) {
      D3D9CommonBuffer* ibo = GetCommonBuffer(d3d9State().indices);
      assert(ibo != nullptr);

      indices.indexBuffer = ibo->GetMappedSlice();
      indices.indexType = DecodeIndexType(ibo->Desc()->Format);
    }

    // Copy over the vertex buffers that are actually required
    VertexContext vertices[caps::MaxStreams];
    for (uint32_t i = 0; i < caps::MaxStreams; i++) {
      const auto& dx9Vbo = d3d9State().vertexBuffers[i];
      auto* vbo = GetCommonBuffer(dx9Vbo.vertexBuffer);
      if (vbo != nullptr) {
        vertices[i].stride = dx9Vbo.stride;
        vertices[i].offset = dx9Vbo.offset;
        vertices[i].buffer = vbo->GetBufferSlice<D3D9_COMMON_BUFFER_TYPE_MAPPING>();
        vertices[i].mappedSlice = vbo->GetMappedSlice();
        vertices[i].pVBO = vbo;

        // If staging upload has been enabled on a buffer then previous buffer lock:
        //   a) triggered a pipeline stall (overlapped mapped ranges, improper flags etc)
        //   b) does not have D3DLOCK_DONOTWAIT, or was in use at Map()
        // 
        // Buffers with staged uploads may have contents valid ONLY until next Map().
        // We must NOT use such buffer directly and have to always copy the contents.
        vertices[i].canUseBuffer = vbo->DoesStagingBufferUploads() == false;
      }
    }

    return internalPrepareDraw(indices, vertices, context);
  }

  D3D9Rtx::PrepareDrawType D3D9Rtx::PrepareDrawUPGeometryForRT(const bool indexed,
                                                               const D3D9BufferSlice& buffer,
                                                               const D3DFORMAT indexFormat,
                                                               const uint32_t indexSize,
                                                               const uint32_t indexOffset,
                                                               const uint32_t vertexSize,
                                                               const uint32_t vertexStride,
                                                               const DrawContext& drawContext) {
    if (!RtxOptions::Get()->enableRaytracing() || !m_enableDrawCallConversion)
      return { true, false };

    m_parent->PrepareTextures();

    // 'buffer' - contains vertex + index data (packed in that order)

    IndexContext indices;
    if (indexed) {
      indices.indexBuffer = buffer.slice.getSliceHandle(indexOffset, indexSize);
      indices.indexType = DecodeIndexType(static_cast<D3D9Format>(indexFormat));
    }

    VertexContext vertices[caps::MaxStreams];
    vertices[0].stride = vertexStride;
    vertices[0].offset = 0;
    vertices[0].buffer = buffer.slice.subSlice(0, vertexSize);
    vertices[0].mappedSlice = buffer.slice.getSliceHandle(0, vertexSize);
    vertices[0].canUseBuffer = true;

    return internalPrepareDraw(indices, vertices, drawContext);
  }

  void D3D9Rtx::ResetSwapChain(const D3DPRESENT_PARAMETERS& presentationParameters) {
    // Early out if the cached present parameters are not out of date

    if (m_activePresentParams.has_value()) {
      if (
        m_activePresentParams->BackBufferWidth == presentationParameters.BackBufferWidth &&
        m_activePresentParams->BackBufferHeight == presentationParameters.BackBufferHeight &&
        m_activePresentParams->BackBufferFormat == presentationParameters.BackBufferFormat &&
        m_activePresentParams->BackBufferCount == presentationParameters.BackBufferCount &&
        m_activePresentParams->MultiSampleType == presentationParameters.MultiSampleType &&
        m_activePresentParams->MultiSampleQuality == presentationParameters.MultiSampleQuality &&
        m_activePresentParams->SwapEffect == presentationParameters.SwapEffect &&
        m_activePresentParams->hDeviceWindow == presentationParameters.hDeviceWindow &&
        m_activePresentParams->Windowed == presentationParameters.Windowed &&
        m_activePresentParams->EnableAutoDepthStencil == presentationParameters.EnableAutoDepthStencil &&
        m_activePresentParams->AutoDepthStencilFormat == presentationParameters.AutoDepthStencilFormat &&
        m_activePresentParams->Flags == presentationParameters.Flags &&
        m_activePresentParams->FullScreen_RefreshRateInHz == presentationParameters.FullScreen_RefreshRateInHz &&
        m_activePresentParams->PresentationInterval == presentationParameters.PresentationInterval
      ) {
        return;
      }
    }

    // Cache the present parameters
    m_activePresentParams = presentationParameters;

    // Inform the backend about potential presenter update
    m_parent->EmitCs([cWidth = m_activePresentParams->BackBufferWidth,
                      cHeight = m_activePresentParams->BackBufferHeight](DxvkContext* ctx) {
      static_cast<RtxContext*>(ctx)->resetScreenResolution({ cWidth, cHeight , 1 });
    });
  }

  void D3D9Rtx::EndFrame(const Rc<DxvkImage>& targetImage, bool callInjectRtx) {
    const auto currentReflexFrameId = GetReflexFrameId();
    
    // Flush any pending game and RTX work
    m_parent->Flush();

    // Inform backend of end-frame
    m_parent->EmitCs([currentReflexFrameId, targetImage, callInjectRtx](DxvkContext* ctx) { 
      static_cast<RtxContext*>(ctx)->endFrame(currentReflexFrameId, targetImage, callInjectRtx); 
    });

    // Reset for the next frame
    m_rtxInjectTriggered = false;
    m_drawCallID = 0;

    m_stagedBonesCount = 0;
  }

  void D3D9Rtx::OnPresent(const Rc<DxvkImage>& targetImage) {
    // Inform backend of present
    m_parent->EmitCs([targetImage](DxvkContext* ctx) { static_cast<RtxContext*>(ctx)->onPresent(targetImage); });
  }
}
