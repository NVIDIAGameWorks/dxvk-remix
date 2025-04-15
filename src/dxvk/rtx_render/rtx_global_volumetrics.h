/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_types.h"
#include "rtx/pass/volume_args.h"
#include "rtx/utility/shader_types.h"
#include "rtx_resources.h"
#include "rtx_camera_manager.h"

namespace dxvk {

  class RtxGlobalVolumetrics : public CommonDeviceObject, public RtxPass {

  public:
    enum QualityLevel : uint32_t {
      Low = 0,
      Medium,
      High,
      Ultra,
      Insane,
      QualityCount
    };

    // We'll be taking the log of transmittance, and so must protect against log(1) == 0, since this will be used in a division.  
    // Care must also be taken to not end up with a number that will break FP16 assumptions internal to volumetrics system. 
    // This number was found empirically.
    static constexpr float MaxTransmittanceValue = 1.f - 1.f / 255.f;

    // Minimum transmittance value to avoid log(0)
    static constexpr float MinTransmittanceValue = 1.f / 255.f;

    enum PresetType : uint32_t {
      Default,
      HeavyFog,
      LightFog,
      Mist,
      Haze,
      Dust,
      Smoke,
      PresetCount
    };

    struct Preset {
      Vector3 transmittanceColor;
      float transmittanceMeasurementDistance;
      Vector3 singleScatteringAlbedo;
      float anisotropy;
      bool enableHeterogeneousFog;
      float noiseFieldSpatialFrequency;
      uint32_t noiseFieldOctaves;
      float noiseFieldDensityScale;

      // Constructor for easier initialization
      Preset(Vector3 transmittanceColor, float transmittanceDistance,
        Vector3 scatteringAlbedo, float aniso, bool enableHeterogeneous,
        float noiseFrequency, uint32_t noiseOctaves, float noiseDensityScale)
        : transmittanceColor(transmittanceColor),
        transmittanceMeasurementDistance(transmittanceDistance),
        singleScatteringAlbedo(scatteringAlbedo),
        anisotropy(aniso),
        enableHeterogeneousFog(enableHeterogeneous),
        noiseFieldSpatialFrequency(noiseFrequency),
        noiseFieldOctaves(noiseOctaves),
        noiseFieldDensityScale(noiseDensityScale) { }
    };

