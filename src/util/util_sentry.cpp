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

#include "util_sentry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <windows.h>

#include "log/log.h"
#include "util_env.h"
#include "util_filesys.h"
#include "util_string.h"
#include "util_local_data.h"
#include "util_crash_report.h"

#ifndef REMIX_SENTRY_ENABLED
#define REMIX_SENTRY_ENABLED 1
#endif

// Sentry is disabled in debug builds, as the prebuilt Release Sentry/Crashpad will fail to link in debug builds;
#if REMIX_SENTRY_ENABLED

#define SENTRY_BUILD_STATIC 1
#include <sentry.h>

// When the build sets -Dremix_sentry_environment= (e.g. CI), that value is used. Otherwise undefined and we use "development" here.
#ifndef REMIX_SENTRY_ENVIRONMENT
#define REMIX_SENTRY_ENVIRONMENT "development"
#endif

namespace dxvk {
namespace sentry {

  namespace {

    static const char* const SENTRY_DSN = "https://24ea109fd3c0225c42518a7b1ecd636a@o4505230501543936.ingest.us.sentry.io/4510796067176448";

    // Max time (ms) to wait for Sentry to upload a GPU crash report (a FATAL event plus the
    // multi-MB Aftermath dump attachment) before the crashed process is killed. sentry_flush()
    // returns as soon as the upload completes, so this is only an upper bound - but it must be
    // generous: the process exits immediately afterward, and any envelope not sent in time is left
    // in the offline cache, which the retry poll does not resend (it skips offline entries).
    static constexpr unsigned int kSentryFlushTimeoutMs = 30000;

    // The upload helper may need to send a multi-MB minidump plus log attachments.
    static constexpr unsigned int kSentryUploadHelperFlushTimeoutMs = 30000;

    // User ID constants
    // Store the user ID in local data so it stays constant over time.
    static constexpr const char* kSentryUserIdKey = "sentryUserId";

    // sentry_close() is not idempotent; guard so we only call it once (we can get both WM_DESTROY and WM_ENDSESSION).
    static std::atomic<bool> s_shutdownDone{ false };
    static std::atomic<bool> s_sentryInitialized{ false };

    static std::atomic<sentry_transaction_t*> s_startupTransaction{ nullptr };
    static std::mutex s_sentryMutex;
    static std::mutex s_gpuCrashQueueMutex;
    static std::string s_pendingGpuCrashPath;

    // Per-session correlation id, attached as the "session_id" tag on crash reports. Stored so it
    // can be restored after being temporarily removed while the usage-tracking transaction is sent
    // (crash reports must not be linkable to usage tracking).
    static constexpr const char* kSessionIdTag = "session_id";
    static std::string s_sessionId;

    bool isSentryActive() {
      return s_sentryInitialized.load(std::memory_order_acquire)
        && !s_shutdownDone.load(std::memory_order_acquire);
    }

    std::string pathToUtf8(const std::filesystem::path& path) {
      return str::fromws(path.wstring().c_str());
    }

    bool isAllowedSentryDatabasePath(const char* sentryDatabasePathUtf8) {
      if (sentryDatabasePathUtf8 == nullptr || sentryDatabasePathUtf8[0] == '\0') {
        return false;
      }
      const std::string pathStr(sentryDatabasePathUtf8);
      if (pathStr.find("..") != std::string::npos) {
        return false;
      }
      const std::filesystem::path path(str::tows(sentryDatabasePathUtf8));
      if (path.filename() != L".sentry-native") {
        return false;
      }
      return std::filesystem::exists(path);
    }

    bool isAutomationMode() {
      static const bool s_result = [] {
        const char* disableDialogs = std::getenv("RTX_AUTOMATION_DISABLE_BLOCKING_DIALOG_BOXES");
        return disableDialogs && std::string(disableDialogs) == "1";
      }();
      return s_result;
    }

