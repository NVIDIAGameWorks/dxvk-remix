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

#include "rtx/utility/shader_types.h"
#ifdef __cplusplus
#include "rtx/concept/camera/camera.h"
#include "rtx/concept/ray_portal/ray_portal.h"
#else
#include "rtx/concept/camera/camera.slangh"
#include "rtx/concept/ray_portal/ray_portal.slangh"
#endif

#include "rtx/pass/nrd_args.h"
#include "rtx/pass/nrc_args.h"
#include "rtx/pass/volume_args.h"
#include "rtx/pass/material_args.h"
#include "rtx/pass/view_distance_args.h"
#include "rtx/concept/light/light_types.h"
#include "rtx/concept/surface/surface_shared.h"
#include "rtx/algorithm/nee_cache_data.h"

struct LightRangeInfo {
  uint offset;
  uint count;
  uint16_t rtxdiSampleCount;
  uint16_t volumeRISSampleCount;
  uint16_t risSampleCount;
  uint16_t pad;
};

// Note: ensure 16B alignment
struct TerrainArgs {
  uint2 cascadeMapSize;    // Number of cascade tiles in each dimension
  float2 rcpCascadeMapSize;

  uint maxCascadeLevel;
  float lastCascadeScale;
  float displaceIn;
  uint pad0;
};

struct NeeCacheArgs {
  uint enable;
  uint enableImportanceSampling;
  uint enableMIS;
  uint enableOnFirstBounce;

  uint enableAnalyticalLight;
  float specularFactor;
  float uniformSamplingProbability;
  float cullingThreshold;

  NeeEnableMode enableModeAfterFirstBounce;
  float ageCullingSpeed;
  float emissiveTextureSampleFootprintScale;
  uint approximateParticleLighting;

  float resolution;
  float minRange;
  float learningRate;
  uint clearCache;

  float triangleExplorationRangeRatio;
  uint  triangleExplorationMaxRange;
  float triangleExplorationProbability;
  float triangleExplorationAcceptRangeRatio;

  uint3 padding;
  uint enableSpatialReuse;
};

struct DomeLightArgs {
  mat4 worldToLightTransform;

  vec3 radiance;
  uint active;

  uint3 pad0;
  uint textureIndex;
};

struct SssArgs {
  uint enableThinOpaque;
  uint enableDiffusionProfile;
  float diffusionProfileScale;
  u16vec2 diffusionProfileDebuggingPixel;
};

#define OBJECT_PICKING_INVALID (cb.clearColorPicking)

// Constant buffer
struct RaytraceArgs {
  // NOTE: this class should be kept as all structs, then all non-structs.  This is because the padding rules are different between C++ and shaders.
  Camera camera;

  // Note: Primary combined variant used in place of the primary direct denoiser when seperated direct/indirect
  // lighting is not used.
  NrdArgs primaryDirectNrd;
  NrdArgs primaryIndirectNrd;
  NrdArgs secondaryCombinedNrd;

  // Note: Not tightly packed, meaning these indices will align with the Ray Portal Index in the
  // Surface Material. Do note however due to elements being potentially "empty" each Ray Portal Hit Info
  // must be checked to be empty or not before usage. Additionally both Ray Portals in a pair will match
  // in state, either being present or not.
  // The first `maxRayPortalCount` portals are for this frame, the second `maxRayPortalCount` are for the previous frame.
  RayPortalHitInfo rayPortalHitInfos[maxRayPortalCount * 2];

  VolumeArgs volumeArgs;
  OpaqueMaterialArgs opaqueMaterialArgs;
  TranslucentMaterialArgs translucentMaterialArgs;
  ViewDistanceArgs viewDistanceArgs;

  LightRangeInfo lightRanges[lightTypeCount];

  TerrainArgs terrainArgs;
  NeeCacheArgs neeCacheArgs;
  DomeLightArgs domeLightArgs;
  NrcArgs nrcArgs;
  SssArgs sssArgs;

  Camera renderTargetCamera;

  // ------------------------- Structs above this line, non structs below this line -----------------------------------

  uint frameIdx;
  float ambientIntensity;
  uint16_t lightCount;
  uint16_t risTotalSampleCount;
  uint16_t volumeRISTotalSampleCount;
  uint16_t rtxdiTotalSampleCount;

  // The maximum probability of continuing a path when Russian Roulette is being used.
  RussianRouletteMode russianRouletteMode;
  float russianRouletteDistanceFactor;
  float russianRouletteDiffuseContinueProbability;
  float russianRouletteSpecularContinueProbability;

  float russianRouletteMaxContinueProbability;
  float russianRoulette1stBounceMinContinueProbability;
  float russianRoulette1stBounceMaxContinueProbability;
  float fireflyFilteringLuminanceThreshold;

