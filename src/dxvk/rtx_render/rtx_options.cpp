/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_options.h"

#include <filesystem>
#include <nvapi.h>
#include "../imgui/imgui.h"
#include "rtx_bridge_message_channel.h"
#include "rtx_terrain_baker.h"
#include "rtx_nee_cache.h"
#include "rtx_rtxdi_rayquery.h"
#include "rtx_restir_gi_rayquery.h"
#include "rtx_composite.h"
#include "rtx_demodulate.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_ray_reconstruction.h"

#include "dxvk_device.h"
#include "rtx_global_volumetrics.h"

namespace dxvk {
  std::unique_ptr<RtxOptions> RtxOptions::m_instance = nullptr;

  void RtxOptions::showUICursorOnChange() {
    if (ImGui::GetCurrentContext() != nullptr) {
      auto& io = ImGui::GetIO();
      io.MouseDrawCursor = RtxOptions::showUICursor() && RtxOptions::showUI() != UIType::None;
    }
  }

  void RtxOptions::blockInputToGameInUIOnChange() {
    const bool doBlock = RtxOptions::blockInputToGameInUI() && RtxOptions::showUI() != UIType::None;

    BridgeMessageChannel::get().send("UWM_REMIX_UIACTIVE_MSG", doBlock ? 1 : 0, 0);
  }

  void RtxOptions::updateUpscalerFromDlssPreset() {
    if (RtxOptions::Automation::disableUpdateUpscaleFromDlssPreset()) {
      return;
    }

    switch (dlssPreset()) {
      // TODO[REMIX-4105] all of these are used right after being set, so this needs to be setImmediately.
      // This should be addressed by REMIX-4109 if that is done before REMIX-4105 is fully cleaned up.
      case DlssPreset::Off:
        upscalerType.setImmediately(UpscalerType::None);
        reflexMode.setImmediately(ReflexMode::None);
        break;
      case DlssPreset::On:
        upscalerType.setImmediately(UpscalerType::DLSS);
        qualityDLSS.setImmediately(DLSSProfile::Auto);
        reflexMode.setImmediately(ReflexMode::LowLatency); // Reflex uses ON under G (not Boost)
        break;
    }
  }

  void RtxOptions::updateUpscalerFromNisPreset() {
    switch (nisPreset()) {
    case NisPreset::Performance:
      resolutionScale.set(0.5f);
      break;
    case NisPreset::Balanced:
      resolutionScale.set(0.66f);
      break;
    case NisPreset::Quality:
      resolutionScale.set(0.75f);
      break;
    case NisPreset::Fullscreen:
      resolutionScale.set(1.0f);
      break;
    }
  }

  void RtxOptions::updateUpscalerFromTaauPreset() {
    switch (taauPreset()) {
    case TaauPreset::UltraPerformance:
      resolutionScale.set(0.33f);
      break;
    case TaauPreset::Performance:
      resolutionScale.set(0.5f);
      break;
    case TaauPreset::Balanced:
      resolutionScale.set(0.66f);
      break;
    case TaauPreset::Quality:
      resolutionScale.set(0.75f);
      break;
    case TaauPreset::Fullscreen:
      resolutionScale.set(1.0f);
      break;
    }
  }

