/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software and to permit persons to whom the
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

#include <string>
#include <vector>

namespace dxvk {

  // Utility functions for Sentry crash reporting.
  // Permission is managed via LocalData::app() (stored in %LOCALAPPDATA%) and synced to Sentry's
  // internal consent file so the bridge can read it directly. Change permission via
  // LocalData::app().set(sentry::kAllowCrashReportingKey, bool), save, then call
  // syncConsentFromUserSettings().
  // Sentry is only included in util_sentry.cpp so this header is safe to use from any target (e.g. libdxvk).
  namespace sentry {

    // UserSettings key for crash reporting permission.
    static constexpr const char* kAllowCrashReportingKey = "allowCrashReporting";

    // UserSettings key for usage tracking permission.
    static constexpr const char* kAllowUsageReportingKey = "allowUsageTracking";

    // Initialize Sentry. Call once at startup, after determining the database path.
    void initialize(const char* version);

    // If the previous run crashed, show a dialog informing the user. Returns true if
    // permission was granted (either previously or via the dialog).
    bool showCrashReportDialogIfNeeded();

    // Shut down Sentry and flush pending data. Call before process exit when possible.
    void shutdown();

    // Sync Sentry's internal consent state from the UserSettings crash reporting permission.
    // Call after changing the permission in UserSettings and saving.
    void syncConsentFromUserSettings();

    // Add a breadcrumb: a step in the trail of "what the app did recently."
    // Breadcrumbs are stored locally (ring buffer, ~100) and are not sent by themselves.
    // They are attached to the next crash when it is sent.
    // Level: -2=trace, -1=debug, 0=info, 1=warning, 2=error, 3=fatal (Sentry sentry_level_e).
    void addBreadcrumb(const std::string& category, const std::string& message, int level = 0);

    // Set a tag on the current scope. Tags are indexed and searchable in Sentry—use for dimensions
    // you want to filter or group by (e.g. gpu_vendor, game, api_level). Keep keys and values short and stable.
    void setTag(const std::string& key, const std::string& value);

    // Set custom context on the current scope. Shown on the event details page but not used as filters—
    // use for debugging payload (longer text, device strings, config). Use setTag when you need to search or group by the value.
    void setContext(const std::string& key, const std::string& value);

    // Set Vulkan instance layers and extensions as structured context, and set overlay tags from the names.
    // Checks for Steam overlay (e.g. VK_LAYER_*_steam_overlay) and other overlays (ReShade, RenderDoc, etc.).
    void setVulkanLayersAndExtensionsContext(const std::vector<std::string>& layerNames,
                                            const std::vector<std::string>& extensionNames);

    // Helper-process entry point: init Sentry with the given database path (e.g. logsDir + "/.sentry-native"),
    // grant consent, and close so pending reports are flushed. Handler path is derived from the
    // calling module (d3d9). Used by the bridge by loading d3d9 from the upload-only helper process.
    // crashType ("GPU"/"CPU"/"unknown") is recorded in the crash-upload.log header for diagnostics.
    void runUploadHelper(const char* sentryDatabasePathUtf8, const char* crashType);

    // Finish the "App Startup" transaction that was started during initialize(). Call once when
    // the first frame is presented. Measures time from Sentry init to first present. Safe to call
    // multiple times; only the first call has an effect.
    void onFirstFrame();

    // Queue a GPU crash report path from the Aftermath callback after writing the dump file.
    // The path must be UTF-8. processPendingGpuCrashReports() is called from the submission
    // queue thread once Aftermath finishes (the render thread is typically frozen by then).
    void queueGpuCrashReport(const char* dumpFilePathUtf8);

    // Capture and upload (or cache) any GPU crash report queued by queueGpuCrashReport(). Returns
    // after reporting; the caller is responsible for terminating the process afterward.
    void processPendingGpuCrashReports();

  }

}