    // Froxel Radiance Cache/Volumetric Lighting ptions
    // Note: The effective froxel grid resolution (based on the resolution scale) and froxelDepthSlices when multiplied together give the number of froxel cells, and this should be greater than the maximum number of
    // "concurrent" threads the GPU can execute at once to saturate execution and ensure maximal occupancy. This can be calculated by looking at how many warps per multiprocessor the GPU can have at once (This can
    // be found in CUDA Tuning guides such as https://docs.nvidia.com/cuda/ampere-tuning-guide/index.html) and then multiplying it by the number of multiprocessors (SMs) on the GPU in question, and finally turning
    // this into a thread count by mulitplying by how many threads per warp there are (typically 32).
    // Example for a RTX 3090: 82 SMs * 64 warps per SM * 32 threads per warp = 167,936 froxels to saturate the GPU. It is fine to be a bit below this though as most gpus will have fewer SMs than this, and higher resolutions
    // will also use more froxels due to how the grid is allocated with respect to the (downscaled when DLSS is in use) resolution, and we don't want the froxel passes to be too expensive (unless higher quality results are desired).
    RTX_OPTION("rtx.volumetrics", float, restirGridGuardBandFactor, 1.1f, "The scale factor for the restir grid guard band, which is an extended part of the viewing frustum for which we should calculate lighting information for, even though they are technically offscreen.  This helps reduce noise in cases where the camera is moving around.");
    RTX_OPTION("rtx.volumetrics", uint32_t, restirGridScale, 4,
               "The scale factor to divide the x and y froxel grid resolution by to determine the x and y dimensions of the ReSTIR froxel grid.\n"
               "Note that unlike the rtx.volumetrics.froxelGridResolutionScale option this is not dividing the render resolution, but rather is a scalar on top of the resulting froxel grid resolution after it is divided by the resolution scale.");
    RTX_OPTION("rtx.volumetrics", uint32_t, froxelGridResolutionScale, 8, "The scale factor to divide the x and y render resolution by to determine the x and y dimensions of the froxel grid.");
    RTX_OPTION("rtx.volumetrics", uint16_t, froxelDepthSlices, 64, "The z dimension of the froxel grid. Must be constant after initialization.");
    RTX_OPTION("rtx.volumetrics", uint16_t, restirFroxelDepthSlices, 128, "The z dimension of the ReSTIR froxel grid. Must be constant after initialization.");
    RTX_OPTION("rtx.volumetrics", uint8_t, maxAccumulationFrames, 128,
               "The number of frames to accumulate volume lighting samples over, maximum of 254.\n"
               "Large values result in greater image stability at the cost of potentially more temporal lag."
               "Should generally be set to as large a value as is viable as the froxel radiance cache is assumed to be fairly noise-free and stable which temporal accumulation helps with.");
    RTX_OPTION("rtx.volumetrics", float, froxelDepthSliceDistributionExponent, 2.0f, "The exponent to use on depth values to nonlinearly distribute froxels away from the camera. Higher values bias more froxels closer to the camera with 1 being linear.");
    RTX_OPTION("rtx.volumetrics", float, froxelMaxDistanceMeters, 20.0f, "The maximum distance in world units to allocate the froxel grid out to. Should be less than the distance between the camera's near and far plane, as the froxel grid will clip to the far plane otherwise.  The unit of measurement is meters.");
    RTX_OPTION("rtx.volumetrics", float, froxelFireflyFilteringLuminanceThreshold, 1000.0f, "Sets the maximum luminance threshold for the volumetric firefly filtering to clamp to.");
    RTX_OPTION("rtx.volumetrics", uint32_t, initialRISSampleCount, 32,
               "The number of RIS samples to select from the global pool of lights when constructing a Reservoir sample.\n"
               "Higher values generally increases the quality of the selected light sample, though similar to the general RIS light sample count has diminishing returns.");
    RTX_OPTION("rtx.volumetrics", bool, enableInitialVisibility, true,
               "Determines whether to trace a visibility ray for Reservoir samples.\n"
               "Results in slightly less noise with the volumetric froxel grid light samples at the cost of a ray per froxel cell each frame and should generally be enabled.");
    RTX_OPTION("rtx.volumetrics", bool, visibilityReuse, true,
               "Determines whether to reuse visibility ray samples spatially across the reservoir.\n"
               "Results in slightly less noise with the volumetric froxel grid light samples at the cost of a ray per froxel cell each frame and should generally be enabled.");
    RTX_OPTION("rtx.volumetrics", bool, enableTemporalResampling, true,
               "Indicates if temporal resampling should be used for volume integration.\n"
               "Temporal resampling allows for reuse of temporal information when picking froxel grid light samples similar to how ReSTIR works, providing higher quality light samples.\n"
               "This should generally be enabled but currently due to the lack of temporal bias correction this option will slightly bias the lighting result.");
    RTX_OPTION("rtx.volumetrics", bool, enableSpatialResampling, true,
               "Indicates if spatial resampling should be used for volume integration.\n"
               "Spatial resampling allows for reuse of spatial information when picking froxel grid light samples similar to how ReSTIR works, providing higher quality light samples.\n");
    RTX_OPTION("rtx.volumetrics", uint16_t, temporalReuseMaxSampleCount, 2, "The number of samples to clamp temporal reservoirs to, should usually be around the value: desired_max_history_frames * average_reservoir_samples.");
    RTX_OPTION("rtx.volumetrics", uint16_t, spatialReuseMaxSampleCount, 8, "The number of spatial samples to perform, generally higher is better, but the law of diminishing returns applies.");
    RTX_OPTION("rtx.volumetrics", float, spatialReuseSamplingRadius, 0.8f, "Search radius (in froxels) to search for neighbour candidates in spatial reuse pass.");
    RTX_OPTION_FLAG("rtx.volumetrics", bool, enableReferenceMode, false, RtxOptionFlags::NoSave, "Enables reference mode for volumetrics.  This is very expensive, but allows for rendering engineers to test how close sampling approximations are to the real thing. This will not save.");
    RTX_OPTION("rtx.volumetrics", bool, enable, true,
               "Enabling volumetric lighting provides higher quality ray traced physical volumetrics, disabling falls back to cheaper depth based fog.\n"
               "Note that disabling this option does not disable the froxel radiance cache as a whole as it is still needed for other non-volumetric lighting approximations.");
    RTX_OPTION("rtx.volumetrics", Vector3, transmittanceColor, Vector3(0.999f, 0.999f, 0.999f),
               "The color to use for calculating transmittance measured at a specific distance.\n"
               "Note that this color is assumed to be in sRGB space and gamma encoded as it will be converted to linear for use in volumetrics.");
    RTX_OPTION("rtx.volumetrics", float, transmittanceMeasurementDistanceMeters, 200.0f, "The distance the specified transmittance color was measured at. Lower distances indicate a denser medium.  The unit of measurement is meters, respects scene scale.");
    RTX_OPTION("rtx.volumetrics", Vector3, singleScatteringAlbedo, Vector3(0.999f, 0.999f, 0.999f),
               "The single scattering albedo (otherwise known as the particle albedo) representing the ratio of scattering to absorption.\n"
               "While color-like in many ways this value is assumed to be more of a mathematical albedo (unlike material albedo which is treated more as a color), and is therefore treated as linearly encoded data (not gamma).");
    RTX_OPTION("rtx.volumetrics", float, anisotropy, 0.0f, "The anisotropy of the scattering phase function (-1 being backscattering, 0 being isotropic, 1 being forward scattering).");
    RTX_OPTION("rtx.volumetrics", bool, enableInPortals, false,
               "Enables using extra frustum-aligned volumes for lighting in portals.\n"
               "Note that enabling this option will require 3x the memory of the typical froxel grid as well as degrade performance in some cases.\n"
               "This option should be enabled always in games using ray portals for proper looking volumetrics through them, but should be disabled on any game not using ray portals.\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");
    RTX_OPTION("rtx.volumetrics", bool, enableHeterogeneousFog, false, "Enables a noise-driven heterogeneous fog approximation.");
    RTX_OPTION("rtx.volumetrics", float, noiseFieldSubStepSizeMeters, 10.0f, "Maximum substep size in world space for sampling the heterogeneous fog noise volume. Units are meters, respects scene scale.");
    RTX_OPTION("rtx.volumetrics", float, noiseFieldSpatialFrequency, 0.02f, "Visual Parameter: Frequency at which to sample the heterogeneous fog noise volume.");
    RTX_OPTION("rtx.volumetrics", uint32_t, noiseFieldOctaves, 4.0f, "Visual Parameter: Controls how much detail is in the heterogeneous fog noise volume.  Detail will be fundamentally limited by the resolution of the froxel grid.");
    RTX_OPTION("rtx.volumetrics", float, noiseFieldDensityScale, 1.0f, "A scale factor for the heterogeneous fog noise volume density.");
    RTX_OPTION("rtx.volumetrics", float, depthOffset, .5f, "Depth offset to avoid volumetric light leaking.");
    RTX_OPTION("rtx.volumetrics", bool, enableAtmosphere, false,
               "Enables a finite atmosphere in the volumetrics system.\n"
               "When false, the volumetric volume is assumed to reach to infinity in every direction, when true the volumetric volume will be limited to that a finite atmosphere controlled by parameters describing atmosphere height and its curvature via a planetary radius.\n"
               "This option should generally be enabled if volumetrics are used in outdoor settings as without a finite atmosphere infinite light sources such as the skybox and distant lights will not function properly.");
    RTX_OPTION("rtx.volumetrics", float, atmospherePlanetRadiusMeters, 10000.f, "Radius of the planet in meters, respects scene scale.");
    RTX_OPTION("rtx.volumetrics", float, atmosphereHeightMeters, 30.0f, "Height of the atmosphere in meters, respects scene scale.");
    RTX_OPTION("rtx.volumetrics", bool, atmosphereInverted, false,
               "A flag to invert the rendering of the volumetric atmosphere if rtx.volumetrics.enableAtmosphere is enabled.\n"
               "Some games render the world upside down and that cannot be detected automatically, this setting can be used to correct that inversion for the volumetric atmosphere.");
    RTX_OPTION_FLAG("rtx.volumetrics", bool, debugDisableRadianceScaling, false, RtxOptionFlags::NoSave,
               "Disables the volumetric radiance scaling feature, this effectively sets the per light radiance scaling to 1.f.  Useful when debugging issues when this feature is suspected.\n"
               "Do not ship your mod with this in the rtx.conf.");

