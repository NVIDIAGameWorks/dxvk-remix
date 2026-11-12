/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

#include <filesystem>
#include <string>

#include "imgui.h"
#include "dxvk_imgui.h"
#include "rtx_render/rtx_imgui.h"
#include "dxvk_device.h"
#include "dxvk_memory_tracker.h"
#include "rtx_render/graph/rtx_graph_manager.h"
#include "rtx_render/rtx_camera.h"
#include "rtx_render/rtx_context.h"
#include "rtx_render/rtx_hash_collision_detection.h"
#include "rtx_render/rtx_nsight_capture.h"
#include "rtx_render/rtx_option_layer_gui.h"
#include "rtx_render/rtx_option_manager.h"
#include "rtx_render/rtx_options.h"
#include "rtx_render/rtx_shader_manager.h"
#include "../util/util_string.h"

namespace dxvk {
  RemixGui::ComboWithKey<CameraAnimationMode> cameraAnimationModeCombo {
    "Camera Animation Mode",
    RemixGui::ComboWithKey<CameraAnimationMode>::ComboEntries { {
        {CameraAnimationMode::CameraShake_LeftRight, "CameraShake Left-Right"},
        {CameraAnimationMode::CameraShake_FrontBack, "CameraShake Front-Back"},
        {CameraAnimationMode::CameraShake_Yaw, "CameraShake Yaw"},
        {CameraAnimationMode::CameraShake_Pitch, "CameraShake Pitch"},
        {CameraAnimationMode::YawRotation, "Camera Yaw Rotation"}
    } }
  };

#ifdef REMIX_DEVELOPMENT
  RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries aliasingPassComboEntries = { {
      { RtxFramePassStage::FrameBegin, "FrameBegin" },
      { RtxFramePassStage::Volumetrics, "Volumetrics" },
      { RtxFramePassStage::SparseRendering, "SparseRendering" },
      { RtxFramePassStage::VolumeIntegrateRestirInitial, "VolumeIntegrateRestirInitial" },
      { RtxFramePassStage::VolumeIntegrateRestirVisible, "VolumeIntegrateRestirVisible" },
      { RtxFramePassStage::VolumeIntegrateRestirTemporal, "VolumeIntegrateRestirTemporal" },
      { RtxFramePassStage::VolumeIntegrateRestirSpatialResampling, "VolumeIntegrateRestirSpatialResampling" },
      { RtxFramePassStage::VolumeIntegrateRaytracing, "VolumeIntegrateRaytracing" },
      { RtxFramePassStage::GBufferPrimaryRays, "GBufferPrimaryRays" },
      { RtxFramePassStage::PSR, "PSR" },
      { RtxFramePassStage::RTXDI_InitialTemporalReuse, "RTXDI_InitialTemporalReuse" },
      { RtxFramePassStage::RTXDI_SpatialReuse, "RTXDI_SpatialReuse" },
      { RtxFramePassStage::NEE_Cache, "NEE_Cache" },
      { RtxFramePassStage::DirectIntegration, "DirectIntegration" },
      { RtxFramePassStage::RTXDI_ComputeGradients, "RTXDI_ComputeGradients" },
      { RtxFramePassStage::IndirectIntegration, "IndirectIntegration" },
      { RtxFramePassStage::NEE_Integration, "NEE_Integration" },
      { RtxFramePassStage::NRC, "NRC" },
      { RtxFramePassStage::RTXDI_FilterGradients, "RTXDI_FilterGradients" },
      { RtxFramePassStage::RTXDI_ComputeConfidence, "RTXDI_ComputeConfidence" },
      { RtxFramePassStage::ReSTIR_GI_TemporalReuse, "ReSTIR_GI_TemporalReuse" },
      { RtxFramePassStage::ReSTIR_GI_SpatialReuse, "ReSTIR_GI_SpatialReuse" },
      { RtxFramePassStage::ReSTIR_GI_FinalShading, "ReSTIR_GI_FinalShading" },
      { RtxFramePassStage::Demodulate, "Demodulate" },
      { RtxFramePassStage::NRD, "NRD" },
      { RtxFramePassStage::CompositionAlphaBlend, "CompositionAlphaBlend" },
      { RtxFramePassStage::Composition, "Composition" },
      { RtxFramePassStage::DLSS, "DLSS" },
      { RtxFramePassStage::DLSSRR, "DLSSRR" },
      { RtxFramePassStage::DLSSNR, "DLSSNR" },
      { RtxFramePassStage::NIS, "NIS" },
      { RtxFramePassStage::XeSS, "XeSS" },
      { RtxFramePassStage::TAA, "TAA" },
      { RtxFramePassStage::DustParticles, "DustParticles" },
      { RtxFramePassStage::Bloom, "Bloom" },
      { RtxFramePassStage::PostFX, "PostFX" },
      { RtxFramePassStage::AutoExposure_Histogram, "AutoExposure_Histogram" },
      { RtxFramePassStage::AutoExposure_Exposure, "AutoExposure_Exposure" },
      { RtxFramePassStage::ToneMapping, "ToneMapping" },
      { RtxFramePassStage::FrameEnd, "FrameEnd" },
  } };

