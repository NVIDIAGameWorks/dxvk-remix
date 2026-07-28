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

// Shared crash-reporting definitions used by both the RTX Remix runtime and the 32-bit bridge, so
// the two stay in sync on how crashes are signaled and how the crash-report dialog is worded.

#include <cstdint>
#include <string>

namespace dxvk {
  // Process exit code the runtime uses when it terminates itself after gracefully handling a GPU
  // crash (device loss) via env::killProcess(). The bridge client reads the server process's exit
  // code to distinguish a GPU crash (handled by the runtime) from a CPU crash (abrupt termination
  // caught by crashpad) or a normal exit, so it can show an appropriate dialog.
  static constexpr uint32_t kRemixGpuCrashExitCode = 0x60D0DEADu;

  // Named window message sent by the runtime to the bridge client immediately when a GPU crash is
  // detected — before the slow Sentry capture and upload. The bridge client registers a handler
  // via gpRemixMessageChannel so it can disable the Windows ghost dialog and prepare the crash
  // dialog window as early as possible, without waiting for the server process to exit.
  static constexpr const char* kGpuCrashNotifyMsgName = "UWM_REMIX_GPU_CRASH_NOTIFY";

  // Title used for all crash-report dialogs.
  static constexpr const wchar_t* kCrashReportDialogTitle = L"RTX Remix Crash Report";

  // Selects the opening line of the crash-report dialog.
  enum class CrashPromptKind {
    CpuImmediate,   // CPU crash, dialog shown at crash time (bridge server exit).
    CpuNextLaunch,  // Crash detected on the next launch (runtime, no bridge).
    GpuImmediate,   // GPU crash, dialog shown at crash time.
  };

  // Builds the crash-report dialog message from shared parts so the runtime and bridge produce
  // identical wording. \p hasPermission selects the "will upload" vs. "ask for permission" text.
  inline std::wstring buildCrashReportMessage(CrashPromptKind kind, bool hasPermission) {
    std::wstring message;

    // Part 1: what happened.
    switch (kind) {
    case CrashPromptKind::CpuImmediate:
      message = L"The RTX Remix Runtime has encountered an unexpected issue. The application will close.\n\n";
      break;
    case CrashPromptKind::CpuNextLaunch:
      message = L"The RTX Remix Runtime crashed last time.\n\n";
      break;
    case CrashPromptKind::GpuImmediate:
      message = L"The RTX Remix Runtime has encountered a GPU Crash. The application will close.\n\n";
      break;
    }

    // Part 2: upload intent / consent request.
    if (hasPermission) {
      message += L"A crash report will be uploaded to help improve Remix. ";
    } else {
      message += L"Would you like to help improve Remix by granting permission to upload crash data?\n\n";
    }

    // Standard body.
    message +=
      L"Data collected will be used in compliance with NVIDIA's Privacy Policy. "
      L"Permission can be changed at any time in the User Menu > Permissions tab.\n\n"
      L"If you can reliably reproduce this crash, please create an issue on our GitHub:\n"
      L"https://github.com/NVIDIAGameWorks/rtx-remix/issues";

    // GPU crashes benefit from an Aftermath dump; nudge the user to enable it for next time.
    if (kind == CrashPromptKind::GpuImmediate) {
      message +=
        L"\n\nAdding 'dxvk.enableAftermath = True' to dxvk.conf will include additional information"
        L" in future GPU crash reports, but may have a small performance overhead.";
    }

    return message;
  }
}
