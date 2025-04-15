/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_nrd_settings.h"
#include "dxvk_device.h"
#include "rtx_imgui.h"
#include "rtx_options.h"

namespace dxvk {
  auto denoiserCombo = ImGui::ComboWithKey<nrd::Denoiser>(
    "Denoiser", {
        {nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR, "ReBLUR"},
        {nrd::Denoiser::RELAX_DIFFUSE_SPECULAR, "ReLAX"},
        {nrd::Denoiser::REFERENCE, "Reference"},
    });

  auto reblurSettingsPresetCombo = 
    ImGui::ComboWithKey<NrdSettings::ReblurSettingsPreset>(
      "Preset", {
          {NrdSettings::ReblurSettingsPreset::Default, "Default"},
          {NrdSettings::ReblurSettingsPreset::Finetuned, "Finetuned"},
      } );

  auto relaxSettingsPresetCombo =
    ImGui::ComboWithKey<NrdSettings::RelaxSettingsPreset>(
      "Preset", {
          {NrdSettings::RelaxSettingsPreset::Default, "Default"},
          {NrdSettings::RelaxSettingsPreset::Finetuned, "Finetuned (More Stable)"},
      });

  auto reblurHitTReconstructionModeCombo =
    ImGui::ComboWithKey<nrd::HitDistanceReconstructionMode>(
      "Hit T Reconstruction Mode", {
          {nrd::HitDistanceReconstructionMode::OFF, "Off"},
          {nrd::HitDistanceReconstructionMode::AREA_3X3, "Area 3x3"},
          {nrd::HitDistanceReconstructionMode::AREA_5X5, "Area 5x5"},
      });

  auto relaxHitTReconstructionModeCombo =
    ImGui::ComboWithKey<nrd::HitDistanceReconstructionMode>(
      "Hit T Reconstruction Mode", {
          {nrd::HitDistanceReconstructionMode::OFF, "Off"},
          {nrd::HitDistanceReconstructionMode::AREA_3X3, "Area 3x3"},
          {nrd::HitDistanceReconstructionMode::AREA_5X5, "Area 5x5"},
      });

  void setReblurPresetSettings(nrd::ReblurSettings& reblurSettings, NrdSettings::ReblurSettingsPreset preset, dxvk::DenoiserType type) {
    reblurSettings = nrd::ReblurSettings();
    switch (preset) {
      case NrdSettings::ReblurSettingsPreset::Finetuned: {
        reblurSettings.maxAccumulatedFrameNum = 32;
        reblurSettings.maxFastAccumulatedFrameNum = 2;
        reblurSettings.diffusePrepassBlurRadius = reblurSettings.specularPrepassBlurRadius = 50.0f;
        reblurSettings.enableAntiFirefly = true;
        reblurSettings.maxBlurRadius = 60;
        reblurSettings.lobeAngleFraction = 0.15f;
        reblurSettings.roughnessFraction = 0.15f;
        reblurSettings.hitDistanceParameters.A = 20;
        if (type == dxvk::DenoiserType::DirectLight) {
          reblurSettings.maxFastAccumulatedFrameNum = 1;
          reblurSettings.diffusePrepassBlurRadius = reblurSettings.specularPrepassBlurRadius = 0.0f;
          reblurSettings.maxBlurRadius = 15.0f;
        }
        break;
      }
    }
    reblurSettings.hitDistanceParameters.A *= RtxOptions::Get()->getMeterToWorldUnitScale();
    if (type != dxvk::DenoiserType::DirectLight) {
      reblurSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
    }
  }

