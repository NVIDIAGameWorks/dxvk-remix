# Sentry Crash Reporting

## 1. For Users

### What data is collected

When RTX Remix crashes, the following is captured and may be uploaded if you give permission:

- **Crash data**: Stack trace, thread state, memory snapshot (minidump), and similar information from the moment of the crash.
- **Log files**: If present, `remix-dxvk.log`, `bridge32.log`, and `bridge64.log` from your RTX Remix logs folder are attached to the report.
- **Extra context**: The runtime may attach breadcrumbs (recent actions), tags (e.g. GPU, game name), and other debugging data that developers have added in code.

Nothing is sent to the internet until you grant permission.

### When reports are uploaded

- **Only after you say Yes** to the crash report permission dialog, or if you have enabled "Allow crash data reporting" in the Permissions tab.
- A crash that happens **before** you have granted permission is kept locally and uploaded once you grant permission (e.g. by clicking Yes on the dialog, or enabling the Permissions-tab checkbox).
- If you say **No** on the post-crash dialog, the pending crash data is deleted from disk and never sent.
- Turning the Permissions-tab setting off stops future uploads.

### When you see the permission dialog

- **x86 games** (x86 game → bridge → x64 Remix): The bridge shows the dialog **immediately** after the x64 Remix process crashes; if you choose Yes, it uploads the report.
- **x64 games** (game loads dxvk-remix directly): The permission dialog appears on the **next** launch after a crash. If permission was already granted, it informs you that data was uploaded; otherwise it asks for permission.

### Where data is stored before upload

- Crash data is stored **locally** under `rtx-remix/logs/.sentry-native/` (next to other Remix logs).
- Size is typically under 10 MB. Data there is removed after a successful upload, or when you decline to upload the crash.
- You can delete this folder while the game is not running; Remix will recreate it. Any pending reports not yet uploaded will be lost.

### How to control upload permission

- **First-use guide**: On first launch, RTX Remix shows a welcome popup with checkboxes for crash and performance data collection. Your choices are saved to `%LOCALAPPDATA%\NVIDIA\RTXRemix\user_settings\`.
- **Crash dialog**: After a crash, if you haven't granted permission, Remix asks whether you'd like to allow crash data uploads. Choosing **Yes** enables uploads; **No** clears pending data without uploading.
- **User menu (Permissions tab)**: Use the "Allow crash data reporting" checkbox to grant or revoke permission at any time.
- **File**: Permission is stored in `%LOCALAPPDATA%\NVIDIA\RTXRemix\user_settings\<hash>.conf`. Sentry's internal consent file (`.sentry-native/user-consent`) is kept in sync automatically.


---

## 2. For Remix Developers (Adding Data to Reports)

Use `#include "util_sentry.h"` and the `dxvk::sentry::` helpers when you want to add data that will be attached to the next crash (or to a non-fatal message). Choose the right API for how the data will be used in Sentry.

| Function | Use when |
|----------|----------|
| **addBreadcrumb(category, message, level)** | Recording a step the app took (e.g. "Loaded texture X", "Started frame"). Stored in a small ring buffer and attached to the **next** crash. Not sent by itself. Levels: -2=trace, -1=debug, 0=info, 1=warning, 2=error, 3=fatal. |
| **setTag(key, value)** | You need to **filter or group** by this value in Sentry (e.g. GPU vendor, game name, API). Keep keys and values short and stable; tags are indexed. |
| **setContext(key, value)** | You need **debugging detail** on the event page but don’t need to search by it (e.g. long device strings, config snippets). Not used as filters. |

**Examples**

- Breadcrumb: `sentry::addBreadcrumb("rendering", "Starting frame render");`
- Tag (filterable): `sentry::setTag("gpu_vendor", "NVIDIA");`
- Context (detail only): `sentry::setContext("graphics", "RTX 4090, Driver 535.98");`

`setUser` is not exposed; use tags or context for user-related metadata if needed.

---

## 3. Integration Reference (Engineers / Modifying the System)

**Where the integration lives and what does what**

- **`src/util/util_local_data.cpp` and `.h`** — The single source of truth for data-collection permission, exposed as `LocalData::app()`. `allowCrashReporting` and `allowUsageTracking` are stored in `%LOCALAPPDATA%\NVIDIA\RTXRemix\user_settings\<hash>.conf` (the key names are defined as `sentry::kAllowCrashReportingKey` / `kAllowUsageReportingKey` in `util_sentry.h`). Sentry's internal consent state is synced from these via `sentry::syncConsentFromUserSettings()`.

- **`src/dxvk/imgui/dxvk_imgui_first_use_guide.cpp`** — Welcome popup with permission checkboxes. Calls `sentry::syncConsentFromUserSettings()` after saving settings changes.

- **`src/util/util_sentry.cpp` and `.h`** — All Sentry behavior lives here: init (database under the RTX Remix logs dir, handler next to the calling module), the shared options (`setSentryOptionsCommon`: DSN, offline caching, metrics/logs disabled — see below), consent dialog, declining (deleting crash data via `clearPendingCrashReports()`), shutdown, and `runUploadHelper`. The DSN is in this file. No other module links Sentry or contains Sentry config.

- **`src/d3d9/d3d9_main.cpp`** — Entry points only. The `ONCE` block in `CreateD3D9()` calls `sentry::initialize()` and `showCrashReportDialogIfNeeded()` (so the consent prompt can appear even if the game crashes every launch). `showCrashReportDialogIfNeeded()` returns silently when running under the bridge (`env::isRemixBridgeActive()`), because the bridge client owns that dialog — this avoids a double prompt. When the game is exiting or the session is ending, **`src/d3d9/d3d9_swapchain.cpp`** calls `sentry::shutdown()` from **D3D9WindowProc** on **WM_DESTROY** (normal window close) or **WM_ENDSESSION** (logoff/system shutdown; Windows does not send WM_DESTROY in that case). The export `RtxSentryRunUploadHelper` is a thin wrapper that calls `sentry::runUploadHelper`; it exists so the bridge upload-only helper can trigger upload without linking Sentry.

