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

#include "rtx_nsight_capture.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#ifndef DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
#define DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE 0
#endif

#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
#include <NGFX_GraphicsCapture_Vulkan.h>
#include <NGFX_Vulkan.h>
#endif

#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

namespace dxvk {
  namespace {
    bool s_initializeAttempted = false;
    bool s_available = false;
    bool s_frameBoundaryWarningLogged = false;
    std::atomic<uint32_t> s_pendingFramesToCapture = 0;
    std::mutex s_statusMutex;
    std::string s_statusText = "Nsight Graphics capture is disabled.";
    std::string s_lastResultText = "No capture requested.";

#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
    const char* resultToString(const NGFX_Result result) {
      switch (result) {
      case NGFX_Result_Success:
        return "Success";
      case NGFX_Result_NotImplemented:
        return "Not implemented";
      case NGFX_Result_LibNotFound:
        return "Library not found";
      case NGFX_Result_InvalidLib:
        return "Invalid library";
      case NGFX_Result_DifferentActivityInjected:
        return "Different activity injected";
      case NGFX_Result_InvalidParameter:
        return "Invalid parameter";
      case NGFX_Result_InvalidState:
        return "Invalid state";
      case NGFX_Result_UnspecifiedError:
        return "Unspecified error";
      case NGFX_Result_Timeout:
        return "Timeout";
      default:
        return "Unknown error";
      }
    }

    std::string resultDetails(const char* operation, const NGFX_Result result) {
      return str::format(operation, " returned ", resultToString(result), " (", static_cast<int32_t>(result), ")");
    }
#endif

    void setStatusText(std::string statusText) {
      std::lock_guard<std::mutex> lock(s_statusMutex);
      s_statusText = std::move(statusText);
    }

    void setLastResultText(std::string lastResultText) {
      std::lock_guard<std::mutex> lock(s_statusMutex);
      s_lastResultText = std::move(lastResultText);
    }

    std::string currentStatusText() {
      std::lock_guard<std::mutex> lock(s_statusMutex);
      return s_statusText;
    }
  }

