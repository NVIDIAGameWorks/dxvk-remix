/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <string>

#include "../dxvk_include.h"

#include "rtx_options.h"

#ifndef DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
#define DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE 0
#endif

namespace dxvk {

  class NsightGraphicsCapture {
  public:
    static constexpr uint32_t kMinFramesToCapture = 1;
    static constexpr uint32_t kMaxFramesToCapture = 60;

    static void initialize();
    static bool isAvailable();
    static uint32_t clampFramesToCapture(uint32_t framesToCapture);
    static void requestGraphicsCapture(uint32_t framesToCapture);
    static void signalFrameBoundary(VkQueue queue);
    static void processPendingCaptureRequest();

    static std::string statusText();
    static std::string lastResultText();

    RTX_OPTION_ENV("rtx.nsight", bool, graphicsCaptureEnabled, false, "DXVK_NSIGHT_GRAPHICS_CAPTURE",
                   "Enables Nsight Graphics self-injection for programmatic graphics captures. Changes take effect after saving rtx.conf and reloading.");
    RTX_OPTION_ENV("rtx.nsight", std::string, graphicsCaptureInstallPath, "", "DXVK_NSIGHT_GRAPHICS_PATH",
                   "Path to the Nsight Graphics installation directory used for self-injection. Changes take effect after saving rtx.conf and reloading.");
    RTX_OPTION_ENV("rtx.nsight", std::string, graphicsCaptureOutputDir, "nsight-captures", "DXVK_NSIGHT_GRAPHICS_OUTPUT_DIR",
                   "Directory where injected Nsight Graphics captures will be written. Changes take effect after saving rtx.conf and reloading.");
    RTX_OPTION_ENV("rtx.nsight", std::string, graphicsCaptureOutputFile, "", "DXVK_NSIGHT_GRAPHICS_OUTPUT_FILE",
                   "Optional output capture file path. If empty, Nsight Graphics chooses the capture file name. Changes take effect after saving rtx.conf and reloading.");
    RTX_OPTION_FULL("rtx.nsight", uint32_t, graphicsCaptureFramesToCapture, 1, environment, 0,
                    "Number of presented frames to capture when triggering Nsight Graphics capture, including DLFG interpolated frames. Valid values: 1 through 60.",
                    args.environment = "DXVK_NSIGHT_GRAPHICS_CAPTURE_FRAMES",
                    args.minValue = kMinFramesToCapture,
                    args.maxValue = kMaxFramesToCapture);
    RTX_OPTION_ENV("rtx.nsight", bool, graphicsCaptureShowHud, false, "DXVK_NSIGHT_GRAPHICS_SHOW_HUD",
                   "Shows the Nsight Graphics HUD when self-injected capture is enabled. Changes take effect after saving rtx.conf and reloading.");
  };

}