    // Set options shared by initialize() and runUploadHelper(). Caller must call sentry_init(options);
    // string args must outlive that call.
    void setSentryOptionsCommon(sentry_options_t* options,
        const char* databasePathUtf8,
        const char* handlerPathUtf8) {
      sentry_options_set_database_path(options, databasePathUtf8);
      sentry_options_set_handler_path(options, handlerPathUtf8);
      // RTX_SENTRY_TEST_DSN lets tests redirect sentry to an unreachable endpoint so that
      // envelopes are cached on disk rather than sent, making post-test inspection possible.
      const char* testDsn = std::getenv("RTX_SENTRY_TEST_DSN");
      sentry_options_set_dsn(options, testDsn ? testDsn : SENTRY_DSN);

      // Metrics and structured logs default to ON in sentry-native >= 0.14. Remix crash reporting
      // is strictly opt-in crash data only, so disable both.
      sentry_options_set_enable_metrics(options, 0);
      sentry_options_set_enable_logs(options, 0);

      // Privacy hardening: disable everything that is not crash/usage data Remix explicitly sends.
      // auto_session_tracking: sends a session-health envelope on every launch/exit silently; Remix
      // never calls sentry_start_session(), so this tracking is unintentional.
      sentry_options_set_auto_session_tracking(options, 0);
      // attach_screenshot: off by default but explicit — a game screenshot may contain sensitive content.
      sentry_options_set_attach_screenshot(options, 0);
      // send_client_reports: SDK-internal discard-reason telemetry; not something Remix opted into.
      sentry_options_set_send_client_reports(options, 0);

      // Consent-aware offline caching (sentry-native >= 0.14 crashpad support): if a crash is
      // captured while consent is not yet granted, the report is cached as an envelope instead of
      // being discarded, and with http_retry it is sent automatically once consent is given. This
      // is what lets a user grant permission AFTER a crash and still have that crash uploaded.
      sentry_options_set_cache_keep(options, SENTRY_CACHE_KEEP_OFFLINE);
      sentry_options_set_http_retry(options, 1);
    }

    // Logging for the upload helper. Writes to crash-upload.log next to the other logs so
    // it survives game relaunches (bridge64.log is truncated on each launch).
    std::mutex s_uploadLogMutex;
    FILE* s_uploadLogFile = nullptr;

    void uploadHelperLogger(sentry_level_t level, const char* message, va_list args, void* userdata) {
      std::lock_guard<std::mutex> lock(s_uploadLogMutex);
      FILE* file = static_cast<FILE*>(userdata);
      if (file == nullptr) {
        return;
      }
      const char* levelStr = "info";
      switch (level) {
      case SENTRY_LEVEL_DEBUG:   levelStr = "debug"; break;
      case SENTRY_LEVEL_WARNING: levelStr = "warn"; break;
      case SENTRY_LEVEL_ERROR:   levelStr = "error"; break;
      case SENTRY_LEVEL_FATAL:   levelStr = "fatal"; break;
      default: break;
      }
      fprintf(file, "[sentry %s] ", levelStr);
      vfprintf(file, message, args);
      fprintf(file, "\n");
      fflush(file);
    }

    // Sentry has no public API to delete pending crash reports when the user declines/revokes
    // consent. sentry_user_consent_revoke() only disables uploads, and with offline caching enabled
    // a declined crash would otherwise sit on disk and get uploaded if consent is granted later. So
    // when the user clicks "No" we delete the crash data ourselves: the crashpad reports and
    // attachments, the offline envelope cache, and the per-run session dirs. We preserve the
    // database identity and consent files (settings.dat, user-consent, installation_id).
    void clearPendingCrashReports() {
      const auto sentryDbDir = util::RtxFileSys::path(util::RtxFileSys::Logs) / ".sentry-native";
      if (!std::filesystem::exists(sentryDbDir)) {
        return;
      }
      std::error_code ec;
      bool anyFailed = false;

      for (const char* subDir : { "reports", "attachments", "cache" }) {
        std::filesystem::remove_all(sentryDbDir / subDir, ec);
        if (ec) {
          anyFailed = true;
          ec.clear();
        }
      }

      std::vector<std::filesystem::path> runPaths;
      for (auto it = std::filesystem::directory_iterator(sentryDbDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const auto name = it->path().filename().string();
        if ((name.size() >= 4 && name.compare(name.size() - 4, 4, ".run") == 0)
            || (name.size() >= 9 && name.compare(name.size() - 9, 9, ".run.lock") == 0)) {
          runPaths.push_back(it->path());
        }
      }
      for (const auto& runPath : runPaths) {
        std::filesystem::remove_all(runPath, ec);
        if (ec) {
          anyFailed = true;
          ec.clear();
        }
      }

      if (anyFailed) {
        const std::string msg = "Failed to remove some pending crash reports. You can manually delete the folder:\n\n" + sentryDbDir.string();
        MessageBoxW(nullptr, str::tows(msg.c_str()).c_str(), L"RTX Remix Crash Report", MB_OK | MB_ICONWARNING);
      }
    }

    void setContextList(const std::string& contextKey, const std::string& listKey, const std::vector<std::string>& values) {
      sentry_value_t list = sentry_value_new_list();
      for (const auto& s : values) {
        sentry_value_append(list, sentry_value_new_string(s.c_str()));
      }
      sentry_value_t context = sentry_value_new_object();
      sentry_value_set_by_key(context, listKey.c_str(), list);
      sentry_set_context(contextKey.c_str(), context);
    }

    std::string toLower(std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
      return s;
    }

    std::string generateRandomId() {
      // 64 bits keeps collision probability under 1% up to ~600 million users
      // (birthday problem: p ≈ n² / 2^65).
      std::random_device random;
      uint64_t value = (uint64_t(random()) << 32) | random();
      char buf[17];
      snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(value));
      return std::string(buf);
    }