  void RtxOptions::updatePresetFromUpscaler() {
    if (RtxOptions::upscalerType() == UpscalerType::None &&
        reflexMode() == ReflexMode::None) {
      RtxOptions::dlssPreset.set(DlssPreset::Off);
    } else if (RtxOptions::upscalerType() == UpscalerType::DLSS &&
               reflexMode() == ReflexMode::LowLatency) {
      if ((graphicsPreset() == GraphicsPreset::Ultra || graphicsPreset() == GraphicsPreset::High) &&
          qualityDLSS() == DLSSProfile::Auto) {
        RtxOptions::dlssPreset.set(DlssPreset::On);
      } else {
        RtxOptions::dlssPreset.set(DlssPreset::Custom);
      }
    } else {
      RtxOptions::dlssPreset.set(DlssPreset::Custom);
    }

    switch (RtxOptions::upscalerType()) {
      case UpscalerType::NIS: {
        const float nisResolutionScale = resolutionScale();
        if (nisResolutionScale <= 0.5f) {
          RtxOptions::nisPreset.set(NisPreset::Performance);
        } else if (nisResolutionScale <= 0.66f) {
          RtxOptions::nisPreset.set(NisPreset::Balanced);
        } else if (nisResolutionScale <= 0.75f) {
          RtxOptions::nisPreset.set(NisPreset::Quality);
        } else {
          RtxOptions::nisPreset.set(NisPreset::Fullscreen);
        }
        break;
      }
      case UpscalerType::TAAU: {
        const float taauResolutionScale = resolutionScale();
        if (taauResolutionScale <= 0.33f) {
          RtxOptions::taauPreset.set(TaauPreset::UltraPerformance);
        } else if (taauResolutionScale <= 0.5f) {
          RtxOptions::taauPreset.set(TaauPreset::Performance);
        } else if (taauResolutionScale <= 0.66f) {
          RtxOptions::taauPreset.set(TaauPreset::Balanced);
        } else if (taauResolutionScale <= 0.75f) {
          RtxOptions::taauPreset.set(TaauPreset::Quality);
        } else {
          RtxOptions::taauPreset.set(TaauPreset::Fullscreen);
        }
        break;
      }
    }
  }

  static bool queryNvidiaArchInfo(NV_GPU_ARCH_INFO& archInfo) {
    NvAPI_Status status;
    status = NvAPI_Initialize();
    if (status != NVAPI_OK) {
      return false;
    }
    
    NvPhysicalGpuHandle nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS];
    NvU32 GpuCount;
    status = NvAPI_EnumPhysicalGPUs(nvGPUHandle, &GpuCount);
    if (status != NVAPI_OK) {
      return false;
    }
    
    assert(GpuCount > 0);