  static auto aliasingBeginPassCombo = RemixGui::ComboWithKey<dxvk::RtxFramePassStage>(
    "Aliasing Begin Pass", RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries{ aliasingPassComboEntries });

  static auto aliasingEndPassCombo = RemixGui::ComboWithKey<dxvk::RtxFramePassStage>(
    "Aliasing End Pass", RemixGui::ComboWithKey<dxvk::RtxFramePassStage>::ComboEntries { aliasingPassComboEntries });

  static auto aliasingExtentCombo = RemixGui::ComboWithKey<RtxTextureExtentType>(
    "Aliasing Extent Type",
    { {
      { RtxTextureExtentType::DownScaledExtent, "DownScaledExtent" },
      { RtxTextureExtentType::TargetExtent, "TargetExtent" },
      { RtxTextureExtentType::Custom, "Custom" },
  } } );

  static auto aliasingFormatCombo = RemixGui::ComboWithKey<RtxTextureFormatCompatibilityCategory>(
     "Aliasing Format",
     { {
      { RtxTextureFormatCompatibilityCategory::Color_Format_8_Bits, "8 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_16_Bits, "16 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_32_Bits, "32 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_64_Bits, "64 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_128_Bits, "128 Bits Color Texture" },
      { RtxTextureFormatCompatibilityCategory::Color_Format_256_Bits, "256 Bits Color Texture" },
      // All other formats
      { RtxTextureFormatCompatibilityCategory::InvalidFormatCompatibilityCategory, "Not Listed Format" },
     } }
  );

  static auto aliasingImageTypeCombo = RemixGui::ComboWithKey<VkImageType>(
     "Aliasing Image Type",
     { {
      { VK_IMAGE_TYPE_1D, "VK_IMAGE_TYPE_1D" },
      { VK_IMAGE_TYPE_2D, "VK_IMAGE_TYPE_2D" },
      { VK_IMAGE_TYPE_3D, "VK_IMAGE_TYPE_3D" },
     } }
  );

  static auto aliasingImageViewTypeCombo = RemixGui::ComboWithKey<VkImageViewType>(
     "Aliasing Image View Type",
     { {
      { VK_IMAGE_VIEW_TYPE_1D, "VK_IMAGE_VIEW_TYPE_1D" },
      { VK_IMAGE_VIEW_TYPE_1D_ARRAY, "VK_IMAGE_VIEW_TYPE_1D_ARRAY" },
      { VK_IMAGE_VIEW_TYPE_2D, "VK_IMAGE_VIEW_TYPE_2D" },
      { VK_IMAGE_VIEW_TYPE_2D_ARRAY, "VK_IMAGE_VIEW_TYPE_2D_ARRAY" },
      { VK_IMAGE_VIEW_TYPE_3D, "VK_IMAGE_VIEW_TYPE_3D" },
      { VK_IMAGE_VIEW_TYPE_CUBE, "VK_IMAGE_VIEW_TYPE_CUBE" },
      { VK_IMAGE_VIEW_TYPE_CUBE_ARRAY, "VK_IMAGE_VIEW_TYPE_CUBE_ARRAY" },
     } }
  );
#endif

  static auto themeCombo = RemixGui::ComboWithKey<ImGUI::Theme>(
    "Mode##theme",
    {
      {ImGUI::Theme::Toolkit,  "Default Theme"},
      {ImGUI::Theme::Legacy,   "Legacy Theme"},
      {ImGUI::Theme::Nvidia,   "NVIDIA Theme"},
  });

  // Styles
  constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
  constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;