    std::string getUserId() {
      static std::once_flag s_once;
      static std::string s_userId;
      std::call_once(s_once, []() {
        std::string storedId = LocalData::shared().get<std::string>(kSentryUserIdKey, std::string());
        if (!storedId.empty()) {
          s_userId = std::move(storedId);
          return;
        }
        s_userId = generateRandomId();
        LocalData::shared().set(kSentryUserIdKey, s_userId);
        LocalData::shared().save();
      });
      return s_userId;
    }

    bool userAllowsCrashReporting() {
      return isAutomationMode()
        || LocalData::app().get<bool>(kAllowCrashReportingKey, false);
    }

    bool promptGpuCrashConsent() {
      if (isAutomationMode()) {
        return true;
      }
      if (env::isRemixBridgeActive()) {
        // The bridge client shows the combined crash dialog when the server exits.
        return LocalData::app().get<bool>(kAllowCrashReportingKey, false);
      }
      const bool hasPermission = LocalData::app().get<bool>(kAllowCrashReportingKey, false);
      const std::wstring message = buildCrashReportMessage(CrashPromptKind::GpuImmediate, hasPermission);
      if (hasPermission) {
        MessageBoxW(nullptr, message.c_str(), kCrashReportDialogTitle, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return true;
      }
      const int result = MessageBoxW(nullptr, message.c_str(), kCrashReportDialogTitle, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST);
      if (result != IDYES) {
        return false;
      }
      LocalData::app().set(kAllowCrashReportingKey, true);
      LocalData::app().save();
      return true;
    }

    void syncConsentFromUserSettingsUnlocked() {
      if (LocalData::app().get<bool>(kAllowCrashReportingKey, false)) {
        sentry_user_consent_give();
      } else {
        sentry_user_consent_revoke();
      }
    }

    void finishStartupTransaction() {
      if (!s_sentryInitialized.load(std::memory_order_acquire)) {
        return;
      }

      sentry_transaction_t* tx = s_startupTransaction.exchange(nullptr, std::memory_order_acq_rel);
      if (!tx) {
        return;
      }

      std::lock_guard<std::mutex> lock(s_sentryMutex);
      if (!s_sentryInitialized.load(std::memory_order_acquire)) {
        sentry_transaction_finish(tx);
        return;
      }
      // Shutdown started while we were taking the transaction; finish without telemetry.
      if (s_shutdownDone.load(std::memory_order_acquire)) {
        sentry_transaction_finish(tx);
        return;
      }

      if (!LocalData::app().get<bool>(kAllowUsageReportingKey, false)) {
        sentry_transaction_finish(tx);
        return;
      }

      const bool crashAllowed = userAllowsCrashReporting();
      if (crashAllowed) {
        sentry_user_consent_give();
      }

      // User ID is for usage tracing only; attach it just long enough to finish the transaction.
      const std::string userId = getUserId();
      if (!userId.empty()) {
        sentry_value_t user = sentry_value_new_user(userId.c_str(), nullptr, nullptr, nullptr);
        sentry_set_user(user);
      }

      // Crash reports must not be linkable to usage tracking, so drop the crash-only session_id tag
      // while this usage-tracking transaction is sent, then restore it for subsequent crash events.
      sentry_remove_tag(kSessionIdTag);
      sentry_transaction_finish(tx);
      sentry_remove_user();
      if (!s_sessionId.empty()) {
        sentry_set_tag(kSessionIdTag, s_sessionId.c_str());
      }

      if (!isAutomationMode()) {
        syncConsentFromUserSettingsUnlocked();
      }
    }

    // consentGiven reflects whether crash-report upload is permitted right now. When false (e.g. a
    // bridge crash before the user has answered the bridge's consent popup) the event is still
    // captured, but require_user_consent keeps it in the offline cache instead of sending it; the
    // bridge upload helper sends it later once consent is granted.
    void captureGpuCrashReport(const char* dumpFilePathUtf8, bool consentGiven) {
      // The Aftermath dump is optional: it only exists when Aftermath is enabled and captured the
      // crash. Without it we still report the GPU crash (event + tags + attached logs), just without
      // the dump attachment.
      const bool hasDump = dumpFilePathUtf8 != nullptr && dumpFilePathUtf8[0] != '\0';
      sentry_value_t event = sentry_value_new_message_event(
          SENTRY_LEVEL_FATAL, "gpu",
          hasDump ? "GPU crash (NVIDIA Aftermath)" : "GPU crash (no Aftermath dump)");
      sentry_scope_t* scope = sentry_local_scope_new();
      sentry_scope_set_tag(scope, "crash_type", "gpu");
      if (hasDump) {
        const std::wstring pathW = str::tows(dumpFilePathUtf8);
        sentry_scope_attach_filew(scope, pathW.c_str());
      }
      const sentry_uuid_t eventId = sentry_capture_event_with_scope(event, scope);
      char eventIdStr[37] = {};
      sentry_uuid_as_string(&eventId, eventIdStr);
      Logger::info(str::format("Sentry GPU crash event id: ", eventIdStr, hasDump ? " (with dump)" : " (no dump)"));
      // Must fully upload here: the process is killed right after, and a GPU crash event is sent via
      // the SDK transport (not crashpad), so an unsent envelope would only land in the offline cache.
      const int flushResult = sentry_flush(kSentryFlushTimeoutMs);
      if (!consentGiven) {
        Logger::info("Sentry GPU crash: captured and cached (crash reporting not yet permitted; will upload if consent is granted)");
      } else if (flushResult == 0) {
        Logger::info("Sentry GPU crash: uploaded");
      } else {
        Logger::warn("Sentry GPU crash: upload flush timed out; left in offline cache for retry");
      }
    }

  }  // namespace

