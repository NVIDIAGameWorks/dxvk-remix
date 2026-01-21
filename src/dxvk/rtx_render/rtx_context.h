/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_asset_exporter.h"
#include "rtx_camera_manager.h"
#include "rtx/pass/nrd_args.h"

#include <cstdint>
#include <chrono>
#include "rtx_options.h"

struct VolumeArgs;
struct RaytraceArgs;

namespace dxvk {
  class DxvkContext;
  class AssetExporter;
  class SceneManager;
  class TerrainBaker;
  struct ExternalDrawState;

  struct D3D9RtxVertexCaptureData;
  struct D3D9SharedPS;
  
  struct DrawParameters {
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    uint32_t vertexOffset = 0;
  };
  /** 
   * \brief RTX context
   * 
   * Tracks pipeline state and records command lists.
   * This is where the actual rendering commands are
   * recorded.
   */

  class RtxContext : public DxvkContext {

  public:
    
    RtxContext(const Rc<DxvkDevice>& device);
    ~RtxContext();

    float getGpuIdleTimeSinceLastCall();

    /**
      * \brief Reset screen resolution, and resize all screen
      *        buffers to specified resolution if required.
      * 
      * \param [in] upscaleExtent: New desired resolution.
      */
    void resetScreenResolution(const VkExtent3D& upscaleExtent);

    /**
      * \brief Triggers the RTX renderer.  Writes to targetImage (if specified) 
      *        and the currently bound render target if not.
      *        Will flush the scene and perform other non rendering tasks.
      * 
      * \param [in] cachedReflexFrameId: The Reflex frame ID at the time of calling, cached so Reflex can have
      * consistent frame IDs throughout the dispatches of an application frame.
      * \param [in] targetImage: Image to store raytraced result in
      */
    void injectRTX(std::uint64_t cachedReflexFrameId, Rc<DxvkImage> targetImage = nullptr);
    void endFrame(std::uint64_t cachedReflexFrameId, Rc<DxvkImage> targetImage = nullptr, bool callInjectRtx = true);

    void onPresent(Rc<DxvkImage> targetImage = nullptr);

    /**
      * \brief Set D3D9 specific constant buffers
      *
      * \param [in] vsFixedFunctionConstants: resource idx of the constant buffer for FF vertex shaders
      * \param [in] psFixedFunctionConstants: resource idx of the constant buffer for FF pixel shaders
      * \param [in] vertexCaptureCB: constant buffer for vertex capture
      */
    void setConstantBuffers(const uint32_t vsFixedFunctionConstants, const uint32_t psFixedFunctionConstants, Rc<DxvkBuffer> vertexCaptureCB);

    /**
      * \brief Adds a batch of lights to the scene context
      *
      * \param [in] pLights: array of light structures
      * \param [in] numLights: number of lights
      */
    void addLights(const D3DLIGHT9* pLights, const uint32_t numLights);

    void clearRenderTarget(const Rc<DxvkImageView>& imageView, VkImageAspectFlags clearAspects, VkClearValue clearValue);
    void clearImageView(const Rc<DxvkImageView>& imageView, VkOffset3D offset, VkExtent3D extent, VkImageAspectFlags aspect, VkClearValue value);

    void commitGeometryToRT(const DrawParameters& params, DrawCallState& drawCallState);
    void commitExternalGeometryToRT(ExternalDrawState&& state);

    static void blitImageHelper(Rc<DxvkContext> ctx, const Rc<DxvkImage>& srcImage, const Rc<DxvkImage>& dstImage, VkFilter filter);

    virtual void flushCommandList() override;

    SceneManager& getSceneManager();
    Resources& getResourceManager();
  
    static void triggerScreenshot() { s_triggerScreenshot = true; }
    static void triggerUsdCapture() { s_triggerUsdCapture = true; }

    void bindCommonRayTracingResources(const Resources::RaytracingOutput& rtOutput);

    void bindResourceView(const uint32_t slot, const Rc<DxvkImageView>& imageView, const Rc<DxvkBufferView>& bufferView);

