#pragma once

#include "d3d9_state.h"
#include "../dxvk/dxvk_buffer.h"
#include "../util/util_threadpool.h"

#include <vector>
#include <optional>

namespace dxvk {
  struct D3D9BufferSlice;
  class DxvkDevice;

  enum class D3D9RtxFlag : uint32_t {
    DirtyLights,
    DirtyClipPlanes,
  };

  using D3D9RtxFlags = Flags<D3D9RtxFlag>;

  //This class handles all of the RTX operations that are required from the D3D9 side.
  struct D3D9Rtx {
    friend class ImGUI; // <-- we want to modify these values directly.

    D3D9Rtx(D3D9DeviceEx* d3d9Device, bool enableDrawCallConversion = true);

    RTX_OPTION("rtx", bool, orthographicIsUI, true, "When enabled, draw calls that are orthographic will be considered as UI.");
    RTX_OPTION("rtx", bool, allowCubemaps, false, "When enabled, cubemaps from the game are processed through Remix, but they may not render correctly.");
    RTX_OPTION("rtx", bool, useVertexCapture, true, "When enabled, injects code into the original vertex shader to capture final shaded vertex positions.  Is useful for games using simple vertex shaders, that still also set the fixed function transform matrices.");
    RTX_OPTION("rtx", bool, useVertexCapturedNormals, true, "When enabled, vertex normals are read from the input assembler and used in raytracing.  This doesn't always work as normals can be in any coordinate space, but can help sometimes.");
    RTX_OPTION("rtx", bool, useWorldMatricesForShaders, true, "When enabled, Remix will utilize the world matrices being passed from the game via D3D9 fixed function API, even when running with shaders.  Sometimes games pass these matrices and they are useful, however for some games they are very unreliable, and should be filtered out.  If you're seeing precision related issues with shader vertex capture, try disabling this setting.");

    // Copy of the parameters issued to D3D9 on DrawXXX
    struct DrawContext {
      D3DPRIMITIVETYPE PrimitiveType;
      INT              BaseVertexIndex;
      UINT             MinVertexIndex;
      UINT             NumVertices;
      UINT             StartIndex;
      UINT             PrimitiveCount;
      BOOL             Indexed;
    };
    static_assert(sizeof(DrawContext) == 28, "Please, recheck initializer usages if this changes.");

    /**
      * \brief: Initialize the D3D9 RTX interface
      */
    void Initialize();

    /**
      * \brief: Signal that an occlusion query has started for the current device
      */
    void BeginOcclusionQuery() {
      ++m_activeOcclusionQueries;
    }

    /**
      * \brief: Signal that an occlusion query has ended for the current device
      */
    void EndOcclusionQuery() {
      --m_activeOcclusionQueries;
      assert(m_activeOcclusionQueries >= 0);
    }

    /**
      * \brief: Signal that a parameter needs to be updated for RTX
      *
      * \param [in] flag: parameter that requires updating
      */
    void SetDirty(D3D9RtxFlag flag) {
      m_flags.set(flag);
    }

    struct PrepareDrawType {
      bool preserveOriginal;
      bool pendingCommit;
    };
    
    /**
      * \brief: Signal that a transform has updated
      *
      * \param [in] idx: index of transform
      */
    void SetTransformDirty(const uint32_t transformIdx) {
      if (transformIdx > GetTransformIndex(D3DTS_WORLD)) {
        m_maxBone = std::max(m_maxBone, transformIdx - GetTransformIndex(D3DTS_WORLD));
      }
    }

    /**
      * \brief: This function is responsible for preparing the geometry for rendering in Direct3D 9.
      *
      * \param [in] indexed: A boolean value indicating whether or not the geometry to be rendered is indexed.
      * \param [in] state : An object of type Direct3DState9 that contains the current state of the Direct3D pipeline.
      * \param [in] context : An object of type Draw that contains the context for the draw call.
      *
      * Returns false if this drawcall should be removed from further processing, returns true otherwise.
      */
    PrepareDrawType PrepareDrawGeometryForRT(const bool indexed, const DrawContext& context);

    /**
      * \brief: This function is responsible for preparing the geometry for rendering in Direct3D 9 
      *         when the vertex and index data is packed into a single buffer: ||VERTICES|INDICES||
      * 
      * \param [in] indexed: A boolean value indicating whether or not the geometry to be rendered is indexed.
      * \param [in] buffer : An object of type D3D9BufferSlice that contains the packed vertex and index data.
      * \param [in] indexFormat : The format of the indices in the buffer.
      * \param [in] indexOffset : The offset of the index data in bytes
      * \param [in] indexSize : The size of the index data in bytes.
      * \param [in] vertexSize : The size of the vertex data in bytes.
      * \param [in] vertexStride : The stride of the vertex data in bytes.
      * \param [in] drawContext : An object of type Draw that contains the context for the draw call.
      *
      * Returns false if this drawcall should be removed from further processing, returns true otherwise.
      */
    PrepareDrawType PrepareDrawUPGeometryForRT(const bool indexed,
                                               const D3D9BufferSlice& buffer,
                                               const D3DFORMAT indexFormat,
                                               const uint32_t indexSize,
                                               const uint32_t indexOffset,
                                               const uint32_t vertexSize,
                                               const uint32_t vertexStride,
                                               const DrawContext& context);

    /**
      * \brief: Sends the pending drawcall geometry/state for raytracing, if nothing pending, does nothing.
      *
      * \param [in] drawContext : An object of type Draw that contains the context for the draw call.
      */
    void CommitGeometryToRT(const DrawContext& drawContext);

