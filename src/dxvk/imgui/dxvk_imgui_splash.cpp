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

#include <cstdint>
#include <cassert>

#include "imgui.h"
#include "../rtx_render/rtx_options.h"
#include "../rtx_render/rtx_bridge_message_channel.h"
#include "../rtx_render/rtx_utils.h"
#include "../util/util_math.h"

namespace dxvk {
  struct SplashSettings {
    RTX_OPTION_ENV("rtx", bool, hideSplashMessage, false, "RTX_HIDE_SPLASH_MESSAGE",
           "A flag to disable the splash message indicating how to use Remix from appearing when the application starts.\n"
           "When set to true this message will be hidden, otherwise it will be displayed on every launch.");
    // Note: 20 chosen as a default here to allow the message to persist long enough to read in case the user focuses on other information on the screen first (e.g.
    // shader compilation messages, text from the application itself in its startup sequence, etc).
    RTX_OPTION("rtx", std::uint32_t, splashMessageDisplayTimeSeconds, 20, "The amount of time in seconds to display the Remix splash message for.");
    RTX_OPTION("rtx", std::string, welcomeMessage, "", "Display a message to the user on startup, leave empty if no message is to be displayed.");
  };

  void ImGuiSplash::update(ImFont* largeFont) {
    using namespace std::chrono;

    // Should we show the splash message?  Don't if hidden, or if UI already active
    if (!m_hasStarted && !SplashSettings::hideSplashMessage() && RtxOptions::Get()->showUI() == UIType::None) {
      // No need to start again
      m_hasStarted = true;

      // Record the start time
      m_startTime = system_clock::now();
    }

    const auto elapsedDuration = system_clock::now() - m_startTime;
    const auto elapsedMilliseconds = duration_cast<milliseconds>(elapsedDuration).count();
    const auto elapsedSeconds = duration_cast<seconds>(elapsedDuration).count();

    assert(elapsedSeconds >= 0);

    if (static_cast<std::uint32_t>(elapsedSeconds) <= SplashSettings::splashMessageDisplayTimeSeconds()) {
      // Show the user the time remaining
      // Note: Clamped to ensure the count does not go negative for a frame (as ImGui does not respond
      // to a close request from within an open popup on the same frame).
      const auto clampedSecondsRemaining = std::max(SplashSettings::splashMessageDisplayTimeSeconds() - elapsedSeconds, 0ll);

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
        std::string message = str::format("Welcome to RTX Remix. Use ", keyBindDescriptor, " to access the RTX Remix Menu and change settings. Closing in ", clampedSecondsRemaining);
        ImGui::Text(message.c_str());
      }
      ImGui::End();

      ImGui::PopStyleColor();
      ImGui::PopFont();

      if (!SplashSettings::welcomeMessage().empty()) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(340.f, 120.f), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x / 2 - 170, viewport->Size.y / 2 - 60), ImGuiCond_Always);
        if (ImGui::Begin("Welcome Message", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
          std::string message = str::format(SplashSettings::welcomeMessage(), " -- Closing in ", clampedSecondsRemaining);
          ImGui::TextWrapped(message.c_str());
        }
        ImGui::End();
      }
    }
  }

}
