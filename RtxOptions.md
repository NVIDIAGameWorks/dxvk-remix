# RTX Options 
RTX Options are configurable parameters for RTX pipeline components. They can be set via rtx.conf in a following format:

    <RTX Option scalar> = <value>
    <RTX Option vector> = <value1>, <value2>, ...

   Tables below enumerate all the options and their defaults set by RTX Remix. Note that the defaults will be overriden by any per-app defaults specified in config.cpp and/or dxvk.conf file at runtime.

## Simple Types
| RTX Option | Type | Default Value | Description |
| :-- | :-: | :-: | :-- |
|rtx.adaptiveAccumulation|bool|True||
|rtx.adaptiveResolutionDenoising|bool|True||
|rtx.allowFSE|bool|False||
|rtx.alwaysWaitForAsyncTextures|bool|False||
|rtx.applicationId|int|102100511|Used for DLSS.|
|rtx.assetEstimatedSizeGB|int|2||
|rtx.asyncTextureUploadPreloadMips|int|8||
|rtx.autoExposure.autoExposureSpeed|float|5|Average exposure changing speed when the image changes.|
|rtx.autoExposure.centerMeteringSize|float|0.5|The importance of pixels around the screen center.|
|rtx.autoExposure.enabled|bool|True|Automatically adjusts exposure so that the image won't be too bright or too dark.|
|rtx.autoExposure.evMaxValue|float|5|Min/Max values tuned by moving from bright/dark locations in game, and adjusting until they look correct.|
|rtx.autoExposure.evMinValue|float|-2|Min/Max values tuned by moving from bright/dark locations in game, and adjusting until they look correct.|
|rtx.autoExposure.exposureAverageMode|int|1|Average mode. Valid values: <Mean=0, Median=1>. The mean mode averages exposures across pixels. The median mode is more stable for extreme pixel values.|
|rtx.autoExposure.exposureCenterMeteringEnabled|bool|False|Gives higher weight to pixels around the screen center.|
|rtx.autoExposure.exposureWeightCurve0|float|1|Curve control point 0.|
|rtx.autoExposure.exposureWeightCurve1|float|1|Curve control point 1.|
|rtx.autoExposure.exposureWeightCurve2|float|1|Curve control point 2.|
|rtx.autoExposure.exposureWeightCurve3|float|1|Curve control point 3.|
|rtx.autoExposure.exposureWeightCurve4|float|1|Curve control point 4.|
|rtx.autoExposure.useExposureCompensation|bool|False|Uses a curve to determine the importance of different exposure levels when calculating average exposure.|
|rtx.blockInputToGameInUI|bool|True||
|rtx.bloom.enable|bool|True||
|rtx.bloom.intensity|float|0.06||
|rtx.bloom.sigma|float|0.1||
|rtx.calculateLightIntensityUsingLeastSquares|bool|True|Enable usage of least squares for approximating a light's falloff curve rather than a more basic single point approach. This will generally result in more accurate matching of the original application's custom light attenuation curves, especially with non physically based linear-style attenuation.|
|rtx.calculateMeshBoundingBox|bool|False|Calculate bounding box for every mesh.|
|rtx.camera.enableFreeCamera|bool|False|Enable free camera.|
|rtx.camera.freeCameraPitch|float|0|Free camera's pitch.|
|rtx.camera.freeCameraPosition|float3|0, 0, 0|Free camera's position.|
|rtx.camera.freeCameraViewRelative|bool|True|Free camera transform is relative to the view.|
|rtx.camera.freeCameraYaw|float|0|Free camera's position.|
|rtx.camera.lockFreeCamera|bool|False|Locks free camera.|
|rtx.camera.trackCamerasSeenStats|bool|False||
|rtx.cameraAnimationAmplitude|float|2||
|rtx.cameraAnimationMode|int|3||
|rtx.cameraShakePeriod|int|20||
|rtx.captureDebugImage|bool|False||
|rtx.captureFramesPerSecond|int|24||
|rtx.captureMaxFrames|int|1||
|rtx.captureMeshColorDelta|float|0.3|Inter-frame color min delta warrants new time sample.|
|rtx.captureMeshNormalDelta|float|0.3|Inter-frame normal min delta warrants new time sample.|
|rtx.captureMeshPositionDelta|float|0.3|Inter-frame position min delta warrants new time sample.|
|rtx.captureMeshTexcoordDelta|float|0.3|Inter-frame texcoord min delta warrants new time sample.|
|rtx.captureNoInstance|bool|False||
|rtx.compositePrimaryDirectDiffuse|bool|True||
|rtx.compositePrimaryDirectSpecular|bool|True||
|rtx.compositePrimaryIndirectDiffuse|bool|True||
|rtx.compositePrimaryIndirectSpecular|bool|True||
|rtx.compositeSecondaryCombinedDiffuse|bool|True||
|rtx.compositeSecondaryCombinedSpecular|bool|True||
|rtx.debugView.debugViewIdx|int|0||
|rtx.debugView.displayType|int|0||
|rtx.debugView.enablePseudoColor|bool|False||
|rtx.debugView.evMaxValue|int|4||
|rtx.debugView.evMinValue|int|-4||
|rtx.debugView.maxValue|float|1||
|rtx.debugView.minValue|float|0||
|rtx.decalNormalOffset|float|0.003|Distance along normal between two adjacent decals.|
|rtx.defaultToAdvancedUI|bool|False||
|rtx.demodulate.demodulateRoughness|bool|True|Demodulate roughness to improve specular details.|
|rtx.demodulate.demodulateRoughnessOffset|float|0.1|Strength of roughness demodulation, lower values are stronger.|
|rtx.demodulate.directLightBoilingThreshold|float|5|Remove direct light sample when its luminance is higher than the average one multiplied by this threshold .|
|rtx.demodulate.enableDirectLightBoilingFilter|bool|True|Boiling filter removing direct light sample when its luminance is too high.|
|rtx.denoiseDirectAndIndirectLightingSeparately|bool|True|Denoising quality, high uses separate denoising of direct and indirect lighting for higher quality at the cost of performance.|
|rtx.denoiser.maxDirectHitTContribution|float|-1||
|rtx.denoiser.nrd.timeDeltaBetweenFrames|float|-1||
|rtx.denoiserIndirectMode|int|16||
|rtx.denoiserMode|int|16||
|rtx.di.confidenceGradientPower|float|8||
|rtx.di.confidenceGradientScale|float|6||
|rtx.di.confidenceHistoryLength|float|8||
|rtx.di.confidenceHitDistanceSensitivity|float|300||
|rtx.di.disocclusionFrames|int|8||
|rtx.di.disocclusionSamples|int|4|The number of spatial reuse samples in disocclusion areas.|
|rtx.di.enableBestLightSampling|bool|True|Whether to include a single best light from the previous frame's pixel neighborhood into initial sampling.|
|rtx.di.enableCrossPortalLight|bool|True||
|rtx.di.enableDenoiserConfidence|bool|True||
|rtx.di.enableDiscardEnlargedPixels|bool|True||
|rtx.di.enableDiscardInvisibleSamples|bool|True|Whether to discard reservoirs that are determined to be invisible in final shading.|
|rtx.di.enableInitialVisibility|bool|True|Whether to trace a visibility ray for the light sample selected in the initial sampling pass.|
|rtx.di.enableRayTracedBiasCorrection|bool|True|Whether to use ray traced bias correction in the spatial reuse pass.|
|rtx.di.enableSampleStealing|bool|True|No visibile IQ gains, but exhibits considerable perf drop (8% in integrate pass).|
|rtx.di.enableSpatialReuse|bool|True|Whether to apply spatial reuse.|
|rtx.di.enableTemporalBiasCorrection|bool|True||
|rtx.di.enableTemporalReuse|bool|True|Whether to apply temporal reuse.|
|rtx.di.gradientFilterPasses|int|4||
|rtx.di.gradientHitDistanceSensitivity|float|10||
|rtx.di.initialSampleCount|int|4|The number of lights randomly selected from the global pool to consider when selecting a light with RTXDI.|
|rtx.di.maxHistoryLength|int|4|Maximum age of reservoirs for temporal reuse.|
|rtx.di.minimumConfidence|float|0.1||
|rtx.di.permutationSamplingNthFrame|int|0|Apply permutation sampling when (frameIdx % this == 0), 0 means off.|
|rtx.di.spatialSamples|int|2|The number of spatial reuse samples in converged areas.|
|rtx.di.stealBoundaryPixelSamplesWhenOutsideOfScreen|bool|True|Steal screen boundary samples when a hit point is outside the screen.|
|rtx.dlssEnhancementDirectLightMaxValue|float|10|The maximum strength of direct lighting enhancement.|
|rtx.dlssEnhancementDirectLightPower|float|0.7|The overall strength of direct lighting enhancement.|
|rtx.dlssEnhancementIndirectLightMaxValue|float|1.5|The maximum strength of indirect lighting enhancement.|
|rtx.dlssEnhancementIndirectLightMinRoughness|float|0.3|The reference roughness in indirect lighting enhancement.|
|rtx.dlssEnhancementIndirectLightPower|float|1|The overall strength of indirect lighting enhancement.|
|rtx.dlssEnhancementMode|int|1|The enhancement filter type. Valid values: <Normal Difference=1, Laplacian=0>. Normal difference mode provides more normal detail at the cost of some noise. Laplacian mode is less aggressive.|
|rtx.dlssPreset|int|1|Combined DLSS Preset for quickly controlling Upscaling, Frame Interpolation and Latency Reduction.|
|rtx.drawCallRange|int2|0, 2147483647||
|rtx.effectLightIntensity|float|1||
|rtx.effectLightPlasmaBall|bool|False||
|rtx.effectLightRadius|float|5||
|rtx.emissiveBlendOverrideEmissiveIntensity|float|0.2|The emissive intensity to use when the emissive blend override is enabled. Adjust this if particles for example look overly bright globally.|
|rtx.emissiveIntensity|float|1||
|rtx.enableAdaptiveResolutionReplacementTextures|bool|True||
|rtx.enableAlphaBlend|bool|True|Enable rendering alpha blended geometry, used for partial opacity and other blending effects on various surfaces in many games.|
|rtx.enableAlphaTest|bool|True|Enable rendering alpha tested geometry, used for cutout style opacity in some games.|
|rtx.enableAsyncTextureUpload|bool|True||
|rtx.enableBillboardOrientationCorrection|bool|True||
|rtx.enableCulling|bool|True|Enable front/backface culling for opaque objects. Objects with alpha blend or alpha test are not culled.|
|rtx.enableCullingInSecondaryRays|bool|False|Enable front/backface culling for opaque objects. Objects with alpha blend or alpha test are not culled.  Only applies in secondary rays, defaults to off.  Generally helps with light bleeding from objects that aren't watertight.|
|rtx.enableDLSSEnhancement|bool|True|Enhances lighting details when DLSS is on.|
|rtx.enableDecalMaterialBlending|bool|True||
|rtx.enableDeveloperOptions|bool|False||
|rtx.enableDirectLighting|bool|True||
|rtx.enableDirectTranslucentShadows|bool|False|Include OBJECT_MASK_TRANSLUCENT into primary visibility rays.|
|rtx.enableEmissiveBlendEmissiveOverride|bool|True|Override typical material emissive information on draw calls with any emissive blending modes to emulate their original look more accurately.|
|rtx.enableEmissiveParticlesInIndirectRays|bool|False||
|rtx.enableFallbackLightShaping|bool|False|Enables light shaping on the fallback light (only used for non-Distant light types).|
|rtx.enableFallbackLightViewPrimaryAxis|bool|False|Enables usage of the camera's view axis as the primary axis for the fallback light's shaping (only used for non - Distant light types). Typically the shaping primary axis may be specified directly, but if desired it may be set to the camera's view axis for a "flashlight" effect.|
|rtx.enableFirstBounceLobeProbabilityDithering|bool|True||
|rtx.enableFog|bool|True||
|rtx.enableFogRemap|bool|False||
|rtx.enableIndirectTranslucentShadows|bool|False|Include OBJECT_MASK_TRANSLUCENT into secondary visibility rays.|
|rtx.enableNearPlaneOverride|bool|False||
|rtx.enablePSRR|bool|True|Enable reflection PSR.|
|rtx.enablePSTR|bool|True|Enable transmission PSR.|
|rtx.enablePSTROutgoingSplitApproximation|bool|True|Enable transmission PSR on outgoing transmission event possibilities (rather than respecting no-split path PSR rule).|
|rtx.enablePSTRSecondaryIncidentSplitApproximation|bool|True|Enable transmission PSR on secondary incident transmission event possibilities (rather than respecting no-split path PSR rule).|
|rtx.enablePortalFadeInEffect|bool|False||
|rtx.enablePresentThrottle|bool|False||
|rtx.enablePreviousTLAS|bool|True||
|rtx.enableRaytracing|bool|True||
|rtx.enableReplacementAssets|bool|True|Enables all enhanced asset replacements (materials, meshes, lights).|
|rtx.enableReplacementLights|bool|True|Enables enhanced light replacements.|
|rtx.enableReplacementMaterials|bool|True|Enables enhanced material replacements.|
|rtx.enableReplacementMeshes|bool|True|Enables enhanced mesh replacements.|
|rtx.enableRussianRoulette|bool|True||
|rtx.enableSecondaryBounces|bool|True||
|rtx.enableSeparateUnorderedApproximations|bool|True|Use a separate loop for surfaces which can have lighting evaluated in an approximate unordered way on each path segment. This improves performance typically.|
|rtx.enableShaderExecutionReorderingInPathtracerGbuffer|bool|False||
|rtx.enableShaderExecutionReorderingInPathtracerIntegrateIndirect|bool|True||
|rtx.enableStochasticAlphaBlend|bool|True|Use stochastic alpha blend.|
|rtx.enableUnorderedResolveInIndirectRays|bool|True||
|rtx.enableVolumetricLighting|bool|False|Enabling volumetric lighting provides higher quality ray traced physical volumetrics, disabling falls back to cheaper depth based fog. Note: it does not disable the volume radiance cache as a whole as it is still needed for particles.|
|rtx.enableVolumetricsInPortals|bool|True|Enables using extra frustum-aligned volumes for lighting in portals.|
|rtx.fallbackLightAngle|float|5|The spread angle to use for the fallback light (used only for Distant light types).|
|rtx.fallbackLightConeAngle|float|25|The cone angle to use for the fallback light shaping (used only for non-Distant light types with shaping enabled).|
|rtx.fallbackLightConeSoftness|float|0.1|The cone softness to use for the fallback light shaping (used only for non-Distant light types with shaping enabled).|
|rtx.fallbackLightDirection|float3|-0.2, -1, 0.4|The direction to use for the fallback light (used only for Distant light types)|
|rtx.fallbackLightFocusExponent|float|2|The focus exponent to use for the fallback light shaping (used only for non-Distant light types with shaping enabled).|
|rtx.fallbackLightMode|int|1||
|rtx.fallbackLightPositionOffset|float3|0, 0, 0|The position offset from the camera origin to use for the fallback light (used only for non-Distant light types).|
|rtx.fallbackLightPrimaryAxis|float3|0, 0, -1|The primary axis to use for the fallback light shaping (used only for non-Distant light types).|
|rtx.fallbackLightRadiance|float3|1.6, 1.8, 2|The radiance to use for the fallback light (used across all light types).|
|rtx.fallbackLightRadius|float|5|The radius to use for the fallback light (used only for Sphere light types).|
|rtx.fallbackLightType|int|0|The light type to use for the fallback light. Determines which other fallback light options are used.|
|rtx.fireflyFilteringLuminanceThreshold|float|1000|Maximum luminance threshold for the firefly filtering to clamp to.|
|rtx.fogColorScale|float|0.25||
|rtx.fogRemapColorStrength|float|1||
|rtx.fogRemapMaxDistanceMax|float|4000||
|rtx.fogRemapMaxDistanceMin|float|100||
|rtx.fogRemapTransmittanceMeasurementDistanceMax|float|12000||
|rtx.fogRemapTransmittanceMeasurementDistanceMin|float|2000||
|rtx.forceCameraJitter|bool|False||
|rtx.forceCutoutAlpha|float|0.5|When an object is added to cutoutTextures, its surface with alpha less than this value will get discarded. This is meant to improve on legacy, low-resolution textures that use blended transparency instead of alpha cutout, which can result in blurry halos around edges. This is generally best handled by generating replacement assets that use either fully opaque, detailed geometry, or fully transparent alpha cutouts on higher resolution textures. Rendered output might still look incorrect even with this flag.|
|rtx.forceHighResolutionReplacementTextures|bool|False||
|rtx.forceVsyncOff|bool|False||
|rtx.freeCameraSpeed|float|200|Free camera speed [GameUnits/s].|
|rtx.froxelDepthSliceDistributionExponent|float|2|The exponent to use on depth values to nonlinearly distribute froxels away from the camera. Higher values bias more froxels closer to the camera with 1 being linear.|
|rtx.froxelDepthSlices|int|48|The z dimension of the froxel grid. Must be constant after initialization.|
|rtx.froxelFilterGaussianSigma|float|1.2|The sigma value of the gaussian function used to filter volumetric radiance values. Larger values cause a smoother filter to be used.|
|rtx.froxelFireflyFilteringLuminanceThreshold|float|1000|Sets the maximum luminance threshold for the volumetric firefly filtering to clamp to.|
|rtx.froxelGridResolutionScale|int|16|The scale factor to divide the x and y render resolution by to determine the x and y dimensions of the froxel grid.|
|rtx.froxelKernelRadiusStabilityHistoryPower|float|2|The power to apply to the kernel radius stability history weight.|
|rtx.froxelMaxDistance|float|2000|The maximum distance in world units to allocate the froxel grid out to. Should be less than the distance between the camera's near and far plane, as the froxel grid will clip to the far plane otherwise.|
|rtx.froxelMaxKernelRadius|int|4|The maximum filtering kernel radius to use when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.|
|rtx.froxelMaxKernelRadiusStabilityHistory|int|64|The maximum history to consider history at maximum stability for filtering.|
|rtx.froxelMaxReservoirSamples|int|6|The maximum number of Reservoir samples to do for each froxel cell when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.|
|rtx.froxelMaxReservoirSamplesStabilityHistory|int|64|The maximum history to consider history at maximum stability for Reservoir samples.|
|rtx.froxelMinKernelRadius|int|2|The minimum filtering kernel radius to use when stability is at its maximum, should be at least 1.|
|rtx.froxelMinKernelRadiusStabilityHistory|int|1|The minimum history to consider history at minimum stability for filtering.|
|rtx.froxelMinReservoirSamples|int|1|The minimum number of Reservoir samples to do for each froxel cell when stability is at its maximum, should be at least 1.|
|rtx.froxelMinReservoirSamplesStabilityHistory|int|1|The minimum history to consider history at minimum stability for Reservoir samples.|
|rtx.froxelReservoirSamplesStabilityHistoryPower|float|2|The power to apply to the Reservoir sample stability history weight.|
|rtx.fusedWorldViewMode|int|0|Set if game uses a fused World-View transform matrix.|
|rtx.graphicsPreset|int|5|Overall rendering preset, higher presets result in higher image quality, lower presets result in better performance.|
|rtx.hideSplashMessage|bool|False||
|rtx.highlightedTexture|int|0|Hash of a texture that should be highlighted.|
|rtx.ignoreGameDirectionalLights|bool|False|Ignores any directional lights coming from the original game (lights added via toolkit still work).|
|rtx.ignoreGamePointLights|bool|False|Ignores any point lights coming from the original game (lights added via toolkit still work).|
|rtx.ignoreGameSpotLights|bool|False|Ignores any spot lights coming from the original game (lights added via toolkit still work).|
|rtx.ignoreStencilVolumeHeuristics|bool|True|Tries to detect stencil volumes and ignore those when pathtracing.  Stencil buffer was used for a variety of effects in the D3D7-9 era, mostly for geometry based lights and shadows - things we don't need when pathtracing.|
|rtx.indirectRaySpreadAngleFactor|float|0.05|A tuning factor for the spread angle calculated from the sampled lobe solid angle PDF.|
|rtx.initializer.asyncAssetLoading|bool|True||
|rtx.initializer.asyncShaderFinalizing|bool|True||
|rtx.initializer.asyncShaderPrewarming|bool|True||
|rtx.instanceOverrideInstanceIdx|int|-1||
|rtx.instanceOverrideInstanceIdxRange|int|15||
|rtx.instanceOverrideSelectedInstancePrintMaterialHash|bool|False||
|rtx.instanceOverrideWorldOffset|float3|0, 0, 0||
|rtx.io.enabled|bool|False||
|rtx.io.memoryBudgetMB|int|256||
|rtx.io.useAsyncQueue|bool|True||
|rtx.isLHS|bool|False||
|rtx.isReflexSupported|bool|True||
|rtx.isShaderExecutionReorderingSupported|bool|False||
|rtx.keepTexturesForTagging|bool|False|Note: this keeps all textures in video memory, which can drastically increase VRAM consumption. Intended to assist with tagging textures that are only used for a short period of time (such as loading screens). Use only when necessary!|
|rtx.legacyMaterial.albedoConstant|float3|1, 1, 1||
|rtx.legacyMaterial.alphaIsThinFilmThickness|bool|False||
|rtx.legacyMaterial.anisotropy|float|0||
|rtx.legacyMaterial.emissiveColorConstant|float3|0, 0, 0||
|rtx.legacyMaterial.emissiveIntensity|float|0||
|rtx.legacyMaterial.enableEmissive|bool|False||
|rtx.legacyMaterial.enableThinFilm|bool|False||
|rtx.legacyMaterial.metallicConstant|float|0.1||
|rtx.legacyMaterial.opacityConstant|float|1||
|rtx.legacyMaterial.roughnessConstant|float|0.7||
|rtx.legacyMaterial.thinFilmThicknessConstant|float|200||
|rtx.legacyMaterial.useAlbedoTextureIfPresent|bool|True||
|rtx.lightConversionDistantLightFixedAngle|float|0.0349|The angular size in radiance of the distant light source for legacy lights converted to distant lights. Set to ~2 degrees in radians by default.|
|rtx.lightConversionDistantLightFixedIntensity|float|1|The fixed intensity (in W/sr) to use for legacy lights converted to distant lights (currently directional lights will convert to distant lights).|
|rtx.lightConversionEqualityDirectionThreshold|float|0.99|The lower cosine angle threshold between two directions used to determine if two directional lights as the same light when uniquely identifying legacy lights for conversion.|
|rtx.lightConversionEqualityDistanceThreshold|float|0.05|The upper distance threshold between two positions used to determine if two positional lights as the same light when uniquely identifying legacy lights for conversion.|
|rtx.lightConversionSphereLightFixedRadius|float|4|The fixed radius in world units to use for legacy lights converted to sphere lights (currently point and spot lights will convert to sphere lights). Use caution with large light radii as many legacy lights will be placed close to geometry and intersect it, causing suboptimal light sampling performance or other visual artifacts (lights clipping through walls, etc).|
|rtx.localtonemap.boostLocalContrast|bool|False|Boosts contrast on local features.|
|rtx.localtonemap.displayMip|int|0|Bottom mip level of tone map pyramid.|
|rtx.localtonemap.exposure|float|0.75|Exposure factor applied on average exposure.|
|rtx.localtonemap.exposurePreferenceOffset|float|0|Offset to reference luminance when calculating the weights a pixel belongs to shadow/normal/highlight areas.|
|rtx.localtonemap.exposurePreferenceSigma|float|4|Transition sharpness between different areas of exposure. Smaller values result in sharper transitions.|
|rtx.localtonemap.finalizeWithACES|bool|True|Applies ACES tone mapping on final result.|
|rtx.localtonemap.highlights|float|4|Highlight area strength. Higher values cause darker highlight.|
|rtx.localtonemap.mip|int|3|Top mip level of tone map pyramid.|
|rtx.localtonemap.shadows|float|2|Shadow area strength. Higher values cause brighter shadows.|
|rtx.localtonemap.useGaussian|bool|True|Uses gaussian kernel to generate tone map pyramid.|
|rtx.logLegacyHashReplacementMatches|bool|False||
|rtx.maxAccumulationFrames|int|254|The number of frames to accumulate volume lighting samples over. More results in greater image stability at the cost of potentially more temporal lag.|
|rtx.maxAnisotropyLevel|float|8|Min of this and the hardware device limits.|
|rtx.maxFogDistance|float|65504||
|rtx.maxPrimsInMergedBLAS|int|50000||
|rtx.minOpaqueDiffuseLobeSamplingProbability|float|0.25|The minimum allowed non-zero value for opaque diffuse probability weights.|
|rtx.minOpaqueOpacityTransmissionLobeSamplingProbability|float|0.25|The minimum allowed non-zero value for opaque opacity probability weights.|
|rtx.minOpaqueSpecularLobeSamplingProbability|float|0.25|The minimum allowed non-zero value for opaque specular probability weights.|
|rtx.minPrimsInStaticBLAS|int|1000||
|rtx.minTranslucentSpecularLobeSamplingProbability|float|0.3|The minimum allowed non-zero value for translucent specular probability weights.|
|rtx.minTranslucentTransmissionLobeSamplingProbability|float|0.25|The minimum allowed non-zero value for translucent transmission probability weights.|
|rtx.nativeMipBias|float|0||
|rtx.nearPlaneOverride|float|0.1||
|rtx.nisPreset|int|1|Adjusts NIS scaling factor, trades quality for performance.|
|rtx.noiseClampHigh|float|2||
|rtx.noiseClampLow|float|0.5||
|rtx.noiseMixRatio|float|0.2||
|rtx.noiseNormalPower|float|0.5||
|rtx.numFramesToKeepBLAS|int|4||
|rtx.numFramesToKeepGeometryData|int|5||
|rtx.numFramesToKeepInstances|int|1||
|rtx.numFramesToKeepLights|int|100||
|rtx.numFramesToKeepMaterialTextures|int|30||
|rtx.opacityMicromap.conservativeEstimationMaxTexelTapsPerMicroTriangle|int|64|Set to 64 as a safer cap. 512 has been found to cause a timeout.|
|rtx.opacityMicromap.enable|bool|True||
|rtx.opacityMicromap.enableParticles|bool|True||
|rtx.opacityMicromap.enableResetEveryFrame|bool|False||
|rtx.opacityMicromap.maxBudgetSizeMB|int|1536||
|rtx.opacityMicromap.maxOmmBuildRequests|int|5000||
|rtx.opacityMicromap.maxVidmemSizePercentage|float|0.15||
|rtx.opacityMicromap.minBudgetSizeMB|int|512||
|rtx.opacityMicromap.minFreeVidmemMBToNotAllocate|int|2560||
|rtx.opacityMicromap.numFramesAtStartToBuildWithHighWorkload|int|0||
|rtx.opacityMicromap.ommBuildRequest_minInstanceFrameAge|int|1||
|rtx.opacityMicromap.ommBuildRequest_minNumFramesRequested|int|5||
|rtx.opacityMicromap.ommBuildRequest_minNumRequests|int|10||
|rtx.opacityMicromap.subdivisionLevel|int|8||
|rtx.opacityMicromap.workloadHighWorkloadMultiplier|int|20||
|rtx.opaqueDiffuseLobeSamplingProbabilityZeroThreshold|float|0.01|The threshold for which to zero opaque diffuse probability weight values.|
|rtx.opaqueMaterial.albedoBias|float|0||
|rtx.opaqueMaterial.albedoScale|float|1||
|rtx.opaqueMaterial.enableThinFilmOverride|bool|False||
|rtx.opaqueMaterial.layeredWaterNormalEnable|bool|True||
|rtx.opaqueMaterial.layeredWaterNormalLodBias|float|5||
|rtx.opaqueMaterial.layeredWaterNormalMotion|float2|-0.25, -0.3||
|rtx.opaqueMaterial.layeredWaterNormalMotionScale|float|9||
|rtx.opaqueMaterial.normalIntensity|float|1||
|rtx.opaqueMaterial.roughnessBias|float|0||
|rtx.opaqueMaterial.roughnessScale|float|1||
|rtx.opaqueMaterial.thinFilmNormalizedThicknessOverride|float|0||
|rtx.opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold|float|0.01|The threshold for which to zero opaque opacity probability weight values.|
|rtx.opaqueSpecularLobeSamplingProbabilityZeroThreshold|float|0.01|The threshold for which to zero opaque specular probability weight values.|
|rtx.particleSoftnessFactor|float|0.05|Multiplier for the view distance that is used to calculate the particle blending range.|
|rtx.pathMaxBounces|int|4|The maximum number of indirect bounces the path will be allowed to complete. Higher values result in better indirect lighting, lower values result in better performance. Must be < 16.|
|rtx.pathMinBounces|int|1|The minimum number of indirect bounces the path must complete before Russian Roulette can be used. Must be < 16.|
|rtx.pipeline.useDeferredOperations|bool|True||
|rtx.pixelHighlightReuseStrength|float|0.5|The specular portion when we reuse last frame's pixel value.|
|rtx.playerModel.backwardOffset|float|18||
|rtx.playerModel.enableInPrimarySpace|bool|False||
|rtx.playerModel.enablePrimaryShadows|bool|True||
|rtx.playerModel.enableVirtualInstances|bool|True||
|rtx.playerModel.eyeHeight|float|64||
|rtx.playerModel.horizontalDetectionDistance|float|34||
|rtx.playerModel.intersectionCapsuleHeight|float|68||
|rtx.playerModel.intersectionCapsuleRadius|float|24||
|rtx.playerModel.verticalDetectionDistance|float|64||
|rtx.postFilterThreshold|float|3|Clamps a pixel when its luminance exceeds x times of the average.|
|rtx.postfx.blurDiameterFraction|float|0.02||
|rtx.postfx.chromaticAberrationAmount|float|0.02||
|rtx.postfx.chromaticCenterAttenuationAmount|float|0.975||
|rtx.postfx.enable|bool|True|Enables post-processing effects.|
|rtx.postfx.enableChromaticAberration|bool|True|Enables chromatic aberration post-processing effect.|
|rtx.postfx.enableMotionBlur|bool|True|Enables motion blur post-processing effect.|
|rtx.postfx.enableMotionBlurEmissive|bool|True||
|rtx.postfx.enableMotionBlurNoiseSample|bool|True||
|rtx.postfx.enableVignette|bool|True|Enables vignette post-processing effect.|
|rtx.postfx.exposureFraction|float|0.4||
|rtx.postfx.motionBlurDynamicDeduction|float|0.075||
|rtx.postfx.motionBlurJitterStrength|float|0.6||
|rtx.postfx.motionBlurMinimumVelocityThresholdInPixel|float|1||
|rtx.postfx.motionBlurSampleCount|int|4||
|rtx.postfx.vignetteIntensity|float|0.8||
|rtx.postfx.vignetteRadius|float|0.8||
|rtx.postfx.vignetteSoftness|float|0.2||
|rtx.presentThrottleDelay|int|16|[ms]|
|rtx.primaryRayMaxInteractions|int|32||
|rtx.psrRayMaxInteractions|int|32||
|rtx.psrrMaxBounces|int|10|The maximum number of Reflection PSR bounces to traverse. Must be 15 or less due to payload encoding.|
|rtx.psrrNormalDetailThreshold|float|0||
|rtx.pstrMaxBounces|int|10|The maximum number of Transmission PSR bounces to traverse. Must be 15 or less due to payload encoding.|
|rtx.pstrNormalDetailThreshold|float|0||
|rtx.qualityDLSS|int|4|Adjusts internal DLSS scaling factor, trades quality for performance.|
|rtx.rayPortalCameraHistoryCorrection|bool|False||
|rtx.rayPortalCameraInBetweenPortalsCorrection|bool|False||
|rtx.rayPortalEnabled|bool|False||
|rtx.rayPortalModelHeightAxis|float3|0, 1, 0||
|rtx.rayPortalModelNormalAxis|float3|0, 0, 1||
|rtx.rayPortalModelWidthAxis|float3|1, 0, 0||
|rtx.rayPortalSamplingWeightMaxDistance|float|1000||
|rtx.rayPortalSamplingWeightMinDistance|float|10||
|rtx.raytraceModePreset|int|1||
|rtx.recompileShadersOnLaunch|bool|False|When set to true runtime shader recompilation will execute on the first frame after launch.|
|rtx.reflexMode|int|1|Reflex mode selection, enabling it helps minimize input latency, boost mode may further reduce latency by boosting GPU clocks in CPU-bound cases.|
|rtx.renderPassGBufferRaytraceMode|int|2||
|rtx.renderPassIntegrateDirectRaytraceMode|int|0||
|rtx.renderPassIntegrateIndirectRaytraceMode|int|2||
|rtx.replaceDirectSpecularHitTWithIndirectSpecularHitT|bool|True||
|rtx.resetBufferCacheOnEveryFrame|bool|True||
|rtx.resetDenoiserHistoryOnSettingsChange|bool|False||
|rtx.resolutionScale|float|0.75||
|rtx.resolveOpaquenessThreshold|float|0.996078|x >= threshold : opaque surface.|
|rtx.resolvePreCombinedMatrices|bool|True||
|rtx.resolveTransparencyThreshold|float|0.00392157|x <= threshold : transparent surface.|
|rtx.restirGI.biasCorrectionMode|int|4|Bias correction mode to combine central with its neighbors in spatial reuse.|
|rtx.restirGI.boilingFilterMaxThreshold|float|20|Boiling filter threshold when surface normal is parallel to view direction.|
|rtx.restirGI.boilingFilterMinThreshold|float|10|Boiling filter threshold when surface normal is perpendicular to view direction.|
|rtx.restirGI.boilingFilterRemoveReservoirThreshold|float|62|Removes a sample when a sample's weight exceeds this threshold.|
|rtx.restirGI.fireflyThreshold|float|50|Clamps specular input to suppress boiling.|
|rtx.restirGI.misMode|int|2|MIS mode to mix specular output with input.|
|rtx.restirGI.misRoughness|float|0.3|Reference roughness when roughness MIS is used. Higher values give ReSTIR inputs higher weight.|
|rtx.restirGI.pairwiseMISCentralWeight|float|0.1|The importance of central sample in pairwise bias correction modes.|
|rtx.restirGI.parallaxAmount|float|0.02|Parallax strength when parallax MIS is used. Higher values give ReSTIR inputs higher weight.|
|rtx.restirGI.permutationSamplingSize|int|2|Permutation sampling strength.|
|rtx.restirGI.reflectionMinParallax|float|3|When the parallax between normal and reflection reprojection is greater than this threshold, randomly choose one reprojected position and reuse the sample on it. Otherwise, get a sample between the two positions.|
|rtx.restirGI.roughnessClamp|float|0.01|Clamps minimum roughness a sample's importance is evaluated.|
|rtx.restirGI.stealBoundaryPixelSamplesWhenOutsideOfScreen|bool|True|Steals ReSTIR GI samples even a hit point is outside the screen. This will further improve highly specular samples at the cost of some bias.|
|rtx.restirGI.temporalAdaptiveHistoryLengthMs|int|500|Temporal history time length, when adaptive temporal history is enabled.|
|rtx.restirGI.temporalFixedHistoryLength|int|30|Fixed temporal history length, when adaptive temporal history is disabled.|
|rtx.restirGI.useAdaptiveTemporalHistory|bool|True|Adjust temporal history length based on frame rate.|
|rtx.restirGI.useBoilingFilter|bool|True|Enables boiling filter to suppress boiling artifacts.|
|rtx.restirGI.useDemodulatedTargetFunction|bool|False|Demodulates target function. This will improve the result in non-pairwise modes.|
|rtx.restirGI.useDiscardEnlargedPixels|bool|True|Discards enlarged samples when the camera is moving towards an object.|
|rtx.restirGI.useFinalVisibility|bool|True|Tests visiblity in output.|
|rtx.restirGI.usePermutationSampling|bool|True|Uses permutation sample to perturb samples. This will improve results in DLSS.|
|rtx.restirGI.useReflectionReprojection|bool|True|Uses reflection reprojection for reflective objects to achieve stable result when the camera is moving.|
|rtx.restirGI.useSampleStealing|int|2|Steals ReSTIR GI samples in path tracer. This will improve highly specular results.|
|rtx.restirGI.useSpatialReuse|bool|True|Enables spatial reuse.|
|rtx.restirGI.useTemporalBiasCorrection|bool|True|Corrects bias caused by temporal reprojection.|
|rtx.restirGI.useTemporalJacobian|bool|True|Calculates Jacobian determinant in temporal reprojection.|
|rtx.restirGI.useTemporalReuse|bool|True|Enables temporal reuse.|
|rtx.restirGI.useVirtualSample|bool|True|Uses virtual position for samples from highly specular surfaces.|
|rtx.restirGI.virtualSampleLuminanceThreshold|float|2|The last path vertex with luminance greater than 2 times of the previous accumulated radiance will get virtualized. Higher values tend to keep the first path vertex with non-zero contribution.|
|rtx.restirGI.virtualSampleRoughnessThreshold|float|0.2|Surface with roughness under this threshold is considered to be highly specular, i.e. a "mirror".|
|rtx.restirGI.virtualSampleSpecularThreshold|float|0.5|If a highly specular path vertex's direct specular light portion is higher than this. Its distance to the light source will get accumulated.|
|rtx.risLightSampleCount|int|7|The number of lights randomly selected from the global pool to consider when selecting a light with RIS.|
|rtx.rngSeedWithFrameIndex|bool|True||
|rtx.russianRoulette1stBounceMaxContinueProbability|float|1|The maximum probability of continuing a path when Russian Roulette is being used on the first bounce.|
|rtx.russianRoulette1stBounceMinContinueProbability|float|0.6|The minimum probability of continuing a path when Russian Roulette is being used on the first bounce.|
|rtx.russianRouletteMaxContinueProbability|float|0.9|The maximum probability of continuing a path when Russian Roulette is being used.|
|rtx.sceneScale|float|1|Defines the ratio of rendering unit (1cm) to game unit, i.e. sceneScale = 1cm / GameUnit.|
|rtx.secondaryRayMaxInteractions|int|8||
|rtx.serializeChangedOptionOnly|bool|True||
|rtx.shakeCamera|bool|False||
|rtx.showUI|int|0|0 = Don't Show, 1 = Show Simple, 2 = Show Advanced.|
|rtx.showUICursor|bool|False||
|rtx.skipDrawCallsPostRTXInjection|bool|False||
|rtx.skipObjectsWithUnknownCamera|bool|False||
|rtx.skipReplacementTextureMipMapLevel|int|0|The texture resolution to use, lower resolution textures may improve performance and reduce video memory usage.|
|rtx.skyBrightness|float|1||
|rtx.skyDrawcallIdThreshold|int|0|It's common in games to render the skybox first, and so, this value provides a simple mechanism to identify those early draw calls that are untextured (textured draw calls can still use the Sky Textures functionality.|
|rtx.skyForceHDR|bool|False|By default sky will be rasterized in the color format used by the game. Set the checkbox to force sky to be rasterized in HDR intermediate format. This may be important when sky textures replaced with HDR textures.|
|rtx.skyProbeSide|int|1024||
|rtx.skyUiDrawcallCount|int|0||
|rtx.stochasticAlphaBlendDepthDifference|float|0.1|Max depth difference for a valid neighbor.|
|rtx.stochasticAlphaBlendDiscardBlackPixel|bool|False|Discard black pixels.|
|rtx.stochasticAlphaBlendEnableFilter|bool|True|Filter samples to suppress noise.|
|rtx.stochasticAlphaBlendInitialSearchRadius|float|10|Initial search radius.|
|rtx.stochasticAlphaBlendNormalSimilarity|float|0.9|Min normal similarity for a valid neighbor.|
|rtx.stochasticAlphaBlendOpacityThreshold|float|0.95|Max opacity to use stochastic alpha blend.|
|rtx.stochasticAlphaBlendPlanarDifference|float|0.2|Max planar difference for a valid neighbor.|
|rtx.stochasticAlphaBlendRadianceVolumeMultiplier|float|1|Radiance volume multiplier.|
|rtx.stochasticAlphaBlendRadiusExpandFactor|float|1.6|Multiply radius by this factor if cannot find a good neighbor.|
|rtx.stochasticAlphaBlendSearchIteration|int|6|Search iterations.|
|rtx.stochasticAlphaBlendSearchTheSameObject|bool|True|Only use radiance samples from the same object.|
|rtx.stochasticAlphaBlendShareNeighbors|bool|True|Share result with other pixels to accelerate search.|
|rtx.stochasticAlphaBlendUseNeighborSearch|bool|True|Get radiance from neighbor opaque pixels.|
|rtx.stochasticAlphaBlendUseRadianceVolume|bool|True|Get radiance from radiance volume.|
|rtx.taauPreset|int|1|Adjusts TAA-U scaling factor, trades quality for performance.|
|rtx.temporalAA.colorClampingFactor|float|1||
|rtx.temporalAA.maximumRadiance|float|10000||
|rtx.temporalAA.newFrameWeight|float|1||
|rtx.tonemap.colorBalance|float3|1, 1, 1||
|rtx.tonemap.colorGradingEnabled|bool|False||
|rtx.tonemap.contrast|float|1||
|rtx.tonemap.curveShift|float|0|Range [0, inf). Amount by which to shift the tone curve up or down. Nonzero values will cause additional clipping.|
|rtx.tonemap.dynamicRange|float|15|Range [0, inf). Without further adjustments, the tone curve will try to fit the entire luminance of the scene into the range [-dynamicRange, 0] in linear photographic stops. Higher values adjust for ambient monitor lighting; perfect conditions -> 17.587 stops.|
|rtx.tonemap.exposureBias|float|0||
|rtx.tonemap.finalizeWithACES|bool|False||
|rtx.tonemap.maxExposureIncrease|float|5|Range [0, inf). Forces the tone curve to not increase luminance values at any point more than this value.|
|rtx.tonemap.saturation|float|1||
|rtx.tonemap.shadowContrast|float|0|Range [0, inf). Additional gamma power to apply to the tone of the tone curve below shadowContrastEnd.|
|rtx.tonemap.shadowContrastEnd|float|0|Range (-inf, 0]. High endpoint for the shadow contrast effect in linear stops; values above this are unaffected.|
|rtx.tonemap.shadowMinSlope|float|0|Range [0, inf). Forces the tone curve below a linear value of 0.18 to have at least this slope, making the tone darker.|
|rtx.tonemap.toneCurveMaxStops|float|8|High endpoint of the tone curve (in log2(linear)).|
|rtx.tonemap.toneCurveMinStops|float|-24|Low endpoint of the tone curve (in log2(linear)).|
|rtx.tonemap.tonemappingEnabled|bool|True||
|rtx.tonemap.tuningMode|bool|False||
|rtx.tonemappingMode|int|1||
|rtx.translucentDecalAlbedoFactor|float|10|Scale for the albedo of decals that are applied to a translucent base material, to make the decals more visible.|
|rtx.translucentMaterial.enableDiffuseLayerOverride|bool|False||
|rtx.translucentMaterial.normalIntensity|float|1||
|rtx.translucentMaterial.transmittanceColorBias|float|0||
|rtx.translucentMaterial.transmittanceColorScale|float|1||
|rtx.translucentSpecularLobeSamplingProbabilityZeroThreshold|float|0.01|The threshold for which to zero translucent specular probability weight values.|
|rtx.translucentTransmissionLobeSamplingProbabilityZeroThreshold|float|0.01|The threshold for which to zero translucent transmission probability weight values.|
|rtx.uniqueObjectDistance|float|300|[cm]|
|rtx.upscalerType|int|1|Upscaling boosts performance with varying degrees of image quality tradeoff depending on the type of upscaler and the quality mode/preset.|
|rtx.upscalingMipBias|float|0||
|rtx.useAnisotropicFiltering|bool|True||
|rtx.useDenoiser|bool|True||
|rtx.useDenoiserReferenceMode|bool|False||
|rtx.useHighlightLegacyMode|bool|False||
|rtx.useHighlightUnsafeAnchorMode|bool|False||
|rtx.useHighlightUnsafeReplacementMode|bool|False||
|rtx.useIntersectionBillboardsOnPrimaryRays|bool|False||
|rtx.useLiveShaderEditMode|bool|False|When set to true shaders will be automatically recompiled when any shader file is updated (saved for instance) in addition to the usual manual recompilation trigger.|
|rtx.useObsoleteHashOnTextureUpload|bool|False||
|rtx.usePartialDdsLoader|bool|True||
|rtx.usePostFilter|bool|True|Uses post filter to remove fireflies in the denoised result.|
|rtx.useRTXDI|bool|True|Enable RTXDI to improve direct light quality.|
|rtx.useRayPortalVirtualInstanceMatching|bool|True||
|rtx.useReSTIRGI|bool|True|Enable ReSTIR GI to improve indirect light quality.|
|rtx.useVertexCapture|bool|False||
|rtx.useVirtualShadingNormalsForDenoising|bool|True||
|rtx.useWhiteMaterialMode|bool|False||
|rtx.validateCPUIndexData|bool|False||
|rtx.vertexColorStrength|float|0.6||
|rtx.viewDistance.distanceFadeMax|float|500|The view distance based on the result of the view distance function to end view distance noise fading at (and effectively draw nothing past this point), only used for the Coherent Noise view distance mode.|
|rtx.viewDistance.distanceFadeMin|float|400|The view distance based on the result of the view distance function to start view distance noise fading at, only used for the Coherent Noise view distance mode.|
|rtx.viewDistance.distanceFunction|int|0|The view distance function, Euclidean is a simple distance from the camera, whereas Planar Euclidean will ignore distance across the world's "up" direction.|
|rtx.viewDistance.distanceMode|int|0|The view distance mode, None disables view distance, Hard Cutoff will cut off geometry past a point, and Coherent Noise will feather geometry out using a stable worldspace noise pattern (experimental).|
|rtx.viewDistance.distanceThreshold|float|500|The view distance to draw out to based on the result of the view distance function, only used for the Hard Cutoff view distance mode.|
|rtx.viewDistance.noiseScale|float|3|The scale per meter value applied to ther world space position fed into the noise generation function for generating the fade in Coherent Noise view distance mode.|
|rtx.viewModel.enable|bool|False|If true, try to resolve view models (e.g. first-person weapons). World geometry doesn't have shadows / reflections / etc from the view models.|
|rtx.viewModel.enableVirtualInstances|bool|True|If true, virtual instances are created to render the view models behind a portal.|
|rtx.viewModel.perspectiveCorrection|bool|True|If true, apply correction to view models (e.g. different FOV is used for view models).|
|rtx.viewModel.rangeMeters|float|1|[meters] Max distance at which to find a portal for view model virtual instances. If rtx.viewModel.separateRays is true, this is also max length of view model rays.|
|rtx.viewModel.scale|float|1|Scale for view models. Minimize to prevent clipping.|
|rtx.viewModel.separateRays|bool|False|If true, launch additional primary rays to render view models on top of everything.|
|rtx.volumetricAnisotropy|float|0|The anisotropy of the scattering phase function (-1 being backscattering, 0 being isotropic, 1 being forward scattering).|
|rtx.volumetricClampedReprojectionConfidencePenalty|float|0.5|The penalty from [0, 1] to apply to the sample count of temporally reprojected reservoirs when reprojection is clamped to the fustrum (indicating lower quality reprojection).|
|rtx.volumetricEnableInitialVisibility|bool|True|Determines whether to trace a visibility ray for Reservoir samples.|
|rtx.volumetricEnableTemporalResampling|bool|True|Indicates if temporal resampling should be used for volume integration.|
|rtx.volumetricInitialRISSampleCount|int|8|The number of RIS samples to select from the global pool of lights when constructing a Reservoir sample.|
|rtx.volumetricSingleScatteringAlbedo|float3|0.9, 0.9, 0.9|The single scattering albedo (otherwise known as the particle albedo) representing the ratio of scattering to absorption.|
|rtx.volumetricTemporalReuseMaxSampleCount|int|200|The number of samples to clamp temporal reservoirs to, should usually be around the value: desired_max_history_frames * average_reservoir_samples.|
|rtx.volumetricTransmittanceColor|float3|0.9, 0.85, 0.8|The color to use for calculating transmittance measured at a specific distance.|
|rtx.volumetricTransmittanceMeasurementDistance|float|10000|The distance the specified transmittance color was measured at. Lower distances indicate a denser medium.|
|rtx.worldSpaceUiBackgroundOffset|float|-0.01|Distance along normal to offset the background of screens.|
|rtx.zUp|bool|False|Indicates that the Z axis is the "upward" axis in the world when true, otherwise the Y axis when false.|