  void initialize(const char* version) {
    if (s_sentryInitialized.load(std::memory_order_acquire)) {
      return;
    }

    const auto dllDir = std::filesystem::path(env::getDllDirectory());
    if (dllDir.empty()) {
      Logger::warn("Sentry: failed to get DLL directory for handler path");
      return;
    }

    const auto logsDir = util::RtxFileSys::path(util::RtxFileSys::Logs);
    const std::string sentryDbPathUtf8 = pathToUtf8(logsDir / ".sentry-native");
    const std::string handlerPathUtf8 = pathToUtf8(dllDir / "crashpad_handler.exe");
    sentry_options_t* options = sentry_options_new();
    setSentryOptionsCommon(options, sentryDbPathUtf8.c_str(), handlerPathUtf8.c_str());
    sentry_options_set_release(options, version);
    sentry_options_set_environment(options, REMIX_SENTRY_ENVIRONMENT);
    // We must use Sentry's built-in consent gate rather than disabling it and checking
    // LocalData ourselves. With require_user_consent = 0, sentry_init() uploads any
    // pending crash reports immediately — before we could check LocalData or show a
    // dialog. The consent gate lets Sentry capture crashes locally regardless, and only
    // upload when we explicitly call sentry_user_consent_give() after confirming the
    // user's permission via LocalData.
    sentry_options_set_require_user_consent(options, 1);

    // Attachments are read at crash time by the Crashpad handler (not at init), so the logs
    // from the session that crashed are what get uploaded. Content is read when the report is
    // built; any unwritten log buffer at crash time may be missing from the attachment.
    const auto attachLogFile = [&](const char* filename) {
      const auto logPath = logsDir / filename;
      if (std::filesystem::exists(logPath)) {
        const std::wstring logPathW = logPath.wstring();
        sentry_options_add_attachmentw(options, logPathW.c_str());
      }
    };
    attachLogFile("remix-dxvk.log");
    attachLogFile("bridge32.log");
    attachLogFile("bridge64.log");

    if (sentry_init(options) != 0) {
      Logger::warn("Sentry: failed to initialize crash reporting");
      return;
    }
    s_sentryInitialized.store(true, std::memory_order_release);
    Logger::info("Sentry: crash reporting initialized");

    std::lock_guard<std::mutex> lock(s_sentryMutex);

    // Start the "App Startup" transaction to measure time-to-first-frame. The transaction is
    // lightweight (no log file attachments) and is finished from onFirstFrame(). Tags set on the
    // global scope (app_folder, gpu, game, etc.) are merged in at finish time.
    const std::string appFolder = std::filesystem::path(env::getExePath()).parent_path().filename().string();
    sentry_set_tag("app_folder", appFolder.c_str());

    // Per-session correlation id: logged once here to remix-dxvk.log AND attached as a searchable
    // Sentry tag. If a user shares their log (e.g. on a GitHub issue), search Sentry for
    // "session_id:<id>" to find the matching crash report. It must be logged at startup because the
    // id cannot be reliably written from inside a crash. The tag is stripped while the usage-tracking
    // transaction is sent (see finishStartupTransaction) so crash reports are not linkable to it.
    sentry_uuid_t sessionUuid = sentry_uuid_new_v4();
    char sessionIdStr[37] = {};
    sentry_uuid_as_string(&sessionUuid, sessionIdStr);
    s_sessionId = sessionIdStr;
    sentry_set_tag(kSessionIdTag, sessionIdStr);
    Logger::info(str::format("Sentry session id (Use this to search for uploaded crash reports): ", sessionIdStr));

    sentry_transaction_context_t* txCtx = sentry_transaction_context_new("App Startup", "app.lifecycle");
    sentry_transaction_context_set_sampled(txCtx, 1);
    s_startupTransaction.store(sentry_transaction_start(txCtx, sentry_value_new_null()), std::memory_order_release);

    if (isAutomationMode()) {
      sentry_user_consent_give();
    } else {
      syncConsentFromUserSettingsUnlocked();
    }
  }