  void setRelaxPresetSettings(nrd::RelaxSettings& relaxSettings, NrdSettings::RelaxSettingsPreset preset, dxvk::DenoiserType type) {
    relaxSettings = nrd::RelaxSettings();
    switch (preset) {
      case NrdSettings::RelaxSettingsPreset::Finetuned: {
        // The following two values need to be the same, or checkerboard from sparse bi-lateral filter will show through.
        // Bumped a little trading stronger and longer blur for less fireflies on disocclusions
        relaxSettings.spatialVarianceEstimationHistoryThreshold = 2;
        relaxSettings.historyFixFrameNum = 2;

        // Anti-firefly just kills all the energy when using probabilistic sampling, but its required currently.
        relaxSettings.enableAntiFirefly = true;

        // Confidence settings,
        // 0.55 for confidenceDrivenRelaxationMultiplier will make noise intense
        //
        // Also increasing the normal edge stopping to 0.6 to relax neighbor pixel normal weight.
        // This will increase the specular lobe angle and be less stict to reject samples which
        // view vector is outside the specular lobe.
        // This will slightly blur the dynamic signal more, but we can stil keep the sharpness.
        //
        // Decreasing confidenceDrivenLuminanceEdgeStoppingRelaxation to 1.4, this can tight the relaxation
        // of luminance of neighbor samples, gives them rather lower weight when blending with center.
        // Should be careful not make thie value too low, or the edge stopping function will be sharp and not
        // effectively smooth the image, which will bring the noise back
        relaxSettings.confidenceDrivenRelaxationMultiplier = 0.7f;
        relaxSettings.confidenceDrivenLuminanceEdgeStoppingRelaxation = 1.4f;
        relaxSettings.confidenceDrivenNormalEdgeStoppingRelaxation = 0.6f;

        if (type == dxvk::DenoiserType::DirectLight) {
          relaxSettings.atrousIterationNum = 5;

          // The general idea is keep the history length as low as possible, for the sample quality, to reduce blurring
          relaxSettings.diffuseMaxAccumulatedFrameNum = 26;
          relaxSettings.diffuseMaxFastAccumulatedFrameNum = 1;
          relaxSettings.specularMaxAccumulatedFrameNum = 26;
          relaxSettings.specularMaxFastAccumulatedFrameNum = 1; // Set it to 1 to reduce specular trail

          // Lower numbers preserve detail
          relaxSettings.diffusePhiLuminance = 0.4f;
          relaxSettings.specularPhiLuminance = 1.5f;

          // Sharpen contact shadows by weakening preblur, RTXDI signal is well behaved and feeding in a bit of noise into DLSS
          // will sharpen/resolve it closer to reference
          relaxSettings.minHitDistanceWeight = 0.001f;
          relaxSettings.diffusePrepassBlurRadius = 0.f;

          relaxSettings.historyFixEdgeStoppingNormalPower = 8.0f;

          // Lower numbers more accurately represent the original material data (but the sampling quality needs to be good)
          relaxSettings.specularLobeAngleSlack = 0.3f;
          relaxSettings.lobeAngleFraction = .45f;
          relaxSettings.roughnessFraction = .15f;
          relaxSettings.luminanceEdgeStoppingRelaxation = .5f;
          relaxSettings.normalEdgeStoppingRelaxation = .3f;
          relaxSettings.roughnessEdgeStoppingRelaxation = .3f;
        } else {
          relaxSettings.enableAntiFirefly = true;
          // Pretty standard, need (at least) 5 to reduce boiling
          relaxSettings.atrousIterationNum = 5;

          // Indirect samples need more history, or they just wont resolve (ideally we could improve the sample quality here, as even this isn't enough in all cases)
          relaxSettings.diffuseMaxAccumulatedFrameNum = 64;
          relaxSettings.diffuseMaxFastAccumulatedFrameNum = 3;
          relaxSettings.specularMaxAccumulatedFrameNum = 64;
          // Keep this low to limit specular trail, but with anti-firefly being more aggresive in NRD 4.11.3
          // we bump it up to NRD's default to allow more samples in the specular signal.
          // This way recover some of muted highlights due to the anti-firefly
          relaxSettings.specularMaxFastAccumulatedFrameNum = 6;

          // Need a large blur radius, since the noise is extremely bad.
          relaxSettings.diffusePrepassBlurRadius = 50.f;
          relaxSettings.specularPrepassBlurRadius = 50.f;

          // Relaxing the normal constraint here, since the samples arent high quality enough for indirect - need to bleed over the normals
          relaxSettings.historyFixEdgeStoppingNormalPower = 8.0f;

          // Generally higher numbers here, since indirect samples are much lower quality...
          relaxSettings.diffusePhiLuminance = 1.0f;
          relaxSettings.specularPhiLuminance = 1.0f;
          relaxSettings.specularLobeAngleSlack = 0.15f;
          relaxSettings.historyClampingColorBoxSigmaScale = 3.f;
          relaxSettings.lobeAngleFraction = .5f;  // Reduced to 0.5 to make specular highlights more defined
          relaxSettings.roughnessFraction = .15f;
          relaxSettings.luminanceEdgeStoppingRelaxation = .65f;
          relaxSettings.normalEdgeStoppingRelaxation = .8f;
          relaxSettings.roughnessEdgeStoppingRelaxation = .5f;
          relaxSettings.diffuseMinLuminanceWeight = .05f;
        }
        break;
      }
    }
    if (type != dxvk::DenoiserType::DirectLight) {
      relaxSettings.hitDistanceReconstructionMode = nrd::HitDistanceReconstructionMode::AREA_3X3;
    }
  }