    void getDenoiseArgs(NrdArgs& outPrimaryDirectNrdArgs, NrdArgs& outPrimaryIndirectNrdArgs, NrdArgs& outSecondaryNrdArgs);
    void updateRaytraceArgsConstantBuffer(Resources::RaytracingOutput& rtOutput, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    D3D9RtxVertexCaptureData& allocAndMapVertexCaptureConstantBuffer();
    D3D9FixedFunctionVS& allocAndMapFixedFunctionVSConstantBuffer();
    D3D9SharedPS& allocAndMapPSSharedStateConstantBuffer();

    static bool checkIsShaderExecutionReorderingSupported(DxvkDevice& device);

    const DxvkScInfo& getSpecConstantsInfo(VkPipelineBindPoint pipeline) const;
    void setSpecConstantsInfo(VkPipelineBindPoint pipeline, const DxvkScInfo& newSpecConstantInfo);

    bool useRayReconstruction() const;

#ifdef REMIX_DEVELOPMENT
    // Note: Cache image views for all resources that used by current frame, so we can do query for resource aliasing at the end of frame.
    //       This is automatically called when binding resources for passes, RtxContext::bindCommonRayTracingResources
    //       When we are not using the binding function in the passes such as DLSSRR, we need to manually cache the image views. Please reference the cache logic in DxvkRayReconstruction::dispatch
    void cacheResourceAliasingImageView(const Rc<DxvkImageView>& imageView);
#endif

    inline void setFramePassStage(const RtxFramePassStage currentFramePassStage) {
#ifdef REMIX_DEVELOPMENT
      m_currentPassStage = currentFramePassStage;
#endif
    }

  protected:
    virtual void updateComputeShaderResources() override;
    virtual void updateRaytracingShaderResources() override;

  private:
    // This enum is for internal use only.
    // There is a mode called UpscalerType in RtxOptions, but it doesn't contain DLSS-RR because RR is considered as a special mode of DLSS.
    enum class InternalUpscaler {
      None = 0,
      DLSS,
      NIS,
      TAAU,
      XeSS,
      FSR,
      DLSS_RR,
    };

    void reportCpuSimdSupport();

    void takeScreenshot(std::string imageName, Rc<DxvkImage> image);

    void checkOpacityMicromapSupport();
    void checkShaderExecutionReorderingSupport();
    void checkNeuralRadianceCacheSupport();

    VkExtent3D setDownscaleExtent(const VkExtent3D& upscaleExtent);

    VkExtent3D onFrameBegin(const VkExtent3D& upscaleExtent);

    void dispatchVolumetrics(const Resources::RaytracingOutput& rtOutput);
    void dispatchIntegrate(const Resources::RaytracingOutput& rtOutput);
    void dispatchPathTracing(const Resources::RaytracingOutput& rtOutput);
    void dispatchDemodulate(const Resources::RaytracingOutput& rtOutput);
    void dispatchNeeCache(const Resources::RaytracingOutput& rtOutput);
    void dispatchDLSS(const Resources::RaytracingOutput& rtOutput);
    void dispatchRayReconstruction(const Resources::RaytracingOutput& rtOutput);
    void dispatchDenoise(const Resources::RaytracingOutput& rtOutput);
    void dispatchComposite(const Resources::RaytracingOutput& rtOutput);
    void dispatchReplaceCompositeWithDebugView(const Resources::RaytracingOutput& rtOutput);
    void dispatchNIS(const Resources::RaytracingOutput& rtOutput);
    void dispatchXeSS(const Resources::RaytracingOutput& rtOutput);
    void dispatchFSR(const Resources::RaytracingOutput& rtOutput);
    void dispatchTemporalAA(const Resources::RaytracingOutput& rtOutput);
    void dispatchToneMapping(const Resources::RaytracingOutput& rtOutput, bool performSRGBConversion);
    void dispatchBloom(const Resources::RaytracingOutput& rtOutput);
    void dispatchPostFx(Resources::RaytracingOutput& rtOutput);
    void dispatchDebugView(Rc<DxvkImage>& srcImage, const Resources::RaytracingOutput& rtOutput, bool captureScreenImage);
    void dispatchObjectPicking(Resources::RaytracingOutput& rtOutput, const VkExtent3D& srcExtent, const VkExtent3D& targetExtent);
    void dispatchDLFG();
    void dispatchFSRFrameGen();
    void updateMetrics(const float gpuIdleTimeMilliseconds) const;

    void rasterizeToSkyMatte(const DrawParameters& params, const DrawCallState& drawCallState);
    void initSkyProbe();
    void rasterizeToSkyProbe(const DrawParameters& params, const DrawCallState& drawCallState);
    void rasterizeSky(const DrawParameters& params, const DrawCallState& drawCallState);
    enum class TryHandleSkyResult {
      Default,
      SkipSubmit,
    };
    TryHandleSkyResult tryHandleSky(const DrawParameters* originalParams, DrawCallState* originalDrawCallState /* can be std::move-d */);

    void bakeTerrain(const DrawParameters& params, DrawCallState& drawCallState, const MaterialData** outOverrideMaterialData);

    InternalUpscaler getCurrentFrameUpscaler();

    InternalUpscaler m_currentUpscaler = InternalUpscaler::None;
    InternalUpscaler m_previousUpscaler = InternalUpscaler::None;

    uint32_t m_frameLastInjected = kInvalidFrameIndex;
    bool m_captureStateForRTX = true;

    Rc<DxvkImage> m_skyProbeImage;
    Rc<DxvkImageView> m_skyProbeCubePlanes[6];
    VkFormat m_skyColorFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkFormat m_skyRtColorFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    VkClearValue m_skyClearValue;
    bool m_skyClearDirty = false;

    bool shouldUseDLSS() const;
    bool shouldUseRayReconstruction() const;
    bool shouldUseNIS() const;
    bool shouldUseTAA() const;
    bool shouldUseXeSS() const;
    bool shouldUseFSR() const;
    bool shouldUseUpscaler() const { return shouldUseDLSS() || shouldUseNIS() || shouldUseTAA() || shouldUseXeSS() || shouldUseFSR(); }

    inline static bool s_triggerScreenshot = false;
    inline static bool s_triggerUsdCapture = false;
    inline static const bool s_capturePrePresentTestScreenshot = env::getEnvVar("RTX_TAKE_PRE_PRESENT_SCREENSHOT_FRAME") != "";

    bool m_rayTracingSupported;
    bool m_dlssSupported;
    bool m_submitContainsInjectRtx = false;
    uint64_t m_cachedReflexFrameId = 0;

    bool m_resetHistory = true;    // Discards use of temporal data in passes

    std::chrono::time_point<std::chrono::steady_clock> m_prevRunningTime;
    uint64_t m_prevGpuIdleTicks;

    bool m_screenshotFrameEnabled = false;
    bool m_triggerDelayedTerminate = false;
    uint32_t m_screenshotFrameNum = -1;
    uint32_t m_terminateAppFrameNum = -1;
    bool m_previousInjectRtxHadScene = false;
    IntegrateIndirectMode m_prevIntegrateIndirectMode = IntegrateIndirectMode::Count;

    DxvkRaytracingInstanceState m_rtState;

    struct {
      std::atomic<uint64_t>           signalValue = 1;
      Rc<sync::Fence>                 signal = new sync::Fence{};
      std::vector<std::future<void>>  asyncTasks = {};
    } m_objectPickingReadback {};

    std::vector<DrawCallState> m_delayedRayTracedSky;

#ifdef REMIX_DEVELOPMENT
    void queryAvailableResourceAliasing();
    void clearResourceAliasingCache();
    void analyzeResourceAliasing();

    struct ResourceCache {
      Rc<DxvkImageView> view;
      RtxFramePassStage beginPassStage = RtxFramePassStage::FrameBegin;
      RtxFramePassStage endPassStage = RtxFramePassStage::FrameEnd;
      std::unordered_set<std::string> names;
    };

    // We only have 5 types of format categories and we won't expect this will exceed 10 in near future. So we hard code the category to 10 types for better performance and easier development.
    std::vector<ResourceCache> m_resourceCacheTable[static_cast<uint32_t>(RtxTextureFormatCompatibilityCategory::Count)];
    std::unordered_map<const DxvkImageView*, std::string> m_viewMap;

    RtxFramePassStage m_currentPassStage = RtxFramePassStage::FrameBegin;
#endif
  };
} // namespace dxvk