  void ImGUI::showDevelopmentSettings(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth((largeUiMode() ? m_largeWindowWidgetWidth : m_regularWindowWidgetWidth) + 50.0f);
    if (ImGui::Button("Take Screenshot")) {
      RtxContext::triggerScreenshot();
    }

    RemixGui::SetTooltipToLastWidgetOnHover("Screenshot will be dumped to, '<exe-dir>/Screenshots'");

    ImGui::SameLine(200.f);
    RemixGui::Checkbox("Include G-Buffer", &RtxOptions::captureDebugImageObject());

    RemixGui::Separator();

    if (RemixGui::CollapsingHeader("Nsight Graphics Capture", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
      RemixGui::Checkbox("Enable Nsight Graphics Capture On Launch", &NsightGraphicsCapture::graphicsCaptureEnabledObject());
      RemixGui::InputText("Nsight Graphics Install Path", &NsightGraphicsCapture::graphicsCaptureInstallPathObject());
      RemixGui::InputText("Capture Output Directory", &NsightGraphicsCapture::graphicsCaptureOutputDirObject());
      RemixGui::InputText("Capture Output File", &NsightGraphicsCapture::graphicsCaptureOutputFileObject());

      RemixGui::DragInt("Capture Frames", &NsightGraphicsCapture::graphicsCaptureFramesToCaptureObject(), 1.0f,
                        NsightGraphicsCapture::kMinFramesToCapture, NsightGraphicsCapture::kMaxFramesToCapture, "%d",
                        ImGuiSliderFlags_AlwaysClamp);
      RemixGui::Checkbox("Show Nsight HUD", &NsightGraphicsCapture::graphicsCaptureShowHudObject());

      ImGui::TextWrapped("Status: %s", NsightGraphicsCapture::statusText().c_str());
      ImGui::TextWrapped("Last Result: %s", NsightGraphicsCapture::lastResultText().c_str());

      ImGui::BeginDisabled(!NsightGraphicsCapture::isAvailable());
      if (ImGui::Button("Trigger Nsight Graphics Capture")) {
        NsightGraphicsCapture::requestGraphicsCapture(NsightGraphicsCapture::graphicsCaptureFramesToCapture());
      }
      ImGui::EndDisabled();
#else
      ImGui::TextWrapped("Nsight Graphics capture requires an x64 or ARM64EC build. See the build instructions in README.md.");
#endif

      ImGui::Unindent();
    }

    RemixGui::Separator();
        
#ifdef REMIX_DEVELOPMENT
    { // Recompile Shaders button and its status information (Only available for Development Remix builds)
      const auto& shaderManager{ ShaderManager::getInstance() };
      const auto shaderReloadPhase{ shaderManager->getShaderReloadPhase() };
      const auto lastShaderReloadStatus{ shaderManager->getLastShaderReloadStatus() };

      // Note: Only allow the Recompile Shaders button to function if a shader recompile is not currently in progress (be
      // it one manually initiated by the user, or something automatic from the live shader edit mode).
      ImGui::BeginDisabled(shaderReloadPhase != ShaderManager::ShaderReloadPhase::Idle);

      if (ImGui::Button("Recompile Shaders")) {
        shaderManager->requestReloadShaders();
      }

      ImGui::EndDisabled();

      ImGui::SameLine(200.f);
      RemixGui::Checkbox("Live shader edit mode", &RtxOptions::Shader::useLiveEditModeObject());

      const char* shaderReloadPhaseText;
      const char* lastShaderReloadStatusText;
      ImVec4 shaderReloadPhaseTextColor;
      ImVec4 lastShaderReloadStatusTextColor;

      switch (shaderReloadPhase) {
      default: assert(false); [[fallthrough]];
      case ShaderManager::ShaderReloadPhase::Idle:
        shaderReloadPhaseText = "Idle";
        shaderReloadPhaseTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      case ShaderManager::ShaderReloadPhase::SPIRVRecompilation:
        shaderReloadPhaseText = "Working (SPIR-V Recompilation)";
        shaderReloadPhaseTextColor = ImVec4(0.73f, 0.87f, 0.54f, 1.0f);
        break;
      case ShaderManager::ShaderReloadPhase::ShaderRecreation:
        shaderReloadPhaseText = "Working (Shader Recreation)";
        shaderReloadPhaseTextColor = ImVec4(0.73f, 0.87f, 0.54f, 1.0f);
        break;
      }

      switch (lastShaderReloadStatus) {
      default: assert(false); [[fallthrough]];
      case ShaderManager::ShaderReloadStatus::Unknown:
        lastShaderReloadStatusText = "N/A";
        lastShaderReloadStatusTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        break;
      case ShaderManager::ShaderReloadStatus::Failure:
        lastShaderReloadStatusText = "Failure";
        lastShaderReloadStatusTextColor = ImVec4(0.83f, 0.32f, 0.32f, 1.0f);
        break;
      case ShaderManager::ShaderReloadStatus::Success:
        lastShaderReloadStatusText = "Success";
        lastShaderReloadStatusTextColor = ImVec4(0.44f, 0.81f, 0.42f, 1.0f);
        break;
      }

      ImGui::TextUnformatted("Shader Reload Phase:");
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, shaderReloadPhaseTextColor);
      ImGui::TextUnformatted(shaderReloadPhaseText);
      ImGui::PopStyleColor();

      ImGui::TextUnformatted("Last Shader Reload Status:");
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, lastShaderReloadStatusTextColor);
      ImGui::TextUnformatted(lastShaderReloadStatusText);
      ImGui::PopStyleColor();
    }

