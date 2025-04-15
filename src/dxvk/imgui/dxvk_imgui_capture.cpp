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

#include "dxvk_imgui_capture.h"

#include "../dxvk_context.h"
#include "../dxvk_device.h"
#include "../dxvk_objects.h"
#include "../rtx_render/rtx_context.h"
#include "../rtx_render/rtx_imgui.h"
#include "../../lssusd/game_exporter_paths.h"

#include "../../util/util_filesys.h"
#include "../../util/util_string.h"

#include <filesystem>

namespace {
  static constexpr ImGuiTreeNodeFlags kCollapsingHeaderFlags = ImGuiTreeNodeFlags_CollapsingHeader;
}

namespace dxvk {
  void ImGuiCapture::update(const Rc<DxvkContext>& ctx) {
    const bool hotkey = ImGUI::checkHotkeyState(RtxOptions::Get()->m_captureHotKey);
    if(hotkey) {
      const bool showMenu = RtxOptions::Get()->m_captureShowMenuOnHotkey;
      const bool menuOpen = m_masterImGui->isTabOpen<ImGUI::Tabs::kTab_Enhancements>();
      if(showMenu && !menuOpen) {
        m_masterImGui->openTab<ImGUI::Tabs::kTab_Enhancements>();
      } else {
        ctx->getCommonObjects()->capturer()->triggerNewCapture();
      }
    }
    m_stageNameInputBox.update(ctx);
    m_progress.update(ctx);
  }

  void ImGuiCapture::show(const Rc<DxvkContext>& ctx) {
    auto capturer = ctx->getCommonObjects()->capturer();
    const bool disableCapture =
      ctx->getCommonObjects()->getSceneManager().areAllReplacementsLoaded() &&
      RtxOptions::Get()->getEnableAnyReplacements();
    constexpr auto headerFlagsDefaultOpen = kCollapsingHeaderFlags | ImGuiTreeNodeFlags_DefaultOpen;
    if(ImGui::CollapsingHeader("USD Scene Capture", headerFlagsDefaultOpen)) {
      ImGui::Indent();
      ImGui::Text(disableCapture ? "Disable enhanced assets to enable capturing." : "Ready to capture.");
      ImGui::BeginDisabled(disableCapture);
      showSceneCapture(ctx);
      if(RtxOptions::Get()->m_captureEnableMultiframe) {
        showTimedCapture(ctx);
        showContinuousCapture(ctx);
      }
      ImGui::Separator();
      ImGui::Checkbox("Correct baked world transforms", &capturer->correctBakedTransformsRef());
      ImGui::Checkbox("Show menu on capture hotkey", &RtxOptions::Get()->m_captureShowMenuOnHotkey);
      if(RtxOptions::Get()->m_captureShowMenuOnHotkey) {
        ImGui::PushTextWrapPos(ImGui::GetCurrentWindow()->Size.x);
        ImGui::PopTextWrapPos();
      }
      ImGui::EndDisabled();
      ImGui::Unindent();
    }
  }
  
