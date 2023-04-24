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

#include <algorithm>
#include <unordered_set>
#include <cassert>
#include <limits>

#include "../util/config/config.h"
#include "../util/xxHash/xxhash.h"
#include "../util/util_math.h"
#include "../util/util_env.h"
#include "rtx_utils.h"
#include "rtx/concept/ray_portal/ray_portal.h"
#include "rtx_volume_integrate.h"
#include "rtx_pathtracer_gbuffer.h"
#include "rtx_pathtracer_integrate_direct.h"
#include "rtx_pathtracer_integrate_indirect.h"
#include "rtx_dlss.h"
#include "rtx_materials.h"
#include "rtx/pass/material_args.h"
#include "rtx_option.h"
#include "rtx_hashing.h"

enum _NV_GPU_ARCHITECTURE_ID;
typedef enum _NV_GPU_ARCHITECTURE_ID NV_GPU_ARCHITECTURE_ID;

// Read-only RTX specific options

namespace dxvk {

  using RenderPassVolumeIntegrateRaytraceMode = DxvkVolumeIntegrate::RaytraceMode;
  using RenderPassGBufferRaytraceMode = DxvkPathtracerGbuffer::RaytraceMode;
  using RenderPassIntegrateDirectRaytraceMode = DxvkPathtracerIntegrateDirect::RaytraceMode;
  using RenderPassIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode;

  enum class UpscalerType : int {
    None = 0,
    DLSS,
    NIS,
    TAAU
  };

  enum class GraphicsPreset : int {
    Ultra = 0,
    High,
    Medium,
    Low,
    Custom,
    // Note: Used to automatically have the graphics preset set on initialization, not used beyond this case
    // as it should be overridden by one of the other values by the time any other code uses it.
    Auto
  };

  enum class RaytraceModePreset {
    Custom = 0,
    Auto = 1
  };

  enum class DlssPreset : int {
    Off = 0,
    On,
    Custom
  };

  enum class NisPreset : int {
    Performance = 0,
    Balanced,
    Quality,
    Fullscreen
  };

  enum class TaauPreset : int {
    Performance = 0,
    Balanced,
    Quality,
    Fullscreen
  };

  enum class CameraAnimationMode : int {
    CameraShake_LeftRight = 0,
    CameraShake_FrontBack,
    CameraShake_Yaw,
    CameraShake_Pitch,
    YawRotation
  };

  enum class TonemappingMode : int {
    Global,
    Local
  };

  enum class UIType : int {
    None = 0,
    Basic,
    Advanced,
    Count
  };

  enum class ReflexMode : int {
    None = 0,
    LowLatency,
    LowLatencyBoost
  };

  enum class FusedWorldViewMode : int {
    None = 0,
    View,
    World
  };

  class RtxOptions {
    friend class ImGUI; // <-- we want to modify these values directly.
    friend class ImGuiSplash; // <-- we want to modify these values directly.
    friend class RtxContext; // <-- we want to modify these values directly.
    friend class RtxInitializer; // <-- we want to modify these values directly.

    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, lightmapTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, skyBoxTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, ignoreTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, ignoreLights, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, uiTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, worldSpaceUiTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, worldSpaceUiBackgroundTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, hideInstanceTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, playerModelTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, playerModelBodyTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, lightConverter, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, particleTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, beamTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, decalTextures, {}, "Static geometric decals or decals with complex topology.\nThese materials will be blended over the materials underneath them.\nA small offset is applied to each flat part of these decals.");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, dynamicDecalTextures, {}, "Dynamically spawned geometric decals, such as bullet holes.\nThese materials will be blended over the materials underneath them.\nA small offset is applied to each triangle fan in these decals.");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, nonOffsetDecalTextures, {}, "Geometric decals with arbitrary topology that are already offset from the base geometry.\nThese materials will be blended over the materials underneath them. ");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, terrainTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, cutoutTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, opacityMicromapIgnoreTextures, {}, "");
    RW_RTX_OPTION("rtx", std::unordered_set<XXH64_hash_t>, animatedWaterTextures, {}, "");

    RW_RTX_OPTION("rtx", std::string, geometryGenerationHashRuleString, "positions,indices,texcoords,geometrydescriptor",
                  "Defines which asset hashes we need to generate via the geometry processing engine.");

    RW_RTX_OPTION("rtx", std::string, geometryAssetHashRuleString, "positions,indices,geometrydescriptor",
                  "Defines which hashes we need to include when sampling from replacements and doing USD capture.");
    
  public:
#ifdef REMIX_DEVELOPMENT
    // Note, this is currently a debug option we don't want to support in shipping config
    RTX_OPTION_ENV("rtx", bool,  enableRaytracing, true, "DXVK_ENABLE_RAYTRACING", "");
#else
    // Shipping config
    bool enableRaytracing() { return true; }
