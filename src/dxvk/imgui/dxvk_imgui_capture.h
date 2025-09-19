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

#include "../rtx_render/rtx_options.h"
#include "../rtx_render/rtx_game_capturer.h"

#include "../../util/rc/util_rc.h"
#include "../../util/rc/util_rc_ptr.h"

#include "imgui.h"

#include <chrono>

namespace dxvk {
  class DxvkContext;
  struct State;

  class ImGuiCapture : public RcObject {
  public:
    ImGuiCapture(dxvk::ImGUI* const masterImGUui)
      : m_masterImGui(masterImGUui) {}
    ImGuiCapture(const ImGuiCapture& other) = delete;
    ImGuiCapture(const ImGuiCapture&& other) = delete;
    void update(const Rc<DxvkContext>& ctx);
    void show(const Rc<DxvkContext>& ctx);
    
  private:
    friend ImGUI;

    void showSceneCapture(const Rc<DxvkContext>& ctx);
    void showTimedCapture(const Rc<DxvkContext>& ctx);
    void showContinuousCapture(const Rc<DxvkContext>& ctx);

    ImGUI* const m_masterImGui = nullptr;

    class StageNameInputBox {
    public:
      StageNameInputBox();
      void update(const Rc<DxvkContext>& ctx);
      void show(const Rc<DxvkContext>& ctx);
      void validateStageName();
      bool m_isCaptureNameInvalid = false;
      bool isStageNameValid();
    private:
      void setValue();
      void replaceTimestampStr();
      bool isInvalidKeywordUsed(std::string name);
      static constexpr size_t kBufSize = 64;
      static constexpr char kImguiId[] = "capture_stage_name_input";
      char m_buf[kBufSize];
      std::string m_previousCaptureName = "";
      std::string_view m_invalidChars = "/<>:\"/\\|?*";
      std::vector<std::string> m_invalidKeywords = { "CON", "PRN", "AUX", "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
      std::string_view m_invalidKeywordDescription = "CON, PRN, AUX, NUL, COM[1-9], LPT[1-9]";
      bool m_focused = false;
    } m_stageNameInputBox;

    class Progress {
      void update(const Rc<DxvkContext>& ctx);
      void show(const Rc<DxvkContext>& ctx);
    private:
      friend ImGuiCapture;
      float m_percent;
      GameCapturer::State m_prevState;
      std::string m_captureStageName = "";
      std::string m_capturePath = "";
      std::vector<std::string> m_output;
    } m_progress;
  };
}