  bool showCrashReportDialogIfNeeded() {
    if (!isSentryActive()) {
      return false;
    }
    if (sentry_get_crashed_last_run() != 1) {
      return false;
    }
    sentry_clear_crashed_last_run();

    // Under the bridge, the bridge client owns the post-crash dialog and upload: it detects the
    // server crash immediately when the server process exits and prompts there. Clear the
    // crashed-last-run flag (above) and stay silent so the user is not prompted a second time on
    // the next launch.
    if (env::isRemixBridgeActive()) {
      return false;
    }

    if (isAutomationMode()) {
      return true;
    }

    const bool hasPermission = LocalData::app().get<bool>(kAllowCrashReportingKey, false);
    const std::wstring message = buildCrashReportMessage(CrashPromptKind::CpuNextLaunch, hasPermission);

    if (hasPermission) {
      MessageBoxW(nullptr, message.c_str(), kCrashReportDialogTitle, MB_OK | MB_ICONINFORMATION);
      return true;
    }

    const int result = MessageBoxW(nullptr, message.c_str(), kCrashReportDialogTitle, MB_YESNO | MB_ICONQUESTION);

    if (result == IDYES) {
      LocalData::app().set(kAllowCrashReportingKey, true);
      LocalData::app().save();
      sentry_user_consent_give();
      return true;
    }

    clearPendingCrashReports();
    return false;
  }

  void onFirstFrame() {
    if (!isSentryActive()) {
      return;
    }
    finishStartupTransaction();
  }

  void shutdown() {
    if (s_shutdownDone.load(std::memory_order_acquire)) {
      return;
    }
    // Finish the startup transaction before marking shutdown (covers exit before first present).
    finishStartupTransaction();
    if (s_shutdownDone.exchange(true)) {
      return;
    }
    if (s_sentryInitialized.exchange(false, std::memory_order_acq_rel)) {
      std::lock_guard<std::mutex> lock(s_sentryMutex);
      sentry_close();
    }
  }

  void syncConsentFromUserSettings() {
    if (!isSentryActive()) {
      return;
    }
    std::lock_guard<std::mutex> lock(s_sentryMutex);
    syncConsentFromUserSettingsUnlocked();
  }

  void addBreadcrumb(const std::string& category, const std::string& message, int level) {
    if (!isSentryActive()) {
      return;
    }
    sentry_value_t crumb = sentry_value_new_breadcrumb(nullptr, message.c_str());
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string(category.c_str()));
    sentry_value_set_by_key(crumb, "level", sentry_value_new_int32(level));
    sentry_add_breadcrumb(crumb);
  }

