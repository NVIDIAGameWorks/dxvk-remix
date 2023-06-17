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

#include <cstring>
#include <cmath>
#include <cassert>

#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_adapter.h"
#include "rtx_context.h"
#include "rtx_asset_exporter.h"
#include "rtx_options.h"
#include "rtx_bindless_resource_manager.h"
#include "rtx_opacity_micromap_manager.h"
#include "rtx_asset_replacer.h"
#include "rtx_terrain_baker.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx/pass/volume_args.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/utility/gpu_printing.h"
#include "rtx_nrd_settings.h"
#include "rtx_scene_manager.h"

#include "../d3d9/d3d9_state.h"
#include "../d3d9/d3d9_spec_constants.h"

#include "../util/log/metrics.h"
#include "../util/util_defer.h"

#include "rtx_imgui.h"
#include "dxvk_scoped_annotation.h"
#include "imgui/dxvk_imgui.h"

#include <ctime>
#include <nvapi.h>

#include <NvLowLatencyVk.h>
#include <pclstats.h>

#include "rtx_matrix_helpers.h"
#include "../util/util_fastops.h"

namespace dxvk {

  Metrics Metrics::s_instance;

  void RtxContext::takeScreenshot(std::string imageName, Rc<DxvkImage> image) {
    // NOTE: Improve this, I'd like all textures from the same frame to have the same time code...  Currently sampling the time on each "dump op" results in different timecodes.
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);

    std::string path = env::getEnvVar("DXVK_SCREENSHOT_PATH");

    if (path.empty()) {
      path = "./Screenshots/";
    } else if (*path.rbegin() != '/') {
      path += '/';
    }