  // The minimum number of indirect bounces the path must complete before Russian Roulette can be used. Must be < 16.
  uint8_t pathMinBounces;
  // The maximum number of indirect bounces the path will be allowed to complete. Must be < 16.
  uint8_t pathMaxBounces;
  // The number of samples to clamp temporal reservoirs to. Note this is not the same as RTXDI's history length as it is not scaled
  // by the number of samples the current reservoir performs (due to variability in how many actual current reservoir samples are done).
  uint16_t volumeTemporalReuseMaxSampleCount;
  // The maximum number of resolve interactions for primary (geometry resolver) rays.
  uint8_t primaryRayMaxInteractions;
  // The maximum number of resolve interactions for PSR (geometry resolver) rays.
  uint8_t psrRayMaxInteractions;
  // The maximum number of resolve interactions for secondary (integrator) rays.
  uint8_t secondaryRayMaxInteractions;
  // The number of active Ray Portals (Used for Ray Portal sampling). Always <= RAY_PORTAL_MAX_COUNT
  uint8_t numActiveRayPortals;
  float secondarySpecularFireflyFilteringThreshold;
  uint  outputParticleLayer;

  // Note: Packed as float16, uses uint16_t due to being shared on C++ side
  uint16_t emissiveBlendOverrideEmissiveIntensity;
  // The maximum number of bounces to evaluate reflection PSR over.
  uint8_t psrrMaxBounces;
  // The maximum number of bounces to evaluate transmission PSR over.
  uint8_t pstrMaxBounces;
  float viewModelRayTMax;
  uint16_t particleSoftnessFactor;
  uint16_t emissiveIntensity;
  uint8_t rtxdiSpatialSamples;
  uint8_t rtxdiDisocclusionSamples;
  uint8_t rtxdiMaxHistoryLength;
  uint8_t virtualInstancePortalIndex; // portal space for which virtual view model or player model instances were generated for

  float indirectRaySpreadAngleFactor;
  // Half the angle of the cone spawned by each pixel to use for ray cone texture filtering.
  float screenSpacePixelSpreadHalfAngle;
  uint debugView;
  float vertexColorStrength;


  vec4 debugKnob;     // For temporary tuning in shaders, has a dedicated UI widget.

  // Values to use on a ray miss
  vec3 clearColorNormal;
  float clearColorDepth;
  uint32_t clearColorPicking;
  uint enableDLSSRR;

  float2 upscaleFactor;   // Displayed(upscaled) / RT resolution

  float primaryDirectMissLinearViewZ;

  uint uniformRandomNumber;
  uint16_t opaqueDiffuseLobeSamplingProbabilityZeroThreshold;
  uint16_t minOpaqueDiffuseLobeSamplingProbability;
  uint16_t opaqueSpecularLobeSamplingProbabilityZeroThreshold;
  uint16_t minOpaqueSpecularLobeSamplingProbability;
  uint16_t opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold;
  uint16_t minOpaqueOpacityTransmissionLobeSamplingProbability;
  uint16_t opaqueDiffuseTransmissionLobeSamplingProbabilityZeroThreshold;
  uint16_t minOpaqueDiffuseTransmissionLobeSamplingProbability;

  uint16_t translucentSpecularLobeSamplingProbabilityZeroThreshold;
  uint16_t minTranslucentSpecularLobeSamplingProbability;
  uint16_t translucentTransmissionLobeSamplingProbabilityZeroThreshold;
  uint16_t minTranslucentTransmissionLobeSamplingProbability;
  float roughnessDemodulationOffset;
  float timeSinceStartSeconds;
  
  uint enableCalculateVirtualShadingNormals;
  uint enableDirectLighting;
  uint enableEmissiveBlendEmissiveOverride;
  uint enablePortalFadeInEffect;
  uint enableRussianRoulette;
  uint enableSecondaryBounces;
  uint enableSeparateUnorderedApproximations;
  uint enableStochasticAlphaBlend;
  uint16_t enableDirectTranslucentShadows;
  uint16_t enableDirectAlphaBlendShadows;
  uint16_t enableIndirectTranslucentShadows;
  uint16_t enableIndirectAlphaBlendShadows;
  uint enableFirstBounceLobeProbabilityDithering;
  uint enableUnorderedResolveInIndirectRays;
  uint enableProbabilisticUnorderedResolveInIndirectRays;
  uint enableUnorderedEmissiveParticlesInIndirectRays;
  uint enableTransmissionApproximationInIndirectRays;
  uint enableDecalMaterialBlending;
  uint enableBillboardOrientationCorrection;
  uint enablePlayerModelInPrimarySpace;
  uint enablePlayerModelPrimaryShadows;
  uint enablePreviousTLAS;
  uint useIntersectionBillboardsOnPrimaryRays;

  uint enableRtxdi;
  uint enableRtxdiPermutationSampling;
  uint enableRtxdiRayTracedBiasCorrection;
  uint enableRtxdiSampleStealing;
  uint enableRtxdiStealBoundaryPixelSamplesWhenOutsideOfScreen;
  uint enableRtxdiCrossPortalLight;
  uint enableRtxdiTemporalBiasCorrection;
  uint enableRtxdiInitialVisibility;
  uint enableRtxdiTemporalReuse;
  uint enableRtxdiSpatialReuse;
  uint enableRtxdiDiscardInvisibleSamples;
  uint enableRtxdiDiscardEnlargedPixels;
  uint enableDirectLightBoilingFilter;
  uint enableRtxdiBestLightSampling;
  float directLightBoilingThreshold;
  float rtxdiDisocclusionFrames;

