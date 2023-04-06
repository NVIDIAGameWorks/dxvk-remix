/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "dxvk_imgui_splash.h"

#include "imgui.h"
#include "../rtx_render/rtx_options.h"
#include "../rtx_render/rtx_bridgemessagechannel.h"
#include "../rtx_render/rtx_utils.h"
#include "../util/util_math.h"

namespace dxvk {

  void ImGuiSplash::update(ImFont* largeFont) {
    using namespace std::chrono;

    // Should we show the splash message?  Don't if hidden, or if UI already active
    if (!m_hasStarted && !RtxOptions::Get()->hideSplashMessage() && RtxOptions::Get()->showUI() == UIType::None) {
      // No need to start again
      m_hasStarted = true;

      // Record the start time
      m_startTime = system_clock::now();
    }

    const auto elapsedDuration = system_clock::now() - m_startTime;
    const int elapsedMilliseconds = duration_cast<milliseconds>(elapsedDuration).count();
    const int elapsedSeconds = duration_cast<seconds>(elapsedDuration).count();

    if (elapsedSeconds <= m_timeToLiveSeconds) {
      // Show the user the time remaining
      // Note: Clamped to ensure the count does not go negative for a frame (as ImGui does not respond
      // to a close request from within an open popup on the same frame).
      const int clampedSecondsRemaining = std::max(m_timeToLiveSeconds - elapsedSeconds, 0);

      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always, ImVec2(0.0f, 0.0f));

      // Note: If largeFont is NULL (as it may be if the font has not loaded yet) this will default to the default font.
      // Large font used to make this more visible as it is important users understand how to access the rendering settings
      // to adjust performance/quality to their desires.
      ImGui::PushFont(largeFont);

      // Note: pi-based scalar used to align cycles with seconds countdown nicely.
      const float pulseInterpolationFactor = (std::cos(elapsedMilliseconds / 1000.0f * kPi / 2.0f) + 1.0f) / 2.0f;

      ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(
        // Note: Darker variant of roughly-NVIDIA green to have good contrast against white text.
        lerp(0.15f, 0.268f, pulseInterpolationFactor),
        lerp(0.15f, 0.42f, pulseInterpolationFactor),
        lerp(0.15f, 0.03f, pulseInterpolationFactor),
        lerp(0.8f, 0.95f, pulseInterpolationFactor)
      ));

      if (ImGui::Begin("Splash Message", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
        const auto keyBindDescriptor = buildKeyBindDescriptorString(RtxOptions::Get()->remixMenuKeyBinds());
        std::string message = str::format("Welcome to NVIDIA RTX Remix.  At any point during gameplay press : ", keyBindDescriptor, " to access the Remix Menu.  Closing in ", clampedSecondsRemaining);
        ImGui::Text(message.c_str());
      }
      ImGui::End();

      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

}