## Complex Types
| RTX Option | Type | Default Value | Description |
| :-- | :-: | :-: | :-- |
|rtx.animatedWaterTextures|hash set|||
|rtx.baseGameModPathRegex|string||Regex used to redirect RTX Remix Runtime to another path for replacements and rtx.conf.|
|rtx.baseGameModRegex|string||Regex used to determine if the base game is running a mod, like a sourcemod.|
|rtx.beamTextures|hash set|||
|rtx.captureInstanceStageName|string|||
|rtx.cutoutTextures|hash set|||
|rtx.decalTextures|hash set||Static geometric decals or decals with complex topology.
These materials will be blended over the materials underneath them.
A small offset is applied to each flat part of these decals.|
|rtx.dynamicDecalTextures|hash set||Dynamically spawned geometric decals, such as bullet holes.
These materials will be blended over the materials underneath them.
A small offset is applied to each triangle fan in these decals.|
|rtx.geometryAssetHashRuleString|string|positions,indices,geometrydescriptor|Defines which hashes we need to include when sampling from replacements and doing USD capture.|
|rtx.geometryGenerationHashRuleString|string|positions,indices,texcoords,geometrydescriptor|Defines which asset hashes we need to generate via the geometry processing engine.|
|rtx.hideInstanceTextures|hash set|||
|rtx.ignoreLights|hash set|||
|rtx.ignoreTextures|hash set|||
|rtx.lightConverter|hash set|||
|rtx.lightmapTextures|hash set|||
|rtx.nonOffsetDecalTextures|hash set||Geometric decals with arbitrary topology that are already offset from the base geometry.
These materials will be blended over the materials underneath them. |
|rtx.opacityMicromapIgnoreTextures|hash set|||
|rtx.particleTextures|hash set|||
|rtx.playerModelBodyTextures|hash set|||
|rtx.playerModelTextures|hash set|||
|rtx.rayPortalModelTextureHashes|hash vector||Texture hashes identifying ray portals. Allowed number of hashes: {0, 2}.|
|rtx.skyBoxTextures|hash set|||
|rtx.sourceRootPath|string||A path pointing at the root folder of the project, used to override the path to the root of the project generated at build-time (as this path is only valid for the machine the project was originally compiled on). Used primarily for locating shader source files for runtime shader recompilation.|
|rtx.terrainTextures|hash set|||
|rtx.uiTextures|hash set|||
|rtx.worldSpaceUiBackgroundTextures|hash set|||
|rtx.worldSpaceUiTextures|hash set|||