    archInfo.version = NV_GPU_ARCH_INFO_VER;
    // Note: Currently only using the first returned GPU Handle. Ideally this should use the GPU Handle Vulkan is using
    // though in the case of a mixed architecture multi-GPU system.
    status = NvAPI_GPU_GetArchInfo(nvGPUHandle[0], &archInfo);
    return status == NVAPI_OK;
  }

  NV_GPU_ARCHITECTURE_ID RtxOptions::getNvidiaArch() {
    NV_GPU_ARCH_INFO archInfo;
    if (queryNvidiaArchInfo(archInfo) == false) {
      return NV_GPU_ARCHITECTURE_TU100;
    }
    
    return archInfo.architecture_id;
  }

  NV_GPU_ARCH_IMPLEMENTATION_ID RtxOptions::getNvidiaChipId() {
    NV_GPU_ARCH_INFO archInfo;
    if (queryNvidiaArchInfo(archInfo) == false) {
      return NV_GPU_ARCH_IMPLEMENTATION_TU100;
    }
    
    return archInfo.implementation_id;
  }

  void RtxOptions::updatePathTracerPreset(PathTracerPreset preset) {
    if (preset == PathTracerPreset::RayReconstruction) {
      // RTXDI
      DxvkRtxdiRayQuery::stealBoundaryPixelSamplesWhenOutsideOfScreen.set(false);
      DxvkRtxdiRayQuery::permutationSamplingNthFrame.set(1);
      DxvkRtxdiRayQuery::enableDenoiserConfidence.set(false);
      DxvkRtxdiRayQuery::enableBestLightSampling.set(false);
      DxvkRtxdiRayQuery::initialSampleCount.set(3);
      DxvkRtxdiRayQuery::spatialSamples.set(2);
      DxvkRtxdiRayQuery::disocclusionSamples.set(2);
      DxvkRtxdiRayQuery::enableSampleStealing.set(false);

      // ReSTIR GI
      if (RtxOptions::useReSTIRGI()) {
        DxvkReSTIRGIRayQuery::setToRayReconstructionPreset();
      }

      // Integrator
      minOpaqueDiffuseLobeSamplingProbability.set(0.05f);
      minOpaqueSpecularLobeSamplingProbability.set(0.05f);
      enableFirstBounceLobeProbabilityDithering.set(false);
      russianRouletteMode.set(RussianRouletteMode::SpecularBased);

      // NEE Cache
      NeeCachePass::enableModeAfterFirstBounce.set(NeeEnableMode::All);

      // Demodulate
      DemodulatePass::enableDirectLightBoilingFilter.set(false);

      // Composite
      CompositePass::postFilterThreshold.set(10.0f);
      CompositePass::usePostFilter.set(false);

    } else if (preset == PathTracerPreset::Default) {
      // This is the default setting used by NRD
      // RTXDI
      DxvkRtxdiRayQuery::stealBoundaryPixelSamplesWhenOutsideOfScreenObject().resetToDefault();
      DxvkRtxdiRayQuery::permutationSamplingNthFrameObject().resetToDefault();
      DxvkRtxdiRayQuery::enableDenoiserConfidenceObject().resetToDefault();
      DxvkRtxdiRayQuery::enableBestLightSamplingObject().resetToDefault();
      DxvkRtxdiRayQuery::initialSampleCountObject().resetToDefault();
      DxvkRtxdiRayQuery::spatialSamplesObject().resetToDefault();
      DxvkRtxdiRayQuery::disocclusionSamplesObject().resetToDefault();
      DxvkRtxdiRayQuery::enableSampleStealingObject().resetToDefault();

      // ReSTIR GI
      if (RtxOptions::useReSTIRGI()) {
        DxvkReSTIRGIRayQuery::setToNRDPreset();
      }

      // Integrator
      minOpaqueDiffuseLobeSamplingProbabilityObject().resetToDefault();
      minOpaqueSpecularLobeSamplingProbabilityObject().resetToDefault();
      enableFirstBounceLobeProbabilityDitheringObject().resetToDefault();
      russianRouletteModeObject().resetToDefault();

      // NEE Cache
      NeeCachePass::enableModeAfterFirstBounceObject().resetToDefault();

      // Demodulate
      DemodulatePass::enableDirectLightBoilingFilterObject().resetToDefault();

      // Composite
      CompositePass::postFilterThresholdObject().resetToDefault();
      CompositePass::usePostFilterObject().resetToDefault();
    }
  }

  void RtxOptions::updateLightingSetting() {
    bool isRayReconstruction = RtxOptions::isRayReconstructionEnabled();
    bool isDLSS = RtxOptions::isDLSSEnabled();
    bool isNative = RtxOptions::upscalerType() == UpscalerType::None;
    if (isRayReconstruction) {
      updatePathTracerPreset(DxvkRayReconstruction::pathTracerPreset());
    } else if (isDLSS) {
      updatePathTracerPreset(PathTracerPreset::Default);
    } else if (isNative) {
      if (!DxvkRayReconstruction::preserveSettingsInNativeMode()) {
        updatePathTracerPreset(PathTracerPreset::Default);
      }
    }
  }
    
  void RtxOptions::updateGraphicsPresets(DxvkDevice* device) {
    // Handle Automatic Graphics Preset (From configuration/default)

    if (RtxOptions::graphicsPreset() == GraphicsPreset::Auto) {
      const DxvkDeviceInfo& deviceInfo = device->adapter()->devicePropertiesExt();
      const uint32_t vendorID = deviceInfo.core.properties.vendorID;
      
      // Default updateGraphicsPresets value, don't want to hit this path intentionally or Low settings will be used
      assert(vendorID != 0);

      Logger::info("Automatic Graphics Preset in use (Set rtx.graphicsPreset to something other than Auto use a non-automatic preset)");

      GraphicsPreset preferredDefault = GraphicsPreset::Low;

      if (vendorID == static_cast<uint32_t>(DxvkGpuVendor::Nvidia)) {
        const NV_GPU_ARCHITECTURE_ID archId = getNvidiaArch();

        if (archId < NV_GPU_ARCHITECTURE_TU100) {
          // Pre-Turing
          Logger::info("NVIDIA architecture without HW RTX support detected, setting default graphics settings to Low, but your experience may not be optimal");
          preferredDefault = GraphicsPreset::Low;
        } else if (archId < NV_GPU_ARCHITECTURE_GA100) {
          // Turing
          Logger::info("NVIDIA Turing architecture detected, setting default graphics settings to Low");
          preferredDefault = GraphicsPreset::Low;
        } else if (archId < NV_GPU_ARCHITECTURE_AD100) {
          // Ampere
          Logger::info("NVIDIA Ampere architecture detected, setting default graphics settings to Medium");
          preferredDefault = GraphicsPreset::Medium;
        } else if (archId < NV_GPU_ARCHITECTURE_GB200) {
          // Ada
          Logger::info("NVIDIA Ada architecture detected, setting default graphics settings to High");
          preferredDefault = GraphicsPreset::High;
        } else {
          // Blackwell and beyond
          Logger::info("NVIDIA Blackwell architecture detected, setting default graphics settings to Ultra");
          preferredDefault = GraphicsPreset::Ultra;
        }
      } else {
        // Default to low if we don't know the hardware
        Logger::info("Non-NVIDIA architecture detected, setting default graphics settings to Low");
        preferredDefault = GraphicsPreset::Low;

        // Setup some other known good defaults for other IHVs.
        RtxOptions::resolutionScale.set(0.5f);
        // Todo: Currently this code is needed to allow the the non-DLSS upscaling paths to reflect the
        // resolution scale setting otherwise the resolution scale may be automatically updated to match
        // some other preset whenever something like updateUpscalerFromTaauPreset is called. Ideally the
        // resolution scale and these presets would not be so disconnected like this (or maybe
        // updatePresetFromUpscaler just needs to be called in the right places).
        RtxOptions::nisPreset.set(NisPreset::Performance);
        RtxOptions::taauPreset.set(TaauPreset::Performance);
      }

      // figure out how much vidmem we have
      VkPhysicalDeviceMemoryProperties memProps = device->adapter()->memoryProperties();
      VkDeviceSize vidMemSize = 0;
      for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if (memProps.memoryTypes[i].propertyFlags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
          vidMemSize = memProps.memoryHeaps[memProps.memoryTypes[i].heapIndex].size;
          break;
        }
      }

      // for 8GB GPUs we lower the quality even further.
      if (vidMemSize <= 8ull * 1024 * 1024 * 1024) {
        Logger::info("8GB GPU detected, lowering quality setting.");
        preferredDefault = (GraphicsPreset)std::clamp((int)preferredDefault + 1, (int) GraphicsPreset::Medium, (int) GraphicsPreset::Low);
        RtxOptions::lowMemoryGpu.set(true);
      } else {
        RtxOptions::lowMemoryGpu.set(false);
      }

      // TODO[REMIX-4105] all of these are used right after being set, so this needs to be setImmediately.
      // This should be addressed by REMIX-4109 if that is done before REMIX-4105 is fully cleaned up.
      RtxOptions::graphicsPreset.setImmediately(preferredDefault);
    }

    auto common = device->getCommon();
    DxvkPostFx& postFx = common->metaPostFx();
    DxvkRtxdiRayQuery& rtxdiRayQuery = common->metaRtxdiRayQuery();
    DxvkReSTIRGIRayQuery& restirGiRayQuery = common->metaReSTIRGIRayQuery();

    // Handle Graphics Presets
    bool isRayReconstruction = RtxOptions::isRayReconstructionEnabled();

    auto lowGraphicsPresetCommonSettings = [&]() {
      pathMinBounces.set(0);
      pathMaxBounces.set(2);
      enableTransmissionApproximationInIndirectRays.set(true);
      enableUnorderedEmissiveParticlesInIndirectRays.set(false);
      denoiseDirectAndIndirectLightingSeparately.set(false);
      enableUnorderedResolveInIndirectRays.set(false);
      NeeCachePass::enable.set(isRayReconstruction);
      rtxdiRayQuery.enableRayTracedBiasCorrection.set(false);
      restirGiRayQuery.biasCorrectionMode.set(ReSTIRGIBiasCorrection::BRDF);
      restirGiRayQuery.useReflectionReprojection.set(false);
      common->metaComposite().enableStochasticAlphaBlend.set(false);
      postFx.enable.set(false);
    };

    auto enableNrcPreset = [&](NeuralRadianceCache::QualityPreset nrcPreset) {
      NeuralRadianceCache& nrc = device->getCommon()->metaNeuralRadianceCache();
      // TODO[REMIX-4105] trying to use NRC for a frame when it isn't supported will cause a crash, so this needs to be setImmediately.
      // Should refactor this to use a separate global for the final state, and indicate user preference with the option. 
      if (nrc.checkIsSupported(device)) {
        RtxOptions::integrateIndirectMode.setImmediately(IntegrateIndirectMode::NeuralRadianceCache);
        nrc.setQualityPreset(nrcPreset);
      } else {
        RtxOptions::integrateIndirectMode.setImmediately(IntegrateIndirectMode::ReSTIRGI);
      }
    };

    assert(graphicsPreset() != GraphicsPreset::Auto);

    RtxGlobalVolumetrics& volumetrics = device->getCommon()->metaGlobalVolumetrics();

    if (graphicsPreset() == GraphicsPreset::Ultra) {
      pathMinBounces.set(1);
      pathMaxBounces.set(4);
      enableTransmissionApproximationInIndirectRays.set(false);
      enableUnorderedEmissiveParticlesInIndirectRays.set(true);
      denoiseDirectAndIndirectLightingSeparately.set(true);
      enableUnorderedResolveInIndirectRays.set(true);
      NeeCachePass::enable.set(true);

      russianRouletteMaxContinueProbability.set(0.9f);
      russianRoulette1stBounceMinContinueProbability.set(0.6f);

      rtxdiRayQuery.enableRayTracedBiasCorrection.set(true);
      restirGiRayQuery.biasCorrectionMode.set(ReSTIRGIBiasCorrection::PairwiseRaytrace);
      restirGiRayQuery.useReflectionReprojection.set(true);
      common->metaComposite().enableStochasticAlphaBlend.set(true);
      postFx.enable.set(true);

      volumetrics.setQualityLevel(RtxGlobalVolumetrics::Ultra);
      enableNrcPreset(NeuralRadianceCache::QualityPreset::Ultra);

      DxvkRayReconstruction::model.set(DxvkRayReconstruction::RayReconstructionModel::Transformer);
    } else if (graphicsPreset() == GraphicsPreset::High) {
      pathMinBounces.set(0);
      pathMaxBounces.set(2);
      enableTransmissionApproximationInIndirectRays.set(true);
      enableUnorderedEmissiveParticlesInIndirectRays.set(false);
      denoiseDirectAndIndirectLightingSeparately.set(false);
      enableUnorderedResolveInIndirectRays.set(true);
      NeeCachePass::enable.set(isRayReconstruction);

      rtxdiRayQuery.enableRayTracedBiasCorrection.set(true);
      restirGiRayQuery.biasCorrectionMode.set(ReSTIRGIBiasCorrection::PairwiseRaytrace);
      restirGiRayQuery.useReflectionReprojection.set(true);
      common->metaComposite().enableStochasticAlphaBlend.set(true);
      postFx.enable.set(true);

      russianRouletteMaxContinueProbability.set(0.9f);
      russianRoulette1stBounceMinContinueProbability.set(0.6f);

      volumetrics.setQualityLevel(RtxGlobalVolumetrics::High);
      enableNrcPreset(NeuralRadianceCache::QualityPreset::High);

      DxvkRayReconstruction::model.set(DxvkRayReconstruction::RayReconstructionModel::Transformer);
    } else if (graphicsPreset() == GraphicsPreset::Medium) {
      lowGraphicsPresetCommonSettings();

      russianRouletteMaxContinueProbability.set(0.7f);
      russianRoulette1stBounceMinContinueProbability.set(0.4f);

      volumetrics.setQualityLevel(RtxGlobalVolumetrics::Medium);
      enableNrcPreset(NeuralRadianceCache::QualityPreset::Medium);

      DxvkRayReconstruction::model.set(DxvkRayReconstruction::RayReconstructionModel::CNN);
    } else if (graphicsPreset() == GraphicsPreset::Low) {
      lowGraphicsPresetCommonSettings();

      russianRouletteMaxContinueProbability.set(0.7f);
      russianRoulette1stBounceMinContinueProbability.set(0.4f);

      volumetrics.setQualityLevel(RtxGlobalVolumetrics::Low);
      enableNrcPreset(NeuralRadianceCache::QualityPreset::Medium);

      DxvkRayReconstruction::model.set(DxvkRayReconstruction::RayReconstructionModel::CNN);
    }

    // Ensure we are using auto DLSS profile since we will be relying on quality downgrades for Medium/Low settings
    //  and if the user has specified a custom DLSS override, we should respect that.
    if (dlssPreset() != DlssPreset::Custom) {
      qualityDLSS.set(DLSSProfile::Auto);
    }

    // else Graphics Preset == Custom
    updateLightingSetting();
  }

  void RtxOptions::updateRaytraceModePresets(const uint32_t vendorID, const VkDriverId driverID) {
    // Handle Automatic Raytrace Mode Preset (From configuration/default)

    if (RtxOptions::raytraceModePreset() == RaytraceModePreset::Auto) {
      Logger::info("Automatic Raytrace Mode Preset in use (Set rtx.raytraceModePreset to something other than Auto use a non-automatic preset)");

      // Note: Left undefined as these values are initialized in all paths.
      DxvkPathtracerGbuffer::RaytraceMode preferredGBufferRaytraceMode;
      DxvkPathtracerIntegrateDirect::RaytraceMode preferredIntegrateDirectRaytraceMode;
      DxvkPathtracerIntegrateIndirect::RaytraceMode preferredIntegrateIndirectRaytraceMode;

      preferredGBufferRaytraceMode = DxvkPathtracerGbuffer::RaytraceMode::RayQuery;
      preferredIntegrateDirectRaytraceMode = DxvkPathtracerIntegrateDirect::RaytraceMode::RayQuery;

      if (vendorID == static_cast<uint32_t>(DxvkGpuVendor::Nvidia) || driverID == VK_DRIVER_ID_MESA_RADV) {
        // Default to a mixture of Trace Ray and Ray Query on NVIDIA and RADV
        if (driverID == VK_DRIVER_ID_MESA_RADV) {
          Logger::info("RADV driver detected, setting default raytrace modes to Trace Ray (Indirect Integrate) and Ray Query (GBuffer, Direct Integrate)");
        } else {
          Logger::info("NVIDIA architecture detected, setting default raytrace modes to Trace Ray (Indirect Integrate) and Ray Query (GBuffer, Direct Integrate)");
        }

        preferredIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode::TraceRay;
      } else {
        // Default to Ray Query on AMD/Intel
        Logger::info("Non-NVIDIA architecture detected, setting default raytrace modes to Ray Query");

        preferredIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode::RayQuery;
      }

      RtxOptions::renderPassGBufferRaytraceMode.set(preferredGBufferRaytraceMode);
      RtxOptions::renderPassIntegrateDirectRaytraceMode.set(preferredIntegrateDirectRaytraceMode);
      RtxOptions::renderPassIntegrateIndirectRaytraceMode.set(preferredIntegrateIndirectRaytraceMode);
    }
  }

  void RtxOptions::resetUpscaler() {
    RtxOptions::upscalerType.set(UpscalerType::DLSS);
    reflexMode.set(ReflexMode::LowLatency);
  }

  std::string RtxOptions::getCurrentDirectory() {
    return std::filesystem::current_path().string();
  }

  bool RtxOptions::needsMeshBoundingBox() {
    return AntiCulling::isObjectAntiCullingEnabled() ||
           AntiCulling::isLightAntiCullingEnabled() ||
           TerrainBaker::needsTerrainBaking() ||
           enableAlwaysCalculateAABB() ||
           NeeCachePass::enable();
  }
}