    auto& exporter = getCommonObjects()->metaExporter();
    exporter.dumpImageToFile(this, path, str::format(imageName, "_", tm.tm_mday, tm.tm_mon, tm.tm_year, "-", tm.tm_hour, tm.tm_min, tm.tm_sec, ".dds"), image);
  }

  void RtxContext::blitImageHelper(const Rc<DxvkImage>& srcImage, const Rc<DxvkImage>& dstImage, VkFilter filter) {
    const DxvkFormatInfo* dstFormatInfo = imageFormatInfo(dstImage->info().format);
    const DxvkFormatInfo* srcFormatInfo = imageFormatInfo(srcImage->info().format);

    const VkImageSubresource dstSubresource = { dstFormatInfo->aspectMask, 0, 0 };
    const VkImageSubresource srcSubresource = { srcFormatInfo->aspectMask, 0, 0 };

    VkExtent3D srcExtent = srcImage->mipLevelExtent(srcSubresource.mipLevel);
    VkExtent3D dstExtent = dstImage->mipLevelExtent(dstSubresource.mipLevel);

    VkImageSubresourceLayers dstSubresourceLayers = {
      dstSubresource.aspectMask,
      dstSubresource.mipLevel,
      dstSubresource.arrayLayer, 1 };

    VkImageSubresourceLayers srcSubresourceLayers = {
      srcSubresource.aspectMask,
      srcSubresource.mipLevel,
      srcSubresource.arrayLayer, 1 };

    VkImageBlit blitInfo;

    blitInfo.dstSubresource = dstSubresourceLayers;
    blitInfo.srcSubresource = srcSubresourceLayers;

    blitInfo.dstOffsets[0] = VkOffset3D{ 0,                        0,                       0 };
    blitInfo.dstOffsets[1] = VkOffset3D{ int32_t(dstExtent.width),  int32_t(dstExtent.height),  1 };

    blitInfo.srcOffsets[0] = VkOffset3D{ 0,                          0,                         0 };
    blitInfo.srcOffsets[1] = VkOffset3D{ int32_t(srcExtent.width),    int32_t(srcExtent.height),    1 };

    VkComponentMapping swizzle = {
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY,
    };

    blitImage(dstImage, swizzle, srcImage, swizzle, blitInfo, filter);
  }

  RtxContext::RtxContext(const Rc<DxvkDevice>& device)
    : DxvkContext(device) {
    // Note: This may not be the best place to check for these features/properties, they ideally would be specified as
    // required upfront, but there's no good place to do that for this RTX extension (the D3D9 stuff does it before device
    // creation), so instead we just check for what is needed.
    // Note: When adding new extensions update DxvkAdapter::createDevice as it is what brings these features over.
    m_rayTracingSupported = (m_device->features().core.features.shaderInt16 &&
                             m_device->features().vulkan11Features.storageBuffer16BitAccess &&
                             m_device->features().vulkan11Features.uniformAndStorageBuffer16BitAccess &&
                             m_device->features().vulkan12Features.bufferDeviceAddress &&
                             m_device->features().vulkan12Features.descriptorIndexing &&
                             m_device->features().vulkan12Features.runtimeDescriptorArray &&
                             m_device->features().vulkan12Features.descriptorBindingPartiallyBound &&
                             m_device->features().vulkan12Features.shaderStorageBufferArrayNonUniformIndexing &&
                             m_device->features().vulkan12Features.shaderSampledImageArrayNonUniformIndexing &&
                             m_device->features().vulkan12Features.descriptorBindingVariableDescriptorCount &&
                             m_device->features().vulkan12Features.shaderInt8 &&
                             m_device->features().vulkan12Features.shaderFloat16 &&
                             m_device->features().vulkan12Features.uniformAndStorageBuffer8BitAccess &&
                             m_device->features().khrAccelerationStructureFeatures.accelerationStructure &&
                             m_device->features().khrRayQueryFeatures.rayQuery &&
                             m_device->features().khrDeviceRayTracingPipelineFeatures.rayTracingPipeline &&
                             m_device->extensions().khrShaderInt8Float16Types &&
                             m_device->properties().coreSubgroup.subgroupSize >= 1 &&
                             m_device->properties().coreSubgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT &&
                             m_device->properties().coreSubgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT);

    m_dlssSupported = (m_device->extensions().nvxBinaryImport &&
                       m_device->extensions().nvxImageViewHandle &&
                       m_device->extensions().khrPushDescriptor);


    if (env::getEnvVar("DXVK_DUMP_SCREENSHOT_FRAME") != "") {
      m_screenshotFrameNum = stoul(env::getEnvVar("DXVK_DUMP_SCREENSHOT_FRAME"));
      m_screenshotFrameEnabled = true;
    }
    if (env::getEnvVar("DXVK_TERMINATE_APP_FRAME") != "") {
      m_terminateAppFrameNum = stoul(env::getEnvVar("DXVK_TERMINATE_APP_FRAME"));
      m_triggerDelayedTerminate = true;
    }

    m_prevRunningTime = std::chrono::system_clock::now();

    checkOpacityMicromapSupport();
    checkShaderExecutionReorderingSupport();
    reportCpuSimdSupport();
  }

  RtxContext::~RtxContext() {
  }

  SceneManager& RtxContext::getSceneManager() {
    return getCommonObjects()->getSceneManager();
  }
  Resources& RtxContext::getResourceManager() {
    return getCommonObjects()->getResources();
  }

  // Returns wall time between calls to this in seconds
  float RtxContext::getWallTimeSinceLastCall() {
    auto currTime = std::chrono::system_clock::now();

    std::chrono::duration<float> elapsedSec = currTime - m_prevRunningTime;
    m_prevRunningTime = currTime;

    return elapsedSec.count();
  }

  // Returns GPU idle time between calls to this in seconds
  float RtxContext::getGpuIdleTimeSinceLastCall() {
    uint64_t currGpuIdleTicks = m_device->getStatCounters().getCtr(DxvkStatCounter::GpuIdleTicks);
    uint64_t delta = currGpuIdleTicks - m_prevGpuIdleTicks;
    m_prevGpuIdleTicks = currGpuIdleTicks;

    return (float)delta * 0.001f * 0.001f; // to secs
  }

  VkExtent3D RtxContext::setDownscaleExtent(const VkExtent3D& upscaleExtent) {
    ScopedCpuProfileZone();
    VkExtent3D downscaleExtent;
    if (shouldUseDLSS()) {
      DxvkDLSS& dlss = m_common->metaDLSS();
      uint32_t displaySize[2] = { upscaleExtent.width, upscaleExtent.height };
      uint32_t renderSize[2];

      dlss.setSetting(displaySize, RtxOptions::Get()->getDLSSQuality(), renderSize);
      downscaleExtent.width = renderSize[0];
      downscaleExtent.height = renderSize[1];
      downscaleExtent.depth = 1;
    } else if (shouldUseNIS() || shouldUseTAA()) {
      auto resolutionScale = RtxOptions::Get()->getResolutionScale();
      downscaleExtent.width = uint32_t(std::roundf(upscaleExtent.width * resolutionScale));
      downscaleExtent.height = uint32_t(std::roundf(upscaleExtent.height * resolutionScale));
      downscaleExtent.depth = 1;
    } else {
      downscaleExtent = upscaleExtent;
    }
    downscaleExtent.width = std::max(downscaleExtent.width, 1u);
    downscaleExtent.height = std::max(downscaleExtent.height, 1u);
    return downscaleExtent;
  }

  void RtxContext::resetScreenResolution(const VkExtent3D& upscaleExtent) {
    // Calculate extents based on if DLSS is enabled or not
    const VkExtent3D downscaleExtent = setDownscaleExtent(upscaleExtent);

    // Resize the RT screen dependant buffers (if needed)
    getResourceManager().onResize(this, downscaleExtent, upscaleExtent);

    // Set up the Camera
    RtCamera& camera = getSceneManager().getCamera();
    uint32_t renderSize[] = { downscaleExtent.width, downscaleExtent.height };
    uint32_t displaySize[] = { upscaleExtent.width, upscaleExtent.height };
    camera.setResolution(renderSize, displaySize);

    // Note: Ensure the rendering resolution is not more than 2^14 - 1. This is due to assuming only
    // 14 of the 16 bits of an integer will be used for these pixel coordinates to pack additional data
    // into the free bits in memory payload structures on the GPU.
    assert((renderSize[0] < (1 << 14)) && (renderSize[1] < (1 << 14)));
  }

  // Hooked into D3D9 presentImage (same place HUD rendering is)
  void RtxContext::injectRTX(std::uint64_t cachedReflexFrameId, Rc<DxvkImage> targetImage) {
    ScopedCpuProfileZone();

    commitGraphicsState<true, false>();

    m_device->setPresentThrottleDelay(RtxOptions::Get()->getPresentThrottleDelay());

    if (!m_rayTracingSupported) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Raytracing doesn't appear to be supported on this HW.")));
      return;
    }

    if (m_frameLastInjected == m_device->getCurrentFrameId())
      return;

    const bool isCameraValid = getSceneManager().getCamera().isValid(m_device->getCurrentFrameId());
    if (!isCameraValid) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Trying to raytrace but not detecting a valid camera.")));
    }

    getCommonObjects()->getTextureManager().kickoff();

    // Update frame counter only after actual rendering
    if (isCameraValid)
      m_frameLastInjected = m_device->getCurrentFrameId();
    
    if (RtxOptions::Get()->alwaysWaitForAsyncTextures()) {
      // Wait for the texture manager to finish async uploads
      RtxTextureManager& textureManager = m_device->getCommon()->getTextureManager();
      textureManager.synchronize();

      // Now complete any pending promotions
      textureManager.finalizeAllPendingTexturePromotions();

    }

    if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS && !getCommonObjects()->metaDLSS().supportsDLSS()) {
      RtxOptions::Get()->upscalerTypeRef() = UpscalerType::TAAU;
    }

    ShaderManager::getInstance()->checkForShaderChanges();

    const float frameTimeSecs = RtxOptions::Get()->timeDeltaBetweenFrames() == 0.f ? getWallTimeSinceLastCall() : RtxOptions::Get()->timeDeltaBetweenFrames();
    const float gpuIdleTimeSecs = getGpuIdleTimeSinceLastCall();

    const bool isRaytracingEnabled = RtxOptions::Get()->enableRaytracing();

    if(isRaytracingEnabled && isCameraValid) {
      if (targetImage == nullptr) {
        targetImage = m_state.om.renderTargets.color[0].view->image();  
      }

      const bool captureTestScreenshot = (m_screenshotFrameEnabled && m_device->getCurrentFrameId() == m_screenshotFrameNum);
      const bool captureScreenImage = s_triggerScreenshot || captureTestScreenshot;
      const bool captureDebugImage = RtxOptions::Get()->shouldCaptureDebugImage();
      s_lastCameraPosition = getSceneManager().getCamera().getPosition();
      
      if(s_triggerUsdCapture) {
        s_triggerUsdCapture = false;
        getSceneManager().triggerUsdCapture();
      }

      if (captureTestScreenshot) {
        Logger::info(str::format("RTX: Test screenshot capture triggered"));
        Logger::info(str::format("RTX: Use separate denoiser ", RtxOptions::Get()->isSeparatedDenoiserEnabled()));
        Logger::info(str::format("RTX: Use rtxdi ", RtxOptions::Get()->useRTXDI()));
        Logger::info(str::format("RTX: Use dlss ", RtxOptions::Get()->isDLSSEnabled()));
        Logger::info(str::format("RTX: Use nis ", RtxOptions::Get()->isNISEnabled()));
        m_screenshotFrameEnabled = false;
      }

      if (captureScreenImage && captureDebugImage) {
        takeScreenshot("orgImage", targetImage);
      }

      this->spillRenderPass(false);

      m_execBarriers.recordCommands(m_cmd);

      flushCommandList();

      ScopedGpuProfileZone(this, "InjectRTX");

      // Signal Reflex rendering start

      RtxReflex& reflex = m_common->metaReflex();

      // Note: Update the Reflex mode in case the option has changed.
      reflex.updateMode();

      // Note: This indicates the start of the bulk of the rendering submission stage, so most rendering operations should
      // come after this point (BLAS building, various rendering passes, etc). Since this is called on the CS thread the Reflex
      // end rendering call should also happen on this same thread for consistency (which it does later when presenting is
      // dispatched to the submit thread as this marks the end of rendering).
      reflex.beginRendering(cachedReflexFrameId);

      // Update all the GPU buffers needed to describe the scene
      getSceneManager().prepareSceneData(this, m_cmd, m_execBarriers, frameTimeSecs);
      
      // If we really don't have any RT to do, just bail early (could be UI/menus rendering)
      if (getSceneManager().getSurfaceBuffer() != nullptr) {

        auto logRenderPassRaytraceModeRayQuery = [=](const char* renderPassName, auto mode) {
          switch (mode) {
          case decltype(mode)::RayQuery:
            Logger::info(str::format("RenderPass ", renderPassName, " Raytrace Mode: Ray Query (CS)"));
            break;
          case decltype(mode)::RayQueryRayGen:
            Logger::info(str::format("RenderPass ", renderPassName, " Raytrace Mode: Ray Query (RGS)"));
            break;
          }
        };

        auto logRenderPassRaytraceMode = [=](const char* renderPassName, auto mode) {
          switch (mode) {
          case decltype(mode)::RayQuery:
          case decltype(mode)::RayQueryRayGen:
            logRenderPassRaytraceModeRayQuery(renderPassName, mode);
            break;
          case decltype(mode)::TraceRay:
            Logger::info(str::format("RenderPass ", renderPassName, " Raytrace Mode: Trace Ray (RGS)"));
            break;
          }
        };

        // Log used raytracing mode
        static RenderPassGBufferRaytraceMode sPrevRenderPassGBufferRaytraceMode = RenderPassGBufferRaytraceMode::Count;
        static RenderPassIntegrateDirectRaytraceMode sPrevRenderPassIntegrateDirectRaytraceMode = RenderPassIntegrateDirectRaytraceMode::Count;
        static RenderPassIntegrateIndirectRaytraceMode sPrevRenderPassIntegrateIndirectRaytraceMode = RenderPassIntegrateIndirectRaytraceMode::Count;
        if (sPrevRenderPassGBufferRaytraceMode != RtxOptions::Get()->getRenderPassGBufferRaytraceMode() ||
            sPrevRenderPassIntegrateDirectRaytraceMode != RtxOptions::Get()->getRenderPassIntegrateDirectRaytraceMode() ||
            sPrevRenderPassIntegrateIndirectRaytraceMode != RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode()) {
          
          sPrevRenderPassGBufferRaytraceMode = RtxOptions::Get()->getRenderPassGBufferRaytraceMode();
          sPrevRenderPassIntegrateDirectRaytraceMode = RtxOptions::Get()->getRenderPassIntegrateDirectRaytraceMode();
          sPrevRenderPassIntegrateIndirectRaytraceMode = RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode();

          logRenderPassRaytraceMode("GBuffer", RtxOptions::Get()->getRenderPassGBufferRaytraceMode());
          logRenderPassRaytraceModeRayQuery("Integrate Direct", RtxOptions::Get()->getRenderPassIntegrateDirectRaytraceMode());
          logRenderPassRaytraceMode("Integrate Indirect", RtxOptions::Get()->getRenderPassIntegrateIndirectRaytraceMode());
  
          m_resetHistory = true;
        }

        // Calculate extents based on if DLSS is enabled or not
        const VkExtent3D downscaledExtent = setDownscaleExtent(targetImage->info().extent);

        if (!getResourceManager().validateRaytracingOutput(downscaledExtent, targetImage->info().extent)) {
          Logger::debug("Raytracing output resources were not available to use this frame, so we must re-create inline.");

          resetScreenResolution(targetImage->info().extent);
        }

        // Allocate/release resources based on each pass's status
        getResourceManager().onFrameBegin(this, getCommonObjects()->getTextureManager(), downscaledExtent, targetImage->info().extent);

        Resources::RaytracingOutput& rtOutput = getResourceManager().getRaytracingOutput();

        // Generate ray tracing constant buffer
        updateRaytraceArgsConstantBuffer(m_cmd, rtOutput, frameTimeSecs, downscaledExtent, targetImage->info().extent);

        // Volumetric Lighting
        dispatchVolumetrics(rtOutput);
        
        // Gbuffer Raytracing
        m_common->metaPathtracerGbuffer().dispatch(this, rtOutput);

        // RTXDI
        m_common->metaRtxdiRayQuery().dispatch(this, rtOutput);
        
        // NEE Cache
        dispatchNeeCache(rtOutput);
        
        // Integration Raytracing
        dispatchIntegrate(rtOutput);

        m_common->metaRtxdiRayQuery().dispatchConfidence(this, rtOutput);

        // ReSTIR GI
        m_common->metaReSTIRGIRayQuery().dispatch(this, rtOutput);
        
        if (captureScreenImage && captureDebugImage) {
          takeScreenshot("baseReflectivity", rtOutput.m_primaryBaseReflectivity.image(Resources::AccessType::Read));
        }

        // Demodulation
        dispatchDemodulate(rtOutput);

        // Note: Primary direct diffuse/specular radiance textures noisy and in a demodulated state after demodulation step.
        if (captureScreenImage && captureDebugImage) {
          takeScreenshot("noisyDiffuse", rtOutput.m_primaryDirectDiffuseRadiance.image(Resources::AccessType::Read));
          takeScreenshot("noisySpecular", rtOutput.m_primaryDirectSpecularRadiance.image(Resources::AccessType::Read));
        }

        // Denoising
        dispatchDenoise(rtOutput, frameTimeSecs);

        // Note: Primary direct diffuse/specular radiance textures denoised but in a still demodulated state after denoising step.
        if (captureScreenImage && captureDebugImage) {
          takeScreenshot("denoisedDiffuse", rtOutput.m_primaryDirectDiffuseRadiance.image(Resources::AccessType::Read));
          takeScreenshot("denoisedSpecular", rtOutput.m_primaryDirectSpecularRadiance.image(Resources::AccessType::Read));
        }

        // Composition
        dispatchComposite(rtOutput);

        dispatchReferenceDenoise(rtOutput, frameTimeSecs);
        
        if (captureScreenImage && captureDebugImage) {
          takeScreenshot("rtxImagePostComposite", rtOutput.m_compositeOutput.resource(Resources::AccessType::Read).image);
        }

        // Upscaling if DLSS/NIS enabled, or the Composition Pass will do upscaling
        if (shouldUseDLSS()) {
          dispatchDLSS(rtOutput);
        } else if (shouldUseNIS()) {
          dispatchNIS(rtOutput);
        } else if (shouldUseTAA()){
          dispatchTemporalAA(rtOutput);
        } else {
          copyImage(
            rtOutput.m_finalOutput.image,
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            { 0, 0, 0 },
            rtOutput.m_compositeOutput.image(Resources::AccessType::Read),
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            { 0, 0, 0 },
            rtOutput.m_compositeOutputExtent);
        }

        dispatchBloom(rtOutput);
        dispatchPostFx(rtOutput);

        // Tone mapping
        // WAR for TREX-553 - disable sRGB conversion as NVTT implicitly applies it during dds->png
        // conversion for 16bit float formats
        const bool performSRGBConversion = !captureScreenImage;
        dispatchToneMapping(rtOutput, performSRGBConversion, frameTimeSecs);

        if (captureScreenImage) {
          if (m_common->metaDebugView().debugViewIdx() == DEBUG_VIEW_DISABLED)
            takeScreenshot("rtxImagePostTonemapping", rtOutput.m_finalOutput.image);
          if (captureDebugImage) {
            takeScreenshot("albedo", rtOutput.m_primaryAlbedo.image);
            takeScreenshot("worldNormals", rtOutput.m_primaryWorldShadingNormal.image);
            takeScreenshot("worldMotion", rtOutput.m_primaryVirtualMotionVector.image);
            takeScreenshot("linearZ", rtOutput.m_primaryLinearViewZ.image);
          }
        }

        // Set up output src
        Rc<DxvkImage> srcImage = rtOutput.m_finalOutput.image;

        // Debug view overrides
        dispatchDebugView(srcImage, rtOutput, captureScreenImage);

        {
          ScopedGpuProfileZone(this, "Blit to Game");

          Rc<DxvkImage> dstImage = targetImage;

          // Note: Nearest neighbor filtering used to give a precise view of debug buffer when DLSS is used. Otherwise the resolution should match 1:1 and
          // this should be the same as using bilinear filtering.
          blitImageHelper(srcImage, dstImage, VkFilter::VK_FILTER_NEAREST);
        }

        getSceneManager().onFrameEnd(this);

        rtOutput.onFrameEnd();
      }

      m_previousInjectRtxHadScene = true;
    } else {
      getSceneManager().clear(this, m_previousInjectRtxHadScene);
      m_previousInjectRtxHadScene = false;
    }

    // Reset the fog state to get it re-discovered on the next frame
    getSceneManager().clearFogState();

    // Update stats
    updateMetrics(frameTimeSecs, gpuIdleTimeSecs);

    m_resetHistory = false;
  }

  // Called right before D3D9 present
  void RtxContext::endFrame(std::uint64_t cachedReflexFrameId, Rc<DxvkImage> targetImage) {
    // Fallback inject (is a no-op if already injected this frame, or no valid RT scene)
    injectRTX(cachedReflexFrameId, targetImage);

    // If injectRTX couldn't screenshot a final image,
    // take a screenshot of a present image (with UI and others)
    {
      const bool isRaytracingEnabled = RtxOptions::Get()->enableRaytracing();
      const bool isCameraValid = getSceneManager().getCamera().isValid(m_device->getCurrentFrameId());

      if (!isRaytracingEnabled || !isCameraValid) {
        const bool captureTestScreenshot = (m_screenshotFrameEnabled && m_device->getCurrentFrameId() == m_screenshotFrameNum);
        const bool captureDxvkScreenImage = s_triggerScreenshot || captureTestScreenshot;
        if (captureDxvkScreenImage) {
          if (targetImage == nullptr) {
            targetImage = m_state.om.renderTargets.color[0].view->image();
          }
          takeScreenshot("rtxImageDxvkView", targetImage);
        }
      }
    }
    s_triggerScreenshot = false;

    // Some time in the future kill process
    if (m_triggerDelayedTerminate &&
        (m_device->getCurrentFrameId() > m_terminateAppFrameNum) &&
        getSceneManager().isGameCapturerIdle()) {
      Logger::info(str::format("RTX: Terminating application"));
      Metrics::serialize();
      getCommonObjects()->metaExporter().waitForAllExportsToComplete();

      env::killProcess();
    }
  }

  void RtxContext::updateMetrics(const float frameTimeSecs, const float gpuIdleTimeSecs) const {
    ScopedCpuProfileZone();
    Metrics::log(Metric::average_frame_time, frameTimeSecs * 1000); // In milliseconds
    Metrics::log(Metric::gpu_idle_ticks, gpuIdleTimeSecs * 1000); // In milliseconds
    uint64_t vidUsageMib = 0;
    uint64_t sysUsageMib = 0;
    // Calc memory usage
    for (uint32_t i = 0; i < m_device->adapter()->memoryProperties().memoryHeapCount; i++) {
      bool isDeviceLocal = m_device->adapter()->memoryProperties().memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

      if (isDeviceLocal) {
        vidUsageMib += m_device->getMemoryStats(i).totalUsed() >> 20;
      }
      else {
        sysUsageMib += m_device->getMemoryStats(i).totalUsed() >> 20;
      }
    }
    Metrics::log(Metric::vid_memory_usage, static_cast<float>(vidUsageMib)); // In MB
    Metrics::log(Metric::sys_memory_usage, static_cast<float>(sysUsageMib)); // In MB
  }

  void RtxContext::setConstantBuffers(const uint32_t vsFixedFunctionConstants, Rc<DxvkBuffer> vertexCaptureCB) {
    m_rtState.vsFixedFunctionCB = m_rc[vsFixedFunctionConstants].bufferSlice.buffer();
    m_rtState.vertexCaptureCB = vertexCaptureCB;
  }

  void RtxContext::addLights(const D3DLIGHT9* pLights, const uint32_t numLights) {
    for (uint32_t i = 0; i < numLights; i++) {
      getSceneManager().addLight(pLights[i]);
    }
  }

  void RtxContext::commitGeometryToRT(const DrawParameters& params, DrawCallState& drawCallState){
    ScopedCpuProfileZone();

    RasterGeometry& geoData = drawCallState.geometryData;
    DrawCallTransforms& transformData = drawCallState.transformData;

    assert(geoData.futureGeometryHashes.valid());
    assert(geoData.positionBuffer.defined());

    const auto fusedMode = RtxOptions::Get()->fusedWorldViewMode();
    if (unlikely(fusedMode != FusedWorldViewMode::None)) {
      if (fusedMode == FusedWorldViewMode::View) {
        // Set World from WorldView transform
        transformData.objectToWorld = transformData.objectToView;
        // Set camera to identity
        transformData.worldToView = Matrix4();
      } else if (fusedMode == FusedWorldViewMode::World) {
        // Nothing to do...
      }
    }

    // Sync any pending work with geometry processing threads
    const RtCamera& camera = m_common->getSceneManager().getCameraManager().getLastSetCamera();
    if (drawCallState.finalizePendingFutures(camera.isValid(m_device->getCurrentFrameId()) ? &camera : nullptr)) {
      // Handle the sky
      drawCallState.isSky = rasterizeSky(params, drawCallState);

      // Bake the terrain
      bakeTerrain(params, drawCallState);

      getSceneManager().submitDrawState(this, m_cmd, drawCallState);
    }
  }

  static uint32_t jenkinsHash(uint32_t a) {
    // http://burtleburtle.net/bob/hash/integer.html
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
  }

  void RtxContext::getDenoiseArgs(NrdArgs& outPrimaryDirectNrdArgs, NrdArgs& outPrimaryIndirectNrdArgs, NrdArgs& outSecondaryNrdArgs) {
    const bool realtimeDenoiserEnabled = RtxOptions::Get()->isDenoiserEnabled() && !RtxOptions::Get()->useDenoiserReferenceMode();
    const bool separateDenoiserEnabled = RtxOptions::Get()->isSeparatedDenoiserEnabled();

    auto& denoiser0 = realtimeDenoiserEnabled ? (separateDenoiserEnabled ? m_common->metaPrimaryDirectLightDenoiser() : m_common->metaPrimaryCombinedLightDenoiser()) : m_common->metaReferenceDenoiser();
    auto& denoiser1 = realtimeDenoiserEnabled ? (separateDenoiserEnabled ? m_common->metaPrimaryIndirectLightDenoiser() : m_common->metaPrimaryCombinedLightDenoiser()) : m_common->metaReferenceDenoiser();
    auto& denoiser2 = m_common->metaSecondaryCombinedLightDenoiser();

    outPrimaryDirectNrdArgs = denoiser0.getNrdArgs();
    outPrimaryIndirectNrdArgs = denoiser1.getNrdArgs();
    outSecondaryNrdArgs = denoiser2.getNrdArgs();
  }

  void RtxContext::updateRaytraceArgsConstantBuffer(Rc<DxvkCommandList> cmdList, Resources::RaytracingOutput& rtOutput, float frameTimeSecs,
                                                    const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) {
    ScopedCpuProfileZone();
    // Prepare shader arguments
    RaytraceArgs &constants = rtOutput.m_raytraceArgs;
    constants = {}; 

    auto const& camera{ getSceneManager().getCamera() };
    const uint32_t frameIdx = m_device->getCurrentFrameId();

    constants.camera = camera.getShaderConstants();

    const CameraManager& cameraManager = getSceneManager().getCameraManager();
    const bool enablePortalVolumes = RtxOptions::Get()->enableVolumetricsInPortals() &&
      cameraManager.isCameraValid(CameraType::Portal0) &&
      cameraManager.isCameraValid(CameraType::Portal1);
    
    // Note: Ensure the number of lights can fit into the ray tracing args.
    assert(getSceneManager().getLightManager().getActiveCount() <= std::numeric_limits<uint16_t>::max());

    constants.frameIdx = RtxOptions::Get()->getRngSeedWithFrameIndex() ? m_device->getCurrentFrameId() : 0;
    constants.lightCount = static_cast<uint16_t>(getSceneManager().getLightManager().getActiveCount());

    constants.fireflyFilteringLuminanceThreshold = RtxOptions::Get()->fireflyFilteringLuminanceThreshold();
    constants.secondarySpecularFireflyFilteringThreshold = RtxOptions::Get()->secondarySpecularFireflyFilteringThreshold();
    constants.primaryRayMaxInteractions = RtxOptions::Get()->getPrimaryRayMaxInteractions();
    constants.psrRayMaxInteractions = RtxOptions::Get()->getPSRRayMaxInteractions();
    constants.secondaryRayMaxInteractions = RtxOptions::Get()->getSecondaryRayMaxInteractions();

    // Todo: Potentially move this to the volume manager in the future to be more organized.
    constants.volumeTemporalReuseMaxSampleCount = RtxOptions::Get()->getVolumetricTemporalReuseMaxSampleCount();
    
    constants.russianRouletteMaxContinueProbability = RtxOptions::Get()->getRussianRouletteMaxContinueProbability();
    constants.russianRoulette1stBounceMinContinueProbability = RtxOptions::Get()->getRussianRoulette1stBounceMinContinueProbability();
    constants.russianRoulette1stBounceMaxContinueProbability = RtxOptions::Get()->getRussianRoulette1stBounceMaxContinueProbability();
    constants.pathMinBounces = RtxOptions::Get()->getPathMinBounces();
    constants.pathMaxBounces = RtxOptions::Get()->getPathMaxBounces();
    // Note: Probability adjustments always in the 0-1 range and therefore less than FLOAT16_MAX.
    constants.opaqueDiffuseLobeSamplingProbabilityZeroThreshold =
      glm::packHalf1x16(RtxOptions::Get()->getOpaqueDiffuseLobeSamplingProbabilityZeroThreshold());
    constants.minOpaqueDiffuseLobeSamplingProbability =
      glm::packHalf1x16(RtxOptions::Get()->getMinOpaqueDiffuseLobeSamplingProbability());
    constants.opaqueSpecularLobeSamplingProbabilityZeroThreshold =
      glm::packHalf1x16(RtxOptions::Get()->getOpaqueSpecularLobeSamplingProbabilityZeroThreshold());
    constants.minOpaqueSpecularLobeSamplingProbability =
      glm::packHalf1x16(RtxOptions::Get()->getMinOpaqueSpecularLobeSamplingProbability());
    constants.opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold =
      glm::packHalf1x16(RtxOptions::Get()->getOpaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold());
    constants.minOpaqueOpacityTransmissionLobeSamplingProbability =
      glm::packHalf1x16(RtxOptions::Get()->getMinOpaqueOpacityTransmissionLobeSamplingProbability());
    constants.translucentSpecularLobeSamplingProbabilityZeroThreshold =
      glm::packHalf1x16(RtxOptions::Get()->getTranslucentSpecularLobeSamplingProbabilityZeroThreshold());
    constants.minTranslucentSpecularLobeSamplingProbability =
      glm::packHalf1x16(RtxOptions::Get()->getMinTranslucentSpecularLobeSamplingProbability());
    constants.translucentTransmissionLobeSamplingProbabilityZeroThreshold =
      glm::packHalf1x16(RtxOptions::Get()->getTranslucentTransmissionLobeSamplingProbabilityZeroThreshold());
    constants.minTranslucentTransmissionLobeSamplingProbability =
      glm::packHalf1x16(RtxOptions::Get()->getMinTranslucentTransmissionLobeSamplingProbability());
    constants.indirectRaySpreadAngleFactor = RtxOptions::Get()->getIndirectRaySpreadAngleFactor();

    // Note: Emissibe blend override emissive intensity always clamped to FLOAT16_MAX, so this packing is fine.
    constants.emissiveBlendOverrideEmissiveIntensity = glm::packHalf1x16(RtxOptions::Get()->getEmissiveBlendOverrideEmissiveIntensity());
    constants.emissiveIntensity = glm::packHalf1x16(RtxOptions::Get()->emissiveIntensity());
    constants.particleSoftnessFactor = glm::packHalf1x16(RtxOptions::Get()->getParticleSoftnessFactor());

    constants.psrrMaxBounces = RtxOptions::Get()->getPSRRMaxBounces();
    constants.pstrMaxBounces = RtxOptions::Get()->getPSTRMaxBounces();

    auto& rtxdi = m_common->metaRtxdiRayQuery();
    constants.enableEmissiveBlendEmissiveOverride = RtxOptions::Get()->isEmissiveBlendEmissiveOverrideEnabled();
    constants.enableRtxdi = RtxOptions::Get()->useRTXDI();
    constants.enableSecondaryBounces = RtxOptions::Get()->isSecondaryBouncesEnabled();
    constants.enableSeparatedDenoisers = RtxOptions::Get()->isSeparatedDenoiserEnabled();
    constants.enableCalculateVirtualShadingNormals = RtxOptions::Get()->isUseVirtualShadingNormalsForDenoisingEnabled();
    constants.enableViewModelVirtualInstances = RtxOptions::Get()->isViewModelVirtualInstancesEnabled();
    constants.enablePSRR = RtxOptions::Get()->isPSRREnabled();
    constants.enablePSTR = RtxOptions::Get()->isPSTREnabled();
    constants.enablePSTROutgoingSplitApproximation = RtxOptions::Get()->isPSTROutgoingSplitApproximationEnabled();
    constants.enablePSTRSecondaryIncidentSplitApproximation = RtxOptions::Get()->isPSTRSecondaryIncidentSplitApproximationEnabled();
    constants.psrrNormalDetailThreshold = RtxOptions::Get()->psrrNormalDetailThreshold();
    constants.pstrNormalDetailThreshold = RtxOptions::Get()->pstrNormalDetailThreshold();
    constants.enableDirectLighting = RtxOptions::Get()->isDirectLightingEnabled();
    constants.enableStochasticAlphaBlend = m_common->metaComposite().enableStochasticAlphaBlend();
    constants.enableSeparateUnorderedApproximations = RtxOptions::Get()->isSeparateUnorderedApproximationsEnabled() && getResourceManager().getTLAS(Tlas::Unordered).accelStructure != nullptr;
    constants.enableDirectTranslucentShadows = RtxOptions::Get()->areDirectTranslucentShadowsEnabled();
    constants.enableIndirectTranslucentShadows = RtxOptions::Get()->areIndirectTranslucentShadowsEnabled();
    constants.enableRussianRoulette = RtxOptions::Get()->isRussianRouletteEnabled();
    constants.enableDemodulateRoughness = m_common->metaDemodulate().demodulateRoughness();
    constants.enableReplaceDirectSpecularHitTWithIndirectSpecularHitT = RtxOptions::Get()->isReplaceDirectSpecularHitTWithIndirectSpecularHitTEnabled();
    constants.enablePortalFadeInEffect = RtxOptions::Get()->isPortalFadeInEffectEnabled();
    constants.enableEnhanceBSDFDetail = (shouldUseDLSS() || shouldUseTAA()) && m_common->metaComposite().enableDLSSEnhancement();
    constants.enhanceBSDFIndirectMode = (uint32_t)m_common->metaComposite().dlssEnhancementMode();
    constants.enhanceBSDFDirectLightPower = m_common->metaComposite().dlssEnhancementDirectLightPower();
    constants.enhanceBSDFIndirectLightPower = m_common->metaComposite().dlssEnhancementIndirectLightPower();
    constants.enhanceBSDFDirectLightMaxValue = m_common->metaComposite().dlssEnhancementDirectLightMaxValue();
    constants.enhanceBSDFIndirectLightMaxValue = m_common->metaComposite().dlssEnhancementIndirectLightMaxValue();
    constants.enhanceBSDFIndirectLightMinRoughness = m_common->metaComposite().dlssEnhancementIndirectLightMinRoughness();
    constants.enableFirstBounceLobeProbabilityDithering = RtxOptions::Get()->isFirstBounceLobeProbabilityDitheringEnabled();
    constants.enableUnorderedResolveInIndirectRays = RtxOptions::Get()->isUnorderedResolveInIndirectRaysEnabled();
    constants.enableUnorderedEmissiveParticlesInIndirectRays = RtxOptions::Get()->enableUnorderedEmissiveParticlesInIndirectRays();
    constants.enableDecalMaterialBlending = RtxOptions::Get()->isDecalMaterialBlendingEnabled();
    constants.enableBillboardOrientationCorrection = RtxOptions::Get()->enableBillboardOrientationCorrection() && RtxOptions::Get()->enableSeparateUnorderedApproximations();
    constants.useIntersectionBillboardsOnPrimaryRays = RtxOptions::Get()->useIntersectionBillboardsOnPrimaryRays() && constants.enableBillboardOrientationCorrection;
    constants.enableDirectLightBoilingFilter = m_common->metaDemodulate().enableDirectLightBoilingFilter() && RtxOptions::Get()->useRTXDI();
    constants.directLightBoilingThreshold = m_common->metaDemodulate().directLightBoilingThreshold();
    constants.translucentDecalAlbedoFactor = RtxOptions::Get()->getTranslucentDecalAlbedoFactor();
    constants.enablePlayerModelInPrimarySpace = RtxOptions::Get()->playerModel.enableInPrimarySpace();
    constants.enablePlayerModelPrimaryShadows = RtxOptions::Get()->playerModel.enablePrimaryShadows();
    constants.enablePreviousTLAS = RtxOptions::Get()->enablePreviousTLAS() && m_common->getSceneManager().isPreviousFrameSceneAvailable();

    constants.terrainArgs = getSceneManager().getTerrainBaker().getTerrainArgs();

    auto& restirGI = m_common->metaReSTIRGIRayQuery();
    constants.enableReSTIRGI = restirGI.shouldDispatch();
    constants.enableReSTIRGITemporalReuse = restirGI.useTemporalReuse();
    constants.enableReSTIRGISpatialReuse = restirGI.useSpatialReuse();
    constants.reSTIRGIMISMode = (uint32_t)restirGI.misMode();
    constants.enableReSTIRGIFinalVisibility = restirGI.useFinalVisibility();
    constants.enableReSTIRGIReflectionReprojection = restirGI.useReflectionReprojection();
    constants.restirGIReflectionMinParallax = restirGI.reflectionMinParallax();
    constants.enableReSTIRGIVirtualSample = restirGI.useVirtualSample();
    constants.reSTIRGIMISModePairwiseMISCentralWeight = restirGI.pairwiseMISCentralWeight();
    constants.reSTIRGIVirtualSampleLuminanceThreshold = restirGI.virtualSampleLuminanceThreshold();
    constants.reSTIRGIVirtualSampleRoughnessThreshold = restirGI.virtualSampleRoughnessThreshold();
    constants.reSTIRGIVirtualSampleSpecularThreshold = restirGI.virtualSampleSpecularThreshold();
    constants.reSTIRGIBiasCorrectionMode = (uint32_t) restirGI.biasCorrectionMode();
    constants.enableReSTIRGIPermutationSampling = restirGI.usePermutationSampling();
    constants.enableReSTIRGISampleStealing = (uint32_t)restirGI.useSampleStealing();
    constants.enableReSTIRGIStealBoundaryPixelSamplesWhenOutsideOfScreen = (uint32_t)restirGI.stealBoundaryPixelSamplesWhenOutsideOfScreen();
    constants.enableReSTIRGIBoilingFilter = restirGI.useBoilingFilter();
    constants.boilingFilterLowerThreshold = restirGI.boilingFilterMinThreshold();
    constants.boilingFilterHigherThreshold = restirGI.boilingFilterMaxThreshold();
    constants.boilingFilterRemoveReservoirThreshold = restirGI.boilingFilterRemoveReservoirThreshold();
    constants.temporalHistoryLength = restirGI.getTemporalHistoryLength(frameTimeSecs * 1000.0);
    constants.permutationSamplingSize = restirGI.permutationSamplingSize();
    constants.enableReSTIRGITemporalBiasCorrection = restirGI.useTemporalBiasCorrection();
    constants.enableReSTIRGIDiscardEnlargedPixels = restirGI.useDiscardEnlargedPixels();
    constants.enableReSTIRGITemporalJacobian = restirGI.useTemporalJacobian();
    constants.reSTIRGIFireflyThreshold = restirGI.fireflyThreshold();
    constants.reSTIRGIRoughnessClamp = restirGI.roughnessClamp();
    constants.reSTIRGIMISRoughness = restirGI.misRoughness();
    constants.reSTIRGIMISParallaxAmount = restirGI.parallaxAmount();
    constants.enableReSTIRGIDemodulatedTargetFunction = restirGI.useDemodulatedTargetFunction();


    m_common->metaNeeCache().setRaytraceArgs(constants);
    constants.surfaceCount = getSceneManager().getAccelManager().getSurfaceCount();

    auto* cameraTeleportDirectionInfo = getSceneManager().getRayPortalManager().getCameraTeleportationRayPortalDirectionInfo();
    constants.teleportationPortalIndex = cameraTeleportDirectionInfo ? cameraTeleportDirectionInfo->entryPortalInfo.portalIndex + 1 : 0;

    // Note: Use half of the vertical FoV for the main camera in radians divided by the vertical resolution to get the effective half angle of a single pixel.
    constants.screenSpacePixelSpreadHalfAngle = getSceneManager().getCamera().getFov() / 2.0f / constants.camera.resolution.y;

    // Note: This value is assumed to be positive (specifically not have the sign bit set) as otherwise it will break Ray Interaction encoding.
    assert(std::signbit(constants.screenSpacePixelSpreadHalfAngle) == false);

    // Debug View
    {
      const DebugView& debugView = m_common->metaDebugView();
      constants.debugView = debugView.debugViewIdx();
      constants.debugKnob = debugView.debugKnob();
      
      constants.gpuPrintThreadIndex = u16vec2 { kInvalidThreadIndex, kInvalidThreadIndex };
      constants.gpuPrintElementIndex = frameIdx % kMaxFramesInFlight;
     
      if (debugView.gpuPrint.enable() && ImGui::IsKeyDown(ImGuiKey_ModCtrl)) {
        if (debugView.gpuPrint.useMousePosition()) {
          Vector2 toDownscaledExtentScale = {
            downscaledExtent.width / static_cast<float>(targetExtent.width),
            downscaledExtent.height / static_cast<float>(targetExtent.height)
          };

          const ImVec2 mousePos = ImGui::GetMousePos();
          constants.gpuPrintThreadIndex = u16vec2 {
            static_cast<uint16_t>(mousePos.x * toDownscaledExtentScale.x),
            static_cast<uint16_t>(mousePos.y * toDownscaledExtentScale.y)
          };
        } else {
          constants.gpuPrintThreadIndex = u16vec2 {
            static_cast<uint16_t>(debugView.gpuPrint.pixelIndex().x), 
            static_cast<uint16_t>(debugView.gpuPrint.pixelIndex().y) 
          };
        }
      }
    }

    getDenoiseArgs(constants.primaryDirectNrd, constants.primaryIndirectNrd, constants.secondaryCombinedNrd);

    RayPortalManager::SceneData portalData = getSceneManager().getRayPortalManager().getRayPortalInfoSceneData();
    constants.numActiveRayPortals = portalData.numActiveRayPortals;
    constants.virtualInstancePortalIndex = getSceneManager().getInstanceManager().getVirtualInstancePortalIndex() & 0xff;

    memcpy(&constants.rayPortalHitInfos[0], &portalData.rayPortalHitInfos, sizeof(portalData.rayPortalHitInfos));
    memcpy(&constants.rayPortalHitInfos[maxRayPortalCount], &portalData.previousRayPortalHitInfos, sizeof(portalData.previousRayPortalHitInfos));

    constants.uniformRandomNumber = jenkinsHash(constants.frameIdx);
    constants.vertexColorStrength = RtxOptions::Get()->vertexColorStrength();
    constants.viewModelRayTMax = RtxOptions::Get()->getViewModelRangeMeters() * RtxOptions::Get()->getMeterToWorldUnitScale();
    constants.roughnessDemodulationOffset = m_common->metaDemodulate().demodulateRoughnessOffset();

    constants.volumeArgs = getSceneManager().getVolumeManager().getVolumeArgs(cameraManager, 
      rtOutput.m_froxelVolumeExtent, rtOutput.m_numFroxelVolumes, getSceneManager().getFogState(), enablePortalVolumes);
    RtxOptions::Get()->opaqueMaterialOptions.fillShaderParams(constants.opaqueMaterialArgs);
    RtxOptions::Get()->translucentMaterialOptions.fillShaderParams(constants.translucentMaterialArgs);
    RtxOptions::Get()->viewDistanceOptions.fillShaderParams(constants.viewDistanceArgs, RtxOptions::Get()->getMeterToWorldUnitScale());

    // We are going to use this value to perform some animations on GPU, to mitigate precision related issues loop time every 24 hours.
    const uint32_t kOneDayMS = 24 * 60 * 60 * 1000;
    constants.timeSinceStartMS = getSceneManager().getGameTimeSinceStartMS() % kOneDayMS;

    m_common->metaRtxdiRayQuery().setRaytraceArgs(rtOutput);
    getSceneManager().getLightManager().setRaytraceArgs(
      constants,
      m_common->metaRtxdiRayQuery().initialSampleCount(),
      RtxOptions::Get()->volumetricInitialRISSampleCount(),
      RtxOptions::Get()->getRISLightSampleCount());

    constants.resolveTransparencyThreshold = RtxOptions::Get()->getResolveTransparencyThreshold();
    constants.resolveOpaquenessThreshold = RtxOptions::Get()->getResolveOpaquenessThreshold();
    constants.resolveStochasticAlphaBlendThreshold = m_common->metaComposite().stochasticAlphaBlendOpacityThreshold();

    constants.volumeClampedReprojectionConfidencePenalty = RtxOptions::Get()->getVolumetricClampedReprojectionConfidencePenalty();

    constants.skyBrightness = RtxOptions::Get()->skyBrightness();
    constants.isLastCompositeOutputValid = rtOutput.m_lastCompositeOutput.matchesWriteFrameIdx(frameIdx - 1);
    constants.isZUp = RtxOptions::Get()->isZUp();
    constants.enableCullingSecondaryRays = RtxOptions::Get()->enableCullingInSecondaryRays();

    // Upload the constants to the GPU
    {
      Rc<DxvkBuffer> cb = getResourceManager().getConstantsBuffer();

      updateBuffer(cb, 0, sizeof(constants), &constants);

      cmdList->trackResource<DxvkAccess::Read>(cb);
    }
  }

  void RtxContext::bindCommonRayTracingResources(const Resources::RaytracingOutput& rtOutput) {
    ScopedCpuProfileZone();
    Rc<DxvkBuffer> constantsBuffer = getResourceManager().getConstantsBuffer();
    Rc<DxvkBuffer> surfaceBuffer = getSceneManager().getSurfaceBuffer();
    Rc<DxvkBuffer> surfaceMappingBuffer = getSceneManager().getSurfaceMappingBuffer();
    Rc<DxvkBuffer> billboardsBuffer = getSceneManager().getBillboardsBuffer();
    Rc<DxvkBuffer> surfaceMaterialBuffer = getSceneManager().getSurfaceMaterialBuffer();
    Rc<DxvkBuffer> volumeMaterialBuffer = getSceneManager().getVolumeMaterialBuffer();
    Rc<DxvkBuffer> lightBuffer = getSceneManager().getLightManager().getLightBuffer();
    Rc<DxvkBuffer> previousLightBuffer = getSceneManager().getLightManager().getPreviousLightBuffer();
    Rc<DxvkBuffer> lightMappingBuffer = getSceneManager().getLightManager().getLightMappingBuffer();
    Rc<DxvkBuffer> gpuPrintBuffer = getResourceManager().getRaytracingOutput().m_gpuPrintBuffer;

    DebugView& debugView = getCommonObjects()->metaDebugView();

    bindAccelerationStructure(BINDING_ACCELERATION_STRUCTURE, getResourceManager().getTLAS(Tlas::Opaque).accelStructure);
    bindAccelerationStructure(BINDING_ACCELERATION_STRUCTURE_PREVIOUS, getResourceManager().getTLAS(Tlas::Opaque).previousAccelStructure.ptr() ? getResourceManager().getTLAS(Tlas::Opaque).previousAccelStructure : getResourceManager().getTLAS(Tlas::Opaque).accelStructure);
    bindAccelerationStructure(BINDING_ACCELERATION_STRUCTURE_UNORDERED, getResourceManager().getTLAS(Tlas::Unordered).accelStructure);
    bindResourceBuffer(BINDING_SURFACE_DATA_BUFFER, DxvkBufferSlice(surfaceBuffer, 0, surfaceBuffer->info().size));
    bindResourceBuffer(BINDING_SURFACE_MAPPING_BUFFER, DxvkBufferSlice(surfaceMappingBuffer, 0, surfaceMappingBuffer.ptr() ? surfaceMappingBuffer->info().size : 0));
    bindResourceBuffer(BINDING_SURFACE_MATERIAL_DATA_BUFFER, DxvkBufferSlice(surfaceMaterialBuffer, 0, surfaceMaterialBuffer->info().size));
    bindResourceBuffer(BINDING_VOLUME_MATERIAL_DATA_BUFFER, volumeMaterialBuffer.ptr() ? DxvkBufferSlice(volumeMaterialBuffer, 0, volumeMaterialBuffer->info().size) : DxvkBufferSlice());
    bindResourceBuffer(BINDING_LIGHT_DATA_BUFFER, DxvkBufferSlice(lightBuffer, 0, lightBuffer.ptr() ? lightBuffer->info().size : 0));
    bindResourceBuffer(BINDING_PREVIOUS_LIGHT_DATA_BUFFER, DxvkBufferSlice(previousLightBuffer, 0, previousLightBuffer.ptr() ? previousLightBuffer->info().size : 0));
    bindResourceBuffer(BINDING_LIGHT_MAPPING, DxvkBufferSlice(lightMappingBuffer, 0, lightMappingBuffer.ptr() ? lightMappingBuffer->info().size : 0));
    bindResourceBuffer(BINDING_BILLBOARDS_BUFFER, DxvkBufferSlice(billboardsBuffer, 0, billboardsBuffer.ptr() ? billboardsBuffer->info().size : 0));
    bindResourceView(BINDING_BLUE_NOISE_TEXTURE, getResourceManager().getBlueNoiseTexture(this), nullptr);
    bindResourceBuffer(BINDING_CONSTANTS, DxvkBufferSlice(constantsBuffer, 0, constantsBuffer->info().size));
    bindResourceView(BINDING_DEBUG_VIEW_TEXTURE, debugView.getDebugOutput(), nullptr);
    bindResourceBuffer(BINDING_GPU_PRINT_BUFFER, DxvkBufferSlice(gpuPrintBuffer, 0, gpuPrintBuffer.ptr() ? gpuPrintBuffer->info().size : 0));
  }

  void RtxContext::checkOpacityMicromapSupport() {
    bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(m_device);

    RtxOptions::Get()->setIsOpacityMicromapSupported(isOpacityMicromapSupported);

    Logger::info(str::format("[RTX info] Opacity Micromap: ", isOpacityMicromapSupported ? "supported" : "not supported"));
  }

  bool RtxContext::checkIsShaderExecutionReorderingSupported(Rc<DxvkDevice> device) {
    const bool allowSER = RtxOptions::Get()->isShaderExecutionReorderingSupported();

    if (!allowSER) {
      return false;
    }

    // SER Extension support check
    const bool isSERExtensionSupported = device->extensions().nvRayTracingInvocationReorder;
    const bool isSERReorderingEnabled =
      VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_NV == device->properties().nvRayTracingInvocationReorderProperties.rayTracingInvocationReorderReorderingHint;
      
    return isSERExtensionSupported && isSERReorderingEnabled;
  }

  bool RtxContext::shouldBakeSky(const DrawCallState& drawCallState) {
    const XXH64_hash_t colorTextureHash = drawCallState.getMaterialData().colorTextures[0].getImageHash();

    // NOTE: we use color texture hash for sky detection, however the replacement is hashed with
    // the whole legacy material hash (which, as of 12/9/2022, equals to color texture hash). Adding a check just in case.
    assert(colorTextureHash == drawCallState.getMaterialData().getHash() && "Texture or material hash method changed!");

    if (drawCallState.getMaterialData().usesTexture()) {
      if (!RtxOptions::Get()->isSkyboxTexture(colorTextureHash)) {
        return false;
      }
    } else {
      if (drawCallState.drawCallID >= RtxOptions::Get()->skyDrawcallIdThreshold()) {
        return false;
      }
    }

    return true;
  }

  bool RtxContext::shouldBakeTerrain(const DrawCallState& drawCallState) {
    if (!TerrainBaker::needsTerrainBaking())
      return false;

    // Check if the hash is marked as a terrain texture
    const XXH64_hash_t colorTextureHash = drawCallState.getMaterialData().colorTextures[0].getImageHash();
    if (!(colorTextureHash && RtxOptions::Get()->isTerrainTexture(colorTextureHash))) {
      return false;
    }

    return true;
  }

  void RtxContext::checkShaderExecutionReorderingSupport() {
    const bool isSERSupported = checkIsShaderExecutionReorderingSupported(m_device);
    
    RtxOptions::Get()->setIsShaderExecutionReorderingSupported(isSERSupported);

    const VkPhysicalDeviceProperties& props = m_device->adapter()->deviceProperties();
    const NV_GPU_ARCHITECTURE_ID archId = RtxOptions::Get()->getNvidiaArch();

    Logger::info(str::format("[RTX info] Shader Execution Reordering: ", isSERSupported ? "supported" : "not supported"));

    bool isShaderExecutionReorderingEnabled = RtxOptions::Get()->isShaderExecutionReorderingInPathtracerGbufferEnabled() ||
      RtxOptions::Get()->isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled();

    Logger::info(str::format("[RTX info] Shader Execution Reordering: ", isShaderExecutionReorderingEnabled ? "enabled" : "disabled"));
  }

  void RtxContext::dispatchVolumetrics(const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(this, "Volumetrics");

    // Volume Raytracing
    {
      auto& volumeIntegrate = m_common->metaVolumeIntegrate();
      volumeIntegrate.dispatch(this, rtOutput, rtOutput.m_raytraceArgs.volumeArgs.numActiveFroxelVolumes);
    }

    // Volume Filtering
    {
      auto& volumeFilter = m_common->metaVolumeFilter();
      volumeFilter.dispatch(this, rtOutput, rtOutput.m_raytraceArgs.volumeArgs.numActiveFroxelVolumes);
    }

    // Volume Preintegration
    // Note: Volume preintegration only needed when volumetric lighting is needed. Otherwise only the integration and filtering are needed for
    // particles and things leveraging the volume radiance cache.
    if (RtxOptions::Get()->isVolumetricLightingEnabled()) {
      auto& volumePreintegrate = m_common->metaVolumePreintegrate();
      volumePreintegrate.dispatch(this, rtOutput, rtOutput.m_raytraceArgs.volumeArgs.numActiveFroxelVolumes);
    }
  }

  void RtxContext::dispatchIntegrate(const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(this, "Integrate Raytracing");
    
    m_common->metaPathtracerIntegrateDirect().dispatch(this, rtOutput);
    m_common->metaPathtracerIntegrateIndirect().dispatch(this, rtOutput);
    m_common->metaPathtracerIntegrateIndirect().dispatchNEE(this, rtOutput);
  }
  
  void RtxContext::dispatchDemodulate(const Resources::RaytracingOutput& rtOutput) {
    ScopedCpuProfileZone();
    DemodulatePass& demodulate = m_common->metaDemodulate();
    demodulate.dispatch(this, rtOutput);
  }

  void RtxContext::dispatchNeeCache(const Resources::RaytracingOutput& rtOutput) {
    NeeCachePass& neeCache = m_common->metaNeeCache();
    neeCache.dispatch(this, rtOutput);
  }
  
  void RtxContext::dispatchReferenceDenoise(const Resources::RaytracingOutput& rtOutput, float frameTimeSecs) {
    if (!RtxOptions::Get()->isDenoiserEnabled() || !RtxOptions::Get()->useDenoiserReferenceMode())
      return;

    DxvkDenoise& denoiser = m_common->metaReferenceDenoiser();
    ScopedGpuProfileZone(this, "Reference");
    const Resources::Resource& compositeInputOutput = rtOutput.m_compositeOutput.resource(Resources::AccessType::ReadWrite);

    DxvkDenoise::Input denoiseInput = {};
    denoiseInput.reference = &compositeInputOutput;
    denoiseInput.diffuse_hitT = denoiseInput.specular_hitT = nullptr;
    // Note: Primary input data used for reference path due to its coherency, not that I think this matters though since it is not doing any denoising.
    denoiseInput.normal_roughness = &rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
    denoiseInput.linearViewZ = &rtOutput.m_primaryLinearViewZ;
    denoiseInput.motionVector = &rtOutput.m_primaryVirtualMotionVector;
    denoiseInput.frameTimeMs = frameTimeSecs * 1000.f;
    denoiseInput.reset = m_resetHistory;

    DxvkDenoise::Output denoiseOutput;
    denoiseOutput.reference = &compositeInputOutput;
    denoiseInput.diffuse_hitT = denoiseInput.specular_hitT = nullptr;

    // Since NRDContext doesn't use DxvkContext abstraction
    // but its using Compute, mark its DxvkContext's Cp pipelines as dirty
    {
      this->spillRenderPass(false);
      m_flags.set(
        DxvkContextFlag::CpDirtyPipeline,
        DxvkContextFlag::CpDirtyPipelineState,
        DxvkContextFlag::CpDirtyResources,
        DxvkContextFlag::CpDirtyDescriptorBinding);
    }

    denoiser.dispatch(m_cmd, m_device, this, m_execBarriers, rtOutput, denoiseInput, denoiseOutput);
  }

  void RtxContext::dispatchDenoise(const Resources::RaytracingOutput& rtOutput, float frameTimeSecs) {
    if (!RtxOptions::Get()->isDenoiserEnabled() || RtxOptions::Get()->useDenoiserReferenceMode())
      return;

    ScopedGpuProfileZone(this, "Denoising");

    auto runDenoising = [&](DxvkDenoise& denoiser, DxvkDenoise& secondLobeReferenceDenoiser, DxvkDenoise::Input& denoiseInput, DxvkDenoise::Output& denoiseOutput) {
      // Since NRDContext doesn't use DxvkContext abstraction
      // but its using Compute, mark its DxvkContext's Cp pipelines as dirty
      {
        this->spillRenderPass(false);
        m_flags.set(
          DxvkContextFlag::CpDirtyPipeline,
          DxvkContextFlag::CpDirtyPipelineState,
          DxvkContextFlag::CpDirtyResources,
          DxvkContextFlag::CpDirtyDescriptorBinding);
      }

      // Need to run the denoiser twice for diffuse and specular when reference denoising is enabled on non-combined inputs
      if (denoiser.isReferenceDenoiserEnabled()) {
        denoiseInput.reference = denoiseInput.diffuse_hitT;
        denoiseOutput.reference = denoiseOutput.diffuse_hitT;
        denoiser.dispatch(m_cmd, m_device, this, m_execBarriers, rtOutput, denoiseInput, denoiseOutput);

        // Reference denoiser accumulates internally, so the second signal has to be denoised through a separate reference denoiser
        secondLobeReferenceDenoiser.copyNrdSettingsFrom(denoiser);
        denoiseInput.reference = denoiseInput.specular_hitT;
        denoiseOutput.reference = denoiseOutput.specular_hitT;
        secondLobeReferenceDenoiser.dispatch(m_cmd, m_device, this, m_execBarriers, rtOutput, denoiseInput, denoiseOutput);
      } else
        denoiser.dispatch(m_cmd, m_device, this, m_execBarriers, rtOutput, denoiseInput, denoiseOutput);
    };

    // Primary direct denoiser used for primary direct lighting when separated, otherwise a special combined direct+indirect denoiser is used when both direct and indirect signals are combined.
    DxvkDenoise& denoiser0 = RtxOptions::Get()->isSeparatedDenoiserEnabled() ? m_common->metaPrimaryDirectLightDenoiser() : m_common->metaPrimaryCombinedLightDenoiser();
    DxvkDenoise& referenceDenoiserSecondLobe0 = m_common->metaReferenceDenoiserSecondLobe0();
    // Primary Indirect denoiser used for primary indirect lighting when separated.
    DxvkDenoise& denoiser1 = m_common->metaPrimaryIndirectLightDenoiser();
    DxvkDenoise& referenceDenoiserSecondLobe1 = m_common->metaReferenceDenoiserSecondLobe1();
    // Secondary combined denoiser always used for secondary lighting.
    DxvkDenoise& denoiser2 = m_common->metaSecondaryCombinedLightDenoiser();
    DxvkDenoise& referenceDenoiserSecondLobe2 = m_common->metaReferenceDenoiserSecondLobe2();

    // Primary Direct light denoiser
    {
      ScopedGpuProfileZone(this, "Primary Direct Denoising");
      
      DxvkDenoise::Input denoiseInput = {};
      denoiseInput.diffuse_hitT = &rtOutput.m_primaryDirectDiffuseRadiance.resource(Resources::AccessType::Read);
      denoiseInput.specular_hitT = &rtOutput.m_primaryDirectSpecularRadiance.resource(Resources::AccessType::Read);
      denoiseInput.normal_roughness = &rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      denoiseInput.linearViewZ = &rtOutput.m_primaryLinearViewZ;
      denoiseInput.motionVector = &rtOutput.m_primaryVirtualMotionVector;
      denoiseInput.disocclusionThresholdMix = &rtOutput.m_primaryDisocclusionThresholdMix;
      denoiseInput.frameTimeMs = frameTimeSecs * 1000.f;
      denoiseInput.reset = m_resetHistory;

      if(RtxOptions::Get()->useRTXDI() && m_common->metaRtxdiRayQuery().getEnableDenoiserConfidence())
        denoiseInput.confidence = &rtOutput.getCurrentRtxdiConfidence().resource(Resources::AccessType::Read);

      DxvkDenoise::Output denoiseOutput;
      denoiseOutput.diffuse_hitT = &rtOutput.m_primaryDirectDiffuseRadiance.resource(Resources::AccessType::Write);
      denoiseOutput.specular_hitT = &rtOutput.m_primaryDirectSpecularRadiance.resource(Resources::AccessType::Write);

      runDenoising(denoiser0, referenceDenoiserSecondLobe0, denoiseInput, denoiseOutput);
    }

    // Primary Indirect light denoiser, if separate denoiser is used.
    if (RtxOptions::Get()->isSeparatedDenoiserEnabled())
    {
      ScopedGpuProfileZone(this, "Primary Indirect Denoising");

      DxvkDenoise::Input denoiseInput = {};
      denoiseInput.diffuse_hitT = &rtOutput.m_primaryIndirectDiffuseRadiance.resource(Resources::AccessType::Read);
      denoiseInput.specular_hitT = &rtOutput.m_primaryIndirectSpecularRadiance.resource(Resources::AccessType::Read);
      denoiseInput.normal_roughness = &rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      denoiseInput.linearViewZ = &rtOutput.m_primaryLinearViewZ;
      denoiseInput.motionVector = &rtOutput.m_primaryVirtualMotionVector;
      denoiseInput.disocclusionThresholdMix = &rtOutput.m_primaryDisocclusionThresholdMix;
      denoiseInput.frameTimeMs = frameTimeSecs * 1000.f;
      denoiseInput.reset = m_resetHistory;

      DxvkDenoise::Output denoiseOutput;
      denoiseOutput.diffuse_hitT = &rtOutput.m_primaryIndirectDiffuseRadiance.resource(Resources::AccessType::Write);
      denoiseOutput.specular_hitT = &rtOutput.m_primaryIndirectSpecularRadiance.resource(Resources::AccessType::Write);

      runDenoising(denoiser1, referenceDenoiserSecondLobe1, denoiseInput, denoiseOutput);
    }

    // Secondary Combined light denoiser
    {
      ScopedGpuProfileZone(this, "Secondary Combined Denoising");

      DxvkDenoise::Input denoiseInput = {};
      denoiseInput.diffuse_hitT = &rtOutput.m_secondaryCombinedDiffuseRadiance.resource(Resources::AccessType::Read);
      denoiseInput.specular_hitT = &rtOutput.m_secondaryCombinedSpecularRadiance.resource(Resources::AccessType::Read);
      denoiseInput.normal_roughness = &rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      denoiseInput.linearViewZ = &rtOutput.m_secondaryLinearViewZ;
      denoiseInput.motionVector = &rtOutput.m_secondaryVirtualMotionVector.resource(Resources::AccessType::Read);
      denoiseInput.frameTimeMs = frameTimeSecs * 1000.f;
      denoiseInput.reset = m_resetHistory;

      DxvkDenoise::Output denoiseOutput;
      denoiseOutput.diffuse_hitT = &rtOutput.m_secondaryCombinedDiffuseRadiance.resource(Resources::AccessType::Write);
      denoiseOutput.specular_hitT = &rtOutput.m_secondaryCombinedSpecularRadiance.resource(Resources::AccessType::Write);

      runDenoising(denoiser2, referenceDenoiserSecondLobe2, denoiseInput, denoiseOutput);
    }
  }

  void RtxContext::dispatchDLSS(const Resources::RaytracingOutput& rtOutput) {
    DxvkDLSS& dlss = m_common->metaDLSS();

    dlss.dispatch(m_cmd, m_device, this, m_execBarriers, rtOutput, m_resetHistory);
  }

  void RtxContext::dispatchNIS(const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(this, "NIS");
    m_common->metaNIS().dispatch(this, rtOutput);
  }

  void RtxContext::dispatchTemporalAA(const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(this, "TAA");

    DxvkTemporalAA& taa = m_common->metaTAA();
    RtCamera& mainCamera = getSceneManager().getCamera();

    if (shouldUseTAA() && !mainCamera.isCameraCut() && taa.shouldDispatch()) {
      float jitterOffset[2];
      mainCamera.getJittering(jitterOffset);

      taa.dispatch(m_cmd, this,
        getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        mainCamera.getShaderConstants().resolution,
        jitterOffset,
        rtOutput.m_compositeOutput.resource(Resources::AccessType::Read),
        rtOutput.m_primaryScreenSpaceMotionVector,
        rtOutput.m_finalOutput,
        true);
    }
  }

  void RtxContext::dispatchComposite(const Resources::RaytracingOutput& rtOutput) {
    if (getSceneManager().getSurfaceBuffer() == nullptr) {
      return;
    }

    ScopedGpuProfileZone(this, "Composite");

    bool isNRDPreCompositionDenoiserEnabled = RtxOptions::Get()->isDenoiserEnabled() && !RtxOptions::Get()->useDenoiserReferenceMode();

    CompositePass::Settings settings;
    settings.fog = getSceneManager().getFogState();
    settings.isNRDPreCompositionDenoiserEnabled = isNRDPreCompositionDenoiserEnabled;
    settings.useUpscaler = shouldUseUpscaler();
    settings.useDLSS = shouldUseDLSS();
    settings.demodulateRoughness = m_common->metaDemodulate().demodulateRoughness();
    settings.roughnessDemodulationOffset = m_common->metaDemodulate().demodulateRoughnessOffset();
    m_common->metaComposite().dispatch(m_cmd, this,
      getSceneManager(),
      rtOutput, settings);
  }

  void RtxContext::dispatchToneMapping(const Resources::RaytracingOutput& rtOutput, bool performSRGBConversion, const float deltaTime) {
    ScopedCpuProfileZone();

    if (m_common->metaDebugView().debugViewIdx() == DEBUG_VIEW_PRE_TONEMAP_OUTPUT)
      return;

    // TODO: I think these are unnecessary, and/or should be automatically done within DXVK 
    this->spillRenderPass(false);
    this->unbindComputePipeline();

    float adjustedDeltaTime = deltaTime;
    if (NrdSettings::getTimeDeltaBetweenFrames() > 0) {
      adjustedDeltaTime = NrdSettings::getTimeDeltaBetweenFrames();
    }
    adjustedDeltaTime = std::max(0.f, adjustedDeltaTime);

    DxvkAutoExposure& autoExposure = m_common->metaAutoExposure();    
    autoExposure.dispatch(m_cmd, m_device, this, 
      getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER),
      rtOutput, adjustedDeltaTime, performSRGBConversion);

    // We don't reset history for tonemapper on m_resetHistory for easier comparison when toggling raytracing modes.
    // The tone curve shouldn't be too different between raytracing modes, 
    // but the reset of denoised buffers causes wide tone curve differences
    // until it converges and thus making comparison of raytracing mode outputs more difficult    
    if(RtxOptions::Get()->tonemappingMode() == TonemappingMode::Global) {
      DxvkToneMapping& toneMapper = m_common->metaToneMapping();
      toneMapper.dispatch(m_cmd, m_device, this, 
        getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER),
        autoExposure.getExposureTexture().view,
        rtOutput, adjustedDeltaTime, performSRGBConversion, autoExposure.enabled());
    }
    DxvkLocalToneMapping& localTonemapper = m_common->metaLocalToneMapping();
    if (localTonemapper.shouldDispatch()){
      localTonemapper.dispatch(m_cmd, m_device, this,
        getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        autoExposure.getExposureTexture().view,
        rtOutput, adjustedDeltaTime, performSRGBConversion, autoExposure.enabled());
    }
  }

  void RtxContext::dispatchBloom(const Resources::RaytracingOutput& rtOutput) {
    ScopedCpuProfileZone();
    DxvkBloom& bloom = m_common->metaBloom();
    if (!bloom.shouldDispatch())
      return;

    // TODO: just in case, because tonemapping does the same
    this->spillRenderPass(false);
    this->unbindComputePipeline();

    bloom.dispatch(m_cmd, this,
      getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      rtOutput.m_finalOutput);
  }

  void RtxContext::dispatchPostFx(Resources::RaytracingOutput& rtOutput) {
    ScopedCpuProfileZone();
    DxvkPostFx& postFx = m_common->metaPostFx();
    RtCamera& mainCamera = getSceneManager().getCamera();
    if (!postFx.enable()) {
      return;
    }

    postFx.dispatch(m_cmd, this,
      getResourceManager().getSampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      mainCamera.getShaderConstants().resolution,
      RtxOptions::Get()->getRngSeedWithFrameIndex() ? m_device->getCurrentFrameId() : 0,
      rtOutput,
      mainCamera.isCameraCut());
  }

  void RtxContext::dispatchDebugView(Rc<DxvkImage>& srcImage, const Resources::RaytracingOutput& rtOutput, bool captureScreenImage)  {
    ScopedCpuProfileZone();

    DebugView& debugView = m_common->metaDebugView();
    const RaytraceArgs& constants = rtOutput.m_raytraceArgs;
    const uint32_t frameIdx = m_device->getCurrentFrameId();

    if (debugView.gpuPrint.enable()) {
      // Read from the oldest element as it is guaranteed to be written on the GPU by now
      VkDeviceSize offset = ((frameIdx + 1) % kMaxFramesInFlight) * sizeof(GpuPrintBufferElement);
      GpuPrintBufferElement* gpuPrintElement = reinterpret_cast<GpuPrintBufferElement*>(rtOutput.m_gpuPrintBuffer->mapPtr(offset));

      if (gpuPrintElement && gpuPrintElement->isValid()) {
        static std::string previousString = "";
        const std::string newString = str::format("GPU print value [", gpuPrintElement->threadIndex.x, ", ", gpuPrintElement->threadIndex.y, "]: ", Config::generateOptionString(reinterpret_cast<Vector4&>(gpuPrintElement->writtenData)));

        // Avoid spamming the console with the same output
        if (newString != previousString) {
          previousString = newString;

          // Add additional info on which we don't want to differentiate when printing out
          const std::string fullInfoString = str::format("Frame: ", gpuPrintElement->frameIndex, " - ", newString);
          Logger::info(fullInfoString);
        }

        // Invalidate the element so that it's not reused
        gpuPrintElement->invalidate();
      }
    }

    if (!debugView.shouldDispatch())
      return;

    debugView.dispatch(m_cmd, this,
      getResourceManager().getSampler(VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
      srcImage, rtOutput, *m_common);

    if (captureScreenImage)
      takeScreenshot("rtxImageDebugView", debugView.getDebugOutput()->image());
  }

  void RtxContext::flushCommandList() {
    ScopedCpuProfileZone();

    DxvkContext::flushCommandList();

    getCommonObjects()->metaGeometryUtils().flushCommandList();
  }

  void RtxContext::updateComputeShaderResources() {
    ScopedCpuProfileZone();
    DxvkContext::updateComputeShaderResources();

    auto&& layout = m_state.cp.pipeline->layout();
    if (layout->requiresExtraDescriptorSet()) {
      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, layout->pipelineLayout(), 
                                  getSceneManager().getBindlessResourceManager().getGlobalBindlessTableSet(BindlessResourceManager::Textures),
                                  BINDING_SET_BINDLESS_TEXTURE2D);

      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, layout->pipelineLayout(), 
                                  getSceneManager().getBindlessResourceManager().getGlobalBindlessTableSet(BindlessResourceManager::Buffers),
                                  BINDING_SET_BINDLESS_RAW_BUFFER);
    }
  }

  void RtxContext::updateRaytracingShaderResources() {
    ScopedCpuProfileZone();
    DxvkContext::updateRaytracingShaderResources();

    auto&& layout = m_state.rp.pipeline->layout();
    if (layout->requiresExtraDescriptorSet()) {
      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout->pipelineLayout(), 
                                  getSceneManager().getBindlessResourceManager().getGlobalBindlessTableSet(BindlessResourceManager::Textures),
                                  BINDING_SET_BINDLESS_TEXTURE2D);

      m_cmd->cmdBindDescriptorSet(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout->pipelineLayout(), 
                                  getSceneManager().getBindlessResourceManager().getGlobalBindlessTableSet(BindlessResourceManager::Buffers),
                                  BINDING_SET_BINDLESS_RAW_BUFFER);
    }
  }

  bool RtxContext::shouldUseDLSS() const {
    return RtxOptions::Get()->isDLSSEnabled() && m_dlssSupported;
  }

  bool RtxContext::shouldUseNIS() const {
    return RtxOptions::Get()->isNISEnabled();
  }

  bool RtxContext::shouldUseTAA() const {
    return RtxOptions::Get()->isTAAEnabled();
  }

  D3D9RtxVertexCaptureData& RtxContext::allocAndMapVertexCaptureConstantBuffer() {
    DxvkBufferSliceHandle slice = m_rtState.vertexCaptureCB->allocSlice();
    invalidateBuffer(m_rtState.vertexCaptureCB, slice);

    return *static_cast<D3D9RtxVertexCaptureData*>(slice.mapPtr);
  }

  D3D9FixedFunctionVS& RtxContext::allocAndMapFixedFunctionConstantBuffer() {
    DxvkBufferSliceHandle slice = m_rtState.vsFixedFunctionCB->allocSlice();
    invalidateBuffer(m_rtState.vsFixedFunctionCB, slice);

    return *static_cast<D3D9FixedFunctionVS*>(slice.mapPtr);
  }

  void RtxContext::rasterizeToSkyMatte(const DrawParameters& params) {
    ScopedGpuProfileZone(this, "rasterizeToSkyMatte");

    auto skyMatteView = getResourceManager().getSkyMatte(this, m_skyColorFormat).view;
    const auto skyMatteExt = skyMatteView->mipLevelExtent(0);

    VkViewport viewport { 0.5f, static_cast<float>(skyMatteExt.height) + 0.5f,
      static_cast<float>(skyMatteExt.width),
      -static_cast<float>(skyMatteExt.height),
      0.f, 1.f
    };

    VkRect2D scissor {
      { 0, 0 },
      { skyMatteExt.width, skyMatteExt.height }
    };

    setViewports(1, &viewport, &scissor);

    DxvkRenderTargets skyRt;
    skyRt.color[0].view = getResourceManager().getCompatibleViewForView(skyMatteView, m_skyRtColorFormat);
    skyRt.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;

    bindRenderTargets(skyRt);

    if (m_skyClearDirty) {
      DxvkContext::clearRenderTarget(skyMatteView, VK_IMAGE_ASPECT_COLOR_BIT, m_skyClearValue);
    }

    if (params.indexCount == 0) {
      DxvkContext::draw(params.vertexCount, params.instanceCount, params.vertexOffset, 0);
    } else {
      DxvkContext::drawIndexed(params.indexCount, params.instanceCount, params.firstIndex, params.vertexOffset, 0);
    }
  }

  void RtxContext::initSkyProbe() {
    auto skyProbeImage = getResourceManager().getSkyProbe(this, m_skyColorFormat).image;

    if (m_skyProbeImage == skyProbeImage)
      return;

    m_skyProbeImage = skyProbeImage;

    DxvkImageViewCreateInfo viewInfo;
    viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_skyRtColorFormat;
    viewInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.minLevel = 0;
    viewInfo.numLevels = 1;
    viewInfo.minLayer = 0;
    viewInfo.numLayers = 1;

    for (uint32_t n = 0; n < 6; n++) {
      viewInfo.minLayer = n;
      m_skyProbeViews[n] = m_device->createImageView(m_skyProbeImage, viewInfo);
    }
  }

  void RtxContext::rasterizeToSkyProbe(const DrawParameters& params) {
    if (!m_rtState.vsFixedFunctionCB.ptr())
      return;

    static Vector3 targets[6] = {
      {+1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
      {0.0f, +1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
      {0.0f, 0.0f, +1.0f}, {0.0f, 0.0f, -1.0f},
    };

    static Vector3 ups[6] = {
      {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f,-1.0f}, {0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
    };

    ScopedGpuProfileZone(this, "rasterizeToSkyProbe");

    // Lazy init
    initSkyProbe();

    // Grab transforms
    
    const auto vs = *static_cast<D3D9FixedFunctionVS*>(m_rtState.vsFixedFunctionCB->mapPtr(0));

    // Save rasterizer state
    const auto ri = m_state.gp.state.rs;

    // Set cull mode to none
    DxvkRasterizerState newRs;
    newRs.depthClipEnable = ri.depthClipEnable();
    newRs.depthBiasEnable = ri.depthBiasEnable();
    newRs.polygonMode = ri.polygonMode();
    newRs.cullMode = VK_CULL_MODE_NONE;
    newRs.frontFace = ri.frontFace();
    newRs.sampleCount = ri.sampleCount();
    newRs.conservativeMode = ri.conservativeMode();
    setRasterizerState(newRs);

    // Figure out camera position
    const auto camPos = inverse(vs.View).data[3].xyz();

    // Create cube plane projection
    Matrix4 proj = vs.Projection;
    proj[0][0] = 1.f;
    proj[1][1] = 1.f;
    proj[2][2] = 1.f;
    proj[2][3] = 1.f;

    const auto& skyProbeExt = m_skyProbeImage->info().extent;

    VkViewport viewport { 0, static_cast<float>(skyProbeExt.height),
      static_cast<float>(skyProbeExt.width),
      -static_cast<float>(skyProbeExt.height),
      0.f, 1.f
    };

    VkRect2D scissor {
      { 0, 0 },
      { skyProbeExt.width, skyProbeExt.height }
    };

    setViewports(1, &viewport, &scissor);

    // Go over sky probe views and rasterize to each plane.
    // NOTE: Ideally sky probe should be rendered in single pass using
    // multiple views, however this would require multiview support
    // plumbing to dxvk side.
    // TODO: add multiview rendering in future.
    uint32_t plane = 0;
    for (auto& skyView : m_skyProbeViews) {
      // Push new state
      D3D9FixedFunctionVS& newState = allocAndMapFixedFunctionConstantBuffer();
      newState = vs;

      Matrix4 view;
      {
        const Vector3 z = normalize(targets[plane]);
        const Vector3 x = normalize(cross(ups[plane], z));
        const Vector3 y = cross(z, x);

        const Vector3 translation(dot(x, -camPos), dot(y, -camPos), dot(z, -camPos));

        view[0] = Vector4(x.x, y.x, z.x, 0.f);
        view[1] = Vector4(x.y, y.y, z.y, 0.f);
        view[2] = Vector4(x.z, y.z, z.z, 0.f);
        view[3] = Vector4(translation.x, translation.y, translation.z, 1.f);
      }

      newState.View = view;
      newState.WorldView = view * vs.World;
      newState.Projection = proj;

      DxvkRenderTargets skyRt;
      skyRt.color[0].view = skyView;
      skyRt.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;

      bindRenderTargets(skyRt);

      if (m_skyClearDirty) {
        DxvkContext::clearRenderTarget(skyView, VK_IMAGE_ASPECT_COLOR_BIT, m_skyClearValue);
      }

      if (params.indexCount == 0) {
        DxvkContext::draw(params.vertexCount, params.instanceCount, params.vertexOffset, 0);
      } else {
        DxvkContext::drawIndexed(params.indexCount, params.instanceCount, params.firstIndex, params.vertexOffset, 0);
      }

      ++plane;
    }

    // Restore rasterizer state
    newRs.cullMode = ri.cullMode();
    setRasterizerState(newRs);

    // Restore vertex state
    allocAndMapFixedFunctionConstantBuffer() = vs;
  }

  void RtxContext::bakeTerrain(const DrawParameters& params, DrawCallState& drawCallState) {
    if (!shouldBakeTerrain(drawCallState))
      return;

    DrawCallTransforms& transformData = drawCallState.transformData;

    Rc<DxvkImageView> previousColorView;

    if (!TerrainBaker::debugDisableBaking()) {
      // Grab and apply replacement texture if any
      // NOTE: only the original color texture will be replaced with albedo-opacity texture
      auto replacementMaterial = getSceneManager().getAssetReplacer()->getReplacementMaterial(drawCallState.getMaterialData().getHash());

      if (replacementMaterial) {
        auto albedoOpacity = replacementMaterial->getOpaqueMaterialData().getAlbedoOpacityTexture();

        if (albedoOpacity.isValid()) {
          uint32_t textureIndex;
          getSceneManager().trackTexture(this, albedoOpacity, textureIndex, true, false);
          albedoOpacity.finalizePendingPromotion();

          // Original 0th colour texture slot
          const uint32_t colorTextureSlot = drawCallState.materialData.colorTextureSlot[0];

          // Save current color texture first
          if (colorTextureSlot < m_rc.size() &&
              m_rc[colorTextureSlot].imageView != nullptr) {
            previousColorView = m_rc[colorTextureSlot].imageView;
          }

          bindResourceView(colorTextureSlot, albedoOpacity.getImageView(), nullptr);
        }
      }
    }

    // Bake the texture
    const bool isBaked = getSceneManager().getTerrainBaker().bakeDrawCall(this, m_state, m_rtState, params, drawCallState, transformData.textureTransform);

    if (isBaked) {
      // Bind the baked terrain texture to the mesh
      if (!TerrainBaker::debugDisableBinding()) {
        // Update the material data
        const Resources::Resource& terrainTexture = getResourceManager().getTerrainTexture(Rc<DxvkContext>(this));
        const Rc<DxvkSampler> terrainSampler = getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        LegacyMaterialData overrideMaterial;
        overrideMaterial.colorTextures[0] = TextureRef(terrainSampler, terrainTexture.view);
        drawCallState.materialData = overrideMaterial;

        // Generate texcoords in the RT shader
        transformData.texgenMode = TexGenMode::CascadedViewPositions;
      }

      // Restore state modified during baking
      if (!TerrainBaker::debugDisableBaking()) {

        // Restore color texture
        if (previousColorView != nullptr) {
          bindResourceView(drawCallState.materialData.colorTextureSlot[0], previousColorView, nullptr);
        }
      }
    }

    return;
  }

  bool RtxContext::rasterizeSky(const DrawParameters& params, const DrawCallState& drawCallState) {
    auto& options = RtxOptions::Get();

    const XXH64_hash_t geometryHash = drawCallState.getHash(RtxOptions::Get()->GeometryAssetHashRule);
    if (!shouldBakeSky(drawCallState) && !RtxOptions::Get()->isSkyboxGeometry(geometryHash))
      return false;

    ScopedGpuProfileZone(this, "rasterizeSky");

    // Grab and apply replacement texture if any
    // NOTE: only the original color texture will be replaced with albedo-opacity texture
    auto replacementMaterial = getSceneManager().getAssetReplacer()->getReplacementMaterial(drawCallState.getMaterialData().getHash());
    bool replacemenIsLDR = false;
    Rc<DxvkImageView> curColorView;

    if (replacementMaterial) {
      auto albedoOpacity = replacementMaterial->getOpaqueMaterialData().getAlbedoOpacityTexture();

      if (albedoOpacity.isValid()) {
        uint32_t textureIndex;
        getSceneManager().trackTexture(this, albedoOpacity, textureIndex, true, false);
        albedoOpacity.finalizePendingPromotion();

        // Original 0th colour texture slot
        const uint32_t colorTextureSlot = drawCallState.materialData.colorTextureSlot[0];

        // Save current color texture first
        if (colorTextureSlot < m_rc.size() &&
            m_rc[colorTextureSlot].imageView != nullptr) {
          curColorView = m_rc[colorTextureSlot].imageView;
        }

        bindResourceView(colorTextureSlot, albedoOpacity.getImageView(), nullptr);
        replacemenIsLDR = TextureUtils::isLDR(albedoOpacity.getImageView()->info().format);
      }
    }

    // Save current RTs
    DxvkRenderTargets curRts = m_state.om.renderTargets;

    // Use game render target format for sky render target views whether it is linear, HDR or sRGB
    m_skyRtColorFormat = curRts.color[0].view->image()->info().format;
    // Use sRGB (or linear for HDR formats) for image and sampling views
    m_skyColorFormat = TextureUtils::toSRGB(m_skyRtColorFormat);

    if (RtxOptions::Get()->skyForceHDR()) {
      if (TextureUtils::isLDR(m_skyRtColorFormat) && (!replacementMaterial || replacemenIsLDR)) {
        ONCE(Logger::warn("Sky may not appear correct: sky intermediate format has been forced to HDR "
                          "while the original sky is LDR and no HDR sky replacement has been found!"));
      }

      m_skyRtColorFormat = m_skyColorFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    }

    // Save viewports
    const uint32_t curViewportCount = m_state.gp.state.rs.viewportCount();
    const DxvkViewportState curVp = m_state.vp;

    rasterizeToSkyMatte(params);
    // TODO: make probe optional?
    rasterizeToSkyProbe(params);

    m_skyClearDirty = false;

    // Restore VPs
    setViewports(curViewportCount, curVp.viewports.data(), curVp.scissorRects.data());

    // Restore RTs
    bindRenderTargets(curRts);

    // Restore color texture
    if (curColorView != nullptr) {
      bindResourceView(drawCallState.materialData.colorTextureSlot[0], curColorView, nullptr);
    }

    return true;
  }

  void RtxContext::clearRenderTarget(const Rc<DxvkImageView>& imageView,
                                     VkImageAspectFlags clearAspects, VkClearValue clearValue) {
    // Capture color for skybox clear
    if (clearAspects & VK_IMAGE_ASPECT_COLOR_BIT) {
      m_skyClearValue = clearValue;

      // Set dirty flag so that next skyprobe rasterize will clear the views.
      // We assume that skybox drawcalls will immediately follow the clear. The logic would
      // need to be revisited if this is not true for some game.
      m_skyClearDirty = true;
    }

    DxvkContext::clearRenderTarget(imageView, clearAspects, clearValue);
  }

  void RtxContext::clearImageView(const Rc<DxvkImageView>& imageView, VkOffset3D offset,
                                  VkExtent3D extent, VkImageAspectFlags aspect, VkClearValue value) {
    // Capture color for skybox clear
    if (aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
      m_skyClearValue = value;

      // Set dirty flag so that next skyprobe rasterize will clear the views.
      // We assume that skybox drawcalls will immediately follow the clear. The logic would
      // need to be revisited if this is not true for some game.
      m_skyClearDirty = true;
    }

    DxvkContext::clearImageView(imageView, offset, extent, aspect, value);
  }

  void RtxContext::reportCpuSimdSupport() {
    switch (fast::getSimdSupportLevel()) {
    case fast::AVX512:
      dxvk::Logger::info("CPU supports SIMD: AVX512");
      break;
    case fast::AVX2:
      dxvk::Logger::info("CPU supports SIMD: AVX2");
      break;
    case fast::SSE4_1:
      dxvk::Logger::info("CPU supports SIMD: SSE 4.1");
      break;
    case fast::SSE3:
      dxvk::Logger::info("CPU supports SIMD: SSE 3");
      break;
    case fast::SSE2:
      dxvk::Logger::info("CPU supports SIMD: SSE 2");
      break;
    case fast::None:
      dxvk::Logger::info("CPU doesn't support SIMD");
      break;
    default:
      Logger::err("Invalid SIMD state");
      break;
    }
  }

} // namespace dxvk
