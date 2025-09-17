/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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

// this gets included from other modules, so use full path to external --- ugly!
#include "../../../external/ngx_sdk_dldn/include/nvsdk_ngx.h"
#include "../../../external/ngx_sdk_dldn/include/nvsdk_ngx_defs_dlssd.h"
#include <memory>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_semaphore.h"

// run DLFG in graphics queue for debugging
// note that this incurs heavy CPU serialization and is not meant to be used in general
// it also causes waits on unsignaled semaphores for the first N frames (generally OK on Windows, but will cause VL errors)
#define __DLFG_USE_GRAPHICS_QUEUE 0
// Note: Currently Reflex without its Vulkan extension has no way of marking Vulkan queue submits as belonging to a specific frame, rather just using
// which present end markers it is between to associate with a given frame. This causes issues however when we mark the present on the DLFG thread
// as the DLFG thread may be quite a ways disconnected from where rendering work is being submitted such that occasionally 0 or 2 frames worth of
// work will fall in between the present markers here, which causes Reflex to generate long sleeps where it shouldn't, resulting in stutters.
// Additionally, this only really matters for the Present marker right now, the out-of-band Present marker can stay where it should be without causing issues.
// As such, until this Vulkan extension is used in our Reflex implementation the begin/end Presentation calls are moved from the DLFG thread to the submit
// thread as a hack when this workaround is enabled to ensure they are placed in a more suitable location that will always come after render queue submission.
// Do not disable this workaround without good reason to do so (e.g. implementing the Vulkan extension and testing to ensure no stutters exist).
#define __DLFG_REFLEX_WORKAROUND 1

// Note: Use __DLFG_QUEUE_INFO_CHECK to check for members on DxvkAdapterQueueInfos as it has
// a mix of optional and non-optional types and needs this special logic rather than simply
// checking if the queue family index is VK_QUEUE_FAMILY_IGNORED like was done originally.
#if __DLFG_USE_GRAPHICS_QUEUE
#define __DLFG_QUEUE graphics
// Note: Graphics queue family does not require a check, should always be present.
#define __DLFG_QUEUE_INFO_CHECK(x) (true)
#else
#define __DLFG_QUEUE present
#define __DLFG_QUEUE_INFO_CHECK(x) (x.present.has_value())
#endif