    // Note: Options for remapping legacy D3D9 fixed function fog parameters to volumetric lighting parameters and overwriting the global volumetric parameters when fixed function fog is enabled.
    // Useful for cases where dynamic fog parameters are used throughout a game (or very per-level) that cannot be captrued merely in a global set of volumetric parameters. To see remapped results
    // volumetric lighting in general must be enabled otherwise these settings will have no effect.
    RTX_OPTION("rtx.volumetrics", bool, enableFogRemap, false,
               "A flag to enable or disable fixed function fog remapping. Only takes effect when volumetrics are enabled.\n"
               "Typically many old games used fixed function fog for various effects and while sometimes this fog can be replaced with proper volumetrics globally, other times require some amount of dynamic behavior controlled by the game.\n"
               "When enabled this option allows for remapping of fixed function fog parameters from the game to volumetric parameters to accomodate this dynamic need.");
    RTX_OPTION("rtx.volumetrics", bool, enableFogColorRemap, false,
               "A flag to enable or disable remapping fixed function fox's color. Only takes effect when fog remapping in general is enabled.\n"
               "Enables or disables remapping functionality relating to the color parameter of fixed function fog with the exception of the multiscattering scale (as this scale can be set to 0 to disable it).\n"
               "This allows dynamic changes to the game's fog color to be reflected somewhat in the volumetrics system. Overrides the specified volumetric transmittance color.");
    RTX_OPTION("rtx.volumetrics", bool, enableFogMaxDistanceRemap, true,
               "A flag to enable or disable remapping fixed function fox's max distance. Only takes effect when fog remapping in general is enabled.\n"
               "Enables or disables remapping functionality relating to the max distance parameter of fixed function fog.\n"
               "This allows dynamic changes to the game's fog max distance to be reflected somewhat in the volumetrics system. Overrides the specified volumetric transmittance measurement distance.");
    RTX_OPTION("rtx.volumetrics", float, fogRemapMaxDistanceMinMeters, 1.0f,
               "A value controlling the \"max distance\" fixed function fog parameter's minimum remapping bound.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.  In meters.");
    RTX_OPTION("rtx.volumetrics", float, fogRemapMaxDistanceMaxMeters, 40.0f,
               "A value controlling the \"max distance\" fixed function fog parameter's maximum remapping bound.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.  In meters.");
    RTX_OPTION("rtx.volumetrics", float, fogRemapTransmittanceMeasurementDistanceMinMeters, 20.0f,
               "A value representing the transmittance measurement distance's minimum remapping bound.\n"
               "When the fixed function fog's \"max distance\" parameter is at or below its specified minimum the volumetric system's transmittance measurement distance will be set to this value and interpolated upwards.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.  In meters.");
    RTX_OPTION("rtx.volumetrics", float, fogRemapTransmittanceMeasurementDistanceMaxMeters, 100.0f,
               "A value representing the transmittance measurement distance's maximum remapping bound.\n"
               "When the fixed function fog's \"max distance\" parameter is at or above its specified maximum the volumetric system's transmittance measurement distance will be set to this value and interpolated upwards.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.  In meters.");
    RTX_OPTION("rtx.volumetrics", float, fogRemapColorMultiscatteringScale, 0.1f,
               "A value representing the scale of the fixed function fog's color in the multiscattering approximation.\n"
               "This scaling factor is applied to the fixed function fog's color and becomes a multiscattering approximation in the volumetrics system.\n"
               "Sometimes useful but this multiscattering approximation is very basic (just a simple ambient term for now essentially) and may not look very good depending on various conditions.");