  uint enableDemodulateRoughness;
  uint enableHitTFiltering;
  uint enableReplaceDirectSpecularHitTWithIndirectSpecularHitT;
  uint enableSeparatedDenoisers;

  uint enableViewModelVirtualInstances;

  uint enablePSRR;
  uint enablePSTR;
  uint enablePSTROutgoingSplitApproximation;
  uint enablePSTRSecondaryIncidentSplitApproximation;
  float psrrNormalDetailThreshold;
  float pstrNormalDetailThreshold;

  uint enableEnhanceBSDFDetail;
  uint enhanceBSDFIndirectMode;
  float enhanceBSDFDirectLightPower;
  float enhanceBSDFIndirectLightPower;
  float enhanceBSDFDirectLightMaxValue;
  float enhanceBSDFIndirectLightMaxValue;
  float enhanceBSDFIndirectLightMinRoughness;

  uint startInMediumMaterialIndex;
  uint enableReSTIRGI;
  uint enableReSTIRGIFinalVisibility;
  uint enableReSTIRGIReflectionReprojection;
  float restirGIReflectionMinParallax;
  uint enableReSTIRGIVirtualSample;
  float reSTIRGIVirtualSampleLuminanceThreshold;
  float reSTIRGIVirtualSampleRoughnessThreshold;
  float reSTIRGIVirtualSampleSpecularThreshold;
  float reSTIRGIVirtualSampleMaxDistanceRatio;
  uint reSTIRGIMISMode;
  float reSTIRGIMISModePairwiseMISCentralWeight;
  uint enableReSTIRGIPermutationSampling;
  uint enableReSTIRGIDLSSRRCompatibilityMode;
  float reSTIRGIDLSSRRTemporalRandomizationRadius;
  uint enableReSTIRGISampleStealing;
  float reSTIRGISampleStealingJitter;
  uint enableReSTIRGIStealBoundaryPixelSamplesWhenOutsideOfScreen;
  uint enableReSTIRGISpatialReuse;
  uint enableReSTIRGITemporalReuse;
  uint reSTIRGIBiasCorrectionMode;
  uint enableReSTIRGIBoilingFilter;
  float boilingFilterLowerThreshold;
  float boilingFilterHigherThreshold;
  float boilingFilterRemoveReservoirThreshold;
  uint temporalHistoryLength;
  uint permutationSamplingSize;
  uint enableReSTIRGITemporalBiasCorrection;
  uint enableReSTIRGIDiscardEnlargedPixels;
  float reSTIRGIHistoryDiscardStrength;
  uint enableReSTIRGITemporalJacobian;
  float reSTIRGIFireflyThreshold;
  float reSTIRGIRoughnessClamp;
  float reSTIRGIMISRoughness;
  float reSTIRGIMISParallaxAmount;
  uint enableReSTIRGIDemodulatedTargetFunction;
  uint enableReSTIRGILightingValidation;
  uint enableReSTIRGIVisibilityValidation;
  float reSTIRGISampleValidationThreshold;
  float reSTIRGIVisibilityValidationRange;

  uint surfaceCount;
  uint teleportationPortalIndex; // 0 means no teleportation, 1+ means portal 0+

  float resolveTransparencyThreshold;
  float resolveOpaquenessThreshold;
  float resolveStochasticAlphaBlendThreshold;
  float translucentDecalAlbedoFactor;

  float pad;

  float skyBrightness;

  uint isLastCompositeOutputValid;
  uint isZUp; // Note: Indicates if the Z axis is the "up" axis in world space if true, otherwise the Y axis if false.
  uint enableCullingSecondaryRays;

  u16vec2 gpuPrintThreadIndex;
  uint gpuPrintElementIndex;
  uint enableObjectPicking;

  DisplacementMode pomMode;
  uint pomEnableDirectLighting;
  uint pomEnableIndirectLighting;
  uint pomEnableNEECache;
  uint pomEnableReSTIRGI;
  uint pomEnablePSR;
  uint pomMaxIterations;
  uint enableSssTransmission;
  uint enableSssTransmissionSingleScattering;
  uint sssTransmissionBsdfSampleCount;
  uint sssTransmissionSingleScatteringSampleCount;
  uint enableTransmissionDiffusionProfileCorrection;
  float totalMipBias;

  uint forceFirstHitInGBufferPass;

  uint enableRaytracedRenderTarget;
  // NRC enablement is controlled by global macros being defined.
  // When macros are not used (i.e. in some passes) this variable controls the NRC enablement.
  uint enableNrc;

  // Debug override to disallow NRC training when it is enabled in the first place,
  // hence why it is not named enableNrcTraining here
  uint allowNrcTraining;

  // NOTE: Add structs to the top section of RaytraceArgs, not the bottom.
};