    ImGui::Separator();

    { // Crash Hotkey Feature - arms both CPU crash (deliberate crash) and GPU crash (dialog + Sentry) hotkeys
      const bool isArmed = RtxOptions::enableCrashHotkey();
      
      // Use warning color when armed to make it visually distinct
      if (isArmed) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      }
      
      const bool changed = RemixGui::Checkbox("Arm Crash Hotkey", &RtxOptions::enableCrashHotkeyObject());
      
      if (isArmed) {
        ImGui::PopStyleColor();
      }
      
      const auto crashHotkeyStr = buildKeyBindDescriptorStringForDisplay(RtxOptions::crashHotkey());
      const auto gpuCrashHotkeyStr = buildKeyBindDescriptorStringForDisplay(RtxOptions::gpuCrashHotkey());
      RemixGui::SetTooltipToLastWidgetOnHover(
        str::format("When armed: ", crashHotkeyStr, " = deliberate CPU crash; ", gpuCrashHotkeyStr, " = deliberate GPU crash.\n"
        "A red warning indicator will appear on screen while armed.").c_str());
      
      // Log state changes for crash dump analysis
      if (changed) {
        const bool nowArmed = RtxOptions::enableCrashHotkey();
        if (nowArmed) {
          Logger::warn(str::format("Crash hotkeys ARMED - ", crashHotkeyStr, " = CPU crash, ", gpuCrashHotkeyStr, " = GPU crash"));
        } else {
          Logger::warn("Crash hotkeys disarmed");
        }
      }
    }
    
    RemixGui::Separator();
