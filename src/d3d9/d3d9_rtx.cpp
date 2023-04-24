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

  D3D9Rtx::D3D9Rtx(D3D9DeviceEx* d3d9Device)
    : m_rtStagingData(d3d9Device->GetDXVKDevice(), (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    , m_parent(d3d9Device)
    , m_gpeWorkers(popcnt_uint8(D3D9Rtx::kAllThreads), "geometry-processing") { }

  void D3D9Rtx::Initialize() {
    // Get constant buffer bindings from D3D9
    m_parent->EmitCs([](DxvkContext* ctx) {
      const uint32_t vsFixedFunctionConstants = computeResourceSlotId(DxsoProgramType::VertexShader, DxsoBindingType::ConstantBuffer, DxsoConstantBuffers::VSFixedFunction);
      static_cast<RtxContext*>(ctx)->setConstantBuffers(vsFixedFunctionConstants);
    });
  }

  const Direct3DState9& D3D9Rtx::d3d9State() const {
    return *m_parent->GetRawState();
  }

  template<typename T>
  void D3D9Rtx::copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex) {
    ZoneScoped;

    assert(indexCount >= 3);

    // Find min/max index
    {
      ZoneScopedN("Find min/max");

      fast::findMinMax<T>(indexCount, pIndices, minIndex, maxIndex);
    }

    // Modify the indices if the min index is non-zero
    {
      ZoneScopedN("Copy indices");

      if (minIndex != 0) {
        fast::copySubtract<T>(pIndicesDst, pIndices, indexCount, (T) minIndex);
      } else {
        memcpy(pIndicesDst, pIndices, sizeof(T) * indexCount);
      }
    }
  }

  template<typename T>
  DxvkBufferSlice D3D9Rtx::processIndexBuffer(const uint32_t indexCount, const uint32_t startIndex, const DxvkBufferSliceHandle& indexSlice, uint32_t& minIndex, uint32_t& maxIndex) {
    ZoneScoped;

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

  void D3D9Rtx::prepareVertexCapture(const int vertexIndexOffset, const uint32_t vertexCount) {
    // struct {
    //  float4 position;
    //  float4 texcoord0;
    // }
    const size_t vertexCaptureDataSize = vertexCount * 2 * sizeof(Vector4);

    D3D9BufferSlice buf = m_parent->AllocTempBuffer<false, true>(vertexCaptureDataSize);

    // Fill in buffer view info
    DxvkBufferViewCreateInfo viewInfo;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.rangeOffset = 0;
    viewInfo.rangeLength = vertexCaptureDataSize;

    // Create underlying buffer view object
    Rc<DxvkBufferView> bufferView = m_parent->GetDXVKDevice()->createBufferView(buf.slice.buffer(), viewInfo);

    // Bind the latest projection to world matrix...
    // NOTE: May be better to move reverse transformation to end of frame, because this won't work if there hasnt been a FF draw this frame to scrape the matrix from...
    const Matrix4& ObjectToProjection = d3d9State().transforms[GetTransformIndex(D3DTS_PROJECTION)] 
                                      * d3d9State().transforms[GetTransformIndex(D3DTS_VIEW)] 
                                      * d3d9State().transforms[GetTransformIndex(D3DTS_WORLD)];
    const Matrix4& vkProjectionToObject = inverse(ObjectToProjection);

    m_parent->EmitCs([vkProjectionToObject,
                      vertexIndexOffset,
                      cBuffer = bufferView](DxvkContext* ctx) {
      RtxContext* rtxCtx = static_cast<RtxContext*>(ctx);
      const uint32_t bindingId = getVertexCaptureBufferSlot();
      rtxCtx->bindResourceView(bindingId, nullptr, cBuffer);
      rtxCtx->setVertexCaptureSlot(bindingId); // Might be overkill to pass this slot since it's essentially static... Might come in handy one day though.

      // Set constants required for vertex shader injection
      ctx->setPushConstantBank(DxvkPushConstantBank::D3D9);
      ctx->pushConstants(offsetof(D3D9RenderStateInfo, projectionToWorld), sizeof(Matrix4), &vkProjectionToObject);
      ctx->pushConstants(offsetof(D3D9RenderStateInfo, baseVertex), sizeof(int), &vertexIndexOffset);
    });
  }

  void D3D9Rtx::processVertices(const VertexContext vertexContext[caps::MaxStreams], int vertexIndexOffset, uint32_t idealTexcoordIndex, RasterGeometry& geoData) {
    // For shader based drawcalls we also want to capture the vertex shader output
    if (likely(m_parent->UseProgrammableVS() && RtxOptions::Get()->isVertexCaptureEnabled())) {
        prepareVertexCapture(vertexIndexOffset, geoData.vertexCount);
    }

    DxvkBufferSlice streamCopies[caps::MaxStreams] {};

    // Process vertex buffers from CPU
    for (const auto& element : d3d9State().vertexDecl->GetElements()) {
      // Get vertex context
      const VertexContext& ctx = vertexContext[element.Stream];

      if (ctx.buffer.handle == VK_NULL_HANDLE)
        continue;

      ZoneScopedN("Process Vertices");
      const int32_t vertexOffset = ctx.offset + ctx.stride * vertexIndexOffset;
      const uint32_t numVertexBytes = ctx.stride * geoData.vertexCount;

      // Validating index data here, vertexCount and vertexIndexOffset accounts for the min/max indices
      if (RtxOptions::Get()->getValidateCPUIndexData()) {
        if (ctx.buffer.length < vertexOffset + numVertexBytes) {
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
        if (idealTexcoordIndex <= MAXD3DDECLUSAGEINDEX && element.UsageIndex == idealTexcoordIndex)
          targetBuffer = &geoData.texcoordBuffer;
        break;
      case D3DDECLUSAGE_COLOR:
        if (element.UsageIndex == 0)
          targetBuffer = &geoData.color0Buffer;
        break;
      }

      if (targetBuffer != nullptr) {
        assert(!targetBuffer->defined());

        // Only do the copy once
        if (!streamCopies[element.Stream].defined()) {
          streamCopies[element.Stream] = m_rtStagingData.alloc(CACHE_LINE_SIZE, numVertexBytes);

          // Acquire prevents the staging allocator from re-using this memory
          streamCopies[element.Stream].buffer()->acquire(DxvkAccess::Read);

          memcpy(streamCopies[element.Stream].mapPtr(0), (uint8_t*) ctx.buffer.mapPtr + vertexOffset, numVertexBytes);
        }

        *targetBuffer = RasterBuffer(streamCopies[element.Stream], element.Offset, ctx.stride, DecodeDecltype(D3DDECLTYPE(element.Type)));
        assert(targetBuffer->offset() % 4 == 0);
      }
    }
  }

  uint32_t D3D9Rtx::processRenderState() {
    if (m_flags.test(D3D9RtxFlag::DirtyObjectTransform)) {
      m_flags.clr(D3D9RtxFlag::DirtyObjectTransform);

      m_parent->EmitCs([cObjectToWorld = d3d9State().transforms[GetTransformIndex(D3DTS_WORLD)]](DxvkContext* ctx) {
        static_cast<RtxContext*>(ctx)->setObjectTransform(cObjectToWorld);
      });
    }

    if (m_flags.test(D3D9RtxFlag::DirtyCameraTransforms)) {
      m_flags.clr(D3D9RtxFlag::DirtyCameraTransforms);

      m_parent->EmitCs([cWorldToView = d3d9State().transforms[GetTransformIndex(D3DTS_VIEW)],
                        cViewToProjection = d3d9State().transforms[GetTransformIndex(D3DTS_PROJECTION)]
                        ](DxvkContext* ctx) {
        static_cast<RtxContext*>(ctx)->setCameraTransforms(cWorldToView, cViewToProjection);
      });
    }

    if (m_flags.test(D3D9RtxFlag::DirtyLights)) {
      m_flags.clr(D3D9RtxFlag::DirtyLights);

      std::vector<D3DLIGHT9> activeLightsRT;
      uint32_t lightIdx = 0;
      for (uint32_t i = 0; i < caps::MaxEnabledLights; i++) {
        auto idx = d3d9State().enabledLightIndices[i];
        if (idx == UINT32_MAX)
          continue;
        activeLightsRT.push_back(d3d9State().lights[idx].value());
      }

      m_parent->EmitCs([activeLightsRT, lightIdx](DxvkContext* ctx) {
        static_cast<RtxContext*>(ctx)->addLights(activeLightsRT.data(), activeLightsRT.size());
        });
    }

    if (m_flags.test(D3D9RtxFlag::DirtyClipPlanes)) {
      m_flags.clr(D3D9RtxFlag::DirtyClipPlanes);

      std::vector<D3D9ClipPlane> planes(caps::MaxClipPlanes);
      for (uint32_t i = 0; i < caps::MaxClipPlanes; i++) {
        planes[i] = (d3d9State().renderStates[D3DRS_CLIPPLANEENABLE] & (1 << i)) ? d3d9State().clipPlanes[i] : D3D9ClipPlane();
      }

      m_parent->EmitCs([cMask = d3d9State().renderStates[D3DRS_CLIPPLANEENABLE],
                        cPlanes = planes](DxvkContext* ctx) {
        static_assert(caps::MaxClipPlanes == dxvk::MaxClipPlanes, "MaxClipPlanes values mismatch");
        static_cast<RtxContext*>(ctx)->setClipPlanes(cMask, (Vector4*) cPlanes.data());
      });
    }

    if (m_parent->UseProgrammablePS()) {
      return processTextures<false>();
    } else {
      return processTextures<true>();
    }
  }

  D3D9Rtx::DrawCallType D3D9Rtx::makeDrawCallType(const Draw& drawContext) {
    int currentDrawCallID = m_drawCallID++;
    if (currentDrawCallID < RtxOptions::Get()->getDrawCallRange().x || 
        currentDrawCallID > RtxOptions::Get()->getDrawCallRange().y) {
      return { RtxGeometryStatus::Ignored, false };
    }

    if (drawContext.PrimitiveCount == 0) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, primitive count was 0."));
      return { RtxGeometryStatus::Ignored, false };
    }

    // Only certain draw calls are worth raytracing
    if (!isPrimitiveSupported(drawContext.PrimitiveType)) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to raytrace an unsupported primitive topology [", drawContext.PrimitiveType, "]. Falling back to rasterization")));
      return { RtxGeometryStatus::Rasterized, false };
    }

    // We only look at RT 0 currently.
    const uint32_t kRenderTargetIndex = 0;

    if (d3d9State().renderTargets[kRenderTargetIndex] == nullptr) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, as no color render target bound."));
      return { RtxGeometryStatus::Ignored, false };
    }

    constexpr DWORD rgbWriteMask = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
    if ((d3d9State().renderStates[ColorWriteIndex(kRenderTargetIndex)] & rgbWriteMask) != rgbWriteMask) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped drawcall, colour write disabled."));
      return { RtxGeometryStatus::Ignored, false };
    }

    if (!s_isDxvkResolutionEnvVarSet) {
      // NOTE: This can fail when setting DXVK_RESOLUTION_WIDTH or HEIGHT
      bool isPrimary = isRenderTargetPrimary(m_activePresentParams, d3d9State().renderTargets[kRenderTargetIndex]->GetCommonTexture()->Desc());

      if (!isPrimary) {
        ONCE(Logger::info("[RTX-Compatibility-Info] Found a draw call to a non-primary render target. Falling back to rasterization"));
        return { RtxGeometryStatus::Rasterized, false };
      }
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

  bool D3D9Rtx::isRenderingUI() {
    // Here we assume drawcalls with an orthographic projection are UI calls (as this pattern is common, and we can't raytrace these objects).
    const bool isOrthographic = (d3d9State().transforms[GetTransformIndex(D3DTS_PROJECTION)][3][3] == 1.0f);
    const bool zWriteEnabled = d3d9State().renderStates[D3DRS_ZWRITEENABLE];
    if (isOrthographic && !zWriteEnabled) {
      return true;
    }

    // Check if UI texture bound
    for (uint32_t idx = 0; idx < caps::TextureStageCount; idx++) {
      // Textures must be contiguously bound
      if (d3d9State().textures[idx] == nullptr)
        break;

      auto texture = GetCommonTexture(d3d9State().textures[idx]);

      const XXH64_hash_t texHash = texture->GetSampleView(false)->image()->getHash();
      if (RtxOptions::Get()->isUiTexture(texHash)) {
        return true;
      }
    }

    return false;
  }

  void D3D9Rtx::internalPrepareDraw(const IndexContext& indexContext, const VertexContext vertexContext[caps::MaxStreams], const Draw& drawContext) {
    ZoneScoped;

    // RTX was injected => treat everything else as rasterized 
    if (m_rtxInjectTriggered) {
      return;
    }

    auto [status, triggerRtxInjection] = makeDrawCallType(drawContext);

    if (status == RtxGeometryStatus::Ignored) {
      return;
    }

    if (triggerRtxInjection) {
      m_parent->EmitCs([](DxvkContext* ctx) {
        static_cast<RtxContext*>(ctx)->injectRTX();
      });

      m_rtxInjectTriggered = true;
      return;
    }

    assert(status == RtxGeometryStatus::RayTraced || status == RtxGeometryStatus::Rasterized);

    // The packet we'll send to RtxContext with information about geometry
    RasterGeometry geoData;
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
        return;
      }

      geoData.vertexCount = maxIndex - minIndex + 1;
      vertexIndexOffset += minIndex;
    } else {
      geoData.vertexCount = GetVertexCount(drawContext.PrimitiveType, drawContext.PrimitiveCount);
    }

    if (geoData.vertexCount == 0) {
      ONCE(Logger::info("[RTX-Compatibility-Info] Skipped invalid drawcall, no vertices detected."));
      return;
    }

    // Fetch all the legacy state (colour modes, alpha test, etc...)
    const DxvkRtxLegacyState legacyState = createLegacyState(m_parent);

    // Fetch all the render state and send it to rtx context (textures, transforms, etc.)
    const uint32_t idealTexcoordIndex = processRenderState();

    // Copy all the vertices into a staging buffer.  Assign fields of the geoData structure.
    processVertices(vertexContext, vertexIndexOffset, idealTexcoordIndex, geoData);
    geoData.futureGeometryHashes = computeHash(geoData, (maxIndex - minIndex));
    std::shared_future<SkinningData> futureSkinningData = processSkinning(geoData);
    if (RtxOptions::Get()->calculateMeshBoundingBox()) {
      geoData.futureBoundingBox = computeAxisAlignedBoundingBox(geoData);
    }

    // Send it
    m_parent->EmitCs([geoData, futureSkinningData, legacyState, status,
                      cUseVS = m_parent->UseProgrammableVS(),
                      cUsePS = m_parent->UseProgrammablePS()](DxvkContext* ctx) {
      assert(dynamic_cast<RtxContext*>(ctx));
      RtxContext* rtxCtx = static_cast<RtxContext*>(ctx);

      rtxCtx->setShaderState(cUseVS, cUsePS);
      rtxCtx->setLegacyState(legacyState);
      rtxCtx->setGeometry(geoData, status);
      rtxCtx->setSkinningData(futureSkinningData);
    });
  }

  std::shared_future<SkinningData> D3D9Rtx::processSkinning(const RasterGeometry& geoData) {
    ZoneScoped;
    if (d3d9State().renderStates[D3DRS_VERTEXBLEND] != D3DVBF_DISABLE) {
      bool hasBlendIndices = d3d9State().vertexDecl != nullptr ? d3d9State().vertexDecl->TestFlag(D3D9VertexDeclFlag::HasBlendIndices) : false;
      bool indexedVertexBlend = hasBlendIndices && d3d9State().renderStates[D3DRS_INDEXEDVERTEXBLENDENABLE];

      uint32_t numBonesPerVertex = 0;
      switch (d3d9State().renderStates[D3DRS_VERTEXBLEND]) {
      case D3DVBF_0WEIGHTS: numBonesPerVertex = 1; break;
      case D3DVBF_1WEIGHTS: numBonesPerVertex = 2; break;
      case D3DVBF_2WEIGHTS: numBonesPerVertex = 3; break;
      case D3DVBF_3WEIGHTS: numBonesPerVertex = 4; break;
      }

      RasterBuffer blendIndices;
      // Analyze the vertex data and find the min and max bone indices used in this mesh.
      // The min index is used to detect a case when vertex blend is enabled but there is just one bone used in the mesh,
      // so we can drop the skinning pass. That is processed in RtxContext::commitGeometryToRT(...)
      if (indexedVertexBlend && geoData.blendIndicesBuffer.defined()) {
        blendIndices = geoData.blendIndicesBuffer;
        // Acquire prevents the staging allocator from re-using this memory
        blendIndices.buffer()->acquire(DxvkAccess::Read);
      }
      
      const uint32_t vertexCount = geoData.vertexCount;

      return m_gpeWorkers.Schedule([cBoneMatrices = d3d9State().transforms, blendIndices, numBonesPerVertex, vertexCount]() -> SkinningData {
        ZoneScoped;
        uint32_t numBones = numBonesPerVertex;

        int minBoneIndex = 0;
        if (blendIndices.defined()) {
          const uint8_t* pBlendIndices = (uint8_t*)blendIndices.mapPtr(blendIndices.offsetFromSlice());
          // Find out how many bone indices are specified for each vertex.
          // This is needed to find out the min bone index and ignore the padding zeroes.
          int maxBoneIndex = -1;
          if (!getMinMaxBoneIndices(pBlendIndices, blendIndices.stride(), vertexCount, numBonesPerVertex, minBoneIndex, maxBoneIndex)) {
            minBoneIndex = 0;
            maxBoneIndex = 0;
          }
          numBones = maxBoneIndex + 1;

          // Release this memory back to the staging allocator
          blendIndices.buffer()->release(DxvkAccess::Read);
        }

        // Pass bone data to RT back-end
        std::vector<Matrix4> bones;
        bones.resize(numBones);
        for (uint32_t i = 0; i < numBones; i++) {
          bones[i] = cBoneMatrices[GetTransformIndex(D3DTS_WORLDMATRIX(i))];
        }

        SkinningData skinningData;
        skinningData.pBoneMatrices = bones;
        skinningData.minBoneIndex = minBoneIndex;
        skinningData.numBones = numBones;
        skinningData.numBonesPerVertex = numBonesPerVertex;
        skinningData.computeHash(); // Computes the hash and stores it in the skinningData itself

        return skinningData;
      });
    }

    return std::shared_future<SkinningData>(); // empty future
  }

  template<bool FixedFunction>
  uint32_t D3D9Rtx::processTextures() {
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
    constexpr uint32_t kMaxTextureBindings = 2;
    constexpr uint32_t NumTexcoordBins = FixedFunction ? (D3DDP_MAXTEXCOORD * kMaxTextureBindings) : kMaxTextureBindings;

    // Build a mapping of texcoord indices to stage
    const uint8_t kInvalidStage = 0xFF;
    uint8_t texcoordIndexToStage[NumTexcoordBins];
    if constexpr (FixedFunction) {
      memset(&texcoordIndexToStage[0], kInvalidStage, sizeof(texcoordIndexToStage));

      for (uint32_t stage = 0; stage < caps::TextureStageCount; stage++) {
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

        // Remix can only handle 2D textures - no cubemaps or volumes.
        if (texture->GetType() != D3DRTYPE_TEXTURE)
          continue;
          
        const XXH64_hash_t texHash = texture->GetSampleView(true)->image()->getHash();

        // Currently we only support regular textures, skip lightmaps.
        if (RtxOptions::Get()->isLightmapTexture(texHash))
          continue;

        // Allow for two stage candidates per texcoord index
        const uint32_t texcoordIndex = data[DXVK_TSS_TEXCOORDINDEX] & 0b111;
        const uint32_t candidateIndex = texcoordIndex * kMaxTextureBindings;
        const uint32_t subIndex = (texcoordIndexToStage[candidateIndex] == kInvalidStage) ? 0 : 1;

        // Don't override if candidate exists
        if(texcoordIndexToStage[candidateIndex + subIndex] == kInvalidStage)
          texcoordIndexToStage[candidateIndex + subIndex] = stage;
      }
    }

    // Find the ideal textures for raytracing, initialize the data to invalid (out of range) to unbind unused textures
    uint32_t texSlotsForRT[kMaxTextureBindings];
    memset(&texSlotsForRT[0], 0xFFFFffff, sizeof(texSlotsForRT));

    uint32_t firstStage = 0;
    for (uint32_t idx = 0, textureID = 0; idx < NumTexcoordBins && textureID < kMaxTextureBindings; idx++) {
      const uint8_t stage = FixedFunction ? texcoordIndexToStage[idx] : textureID;
      if (stage == kInvalidStage || d3d9State().textures[stage] == nullptr)
        continue;

      // Send the texture stage state for first texture slot (or 0th stage if no texture)
      if (FixedFunction && textureID == 0)
        firstStage = stage;

      // Cache the slot we want to bind
      auto shaderSampler = RemapStateSamplerShader(stage);
      texSlotsForRT[textureID++] = computeResourceSlotId(shaderSampler.first, DxsoBindingType::Image, uint32_t(shaderSampler.second));
    }

    DxvkRtxTextureStageState texStageState = createTextureStageState(d3d9State(), firstStage);

    m_parent->EmitCs([texSlotsForRT, texStageState](DxvkContext* ctx) {
      static_cast<RtxContext*>(ctx)->setTextureStageState(texStageState);
      static_cast<RtxContext*>(ctx)->setTextureSlots(texSlotsForRT[0], texSlotsForRT[1]);
    });

    return FixedFunction ? texStageState.texcoordIndex : 0;
  }

  void D3D9Rtx::PrepareDrawGeometryForRT(const bool indexed, const Draw& context) {
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
        vertices[i].buffer = vbo->GetMappedSlice();
      }
    }

    return internalPrepareDraw(indices, vertices, context);
  }

  void D3D9Rtx::PrepareDrawUPGeometryForRT(const bool indexed, 
                                           const D3D9BufferSlice& buffer,
                                           const D3DFORMAT indexFormat,
                                           const uint32_t indexSize,
                                           const uint32_t indexOffset,
                                           const uint32_t vertexSize,
                                           const uint32_t vertexStride,
                                           const Draw& drawContext) {
    // 'buffer' - contains vertex + index data (packed in that order)

    IndexContext indices;
    if (indexed) {
      indices.indexBuffer = buffer.slice.getSliceHandle(indexOffset, indexSize);
      indices.indexType = DecodeIndexType(static_cast<D3D9Format>(indexFormat));
    }

    VertexContext vertices[caps::MaxStreams];
    vertices[0].stride = vertexStride;
    vertices[0].offset = 0;
    vertices[0].buffer = buffer.slice.getSliceHandle(0, vertexSize);

    return internalPrepareDraw(indices, vertices, drawContext);
  }

  void D3D9Rtx::ResetSwapChain(const D3DPRESENT_PARAMETERS& presentationParameters) {
    if (0 == memcmp(&m_activePresentParams, &presentationParameters,
                    sizeof(presentationParameters))) {
      return;
    }

    // Cache these
    m_activePresentParams = presentationParameters;

    // Inform the backend about potential presenter update
    m_parent->EmitCs([cWidth = m_activePresentParams.BackBufferWidth,
                      cHeight = m_activePresentParams.BackBufferHeight](DxvkContext* ctx) {
      static_cast<RtxContext*>(ctx)->resetScreenResolution({ cWidth, cHeight , 1 });
    });
  }

  void D3D9Rtx::EndFrame() {
    // Inform backend of end-frame
    m_parent->EmitCs([](DxvkContext* ctx) { static_cast<RtxContext*>(ctx)->endFrame(); });

    // Reset for the next frame
    m_rtxInjectTriggered = false;
    m_drawCallID = 0;
  }
}