  void setTag(const std::string& key, const std::string& value) {
    if (!isSentryActive()) {
      return;
    }
    sentry_set_tag(key.c_str(), value.c_str());
  }

  void setContext(const std::string& key, const std::string& value) {
    if (!isSentryActive()) {
      return;
    }
    sentry_value_t context = sentry_value_new_object();
    sentry_value_set_by_key(context, "value", sentry_value_new_string(value.c_str()));
    sentry_set_context(key.c_str(), context);
  }


  void setVulkanLayersAndExtensionsContext(const std::vector<std::string>& layerNames,
                                          const std::vector<std::string>& extensionNames) {
    if (!isSentryActive()) {
      return;
    }
    setContextList("vulkan_instance_layers", "layers", layerNames);
    setContextList("vulkan_instance_extensions", "extensions", extensionNames);

    bool hasSteam = false;
    bool hasAny = false;
    constexpr const char* steamOverlayKeyword = "steam_overlay";
    constexpr const char* anyOverlayKeywords[] = {
        "overlay", "reshade", "capture", "renderdoc"
    };

    for (const auto& name : layerNames) {
      const std::string nameLower = toLower(name);
      if (nameLower.find(steamOverlayKeyword) != std::string::npos) {
        hasSteam = true;
        break;
      }
      for (const char* kw : anyOverlayKeywords) {
        if (nameLower.find(kw) != std::string::npos) {
          hasAny = true;
          break;
        }
      }
    }
    if (!hasAny) {
      for (const auto& name : extensionNames) {
        const std::string nameLower = toLower(name);
        for (const char* kw : anyOverlayKeywords) {
          if (nameLower.find(kw) != std::string::npos) {
            hasAny = true;
            break;
          }
        }
        if (hasAny) {
          break;
        }
      }
    }

    setTag("has_steam_overlay", hasSteam ? "true" : "false");
    setTag("has_any_overlay", (hasSteam || hasAny) ? "true" : "false");
  }