#endif


    showVsyncOptions(false);

    // Render GUI for memory profiler here
    GpuMemoryTracker::renderGui();

    if (RemixGui::CollapsingHeader("Camera", collapsingHeaderFlags)) {
      ImGui::Indent();

      RtCamera::showImguiSettings();

      {
        ImGui::PushID("CameraInfos");
        auto& cameraManager = ctx->getCommonObjects()->getSceneManager().getCameraManager();
        if (RemixGui::CollapsingHeader("Types", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
          constexpr static std::pair<CameraType::Enum, const char*> cameras[] = {
            { CameraType::Main,             "Main" },
            { CameraType::ViewModel,        "ViewModel" },
            { CameraType::Portal0,          "Portal0" },
            { CameraType::Portal1,          "Portal1" },
            { CameraType::Sky,              "Sky" },
            { CameraType::RenderToTexture,  "RenderToTexture" },
          };
          // C++20: should be static_assert with std::ranges::find_if
          assert(
            std::find_if(
              std::begin(cameras),
              std::end(cameras),
              [](const auto& p) { return p.first == CameraType::Unknown; })
            == std::end(cameras));
          static_assert(std::size(cameras) == CameraType::Count - 1);

          static auto printCamera = [](const char* name, const RtCamera* c) {
            if (RemixGui::CollapsingHeader(name, collapsingHeaderFlags)) {
              ImGui::Indent();
              if (c) {
                ImGui::Text("Position: %.2f %.2f %.2f", c->getPosition().x, c->getPosition().y, c->getPosition().z);
                ImGui::Text("Direction: %.2f %.2f %.2f", c->getDirection().x, c->getDirection().y, c->getDirection().z);
                ImGui::Text("Vertical FOV: %.1f", c->getFov() * kRadiansToDegrees);
                ImGui::Text("Near / Far plane: %.1f / %.1f", c->getNearPlane(), c->getFarPlane());
                ImGui::Text("Projection Handedness: %s", c->isLHS() ? "Left-handed" : "Right-handed");
                ImGui::Text("Overall Handedness: %s", c->isLHS() ^ isMirrorTransform(c->getViewToWorld(false))   ? "Left-handed" : "Right-handed");
              } else {
                ImGui::Text("Position: -");
                ImGui::Text("Direction: -");
                ImGui::Text("Vertical FOV: -");
                ImGui::Text("Near / Far plane: -");
                ImGui::Text("-");
                ImGui::Text("-");
              }
              ImGui::Unindent();
            }
          };

          for (const auto& [type, name] : cameras) {
            printCamera(name, cameraManager.isCameraValid(type) ? &cameraManager.getCamera(type) : nullptr);
          }
          ImGui::Unindent();
        }
        ImGui::PopID();
      }

      if (RemixGui::CollapsingHeader("Camera Animation", collapsingHeaderClosedFlags)) {
        RemixGui::Checkbox("Animate Camera", &RtxOptions::shakeCameraObject());
        cameraAnimationModeCombo.getKey(&RtxOptions::cameraAnimationModeObject());
        RemixGui::DragFloat("Animation Amplitude", &RtxOptions::cameraAnimationAmplitudeObject(), 0.1f, 0.f, 1000.f, "%.2f", sliderFlags);
        RemixGui::DragInt("Shake Period", &RtxOptions::cameraShakePeriodObject(), 0.1f, 1, 100, "%d", sliderFlags);
      }

      if (RemixGui::CollapsingHeader("Advanced", collapsingHeaderClosedFlags)) {

        RemixGui::Checkbox("Portals: Camera History Correction", &RtxOptions::rayPortalCameraHistoryCorrectionObject());
        RemixGui::Checkbox("Portals: Camera In-Between Portals Correction", &RtxOptions::rayPortalCameraInBetweenPortalsCorrectionObject());

        if (RtxOptions::rayPortalCameraInBetweenPortalsCorrection()) {
          ImGui::Indent();

          RemixGui::DragFloat("Portals: Camera In-Between Portals Correction Threshold", &RtxOptions::rayPortalCameraInBetweenPortalsCorrectionThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);

          ImGui::Unindent();
        }

        RemixGui::Checkbox("Skip Objects Rendered with Unknown Camera", &RtxOptions::skipObjectsWithUnknownCameraObject());

        RemixGui::Checkbox("Override Near Plane (if less than original)", &RtxOptions::enableNearPlaneOverrideObject());
        ImGui::BeginDisabled(!RtxOptions::enableNearPlaneOverride());
        RemixGui::DragFloat("Desired Near Plane Distance", &RtxOptions::nearPlaneOverrideObject(), 0.01f, 0.0001f, FLT_MAX, "%.3f");
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Camera Sequence", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      RtCameraSequence::getInstance()->showImguiSettings();
      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Developer Options", collapsingHeaderFlags)) {
      ImGui::Indent();
      RemixGui::Checkbox("Enable Preserve Path", &RtxOptions::enablePreservePathObject());
      RemixGui::Checkbox("Enable Instance Debugging", &RtxOptions::enableInstanceDebuggingToolsObject());
      RemixGui::Checkbox("Disable Draw Calls Post RTX Injection", &RtxOptions::skipDrawCallsPostRTXInjectionObject());
      RemixGui::Checkbox("Break into Debugger On Press of Key 'B'", &RtxOptions::enableBreakIntoDebuggerOnPressingBObject());
      RemixGui::Checkbox("Block Input to Game in UI", &RtxOptions::blockInputToGameInUIObject());
      RemixGui::Checkbox("Force Camera Jitter", &RtxOptions::forceCameraJitterObject());
      RemixGui::Checkbox("Force Static Scene Motion Vectors", &RtxOptions::forceStaticSceneMotionVectorsObject());
      RemixGui::DragInt("Camera Jitter Sequence Length", &RtxOptions::cameraJitterSequenceLengthObject());
      
      RemixGui::DragIntRange2("Draw Call Range Filter", &RtxOptions::drawCallRangeObject(), 1.f, 0, INT32_MAX, nullptr, nullptr, ImGuiSliderFlags_AlwaysClamp);
      RemixGui::InputInt("Instance Index Start", &RtxOptions::instanceOverrideInstanceIdxObject());
      RemixGui::InputInt("Instance Index Range", &RtxOptions::instanceOverrideInstanceIdxRangeObject());
      RemixGui::DragFloat3("Instance World Offset", &RtxOptions::instanceOverrideWorldOffsetObject(), 0.1f, -100.f, 100.f, "%.3f", sliderFlags);
      RemixGui::Checkbox("Instance - Print Hash", &RtxOptions::instanceOverrideSelectedInstancePrintMaterialHashObject());

      ImGui::Unindent();
      RemixGui::Checkbox("Throttle presents", &RtxOptions::enablePresentThrottleObject());
      if (RtxOptions::enablePresentThrottle()) {
        ImGui::Indent();
        RemixGui::SliderInt("Present delay", &RtxOptions::presentThrottleDelayObject(), 1, 1000, "%d ms", sliderFlags);
        ImGui::Unindent();
      }
      RemixGui::Checkbox("Hash Collision Detection", &HashCollisionDetectionOptions::enableObject());
      RemixGui::Checkbox("Validate CPU index data", &RtxOptions::validateCPUIndexDataObject());

#ifdef REMIX_DEVELOPMENT
      if (RemixGui::CollapsingHeader("Resource Aliasing Query", collapsingHeaderClosedFlags)) {
        ImGui::Indent();
        aliasingBeginPassCombo.getKey(&RtxOptions::Aliasing::beginPassObject());
        aliasingEndPassCombo.getKey(&RtxOptions::Aliasing::endPassObject());
        aliasingFormatCombo.getKey(&RtxOptions::Aliasing::formatCategoryObject());
        aliasingExtentCombo.getKey(&RtxOptions::Aliasing::extentTypeObject());
        const auto aliasingExtentType = RtxOptions::Aliasing::extentType();
        if (aliasingExtentType == RtxTextureExtentType::Custom) {
          RemixGui::DragInt("Aliasing Width", &RtxOptions::Aliasing::widthObject());
          RemixGui::DragInt("Aliasing Height", &RtxOptions::Aliasing::heightObject());
        }
        if (RtxOptions::Aliasing::imageType() == VkImageType::VK_IMAGE_TYPE_3D)
        {
          RemixGui::DragInt("Aliasing Depth", &RtxOptions::Aliasing::depthObject());
        }
        RemixGui::DragInt("Aliasing Layer", &RtxOptions::Aliasing::layerObject());
        aliasingImageTypeCombo.getKey(&RtxOptions::Aliasing::imageTypeObject());
        aliasingImageViewTypeCombo.getKey(&RtxOptions::Aliasing::imageViewTypeObject());

        if (IMGUI_ADD_TOOLTIP(ImGui::Button("Check aliasing for a new resource"),
          "Make sure to check the resources can be aliased under all major settings. For example, DLSS-RR or NRD, NRC or ReSTIR-GI.")) {
          Resources::s_queryAliasing = true;
        } else {
          Resources::s_queryAliasing = false;
        }
        std::string resourceAliasingQueryText = "Resource Aliasing Query Result: (";
        if (RtxOptions::enableRayReconstruction()) {
          resourceAliasingQueryText += "DLSS-RR, ";
        } else {
          resourceAliasingQueryText += "NRD, ";
        }
        if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache) {
          resourceAliasingQueryText += "NRC)";
        } else if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
          resourceAliasingQueryText += "ReSTIR-GI)";
        } else {
          resourceAliasingQueryText += "ImportanceSampled)";
        }

        ImGui::Text(resourceAliasingQueryText.c_str());
        ImGui::Text("%s", Resources::s_resourceAliasingQueryText.c_str());

        if (IMGUI_ADD_TOOLTIP(ImGui::Button("Check aliasing for current resources"), "Make sure the resources are being active when checking for aliasing.")) {
          Resources::s_startAliasingAnalyzer = true;
        } else {
          Resources::s_startAliasingAnalyzer = false;
        }
        auto& str = Resources::s_aliasingAnalyzerResultText;
        ImGui::Text("Available Aliasing:\n%s", Resources::s_aliasingAnalyzerResultText.c_str());
        ImGui::Unindent();
      }
#endif
    }

    if (IMGUI_ADD_TOOLTIP(RemixGui::CollapsingHeader("Option Layers"), "View what options are present in each layer, and alter the blend strength and threshold for them.")) {
      ImGui::Indent();
      static char optionLayerFilter[256] = "";
      // Filter for option layer contents
      IMGUI_ADD_TOOLTIP(ImGui::InputText("RtxOption Display Filter", optionLayerFilter, IM_ARRAYSIZE(optionLayerFilter)), 
          "Filter options displayed in the Contents sections. Only options containing this text will be shown.");

          RemixGui::Checkbox("Pause Graph Execution", &GraphManager::pauseGraphUpdatesObject());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
          "Many graphs set `enable`, `blendStrength`, and `blendThreshold` every frame.\n"
          "Pausing the graph execution will allow controlling these values without interference.");
      }

        // Pre-compute lowercased filter once for efficiency
      std::string filterLower = optionLayerFilter;
      std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

      uint32_t optionLayerCounter = 1;
      for (auto& [layerKey, optionLayerPtr] : RtxOptionManager::getLayerRegistry()) {
        RtxOptionLayer& optionLayer = *optionLayerPtr;

        const bool isDefaultLayer = layerKey == kRtxOptionLayerDefaultKey;
        const bool isUserLayer = layerKey == kRtxOptionLayerUserKey;
        const bool isQualityLayer = layerKey == kRtxOptionLayerQualityKey;
        const bool isDxvkLayer = layerKey == kRtxOptionLayerDxvkConfKey;
        const bool isRtxConfLayer = layerKey == kRtxOptionLayerRtxConfKey;
        const bool hasSaveableConfig = optionLayer.hasSaveableConfigFile() && !isDxvkLayer;
        const bool hasUnsaved = hasSaveableConfig && optionLayer.hasUnsavedChanges();

        // Skip layers with no values (empty layers), unless they have a saveable config
        // Dynamic layers with saveable configs should still be shown even when empty/disabled
        if (!optionLayer.hasValues() && !hasSaveableConfig) {
          continue;
        }
        
        // Determine display name - system layers have proper names,
        // dynamically loaded layers have file paths as names which need shortening
        std::string displayName = optionLayer.getName();
        
        // Add config file indicator for layers with associated config files
        if (isRtxConfLayer) {
          displayName += " (rtx.conf)";
        } else if (isUserLayer) {
          displayName += " (user.conf)";
        } else if (isDxvkLayer) {
          displayName += " (dxvk.conf)";
        }
        
        // For non-system layers, shorten long file paths for display
        if (displayName.length() > 40) {
          // Try to extract just the file name from the path
          const std::string modsMarker = (std::filesystem::path("rtx-remix") / "mods" / "").string();
          size_t modsPos = displayName.find(modsMarker);
          if (modsPos != std::string::npos) {
            displayName = displayName.substr(modsPos + modsMarker.length());
          } else {
            // Just take the last portion of the path
            size_t lastSep = displayName.find_last_of("/\\");
            if (lastSep != std::string::npos) {
              displayName = displayName.substr(lastSep + 1);
            }
          }
        }
        
        // Build header text and styling
        std::string unsavedIndicator = hasUnsaved ? " *" : "";
        const std::string optionLayerText = std::to_string(optionLayerCounter++) + ". " + displayName + unsavedIndicator + "###" + displayName;
        
        // Determine if layer is active (only applies to layers with blend controls)
        bool pendingEnabled = optionLayer.getPendingEnabled();
        float pendingStrength = optionLayer.getPendingBlendStrength();
        float pendingThreshold = optionLayer.getPendingBlendThreshold();
        const bool isLayerActive = !hasSaveableConfig || (pendingEnabled && pendingStrength > pendingThreshold);
        
        // Apply header styling
        bool pushedStyle = false;
        if (!isLayerActive) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
          pushedStyle = true;
        } else if (hasUnsaved) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
          pushedStyle = true;
        }
        
        // Build tooltip
        std::string tooltipText;
        if (isQualityLayer) {
          tooltipText = "Quality preset settings (highest priority). Empty when Graphics Preset is Custom.";
        } else if (isUserLayer) {
          tooltipText = "A User's local graphics settings.  Overrides all other layers except Quality Presets.";
        } else if (isDefaultLayer) {
          tooltipText = "Default values for each option, as defined in RtxOptions.md.";
        } else {
          tooltipText = optionLayer.getName();
        }
        if (!optionLayer.getFilePath().empty() && optionLayer.getFilePath() != optionLayer.getName()) {
          tooltipText += "\nFile: " + optionLayer.getFilePath();
        }
        if (hasUnsaved) {
          tooltipText += "\n[Has unsaved changes]";
        }
        
        bool headerOpen = IMGUI_ADD_TOOLTIP(RemixGui::CollapsingHeader(optionLayerText.c_str(), collapsingHeaderClosedFlags), tooltipText.c_str());
        
        if (pushedStyle) {
          ImGui::PopStyleColor();
        }
        
        if (headerOpen) {
          ImGui::Indent();
          
          // Priority display
          if (isQualityLayer) {
            ImGui::Text("Priority: MAX");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Highest possible priority - quality preset settings control these options when preset is not Custom.\nThis layer is empty when Graphics Preset is set to Custom.");
            }
          } else if (isUserLayer) {
            ImGui::Text("Priority: MAX - 1");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Second highest priority - user settings that override all layers except Quality Presets.\nWhen Graphics Preset is Custom, this becomes the effective highest priority layer.");
            }
          } else if (isDefaultLayer) {
            ImGui::Text("Priority: 0");
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("Lowest possible priority - every other layer will be applied on top of this layer.");
            }
          } else {
            ImGui::Text("Priority: %u", optionLayer.getLayerKey().priority);
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip(
                "Layers are applied starting with the lowest priority layer, ending with the highest.\n"
                "Each layer overrides the values written before it.\n"
                "If a layer's blendWeight is not 1 and the option is a float or Vector type,\n"
                "then the values will be calculated as LERP(previousValue, layerValue, blendWeight).");
            }
          }
          
          // Enable/blend controls only for saveable config layers (Remix Config, User, dynamically loaded mods)
          if (hasSaveableConfig && !isUserLayer) {
            const std::string optionLayerEnabledText = "Enabled###Enabled_" + displayName;
            const std::string optionLayerStrengthText = " Strength###Strength_" + displayName;
            const std::string optionLayerThresholdText = " Threshold###Threshold_" + displayName;
            
            if (IMGUI_ADD_TOOLTIP(ImGui::Checkbox(optionLayerEnabledText.c_str(), &pendingEnabled), "Check to enable the option layer. Uncheck to disable it.")) {
              optionLayer.requestEnabled(pendingEnabled);
            }

            if (IMGUI_ADD_TOOLTIP(ImGui::SliderFloat(optionLayerStrengthText.c_str(), &pendingStrength, 0.0f, 1.0f),
                                  "Adjusts the blending strength of this option layer (0 = off, 1 = full effect).")) {
              optionLayer.requestBlendStrength(pendingStrength);
            }

            if (IMGUI_ADD_TOOLTIP(ImGui::SliderFloat(optionLayerThresholdText.c_str(), &pendingThreshold, 0.0f, 1.0f),
                                  "Sets the blending strength threshold for this option layer.")) {
              optionLayer.requestBlendThreshold(pendingThreshold);
            }
          }
          
          // Action buttons only for saveable config layers
          if (hasSaveableConfig) {
            OptionLayerUI::renderLayerButtons(optionLayerPtr.get(), displayName.c_str());
          }
          
          // Contents section
          const std::string optionLayerContentsText = "Contents###Contents_" + displayName;
          if (RemixGui::CollapsingHeader(optionLayerContentsText.c_str(), collapsingHeaderClosedFlags)) {
            ImGui::Indent();
            OptionLayerUI::displayContents(optionLayer, filterLower);
            ImGui::Unindent();
          }
          
          ImGui::Unindent();
        }
      }

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("UI Options")) {
      ImGui::Indent();

      if (m_pendingUIOptionsScroll) {
        ImGui::SetScrollHereY(0.0f);
        m_pendingUIOptionsScroll = false;
      }

      {
        if (RemixGui::Checkbox("Compact UI", &compactGuiObject())) {
          // Scroll to UI Options on the next frame
          m_pendingUIOptionsScroll = true;
        }
      }

      RemixGui::Checkbox("Always Developer Menu", &RtxOptions::defaultToAdvancedUIObject());

      if (RemixGui::SliderFloat("Background Alpha", &backgroundAlphaObject(), 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        adjustStyleBackgroundAlpha(backgroundAlpha());
      }

      
      if (RemixGui::Checkbox("Use Large UI", &largeUiModeObject())) {
        m_pendingUIOptionsScroll = true;
      }

      {
        constexpr float indent = 60.0f;
        ImGui::PushID("gui theme");
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::Text("GUI Theme:");
        ImGui::PushItemWidth(ImGui::GetContentRegionMax().x - indent);

        if (themeCombo.getKey(&themeGuiObject())) {
          m_pendingUIOptionsScroll = true;
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
      }

      ImGui::Unindent();
    }

    ImGui::PopItemWidth();
  }
}