// Forward declarations from NGX library.
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace dxvk {
  class RtCamera;
  class DxvkDevice;
  class DxvkContext;

  class NGXDLSSContext;
  class NGXRayReconstructionContext;
  class NGXDLFGContext;
  class NGXContext final {
  public:
    explicit NGXContext(DxvkDevice* device);

    ~NGXContext() {
      shutdown();
    }

    NGXContext(const NGXContext&)                = delete;
    NGXContext(NGXContext&&) noexcept            = delete;
    NGXContext& operator=(const NGXContext&)     = delete;
    NGXContext& operator=(NGXContext&&) noexcept = delete;

    void shutdown();

    bool supportsDLSS() {
      return m_supportsDLSS;
    }

    bool supportsDLFG() {
      return m_supportsDLFG;
    }

    uint32_t dlfgMaxInterpolatedFrames() {
      return m_dlfgMaxInterpolatedFrames;
    }

    bool supportsRayReconstruction() {
      return m_supportsRayReconstruction;
    }

    const std::string& getDLFGNotSupportedReason() {
      return m_dlfgNotSupportedReason;
    }
    
    std::unique_ptr<NGXDLSSContext> createDLSSContext();
    std::unique_ptr<NGXRayReconstructionContext> createRayReconstructionContext();
    std::unique_ptr<NGXDLFGContext> createDLFGContext();
    
  private:
    bool initialize();

    DxvkDevice* m_device = nullptr;

    bool m_initialized = false;
    bool m_supportsDLSS = false;
    bool m_supportsDLFG = false;
    uint32_t m_dlfgMaxInterpolatedFrames = 0;
    bool m_supportsRayReconstruction = false;

    bool checkDLSSSupport(NVSDK_NGX_Parameter* params);
    void checkDLFGSupport(NVSDK_NGX_Parameter* params);

    std::string m_dlfgNotSupportedReason;
  };

  class NGXFeatureContext {
  public:
    NGXFeatureContext(const NGXFeatureContext&)                = delete;
    NGXFeatureContext(NGXFeatureContext&&) noexcept            = delete;
    NGXFeatureContext& operator=(const NGXFeatureContext&)     = delete;
    NGXFeatureContext& operator=(NGXFeatureContext&&) noexcept = delete;

    virtual ~NGXFeatureContext();
    virtual void releaseNGXFeature() = 0;

  protected:
    explicit NGXFeatureContext(DxvkDevice* device);

    DxvkDevice* m_device = nullptr;
    NVSDK_NGX_Parameter* m_parameters = nullptr;
  };

  class NGXDLSSContext final : public NGXFeatureContext {
  public:
    struct OptimalSettings {
      uint32_t optimalRenderSize[2];
      uint32_t minRenderSize[2];
      uint32_t maxRenderSize[2];
    };

    struct NGXBuffers {
      const Resources::Resource* pUnresolvedColor;
      const Resources::Resource* pResolvedColor;
      const Resources::Resource* pMotionVectors;
      const Resources::Resource* pDepth;
      const Resources::Resource* pExposure;
      const Resources::Resource* pBiasCurrentColorMask;
    };

    struct NGXSettings
    {
      bool resetAccumulation;
      bool antiGhost;
      float preExposure;
      float jitterOffset[2];
      float motionVectorScale[2];
    };

    // Query optimal DLSS settings for a given resolution and performance/quality profile.
    OptimalSettings queryOptimalSettings(const uint32_t displaySize[2], NVSDK_NGX_PerfQuality_Value perfQuality) const;

    // initialize DLSS context, throws exception on failure
    void initialize(
      Rc<DxvkContext> renderContext,
      uint32_t maxRenderSize[2],
      uint32_t displayOutSize[2],
      bool isContentHDR,
      bool depthInverted,
      bool autoExposure,
      bool sharpening,
      NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf);

    /** Release DLSS.
    */
    void releaseNGXFeature() override;

    /** Checks if DLSS is initialized.
    */
    bool isDLSSInitialized() const { return m_initialized && m_featureDLSS != nullptr; }

    /** Evaluate DLSS.
    */
    bool evaluateDLSS(Rc<DxvkContext> renderContext, const NGXBuffers& buffers, const NGXSettings& settings) const;

    void setWorldToViewMatrix(const Matrix4& worldToView) {
      m_worldToViewMatrix = worldToView;
    }

    void setViewToProjectionMatrix(const Matrix4& viewToProjection) {
      m_viewToProjectionMatrix = viewToProjection;
    }

  public:
    // note: ctor is public due to make_unique/unique_ptr, but not intended as public --- use NGXWrapper::createDLSSContext instead
    explicit NGXDLSSContext(DxvkDevice* device);
    ~NGXDLSSContext() override;

    NGXDLSSContext(const NGXDLSSContext&)                = delete;
    NGXDLSSContext(NGXDLSSContext&&) noexcept            = delete;
    NGXDLSSContext& operator=(const NGXDLSSContext&)     = delete;
    NGXDLSSContext& operator=(NGXDLSSContext&&) noexcept = delete;

  private:
    bool m_initialized = false;
    NVSDK_NGX_Handle* m_featureDLSS = nullptr;
    Matrix4 m_worldToViewMatrix;
    Matrix4 m_viewToProjectionMatrix;
  };

  class NGXRayReconstructionContext final : public NGXFeatureContext {
  public:
    struct QuerySettings {
      uint32_t optimalRenderSize[2];
      uint32_t minRenderSize[2];
      uint32_t maxRenderSize[2];
    };

    struct NGXBuffers {
      const Resources::Resource* pUnresolvedColor;
      const Resources::Resource* pResolvedColor;
      const Resources::Resource* pMotionVectors;
      const Resources::Resource* pDepth;
      const Resources::Resource* pDiffuseAlbedo;
      const Resources::Resource* pSpecularAlbedo;
      const Resources::Resource* pExposure;
      const Resources::Resource* pPosition;
      const Resources::Resource* pNormals;
      const Resources::Resource* pRoughness;
      const Resources::Resource* pBiasCurrentColorMask;
      const Resources::Resource* pHitDistance;
      const Resources::Resource* pInTransparencyLayer;
      const Resources::Resource* pDisocclusionMask;
    };

    struct NGXSettings {
      bool resetAccumulation;
      bool antiGhost;
      float preExposure;
      float jitterOffset[2];
      float motionVectorScale[2];
      bool autoExposure;
      float frameTimeMilliseconds;
    };

    // Query optimal DLSS-RR settings for a given resolution and performance/quality profile.
    QuerySettings queryOptimalSettings(const uint32_t displaySize[2], NVSDK_NGX_PerfQuality_Value perfQuality) const;

    // initialize DLSS context, throws exception on failure
    void initialize(
      Rc<DxvkContext> renderContext,
      uint32_t maxRenderSize[2],
      uint32_t displayOutSize[2],
      bool isContentHDR,
      bool depthInverted,
      bool autoExposure,
      bool sharpening,
      NVSDK_NGX_RayReconstruction_Hint_Render_Preset dlssdModel,
      NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf);

    /** Release DLSS-RR
    */
    void releaseNGXFeature() override;

    /** Checks if DLSS is initialized.
    */
    bool isRayReconstructionInitialized() const {
      return m_initialized && m_featureRayReconstruction != nullptr;
    }

    /** Evaluate DLSS-RR
    */
    bool evaluateRayReconstruction(Rc<DxvkContext> renderContext, const NGXBuffers& buffers, const NGXSettings& settings) const;

    void setWorldToViewMatrix(const Matrix4& worldToView) {
      m_worldToViewMatrix = worldToView;
    }

    void setViewToProjectionMatrix(const Matrix4& viewToProjection) {
      m_viewToProjectionMatrix = viewToProjection;
    }

  public:
    // note: ctor is public due to make_unique/unique_ptr, but not intended as public --- use NGXWrapper::createRayReconstructionContext instead
    explicit NGXRayReconstructionContext(DxvkDevice* device);
    ~NGXRayReconstructionContext() override;

    NGXRayReconstructionContext(const NGXRayReconstructionContext&)                = delete;
    NGXRayReconstructionContext(NGXRayReconstructionContext&&) noexcept            = delete;
    NGXRayReconstructionContext& operator=(const NGXRayReconstructionContext&)     = delete;
    NGXRayReconstructionContext& operator=(NGXRayReconstructionContext&&) noexcept = delete;

  private:
    bool m_initialized = false;
    NVSDK_NGX_Handle* m_featureRayReconstruction = nullptr;
    Matrix4 m_worldToViewMatrix;
    Matrix4 m_viewToProjectionMatrix;
  };

  class NGXDLFGContext final : public NGXFeatureContext {
  public:
    typedef enum {
      Failure,
      Success,
    } EvaluateResult;

    void initialize(
      Rc<DxvkContext> renderContext,
      VkCommandBuffer commandList,
      uint32_t displayOutSize[2],
      VkFormat outputFormat
      );

    // interpolates one frame
    // DLFG keeps copies of each real frame, so we only need to pass in the current frame here
    // the first kNumWarmUpFrames won't be interpolated so interpolatedOutput may not be valid, this function returns true if interpolation happened
    EvaluateResult evaluate(
      Rc<DxvkContext> renderContext,
      VkCommandBuffer clientCommandList,
      Rc<DxvkImageView> interpolatedOutput,
      Rc<DxvkImageView> compositedColorBuffer,
      Rc<DxvkImageView> motionVectors,
      Rc<DxvkImageView> depth,
      const RtCamera& camera,
      Vector2 motionVectorScale,
      uint32_t interpolatedFrameIndex,
      uint32_t interpolatedFrameCount,
      bool resetHistory);

    void releaseNGXFeature() override;

  public:
    // note: ctor is public due to make_unique/unique_ptr, but not intended as public --- use NGXWrapper::createDLFGContext instead
    explicit NGXDLFGContext(DxvkDevice* device);
    ~NGXDLFGContext() override;

    NGXDLFGContext(const NGXDLFGContext&)                = delete;
    NGXDLFGContext(NGXDLFGContext&&) noexcept            = delete;
    NGXDLFGContext& operator=(const NGXDLFGContext&)     = delete;
    NGXDLFGContext& operator=(NGXDLFGContext&&) noexcept = delete;

  private:
    NVSDK_NGX_Handle* m_feature = nullptr;
  };
}