    /**
      * \brief: Signal that a swapchain has been resized or reconfigured.
      * 
      * \param [in] presentationParameters: A reference to the D3D present params.
      */
    void ResetSwapChain(const D3DPRESENT_PARAMETERS& presentationParameters);

    /**
      * \brief: Signal that we've reached the end of the frame.
      */
    void EndFrame(const Rc<DxvkImage>& targetImage, bool callInjectRtx = true);

    /**
      * \brief: Signal that we're about to present the image.
      */
    void OnPresent(const Rc<DxvkImage>& targetImage);

    /**
      * \brief: Increments the Reflex frame ID. Should be called after presentation and only after every Reflex related marker
      * call for the current frame (this typically means other threads running in parallel will need to cache this value from the
      * frame they were dispatched on).
      */
    void IncrementReflexFrameId() {
      ++m_reflexFrameId;
    }

    /**
      * \brief: Gets the Reflex frame ID for the current frame on the main thread. This is incremented after each present.
      * Only intended for use with Reflex, other methods for getting a frame ID exist which may make more sense for other systems.
      */
    uint64_t GetReflexFrameId() const {
      return m_reflexFrameId;
    }

  private: 
    // Give threads specific tasks, to reduce the chance of 
    //  critical work being pre-empted.
    enum WorkerTasks : uint8_t {
      kSkinningThread = 1 << 0,

      kHashingThread0 = 1 << 1,
      kHashingThread1 = 1 << 2,
      kHashingThread2 = 1 << 3,

      kHashingThreads = (kHashingThread0 | kHashingThread1 | kHashingThread2),
      kAllThreads = (kHashingThreads | kSkinningThread)
    };

    inline static const uint32_t kMaxConcurrentDraws = 6 * 1024; // some games issuing >3000 draw calls per frame...  account for some consumer thread lag with x2
    using GeometryProcessor = WorkerThreadPool<kMaxConcurrentDraws>;
    const std::unique_ptr<GeometryProcessor> m_pGeometryWorkers;
    AtomicQueue<DrawCallState, kMaxConcurrentDraws> m_drawCallStateQueue;

    DrawCallState m_activeDrawCallState;

    DxvkStagingDataAlloc m_rtStagingData;
    DxvkStagingDataAlloc m_vertexCaptureData;
    D3D9DeviceEx* m_parent;

    std::optional<D3DPRESENT_PARAMETERS> m_activePresentParams;

    D3D9RtxFlags m_flags = 0xFFFFffff;

    uint32_t m_drawCallID = 0;
    // Note: A frame identifier the the main thread holds on to passed down into thread invocations such that
    // Reflex markers have a consistent ID despite executing in parallel (as typical methods of getting a frame ID
    // in DXVK depend on say when the submit thread's present happens which is unpredictable).
    uint64_t m_reflexFrameId = 0;

    std::vector<Matrix4> m_stagedBones;
    uint32_t m_stagedBonesCount = 0;
    uint32_t m_maxBone = 0;

    const bool m_enableDrawCallConversion;
    bool m_rtxInjectTriggered = false;
    bool m_forceGeometryCopy = false;
    DWORD m_texcoordIndex = 0;

    int m_activeOcclusionQueries = 0;

    Rc<DxvkBuffer> m_vsVertexCaptureData;

    fast_unordered_cache<Rc<DxvkSampler>> m_samplerCache;

    struct IndexContext {
      VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
      DxvkBufferSliceHandle indexBuffer;
    };

    struct VertexContext {
      uint32_t stride = 0;
      uint32_t offset = 0;
      DxvkBufferSlice buffer;
      DxvkBufferSliceHandle mappedSlice;
      D3D9CommonBuffer* pVBO = nullptr;
      bool canUseBuffer;
    };

    static bool isPrimitiveSupported(const D3DPRIMITIVETYPE PrimitiveType) {
      return (PrimitiveType == D3DPT_TRIANGLELIST || PrimitiveType == D3DPT_TRIANGLEFAN || PrimitiveType == D3DPT_TRIANGLESTRIP);
    }

    const Direct3DState9& d3d9State() const;

    template<typename T>
    static void copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex);

    template<typename T>
    DxvkBufferSlice processIndexBuffer(const uint32_t indexCount, const uint32_t startIndex, const DxvkBufferSliceHandle& indexSlice, uint32_t& minIndex, uint32_t& maxIndex);

    void prepareVertexCapture(const int vertexIndexOffset);

    void processVertices(const VertexContext vertexContext[caps::MaxStreams], int vertexIndexOffset, RasterGeometry& geoData);

    bool processRenderState();

    template<bool FixedFunction>
    bool processTextures();

    PrepareDrawType internalPrepareDraw(const IndexContext& indexContext, const VertexContext vertexContext[caps::MaxStreams], const DrawContext& drawContext);

    void triggerInjectRTX();


    struct DrawCallType {
      RtxGeometryStatus status;
      bool triggerRtxInjection;
    };
    DrawCallType makeDrawCallType(const DrawContext& drawContext);

    bool checkBoundTextureCategory(const fast_unordered_set& textureCategory) const;

    bool isRenderingUI();

    Future<SkinningData> processSkinning(const RasterGeometry& geoData);

    Future<AxisAlignedBoundingBox> computeAxisAlignedBoundingBox(const RasterGeometry& geoData);

    Future<GeometryHashes> computeHash(const RasterGeometry& geoData, const uint32_t maxIndexValue);

    void submitActiveDrawCallState();
  };
}
