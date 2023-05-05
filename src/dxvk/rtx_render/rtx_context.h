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

#include <chrono>
#include "rtx_options.h"

struct VolumeArgs;
struct RaytraceArgs;

namespace dxvk {
  class DxvkContext;
  class AssetExporter;
  class SceneManager;

  /** 
   * \brief RTX context
   * 
   * Tracks pipeline state and records command lists.
   * This is where the actual rendering commands are
   * recorded.
   */

  class RtxContext : public DxvkContext {
    struct DrawParameters {
      uint32_t vertexCount = 0;
      uint32_t indexCount = 0;
      uint32_t instanceCount = 0;
      uint32_t firstIndex = 0;
      uint32_t vertexOffset = 0;
      uint32_t firstInstance = 0;
    };

  public:
    
    RtxContext(const Rc<DxvkDevice>& device);
    ~RtxContext();

    float getWallTimeSinceLastCall();
    float getGpuIdleTimeSinceLastCall();

    void endFrame(Rc<DxvkImage> targetImage = nullptr);

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
      * \param [in] targetImage: Image to store raytraced result in
      */
    void injectRTX(Rc<DxvkImage> targetImage = nullptr);

    /**
      * \brief Sets the user clip planes from a fixed-function renderer
      *
      * This function does not affect normal rendering and is only used by RTX
      * to pull the clip plane data.
      *
      * \param [in] enableMask Bitmask of enabled clip planes
      * \param [in] planes Array of clip plane data
      */
    void setClipPlanes(uint32_t enableMask, const Vector4 planes[MaxClipPlanes]);

    /**
      * \brief Sets the active shader state for context
      *
      * \param [in] useProgrammableVS: Using shader or fixed function
      * \param [in] useProgrammablePS: Using shader or fixed function
      */
    void setShaderState(const bool useProgrammableVS, const bool useProgrammablePS);

    /**
      * \brief Sets the geometry state for the context
      *
      * \param [in] geometry: A collection of vertex and index buffers required for raytracing
      */
    void setGeometry(const RasterGeometry& geometry, RtxGeometryStatus status);

    /**
      * \brief Sets the active textures on context
      *
      * \param [in] colorTextureSlot: Index into m_rc containing 1st texture
      * \param [in] colorTextureSlot2: Index into m_rc containing 2nd texture
      */
    void setTextureSlots(const uint32_t colorTextureSlot, const uint32_t colorTextureSlot2);

    /**
      * \brief Set the current object2world transform on context
      *
      * \param [in] objectToWorld: matrix containing the object to world transform
      */
    void setObjectTransform(const Matrix4& objectToWorld);

    /**
      * \brief Set the current camera matrices on context
      *
      * \param [in] worldToView: matrix containing the view transform
      * \param [in] viewToProjection: matrix containing the projection transform
      */
    void setCameraTransforms(const Matrix4& worldToView, const Matrix4& viewToProjection);
    void setConstantBuffers(const uint32_t vsFixedFunctionConstants);

    /**
      * \brief Set future skinning data
      *
      * \param [in] skinningData: shared future containing skinning data
      */
    void setSkinningData(std::shared_future<SkinningData> skinningData);

    /**
      * \brief Set legacy rendering state on the context
      *
      * \param [in] stage: structure containing state to set
      */
    void setLegacyState(const DxvkRtxLegacyState& stage);

    /**
      * \brief Sets the current legacy texture stage state on context
      *
      * \param [in] stage: structure containing the legacy state
      */
    void setTextureStageState(const DxvkRtxTextureStageState& stage);

    /**
      * \brief Adds a batch of lights to the scene context
      *
      * \param [in] pLights: array of light structures
      * \param [in] numLights: number of lights
      */
    void addLights(const D3DLIGHT9* pLights, const uint32_t numLights);

    void clearRenderTarget(const Rc<DxvkImageView>& imageView, VkImageAspectFlags clearAspects, VkClearValue clearValue);
    void clearImageView(const Rc<DxvkImageView>& imageView, VkOffset3D offset, VkExtent3D extent, VkImageAspectFlags aspect, VkClearValue value);

    RtxGeometryStatus commitGeometryToRT(const DrawParameters& params);

    virtual void beginRecording(const Rc<DxvkCommandList>& cmdList) override;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance) override;
    virtual void flushCommandList() override;

    void blitImageHelper(const Rc<DxvkImage>& srcImage, const Rc<DxvkImage>& dstImage, VkFilter filter);

    SceneManager& getSceneManager();
    Resources& getResourceManager();
  
    static void triggerScreenshot() { s_triggerScreenshot = true; }
    static void triggerUsdCapture() { s_triggerUsdCapture = true; }
    static const Vector3& getLastCameraPosition() { return s_lastCameraPosition; }

    void bindCommonRayTracingResources(const Resources::RaytracingOutput& rtOutput);

    void getDenoiseArgs(NrdArgs& outPrimaryDirectNrdArgs, NrdArgs& outPrimaryIndirectNrdArgs, NrdArgs& outSecondaryNrdArgs);
    void updateRaytraceArgsConstantBuffer(Rc<DxvkCommandList> cmdList, Resources::RaytracingOutput& rtOutput, float frameTimeSecs);

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

    bool isStencilShadowVolumeState();

    void rasterizeToSkyMatte(const DrawParameters& params);
    void initSkyProbe();
    void rasterizeToSkyProbe(const DrawParameters& params);
    bool rasterizeSky(const DrawParameters& params, const DrawCallState& drawCallState);

    void enableRtxCapture();
    void disableRtxCapture();

    uint32_t m_frameLastInjected = kInvalidFrameIndex;
    bool m_captureStateForRTX = true;

    Rc<DxvkImage> m_skyProbeImage;
    std::array<Rc<DxvkImageView>, 6> m_skyProbeViews;
    VkFormat m_skyColorFormat;
    VkFormat m_skyRtColorFormat;
    VkClearValue m_skyClearValue;
    bool m_skyClearDirty = false;

    void updateReflexConstants();

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
    int32_t m_drawCallID = 0;

    std::chrono::time_point<std::chrono::system_clock> m_prevRunningTime;
    uint64_t m_prevGpuIdleTicks;

    bool m_screenshotFrameEnabled = false;
    bool m_triggerDelayedTerminate = false;
    uint32_t m_screenshotFrameNum = -1;
    uint32_t m_terminateAppFrameNum = -1;
    float m_terrainOffset = 0.f;
    XXH64_hash_t m_lastTerrainMaterial = 0;
    bool m_previousInjectRtxHadScene = false;

    DxvkRaytracingInstanceState m_rtState;
  };
} // namespace dxvk