  void runUploadHelper(const char* sentryDatabasePathUtf8, const char* crashType) {
    if (!isAllowedSentryDatabasePath(sentryDatabasePathUtf8)) {
      Logger::warn("Sentry: rejected upload request with invalid database path");
      return;
    }
    // This runs in the short-lived "--sentry-upload-only" helper process, which the bridge
    // launches only after the user has consented to uploading. Its job is to send any crash reports
    // held in the offline cache (see setSentryOptionsCommon: cache_keep + http_retry). Granting
    // consent below is what triggers sending reports cached while consent was absent.
    const std::filesystem::path sentryDbDir(sentryDatabasePathUtf8);

    const auto dllDir = std::filesystem::path(env::getDllDirectory());
    if (dllDir.empty()) {
      Logger::warn("Sentry: failed to get DLL directory for handler path");
      return;
    }

    // Dedicated log for the helper, so a headless upload can be diagnosed after the fact. Uses a
    // fresh file each run (survives game relaunches, unlike bridge64.log which is truncated).
    const std::filesystem::path uploadLogPath = sentryDbDir.parent_path() / "crash-upload.log";
    s_uploadLogFile = _wfopen(uploadLogPath.wstring().c_str(), L"w");

    if (s_uploadLogFile != nullptr) {
      // Timestamped header + crash type so each upload session is distinguishable (this file is
      // recreated each run, but a header still helps when it is copied/collected out of context).
      SYSTEMTIME st = {};
      GetLocalTime(&st);
      fprintf(s_uploadLogFile, "--- crash-upload: type=%s, time=%04d-%02d-%02d %02d:%02d:%02d ---\n",
              (crashType != nullptr && crashType[0] != '\0') ? crashType : "unknown",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
      // Record the cached crash envelopes we are about to send. Their filenames embed the Sentry
      // event id (e.g. <timestamp>-00-<event-uuid>.envelope), correlating this upload to a specific
      // crash so a report on the Sentry side can be traced back to a user's local log.
      std::error_code ec;
      const std::filesystem::path cacheDir = sentryDbDir / "cache";
      for (auto it = std::filesystem::directory_iterator(cacheDir, ec);
           !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (it->path().extension() == ".envelope") {
          fprintf(s_uploadLogFile, "--- pending crash envelope: %s ---\n",
                  it->path().filename().string().c_str());
        }
      }
      fflush(s_uploadLogFile);
    }

    const std::string handlerPathUtf8 = pathToUtf8(dllDir / "crashpad_handler.exe");
    sentry_options_t* options = sentry_options_new();
    setSentryOptionsCommon(options, sentryDatabasePathUtf8, handlerPathUtf8.c_str());
    if (s_uploadLogFile != nullptr) {
      sentry_options_set_debug(options, 1);
      sentry_options_set_logger(options, uploadHelperLogger, s_uploadLogFile);
      // Log at INFO and above only: this keeps the high-level upload breadcrumbs while dropping the
      // verbose DEBUG lines, which also avoids logging the ingest URL (it carries the DSN key).
      sentry_options_set_logger_level(options, SENTRY_LEVEL_INFO);
    }
    sentry_options_set_require_user_consent(options, 1);

    if (sentry_init(options) == 0) {
      std::lock_guard<std::mutex> lock(s_sentryMutex);
      // Granting consent schedules sending any envelopes cached while consent was revoked (this
      // relies on http_retry being enabled in setSentryOptionsCommon).
      sentry_user_consent_give();
      sentry_flush(kSentryUploadHelperFlushTimeoutMs);
      sentry_close();
    } else {
      Logger::warn("Sentry: upload helper sentry_init() failed");
    }

    if (s_uploadLogFile != nullptr) {
      fclose(s_uploadLogFile);
      s_uploadLogFile = nullptr;
    }
  }

  void queueGpuCrashReport(const char* dumpFilePathUtf8) {
    if (dumpFilePathUtf8 == nullptr || dumpFilePathUtf8[0] == '\0') {
      return;
    }
    if (!std::filesystem::exists(dumpFilePathUtf8)) {
      return;
    }
    std::lock_guard<std::mutex> lock(s_gpuCrashQueueMutex);
    s_pendingGpuCrashPath = dumpFilePathUtf8;
  }

  void processPendingGpuCrashReports() {
    if (!isSentryActive()) {
      return;
    }

    // The dump path may be empty: Aftermath produces it, so if Aftermath is disabled or failed there
    // is no dump. We still report the GPU crash, just without the dump attachment.
    std::string dumpPath;
    {
      std::lock_guard<std::mutex> lock(s_gpuCrashQueueMutex);
      dumpPath = std::move(s_pendingGpuCrashPath);
      s_pendingGpuCrashPath.clear();
    }
    if (!dumpPath.empty() && !std::filesystem::exists(dumpPath)) {
      dumpPath.clear();
    }

    {
      std::lock_guard<std::mutex> lock(s_sentryMutex);
      if (isSentryActive()) {
        // Under the bridge, the bridge client shows the post-crash consent dialog and its upload
        // helper sends the report once consent is granted (or clears it on "No"). So we must capture
        // the GPU event even without current consent - require_user_consent gates the actual send and
        // caches the event until consent is given, mirroring how crashpad captures CPU crashes
        // unconditionally. Without the bridge there is no later upload opportunity, so we only capture
        // if the user consents at the dialog promptGpuCrashConsent() shows here.
        const bool bridgeActive = env::isRemixBridgeActive();
        const bool consentGranted = promptGpuCrashConsent();
        if (consentGranted || bridgeActive) {
          if (consentGranted) {
            sentry_user_consent_give();
          }
          captureGpuCrashReport(dumpPath.empty() ? nullptr : dumpPath.c_str(), consentGranted);
        }
      }
    }
  }

} // namespace sentry
} // namespace dxvk

#else

// Sentry disabled (e.g. debug build): no-op implementations so callers link without Sentry libs.

namespace dxvk {
namespace sentry {

  void initialize(const char*) {}

  bool showCrashReportDialogIfNeeded() {
    return false;
  }

  void shutdown() {}

  void syncConsentFromUserSettings() {}

  void addBreadcrumb(const std::string&, const std::string&, int) {}

  void setTag(const std::string&, const std::string&) {}

  void setContext(const std::string&, const std::string&) {}

  void setVulkanLayersAndExtensionsContext(const std::vector<std::string>&, const std::vector<std::string>&) {}

  void onFirstFrame() {}

  void runUploadHelper(const char*, const char*) {}

  void queueGpuCrashReport(const char*) {}

  void processPendingGpuCrashReports() {}

} // namespace sentry
} // namespace dxvk

#endif
