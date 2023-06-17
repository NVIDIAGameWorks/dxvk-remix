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

#include "../dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_asset_exporter.h"
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

  struct D3D9RtxVertexCaptureData;
  
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

    float getWallTimeSinceLastCall();
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
    void endFrame(std::uint64_t cachedReflexFrameId, Rc<DxvkImage> targetImage = nullptr);

    /**
      * \brief Set D3D9 specific constant buffers
      *
      * \param [in] vsFixedFunctionConstants: resource idx of the constant buffer for FF vertex shaders
      * \param [in] vertexCaptureCB: constant buffer for vertex capture
      */
    void setConstantBuffers(const uint32_t vsFixedFunctionConstants, Rc<DxvkBuffer> vertexCaptureCB);

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

    void blitImageHelper(const Rc<DxvkImage>& srcImage, const Rc<DxvkImage>& dstImage, VkFilter filter);

    virtual void flushCommandList() override;

    SceneManager& getSceneManager();
    Resources& getResourceManager();
  
    static void triggerScreenshot() { s_triggerScreenshot = true; }
    static void triggerUsdCapture() { s_triggerUsdCapture = true; }
    static const Vector3& getLastCameraPosition() { return s_lastCameraPosition; }

    void bindCommonRayTracingResources(const Resources::RaytracingOutput& rtOutput);

    void getDenoiseArgs(NrdArgs& outPrimaryDirectNrdArgs, NrdArgs& outPrimaryIndirectNrdArgs, NrdArgs& outSecondaryNrdArgs);
    void updateRaytraceArgsConstantBuffer(Rc<DxvkCommandList> cmdList, Resources::RaytracingOutput& rtOutput, float frameTimeSecs,
                                          const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    D3D9RtxVertexCaptureData& allocAndMapVertexCaptureConstantBuffer();
    D3D9FixedFunctionVS& allocAndMapFixedFunctionConstantBuffer();

    static bool checkIsShaderExecutionReorderingSupported(Rc<DxvkDevice> device);

    static bool shouldBakeSky(const DrawCallState& drawCallState);
    static bool shouldBakeTerrain(const DrawCallState& drawCallState);

  protected:
    virtual void updateComputeShaderResources() override;
    virtual void updateRaytracingShaderResources() override;

  private:
    void reportCpuSimdSupport();

    void takeScreenshot(std::string imageName, Rc<DxvkImage> image);

    void checkOpacityMicromapSupport();
    void checkShaderExecutionReorderingSupport();

    VkExtent3D setDownscaleExtent(const VkExtent3D& upscaleExtent);

    void dispatchVolumetrics(const Resources::RaytracingOutput& rtOutput);
    void dispatchIntegrate(const Resources::RaytracingOutput& rtOutput);
    void dispatchDemodulate(const Resources::RaytracingOutput& rtOutput);
    void dispatchNeeCache(const Resources::RaytracingOutput& rtOutput);
    void dispatchDLSS(const Resources::RaytracingOutput& rtOutput);
    void dispatchDenoise(const Resources::RaytracingOutput& rtOutput, float frameTimeSecs);
    void dispatchReferenceDenoise(const Resources::RaytracingOutput& rtOutput, float frameTimeSecs);
    void dispatchComposite(const Resources::RaytracingOutput& rtOutput);
    void dispatchNIS(const Resources::RaytracingOutput& rtOutput);
    void dispatchTemporalAA(const Resources::RaytracingOutput& rtOutput);
    void dispatchToneMapping(const Resources::RaytracingOutput& rtOutput, bool performSRGBConversion, const float deltaTime);
    void dispatchBloom(const Resources::RaytracingOutput& rtOutput);
    void dispatchPostFx(Resources::RaytracingOutput& rtOutput);
    void dispatchDebugView(Rc<DxvkImage>& srcImage, const Resources::RaytracingOutput& rtOutput, bool captureScreenImage);
    void updateMetrics(const float frameTimeSecs, const float gpuIdleTimeSecs) const;

    void rasterizeToSkyMatte(const DrawParameters& params);
    void initSkyProbe();
    void rasterizeToSkyProbe(const DrawParameters& params);
    bool rasterizeSky(const DrawParameters& params, const DrawCallState& drawCallState);
    void bakeTerrain(const DrawParameters& params, DrawCallState& drawCallState);

    uint32_t m_frameLastInjected = kInvalidFrameIndex;
    bool m_captureStateForRTX = true;

    Rc<DxvkImage> m_skyProbeImage;
    std::array<Rc<DxvkImageView>, 6> m_skyProbeViews;
    VkFormat m_skyColorFormat;
    VkFormat m_skyRtColorFormat;
    VkClearValue m_skyClearValue;
    bool m_skyClearDirty = false;

    bool requiresDrawCall() const;

    bool shouldUseDLSS() const;
    bool shouldUseNIS() const;
    bool shouldUseTAA() const;
    bool shouldUseUpscaler() const { return shouldUseDLSS() || shouldUseNIS() || shouldUseTAA(); }

    inline static bool s_triggerScreenshot = false;
    inline static bool s_triggerUsdCapture = false;
    inline static Vector3 s_lastCameraPosition = 0.f;

    bool m_rayTracingSupported;
    bool m_dlssSupported;

    bool m_resetHistory = true;    // Discards use of temporal data in passes

    std::chrono::time_point<std::chrono::system_clock> m_prevRunningTime;
    uint64_t m_prevGpuIdleTicks;

    bool m_screenshotFrameEnabled = false;
    bool m_triggerDelayedTerminate = false;
    uint32_t m_screenshotFrameNum = -1;
    uint32_t m_terminateAppFrameNum = -1;
    bool m_previousInjectRtxHadScene = false;

    DxvkRaytracingInstanceState m_rtState;
  };
} // namespace dxvk