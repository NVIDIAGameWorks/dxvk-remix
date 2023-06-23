/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include <cassert>
#include <cmath>

#include "rtx_light_manager.h"
#include "rtx_context.h"
#include "rtx_options.h"

#include "../util/util_color.h"
#include "../d3d9/d3d9_state.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"

namespace dxvk {

  VolumeManager::VolumeManager(DxvkDevice* device)
    : CommonDeviceObject(device) {
  }

  VolumeManager::~VolumeManager() {
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

  VolumeArgs VolumeManager::getVolumeArgs(CameraManager const& cameraManager, VkExtent3D froxelGridDimensions, uint32_t numFroxelVolumes, FogState const& fogState, bool enablePortalVolumes) const {
    // Calculate the volumetric parameters from options and the fixed function fog state

    // Note: Volumetric transmittance color option is in gamma space, so must be converted to linear for usage in the volumetric system.
    Vector3 volumetricTransmittanceColor{ sRGBGammaToLinear(RtxOptions::Get()->getVolumetricTransmittanceColor()) };
    // Note: Fall back to usual default in cases such as the "none" D3D fog mode, no fog remapping specified, or invalid values in the fog mode derivation
    // (such as dividing by zero).
    float volumetricTransmittanceMeasurementDistance = RtxOptions::Get()->getVolumetricTransmittanceMeasurementDistance();
    Vector3 multiScatteringEstimate = Vector3();

    // Todo: Make this configurable in the future as this threshold was created specifically for Portal RTX's underwater fixed function fog.
    constexpr float waterFogDensityThrehold = 0.065f;
    const bool canUsePhysicalFog = shouldConvertToPhysicalFog(fogState, waterFogDensityThrehold);

    if (
      RtxOptions::Get()->enableFogRemap() &&
      // Note: Only consider remapping fog if any fixed function fog is actually enabled (not the "none" mode).
      fogState.mode != D3DFOG_NONE &&
      canUsePhysicalFog
    ) {
      // Handle Fog Color remapping
      // Note: This must happen first as max distance remapping will depend on the luminance derived from the color determined here.

      if (RtxOptions::Get()->enableFogColorRemap()) {
        // Note: Legacy fixed function fog color is in gamma space as all the rendering in old games was typically in gamma space, same assumption we make
        // for textures/lights.
        volumetricTransmittanceColor = sRGBGammaToLinear(fogState.color);
      }

      // Handle Fog Max Distance remapping

      if (RtxOptions::Get()->enableFogMaxDistanceRemap()) {
        // Switch transmittance measurement distance derivation from D3D9 fog based on which fog mode is in use

        if (fogState.mode == D3DFOG_LINEAR) {
          float fogRemapMaxDistanceMin { RtxOptions::Get()->getFogRemapMaxDistanceMin() };
          float fogRemapMaxDistanceMax { RtxOptions::Get()->getFogRemapMaxDistanceMax() };
          float fogRemapTransmittanceMeasurementDistanceMin { RtxOptions::Get()->getFogRemapTransmittanceMeasurementDistanceMin() };
          float fogRemapTransmittanceMeasurementDistanceMax { RtxOptions::Get()->getFogRemapTransmittanceMeasurementDistanceMax() };

          // Note: Ensure the mins and maxes are consistent with eachother.
          fogRemapMaxDistanceMax = std::max(fogRemapMaxDistanceMax, fogRemapMaxDistanceMin);
          fogRemapTransmittanceMeasurementDistanceMax = std::max(fogRemapTransmittanceMeasurementDistanceMax, fogRemapTransmittanceMeasurementDistanceMin);

          float const maxDistanceRange{ fogRemapMaxDistanceMax - fogRemapMaxDistanceMin };
          float const transmittanceMeasurementDistanceRange{ fogRemapTransmittanceMeasurementDistanceMax - fogRemapTransmittanceMeasurementDistanceMin };
          // Todo: Scene scale stuff ignored for now because scene scale stuff is not actually functioning properly. Add back in if it's ever fixed.
          // Note: Remap the end fog state distance into renderer units so that options can all be in renderer units (to be consistent with everything else).
          // float const normalizedRange{ (fogState.end * RtxOptions::Get()->getSceneScale() - fogRemapMaxDistanceMin) / maxDistanceRange };
          float const normalizedRange{ (fogState.end - fogRemapMaxDistanceMin) / maxDistanceRange };

          volumetricTransmittanceMeasurementDistance = normalizedRange * transmittanceMeasurementDistanceRange + fogRemapTransmittanceMeasurementDistanceMin;
        } else if (fogState.mode == D3DFOG_EXP || fogState.mode == D3DFOG_EXP2) {
          // Note: Derived using the following, doesn't take fog color into account but that is fine for a rough estimate:
          // density = -ln(color) / measurement_distance (For exp)
          // density^2 = -ln(color) / measurement_distance (For exp2)

          if (fogState.density != 0.0f) {
            float const transmittanceColorLuminance{ sRGBLuminance(volumetricTransmittanceColor) };

            volumetricTransmittanceMeasurementDistance = -log(transmittanceColorLuminance) / fogState.density;
            // Todo: Scene scale stuff ignored for now because scene scale stuff is not actually functioning properly. Add back in if it's ever fixed.
            // Note: Convert transmittance measurement distance into our engine's units (from game-specific world units due to being derived
            // from the D3D9 side of things). This in effect is the same as dividing the density by the scene scale.
            // volumetricTransmittanceMeasurementDistance *= RtxOptions::Get()->getSceneScale();
          }
        }
      }

      // Add some "ambient" from the original fog as a constant term applied to fog during preintegration
      multiScatteringEstimate = fogState.color * RtxOptions::Get()->fogRemapColorMultiscatteringScale();
    }

    // Calculate scattering and attenuation coefficients for the volume

    Vector3 const volumetricAttenuationCoefficient{
      -log(volumetricTransmittanceColor.x) / volumetricTransmittanceMeasurementDistance,
      -log(volumetricTransmittanceColor.y) / volumetricTransmittanceMeasurementDistance,
      -log(volumetricTransmittanceColor.z) / volumetricTransmittanceMeasurementDistance
    };
    Vector3 const volumetricScatteringCoefficient{ volumetricAttenuationCoefficient * RtxOptions::Get()->getVolumetricSingleScatteringAlbedo() };

    const RtCamera& mainCamera = cameraManager.getMainCamera();

    // Set Volumetric Arguments

    VolumeArgs volumeArgs = { };
    volumeArgs.froxelGridDimensions.x = static_cast<uint>(froxelGridDimensions.width);
    volumeArgs.froxelGridDimensions.y = static_cast<uint>(froxelGridDimensions.height);
    volumeArgs.inverseFroxelGridDimensions.x = 1.0f / static_cast<float>(froxelGridDimensions.width);
    volumeArgs.inverseFroxelGridDimensions.y = 1.0f / static_cast<float>(froxelGridDimensions.height);
    volumeArgs.froxelDepthSlices = static_cast<uint16_t>(froxelGridDimensions.depth);
    volumeArgs.maxAccumulationFrames = static_cast<uint16_t>(RtxOptions::Get()->getMaxAccumulationFrames());
    volumeArgs.froxelDepthSliceDistributionExponent = RtxOptions::Get()->getFroxelDepthSliceDistributionExponent();
    volumeArgs.froxelMaxDistance = RtxOptions::Get()->getFroxelMaxDistance();
    volumeArgs.froxelFireflyFilteringLuminanceThreshold = RtxOptions::Get()->getFroxelFireflyFilteringLuminanceThreshold();
    volumeArgs.froxelFilterGaussianSigma = RtxOptions::Get()->getFroxelFilterGaussianSigma();
    volumeArgs.attenuationCoefficient = volumetricAttenuationCoefficient;
    volumeArgs.enableVolumetricLighting = RtxOptions::Get()->isVolumetricLightingEnabled() && canUsePhysicalFog;
    volumeArgs.scatteringCoefficient = volumetricScatteringCoefficient;
    volumeArgs.minReservoirSamples = RtxOptions::Get()->getFroxelMinReservoirSamples();
    volumeArgs.maxReservoirSamples = RtxOptions::Get()->getFroxelMaxReservoirSamples();
    volumeArgs.minKernelRadius = RtxOptions::Get()->getFroxelMinKernelRadius();
    volumeArgs.maxKernelRadius = RtxOptions::Get()->getFroxelMaxKernelRadius();
    volumeArgs.minReservoirSamplesStabilityHistory = RtxOptions::Get()->getFroxelMinReservoirSamplesStabilityHistory();
    volumeArgs.reservoirSamplesStabilityHistoryRange = RtxOptions::Get()->getFroxelReservoirSamplesStabilityHistoryRange();
    volumeArgs.minKernelRadiusStabilityHistory = RtxOptions::Get()->getFroxelMinKernelRadiusStabilityHistory();
    volumeArgs.kernelRadiusStabilityHistoryRange = RtxOptions::Get()->getFroxelKernelRadiusStabilityHistoryRange();
    volumeArgs.reservoirSamplesStabilityHistoryPower = RtxOptions::Get()->getFroxelReservoirSamplesStabilityHistoryPower();
    volumeArgs.kernelRadiusStabilityHistoryPower = RtxOptions::Get()->getFroxelKernelRadiusStabilityHistoryPower();
    volumeArgs.enableVolumeRISInitialVisibility = RtxOptions::Get()->isVolumetricEnableInitialVisibilityEnabled();
    volumeArgs.enableVolumeTemporalResampling = RtxOptions::Get()->isVolumetricEnableTemporalResamplingEnabled();
    volumeArgs.numFroxelVolumes = numFroxelVolumes;
    volumeArgs.numActiveFroxelVolumes = enablePortalVolumes ? numFroxelVolumes : 1;
    volumeArgs.inverseNumFroxelVolumes = 1.0f / static_cast<float>(numFroxelVolumes);
    // Note: Set to clamp to the center position (0.5) of the first and last froxel on the U axis to clamp to that value.
    volumeArgs.minFilteredRadianceU = 0.5f / static_cast<float>(froxelGridDimensions.width);
    volumeArgs.maxFilteredRadianceU = 1.f - volumeArgs.minFilteredRadianceU;
    volumeArgs.multiScatteringEstimate = multiScatteringEstimate;

    volumeArgs.cameras[froxelVolumeMain] = mainCamera.getVolumeShaderConstants();
    if (enablePortalVolumes) {
      volumeArgs.cameras[froxelVolumePortal0] = cameraManager.getCamera(CameraType::Portal0).getVolumeShaderConstants();
      volumeArgs.cameras[froxelVolumePortal1] = cameraManager.getCamera(CameraType::Portal1).getVolumeShaderConstants();
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

    return volumeArgs;
  }

}  // namespace dxvk