  void setCommonPresetSettings(nrd::CommonSettings& common, dxvk::DenoiserType type) {
    static_assert(nrd::CommonSettings{}.denoisingRange == 500000.0f, "NRD's default settings has changed, denoisingRange must be re-evaluated");
    constexpr float denoisingRangeLimit = nrd::CommonSettings{}.denoisingRange;

    // Note: Chosen as values outside the denoising range are used to indicate misses to the denoiser to save on performance when the denoiser is not needed.
    // Note: NRDSettings.h states that the max value of 'denoisingRange' is 524031,
    // which is calculated as (NRD_FP16_MAX / NRD_FP16_VIEWZ_SCALE - 1) = (65504.0 / 0.125 - 1)
    // to fit into float16 value range. And because of NRD_FP16_VIEWZ_SCALE=0.125, NRD allows to have values >65504.0,
    // which is needed in games that have far geometry and have 1 unit as a small quantity (e.g. 1 cm).
    // In such games, having it less than 65504.0 may involve visual artifacts (like a complete lack of lighting in the distance).
    // So because of that (and since it's a default value of 'CommonSettings::denoisingRange'), value of 500000 was chosen.
    common.denoisingRange = denoisingRangeLimit;

    if (type == dxvk::DenoiserType::Secondaries) {
      // Relax this substantially for secondaries, to improve quality of curved glass
      common.disocclusionThreshold = 0.1f;
    } else {
      // Separate disocclusion threshold for transmission through curved glass
      common.disocclusionThresholdAlternate = 0.1f;
    }
  }

