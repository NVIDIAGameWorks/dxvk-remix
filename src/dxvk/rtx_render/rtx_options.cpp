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
#include "rtx_options.h"
#include <filesystem>
#include <nvapi.h>
#include "rtx_terrain_baker.h"
#include "rtx_render/rtx_nee_cache.h"
#include "rtx_render/rtx_rtxdi_rayquery.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "rtx_render/rtx_composite.h"
#include "rtx_render/rtx_demodulate.h"

#include "dxvk_device.h"

namespace dxvk {
  std::unique_ptr<RtxOptions> RtxOptions::pInstance = nullptr;

  void RtxOptions::updateUpscalerFromDlssPreset() {
    if (RtxOptions::Automation::disableUpdateUpscaleFromDlssPreset()) {
      return;
    }

    switch (dlssPreset()) {
      case DlssPreset::Off:
        upscalerTypeRef() = UpscalerType::None;
        reflexModeRef() = ReflexMode::None;
        break;
      case DlssPreset::On:
        upscalerTypeRef() = UpscalerType::DLSS;
        qualityDLSSRef() = DLSSProfile::Auto;
        reflexModeRef() = ReflexMode::LowLatency; // Reflex uses ON under G (not Boost)
        break;
    }
  }

  void RtxOptions::updateUpscalerFromNisPreset() {
    switch (nisPreset()) {
    case NisPreset::Performance:
      resolutionScaleRef() = 0.5f;
      break;
    case NisPreset::Balanced:
      resolutionScaleRef() = 0.66f;
      break;
    case NisPreset::Quality:
      resolutionScaleRef() = 0.75f;
      break;
    case NisPreset::Fullscreen:
      resolutionScaleRef() = 1.0f;
      break;
    }
  }

  void RtxOptions::updateUpscalerFromTaauPreset() {
    switch (taauPreset()) {
    case TaauPreset::Performance:
      resolutionScaleRef() = 0.5f;
      break;
    case TaauPreset::Balanced:
      resolutionScaleRef() = 0.66f;
      break;
    case TaauPreset::Quality:
      resolutionScaleRef() = 0.75f;
      break;
    case TaauPreset::Fullscreen:
      resolutionScaleRef() = 1.0f;
      break;
    }
  }