- **`bridge/src/client/d3d9_lss.cpp`** — When the Remix process exits unexpectedly, the client shows the crash dialog (`promptCrashReportAndApplyChoice()`). It reads `allowCrashReporting` from the UserSettings file in `%LOCALAPPDATA%` (same path logic as the runtime: xxHash of the canonical game root path, via `dxvk::perGameConfigFileName()`). If the user already had permission, it informs them and triggers upload. If not, it asks for permission. On Yes, it records consent (`Config::setUserSetting`) and launches the server with `--sentry-upload-only`. On No, it deletes the pending crash data (`clearPendingCrashData()` — reports, attachments, offline cache, and `.run` dirs), mirroring the runtime's `clearPendingCrashReports()`.

- **`bridge/src/server/main.cpp`** — At startup it checks for `--sentry-upload-only`; if present it skips full runtime init, loads d3d9, calls the `RtxSentryRunUploadHelper` export, and exits. The bridge does not link Sentry; all config is in d3d9.

**Consent flow (runtime vs bridge)**

`UserSettings` (stored in `%LOCALAPPDATA%`) is the canonical source of permission. Both the runtime and the bridge read this file directly (the per-game filename is `xxHash(canonical game root path).conf`, computed by the header-only `dxvk::perGameConfigFileName()`). Sentry's internal consent is synced from UserSettings at runtime init so that Sentry's upload gate stays consistent.

When running with the bridge, the bridge process can immediately detect crashes and show a dialog. When running without the bridge, there is no process left after a crash, so the dialog is shown on the next launch.

The bridge popup is implemented in `bridge/src/client/d3d9_lss.cpp` (`promptCrashReportAndApplyChoice()`). The runtime popup is implemented in `src/util/util_sentry.cpp` (`showCrashReportDialogIfNeeded()`), invoked from `d3d9_main.cpp` on the next init (and suppressed under the bridge).

**Consent-aware offline caching (why deferred consent works)**

This is the mechanism that lets a user grant permission *after* a crash and still have that crash uploaded. It is configured in `setSentryOptionsCommon()`:

- `sentry_options_set_cache_keep(options, SENTRY_CACHE_KEEP_OFFLINE)` and `sentry_options_set_http_retry(options, 1)`.

With these, a crash captured while consent is absent is not discarded: the crashpad backend converts the completed report into a Sentry envelope in `.sentry-native/cache/`, and the retry module sends it once consent is given (`sentry_user_consent_give()`, which the upload helper calls). This requires **sentry-native ≥ 0.14** with the crashpad-backend consent-caching support; the repo ships **0.15.3** (see `packman-external.xml`). Before this, crashpad marked such reports "completed/skipped" and they were lost — no amount of enabling uploads afterward could recover them.

The upload-only helper (`runUploadHelper`) writes a diagnostic log to `rtx-remix/logs/crash-upload.log`. It uses Sentry's own logger at `INFO` level (not `DEBUG`), which keeps high-level breadcrumbs while omitting the ingest URL (it carries the DSN key).

**Privacy: metrics and structured logs are disabled**

sentry-native enables metrics and structured logs by default (since 0.14). Remix crash reporting is strictly opt-in crash data only, so `setSentryOptionsCommon()` disables both: `sentry_options_set_enable_metrics(options, 0)` and `sentry_options_set_enable_logs(options, 0)`.

**Correlating a user's logs with a Sentry crash**

`initialize()` generates a per-session UUID, logs it once to `remix-dxvk.log` as `Sentry session id`, and attaches it as the Sentry tag `session_id`. `remix-dxvk.log` is also attached to the crash report. So given a user's log (e.g. from a GitHub issue), search Sentry for `session_id:<id>` to find the exact crash event.

**Build and deployment (non-obvious)**

- **Sentry is not included in debug builds.** The prebuilt Sentry/Crashpad libraries are Release-only and fail to link in debug; the build turns Sentry off when `buildtype` is `debug` (`external-build/meson.build`).

- **`RTX_AUTOMATION_DISABLE_BLOCKING_DIALOG_BOXES=1`** is used in CI so that all blocking dialog boxes (crash report consent, first-use guide, etc.) are suppressed and crash reporting consent is granted automatically.

- **`crashpad_handler.exe` and `crashpad_wer.dll` must be deployed next to the dxvk-remix `d3d9.dll`.** Sentry is configured with the handler path relative to the module that contains the Sentry code (d3d9), so the handler has to be in the same directory as d3d9. The main build installs them there; the bridge build copies them into the server output (e.g. `.trex`) so that when the server loads d3d9 for upload-only, the handler is next to it.

### File layout (conceptual)

```text
<game or .trex>/
  d3d9.dll
  crashpad_handler.exe
  crashpad_wer.dll

<rtx-remix>/logs/
  remix-dxvk.log, bridge32.log, bridge64.log   (attached if present)
  crash-upload.log                             (upload-only helper diagnostics)
  .sentry-native/                              (crash DB)
    settings.dat, user-consent, installation_id  (identity/consent; preserved on decline)
    reports/, attachments/, cache/               (crash data; deleted on decline/after upload)
```

---

## References

- [Sentry Native SDK](https://docs.sentry.io/platforms/native/)
- [Crashpad Backend](https://docs.sentry.io/platforms/native/configuration/backends/crashpad/)
- [Sentry Native API](https://docs.sentry.io/platforms/native/usage/)