  void NrdSettings::initialize(const nrd::LibraryDesc& libraryDesc, const dxvk::Config& config, DenoiserType type) {
    m_libraryDesc = libraryDesc;
    m_type = type;

    switch (type) {
    case DenoiserType::Reference:
      m_denoiserDesc.denoiser = nrd::Denoiser::REFERENCE;
      break;
    case DenoiserType::Secondaries:
    case DenoiserType::DirectAndIndirectLight:
      m_denoiserDesc.denoiser = denoiserMode();
      m_adaptiveAccumulationLengthMs = 500.f;
      break;
    case DenoiserType::DirectLight:
      m_denoiserDesc.denoiser = denoiserMode();
      m_reblurSettingsPreset = ReblurSettingsPreset::Default;
      // Note: use a faster accumulation setting for direct light because we want shadows to change quicker.
      // This is mostly needed for sharp shadows of the player model.
      m_adaptiveAccumulationLengthMs = 250.f;
      break;
    case DenoiserType::IndirectLight:
      m_denoiserDesc.denoiser = denoiserIndirectMode();
      m_adaptiveAccumulationLengthMs = 450.f;
      break;
    }

    switch (m_denoiserDesc.denoiser) {
    case nrd::Denoiser::REFERENCE:
    case nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR:
    case nrd::Denoiser::RELAX_DIFFUSE_SPECULAR:
      break;
    default:
      assert(0 && "Invalid denoiser mode");
      m_denoiserDesc.denoiser = sDefaultDenoiser;
      break;
    }

    if (getTimeDeltaBetweenFrames() > 0) {
      m_groupedSettings.timeDeltaBetweenFrames = getTimeDeltaBetweenFrames();
    }
    m_groupedSettings.timeDeltaBetweenFrames = std::max(0.f, m_groupedSettings.timeDeltaBetweenFrames);

    if (maxDirectHitTContribution() > 0) {
      m_groupedSettings.maxDirectHitTContribution = maxDirectHitTContribution();
    }
    m_groupedSettings.maxDirectHitTContribution = std::clamp(m_groupedSettings.maxDirectHitTContribution, 0.f, 1.f);
    
    // High number for it to keep accumulating forever
    // Using int max since it's controlled by imgui's int typed widget
    m_referenceSettings.maxAccumulatedFrameNum = INT32_MAX; 

    setCommonPresetSettings(m_commonSettings, m_type);
    setReblurPresetSettings(m_reblurSettings, m_reblurSettingsPreset, m_type);
    setRelaxPresetSettings(m_relaxSettings, m_relaxSettingsPreset, m_type);

    m_reblurInternalBlurRadius.maxBlurRadius = m_reblurSettings.maxBlurRadius;
    m_reblurInternalBlurRadius.diffusePrepassBlurRadius = m_reblurSettings.diffusePrepassBlurRadius;
    m_reblurInternalBlurRadius.specularPrepassBlurRadius = m_reblurSettings.specularPrepassBlurRadius;

    m_relaxInternalBlurRadius.diffusePrepassBlurRadius = m_relaxSettings.diffusePrepassBlurRadius;
    m_relaxInternalBlurRadius.specularPrepassBlurRadius = m_relaxSettings.specularPrepassBlurRadius;
  }