#endif

    RTX_OPTION_FLAG("rtx", bool, keepTexturesForTagging, false, RtxOptionFlags::NoSave, "Note: this keeps all textures in video memory, which can drastically increase VRAM consumption. Intended to assist with tagging textures that are only used for a short period of time (such as loading screens). Use only when necessary!");

    RTX_OPTION("rtx", bool,  skipDrawCallsPostRTXInjection, false, "");
    RTX_OPTION("rtx", DlssPreset,  dlssPreset, DlssPreset::On, "Combined DLSS Preset for quickly controlling Upscaling, Frame Interpolation and Latency Reduction.");
    RTX_OPTION("rtx", NisPreset,  nisPreset, NisPreset::Balanced, "Adjusts NIS scaling factor, trades quality for performance.");
    RTX_OPTION("rtx", TaauPreset,  taauPreset, TaauPreset::Balanced,  "Adjusts TAA-U scaling factor, trades quality for performance.");
    RTX_OPTION_ENV("rtx", GraphicsPreset, graphicsPreset, GraphicsPreset::Auto, "DXVK_GRAPHICS_PRESET_TYPE", "Overall rendering preset, higher presets result in higher image quality, lower presets result in better performance.");
    RTX_OPTION_ENV("rtx", RaytraceModePreset, raytraceModePreset, RaytraceModePreset::Auto, "DXVK_RAYTRACE_MODE_PRESET_TYPE", "");
    RTX_OPTION("rtx", std::string, sourceRootPath, "", "A path pointing at the root folder of the project, used to override the path to the root of the project generated at build-time (as this path is only valid for the machine the project was originally compiled on). Used primarily for locating shader source files for runtime shader recompilation.");
    RTX_OPTION("rtx", bool,  recompileShadersOnLaunch, false, "When set to true runtime shader recompilation will execute on the first frame after launch.");
    RTX_OPTION("rtx", bool, useLiveShaderEditMode, false, "When set to true shaders will be automatically recompiled when any shader file is updated (saved for instance) in addition to the usual manual recompilation trigger.");
    RTX_OPTION("rtx", float, emissiveIntensity, 1.0f, "");
    RTX_OPTION("rtx", float, fireflyFilteringLuminanceThreshold, 1000.0f, "Maximum luminance threshold for the firefly filtering to clamp to.");
    RTX_OPTION("rtx", float, vertexColorStrength, 0.6f, "");
    RTX_OPTION("rtx", bool, allowFSE, false, "");
    RTX_OPTION("rtx", std::string, baseGameModRegex, "", "Regex used to determine if the base game is running a mod, like a sourcemod.");
    RTX_OPTION("rtx", std::string, baseGameModPathRegex, "", "Regex used to redirect RTX Remix Runtime to another path for replacements and rtx.conf.");

  public:
    struct ViewModel {
      friend class ImGUI;
      RTX_OPTION("rtx.viewModel", bool, enable, false, "If true, try to resolve view models (e.g. first-person weapons). World geometry doesn't have shadows / reflections / etc from the view models.");
      RTX_OPTION("rtx.viewModel", float, rangeMeters, 1.0f, "[meters] Max distance at which to find a portal for view model virtual instances. If rtx.viewModel.separateRays is true, this is also max length of view model rays.");
      RTX_OPTION("rtx.viewModel", float, scale, 1.0f, "Scale for view models. Minimize to prevent clipping.");
      RTX_OPTION("rtx.viewModel", bool, enableVirtualInstances, true, "If true, virtual instances are created to render the view models behind a portal.");
      RTX_OPTION("rtx.viewModel", bool, perspectiveCorrection, true, "If true, apply correction to view models (e.g. different FOV is used for view models).");
      RTX_OPTION("rtx.viewModel", bool, separateRays, false, "If true, launch additional primary rays to render view models on top of everything.");
    } viewModel;

  public:
    struct PlayerModel {
      friend class ImGUI;
      RTX_OPTION("rtx.playerModel", bool, enableVirtualInstances, true, "");
      RTX_OPTION("rtx.playerModel", bool, enableInPrimarySpace, false, "");
      RTX_OPTION("rtx.playerModel", bool, enablePrimaryShadows, true, "");
      RTX_OPTION("rtx.playerModel", float, backwardOffset, 18.f, "");
      RTX_OPTION("rtx.playerModel", float, horizontalDetectionDistance, 34.f, "");
      RTX_OPTION("rtx.playerModel", float, verticalDetectionDistance, 64.f, "");
      RTX_OPTION("rtx.playerModel", float, eyeHeight, 64.f, "");
      RTX_OPTION("rtx.playerModel", float, intersectionCapsuleRadius, 24.f, "");
      RTX_OPTION("rtx.playerModel", float, intersectionCapsuleHeight, 68.f, "");
    } playerModel;

    RTX_OPTION("rtx", bool, resolvePreCombinedMatrices, true, "");
    RTX_OPTION("rtx", bool, useVertexCapture, false, "");
    RTX_OPTION("rtx", uint32_t, minPrimsInStaticBLAS, 1000, "");
    RTX_OPTION("rtx", uint32_t, maxPrimsInMergedBLAS, 50000, "");
    
    // Camera
    RTX_OPTION("rtx", bool, shakeCamera, false, "");
    RTX_OPTION("rtx", CameraAnimationMode, cameraAnimationMode, CameraAnimationMode::CameraShake_Pitch, "");
    RTX_OPTION("rtx", int, cameraShakePeriod, 20, "");
    RTX_OPTION("rtx", float, cameraAnimationAmplitude, 2.0f, "");
    RTX_OPTION("rtx", bool, skipObjectsWithUnknownCamera, false, "");
    RTX_OPTION("rtx", bool, enableNearPlaneOverride, false, "");
    RTX_OPTION("rtx", float, nearPlaneOverride, 0.1f, "");

    RTX_OPTION("rtx", bool, useRayPortalVirtualInstanceMatching, true, "");
    RTX_OPTION("rtx", bool, enablePortalFadeInEffect, false, "");

    RTX_OPTION_ENV("rtx", bool, useRTXDI, true, "DXVK_USE_RTXDI", "");
    RTX_OPTION_ENV("rtx", bool, useReSTIRGI, true, "DXVK_USE_RESTIR_GI", "");
    RTX_OPTION_ENV("rtx", UpscalerType, upscalerType, UpscalerType::DLSS, "DXVK_UPSCALER_TYPE", "Upscaling boosts performance with varying degrees of image quality tradeoff depending on the type of upscaler and the quality mode/preset.");
    RTX_OPTION("rtx", float, resolutionScale, 0.75f, "");
    RTX_OPTION("rtx", bool, forceCameraJitter, false, "");
    RTX_OPTION("rtx", bool, enableDirectLighting, true, "");
    RTX_OPTION("rtx", bool, enableSecondaryBounces, true, "");
    RTX_OPTION("rtx", bool, zUp, false, "Indicates that the Z axis is the \"upward\" axis in the world when true, otherwise the Y axis when false.");
    RTX_OPTION("rtx", float, uniqueObjectDistance, 300.f, "[cm]");
    RTX_OPTION_FLAG("rtx", UIType, showUI, UIType::None, RtxOptionFlags::NoSave | RtxOptionFlags::NoReset, "0 = Don't Show, 1 = Show Simple, 2 = Show Advanced.");
    RTX_OPTION_FLAG("rtx", bool, defaultToAdvancedUI, false, RtxOptionFlags::NoReset, "");
    RTX_OPTION("rtx", bool, showUICursor, false, "");
    RTX_OPTION_FLAG("rtx", bool, blockInputToGameInUI, true, RtxOptionFlags::NoSave, "");
    RTX_OPTION("rtx", bool, hideSplashMessage, false, "");
  private:
    VirtualKeys m_remixMenuKeyBinds;
  public:
    const VirtualKeys& remixMenuKeyBinds() const { return m_remixMenuKeyBinds; }

    RTX_OPTION("rtx", DLSSProfile, qualityDLSS, DLSSProfile::Auto, "Adjusts internal DLSS scaling factor, trades quality for performance.");
    // Note: All ray tracing modes depend on the rtx.raytraceModePreset option as they may be overridden by automatic defaults for a specific vendor if the preset is set to Auto. Set
    // to Custom to ensure these settings are not overridden.
    //RenderPassVolumeIntegrateRaytraceMode renderPassVolumeIntegrateRaytraceMode = RenderPassVolumeIntegrateRaytraceMode::RayQuery;
    RTX_OPTION_ENV("rtx", RenderPassGBufferRaytraceMode, renderPassGBufferRaytraceMode, RenderPassGBufferRaytraceMode::TraceRay, "DXVK_RENDER_PASS_GBUFFER_RAYTRACE_MODE", "");
    RTX_OPTION_ENV("rtx", RenderPassIntegrateDirectRaytraceMode, renderPassIntegrateDirectRaytraceMode, RenderPassIntegrateDirectRaytraceMode::RayQuery, "DXVK_RENDER_PASS_INTEGRATE_DIRECT_RAYTRACE_MODE", "");
    RTX_OPTION_ENV("rtx", RenderPassIntegrateIndirectRaytraceMode, renderPassIntegrateIndirectRaytraceMode, RenderPassIntegrateIndirectRaytraceMode::TraceRay, "DXVK_RENDER_PASS_INTEGRATE_INDIRECT_RAYTRACE_MODE", "");
    RTX_OPTION("rtx", bool, captureDebugImage, false, "");

    // Denoiser Options
    RTX_OPTION_ENV("rtx", bool, useDenoiser, true, "DXVK_USE_DENOISER", "");
    RTX_OPTION_ENV("rtx", bool, useDenoiserReferenceMode, false, "DXVK_USE_DENOISER_REFERENCE_MODE", "");
    RTX_OPTION_ENV("rtx", bool, denoiseDirectAndIndirectLightingSeparately, true, "DXVK_DENOISE_DIRECT_AND_INDIRECT_LIGHTING_SEPARATELY", "Denoising quality, high uses separate denoising of direct and indirect lighting for higher quality at the cost of performance.");
    RTX_OPTION("rtx", bool, replaceDirectSpecularHitTWithIndirectSpecularHitT, true, "");
    RTX_OPTION("rtx", bool, adaptiveResolutionDenoising, true, "");
    RTX_OPTION_ENV("rtx", bool, adaptiveAccumulation, true, "DXVK_USE_ADAPTIVE_ACCUMULATION", "");

    RTX_OPTION("rtx", uint32_t, numFramesToKeepInstances, 1, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepBLAS, 4, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepLights, 100, ""); // NOTE: This was the default we've had for a while, can probably be reduced...
    RTX_OPTION("rtx", uint32_t, numFramesToKeepGeometryData, 5, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepMaterialTextures, 30, "");
    RTX_OPTION("rtx", bool, enablePreviousTLAS, true, "");
    RTX_OPTION("rtx", float, sceneScale, 1, "Defines the ratio of rendering unit (1cm) to game unit, i.e. sceneScale = 1cm / GameUnit.");


    // Resolve Options
    RTX_OPTION("rtx", uint8_t, primaryRayMaxInteractions, 32, "");
    RTX_OPTION("rtx", uint8_t, psrRayMaxInteractions, 32, "");
    RTX_OPTION("rtx", uint8_t, secondaryRayMaxInteractions, 8, "");
    RTX_OPTION("rtx", bool, enableSeparateUnorderedApproximations, true, "Use a separate loop for surfaces which can have lighting evaluated in an approximate unordered way on each path segment. This improves performance typically.");
    RTX_OPTION("rtx", bool, enableDirectTranslucentShadows, false, "Include OBJECT_MASK_TRANSLUCENT into primary visibility rays.");
    RTX_OPTION("rtx", bool, enableIndirectTranslucentShadows, false, "Include OBJECT_MASK_TRANSLUCENT into secondary visibility rays.");

    RTX_OPTION("rtx", float, resolveTransparencyThreshold, 1 / 255.f, "x <= threshold : transparent surface.");
    RTX_OPTION("rtx", float, resolveOpaquenessThreshold, 254 / 255.f, "x >= threshold : opaque surface.");

    // PSR Options
    RTX_OPTION("rtx", bool, enablePSRR, true, "Enable reflection PSR.");
    RTX_OPTION("rtx", bool, enablePSTR, true, "Enable transmission PSR.");
    RTX_OPTION("rtx", uint8_t, psrrMaxBounces, 10, "The maximum number of Reflection PSR bounces to traverse. Must be 15 or less due to payload encoding.");
    RTX_OPTION("rtx", uint8_t, pstrMaxBounces, 10, "The maximum number of Transmission PSR bounces to traverse. Must be 15 or less due to payload encoding.");
    RTX_OPTION("rtx", bool, enablePSTROutgoingSplitApproximation, true, "Enable transmission PSR on outgoing transmission event possibilities (rather than respecting no-split path PSR rule).");
    RTX_OPTION("rtx", bool, enablePSTRSecondaryIncidentSplitApproximation, true, "Enable transmission PSR on secondary incident transmission event possibilities (rather than respecting no-split path PSR rule).");
    
    // Any PSR reflection or transmission from a surface with 'normalDetail' over these value will generate a 1.0 in the disocclusionThresholdMix mask.
    // Note that 0 is a valid setting as it means that any detail at all, no matter how small, will set that mask bit.
    RTX_OPTION("rtx", float, psrrNormalDetailThreshold, 0.0, ""); 
    RTX_OPTION("rtx", float, pstrNormalDetailThreshold, 0.0, "");

    // Shader Execution Reordering Options
    RTX_OPTION_FULL("rtx", bool, isShaderExecutionReorderingSupported, false, "DXVK_IS_SHADER_EXECUTION_REORDERING_SUPPORTED", RtxOptionFlags::NoSave, ""); // To be removed / not written to docs
    RTX_OPTION("rtx", bool, enableShaderExecutionReorderingInPathtracerGbuffer, false, "");
    RTX_OPTION("rtx", bool, enableShaderExecutionReorderingInPathtracerIntegrateIndirect, true, "");

    // Path Options
    RTX_OPTION("rtx", bool, enableRussianRoulette, true, "");
    RTX_OPTION("rtx", float, russianRouletteMaxContinueProbability, 0.9f, "The maximum probability of continuing a path when Russian Roulette is being used.");
    RTX_OPTION("rtx", float, russianRoulette1stBounceMinContinueProbability, 0.6f, "The minimum probability of continuing a path when Russian Roulette is being used on the first bounce.");
    RTX_OPTION("rtx", float, russianRoulette1stBounceMaxContinueProbability, 1.f, "The maximum probability of continuing a path when Russian Roulette is being used on the first bounce.");
    RTX_OPTION_ENV("rtx", uint8_t, pathMinBounces, 1, "DXVK_PATH_TRACING_MIN_BOUNCES", "The minimum number of indirect bounces the path must complete before Russian Roulette can be used. Must be < 16.");
    RTX_OPTION_ENV("rtx", uint8_t, pathMaxBounces, 4, "DXVK_PATH_TRACING_MAX_BOUNCES", "The maximum number of indirect bounces the path will be allowed to complete. Higher values result in better indirect lighting, lower values result in better performance. Must be < 16.");
    // Note: Use caution when adjusting any zero thresholds as values too high may cause entire lobes of contribution to be missing in material edge cases. For example
    // with translucency, a zero threshold on the specular lobe of 0.05 removes the entire contribution when viewing straight on for any glass with an IoR below 1.58 or so
    // which can be paticularly noticable in some scenes. To bias sampling more in the favor of one lobe the min probability should be used instead, but be aware this will
    // end up wasting more samples in some cases versus pure importance sampling (but may help denoising if it cannot deal with super sparse signals).
    RTX_OPTION("rtx", float, opaqueDiffuseLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque diffuse probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueDiffuseLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque diffuse probability weights.");
    RTX_OPTION("rtx", float, opaqueSpecularLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque specular probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueSpecularLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque specular probability weights.");
    RTX_OPTION("rtx", float, opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque opacity probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueOpacityTransmissionLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque opacity probability weights.");
    // Note: 0.01 chosen as mentioned before to avoid cutting off reflection lobe on most common types of glass when looking straight on (a base reflectivity
    // of 0.01 corresponds to an IoR of 1.22 or so). Avoid changing this default without good reason to prevent glass from losing its reflection contribution.
    RTX_OPTION("rtx", float, translucentSpecularLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero translucent specular probability weight values.");
    RTX_OPTION("rtx", float, minTranslucentSpecularLobeSamplingProbability, 0.3f, "The minimum allowed non-zero value for translucent specular probability weights.");
    RTX_OPTION("rtx", float, translucentTransmissionLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero translucent transmission probability weight values.");
    RTX_OPTION("rtx", float, minTranslucentTransmissionLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for translucent transmission probability weights.");
    RTX_OPTION("rtx", float, indirectRaySpreadAngleFactor, 0.05f, "A tuning factor for the spread angle calculated from the sampled lobe solid angle PDF.");
    RTX_OPTION("rtx", bool, rngSeedWithFrameIndex, true, "");
    RTX_OPTION("rtx", bool, enableFirstBounceLobeProbabilityDithering, true, "");
    RTX_OPTION("rtx", bool, enableUnorderedResolveInIndirectRays, true, "");
    RTX_OPTION_ENV("rtx", bool, enableEmissiveParticlesInIndirectRays, false, "DXVK_EMISSIVE_INDIRECT_PARTICLES", "");
    RTX_OPTION("rtx", bool, enableDecalMaterialBlending, true, "");
    RTX_OPTION("rtx", bool, enableBillboardOrientationCorrection, true, "");
    RTX_OPTION("rtx", bool, useIntersectionBillboardsOnPrimaryRays, false, "");
    RTX_OPTION("rtx", float, translucentDecalAlbedoFactor, 10.f, "Scale for the albedo of decals that are applied to a translucent base material, to make the decals more visible.");
    RTX_OPTION("rtx", float, decalNormalOffset, 0.003f, "Distance along normal between two adjacent decals.");
    RTX_OPTION("rtx", float, worldSpaceUiBackgroundOffset, -0.01f, "Distance along normal to offset the background of screens.");

    // Light Selection/Sampling Options
    RTX_OPTION("rtx", uint16_t, risLightSampleCount, 7, "The number of lights randomly selected from the global pool to consider when selecting a light with RIS.");

    // Volumetrics Options
    // Note: The effective froxel grid resolution (based on the resolution scale) and froxelDepthSlices when multiplied together give the number of froxel cells, and this should be greater than the maximum number of
    // "concurrent" threads the GPU can execute at once to saturate execution and ensure maximal occupancy. This can be calculated by looking at how many warps per multiprocessor the GPU can have at once (This can
    // be found in CUDA Tuning guides such as https://docs.nvidia.com/cuda/ampere-tuning-guide/index.html) and then multiplying it by the number of multiprocessors (SMs) on the GPU in question, and finally turning
    // this into a thread count by mulitplying by how many threads per warp there are (typically 32).
    // Example for a RTX 3090: 82 SMs * 64 warps per SM * 32 threads per warp = 167,936 froxels to saturate the GPU. It is fine to be a bit below this though as most gpus will have fewer SMs than this, and higher resolutions
    // will also use more froxels due to how the grid is allocated with respect to the (downscaled when DLSS is in use) resolution, and we don't want the froxel passes to be too expensive (unless higher quality results are desired).
    RTX_OPTION("rtx", uint32_t, froxelGridResolutionScale, 16, "The scale factor to divide the x and y render resolution by to determine the x and y dimensions of the froxel grid.");
    RTX_OPTION("rtx", uint16_t, froxelDepthSlices, 48, "The z dimension of the froxel grid. Must be constant after initialization.");
    RTX_OPTION("rtx", uint8_t, maxAccumulationFrames, 254, "The number of frames to accumulate volume lighting samples over. More results in greater image stability at the cost of potentially more temporal lag.");
    RTX_OPTION("rtx", float, froxelDepthSliceDistributionExponent, 2.0f, "The exponent to use on depth values to nonlinearly distribute froxels away from the camera. Higher values bias more froxels closer to the camera with 1 being linear.");
    RTX_OPTION("rtx", float, froxelMaxDistance, 2000.0f, "The maximum distance in world units to allocate the froxel grid out to. Should be less than the distance between the camera's near and far plane, as the froxel grid will clip to the far plane otherwise.");
    RTX_OPTION("rtx", float, froxelFireflyFilteringLuminanceThreshold, 1000.0f, "Sets the maximum luminance threshold for the volumetric firefly filtering to clamp to.");
    RTX_OPTION("rtx", float, froxelFilterGaussianSigma, 1.2f, "The sigma value of the gaussian function used to filter volumetric radiance values. Larger values cause a smoother filter to be used.");
    RTX_OPTION("rtx", uint32_t, volumetricInitialRISSampleCount, 8, "The number of RIS samples to select from the global pool of lights when constructing a Reservoir sample.");
    RTX_OPTION("rtx", bool, volumetricEnableInitialVisibility, true, "Determines whether to trace a visibility ray for Reservoir samples.");
    RTX_OPTION("rtx", bool, volumetricEnableTemporalResampling, true, "Indicates if temporal resampling should be used for volume integration.");
    RTX_OPTION("rtx", uint16_t, volumetricTemporalReuseMaxSampleCount, 200, "The number of samples to clamp temporal reservoirs to, should usually be around the value: desired_max_history_frames * average_reservoir_samples.");
    RTX_OPTION("rtx", float, volumetricClampedReprojectionConfidencePenalty, 0.5f, "The penalty from [0, 1] to apply to the sample count of temporally reprojected reservoirs when reprojection is clamped to the fustrum (indicating lower quality reprojection).");
    RTX_OPTION("rtx", uint8_t, froxelMinReservoirSamples, 1, "The minimum number of Reservoir samples to do for each froxel cell when stability is at its maximum, should be at least 1.");
    RTX_OPTION("rtx", uint8_t, froxelMaxReservoirSamples, 6, "The maximum number of Reservoir samples to do for each froxel cell when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.");
    RTX_OPTION("rtx", uint8_t, froxelMinKernelRadius, 2, "The minimum filtering kernel radius to use when stability is at its maximum, should be at least 1.");
    RTX_OPTION("rtx", uint8_t, froxelMaxKernelRadius, 4, "The maximum filtering kernel radius to use when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.");
    RTX_OPTION("rtx", uint8_t, froxelMinReservoirSamplesStabilityHistory, 1, "The minimum history to consider history at minimum stability for Reservoir samples.");
    RTX_OPTION("rtx", uint8_t, froxelMaxReservoirSamplesStabilityHistory, 64, "The maximum history to consider history at maximum stability for Reservoir samples.");
    RTX_OPTION("rtx", uint8_t, froxelMinKernelRadiusStabilityHistory, 1, "The minimum history to consider history at minimum stability for filtering.");
    RTX_OPTION("rtx", uint8_t, froxelMaxKernelRadiusStabilityHistory, 64, "The maximum history to consider history at maximum stability for filtering.");
    RTX_OPTION("rtx", float, froxelReservoirSamplesStabilityHistoryPower, 2.0f, "The power to apply to the Reservoir sample stability history weight.");
    RTX_OPTION("rtx", float, froxelKernelRadiusStabilityHistoryPower, 2.0f, "The power to apply to the kernel radius stability history weight.");
    RTX_OPTION("rtx", bool, enableVolumetricLighting, false, "Enabling volumetric lighting provides higher quality ray traced physical volumetrics, disabling falls back to cheaper depth based fog. Note: it does not disable the volume radiance cache as a whole as it is still needed for particles.");
    RTX_OPTION("rtx", Vector3, volumetricTransmittanceColor, Vector3(0.9f, 0.85f, 0.8f), "The color to use for calculating transmittance measured at a specific distance.");
    RTX_OPTION("rtx", float, volumetricTransmittanceMeasurementDistance, 10000.0f, "The distance the specified transmittance color was measured at. Lower distances indicate a denser medium.");
    RTX_OPTION("rtx", Vector3, volumetricSingleScatteringAlbedo, Vector3(0.9f, 0.9f, 0.9f), "The single scattering albedo (otherwise known as the particle albedo) representing the ratio of scattering to absorption.");
    RTX_OPTION("rtx", float, volumetricAnisotropy, 0.0f, "The anisotropy of the scattering phase function (-1 being backscattering, 0 being isotropic, 1 being forward scattering).");
    RTX_OPTION("rtx", bool, enableVolumetricsInPortals, true, "Enables using extra frustum-aligned volumes for lighting in portals.");

    // Note: Options for remapping legacy D3D9 fixed function fog parameters to volumetric lighting parameters and overwriting the global volumetric parameters when fixed function fog is enabled.
    // Useful for cases where dynamic fog parameters are used throughout a game (or very per-level) that cannot be captrued merely in a global set of volumetric parameters. To see remapped results
    // volumetric lighting in general must be enabled otherwise these settings will have no effect.
    RTX_OPTION("rtx", bool, enableFogRemap, false, "");
    RTX_OPTION("rtx", float, fogRemapMaxDistanceMin, 100.0f, "");
    RTX_OPTION("rtx", float, fogRemapMaxDistanceMax, 4000.0f, "");
    RTX_OPTION("rtx", float, fogRemapTransmittanceMeasurementDistanceMin, 2000.0f, "");
    RTX_OPTION("rtx", float, fogRemapTransmittanceMeasurementDistanceMax, 12000.0f, "");
    RTX_OPTION("rtx", float, fogRemapColorStrength, 1.0f, "");

    // Note: Cached values used to precompute quantities for options fetching to not have to needlessly recompute them.
    uint8_t cachedFroxelReservoirSamplesStabilityHistoryRange;
    uint8_t cachedFroxelKernelRadiusStabilityHistoryRange;

    // Alpha Test/Blend Options
    RTX_OPTION("rtx", bool, enableAlphaBlend, true, "Enable rendering alpha blended geometry, used for partial opacity and other blending effects on various surfaces in many games.");
    RTX_OPTION("rtx", bool, enableAlphaTest, true, "Enable rendering alpha tested geometry, used for cutout style opacity in some games.");
    RTX_OPTION("rtx", bool, enableCulling, true, "Enable culling for opaque objects. Objects with alpha blend or alpha test are not culled.");
    RTX_OPTION("rtx", bool, enableEmissiveBlendEmissiveOverride, true, "Override typical material emissive information on draw calls with any emissive blending modes to emulate their original look more accurately.");
    RTX_OPTION("rtx", float, emissiveBlendOverrideEmissiveIntensity, 0.2f, "The emissive intensity to use when the emissive blend override is enabled. Adjust this if particles for example look overly bright globally.");
    RTX_OPTION("rtx", float, particleSoftnessFactor, 0.05f, "Multiplier for the view distance that is used to calculate the particle blending range.");
    RTX_OPTION("rtx", float, forceCutoutAlpha, 0.5f, "When an object is added to cutoutTextures, its surface with alpha less than this value will get discarded. This is meant to improve on legacy, low-resolution textures that use blended transparency instead of alpha cutout, which can result in blurry halos around edges. This is generally best handled by generating replacement assets that use either fully opaque, detailed geometry, or fully transparent alpha cutouts on higher resolution textures. Rendered output might still look incorrect even with this flag.");

    // Ray Portal Options
    // Note: Not a set as the ordering of the hashes is important. Keep this list small to avoid expensive O(n) searching (should only have 2 or 4 elements usually).
    // Also must always be a multiple of 2 for proper functionality as each pair of hashes defines a portal connection.
    RTX_OPTION("rtx", std::vector<XXH64_hash_t>, rayPortalModelTextureHashes, {}, "Texture hashes identifying ray portals. Allowed number of hashes: {0, 2}.");
    // Todo: Add option for if a model to world transform matrix should be used or if PCA should be used instead to attempt to guess what the matrix should be (for games with
    // pretransformed Ray Portal vertices).
    // Note: Axes used for orienting the portal when PCA is used.
    RTX_OPTION("rtx", Vector3, rayPortalModelNormalAxis, Vector3(0.0f, 0.0f, 1.0f), "");
    RTX_OPTION("rtx", Vector3, rayPortalModelWidthAxis, Vector3(1.0f, 0.0f, 0.0f), "");
    RTX_OPTION("rtx", Vector3, rayPortalModelHeightAxis, Vector3(0.0f, 1.0f, 0.0f), "");
    RTX_OPTION("rtx", float, rayPortalSamplingWeightMinDistance, 10.0f, "");
    RTX_OPTION("rtx", float, rayPortalSamplingWeightMaxDistance, 1000.0f, "");
    RTX_OPTION("rtx", bool, rayPortalCameraHistoryCorrection, false, "");
    RTX_OPTION("rtx", bool, rayPortalCameraInBetweenPortalsCorrection, false, "");

    RTX_OPTION("rtx", bool, useWhiteMaterialMode, false, "");
    RTX_OPTION("rtx", bool, useHighlightLegacyMode, false, "");
    RTX_OPTION("rtx", bool, useHighlightUnsafeAnchorMode, false, "");
    RTX_OPTION("rtx", bool, useHighlightUnsafeReplacementMode, false, "");
    RTX_OPTION("rtx", float, nativeMipBias, 0.f, "");
    RTX_OPTION("rtx", float, upscalingMipBias, 0.f, "");
    RTX_OPTION("rtx", bool, useAnisotropicFiltering, true, "");
    RTX_OPTION("rtx", float, maxAnisotropyLevel, 8.0f, "Min of this and the hardware device limits.");

    // Developer Options
    RTX_OPTION("rtx", bool, enableDeveloperOptions, false, "");
    RTX_OPTION("rtx", Vector2i, drawCallRange, Vector2i(0, INT32_MAX), "");
    RTX_OPTION("rtx", Vector3, instanceOverrideWorldOffset, Vector3(0.f, 0.f, 0.f), "");
    RTX_OPTION("rtx", uint, instanceOverrideInstanceIdx, UINT32_MAX, "");
    RTX_OPTION("rtx", uint, instanceOverrideInstanceIdxRange, 15, "");
    RTX_OPTION("rtx", bool, instanceOverrideSelectedInstancePrintMaterialHash, false, "");
    // adds a fixed delay after present, for those hot summer days...
    RTX_OPTION("rtx", bool, enablePresentThrottle, false, "");
    RTX_OPTION("rtx", int32_t, presentThrottleDelay, 16, "[ms]");
    RTX_OPTION_ENV("rtx", bool, validateCPUIndexData, false, "DXVK_VALIDATE_CPU_INDEX_DATA", "");

    struct OpacityMicromap
    {
      friend class RtxOptions;
      friend class ImGUI;
      bool isSupported = false;
      RTX_OPTION_ENV("rtx.opacityMicromap", bool, enable, true, "DXVK_ENABLE_OPACITY_MICROMAP", "");
    } opacityMicromap;

    RTX_OPTION("rtx", ReflexMode, reflexMode, ReflexMode::LowLatency, "Reflex mode selection, enabling it helps minimize input latency, boost mode may further reduce latency by boosting GPU clocks in CPU-bound cases."); // default to low-latency (not boost)
    RTX_OPTION_FLAG("rtx", bool, isReflexSupported, true, RtxOptionFlags::NoSave, "");// default to true, we will do a compat check during init and disable if not supported

    RTX_OPTION_FLAG("rtx", bool, forceVsyncOff, false, RtxOptionFlags::NoSave, "");

    // Replacement options
    RTX_OPTION("rtx", bool, enableReplacementAssets, true, "Enables all enhanced asset replacements (materials, meshes, lights).");
    RTX_OPTION("rtx", bool, enableReplacementLights, true, "Enables enhanced light replacements.");
    RTX_OPTION("rtx", bool, enableReplacementMeshes, true, "Enables enhanced mesh replacements.");
    RTX_OPTION("rtx", bool, enableReplacementMaterials, true, "Enables enhanced material replacements.");
    RTX_OPTION("rtx", bool, enableAdaptiveResolutionReplacementTextures, true, "");
    RTX_OPTION("rtx", bool, forceHighResolutionReplacementTextures, false, "");
    RTX_OPTION("rtx", int,  skipReplacementTextureMipMapLevel, 0, "The texture resolution to use, lower resolution textures may improve performance and reduce video memory usage.");
    RTX_OPTION("rtx", int,  assetEstimatedSizeGB, 2, "");
    RTX_OPTION_ENV("rtx", bool, enableAsyncTextureUpload, true, "DXVK_ASYNC_TEXTURE_UPLOAD", "");
    RTX_OPTION_ENV("rtx", bool, alwaysWaitForAsyncTextures, false, "DXVK_WAIT_ASYNC_TEXTURES", "");
    RTX_OPTION("rtx", int,  asyncTextureUploadPreloadMips, 8, "");
    RTX_OPTION("rtx", bool, usePartialDdsLoader, true, "");

    RTX_OPTION("rtx", TonemappingMode, tonemappingMode, TonemappingMode::Local, "");

    // Capture Options
    //   General
    RTX_OPTION("rtx", bool, captureNoInstance, false, "");
    RTX_OPTION("rtx", std::string, captureInstanceStageName, "", "");
    RTX_OPTION("rtx", uint32_t, captureMaxFrames, 1, "");
    RTX_OPTION("rtx", uint32_t, captureFramesPerSecond, 24, "");
    //   Mesh
    RTX_OPTION("rtx", float, captureMeshPositionDelta, 0.3f, "Inter-frame position min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshNormalDelta, 0.3f, "Inter-frame normal min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshTexcoordDelta, 0.3f, "Inter-frame texcoord min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshColorDelta, 0.3f, "Inter-frame color min delta warrants new time sample.");

    RTX_OPTION("rtx", bool, calculateMeshBoundingBox, false, "Calculate bounding box for every mesh.");

    RTX_OPTION("rtx", bool, resetBufferCacheOnEveryFrame, true, "");

    // Note: this will cause history rejection when camera gets teleported through portal (TREX-641)
    RTX_OPTION("rtx", bool, useVirtualShadingNormalsForDenoising, true, "");
    RTX_OPTION("rtx", bool, resetDenoiserHistoryOnSettingsChange, false, "");

    RTX_OPTION("rtx", float, skyBrightness, 1.f, "");
    RTX_OPTION("rtx", bool, skyForceHDR, false, "By default sky will be rasterized in the color format used by the game. Set the checkbox to force sky to be rasterized in HDR intermediate format. This may be important when sky textures replaced with HDR textures.");
    RTX_OPTION("rtx", uint32_t, skyProbeSide, 1024, "");
    RTX_OPTION_FLAG("rtx", uint32_t, skyUiDrawcallCount, 0, RtxOptionFlags::NoSave, "");
    RTX_OPTION("rtx", uint32_t, skyDrawcallIdThreshold, 0, "It's common in games to render the skybox first, and so, this value provides a simple mechanism to identify those early draw calls that are untextured (textured draw calls can still use the Sky Textures functionality.");

    // TODO (REMIX-656): Remove this once we can transition content to new hash
    RTX_OPTION("rtx", bool, logLegacyHashReplacementMatches, false, "");

    RTX_OPTION("rtx", FusedWorldViewMode, fusedWorldViewMode, FusedWorldViewMode::None, "Set if game uses a fused World-View transform matrix.");

    RTX_OPTION_FLAG("rtx", XXH64_hash_t, highlightedTexture, kEmptyHash, RtxOptionFlags::NoSave, "Hash of a texture that should be highlighted.");

  public:
    LegacyMaterialDefaults legacyMaterial;
    OpaqueMaterialOptions opaqueMaterialOptions;
    TranslucentMaterialOptions translucentMaterialOptions;
    ViewDistanceOptions viewDistanceOptions;

    HashRule GeometryHashGenerationRule = 0;
    HashRule GeometryAssetHashRule = 0;

  private:
    // These cannot be overridden, and should match the defaults in the respective MDLs
    const OpaqueMaterialDefaults opaqueMaterialDefaults{};
    const TranslucentMaterialDefaults translucentMaterialDefaults{};
    const RayPortalMaterialDefaults rayPortalMaterialDefaults{};
    const SharedMaterialDefaults sharedMaterialDefaults{};

    int initialSkipReplacementTextureMipMapLevel;

    RTX_OPTION("rtx", float, effectLightIntensity, 1.f, "");
    RTX_OPTION("rtx", float, effectLightRadius, 5.f, "");
    RTX_OPTION("rtx", bool, effectLightPlasmaBall, false, "");

    // Whether or not to use slower XXH64 hash on texture upload. New projects should not enable this option.
    RTX_OPTION("rtx", bool, useObsoleteHashOnTextureUpload, false, "");

    RTX_OPTION("rtx", bool, serializeChangedOptionOnly, true, "");

    RTX_OPTION("rtx", bool, isLHS, false, "");
    RTX_OPTION("rtx", bool, ignoreStencilVolumeHeuristics, true, "Tries to detect stencil volumes and ignore those when pathtracing.  Stencil buffer was used for a variety of effects in the D3D7-9 era, mostly for geometry based lights and shadows - things we don't need when pathtracing.");
    
    RTX_OPTION("rtx", uint32_t, applicationId, 102100511, "Used for DLSS.");

    static std::unique_ptr<RtxOptions> pInstance;
    RtxOptions() { }

    // Note: Should be called whenever the min/max stability history values are changed.
    // Ideally would be done through a setter function but ImGui needs direct access to the original options with how we currently have it set up.
    void updateCachedVolumetricOptions() {
      assert(froxelMaxReservoirSamplesStabilityHistory() >= froxelMinReservoirSamplesStabilityHistory());
      assert(froxelMaxKernelRadiusStabilityHistory() >= froxelMinKernelRadiusStabilityHistory());

      cachedFroxelReservoirSamplesStabilityHistoryRange = froxelMaxReservoirSamplesStabilityHistory() - froxelMinReservoirSamplesStabilityHistory();
      cachedFroxelKernelRadiusStabilityHistoryRange = froxelMaxKernelRadiusStabilityHistory() - froxelMinKernelRadiusStabilityHistory();
    }

  public:

    RtxOptions(const Config& options) {
      if (sourceRootPath() == "./")
        sourceRootPathRef() = getCurrentDirectory() + "/";

      RTX_OPTION_CLAMP_MIN(emissiveIntensity, 0.0f);
      // Note: Clamp to positive values as negative luminance thresholds are not valid.
      RTX_OPTION_CLAMP_MIN(fireflyFilteringLuminanceThreshold, 0.0f);
      RTX_OPTION_CLAMP(vertexColorStrength, 0.0f, 1.0f);
   
      // Render pass modes

      //renderPassVolumeIntegrateRaytraceMode = (RenderPassVolumeIntegrateRaytraceMode) std::min(
      //  options.getOption<uint32_t>("rtx.renderPassVolumeIntegrateRaytraceMode", (uint32_t) renderPassVolumeIntegrateRaytraceMode, "DXVK_RENDER_PASS_VOLUME_INTEGRATE_RAYTRACE_MODE"),
      //  (uint32_t) (RenderPassVolumeIntegrateRaytraceMode::Count) -1);

      renderPassGBufferRaytraceModeRef() = (RenderPassGBufferRaytraceMode) std::min(
        (uint32_t) renderPassGBufferRaytraceMode(),
        (uint32_t) (RenderPassGBufferRaytraceMode::Count) -1);

      renderPassIntegrateDirectRaytraceModeRef() = (RenderPassIntegrateDirectRaytraceMode) std::min(
        (uint32_t) renderPassIntegrateDirectRaytraceMode(),
        (uint32_t) (RenderPassIntegrateDirectRaytraceMode::Count) - 1);
      
      renderPassIntegrateIndirectRaytraceModeRef() = (RenderPassIntegrateIndirectRaytraceMode) std::min(
        (uint32_t) renderPassIntegrateIndirectRaytraceMode(),
        (uint32_t) (RenderPassIntegrateIndirectRaytraceMode::Count) - 1);

      // Pathtracing options
      //enableShaderExecutionReorderingInVolumeIntegrate =
      //  options.getOption<bool>("rtx.enableShaderExecutionReorderingInVolumeIntegrate", enableShaderExecutionReorderingInVolumeIntegrate);
      //enableShaderExecutionReorderingInPathtracerIntegrateDirect =
      //  options.getOption<bool>("rtx.enableShaderExecutionReorderingInPathtracerIntegrateDirect", enableShaderExecutionReorderingInPathtracerIntegrateDirect);

      // Resolve Options

      // Note: Clamped due to 8 bit usage on GPU.
      RTX_OPTION_CLAMP(primaryRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(psrRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(secondaryRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(resolveTransparencyThreshold, 0.f, 1.f);
      RTX_OPTION_CLAMP(resolveOpaquenessThreshold, resolveTransparencyThreshold(), 1.f);

      // PSR Options
      
      // Note: Clamped due to 8 bit usage on GPU.
      RTX_OPTION_CLAMP(psrrMaxBounces, static_cast<uint8_t>(1), static_cast<uint8_t>(254));
      RTX_OPTION_CLAMP(pstrMaxBounces, static_cast<uint8_t>(1), static_cast<uint8_t>(254));
      
      // Path Options
      RTX_OPTION_CLAMP(russianRouletteMaxContinueProbability, 0.0f, 1.0f);
      // Note: Clamped to 15 due to usage on GPU.
      RTX_OPTION_CLAMP(pathMinBounces, static_cast<uint8_t>(0), static_cast<uint8_t>(15));
      // Note: Clamp to the minimum bounce count additionally.
      RTX_OPTION_CLAMP(pathMaxBounces, pathMinBounces(), static_cast<uint8_t>(15));

      // Light Selection/Sampling Options

      // Note: Clamped due to 16 bit usage on GPU.
      RTX_OPTION_CLAMP(risLightSampleCount, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());

      // Volumetrics Options
      RTX_OPTION_CLAMP_MIN(froxelGridResolutionScale, static_cast<uint32_t>(1));
      RTX_OPTION_CLAMP(froxelDepthSlices, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());
      RTX_OPTION_CLAMP(maxAccumulationFrames, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP_MIN(froxelDepthSliceDistributionExponent, 1e-4f);
      RTX_OPTION_CLAMP_MIN(froxelMaxDistance, 0.0f);
      // Note: Clamp to positive values as negative luminance thresholds are not valid.
      RTX_OPTION_CLAMP_MIN(froxelFireflyFilteringLuminanceThreshold, 0.0f);
      RTX_OPTION_CLAMP_MIN(froxelFilterGaussianSigma, 0.0f);

      RTX_OPTION_CLAMP_MIN(volumetricInitialRISSampleCount, static_cast<uint32_t>(1));
      RTX_OPTION_CLAMP(volumetricTemporalReuseMaxSampleCount, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());
      RTX_OPTION_CLAMP(volumetricClampedReprojectionConfidencePenalty, 0.0f, 1.0f);

      RTX_OPTION_CLAMP(froxelMinReservoirSamples, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxReservoirSamples, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinKernelRadius, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxKernelRadius, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinReservoirSamplesStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxReservoirSamplesStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinKernelRadiusStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxKernelRadiusStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP_MIN(froxelReservoirSamplesStabilityHistoryPower, 0.0f);
      RTX_OPTION_CLAMP_MIN(froxelKernelRadiusStabilityHistoryPower, 0.0f);

      RTX_OPTION_CLAMP_MAX(froxelMinReservoirSamples, froxelMaxReservoirSamples());
      RTX_OPTION_CLAMP_MIN(froxelMaxReservoirSamples, froxelMinReservoirSamples());
      RTX_OPTION_CLAMP_MAX(froxelMinKernelRadius, froxelMaxKernelRadius());
      RTX_OPTION_CLAMP_MIN(froxelMaxKernelRadius, froxelMinKernelRadius());
      RTX_OPTION_CLAMP_MAX(froxelMinReservoirSamplesStabilityHistory, froxelMaxReservoirSamplesStabilityHistory());
      RTX_OPTION_CLAMP_MIN(froxelMaxReservoirSamplesStabilityHistory, froxelMinReservoirSamplesStabilityHistory());
      RTX_OPTION_CLAMP_MAX(froxelMinKernelRadiusStabilityHistory, froxelMaxKernelRadiusStabilityHistory());
      RTX_OPTION_CLAMP_MIN(froxelMaxKernelRadiusStabilityHistory, froxelMinKernelRadiusStabilityHistory());

      RTX_OPTION_CLAMP_MIN(volumetricTransmittanceMeasurementDistance, 0.0f);
      RTX_OPTION_CLAMP(volumetricAnisotropy, -1.0f, 1.0f);

      volumetricTransmittanceColorRef().x = std::clamp(volumetricTransmittanceColor().x, 0.0f, 1.0f);
      volumetricTransmittanceColorRef().y = std::clamp(volumetricTransmittanceColor().y, 0.0f, 1.0f);
      volumetricTransmittanceColorRef().z = std::clamp(volumetricTransmittanceColor().z, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().x = std::clamp(volumetricSingleScatteringAlbedo().x, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().y = std::clamp(volumetricSingleScatteringAlbedo().y, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().z = std::clamp(volumetricSingleScatteringAlbedo().z, 0.0f, 1.0f);

      RTX_OPTION_CLAMP_MIN(fogRemapMaxDistanceMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapMaxDistanceMax, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapTransmittanceMeasurementDistanceMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapTransmittanceMeasurementDistanceMax, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapColorStrength, 0.0f);

      fogRemapMaxDistanceMinRef() = std::min(fogRemapMaxDistanceMin(), fogRemapMaxDistanceMax());
      fogRemapMaxDistanceMaxRef() = std::max(fogRemapMaxDistanceMin(), fogRemapMaxDistanceMax());
      fogRemapTransmittanceMeasurementDistanceMinRef() = std::min(fogRemapTransmittanceMeasurementDistanceMin(), fogRemapTransmittanceMeasurementDistanceMax());
      fogRemapTransmittanceMeasurementDistanceMaxRef() = std::max(fogRemapTransmittanceMeasurementDistanceMin(), fogRemapTransmittanceMeasurementDistanceMax());

      updateCachedVolumetricOptions();

      // Alpha Test/Blend Options

      // Note: Clamped to float16 max due to usage on GPU and positive values as emissive intensity values cannot be negative.
      RTX_OPTION_CLAMP(emissiveBlendOverrideEmissiveIntensity, 0.0f, FLOAT16_MAX);
      RTX_OPTION_CLAMP(particleSoftnessFactor, 0.0f, 1.0f);
      
      // Ray Portal Options
      // Note: Ensure the Ray Portal texture hashes are always in pairs of 2
      auto& rayPortalModelTextureHashes = rayPortalModelTextureHashesRef();
      if (rayPortalModelTextureHashes.size() % 2 == 1) {
        rayPortalModelTextureHashes.pop_back();
      }

      if (rayPortalModelTextureHashes.size() > maxRayPortalCount) {
        rayPortalModelTextureHashes.erase(rayPortalModelTextureHashes.begin() + maxRayPortalCount, rayPortalModelTextureHashes.end());
      }

      assert(rayPortalModelTextureHashes.size() % 2 == 0);
      assert(rayPortalModelTextureHashes.size() <= maxRayPortalCount);

      // Note: Ensure the portal sampling weight min and max distance are well defined
      RTX_OPTION_CLAMP_MIN(rayPortalSamplingWeightMinDistance, 0.0f);
      RTX_OPTION_CLAMP_MIN(rayPortalSamplingWeightMaxDistance, 0.0f);
      RTX_OPTION_CLAMP_MAX(rayPortalSamplingWeightMinDistance, rayPortalSamplingWeightMaxDistance());

      assert(rayPortalSamplingWeightMinDistance() >= 0.0f);
      assert(rayPortalSamplingWeightMaxDistance() >= 0.0f);
      assert(rayPortalSamplingWeightMinDistance() <= rayPortalSamplingWeightMaxDistance());
      
      // View Distance Options

      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceThreshold, 0.0f);
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMax, 0.0f);
      RTX_OPTION_CLAMP_MAX(viewDistanceOptions.distanceFadeMin, viewDistanceOptions.distanceFadeMax());
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMax, viewDistanceOptions.distanceFadeMin());

      // Replacement options

      if (env::getEnvVar("DXVK_DISABLE_ASSET_REPLACEMENT") == "1") {
        enableReplacementAssetsRef() = false;
        enableReplacementLightsRef() = false;
        enableReplacementMeshesRef() = false;
        enableReplacementMaterialsRef() = false;
      }

      // Cache this so we don't change during runtime.
      initialSkipReplacementTextureMipMapLevel = skipReplacementTextureMipMapLevel();

      const VirtualKeys& kDefaultRemixMenuKeyBinds { VirtualKey{VK_MENU},VirtualKey{'X'} };
      m_remixMenuKeyBinds = options.getOption<VirtualKeys>("rtx.remixMenuKeyBinds", kDefaultRemixMenuKeyBinds);

      GeometryHashGenerationRule = createRule("Geometry generation", geometryGenerationHashRuleString());
      GeometryAssetHashRule = createRule("Geometry asset", geometryAssetHashRuleString());
    }

    void updateUpscalerFromDlssPreset();
    void updateUpscalerFromNisPreset();
    void updateUpscalerFromTaauPreset();
    void updatePresetFromUpscaler();
    NV_GPU_ARCHITECTURE_ID getNvidiaArch();
    void updateGraphicsPresets(const uint32_t vendorID = 0);
    void updateRaytraceModePresets(const uint32_t vendorID);

    void resetUpscaler();

    inline static const std::string kRtxConfigFilePath = "rtx.conf";

    void serialize() {
      Config newConfig;
      RtxOption<bool>::writeOptions(newConfig, serializeChangedOptionOnly());
      Config::serializeCustomConfig(newConfig, kRtxConfigFilePath, "rtx.");
    }

    void reset() {
      RtxOption<bool>::resetOptions();
    }

    static std::unique_ptr<RtxOptions>& Create(const Config& options) {
      if (pInstance == nullptr)
        pInstance = std::make_unique<RtxOptions>(options);
      return pInstance;
    }

    static std::unique_ptr<RtxOptions>& Get() { return pInstance; }

    bool isLightmapTexture(const XXH64_hash_t& h) const {
      return lightmapTextures().find(h) != lightmapTextures().end();
    }

    bool isSkyboxTexture(const XXH64_hash_t& h) const {
      return skyBoxTextures().find(h) != skyBoxTextures().end();
    }

    bool shouldIgnoreTexture(const XXH64_hash_t& h) const {
      return ignoreTextures().find(h) != ignoreTextures().end();
    }
    
    bool shouldIgnoreLight(const XXH64_hash_t& h) const {
      return ignoreLights().find(h) != ignoreLights().end();
    }

    bool isUiTexture(const XXH64_hash_t& h) const {
      return uiTextures().find(h) != uiTextures().end();
    }
    
    bool isWorldSpaceUiTexture(const XXH64_hash_t& h) const {
      return worldSpaceUiTextures().find(h) != worldSpaceUiTextures().end();
    }

    bool isWorldSpaceUiBackgroundTexture(const XXH64_hash_t& h) const {
      return worldSpaceUiBackgroundTextures().find(h) != worldSpaceUiBackgroundTextures().end();
    }

    bool isHideInstanceTexture(const XXH64_hash_t& h) const {
      return hideInstanceTextures().find(h) != hideInstanceTextures().end();
    }
    
    bool isPlayerModelTexture(const XXH64_hash_t& h) const {
      return playerModelTextures().find(h) != playerModelTextures().end();
    }

    bool isPlayerModelBodyTexture(const XXH64_hash_t& h) const {
      return playerModelBodyTextures().find(h) != playerModelBodyTextures().end();
    }

    bool isParticleTexture(const XXH64_hash_t& h) const {
      return particleTextures().find(h) != particleTextures().end();
    }

    bool isBeamTexture(const XXH64_hash_t& h) const {
      return beamTextures().find(h) != beamTextures().end();
    }

    bool isDecalTexture(const XXH64_hash_t& h) const {
      return decalTextures().find(h) != decalTextures().end();
    }

    bool isCutoutTexture(const XXH64_hash_t& h) const {
      return cutoutTextures().find(h) != cutoutTextures().end();
    }

    bool isDynamicDecalTexture(const XXH64_hash_t& h) const {
      return dynamicDecalTextures().find(h) != dynamicDecalTextures().end();
    }

    bool isNonOffsetDecalTexture(const XXH64_hash_t& h) const {
      return nonOffsetDecalTextures().find(h) != nonOffsetDecalTextures().end();
    }

    bool isTerrainTexture(const XXH64_hash_t& h) const {
      return terrainTextures().find(h) != terrainTextures().end();
    }

    bool shouldOpacityMicromapIgnoreTexture(const XXH64_hash_t& h) const {
      return opacityMicromapIgnoreTextures().find(h) != opacityMicromapIgnoreTextures().end();
    }

    bool isAnimatedWaterTexture(const XXH64_hash_t& h) const {
      return animatedWaterTextures().find(h) != animatedWaterTextures().end();
    }

    bool getRayPortalTextureIndex(const XXH64_hash_t& h, std::size_t& index) const {
      const auto findResult = std::find(rayPortalModelTextureHashes().begin(), rayPortalModelTextureHashes().end(), h);

      if (findResult == rayPortalModelTextureHashes().end()) {
        return false;
      }

      index = std::distance(rayPortalModelTextureHashes().begin(), findResult);

      return true;
    }

    bool shouldConvertToLight(const XXH64_hash_t& h) const {
      return lightConverter().find(h) != lightConverter().end();
    }

    const ivec2 getDrawCallRange() const { Vector2i v = drawCallRange(); return ivec2{v.x, v.y}; }
    bool isVertexCaptureEnabled() const { return useVertexCapture(); }
    uint32_t getMinPrimsInStaticBLAS() const { return minPrimsInStaticBLAS(); }

    // Camera
    CameraAnimationMode getCameraAnimationMode() { return cameraAnimationMode(); }
    bool isCameraShaking() { return shakeCamera(); }
    int getCameraShakePeriod() { return cameraShakePeriod(); }
    float getCameraAnimationAmplitude() { return cameraAnimationAmplitude(); }
    bool getSkipObjectsWithUnknownCamera() const { return skipObjectsWithUnknownCamera(); }

    bool isRayPortalVirtualInstanceMatchingEnabled() const { return useRayPortalVirtualInstanceMatching(); }
    bool isPortalFadeInEffectEnabled() const { return enablePortalFadeInEffect(); }
    bool isUpscalerEnabled() const { return upscalerType() != UpscalerType::None; }

    bool isDLSSEnabled() const {
      return upscalerType() == UpscalerType::DLSS;
    }

    bool isNISEnabled() const { return upscalerType() == UpscalerType::NIS; }
    bool isTAAEnabled() const { return upscalerType() == UpscalerType::TAAU; }
    bool isDirectLightingEnabled() const { return enableDirectLighting(); }
    bool isSecondaryBouncesEnabled() const { return enableSecondaryBounces(); }
    bool isDenoiserEnabled() const { return useDenoiser(); }
    bool isSeparatedDenoiserEnabled() const { return denoiseDirectAndIndirectLightingSeparately(); }
    bool isReplaceDirectSpecularHitTWithIndirectSpecularHitTEnabled() const { return replaceDirectSpecularHitTWithIndirectSpecularHitT(); }
    void setReplaceDirectSpecularHitTWithIndirectSpecularHitT(const bool enableReplaceDirectSpecularHitTWithIndirectSpecularHitT) {
      replaceDirectSpecularHitTWithIndirectSpecularHitTRef() = enableReplaceDirectSpecularHitTWithIndirectSpecularHitT;
    }
    bool isAdaptiveResolutionDenoisingEnabled() const { return adaptiveResolutionDenoising(); }
    bool shouldCaptureDebugImage() const { return captureDebugImage(); }
    bool isResolvePreCombinedMatricesEnabled() const { return resolvePreCombinedMatrices(); }
    bool isLiveShaderEditModeEnabled() const { return useLiveShaderEditMode(); }
    bool isZUp() const { return zUp(); }
    float getUniqueObjectDistance() const { return uniqueObjectDistance(); }
    float getUniqueObjectDistanceSqr() const { return uniqueObjectDistance() * uniqueObjectDistance(); }
    float getResolutionScale() const { return resolutionScale(); }
    DLSSProfile getDLSSQuality() const { return qualityDLSS(); }
    uint32_t getNumFramesToKeepInstances() const { return numFramesToKeepInstances(); }
    uint32_t getNumFramesToKeepBLAS() const { return numFramesToKeepBLAS(); }
    uint32_t getNumFramesToKeepLights() const { return numFramesToKeepLights(); }
    uint32_t getNumFramesToPutLightsToSleep() const { return numFramesToKeepLights() /2; }
    float getMeterToWorldUnitScale() const { return 100.f * getSceneScale(); } // T-Rex world unit is in 1cm 
    float getSceneScale() const { return sceneScale(); }

    // Render Pass Modes
    //RenderPassVolumeIntegrateRaytraceMode getRenderPassVolumeIntegrateRaytraceMode() const { return renderPassVolumeIntegrateRaytraceMode; }
    RenderPassGBufferRaytraceMode getRenderPassGBufferRaytraceMode() const { return renderPassGBufferRaytraceMode(); }
    RenderPassIntegrateDirectRaytraceMode getRenderPassIntegrateDirectRaytraceMode() const { return renderPassIntegrateDirectRaytraceMode(); }
    RenderPassIntegrateIndirectRaytraceMode getRenderPassIntegrateIndirectRaytraceMode() const { return renderPassIntegrateIndirectRaytraceMode(); }

    // View Model
    bool isViewModelEnabled() const { return viewModel.enable(); }
    float getViewModelRangeMeters() const { return viewModel.rangeMeters(); }
    float getViewModelScale() const { return viewModel.scale(); }
    bool isViewModelVirtualInstancesEnabled() const { return viewModel.enableVirtualInstances(); }
    bool isViewModelPerspectiveCorrectionEnabled() const { return viewModel.perspectiveCorrection(); }
    bool isViewModelSeparateRaysEnabled() const { return viewModel.separateRays(); }

    // Resolve Options
    uint8_t getPrimaryRayMaxInteractions() const { return primaryRayMaxInteractions(); }
    uint8_t getPSRRayMaxInteractions() const { return psrRayMaxInteractions(); }
    uint8_t getSecondaryRayMaxInteractions() const { return secondaryRayMaxInteractions(); }
    bool isSeparateUnorderedApproximationsEnabled() const { return enableSeparateUnorderedApproximations(); }
    bool areDirectTranslucentShadowsEnabled() const { return enableDirectTranslucentShadows(); }
    bool areIndirectTranslucentShadowsEnabled() const { return enableIndirectTranslucentShadows(); }
    float getResolveTransparencyThreshold() const { return resolveTransparencyThreshold(); }
    float getResolveOpaquenessThreshold() const { return resolveOpaquenessThreshold(); }

    // PSR Options
    bool isPSRREnabled() const { return enablePSRR(); }
    bool isPSTREnabled() const { return enablePSTR(); }
    uint8_t getPSRRMaxBounces() const { return psrrMaxBounces(); }
    uint8_t getPSTRMaxBounces() const { return pstrMaxBounces(); }
    bool isPSTROutgoingSplitApproximationEnabled() const { return enablePSTROutgoingSplitApproximation(); }
    bool isPSTRSecondaryIncidentSplitApproximationEnabled() const { return enablePSTRSecondaryIncidentSplitApproximation(); }
    
    bool getIsShaderExecutionReorderingSupported() const { return isShaderExecutionReorderingSupported(); }
    void setIsShaderExecutionReorderingSupported(bool enabled) { isShaderExecutionReorderingSupportedRef() = enabled; }
    //bool isShaderExecutionReorderingInVolumeIntegrateEnabled() const { return enableShaderExecutionReorderingInVolumeIntegrate && isShaderExecutionReorderingSupported; }
    bool isShaderExecutionReorderingInPathtracerGbufferEnabled() const { return enableShaderExecutionReorderingInPathtracerGbuffer() && isShaderExecutionReorderingSupported(); }
    //bool isShaderExecutionReorderingInPathtracerIntegrateDirectEnabled() const { return enableShaderExecutionReorderingInPathtracerIntegrateDirect && isShaderExecutionReorderingSupported; }
    bool isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled() const { return enableShaderExecutionReorderingInPathtracerIntegrateIndirect() && isShaderExecutionReorderingSupported(); }

    // Path Options
    bool isRussianRouletteEnabled() const { return enableRussianRoulette(); }
    bool isFirstBounceLobeProbabilityDitheringEnabled() const { return enableFirstBounceLobeProbabilityDithering(); }
    bool isUnorderedResolveInIndirectRaysEnabled() const { return enableUnorderedResolveInIndirectRays(); }
    bool isEmissiveParticlesInIndirectRaysEnabled() const { return enableEmissiveParticlesInIndirectRays(); }
    bool isDecalMaterialBlendingEnabled() const { return enableDecalMaterialBlending(); }
    float getTranslucentDecalAlbedoFactor() const { return translucentDecalAlbedoFactor(); }
    float getDecalNormalOffset() const { return decalNormalOffset(); }
    float getRussianRouletteMaxContinueProbability() const { return russianRouletteMaxContinueProbability(); }
    float getRussianRoulette1stBounceMinContinueProbability() const { return russianRoulette1stBounceMinContinueProbability(); }
    float getRussianRoulette1stBounceMaxContinueProbability() const { return russianRoulette1stBounceMaxContinueProbability(); }
    uint8_t getPathMinBounces() const { return pathMinBounces(); }
    uint8_t getPathMaxBounces() const { return pathMaxBounces(); }
    float getOpaqueDiffuseLobeSamplingProbabilityZeroThreshold() const { return opaqueDiffuseLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueDiffuseLobeSamplingProbability() const { return minOpaqueDiffuseLobeSamplingProbability(); }
    float getOpaqueSpecularLobeSamplingProbabilityZeroThreshold() const { return opaqueSpecularLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueSpecularLobeSamplingProbability() const { return minOpaqueSpecularLobeSamplingProbability(); }
    float getOpaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold() const { return opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueOpacityTransmissionLobeSamplingProbability() const { return minOpaqueOpacityTransmissionLobeSamplingProbability(); }
    float getTranslucentSpecularLobeSamplingProbabilityZeroThreshold() const { return translucentSpecularLobeSamplingProbabilityZeroThreshold(); }
    float getMinTranslucentSpecularLobeSamplingProbability() const { return minTranslucentSpecularLobeSamplingProbability(); }
    float getTranslucentTransmissionLobeSamplingProbabilityZeroThreshold() const { return translucentTransmissionLobeSamplingProbabilityZeroThreshold(); }
    float getMinTranslucentTransmissionLobeSamplingProbability() const { return minTranslucentTransmissionLobeSamplingProbability(); }
    float getIndirectRaySpreadAngleFactor() const { return indirectRaySpreadAngleFactor(); }
    bool getRngSeedWithFrameIndex() const { return rngSeedWithFrameIndex(); }

    // Light Selection/Sampling Options
    uint16_t getRISLightSampleCount() const { return risLightSampleCount(); }

    // Volumetrics Options
    uint32_t getFroxelGridResolutionScale() const { return froxelGridResolutionScale(); }
    uint16_t getFroxelDepthSlices() const { return froxelDepthSlices(); }
    uint8_t getMaxAccumulationFrames() const { return maxAccumulationFrames(); }
    float getFroxelDepthSliceDistributionExponent() const { return froxelDepthSliceDistributionExponent(); }
    float getFroxelMaxDistance() const { return froxelMaxDistance(); }
    float getFroxelFireflyFilteringLuminanceThreshold() const { return froxelFireflyFilteringLuminanceThreshold(); }
    float getFroxelFilterGaussianSigma() const { return froxelFilterGaussianSigma(); }
    bool isVolumetricEnableInitialVisibilityEnabled() const { return volumetricEnableInitialVisibility(); }
    bool isVolumetricEnableTemporalResamplingEnabled() const { return volumetricEnableTemporalResampling(); }
    uint16_t getVolumetricTemporalReuseMaxSampleCount() const { return volumetricTemporalReuseMaxSampleCount(); }
    float getVolumetricClampedReprojectionConfidencePenalty() const { return volumetricClampedReprojectionConfidencePenalty(); }
    uint8_t getFroxelMinReservoirSamples() const { return froxelMinReservoirSamples(); }
    uint8_t getFroxelMaxReservoirSamples() const { return froxelMaxReservoirSamples(); }
    uint8_t getFroxelMinKernelRadius() const { return froxelMinKernelRadius(); }
    uint8_t getFroxelMaxKernelRadius() const { return froxelMaxKernelRadius(); }
    uint8_t getFroxelMinReservoirSamplesStabilityHistory() const { return froxelMinReservoirSamplesStabilityHistory(); }
    uint8_t getFroxelReservoirSamplesStabilityHistoryRange() const { return cachedFroxelReservoirSamplesStabilityHistoryRange; }
    uint8_t getFroxelMinKernelRadiusStabilityHistory() const { return froxelMinKernelRadiusStabilityHistory(); }
    uint8_t getFroxelKernelRadiusStabilityHistoryRange() const { return cachedFroxelKernelRadiusStabilityHistoryRange; }
    float getFroxelReservoirSamplesStabilityHistoryPower() const { return froxelReservoirSamplesStabilityHistoryPower(); }
    float getFroxelKernelRadiusStabilityHistoryPower() const { return froxelKernelRadiusStabilityHistoryPower(); }
    bool isVolumetricLightingEnabled() const { return enableVolumetricLighting(); }
    Vector3 getVolumetricTransmittanceColor() const { return volumetricTransmittanceColor(); }
    float getVolumetricTransmittanceMeasurementDistance() const { return volumetricTransmittanceMeasurementDistance(); };
    Vector3 getVolumetricSingleScatteringAlbedo() const { return volumetricSingleScatteringAlbedo(); };
    float getVolumetricAnisotropy() const { return volumetricAnisotropy(); }
    bool isFogRemapEnabled() const { return enableFogRemap(); }
    float getFogRemapMaxDistanceMin() const { return fogRemapMaxDistanceMin(); }
    float getFogRemapMaxDistanceMax() const { return fogRemapMaxDistanceMax(); }
    float getFogRemapTransmittanceMeasurementDistanceMin() const { return fogRemapTransmittanceMeasurementDistanceMin(); }
    float getFogRemapTransmittanceMeasurementDistanceMax() const { return fogRemapTransmittanceMeasurementDistanceMax(); }
    
    // Alpha Test/Blend Options
    bool isAlphaBlendEnabled() const { return enableAlphaBlend(); }
    bool isAlphaTestEnabled() const { return enableAlphaTest(); }
    bool isCullingEnabled() const { return enableCulling(); }
    bool isEmissiveBlendEmissiveOverrideEnabled() const { return enableEmissiveBlendEmissiveOverride(); }
    float getEmissiveBlendOverrideEmissiveIntensity() const { return emissiveBlendOverrideEmissiveIntensity(); }
    float getParticleSoftnessFactor() const { return particleSoftnessFactor(); }

    // Ray Portal Options
    std::size_t getRayPortalPairCount() const { return rayPortalModelTextureHashes().size() / 2; }
    Vector3 getRayPortalWidthAxis() const { return rayPortalModelWidthAxis(); }
    Vector3 getRayPortalHeightAxis() const { return rayPortalModelHeightAxis(); }
    float getRayPortalSamplingWeightMinDistance() const { return rayPortalSamplingWeightMinDistance(); }
    float getRayPortalSamplingWeightMaxDistance() const { return rayPortalSamplingWeightMaxDistance(); }
    bool getRayPortalCameraHistoryCorrection() const { return rayPortalCameraHistoryCorrection(); }
    bool getRayPortalCameraInBetweenPortalsCorrection() const { return rayPortalCameraInBetweenPortalsCorrection(); }

    bool getWhiteMaterialModeEnabled() const { return useWhiteMaterialMode(); }
    bool getHighlightLegacyModeEnabled() const { return useHighlightLegacyMode(); }
    bool getHighlightUnsafeAnchorModeEnabled() const { return useHighlightUnsafeAnchorMode(); }
    bool getHighlightUnsafeReplacementModeEnabled() const { return useHighlightUnsafeReplacementMode(); }
    float getNativeMipBias() const { return nativeMipBias(); }
    bool getAnisotropicFilteringEnabled() const { return useAnisotropicFiltering(); }
    float getMaxAnisotropyLevel() const { return maxAnisotropyLevel(); }
  
    // Developer Options
    bool getDeveloperOptionsEnabled() const { return enableDeveloperOptions(); }
    ivec2 getDrawCallRange() { Vector2i v = drawCallRange(); return ivec2{v.x, v.y}; };
    Vector3 getOverrideWorldOffset() { return instanceOverrideWorldOffset(); }
    uint getInstanceOverrideInstanceIdx() { return instanceOverrideInstanceIdx(); }
    uint getInstanceOverrideInstanceIdxRange() { return instanceOverrideInstanceIdxRange(); }
    bool getInstanceOverrideSelectedPrintMaterialHash() { return instanceOverrideSelectedInstancePrintMaterialHash(); }
    
    bool getIsOpacityMicromapSupported() const { return opacityMicromap.isSupported; }
    void setIsOpacityMicromapSupported(bool enabled) { opacityMicromap.isSupported = enabled; }
    bool getEnableOpacityMicromap() const { return opacityMicromap.enable() && opacityMicromap.isSupported; }
    void setEnableOpacityMicromap(bool enabled) { opacityMicromap.enableRef() = enabled; }

    bool getEnableAnyReplacements() { return enableReplacementAssets() && (enableReplacementLights() || enableReplacementMeshes() || enableReplacementMaterials()); }
    bool getEnableReplacementLights() { return enableReplacementAssets() && enableReplacementLights(); }
    bool getEnableReplacementMeshes() { return enableReplacementAssets() && enableReplacementMeshes(); }
    bool getEnableReplacementMaterials() { return enableReplacementAssets() && enableReplacementMaterials(); }

    // Capture Options
    //   General
    bool getCaptureNoInstance() const { return captureNoInstance(); }
    std::string getCaptureInstanceStageName() const { return captureInstanceStageName(); }
    uint32_t getCaptureMaxFrames() const { return captureMaxFrames(); }
    size_t getCaptureFramesPerSecond() const { return captureFramesPerSecond(); }
    //   Mesh
    float getCaptureMeshPositionDelta() const { return captureMeshPositionDelta(); }
    float getCaptureMeshNormalDelta() const { return captureMeshNormalDelta(); }
    float getCaptureMeshTexcoordDelta() const { return captureMeshTexcoordDelta(); }
    float getCaptureMeshColorDelta() const { return captureMeshColorDelta(); }
    
    bool isUseVirtualShadingNormalsForDenoisingEnabled() const { return useVirtualShadingNormalsForDenoising(); }
    bool isResetDenoiserHistoryOnSettingsChangeEnabled() const { return resetDenoiserHistoryOnSettingsChange(); }
    
    const OpaqueMaterialDefaults& getOpaqueMaterialDefaults() const { return opaqueMaterialDefaults; }
    const TranslucentMaterialDefaults& getTranslucentMaterialDefaults() const { return translucentMaterialDefaults; }
    const RayPortalMaterialDefaults& getRayPortalMaterialDefaults() const { return rayPortalMaterialDefaults; }
    const SharedMaterialDefaults& getSharedMaterialDefaults() const { return sharedMaterialDefaults; }

    int32_t getPresentThrottleDelay() const { return enablePresentThrottle() ? presentThrottleDelay() : 0; }
    bool getValidateCPUIndexData() const { return validateCPUIndexData(); }

    float getEffectLightIntensity() const { return effectLightIntensity(); }
    float getEffectLightRadius() const { return effectLightRadius(); }
    bool getEffectLightPlasmaBall() const { return effectLightPlasmaBall(); }
    std::string getCurrentDirectory() const;

    bool shouldUseObsoleteHashOnTextureUpload() const { return useObsoleteHashOnTextureUpload(); }

    int getInitialSkipReplacementTextureMipMapLevel() const { return initialSkipReplacementTextureMipMapLevel; }
  };
}