  void RtxOptions::updatePresetFromUpscaler() {
    if (RtxOptions::Get()->upscalerType() == UpscalerType::None &&
        reflexMode() == ReflexMode::None) {
      RtxOptions::Get()->dlssPresetRef() = DlssPreset::Off;
    } else if (RtxOptions::Get()->upscalerType() == UpscalerType::DLSS &&
               reflexMode() == ReflexMode::LowLatency) {
      if ((graphicsPreset() == GraphicsPreset::Ultra || graphicsPreset() == GraphicsPreset::High) &&
          qualityDLSS() == DLSSProfile::Auto) {
        RtxOptions::Get()->dlssPresetRef() = DlssPreset::On;
      } else {
        RtxOptions::Get()->dlssPresetRef() = DlssPreset::Custom;
      }
    } else {
      RtxOptions::Get()->dlssPresetRef() = DlssPreset::Custom;
    }

    switch (RtxOptions::Get()->upscalerType()) {
      case UpscalerType::NIS: {
        const float nisResolutionScale = resolutionScale();
        if (nisResolutionScale <= 0.5f) {
          RtxOptions::Get()->nisPresetRef() = NisPreset::Performance;
        } else if (nisResolutionScale <= 0.66f) {
          RtxOptions::Get()->nisPresetRef() = NisPreset::Balanced;
        } else if (nisResolutionScale <= 0.75f) {
          RtxOptions::Get()->nisPresetRef() = NisPreset::Quality;
        } else {
          RtxOptions::Get()->nisPresetRef() = NisPreset::Fullscreen;
        }
        break;
      }
      case UpscalerType::TAAU: {
        const float taauResolutionScale = resolutionScale();
        if (taauResolutionScale <= 0.5f) {
          RtxOptions::Get()->taauPresetRef() = TaauPreset::Performance;
        } else if (taauResolutionScale <= 0.66f) {
          RtxOptions::Get()->taauPresetRef() = TaauPreset::Balanced;
        } else if (taauResolutionScale <= 0.75f) {
          RtxOptions::Get()->taauPresetRef() = TaauPreset::Quality;
        } else {
          RtxOptions::Get()->taauPresetRef() = TaauPreset::Fullscreen;
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
    if (preset == PathTracerPreset::ReSTIR) {
      // This preset uses ReSTIR GI to improve signal quality,
      // Some settings are changed to adapt to DLSS-RR
      // RTXDI
      DxvkRtxdiRayQuery::stealBoundaryPixelSamplesWhenOutsideOfScreenRef() = false;
      DxvkRtxdiRayQuery::permutationSamplingNthFrameRef() = 1;
      DxvkRtxdiRayQuery::enableDenoiserConfidenceRef() = false;

      // ReSTIR GI
      useReSTIRGIRef() = true;
      DxvkReSTIRGIRayQuery::setToRayReconstructionPreset();

      // Integrator
      minOpaqueDiffuseLobeSamplingProbabilityRef() = 0.05f;
      minOpaqueSpecularLobeSamplingProbabilityRef() = 0.05f;
      enableFirstBounceLobeProbabilityDitheringRef() = false;
      russianRouletteModeRef() = RussianRouletteMode::SpecularBased;

      // NEE Cache
      NeeCachePass::enableRef() = true;
      NeeCachePass::enableModeAfterFirstBounceRef() = NeeEnableMode::All;

      // Demodulate
      DemodulatePass::enableDirectLightBoilingFilterRef() = false;

      // Composite
      CompositePass::postFilterThresholdRef() = 10.0f;
      CompositePass::usePostFilterRef() = false;
      denoiseDirectAndIndirectLightingSeparatelyRef() = true;

    } else if (preset == PathTracerPreset::Default) {
      // This is the default setting used by NRD
      // RTXDI
      DxvkRtxdiRayQuery::stealBoundaryPixelSamplesWhenOutsideOfScreenRef() = true;
      DxvkRtxdiRayQuery::permutationSamplingNthFrameRef() = 0;
      DxvkRtxdiRayQuery::enableDenoiserConfidenceRef() = true;

      // ReSTIR GI
      DxvkReSTIRGIRayQuery::setToNRDPreset();

      // Integrator
      minOpaqueDiffuseLobeSamplingProbabilityRef() = 0.25f;
      minOpaqueSpecularLobeSamplingProbabilityRef() = 0.25f;
      enableFirstBounceLobeProbabilityDitheringRef() = true;
      russianRouletteModeRef() = RussianRouletteMode::ThroughputBased;

      // NEE Cache
      NeeCachePass::enableModeAfterFirstBounceRef() = NeeEnableMode::SpecularOnly;

      // Demodulate
      DemodulatePass::enableDirectLightBoilingFilterRef() = true;

      // Composite
      CompositePass::postFilterThresholdRef() = 3.0f;
      CompositePass::usePostFilterRef() = true;
    }
  }

  void RtxOptions::updateLightingSetting() {
    bool isRayReconstruction = RtxOptions::Get()->isRayReconstructionEnabled();
    bool isDLSS = RtxOptions::Get()->isDLSSEnabled();
    bool isNative = RtxOptions::Get()->upscalerType() == UpscalerType::None;
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
    
  void RtxOptions::updateGraphicsPresets(const DxvkDevice* device) {
    // Handle Automatic Graphics Preset (From configuration/default)

    if (RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Auto) {
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
          Logger::info("NVIDIA Ampere architecture detected, setting default graphics settings to High");
          preferredDefault = GraphicsPreset::High;
        } else {
          // Ada and beyond

          // figure out how much vidmem we have
          VkPhysicalDeviceMemoryProperties memProps = device->adapter()->memoryProperties();
          VkDeviceSize vidMemSize = 0;
          for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if (memProps.memoryTypes[i].propertyFlags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
              vidMemSize = memProps.memoryHeaps[memProps.memoryTypes[i].heapIndex].size;
              break;
            }
          }

          if (vidMemSize > 8ull * 1024 * 1024 * 1024) {
            Logger::info("NVIDIA Ada architecture detected, setting default graphics settings to Ultra");
            preferredDefault = GraphicsPreset::Ultra;
          } else {
            Logger::info("NVIDIA Ada architecture detected, setting default graphics settings to High");
            preferredDefault = GraphicsPreset::High;
          }
        }
      } else {
        // Default to low if we don't know the hardware
        Logger::info("Non-NVIDIA architecture detected, setting default graphics settings to Low");
        preferredDefault = GraphicsPreset::Low;

        // Setup some other known good defaults for other IHVs.
        RtxOptions::Get()->resolutionScaleRef() = 0.5f;
        // Todo: Currently this code is needed to allow the the non-DLSS upscaling paths to reflect the
        // resolution scale setting otherwise the resolution scale may be automatically updated to match
        // some other preset whenever something like updateUpscalerFromTaauPreset is called. Ideally the
        // resolution scale and these presets would not be so disconnected like this (or maybe
        // updatePresetFromUpscaler just needs to be called in the right places).
        RtxOptions::Get()->nisPresetRef() = NisPreset::Performance;
        RtxOptions::Get()->taauPresetRef() = TaauPreset::Performance;
      }

      RtxOptions::Get()->graphicsPresetRef() = preferredDefault;
    }

    // Handle Graphics Presets
    bool isRayReconstruction = RtxOptions::Get()->isRayReconstructionEnabled();

    auto lowGraphicsPresetCommonSettings = [&]() {
      pathMinBouncesRef() = 0;
      pathMaxBouncesRef() = 2;
      enableTransmissionApproximationInIndirectRaysRef() = true;
      enableVolumetricLightingRef() = false;
      enableUnorderedEmissiveParticlesInIndirectRaysRef() = false;
      denoiseDirectAndIndirectLightingSeparatelyRef() = false;
      minReplacementTextureMipMapLevelRef() = 1;
      enableUnorderedResolveInIndirectRaysRef() = false;
      NeeCachePass::enableRef() = isRayReconstruction;
    };

    assert(graphicsPreset() != GraphicsPreset::Auto);

    if (graphicsPreset() == GraphicsPreset::Ultra) {
      pathMinBouncesRef() = 1;
      pathMaxBouncesRef() = 4;
      enableTransmissionApproximationInIndirectRaysRef() = false;
      enableVolumetricLightingRef() = true;
      enableUnorderedEmissiveParticlesInIndirectRaysRef() = true;
      denoiseDirectAndIndirectLightingSeparatelyRef() = true;
      minReplacementTextureMipMapLevelRef() = 0;
      enableUnorderedResolveInIndirectRaysRef() = true;
      NeeCachePass::enableRef() = true;

      russianRouletteMaxContinueProbabilityRef() = 0.9f;
      russianRoulette1stBounceMinContinueProbabilityRef() = 0.6f;
    } else if (graphicsPreset() == GraphicsPreset::High) {
      pathMinBouncesRef() = 0;
      pathMaxBouncesRef() = 2;
      enableTransmissionApproximationInIndirectRaysRef() = true;
      enableVolumetricLightingRef() = true;
      enableUnorderedEmissiveParticlesInIndirectRaysRef() = false;
      denoiseDirectAndIndirectLightingSeparatelyRef() = false;
      minReplacementTextureMipMapLevelRef() = 1;
      enableUnorderedResolveInIndirectRaysRef() = true;
      NeeCachePass::enableRef() = isRayReconstruction;

      russianRouletteMaxContinueProbabilityRef() = 0.9f;
      russianRoulette1stBounceMinContinueProbabilityRef() = 0.6f;
    } else if (graphicsPreset() == GraphicsPreset::Medium) {
      lowGraphicsPresetCommonSettings();

      russianRouletteMaxContinueProbabilityRef() = 0.7f;
      russianRoulette1stBounceMinContinueProbabilityRef() = 0.4f;
    } else if(graphicsPreset() == GraphicsPreset::Low) {
      lowGraphicsPresetCommonSettings();

      russianRouletteMaxContinueProbabilityRef() = 0.7f;
      russianRoulette1stBounceMinContinueProbabilityRef() = 0.4f;
    }
    // else Graphics Preset == Custom
    updateLightingSetting();
    forceHighResolutionReplacementTexturesRef() = false;
  }

  void RtxOptions::updateRaytraceModePresets(const uint32_t vendorID, const VkDriverId driverID) {
    // Handle Automatic Raytrace Mode Preset (From configuration/default)

    if (RtxOptions::Get()->raytraceModePreset() == RaytraceModePreset::Auto) {
      Logger::info("Automatic Raytrace Mode Preset in use (Set rtx.raytraceModePreset to something other than Auto use a non-automatic preset)");

      // Note: Left undefined as these values are initialized in all paths.
      DxvkPathtracerGbuffer::RaytraceMode preferredGBufferRaytraceMode;
      DxvkPathtracerIntegrateDirect::RaytraceMode preferredIntegrateDirectRaytraceMode;
      DxvkPathtracerIntegrateIndirect::RaytraceMode preferredIntegrateIndirectRaytraceMode;

      if (vendorID == static_cast<uint32_t>(DxvkGpuVendor::Nvidia) || driverID == VK_DRIVER_ID_MESA_RADV) {
        // Default to a mixture of Trace Ray and Ray Query on NVIDIA and RADV
        if (driverID == VK_DRIVER_ID_MESA_RADV) {
          Logger::info("RADV driver detected, setting default raytrace modes to Trace Ray (GBuffer/Indirect Integrate) and Ray Query (Direct Integrate)");
        } else {
          Logger::info("NVIDIA architecture detected, setting default raytrace modes to Trace Ray (GBuffer/Indirect Integrate) and Ray Query (Direct Integrate)");
        }

        preferredGBufferRaytraceMode = DxvkPathtracerGbuffer::RaytraceMode::TraceRay;
        preferredIntegrateDirectRaytraceMode = DxvkPathtracerIntegrateDirect::RaytraceMode::RayQuery;
        preferredIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode::TraceRay;
      } else {
        // Default to Ray Query on AMD/Intel
        Logger::info("Non-NVIDIA architecture detected, setting default raytrace modes to Ray Query");

        preferredGBufferRaytraceMode = DxvkPathtracerGbuffer::RaytraceMode::RayQuery;
        preferredIntegrateDirectRaytraceMode = DxvkPathtracerIntegrateDirect::RaytraceMode::RayQuery;
        preferredIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode::RayQuery;
      }

      RtxOptions::Get()->renderPassGBufferRaytraceModeRef() = preferredGBufferRaytraceMode;
      RtxOptions::Get()->renderPassIntegrateDirectRaytraceModeRef() = preferredIntegrateDirectRaytraceMode;
      RtxOptions::Get()->renderPassIntegrateIndirectRaytraceModeRef() = preferredIntegrateIndirectRaytraceMode;
    }
  }

  void RtxOptions::resetUpscaler() {
    RtxOptions::Get()->upscalerTypeRef() = UpscalerType::DLSS;
    reflexModeRef() = ReflexMode::LowLatency;
  }

  std::string RtxOptions::getCurrentDirectory() const {
    return std::filesystem::current_path().string();
  }

  bool RtxOptions::needsMeshBoundingBox() {
    return AntiCulling::Object::enable() ||
           AntiCulling::Light::enable()  ||
           TerrainBaker::needsTerrainBaking() ||
           enableAlwaysCalculateAABB() ||
           NeeCachePass::enable();
  }
}