    enum class RaytraceMode {
      RayQuery = 0,
      RayQueryRayGen,
      TraceRay,
      Count
    };

    RtxGlobalVolumetrics(DxvkDevice* device);
    ~RtxGlobalVolumetrics() = default;

    VolumeArgs getVolumeArgs(CameraManager const& cameraManager, FogState const& fogState, bool enablePortalVolumes) const;

    void dispatch(class RtxContext* ctx, const Resources::RaytracingOutput& rtOutput, uint32_t numActiveFroxelVolumes);
    
    const Resources::Resource& getCurrentVolumeReservoirs() const { return m_volumeReservoirs[0]; }
    const Resources::Resource& getPreviousVolumeReservoirs() const { return m_volumeReservoirs[1]; }
    const Resources::Resource& getCurrentVolumeAccumulatedRadianceY() const { return m_volumeAccumulatedRadianceY[m_swapTextures]; }
    const Resources::Resource& getPreviousVolumeAccumulatedRadianceY() const { return m_volumeAccumulatedRadianceY[!m_swapTextures]; }
    const Resources::Resource& getCurrentVolumeAccumulatedRadianceCoCg() const { return m_volumeAccumulatedRadianceCoCg[m_swapTextures]; }
    const Resources::Resource& getPreviousVolumeAccumulatedRadianceCoCg() const { return m_volumeAccumulatedRadianceCoCg[!m_swapTextures]; }
    const Resources::Resource& getCurrentVolumeAccumulatedRadianceAge() const { return m_volumeAccumulatedRadianceAge[m_swapTextures]; }
    const Resources::Resource& getPreviousVolumeAccumulatedRadianceAge() const { return m_volumeAccumulatedRadianceAge[!m_swapTextures]; }

    void showImguiSettings();

    void setQualityLevel(const QualityLevel desiredQualityLevel);
    void setPreset(const PresetType desiredPreset);

    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;

    virtual void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) override;
    virtual void releaseDownscaledResource() override;

  private:
    VkExtent3D m_froxelVolumeExtent;
    VkExtent3D m_restirFroxelVolumeExtent;
    uint32_t m_numFroxelVolumes;

    Resources::Resource m_volumeReservoirs[2];
    Resources::Resource m_volumeAccumulatedRadianceY[2];
    Resources::Resource m_volumeAccumulatedRadianceCoCg[2];
    Resources::Resource m_volumeAccumulatedRadianceAge[2];
    bool m_swapTextures = false;
    bool m_rebuildFroxels = false;

    DxvkRaytracingPipelineShaders getPipelineShaders(bool useRayQuery) const;

    virtual bool isEnabled() const override;
  };
}
