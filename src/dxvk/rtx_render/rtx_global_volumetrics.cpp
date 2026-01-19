/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_global_volumetrics.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_composite.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/volumetrics/volume_integrate_binding_indices.h"

#include <rtx_shaders/volume_integrate_rayquery.h>
#include <rtx_shaders/volume_restir_initial.h>
#include <rtx_shaders/volume_restir_visibility.h>
#include <rtx_shaders/volume_restir_temporal.h>
#include <rtx_shaders/volume_restir_spatial_resampling.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class VolumeRestirShaderInitial : public ManagedShader {
      SHADER_SOURCE(VolumeRestirShaderInitial, VK_SHADER_STAGE_COMPUTE_BIT, volume_restir_initial)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE3D(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT)

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeRestirShaderInitial);

    class VolumeRestirShaderVisibility : public ManagedShader {
      SHADER_SOURCE(VolumeRestirShaderVisibility, VK_SHADER_STAGE_COMPUTE_BIT, volume_restir_visibility)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)
        END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeRestirShaderVisibility);

    class VolumeRestirShaderTemporal : public ManagedShader {
      SHADER_SOURCE(VolumeRestirShaderTemporal, VK_SHADER_STAGE_COMPUTE_BIT, volume_restir_temporal)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE3D(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT)

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)
        END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeRestirShaderTemporal);

    class VolumeRestirShaderSpatialResampling : public ManagedShader {
      SHADER_SOURCE(VolumeRestirShaderSpatialResampling, VK_SHADER_STAGE_COMPUTE_BIT, volume_restir_spatial_resampling)

        BINDLESS_ENABLED()

        BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE3D(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT)

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeRestirShaderSpatialResampling);

    class VolumeIntegrateShader : public ManagedShader {
      SHADER_SOURCE(VolumeIntegrateShader, VK_SHADER_STAGE_COMPUTE_BIT, volume_integrate_rayquery)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER3D(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_Y)
        SAMPLER3D(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_CO_CG)
        SAMPLER3D(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_AGE)
        TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_Y)
        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_CO_CG)
        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_AGE)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeIntegrateShader);
  }

  static const std::array<RtxGlobalVolumetrics::Preset, RtxGlobalVolumetrics::PresetCount> Presets = {
      RtxGlobalVolumetrics::Preset( // Default
          Vector3(0.999f, 0.999f, 0.999f),  // transmittanceColor
          200.0f,                         // transmittanceMeasurementDistance
          Vector3(0.999f, 0.999f, 0.999f),  // singleScatteringAlbedo
          0.0f                             // anisotropy
      ),
      RtxGlobalVolumetrics::Preset( // HeavyFog
          Vector3(0.85f, 0.85f, 0.85f),
          5.0f,
          Vector3(0.9f, 0.9f, 0.9f),
          -0.2f
      ),
      RtxGlobalVolumetrics::Preset( // LightFog
          Vector3(0.93f, 0.93f, 0.93f),
          15.0f,
          Vector3(0.95f, 0.95f, 0.95f),
          -0.1f
      ),
      RtxGlobalVolumetrics::Preset( // Mist
          Vector3(0.96f, 0.96f, 0.96f),
          50.0f,
          Vector3(0.98f, 0.98f, 0.98f),
          0.1f
      ),
      RtxGlobalVolumetrics::Preset( // Haze
          Vector3(0.9f, 0.85f, 0.75f),
          70.0f,
          Vector3(0.8f, 0.8f, 0.8f),
          0.2f
      ),
      RtxGlobalVolumetrics::Preset( // Dust
          Vector3(0.87f, 0.73f, 0.5f),
          60.0f,
          Vector3(0.85f, 0.75f, 0.65f),
          0.4f
      ),
      RtxGlobalVolumetrics::Preset( // Smoke
          Vector3(0.87f, 0.73f, 0.5f),
          20.0f,
          Vector3(0.85f, 0.75f, 0.65f),
          0.6f
      )
  };

  RtxGlobalVolumetrics::RtxGlobalVolumetrics(DxvkDevice* device) : CommonDeviceObject(device), RtxPass(device) {
    // Volumetrics Options

    transmittanceColor.setDeferred(Vector3(
      std::clamp(transmittanceColor().x, 0.0f, 1.0f),
      std::clamp(transmittanceColor().y, 0.0f, 1.0f),
      std::clamp(transmittanceColor().z, 0.0f, 1.0f)));
    singleScatteringAlbedo.setDeferred(Vector3(
      std::clamp(singleScatteringAlbedo().x, 0.0f, 1.0f),
      std::clamp(singleScatteringAlbedo().y, 0.0f, 1.0f),
      std::clamp(singleScatteringAlbedo().z, 0.0f, 1.0f)));

    fogRemapMaxDistanceMinMeters.setDeferred(std::min(fogRemapMaxDistanceMinMeters(), fogRemapMaxDistanceMaxMeters()));
    fogRemapMaxDistanceMaxMeters.setDeferred(std::max(fogRemapMaxDistanceMinMeters(), fogRemapMaxDistanceMaxMeters()));
    fogRemapTransmittanceMeasurementDistanceMinMeters.setDeferred(std::min(fogRemapTransmittanceMeasurementDistanceMinMeters(), fogRemapTransmittanceMeasurementDistanceMaxMeters()));
    fogRemapTransmittanceMeasurementDistanceMaxMeters.setDeferred(std::max(fogRemapTransmittanceMeasurementDistanceMinMeters(), fogRemapTransmittanceMeasurementDistanceMaxMeters()));
  }

  // Quality level presets, x component controls the froxelGridResolutionScale and the y component controls the froxelDepthSlices settings.
  static const int2 qualityModes[RtxGlobalVolumetrics::QualityLevel::QualityCount] = {
    int2(32, 48),
    int2(16, 48),
    int2(8,  48),
    int2(4,  48),
    int2(3,  48)
  };
  // Note: Higher end options brought down when Portals are in use due to the the current volumetric solution for Portals requiring 3x more video memory
  // and cost. Additionally, these settings are tuned somewhat specifically for Portal RTX to further adjust performance to a desired level, in the future
  // a more generalized system is needed so this sort of performance/quality tradeoff may be made on a per-game basis.
  // Note: 32, 16, 12, 8, 4 is probably a more reasonable set of resolution scales, but set mostly to 16 for performance reasons for now.
  // See REMIX-3834 for more information.
  static const int2 portalQualityModes[RtxGlobalVolumetrics::QualityLevel::QualityCount] = {
    int2(32, 48),
    int2(16, 48),
    int2(16, 48),
    int2(16, 48),
    int2(8,  48)
  };

  void RtxGlobalVolumetrics::showPresetMenu() {
    const char* volumericQualityLevelName[] = {
      "Low",
      "Medium",
      "High",
      "Ultra",
      "Insane"
    };
    static_assert(sizeof(volumericQualityLevelName) / sizeof(volumericQualityLevelName[0]) == QualityLevel::QualityCount);

    for (uint32_t i = 0; i < QualityLevel::QualityCount; i++) {
      if (ImGui::Button(volumericQualityLevelName[i])) {
        setQualityLevel((QualityLevel) i);
      }
      ImGui::SameLine();
    }
    ImGui::TextUnformatted("Quality Level Preset");
  }

  void RtxGlobalVolumetrics::showImguiUserSettings() {
    showPresetMenu();
  }

  void RtxGlobalVolumetrics::showImguiSettings() {
    if (RemixGui::CollapsingHeader("Froxel Radiance Cache", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();

      showPresetMenu();

      RemixGui::Separator();

      static bool showAdvanced = false;
      RemixGui::Checkbox("Show Advanced Options", &showAdvanced);

      if (showAdvanced) {
        m_rebuildFroxels |= RemixGui::DragInt("Froxel Grid Resolution Scale", &froxelGridResolutionScaleObject(), 0.1f, 1);
        m_rebuildFroxels |= RemixGui::DragInt("Froxel Depth Slices", &froxelDepthSlicesObject(), 0.1f, 1, UINT16_MAX);
        RemixGui::DragInt("Max Accumulation Frames", &maxAccumulationFramesObject(), 0.1f, 1, UINT8_MAX);
        RemixGui::DragFloat("Froxel Depth Slice Distribution Exponent", &froxelDepthSliceDistributionExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Froxel Max Distance", &froxelMaxDistanceMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Froxel Firefly Filtering Luminance Threshold", &froxelFireflyFilteringLuminanceThresholdObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::Checkbox("Per-Portal Volumes", &enableInPortalsObject());

        RemixGui::Separator();
        RemixGui::Checkbox("Enable Reference Mode", &enableReferenceModeObject());
        RemixGui::Separator();

        ImGui::BeginDisabled(enableReferenceMode());

        m_rebuildFroxels |= RemixGui::DragInt("Restir Grid Downsample Factor", &restirGridScaleObject(), 0.1f, 1);
        m_rebuildFroxels |= RemixGui::DragInt("Restir Froxel Depth Slices", &restirFroxelDepthSlicesObject(), 0.1f, 1, UINT16_MAX);
        RemixGui::DragFloat("Restir Guard Band Scale Factor", &restirGridGuardBandFactorObject(), 0.1f, 1.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        RemixGui::DragInt("Initial RIS Sample Count", &initialRISSampleCountObject(), 0.05f, 1, UINT8_MAX);
        RemixGui::Checkbox("Enable Initial Visibility", &enableInitialVisibilityObject());
        ImGui::BeginDisabled(!enableInitialVisibility());
        RemixGui::Checkbox("Enable Visibility Reuse", &visibilityReuseObject());
        ImGui::EndDisabled();

        RemixGui::Checkbox("Enable Temporal Resampling", &enableTemporalResamplingObject());
        ImGui::BeginDisabled(!enableTemporalResampling());
        RemixGui::DragInt("Temporal Resampling Max Sample Count", &temporalReuseMaxSampleCountObject(), 1.0f, 1, UINT16_MAX);
        ImGui::EndDisabled();

        RemixGui::Separator();

        RemixGui::Checkbox("Enable Spatial Resampling", &enableSpatialResamplingObject());
        ImGui::BeginDisabled(!enableSpatialResampling());
        RemixGui::DragInt("Spatial Resampling Max Sample Count", &spatialReuseMaxSampleCountObject(), 1.0f, 1, UINT16_MAX);
        RemixGui::DragFloat("Clamped Spatial Resampling Search Radius", &spatialReuseSamplingRadiusObject(), 0.01f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();

        ImGui::EndDisabled();
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Volumetric Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();

      RemixGui::Checkbox("Enable Volumetric Lighting", &enableObject());
      {
        ImGui::Indent();
        ImGui::BeginDisabled(!enable());

        const char* volumericPresetName[] = {
          "-Select Preset and Hit Apply-",
          "Default",
          "Heavy Fog",
          "Light Fog",
          "Mist",
          "Haze",
          "Dust",
          "Smoke",
        };
        static_assert((sizeof(volumericPresetName) / sizeof(volumericPresetName[0]) - 1) == PresetType::PresetCount);

        ImGui::Text("Volumetric Visual Presets:");

        const float indent = 60.0f;
        static int itemIndex = 0;
        ImGui::PushItemWidth(ImMax(ImGui::GetContentRegionMax().x - indent, 1.0f));
        ImGui::PushID("volumetric visual preset");
        ImGui::ListBox("", &itemIndex, &volumericPresetName[0], (int) PresetType::PresetCount + 1, 3);
        ImGui::PopID();
        ImGui::PopItemWidth();

        if (ImGui::Button("Apply", ImVec2(ImMax(ImGui::GetContentRegionMax().x - indent, 1.0f), 0)) && itemIndex > 0) {
          setPreset((PresetType) (itemIndex - 1));
          itemIndex = 0;
        }

        RemixGui::Separator();

        static bool showAdvanced = false;
        RemixGui::Checkbox("Show Advanced Material Options", &showAdvanced);

        if (showAdvanced) {
          RemixGui::DragFloat3("Transmittance Color", &transmittanceColorObject(), 0.01f, 0.0f, MaxTransmittanceValue, "%.3f");
          RemixGui::DragFloat("Transmittance Measurement Distance", &transmittanceMeasurementDistanceMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat3("Single Scattering Albedo", &singleScatteringAlbedoObject(), 0.01f, 0.0f, 1.0f, "%.3f");
          RemixGui::DragFloat("Anisotropy", &anisotropyObject(), 0.01f, -.99f, .99f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Depth Offset", &depthOffsetObject(), 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

          RemixGui::Separator();

          RemixGui::Checkbox("Enable Heterogeneous Fog", &enableHeterogeneousFogObject());

          ImGui::BeginDisabled(!enableHeterogeneousFog());
          RemixGui::DragFloat("Noise Field Substep Size", &noiseFieldSubStepSizeMetersObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragInt("Noise Field Number of Octaves", &noiseFieldOctavesObject(), 0.05f, 1, 8);
          RemixGui::DragFloat("Noise Field Time Scale", &noiseFieldTimeScaleObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Noise Field Density Scale", &noiseFieldDensityScaleObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Noise Field Density Exponent", &noiseFieldDensityExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Noise Field Initial Frequency", &noiseFieldInitialFrequencyPerMeterObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Noise Field Lacunarity", &noiseFieldLacunarityObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Noise Field Gain", &noiseFieldGainObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::EndDisabled();
        }

        RemixGui::Separator();

        RemixGui::Checkbox("Atmosphere Enabled", &enableAtmosphereObject());
        ImGui::Indent();
        ImGui::BeginDisabled(!enableAtmosphere());
        {
          RemixGui::DragFloat("Planet Radius", &atmospherePlanetRadiusMetersObject(), 0.1f, -FLT_MAX, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::DragFloat("Height", &atmosphereHeightMetersObject(), 0.1f, -FLT_MAX, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          RemixGui::Checkbox("Inverted", &atmosphereInvertedObject());
          ImGui::EndDisabled();
        }
        ImGui::Unindent();

        RemixGui::Separator();
        RemixGui::Checkbox("Enable Legacy Fog Remapping", &enableFogRemapObject());
        RemixGui::Separator();

        ImGui::BeginDisabled(!enableFogRemap());
        {
          ImGui::Indent();

          RemixGui::Checkbox("Enable Fog Color Remapping", &enableFogColorRemapObject());

          RemixGui::Checkbox("Enable Fog Max Distance Remapping", &enableFogMaxDistanceRemapObject());

          ImGui::BeginDisabled(!enableFogMaxDistanceRemap());
          {
            RemixGui::DragFloat("Legacy Max Distance Min", &fogRemapMaxDistanceMinMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            RemixGui::DragFloat("Legacy Max Distance Max", &fogRemapMaxDistanceMaxMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            RemixGui::DragFloat("Remapped Transmittance Measurement Distance Min", &fogRemapTransmittanceMeasurementDistanceMinMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            RemixGui::DragFloat("Remapped Transmittance Measurement Distance Max", &fogRemapTransmittanceMeasurementDistanceMaxMetersObject(), 0.25f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
          }
          ImGui::EndDisabled();

          RemixGui::DragFloat("Color Multiscattering Scale", &fogRemapColorMultiscatteringScaleObject(), 0.01f, 0.0f, FLT_MAX, "%.2f", ImGuiSliderFlags_AlwaysClamp);

          ImGui::Unindent();
        }
        ImGui::EndDisabled();

        ImGui::EndDisabled();
        ImGui::Unindent();
      }

      RemixGui::Separator();
      ImGui::Dummy({ 0, 4 });
      {
        ImGui::Indent();
        m_device->getCommon()->metaComposite().showDepthBasedFogImguiSettings();
        ImGui::Unindent();
      }

      ImGui::Unindent();
    }
  }

  void RtxGlobalVolumetrics::setQualityLevel(const QualityLevel desiredQualityLevel) {
    // Note: Checking for Portals in volumetrics being enabled here may not work if this option is changed via ImGui on the
    // same frame the quality level is set (since the quality level currently is set before the checkbox is read). In practice
    // though this should never happen.
    int2 qualityPreset;

    if (enableInPortals()) {
      qualityPreset = portalQualityModes[desiredQualityLevel];
    } else {
      qualityPreset = qualityModes[desiredQualityLevel];
    }

    // Set new values based on preset values and cache old values

    const auto newFroxelGridResolutionScale = qualityPreset.x;
    const auto newFroxelDepthSlices = qualityPreset.y;
    const auto oldFroxelGridResolutionScale = froxelGridResolutionScale();
    const auto oldFroxelDepthSlices = froxelDepthSlices();

    froxelGridResolutionScale.setDeferred(newFroxelGridResolutionScale);
    froxelDepthSlices.setDeferred(newFroxelDepthSlices);

    // Indicate that the froxel resources should be rebuilt if any relevant values changed

    if (
      newFroxelGridResolutionScale != oldFroxelGridResolutionScale ||
      newFroxelDepthSlices != oldFroxelDepthSlices
    ) {
      m_rebuildFroxels = true;
    }
  }

  void RtxGlobalVolumetrics::setPreset(const PresetType presetType) {
    const RtxGlobalVolumetrics::Preset& preset = Presets[presetType];

    // Set RTX options using the values from the preset
    transmittanceColor.setDeferred(preset.transmittanceColor);
    transmittanceMeasurementDistanceMeters.setDeferred(preset.transmittanceMeasurementDistance);
    singleScatteringAlbedo.setDeferred(preset.singleScatteringAlbedo);
    anisotropy.setDeferred(preset.anisotropy);
    enableFogRemap.setDeferred(false);
  }

  // This function checks the fog density to determine using physical fog or fix function fog.
  // When the fog density is over threshold, we will use fix function fog as call back.
  // A typical use for this function is checking if the player is in the water, which has high density and we want to use fix function fog.
  // Note: Fogs in Portal uses linear fix fog function, so the density can only be approximated
  bool shouldConvertToPhysicalFog(const FogState& fogState, const float fogDensityThrehold) {
    if (fogState.mode == D3DFOG_NONE || (fogState.mode == D3DFOG_LINEAR && fogState.end < 1e-7f)) {
      return true;
    }

    // Exponential fog function approximation with linear fog function:
    // Push the linear function start point (x = 0) towards exponential function,
    // then make the exp function as close as to the linear function when x=end (make the exp function curve convergence to the linear)
    // ExpFunc(0) = Linear(0) -> Move linear function to match exp function start point, we get a new linear function Linear'(x)
    // ExpFunc(end) ~ Linear'(end)
    // e^(-D * f) = (eps + (1 - (f - n) / f)
    // => D = ln(1 / (eps + (1 - (f - n) / f ) ) ) / f
    constexpr float epsilon = 0.001f;

    const float n = fogState.scale;
    const float invF = 1.0f / fogState.end;

    const float approximateExpFarPointValue = epsilon + n * invF; // eps + (1.0f - (f - n) / f) = esp + (1.0f - n / f)
    const float approximateDensity = std::log(1.0f / approximateExpFarPointValue) * invF;

    return approximateDensity < fogDensityThrehold;
  }

  VolumeArgs RtxGlobalVolumetrics::getVolumeArgs(CameraManager const& cameraManager, FogState const& fogState, bool enablePortalVolumes) const {
    // Calculate the volumetric parameters from options and the fixed function fog state

    // Note: Volumetric transmittance color option is in gamma space, so must be converted to linear for usage in the volumetric system.
    Vector3 transmittanceColorLinear{ sRGBGammaToLinear(transmittanceColor()) };

    // Note: Fall back to usual default in cases such as the "none" D3D fog mode, no fog remapping specified, or invalid values in the fog mode derivation
    // (such as dividing by zero).
    float transmittanceMeasurementDistance = transmittanceMeasurementDistanceMeters() * RtxOptions::getMeterToWorldUnitScale();
    Vector3 multiScatteringEstimate = Vector3();

    // Check if fog density is below the configurable threshold to determine if physical volumetrics should be used.
    // This threshold was created specifically for Portal RTX's underwater fixed function fog.
    const bool canUsePhysicalFog = shouldConvertToPhysicalFog(fogState, waterFogDensityThreshold());

    if (
      enableFogRemap() &&
      // Note: Only consider remapping fog if any fixed function fog is actually enabled (not the "none" mode).
      fogState.mode != D3DFOG_NONE &&
      canUsePhysicalFog
    ) {
      // Handle Fog Color remapping
      // Note: This must happen first as max distance remapping will depend on the luminance derived from the color determined here.
      if (enableFogColorRemap()) {
        // Note: Legacy fixed function fog color is in gamma space as all the rendering in old games was typically in gamma space, same assumption we make
        // for textures/lights.
        transmittanceColorLinear = sRGBGammaToLinear(fogState.color);
      }

      // Clamp to avoid black color, which may cause NaN issue.
      transmittanceColorLinear = clamp(transmittanceColorLinear, Vector3(MinTransmittanceValue), Vector3(MaxTransmittanceValue));

      // Handle Fog Max Distance remapping

      if (enableFogMaxDistanceRemap()) {
        // Switch transmittance measurement distance derivation from D3D9 fog based on which fog mode is in use

        if (fogState.mode == D3DFOG_LINEAR) {
          float fogRemapMaxDistanceMin { fogRemapMaxDistanceMinMeters() * RtxOptions::getMeterToWorldUnitScale() };
          float fogRemapMaxDistanceMax { fogRemapMaxDistanceMaxMeters() * RtxOptions::getMeterToWorldUnitScale() };
          float fogRemapTransmittanceMeasurementDistanceMin { fogRemapTransmittanceMeasurementDistanceMinMeters() * RtxOptions::getMeterToWorldUnitScale() };
          float fogRemapTransmittanceMeasurementDistanceMax { fogRemapTransmittanceMeasurementDistanceMaxMeters() * RtxOptions::getMeterToWorldUnitScale() };

          // Note: Ensure the mins and maxes are consistent with eachother.
          fogRemapMaxDistanceMax = std::max(fogRemapMaxDistanceMax, fogRemapMaxDistanceMin);
          fogRemapTransmittanceMeasurementDistanceMax = std::max(fogRemapTransmittanceMeasurementDistanceMax, fogRemapTransmittanceMeasurementDistanceMin);

          float const maxDistanceRange { fogRemapMaxDistanceMax - fogRemapMaxDistanceMin };
          float const transmittanceMeasurementDistanceRange { fogRemapTransmittanceMeasurementDistanceMax - fogRemapTransmittanceMeasurementDistanceMin };
          // Todo: Scene scale stuff ignored for now because scene scale stuff is not actually functioning properly. Add back in if it's ever fixed.
          // Note: Remap the end fog state distance into renderer units so that options can all be in renderer units (to be consistent with everything else).
          // float const normalizedRange{ (fogState.end * sceneScale() - fogRemapMaxDistanceMin) / maxDistanceRange };
          float const normalizedRange { (fogState.end - fogRemapMaxDistanceMin) / maxDistanceRange };

          transmittanceMeasurementDistance = normalizedRange * transmittanceMeasurementDistanceRange + fogRemapTransmittanceMeasurementDistanceMin;
        } else if (fogState.mode == D3DFOG_EXP || fogState.mode == D3DFOG_EXP2) {
          // Note: Derived using the following, doesn't take fog color into account but that is fine for a rough estimate:
          // density = -ln(color) / measurement_distance (For exp)
          // density^2 = -ln(color) / measurement_distance (For exp2)

          if (fogState.density != 0.0f) {
            float const transmittanceColorLuminance { sRGBLuminance(transmittanceColorLinear) };

            transmittanceMeasurementDistance = -log(transmittanceColorLuminance) / fogState.density;
            // Todo: Scene scale stuff ignored for now because scene scale stuff is not actually functioning properly. Add back in if it's ever fixed.
            // Note: Convert transmittance measurement distance into our engine's units (from game-specific world units due to being derived
            // from the D3D9 side of things). This in effect is the same as dividing the density by the scene scale.
            // transmittanceMeasurementDistance *= sceneScale();
          }
        }
      }

      // Add some "ambient" from the original fog as a constant term applied to fog during preintegration
      multiScatteringEstimate = fogState.color * fogRemapColorMultiscatteringScale();
    }

    // Calculate scattering and attenuation coefficients for the volume

    Vector3 const volumetricAttenuationCoefficient{
      -log(transmittanceColorLinear.x) / transmittanceMeasurementDistance,
      -log(transmittanceColorLinear.y) / transmittanceMeasurementDistance,
      -log(transmittanceColorLinear.z) / transmittanceMeasurementDistance
    };
    Vector3 const volumetricScatteringCoefficient{ volumetricAttenuationCoefficient * singleScatteringAlbedo() };

    const RtCamera& mainCamera = cameraManager.getMainCamera();

    // Set Volumetric Arguments

    VolumeArgs volumeArgs = { };

    volumeArgs.froxelGridDimensions.x = static_cast<uint>(m_froxelVolumeExtent.width);
    volumeArgs.froxelGridDimensions.y = static_cast<uint>(m_froxelVolumeExtent.height);
    volumeArgs.inverseFroxelGridDimensions.x = 1.0f / static_cast<float>(m_froxelVolumeExtent.width);
    volumeArgs.inverseFroxelGridDimensions.y = 1.0f / static_cast<float>(m_froxelVolumeExtent.height);

    volumeArgs.restirFroxelGridDimensions.x = static_cast<uint>(m_restirFroxelVolumeExtent.width);
    volumeArgs.restirFroxelGridDimensions.y = static_cast<uint>(m_restirFroxelVolumeExtent.height);
    volumeArgs.restirInverseFroxelGridDimensions.x = 1.0f / static_cast<float>(m_restirFroxelVolumeExtent.width);
    volumeArgs.restirInverseFroxelGridDimensions.y = 1.0f / static_cast<float>(m_restirFroxelVolumeExtent.height);

    volumeArgs.froxelDepthSlices = static_cast<uint16_t>(m_froxelVolumeExtent.depth);
    volumeArgs.restirFroxelDepthSlices = static_cast<uint16_t>(m_restirFroxelVolumeExtent.depth);

    volumeArgs.maxAccumulationFrames = static_cast<uint16_t>(maxAccumulationFrames());
    volumeArgs.froxelDepthSliceDistributionExponent = froxelDepthSliceDistributionExponent();
    volumeArgs.froxelMaxDistance = froxelMaxDistanceMeters() * RtxOptions::getMeterToWorldUnitScale();
    volumeArgs.froxelFireflyFilteringLuminanceThreshold = froxelFireflyFilteringLuminanceThreshold();
    volumeArgs.attenuationCoefficient = volumetricAttenuationCoefficient;
    volumeArgs.enable = enable() && canUsePhysicalFog;
    volumeArgs.scatteringCoefficient = volumetricScatteringCoefficient;
    volumeArgs.enableVolumeRISInitialVisibility = enableInitialVisibility();
    volumeArgs.enablevisibilityReuse = visibilityReuse();
    // Note: We need to invalidate the volumetric reservoir when detecting camera cut to avoid accumulating the history from different scenes
    volumeArgs.enableVolumeTemporalResampling = enableTemporalResampling() && !cameraManager.getMainCamera().isCameraCut();
    volumeArgs.enableVolumeSpatialResampling = enableSpatialResampling() && !cameraManager.getMainCamera().isCameraCut();
    volumeArgs.numSpatialSamples = spatialReuseMaxSampleCount();
    volumeArgs.spatialSamplingRadius = spatialReuseSamplingRadius();
    volumeArgs.numFroxelVolumes = m_numFroxelVolumes;     
    volumeArgs.numActiveFroxelVolumes = enablePortalVolumes ? m_numFroxelVolumes : 1;
    volumeArgs.inverseNumFroxelVolumes = 1.0f / static_cast<float>(m_numFroxelVolumes);
    // Note: Set to clamp to the center position (0.5) of the first and last froxel on the U axis to clamp to that value.
    volumeArgs.minFilteredRadianceU = 0.5f / static_cast<float>(m_froxelVolumeExtent.width);
    volumeArgs.maxFilteredRadianceU = 1.f - volumeArgs.minFilteredRadianceU;
    volumeArgs.multiScatteringEstimate = multiScatteringEstimate;
    volumeArgs.enableReferenceMode = enableReferenceMode();
    volumeArgs.volumetricFogAnisotropy = anisotropy();

    volumeArgs.enableNoiseFieldDensity = enableHeterogeneousFog();
    volumeArgs.noiseFieldSubStepSize = noiseFieldSubStepSizeMeters() * RtxOptions::getMeterToWorldUnitScale();
    volumeArgs.noiseFieldOctaves = noiseFieldOctaves();
    volumeArgs.noiseFieldTimeScale = noiseFieldTimeScale();
    volumeArgs.noiseFieldDensityScale = noiseFieldDensityScale();
    volumeArgs.noiseFieldDensityExponent = noiseFieldDensityExponent();
    volumeArgs.noiseFieldOctaves = noiseFieldOctaves();
    volumeArgs.noiseFieldInitialFrequency = noiseFieldInitialFrequencyPerMeter() / RtxOptions::getMeterToWorldUnitScale();
    volumeArgs.noiseFieldLacunarity = noiseFieldLacunarity();
    volumeArgs.noiseFieldGain = noiseFieldGain();

    volumeArgs.depthOffset = depthOffset();

    const float invertedWorld = atmosphereInverted() ? -1.f : 1.f;
    const Vector3 sceneUpDirection = RtxOptions::zUp() ? Vector3(0, 0, invertedWorld) : Vector3(0, invertedWorld, 0);

    const float atmosphereHeight = atmosphereHeightMeters() * RtxOptions::getMeterToWorldUnitScale();
    const float planetRadius = atmospherePlanetRadiusMeters() * RtxOptions::getMeterToWorldUnitScale();
    // Create a virtual planet center by projecting the camera position onto the plane defined by the origin and scene up direction.
    // Todo: Consider pre-transforming this planet center into the various volume camera translated world spaces to avoid needing to do this translation on the GPU constantly. May be just as costly however
    // to do an additional indexed lookup rather than a simple subtraction however, but depends on how well the compiler can optimize such things.
    const Vector3 planetCenter = project(mainCamera.getPosition(), Vector3(), sceneUpDirection) - sceneUpDirection * planetRadius;
    const float atmosphereRadius = atmosphereHeight + planetRadius;

    volumeArgs.enableAtmosphere = enableAtmosphere();
    volumeArgs.sceneUpDirection = sceneUpDirection;
    volumeArgs.atmosphereHeight = atmosphereHeight;
    volumeArgs.planetCenter = planetCenter;
    volumeArgs.atmosphereRadiusSquared = atmosphereRadius * atmosphereRadius;
    volumeArgs.maxAttenuationDistanceForNoAtmosphere = transmittanceMeasurementDistance * 5;

    volumeArgs.cameras[froxelVolumeMain] = mainCamera.getVolumeShaderConstants(volumeArgs.froxelMaxDistance);
    if (enablePortalVolumes) {
      volumeArgs.cameras[froxelVolumePortal0] = cameraManager.getCamera(CameraType::Portal0).getVolumeShaderConstants(volumeArgs.froxelMaxDistance);
      volumeArgs.cameras[froxelVolumePortal1] = cameraManager.getCamera(CameraType::Portal1).getVolumeShaderConstants(volumeArgs.froxelMaxDistance);
    }

    volumeArgs.restirCameras[froxelVolumeMain] = mainCamera.getVolumeShaderConstants(volumeArgs.froxelMaxDistance, restirGridGuardBandFactor());
    if (enablePortalVolumes) {
      volumeArgs.restirCameras[froxelVolumePortal0] = cameraManager.getCamera(CameraType::Portal0).getVolumeShaderConstants(volumeArgs.froxelMaxDistance, restirGridGuardBandFactor());
      volumeArgs.restirCameras[froxelVolumePortal1] = cameraManager.getCamera(CameraType::Portal1).getVolumeShaderConstants(volumeArgs.froxelMaxDistance, restirGridGuardBandFactor());
    }

    // Validate the froxel max distance against the camera
    // Note: This allows the user to be informed of if the froxel grid will be clipped against the far plane of the camera if the value is ever set too large for
    // some camera used for rendering (though hard to say if this is a problem as it may trigger on random strange cameras in some games).

    // Note: Camera should always be valid at this point as we rely on data from it, additionally this is checked
    // before ray tracing is even done.
    assert(mainCamera.isValid(m_device->getCurrentFrameId()));

    const float cameraFrustumMaxDistance = mainCamera.getFarPlane() - mainCamera.getNearPlane();

    if (volumeArgs.froxelMaxDistance > cameraFrustumMaxDistance) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Volume Froxel Max Distance set to ", volumeArgs.froxelMaxDistance, " but current camera frustum allows only a maximum of ", cameraFrustumMaxDistance)));
    }

    // Note: We need to invalidate the volumetric history buffers (radiance and age buffers) when detecting camera cut to avoid accumulating the history from different scenes
    volumeArgs.resetHistory = cameraManager.getMainCamera().isCameraCut();

    return volumeArgs;
  }

  void RtxGlobalVolumetrics::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput, uint32_t numActiveFroxelVolumes) {
    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view. Note this is fine here as the temporal reprojection lookups will ensure
    // their UVW coordinates are not out of the [0, 1] range before looking up the value.
    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_Y, getPreviousVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_Y, linearSampler);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_CO_CG, getPreviousVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_CO_CG, linearSampler);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_AGE, getPreviousVolumeAccumulatedRadianceAge().view, nullptr);
    ctx->bindResourceSampler(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT_AGE, linearSampler);

    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_Y, getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_CO_CG, getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT_AGE, getCurrentVolumeAccumulatedRadianceAge().view, nullptr);

    auto numRaysExtent = m_froxelVolumeExtent;
    numRaysExtent.width *= numActiveFroxelVolumes;

    auto numRestirCellsExtent = m_restirFroxelVolumeExtent;
    numRestirCellsExtent.width *= numActiveFroxelVolumes;

    // Compute restir
    {
      ScopedGpuProfileZone(ctx, "Volume Integrate Restir Initial");
      ctx->setFramePassStage(RtxFramePassStage::VolumeIntegrateRestirInitial);
      VkExtent3D workgroups = util::computeBlockCount(numRestirCellsExtent, VkExtent3D { 16, 8, 1 });

      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, getCurrentVolumeReservoirs().view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeRestirShaderInitial::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    if(visibilityReuse()) {
      ScopedGpuProfileZone(ctx, "Volume Integrate Restir Visible");
      ctx->setFramePassStage(RtxFramePassStage::VolumeIntegrateRestirVisible);
      VkExtent3D workgroups = util::computeBlockCount(numRestirCellsExtent, VkExtent3D { 16, 8, 1 });

      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, getCurrentVolumeReservoirs().view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeRestirShaderVisibility::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Volume Integrate Restir Temporal");
      ctx->setFramePassStage(RtxFramePassStage::VolumeIntegrateRestirTemporal);
      VkExtent3D workgroups = util::computeBlockCount(numRestirCellsExtent, VkExtent3D { 16, 8, 1 });

      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT, getPreviousVolumeReservoirs().view, nullptr);
      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, getCurrentVolumeReservoirs().view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeRestirShaderTemporal::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    {
      ScopedGpuProfileZone(ctx, "Volume Integrate Restir Spatial Resampling");
      ctx->setFramePassStage(RtxFramePassStage::VolumeIntegrateRestirSpatialResampling);
      VkExtent3D workgroups = util::computeBlockCount(numRestirCellsExtent, VkExtent3D { 16, 8, 1 });

      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT, getCurrentVolumeReservoirs().view, nullptr);
      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, getPreviousVolumeReservoirs().view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeRestirShaderSpatialResampling::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // Dispatch rays
    {
      ScopedGpuProfileZone(ctx, "Volume Integrate Raytracing");
      ctx->setFramePassStage(RtxFramePassStage::VolumeIntegrateRaytracing);
      VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 16, 8, 1 });

      ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, getPreviousVolumeReservoirs().view, nullptr);

      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeIntegrateShader::getShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }

    // Todo: Implement TraceRay path if needed some day, currently not though.
    /*
    switch (getRenderPassVolumeIntegrateRaytraceMode()) {
    case RaytraceMode::RayQuery:
      VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
      ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader());
      ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
      break;
    case RaytraceMode::RayQueryRayGen:
      ctx.bindRaytracingPipelineShaders(getPipelineShaders(true));
      ctx.traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    case RaytraceMode::TraceRay:
      ctx.bindRaytracingPipelineShaders(getPipelineShaders(false));
      ctx.traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    }
    */
  }

  DxvkRaytracingPipelineShaders RtxGlobalVolumetrics::getPipelineShaders(bool useRayQuery) const {
    DxvkRaytracingPipelineShaders shaders;
    // Todo: Implement TraceRay path if needed some day, currently not though.
    /*
    if (useRayQuery) {
      shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_rayquery_raygen));
      shaders.debugName = "Volume Integrate RayQuery (RGS)";
    } else {
      if (isShaderExecutionReorderingInVolumeIntegrateEnabled())
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_raygen_ser));
      else
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_raygen));
      shaders.addGeneralShader(VolumeIntegrateMissShader::getShader());

      ADD_HIT_GROUPS(VolumeIntegrateClosestHitShader, volume_integrate);

      shaders.debugName = "Volume Integrate TraceRay (RGS)";
    }
    */
    return shaders;
  }

  void RtxGlobalVolumetrics::onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) {
    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    m_swapTextures = !m_swapTextures;

    if (m_rebuildFroxels) {
      createDownscaledResource(ctx, frameBeginCtx.downscaledExtent);
      m_rebuildFroxels = false;
    }
  }

  void RtxGlobalVolumetrics::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    m_froxelVolumeExtent = util::computeBlockCount(downscaledExtent, VkExtent3D {
      froxelGridResolutionScale(),
      froxelGridResolutionScale(),
      1
    });
    m_froxelVolumeExtent.depth = froxelDepthSlices();
    m_numFroxelVolumes = enableInPortals() ? maxRayPortalCount + 1 : 1;

    VkExtent3D froxelGridFullDimensions = m_froxelVolumeExtent;
    // Note: preintegrated radiance is only computed for one (main) volume, not all of them

    froxelGridFullDimensions.width *= m_numFroxelVolumes;

    m_volumeAccumulatedRadianceY[0] = Resources::createImageResource(ctx, "volume accumulated radiance SH(Y) 0", froxelGridFullDimensions, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeAccumulatedRadianceY[1] = Resources::createImageResource(ctx, "volume accumulated radiance SH(Y) 1", froxelGridFullDimensions, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeAccumulatedRadianceCoCg[0] = Resources::createImageResource(ctx, "volume accumulated radiance (Co, Cg) 0", froxelGridFullDimensions, VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeAccumulatedRadianceCoCg[1] = Resources::createImageResource(ctx, "volume accumulated radiance (Co, Cg) 1", froxelGridFullDimensions, VK_FORMAT_R16G16_SFLOAT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeAccumulatedRadianceAge[0] = Resources::createImageResource(ctx, "volume accumulated radiance (Age) 0", froxelGridFullDimensions, VK_FORMAT_R8_UNORM, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeAccumulatedRadianceAge[1] = Resources::createImageResource(ctx, "volume accumulated radiance (Age) 1", froxelGridFullDimensions, VK_FORMAT_R8_UNORM, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);

    // Calculate the restir grid resolution
    m_restirFroxelVolumeExtent = util::computeBlockCount(m_froxelVolumeExtent, VkExtent3D { restirGridScale(), restirGridScale(), 1 });
    m_restirFroxelVolumeExtent.depth = restirFroxelDepthSlices();

    VkExtent3D restirFroxelGridFullDimensions = m_restirFroxelVolumeExtent;
    restirFroxelGridFullDimensions.width *= m_numFroxelVolumes;

    m_volumeReservoirs[0] = Resources::createImageResource(ctx, "volume reservoir 0", restirFroxelGridFullDimensions, VK_FORMAT_R32G32B32A32_UINT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
    m_volumeReservoirs[1] = Resources::createImageResource(ctx, "volume reservoir 1", restirFroxelGridFullDimensions, VK_FORMAT_R32G32B32A32_UINT, 1, VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D);
  }

  void RtxGlobalVolumetrics::releaseDownscaledResource() {
    for (uint32_t i = 0; i < 2; i++) {
      m_volumeAccumulatedRadianceY[i].reset();
      m_volumeAccumulatedRadianceCoCg[i].reset();
      m_volumeAccumulatedRadianceAge[i].reset();
      m_volumeReservoirs[i].reset();
    }
  }

  bool RtxGlobalVolumetrics::isEnabled() const {
    return true;
  }
}