  void ImGuiCapture::showSceneCapture(const Rc<DxvkContext>& ctx) {
    ImGui::Text("Name");
    static const float nameX = ImGui::GetItemRectSize().x;
    ImGui::SameLine();
    m_stageNameInputBox.show(ctx);
    static const float inputX = ImGui::GetItemRectSize().x;
    ImGui::SameLine();
    static float commonButtonWidth = 0.f;
    if(ImGui::Button("Capture Scene", ImVec2(commonButtonWidth,0.f))) {
      if (this->m_stageNameInputBox.isStageNameValid()) {
        RtxOptions::Get()->m_captureInstances.getValue() = true;
        ctx->getCommonObjects()->capturer()->triggerNewCapture();
        this->m_stageNameInputBox.m_isCaptureNameInvalid = false;
      }
      else {
        this->m_stageNameInputBox.m_isCaptureNameInvalid = true;
      }
    }
    const float firstButtonWidth = ImGui::GetItemRectSize().x;
    ImGui::Dummy(ImVec2());
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(nameX + inputX,0.f));
    ImGui::SameLine();
    if(ImGui::Button("Capture Assets Only", ImVec2(commonButtonWidth,0.f))) {
      RtxOptions::Get()->m_captureInstances.getValue() = false;
      ctx->getCommonObjects()->capturer()->triggerNewCapture();
    }
    commonButtonWidth = std::max(firstButtonWidth, ImGui::GetItemRectSize().x);
    m_stageNameInputBox.validateStageName();
    m_progress.show(ctx);
  }

  void ImGuiCapture::showTimedCapture(const Rc<DxvkContext>& ctx) {
    if(ImGui::CollapsingHeader("Timed Capture", kCollapsingHeaderFlags)) {
      ImGui::Indent();
      ImGui::InputInt("Max Frames", &RtxOptions::Get()->m_captureMaxFrames);
      ImGui::InputInt("Frames Per Second", &RtxOptions::Get()->m_captureFramesPerSecond);
      if(ImGui::CollapsingHeader("Animation Compression", kCollapsingHeaderFlags)) {
        ImGui::Indent();
        ImGui::Text("Inter-frame Mesh Deltas");
        ImGui::InputFloat("Position",&RtxOptions::Get()->m_captureMeshPositionDelta);
        ImGui::InputFloat("Normal",&RtxOptions::Get()->m_captureMeshNormalDelta);
        ImGui::InputFloat("Texcoord",&RtxOptions::Get()->m_captureMeshTexcoordDelta);
        ImGui::InputFloat("Color",&RtxOptions::Get()->m_captureMeshColorDelta);
        ImGui::InputFloat("Blend Weight",&RtxOptions::Get()->m_captureMeshBlendWeightDelta);
        ImGui::Unindent();
      }
      ImGui::Unindent();
    }
  }

  void ImGuiCapture::showContinuousCapture(const Rc<DxvkContext>& ctx) {
    if(ImGui::CollapsingHeader("Continuous Capture", kCollapsingHeaderFlags)) {
    }
  }

  ImGuiCapture::StageNameInputBox::StageNameInputBox() {
    auto& instanceStageName = RtxOptions::Get()->m_captureInstanceStageName;
    memset(&m_buf[0], '\0', kBufSize);
    const char* const defaultVal = instanceStageName.getValue().c_str();
    strncpy(&m_buf[0], defaultVal, strlen(defaultVal));
  }

  void ImGuiCapture::StageNameInputBox::validateStageName() {
    if (m_isCaptureNameInvalid) {
      auto& instanceStageNameRtxOpt = RtxOptions::Get()->m_captureInstanceStageName;
      std::string msg = "Invalid capture name detected. Please remove any invalid characters or use of any invalid keywords specified in the description as capture names to take capture.";
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 32, 0, 255));
      ImGui::TextWrapped(msg.c_str());
      ImGui::PopStyleColor();
    }
  }

  void ImGuiCapture::StageNameInputBox::update(const Rc<DxvkContext>& ctx) {
    if(m_focused) {
      setValue();
    }
    if(m_timestampPos != std::string::npos) {
      replaceTimestampStr();
    }
  }
  
  bool ImGuiCapture::StageNameInputBox::isInvalidKeywordUsed(std::string name) { 
    // To perform a case-insensitive compare
    std::transform(name.begin(), name.end(), name.begin(), toupper);
    for (const std::string& keyword : m_invalidKeywords) {
      if (keyword == name) {
        return false;
      }
    }
    return true;
  }

  bool ImGuiCapture::StageNameInputBox::isStageNameValid() {
    std::string name(m_buf);
    m_previousCaptureName = name;
    for (const char& curChar : m_invalidChars) {
      if (name.find(curChar) != std::string::npos) {
        return false;
      }
    }
    std::size_t fileExtensionPos = name.find(".");
    if (fileExtensionPos != std::string::npos) {
      return isInvalidKeywordUsed(name.substr(0, fileExtensionPos));
    }
    else {
      return isInvalidKeywordUsed(name);
    }
  }

  void ImGuiCapture::StageNameInputBox::setValue() {
    const auto& instanceStageNameRtxOpt = RtxOptions::Get()->m_captureInstanceStageName;
    const auto& timestampReplacementStr =
      RtxOptions::Get()->m_captureTimestampReplacement.getValue();
    std::string bufStr(m_buf);
    // Avoid displaying error message when capture name is changed from previous capture
    if (m_isCaptureNameInvalid && m_previousCaptureName != bufStr) {
      m_isCaptureNameInvalid = false;
    }
    if (bufStr.empty()) {
      instanceStageNameRtxOpt.getValue() = timestampReplacementStr + lss::ext::usd;
    } else {    
      const auto usdExtPos =
        bufStr.find(lss::ext::usd, bufStr.length() - lss::ext::usda.length() - 1);
      const std::string ext = (usdExtPos == std::string::npos) ? lss::ext::usd : "";
      instanceStageNameRtxOpt.getValue() = bufStr + ext;
    }
    m_timestampPos = instanceStageNameRtxOpt.getValue().find(timestampReplacementStr);
  }
  
  void ImGuiCapture::StageNameInputBox::replaceTimestampStr() {
    const std::time_t curTime = std::time(nullptr);
    std::tm locTime;
    // The vanilla versions of localtime are not thread safe, see:
    // https://en.cppreference.com/w/cpp/chrono/c/localtime
    localtime_s(&locTime, &curTime);
    static constexpr size_t kTimeStrLen = 19; // length of YYYY-MM-DD_HH-MM-SS
    const auto putTime = std::put_time(&locTime, "%Y-%m-%d_%H-%M-%S");
    
    auto& instanceStageNameStr = RtxOptions::Get()->m_captureInstanceStageName.getValue();
    const auto& timestampReplacementStr =
      RtxOptions::Get()->m_captureTimestampReplacement.getValue();
    const auto endTimestampPos = (m_focused) ?
      m_timestampPos + timestampReplacementStr.length() :
      m_timestampPos + kTimeStrLen;
      
    std::stringstream stageNameSS;
    stageNameSS << instanceStageNameStr.substr(0, m_timestampPos);
    stageNameSS << putTime;
    stageNameSS << instanceStageNameStr.substr(endTimestampPos);
    instanceStageNameStr = stageNameSS.str();
  }
  
  void ImGuiCapture::StageNameInputBox::show(const Rc<DxvkContext>& ctx) {
    auto& instanceStageNameRtxOpt = RtxOptions::Get()->m_captureInstanceStageName;
    const auto toolTip = str::format(
      instanceStageNameRtxOpt.getDescription(), '\n', '\n',
      instanceStageNameRtxOpt.getValue(), '\n', '\n', 
      "Invalid chars: ",  m_invalidChars, '\n', '\n',
      "Invalid keywords(case-insensitive): ", m_invalidKeywordDescription);

    ImGui::PushID(kImguiId);
    IMGUI_ADD_TOOLTIP(
      ImGui::InputText(" ", m_buf, kBufSize, ImGuiInputTextFlags_EnterReturnsTrue), toolTip.c_str());
    ImGui::PopID();
    m_focused = ImGui::IsItemFocused();
  }

  void ImGuiCapture::Progress::update(const Rc<DxvkContext>& ctx) {
    const auto& state = ctx->getCommonObjects()->capturer()->getState();
    if (state != m_prevState) {
      m_prevState = state;
      m_output.clear();
      if (state.has<GameCapturer::State::Complete>()) {
        const auto& completedCapture =
          ctx->getCommonObjects()->capturer()->queryCompleteCapture();
        m_captureStageName = completedCapture.stageName;
        m_capturePath = completedCapture.stagePath;
        m_percent = 1.f;
        const bool captureInstances = (RtxOptions::Get()->m_captureInstances.getValue());
        m_output.push_back((captureInstances) ? "Scene captured to:" : "Assets captured to:");
        const std::string destination =
          (captureInstances) ? m_captureStageName : util::RtxFileSys::path(util::RtxFileSys::Captures).string();
        m_output.push_back(destination);
        return;
      }
      if (state.has<GameCapturer::State::Initializing>()) {
        m_percent = 0.f;
        m_output.push_back("Initializing capture...");
        return;
      }
      m_output.push_back("Initialized!");
      if (state.has<GameCapturer::State::Capturing>()) {
        m_percent = 0.10f;
        m_output.push_back("Capturing...");
        return;
      }
      auto capturedOutput = (RtxOptions::Get()->m_captureInstances.getValue()) ?
        std::string("Scene captured!") : std::string("Assets captured!");
      m_output.push_back(capturedOutput.c_str());
      if (state.has<GameCapturer::State::PreppingExport>()) {
        m_percent = 0.5f;
        m_output.push_back("Prepping export to USD...");
        return;
      }
      m_output.push_back("Export prep complete!");
      if (state.has<GameCapturer::State::Exporting>()) {
        m_percent = 0.60f;
        m_output.push_back("Exporting to USD...");
        return;
      }
    }
  }
  
  void ImGuiCapture::Progress::show(const Rc<DxvkContext>& ctx) {
    ImGui::Text("Progress");
    static const ImVec4 barColor = ImVec4{ 0.268f, 0.42f, 0.03f, 1.0f };
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    ImGui::ProgressBar(m_percent);
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(ImGui::GetItemRectSize().x);
    for(const auto& outputLine : m_output) {
      ImGui::Text(outputLine.c_str());
    }
    if (m_prevState.has<GameCapturer::State::Complete>() &&
        ImGui::SmallButton("Copy Full Path")) {
      const bool captureInstances = (RtxOptions::Get()->m_captureInstances.getValue());
      const std::string toCopy =
        (captureInstances) ? m_capturePath : util::RtxFileSys::path(util::RtxFileSys::Captures).string();
      ImGui::SetClipboardText(toCopy.c_str());
    }
    ImGui::PopTextWrapPos();
  }

}
