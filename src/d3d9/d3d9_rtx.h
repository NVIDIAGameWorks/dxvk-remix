#pragma once

#include "d3d9_state.h"
#include "../dxvk/dxvk_buffer.h"
#include "../util/util_threadpool.h"
#include <vector>

namespace dxvk {
  struct D3D9BufferSlice;
  class DxvkDevice;

  enum class D3D9RtxFlag : uint32_t {
    DirtyObjectTransform,
    DirtyCameraTransforms,
    DirtyLights,
    DirtyClipPlanes,
  };

  using D3D9RtxFlags = Flags<D3D9RtxFlag>;

  //This class handles all of the RTX operations that are required from the D3D9 side.
  struct D3D9Rtx {
    friend class ImGUI; // <-- we want to modify these values directly.

    D3D9Rtx(D3D9DeviceEx* d3d9Device);

    RTX_OPTION("rtx", bool, useVertexCapture, true, "When enabled, injects code into the original vertex shader to capture final shaded vertex positions.  Is useful for games using simple vertex shaders, that still also set the fixed function transform matrices.");
    RTX_OPTION("rtx", bool, useVertexCapturedNormals, true, "When enabled, vertex normals are read from the input assembler and used in raytracing.  This doesn't always work as normals can be in any coordinate space, but can help sometimes.");

    // Copy of the parameters issued to D3D9 on DrawXXX
    struct Draw {
      D3DPRIMITIVETYPE PrimitiveType;
      INT              BaseVertexIndex;
      UINT             MinVertexIndex;
      UINT             NumVertices;
      UINT             StartIndex;
      UINT             PrimitiveCount;
    };

    /**
      * \brief: Initialize the D3D9 RTX interface
      */
    void Initialize();

    /**
      * \brief: Signal that a parameter needs to be updated for RTX
      *
      * \param [in] flag: parameter that requires updating
      */
    void SetDirty(D3D9RtxFlag flag) {
      m_flags.set(flag);
    }

    /**
      * \brief: Signal that a transform has updated
      *
      * \param [in] idx: index of transform
      */
    void SetTransformDirty(const uint32_t transformIdx) {
      switch (transformIdx) {
      case GetTransformIndex(D3DTS_VIEW):
      case GetTransformIndex(D3DTS_PROJECTION):
        SetDirty(D3D9RtxFlag::DirtyCameraTransforms);
        break;
      case GetTransformIndex(D3DTS_WORLD):
        SetDirty(D3D9RtxFlag::DirtyObjectTransform);
        break;
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
    bool PrepareDrawGeometryForRT(const bool indexed, const Draw& context);

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
    bool PrepareDrawUPGeometryForRT(const bool indexed,
                                    const D3D9BufferSlice& buffer,
                                    const D3DFORMAT indexFormat,
                                    const uint32_t indexSize,
                                    const uint32_t indexOffset,
                                    const uint32_t vertexSize,
                                    const uint32_t vertexStride,
                                    const Draw& context);

    /**
      * \brief: Signal that a swapchain has been resized or reconfigured.
      * 
      * \param [in] presentationParameters: A reference to the D3D present params.
      */
    void ResetSwapChain(const D3DPRESENT_PARAMETERS& presentationParameters);

    /**
      * \brief: Signal that we've reached the end of the frame.
      */
    void EndFrame();

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
    WorkerThreadPool<4 * 1024> m_gpeWorkers;

    DxvkStagingDataAlloc m_rtStagingData;
    D3D9DeviceEx* m_parent;

    D3DPRESENT_PARAMETERS m_activePresentParams;

    D3D9RtxFlags m_flags;

    uint32_t m_drawCallID = 0;

    bool m_rtxInjectTriggered = false;

    Rc<DxvkBuffer> m_vsVertexCaptureData;

    struct IndexContext {
      VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
      DxvkBufferSliceHandle indexBuffer;
    };

    struct VertexContext {
      uint32_t stride = 0;
      uint32_t offset = 0;
      DxvkBufferSliceHandle buffer;
    };

    static bool isPrimitiveSupported(const D3DPRIMITIVETYPE PrimitiveType) {
      return (PrimitiveType == D3DPT_TRIANGLELIST || PrimitiveType == D3DPT_TRIANGLEFAN || PrimitiveType == D3DPT_TRIANGLESTRIP);
    }

    const Direct3DState9& d3d9State() const;

    template<typename T>
    static void copyIndices(const uint32_t indexCount, T* pIndicesDst, const T* pIndices, uint32_t& minIndex, uint32_t& maxIndex);

    template<typename T>
    DxvkBufferSlice processIndexBuffer(const uint32_t indexCount, const uint32_t startIndex, const DxvkBufferSliceHandle& indexSlice, uint32_t& minIndex, uint32_t& maxIndex);

    void prepareVertexCapture(RasterGeometry& geoData, const int vertexIndexOffset);

    void processVertices(const VertexContext vertexContext[caps::MaxStreams], int vertexIndexOffset, uint32_t idealTexcoordIndex, RasterGeometry& geoData);

    uint32_t processRenderState();

    template<bool FixedFunction>
    uint32_t processTextures();

    bool internalPrepareDraw(const IndexContext& indexContext, const VertexContext vertexContext[caps::MaxStreams], const Draw& drawContext);
    
    struct DrawCallType {
      RtxGeometryStatus status;
      bool triggerRtxInjection;
    };
    DrawCallType makeDrawCallType(const Draw& drawContext);

    bool isRenderingUI();

    std::shared_future<SkinningData> processSkinning(const RasterGeometry& geoData);

    std::shared_future<AxisAlignBoundingBox> computeAxisAlignedBoundingBox(const RasterGeometry& geoData);

    std::shared_future<GeometryHashes> computeHash(const RasterGeometry& geoData, const uint32_t maxIndexValue);
  };
}