  void NsightGraphicsCapture::initialize() {
    if (s_initializeAttempted) {
      return;
    }

    s_initializeAttempted = true;

    if (!graphicsCaptureEnabled()) {
      return;
    }

#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
    const std::string install = graphicsCaptureInstallPath();
    if (install.empty()) {
      const std::string statusText = "Nsight Graphics capture is enabled, but no installation path was configured.";
      setStatusText(statusText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", statusText, " Set rtx.nsight.graphicsCaptureInstallPath or DXVK_NSIGHT_GRAPHICS_PATH."));
      return;
    }

    const std::string captureOutputDir = graphicsCaptureOutputDir();
    if (!captureOutputDir.empty()) {
      env::createDirectory(captureOutputDir);
    }

    NGFX_SetLibraryLoadFn(NGFX_LoadLib_NoVerification);

    NGFX_GraphicsCapture_InjectionSettings settings = { NGFX_GraphicsCapture_InjectionSettings_VER };
    NGFX_GraphicsCapture_InjectionSettings_SetDefaults(&settings);

    const std::string captureOutputFile = graphicsCaptureOutputFile();
    settings.noHUD = !graphicsCaptureShowHud();
    settings.outputDir = captureOutputDir.empty() ? nullptr : captureOutputDir.c_str();
    settings.outputFile = captureOutputFile.empty() ? nullptr : captureOutputFile.c_str();
    settings.frameCount = clampFramesToCapture(graphicsCaptureFramesToCapture());
    settings.captureDefaultHotkey = false;
    settings.terminateAfterCapture = false;
    settings.noStreamlineCapture = true;

    const std::wstring installPathWide = str::tows(install.c_str());
    NGFX_GraphicsCapture_Inject_Vulkan_Params injectParams = { NGFX_GraphicsCapture_Inject_Vulkan_Params_VER };
    injectParams.installationPath = installPathWide.c_str();
    injectParams.settings = &settings;

    // If the DLL wasn't found, this call will fail
    NGFX_Result result = NGFX_GraphicsCapture_Inject_Vulkan(&injectParams);
    if (result != NGFX_Result_Success) {
      const std::string statusText = resultDetails("Injection", result);
      setStatusText(statusText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", statusText));
      return;
    }

    NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params initParams = { NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params_VER };
    result = NGFX_GraphicsCapture_InitializeActivity_Vulkan(&initParams);
    if (result != NGFX_Result_Success) {
      const std::string statusText = resultDetails("InitializeActivity", result);
      setStatusText(statusText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", statusText));
      return;
    }

    s_available = true;
    setStatusText("Nsight Graphics capture is available.");
    Logger::info(str::format("[Nsight Graphics Capture] Self-injection initialized. Installation directory: ", install, ". Output directory: ", captureOutputDir));
#else
    const std::string statusText = "Nsight Graphics capture is not supported by this build.";
    setStatusText(statusText);
    Logger::warn(str::format("[Nsight Graphics Capture] ", statusText));
#endif
  }

  bool NsightGraphicsCapture::isAvailable() {
    return s_available;
  }

  uint32_t NsightGraphicsCapture::clampFramesToCapture(uint32_t framesToCapture) {
    return std::clamp(framesToCapture, kMinFramesToCapture, kMaxFramesToCapture);
  }

  void NsightGraphicsCapture::requestGraphicsCapture(uint32_t framesToCapture) {
#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
    if (!s_available) {
      const std::string lastResultText = "Capture unavailable. " + currentStatusText();
      setLastResultText(lastResultText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", lastResultText));
      return;
    }

    framesToCapture = clampFramesToCapture(framesToCapture);
    graphicsCaptureFramesToCapture.setDeferred(framesToCapture);
    s_pendingFramesToCapture.store(framesToCapture);

    const std::string lastResultText = str::format("Capture request queued after the current frame boundary. Frames: ", framesToCapture);
    setLastResultText(lastResultText);
    Logger::info(str::format("[Nsight Graphics Capture] ", lastResultText));
#else
    const std::string lastResultText = "Capture unavailable. Nsight Graphics capture is not supported by this build.";
    setLastResultText(lastResultText);
    Logger::warn(str::format("[Nsight Graphics Capture] ", lastResultText));
#endif
  }

  void NsightGraphicsCapture::signalFrameBoundary(VkQueue queue) {
#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
    if (!s_available) {
      return;
    }

    NGFX_FrameBoundary_Vulkan_Params params = { NGFX_FrameBoundary_Vulkan_Params_VER };
    params.queue = queue;

    const NGFX_Result result = NGFX_FrameBoundary_Vulkan(&params);
    if (result != NGFX_Result_Success && !s_frameBoundaryWarningLogged) {
      s_frameBoundaryWarningLogged = true;
      Logger::warn(str::format("[Nsight Graphics Capture] FrameBoundary returned ", resultToString(result), " (", static_cast<int32_t>(result), ")"));
    }
#else
    (void) queue;
#endif
  }

  void NsightGraphicsCapture::processPendingCaptureRequest() {
#if DXVK_ENABLE_NSIGHT_GRAPHICS_CAPTURE
    const uint32_t framesToCapture = s_pendingFramesToCapture.exchange(0);
    if (framesToCapture == 0) {
      return;
    }

    if (!s_available) {
      const std::string lastResultText = "Capture unavailable. " + currentStatusText();
      setLastResultText(lastResultText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", lastResultText));
      return;
    }

    NGFX_GraphicsCapture_RequestCapture_Vulkan_Params params = { NGFX_GraphicsCapture_RequestCapture_Vulkan_Params_VER };
    params.delimiter = NGFX_GraphicsCapture_Delimiter_FrameBoundary;
    params.framesBeforeStart = 0;
    params.framesToCapture = framesToCapture;

    const NGFX_Result result = NGFX_GraphicsCapture_RequestCapture_Vulkan(&params);
    std::string lastResultText = resultDetails("RequestCapture", result);
    if (result == NGFX_Result_Success) {
      lastResultText = str::format(lastResultText, ". Capture starts at the next frame boundary. Presented frames: ", framesToCapture);
      setLastResultText(lastResultText);
      Logger::info(str::format("[Nsight Graphics Capture] ", lastResultText));
    } else {
      setLastResultText(lastResultText);
      Logger::warn(str::format("[Nsight Graphics Capture] ", lastResultText));
    }
#else
    s_pendingFramesToCapture.store(0);
#endif
  }

  std::string NsightGraphicsCapture::statusText() {
    std::lock_guard<std::mutex> lock(s_statusMutex);
    return s_statusText;
  }

  std::string NsightGraphicsCapture::lastResultText() {
    std::lock_guard<std::mutex> lock(s_statusMutex);
    return s_lastResultText;
  }

}