  void NrdSettings::showImguiSettings() {

    const ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    const ImGuiTreeNodeFlags collapsingHeaderFlagsOpen = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_CollapsingHeader;
    const ImGuiTreeNodeFlags collapsingHeaderFlagsClosed = ImGuiTreeNodeFlags_CollapsingHeader;

    // New settings
    {
      ImGui::Separator();
      ImGui::Text("NRD v%u.%u.%u", m_libraryDesc.versionMajor, m_libraryDesc.versionMinor, m_libraryDesc.versionBuild);
      ImGui::PushItemWidth(160.f);
    }

    if (m_type != DenoiserType::Reference) {
      denoiserCombo.getKey(&m_denoiserDesc.denoiser);
    }

    m_resetHistory |= ImGui::Button("Reset History");
    const bool resetHistoryOnSettingsChange = RtxOptions::Get()->isResetDenoiserHistoryOnSettingsChangeEnabled();

#define ADVANCED if (m_showAdvancedSettings)
    ImGui::Checkbox("Advanced Settings", &m_showAdvancedSettings);
    SettingsImpactingDenoiserOutput prevGroupedSettings = m_groupedSettings;

    
    if (m_type != DenoiserType::DirectLight && ImGui::CollapsingHeader("Integrator Settings", collapsingHeaderFlagsClosed)) {
      ImGui::Indent();

      if (m_type == DenoiserType::DirectAndIndirectLight && ImGui::CollapsingHeader("Diffuse", collapsingHeaderFlagsClosed)) {
        ImGui::Indent();
        ImGui::PushID("Diffuse");
        ImGui::SliderFloat("Max Direct HitT %", &m_groupedSettings.maxDirectHitTContribution, 0.0f, 1.0f);
        ImGui::PopID();
        ImGui::Unindent();
      }

      // Note: Add Specular NRD settings here if any are to be modified via the GUI.

      ImGui::Unindent();

      if (resetHistoryOnSettingsChange && memcmp(&m_groupedSettings, &prevGroupedSettings, sizeof(m_groupedSettings)) != 0) {
        m_resetHistory = true;
      }
    }

    if (m_denoiserDesc.denoiser != nrd::Denoiser::REFERENCE && ImGui::CollapsingHeader("Common Settings", collapsingHeaderFlagsClosed)) {
      ImGui::Indent();

      // Note: Set to match the range limit checked in rtx_nrd_context.cpp
      static_assert(nrd::CommonSettings{}.denoisingRange == 500000.0f, "NRD's default settings has changed, denoisingRange must be re-evaluated");
      constexpr float denoisingRangeLimit = nrd::CommonSettings{}.denoisingRange;

      ImGui::Checkbox("Validation Layer", &m_commonSettings.enableValidation);

      bool settingsChanged = false;

      settingsChanged |= ImGui::DragFloat("Frame Time Delta [ms]", &m_groupedSettings.timeDeltaBetweenFrames, 0.1f, 0.f, 1000.f, "%.1f", sliderFlags);
      // Note: the space after "Debug" in the widget name is intentional. "Debug" imgui widget 
      // triggers a different code path in imgui resulting in asserts. Because reasons...
      ImGui::DragFloat("Debug ", &m_commonSettings.debug, 0.001f, 0.0f, 1.f, "%.3f", sliderFlags);
      settingsChanged |= ImGui::DragFloat("Denoising Range", &m_commonSettings.denoisingRange, 100.f, 0.0f, denoisingRangeLimit, "%.1f", sliderFlags);
      settingsChanged |= ImGui::DragFloat("Disocclusion Threshold", &m_commonSettings.disocclusionThreshold, 0.01f, 0.0f, 1.f, "%.3f", sliderFlags);
      if (m_type != DenoiserType::Secondaries)
        settingsChanged |= ImGui::DragFloat("Disocclusion Threshold Alt.", &m_commonSettings.disocclusionThresholdAlternate, 0.01f, 0.0f, 1.f, "%.3f", sliderFlags);
      ImGui::DragFloat("Split screen: Noisy | Denoised Output", &m_commonSettings.splitScreen, 0.001f, 0.0f, 1.f, "%.3f", sliderFlags);

      if (resetHistoryOnSettingsChange && settingsChanged)
        m_resetHistory = true;

      ImGui::Unindent();
    }


    // Reference
    if (m_denoiserDesc.denoiser == nrd::Denoiser::REFERENCE) {
      if (ImGui::CollapsingHeader("Reference Settings", collapsingHeaderFlagsClosed)) {
        ImGui::Indent();
        ImGui::InputInt("Max Frames To Accumulate", &m_referenceSettings.maxAccumulatedFrameNum);
        ImGui::Unindent();
      }
    }

    // Reblur
    if (m_denoiserDesc.denoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) {
      if (ImGui::CollapsingHeader("Reblur Settings", collapsingHeaderFlagsClosed)) {
        ImGui::Indent();

        nrd::ReblurSettings prevReblurSettingsState;
        if (resetHistoryOnSettingsChange)
          prevReblurSettingsState = m_reblurSettings;

        auto prevReblurSettingsPreset = m_reblurSettingsPreset;
        reblurSettingsPresetCombo.getKey(&m_reblurSettingsPreset);
        if (m_reblurSettingsPreset != prevReblurSettingsPreset)
          setReblurPresetSettings(m_reblurSettings, m_reblurSettingsPreset, m_type);

        {
          if (!RtxOptions::Get()->adaptiveAccumulation()) {
            ImGui::SliderInt("History length [frames]", &m_reblurSettings.maxAccumulatedFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
          }
          else {
            ImGui::SliderFloat("History length [ms]", &m_adaptiveAccumulationLengthMs, 10.f, 1000.f, "%.1f");
            ImGui::SliderInt("Min history length [ms]", &m_adaptiveMinAccumulatedFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
          }
          ImGui::Checkbox("Anti-firefly", &m_reblurSettings.enableAntiFirefly);
          ImGui::SameLine();
          ImGui::Checkbox("Performance mode", &m_reblurSettings.enablePerformanceMode);
          ImGui::SameLine();
          reblurHitTReconstructionModeCombo.getKey(&m_reblurSettings.hitDistanceReconstructionMode);

          ADVANCED ImGui::SliderFloat("Hit distance parameters A", &m_reblurSettings.hitDistanceParameters.A, 0.0f, 10000.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Hit distance parameters B", &m_reblurSettings.hitDistanceParameters.B, 0.0f, 10.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Hit distance parameters C", &m_reblurSettings.hitDistanceParameters.C, 1.0f, 100.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Hit distance parameters D", &m_reblurSettings.hitDistanceParameters.D, -100.0f, 0.0f, "%.2f");

          ImGui::Text("PRE-PASS:");
          const float maxBlurRadius = RtxOptions::Get()->isAdaptiveResolutionDenoisingEnabled() ? 200.0f : 100.0f;
          ImGui::SliderFloat("Diffuse preblur radius", &m_reblurInternalBlurRadius.diffusePrepassBlurRadius, 0.0f, maxBlurRadius, "%.1f");
          ImGui::SliderFloat("Specular preblur radius", &m_reblurInternalBlurRadius.specularPrepassBlurRadius, 0.0f, maxBlurRadius, "%.1f");

          ImGui::Text("SPATIAL FILTERING:");
          ImGui::SliderFloat("Max blur radius [pixels]", &m_reblurInternalBlurRadius.maxBlurRadius, 0.0f, RtxOptions::Get()->isAdaptiveResolutionDenoisingEnabled() ? 120.0f : 60.0f, "%.1f");

          ImGui::SliderInt("History fix frame Number", &m_reblurSettings.historyFixFrameNum, 0, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
          ImGui::SliderFloat("Min blur radius [pixels]", &m_reblurSettings.minBlurRadius, 0.0f, maxBlurRadius, "%.1f");
          ImGui::SliderFloat("Max blur radius [pixels]", &m_reblurSettings.maxBlurRadius, 0.0f, maxBlurRadius, "%.1f");
          ImGui::SliderFloat("Lobe angle fraction [normalized %]", &m_reblurSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Roughness fraction [normalized %]", &m_reblurSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Responsive accumulation roughness threshold", &m_reblurSettings.responsiveAccumulationRoughnessThreshold, 0.0f, 1.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Plane distance sensitivity [normalized %]", &m_reblurSettings.planeDistanceSensitivity, 0.0f, 1.0f, "%.2f");
          ADVANCED ImGui::SliderFloat2("Specular probability threshold for mvec modification", m_reblurSettings.specularProbabilityThresholdsForMvModification, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Firefly suppressor min relative scale", &m_reblurSettings.fireflySuppressorMinRelativeScale, 1.0f, 3.0f, "%.2f");
          ADVANCED ImGui::Checkbox("Enable Prepass Only For Specular Motion Estimation", &m_reblurSettings.usePrepassOnlyForSpecularMotionEstimation);
          
          ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.6f);

          ImGui::Text("ANTI-LAG:");
          ADVANCED ImGui::SliderFloat("Luminance sigma scale", &m_reblurSettings.antilagSettings.luminanceSigmaScale, 0.0f, 10.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Luminance sensitivity to darkness", &m_reblurSettings.antilagSettings.luminanceSensitivity, 0.0f, 100.0f, "%.2f");

          ADVANCED ImGui::SliderFloat("Hit distance sigma scale", &m_reblurSettings.antilagSettings.hitDistanceSigmaScale, 0.0f, 10.0f, "%.2f");
          ADVANCED ImGui::SliderFloat("Hit distance sensitivity to darkness", &m_reblurSettings.antilagSettings.hitDistanceSensitivity, 0.0f, 100.0f, "%.2f");
        }

        if (resetHistoryOnSettingsChange && memcmp(&m_reblurSettings, &prevReblurSettingsState, sizeof(m_reblurSettings)) != 0)
          m_resetHistory = true;

        ImGui::Unindent();
      }
    }

    if (m_denoiserDesc.denoiser == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR) {
      if (ImGui::CollapsingHeader("ReLAX Settings", collapsingHeaderFlagsClosed)) {
        ImGui::Indent();

        nrd::RelaxSettings prevRelaxSettingsState;
        if (resetHistoryOnSettingsChange)
          prevRelaxSettingsState = m_relaxSettings;

        auto prevRelaxSettingsPreset = m_relaxSettingsPreset;
        relaxSettingsPresetCombo.getKey(&m_relaxSettingsPreset);
        if (m_relaxSettingsPreset != prevRelaxSettingsPreset)
          setRelaxPresetSettings(m_relaxSettings, m_relaxSettingsPreset, m_type);

        {
          if (!RtxOptions::Get()->adaptiveAccumulation()) {
            ImGui::SliderInt("Diffuse history length [frames]", &m_relaxSettings.diffuseMaxAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
            ImGui::SliderInt("Specular history length [frames]", &m_relaxSettings.specularMaxAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
          } else {
            ImGui::SliderFloat("History Length [ms]", &m_adaptiveAccumulationLengthMs, 10.f, 1000.f, "%.1f");
            ImGui::SliderInt("Min History Length [frames]", &m_adaptiveMinAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
          }
          ImGui::SliderInt("Diffuse fast history length [frames]", &m_relaxSettings.diffuseMaxFastAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
          ImGui::SliderInt("Specular fast history length [frames]", &m_relaxSettings.specularMaxFastAccumulatedFrameNum, 0, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
          ImGui::Checkbox("Anti-firefly", &m_relaxSettings.enableAntiFirefly);
          ImGui::Checkbox("Roughness edge stopping", &m_relaxSettings.enableRoughnessEdgeStopping);
          relaxHitTReconstructionModeCombo.getKey(&m_relaxSettings.hitDistanceReconstructionMode);

          ImGui::Text("PRE-PASS:");
          const float maxBlurRadius = RtxOptions::Get()->isAdaptiveResolutionDenoisingEnabled() ? 200.0f : 100.0f;
          ImGui::SliderFloat("Diffuse preblur radius", &m_relaxInternalBlurRadius.diffusePrepassBlurRadius, 0.0f, maxBlurRadius, "%.1f");
          ImGui::SliderFloat("Specular preblur radius", &m_relaxInternalBlurRadius.specularPrepassBlurRadius, 0.0f, maxBlurRadius, "%.1f");

          ImGui::Text("REPROJECTION:");
          ImGui::SliderFloat("Specular variance boost", &m_relaxSettings.specularVarianceBoost, 0.0f, 8.0f, "%.2f");
          ImGui::SliderFloat("Clamping color sigma scale", &m_relaxSettings.historyClampingColorBoxSigmaScale, 0.0f, 10.0f, "%.1f");

          ImGui::Text("SPATIAL FILTERING:");
          ImGui::SliderInt("A-trous iterations", (int32_t*) &m_relaxSettings.atrousIterationNum, 2, 8);
          ImGui::SliderFloat("Diffuse phi luminance", &m_relaxSettings.diffusePhiLuminance, 0.0f, 10.0f, "%.1f");
          ImGui::SliderFloat("Specular phi luminance", &m_relaxSettings.specularPhiLuminance, 0.0f, 10.0f, "%.1f");
          ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.9f);
          ImGui::SliderFloat("Lobe angle fraction [normalized %]", &m_relaxSettings.lobeAngleFraction, 0.0f, 1.0f, "%.2f");
          ImGui::SetNextItemWidth(ImGui::CalcItemWidth() * 0.9f);
          ImGui::SliderFloat("Roughness fraction [normalized %]", &m_relaxSettings.roughnessFraction, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Luminance edge stopping relaxation", &m_relaxSettings.luminanceEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Normal edge stopping relaxation", &m_relaxSettings.normalEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Roughness edge stopping relaxation", &m_relaxSettings.roughnessEdgeStoppingRelaxation, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Specular lobe angle slack [degrees]", &m_relaxSettings.specularLobeAngleSlack, 0.0f, 89.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
          ImGui::SliderFloat("Min Hit Distance Weight", &m_relaxSettings.minHitDistanceWeight, 0.0f, 0.2f, "%.3f");
          ImGui::SliderFloat("Diffuse min luminance weight", &m_relaxSettings.diffuseMinLuminanceWeight, 0.0f, 1.0f, "%.3f");
          ImGui::SliderFloat("Specular min luminance weight", &m_relaxSettings.specularMinLuminanceWeight, 0.0f, 1.0f, "%.3f");
          ImGui::SliderFloat("Depth threshold [normalized %]", &m_relaxSettings.depthThreshold, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
          ImGui::SliderFloat("Confidence driven relaxation multiplier", &m_relaxSettings.confidenceDrivenRelaxationMultiplier, 0.0f, 1.0f, "%.3f");
          ImGui::SliderFloat("Confidence driven luminance edge stopping relaxation", &m_relaxSettings.confidenceDrivenLuminanceEdgeStoppingRelaxation, 0.0f, 5.0f, "%.3f");
          ImGui::SliderFloat("Confidence driven normal edge stopping relaxation", &m_relaxSettings.confidenceDrivenNormalEdgeStoppingRelaxation, 0.0f, 1.0f, "%.3f");

          ImGui::Text("DISOCCLUSION FIX:");
          ImGui::SliderFloat("Edge-stop normal power", &m_relaxSettings.historyFixEdgeStoppingNormalPower, 0.0f, 128.0f, "%.1f");
          ImGui::SliderInt("Frames to fix", (int32_t*) &m_relaxSettings.historyFixFrameNum, 0, 3);

          ImGui::Text("SPATIAL VARIANCE ESTIMATION:");
          ImGui::SliderInt("History threshold", (int32_t*) &m_relaxSettings.spatialVarianceEstimationHistoryThreshold, 0, 10);

          ImGui::Text("ANTI-LAG:");
          ImGui::SliderFloat("History acceleration amount", &m_relaxSettings.antilagSettings.accelerationAmount, 0.0f, 1.0f, "%.2f");
          ImGui::SliderFloat("Spatial sigma scale", &m_relaxSettings.antilagSettings.spatialSigmaScale, 0.0f, 100.0f, "%.2f");

          ImGui::SliderFloat("Temporal sigma scale", &m_relaxSettings.antilagSettings.temporalSigmaScale, 0.0f, 100.0f, "%.2f");
          ImGui::SliderFloat("History reset amount", &m_relaxSettings.antilagSettings.resetAmount, 0.0f, 1.0f, "%.2f");
        }
        ImGui::Unindent();

        if (resetHistoryOnSettingsChange && memcmp(&m_relaxSettings, &prevRelaxSettingsState, sizeof(m_relaxSettings)) != 0) {
          m_resetHistory = true;
        }
      }
    }
  }

  float NrdSettings::getTimeDeltaBetweenFrames() {
    return timeDeltaBetweenFrames();
  }

  void NrdSettings::updateAdaptiveAccumulation(float frameTimeMs) {
    const uint32_t maxAccumFrameNum = static_cast<uint32_t>(ceil(m_adaptiveAccumulationLengthMs / frameTimeMs));
    if (m_denoiserDesc.denoiser == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR) {
      m_reblurSettings.maxAccumulatedFrameNum = Clamp(maxAccumFrameNum, m_adaptiveMinAccumulatedFrameNum, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
    } else if (m_denoiserDesc.denoiser == nrd::Denoiser::RELAX_DIFFUSE_SPECULAR) {
      m_relaxSettings.diffuseMaxAccumulatedFrameNum = Clamp(maxAccumFrameNum, m_adaptiveMinAccumulatedFrameNum, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
      m_relaxSettings.specularMaxAccumulatedFrameNum = Clamp(maxAccumFrameNum, m_adaptiveMinAccumulatedFrameNum, nrd::RELAX_MAX_HISTORY_FRAME_NUM);
    }
  }
} // namespace dxvk
