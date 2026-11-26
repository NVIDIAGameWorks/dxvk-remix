/*
* Copyright (c) 2026, Kim2091
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
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui/imgui.h"
#include "numos/renderer.h"
#include "numos/seed.h"

#include "rtx_numos_ui.h"
#include "rtx_global_volumetrics.h"
#include "rtx_imgui.h"
#include "rtx_numos.h"
#include "rtx_numos_settings.h"
#include "rtx_numos_weather.h"
#include "rtx_option_blocking_popup.h"
#include "rtx_options.h"
#include "rtx_scene_manager.h"
#include "../dxvk_context.h"
#include "../dxvk_device.h"

namespace dxvk {

  namespace {

    constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;
    constexpr const char* kKeyPrefix = "rtx.atmosphere.";
    // This makes a control take its range or speed from the range of NUMOS for the setting.
    const float kAuto = std::numeric_limits<float>::quiet_NaN();

    // These are set at the top of every page, to mark the settings that the showing preset overrides.
    const NumosWeather::Preset* g_weatherPreset = nullptr;
    const numos::Settings* g_shownSettings = nullptr;
    const numos::FrameOutputs* g_numosOutputs = nullptr;

    void openPresetWindow();

    void beginPage(DxvkContext* ctx) {
      NumosWeather& weather = ctx->getCommonObjects()->metaNumosWeather();
      g_weatherPreset = weather.find(weather.activePreset());
      g_shownSettings = &weather.current();
      g_numosOutputs = ctx->getCommonObjects()->metaNumos().outputs();
    }

    // Each setting gets one control, with NUMOS's description as its tooltip.
    // A setting that the showing preset overrides is read-only and shows the value the sky uses.

    enum class Kind { Auto, Color, SpeedMetersPerSecond };

    size_t indexOf(const NumosSettings::Entry& e) {
      return size_t(e.pInfo - numos::settingInfos().data());
    }

    bool overriddenByWeather(const NumosSettings::Entry& e) {
      const size_t i = indexOf(e);
      return g_weatherPreset != nullptr && g_shownSettings != nullptr && i < g_weatherPreset->set.size() &&
             g_weatherPreset->set[i];
    }

    template <typename T>
    T shownValue(const NumosSettings::Entry& e) {
      return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(g_shownSettings) + e.pInfo->offset);
    }

    void descriptionTooltip(const NumosSettings::Entry& e) {
      if (e.pInfo->description && e.pInfo->description[0]) {
        RemixGui::SetTooltipToLastWidgetOnHover(e.pInfo->description);
      }
    }

    void weatherTag() {
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.98f, 0.69f, 0.2f, 1.0f), "weather");
      const std::string tip = "Set by the weather preset \"" + (g_weatherPreset ? g_weatherPreset->name : std::string()) +
                              "\" while it shows: this is the value the sky uses. Edit it in the Weather Presets "
                              "window (Weather tab). Your own value returns when the preset no longer shows.";
      RemixGui::SetTooltipToLastWidgetOnHover(tip.c_str());
    }

    const NumosSettings::Entry* lookup(const char* key) {
      const NumosSettings::Entry* e = NumosSettings::find(key);
      if (e == nullptr || e->option() == nullptr) {
        ImGui::TextDisabled("%s: not available in this build", key);
        return nullptr;
      }
      return e;
    }

    bool isColorName(const std::string& name) {
      for (const char* word : { "Color", "color", "Tint", "tint", "Albedo", "albedo", "Illuminance" }) {
        if (name.find(word) != std::string::npos) {
          return true;
        }
      }
      return false;
    }

    void drawEntry(const NumosSettings::Entry& e, const char* label, float speed, float mn, float mx, const char* format,
                   Kind kind) {
      const numos::SettingInfo& info = *e.pInfo;
      const std::string id = std::string(label) + "###" + info.key;
      if (std::isnan(mn)) {
        mn = info.minValue == numos::NUMOS_NO_MIN ? 0.0f : info.minValue;
      }
      // An equal minimum and maximum leaves the value unclamped.
      if (std::isnan(mx)) {
        mx = info.maxValue == numos::NUMOS_NO_MAX ? 0.0f : info.maxValue;
      }
      const bool weather = overriddenByWeather(e);
      if (weather) {
        ImGui::BeginDisabled(true);
      }

      if (e.pBoolOption) {
        if (weather) {
          bool v = shownValue<bool>(e);
          ImGui::Checkbox(id.c_str(), &v);
        } else {
          RemixGui::Checkbox(id.c_str(), e.pBoolOption);
        }
      } else if (e.pFloatOption) {
        if (kind == Kind::SpeedMetersPerSecond) {
          // The setting is in km/s and is shown in m/s.
          float valueMs = (weather ? shownValue<float>(e) : e.pFloatOption->get()) * 1000.0f;
          const float speedMs = std::isnan(speed) ? 0.1f : speed;
          if (weather) {
            ImGui::DragFloat(id.c_str(), &valueMs, speedMs, mn, mx, "%.1f m/s", kClamp);
          } else {
            RemixGui::RtxOptionUxWrapper wrapper(e.pFloatOption);
            if (RemixGui::DragFloat(id.c_str(), &valueMs, speedMs, mn, mx, "%.1f m/s", kClamp)) {
              RemixGui::CheckRtxOptionPopups(e.pFloatOption);
              e.pFloatOption->setDeferred(valueMs * 0.001f);
            }
          }
        } else {
          if (std::isnan(speed)) {
            speed = mn < mx ? std::max((mx - mn) / 1000.0f, 1e-5f)
                            : std::max(std::abs(e.pFloatOption->getDefaultValue()) * 0.01f, 1e-4f);
          }
          const char* fmt = format ? format : "%.4g";
          if (weather) {
            float v = shownValue<float>(e);
            ImGui::DragFloat(id.c_str(), &v, speed, mn, mx, fmt, kClamp);
          } else {
            RemixGui::DragFloat(id.c_str(), e.pFloatOption, speed, mn, mx, fmt, kClamp);
          }
        }
      } else if (e.pIntOption || e.pUintOption) {
        const int lo = mn < mx ? int(mn) : (e.pUintOption ? 0 : INT_MIN);
        const int hi = mn < mx ? int(std::min(mx, float(INT_MAX))) : INT_MAX;
        const float s = std::isnan(speed) ? 0.1f : speed;
        if (weather) {
          int v = e.pIntOption ? shownValue<int32_t>(e) : int(std::min(shownValue<uint32_t>(e), uint32_t(INT_MAX)));
          ImGui::DragInt(id.c_str(), &v, s, lo, hi, "%d", kClamp);
        } else if (e.pIntOption) {
          RemixGui::DragInt(id.c_str(), e.pIntOption, s, lo, hi, "%d", kClamp);
        } else {
          RemixGui::DragInt(id.c_str(), e.pUintOption, s, lo, hi, "%d", kClamp);
        }
      } else if (e.pVec3Option) {
        const bool color = kind == Kind::Color || (kind == Kind::Auto && isColorName(info.key));
        if (weather) {
          const numos::Vector3 shown = shownValue<numos::Vector3>(e);
          float v[3] = { shown.x, shown.y, shown.z };
          if (color) {
            ImGui::ColorEdit3(id.c_str(), v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
          } else {
            ImGui::DragFloat3(id.c_str(), v, 0.01f, mn, mx, format ? format : "%.4g");
          }
        } else if (color) {
          RemixGui::ColorEdit3(id.c_str(), e.pVec3Option, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        } else {
          RemixGui::DragFloat3(id.c_str(), e.pVec3Option, std::isnan(speed) ? 0.01f : speed, mn, mx,
                               format ? format : "%.4g");
        }
      }

      if (weather) {
        ImGui::EndDisabled();
      }
      descriptionTooltip(e);
      if (weather) {
        weatherTag();
      }
    }

    // Draws a curated control, whose key is relative to rtx.atmosphere.
    void setting(const char* key, const char* label, float speed = kAuto, float mn = kAuto, float mx = kAuto,
                 const char* format = nullptr, Kind kind = Kind::Auto) {
      const std::string full = std::string(kKeyPrefix) + key;
      if (const NumosSettings::Entry* e = lookup(full.c_str())) {
        drawEntry(*e, label, speed, mn, mx, format, kind);
      }
    }

    void color(const char* key, const char* label) {
      setting(key, label, kAuto, kAuto, kAuto, nullptr, Kind::Color);
    }

    void speedMs(const char* key, const char* label, float stepMs, float minMs, float maxMs) {
      setting(key, label, stepMs, minMs, maxMs, nullptr, Kind::SpeedMetersPerSecond);
    }

    // Shows each values[i] of an integer setting as names[i].
    void choice(const char* key, const char* label, const int* values, const char* const* names, int count) {
      const std::string full = std::string(kKeyPrefix) + key;
      const NumosSettings::Entry* e = lookup(full.c_str());
      if (e == nullptr || e->pIntOption == nullptr) {
        return;
      }
      const std::string id = std::string(label) + "###" + e->pInfo->key;
      const bool weather = overriddenByWeather(*e);
      const int current = weather ? shownValue<int32_t>(*e) : e->pIntOption->get();
      int selected = -1;
      for (int i = 0; i < count; ++i) {
        if (values[i] == current) {
          selected = i;
        }
      }
      ImGui::BeginDisabled(weather);
      if (weather) {
        ImGui::Combo(id.c_str(), &selected, names, count);
      } else {
        RemixGui::RtxOptionUxWrapper wrapper(e->pIntOption);
        if (RemixGui::Combo(id.c_str(), &selected, names, count) && selected >= 0) {
          RemixGui::CheckRtxOptionPopups(e->pIntOption);
          e->pIntOption->setDeferred(values[selected]);
        }
      }
      ImGui::EndDisabled();
      descriptionTooltip(*e);
      if (weather) {
        weatherTag();
      }
    }

    // Replaces the tooltip of the row drawn since rowStart with text for players.
    // The row's reset button and weather tag keep their own tooltips.
    // The row spans the current column only, so a row beside it in another column keeps its own tooltip.
    void playerTooltip(const ImVec2& rowStart, float rowEndX, const char* text) {
      const ImVec2 rowEnd(rowEndX, ImGui::GetCursorScreenPos().y);
      if (ImGui::IsWindowHovered() && !ImGui::IsItemHovered() && ImGui::IsMouseHoveringRect(rowStart, rowEnd, false)) {
        RemixGui::SetTooltipUnformatted(text);
      }
    }

    // Draws a curated setting with a tooltip for players.
    void userSetting(const char* key, const char* label, const char* tooltip, float speed = kAuto, float mn = kAuto,
                     float mx = kAuto, const char* format = nullptr) {
      const ImVec2 rowStart = ImGui::GetCursorScreenPos();
      const float rowEndX = rowStart.x + ImGui::GetContentRegionAvail().x;
      setting(key, label, speed, mn, mx, format);
      playerTooltip(rowStart, rowEndX, tooltip);
    }

    void userChoice(const char* key, const char* label, const char* tooltip, const int* values, const char* const* names,
                    int count) {
      const ImVec2 rowStart = ImGui::GetCursorScreenPos();
      const float rowEndX = rowStart.x + ImGui::GetContentRegionAvail().x;
      choice(key, label, values, names, count);
      playerTooltip(rowStart, rowEndX, tooltip);
    }

    void weatherNote() {
      if (g_weatherPreset != nullptr) {
        ImGui::TextWrapped("Settings marked \"weather\" follow the current weather and cannot be changed while it shows.");
      }
    }

    bool boolValue(const char* key) {
      const std::string full = std::string(kKeyPrefix) + key;
      const NumosSettings::Entry* e = NumosSettings::find(full.c_str());
      if (e == nullptr || e->pBoolOption == nullptr) {
        return false;
      }
      return overriddenByWeather(*e) ? shownValue<bool>(*e) : e->pBoolOption->get();
    }

    float floatValue(const char* key) {
      const std::string full = std::string(kKeyPrefix) + key;
      const NumosSettings::Entry* e = NumosSettings::find(full.c_str());
      if (e == nullptr || e->pFloatOption == nullptr) {
        return 0.0f;
      }
      return overriddenByWeather(*e) ? shownValue<float>(*e) : e->pFloatOption->get();
    }

    // These functions draw the Sky tab.

    void showSkyTab() {
      setting("sunIntensity", "Sun Intensity", 0.01f, 0.0f, 100.0f, "%.2f");

      const bool timeCycle = boolValue("timeCycleEnable");
      ImGui::BeginDisabled(timeCycle);
      setting("sunElevation", "Sun Elevation", 0.01f, -90.0f, 90.0f, "%.2f deg");
      setting("sunRotation", "Sun Rotation", 0.01f, 0.0f, 360.0f, "%.1f deg");
      ImGui::EndDisabled();
      if (timeCycle) {
        ImGui::TextDisabled("The day / night cycle below drives the sun.");
      }

      if (ImGui::TreeNode("Sun size & shadows")) {
        ImGui::BeginDisabled(floatValue("sunShadowSoftnessDeg") > 0.0f);
        setting("sunSize", "Sun Size", 0.01f, 0.0f, 10.0f, "%.3f deg");
        ImGui::EndDisabled();
        setting("sunShadowSoftnessDeg", "Shadow Softness", 0.01f, 0.0f, 10.0f, "%.3f deg");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Day / night cycle")) {
        setting("timeCycleEnable", "Enable Time Cycle");
        setting("timeOfDayHours", "Time Of Day", 0.01f, 0.0f, 24.0f, "%.2f h");
        setting("dayLengthMinutes", "Day Length", 0.1f, 0.01f, 600.0f, "%.2f min");
        setting("northOffsetDegrees", "North Offset", 0.1f, -360.0f, 360.0f, "%.1f deg");
        if (ImGui::TreeNode("Season & location")) {
          setting("latitudeDegrees", "Latitude", 0.1f, -90.0f, 90.0f, "%.1f deg");
          setting("dayOfYear", "Day Of Year", 1.0f, 1.0f, 365.0f);
          setting("calendarYear", "Year", 1.0f, 1900.0f, 2100.0f);
          ImGui::TreePop();
        }
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Sky color & brightness")) {
        setting("airDensity", "Air", 0.01f, 0.0f, 100.0f, "%.2f");
        color("airTint", "Air Tint");
        setting("aerosolDensity", "Dust", 0.01f, 0.0f, 100.0f, "%.2f");
        color("aerosolTint", "Dust Tint");
        setting("ozoneDensity", "Ozone", 0.01f, 0.0f, 100.0f, "%.2f");
        setting("skyIndirectRadianceScale", "Sky Indirect Scale", 0.01f, 0.0f, 20.0f, "%.2f");
        setting("groundAlbedo", "Ground Albedo", 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Atmosphere model (advanced)")) {
        setting("planetRadius", "Planet Radius", 10.0f, 1000.0f, 10000.0f, "%.0f km");
        setting("atmosphereThickness", "Atmosphere Thickness", 1.0f, 10.0f, 500.0f, "%.0f km");
        setting("mieAnisotropy", "Mie Anisotropy", 0.01f, -1.0f, 1.0f, "%.2f");
        color("sunIlluminance", "Sun Illuminance");
        setting("rayleighScattering", "Air Scattering", 0.0005f, 0.0f, 0.1f, "%.4f /km");
        setting("mieScattering", "Dust Scattering", 0.0005f, 0.0f, 0.05f, "%.4f /km");
        setting("mieAbsorption", "Dust Absorption", 0.0005f, 0.0f, 0.05f, "%.4f /km");
        setting("ozoneAbsorption", "Ozone Absorption", 0.0001f, 0.0f, 0.05f, "%.5f /km");
        setting("ozoneLayerAltitude", "Ozone Layer Altitude", 0.5f, 0.0f, 50.0f, "%.1f km");
        setting("ozoneLayerWidth", "Ozone Layer Width", 0.5f, 1.0f, 30.0f, "%.1f km");
        setting("multiScatterPhysicalStrength", "Multiscatter Physical Strength", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("directionalLightRadianceScale", "Sun / Moon Light Scale", 0.01f, 0.0f, 10.0f, "%.2f");
        setting("radianceAutoScale", "Night Precision (Pre-exposure)");
        ImGui::TreePop();
      }
    }

    // These functions draw the Clouds tab.

    void showCloudQuality() {
      {
        static const int values[] = { 0, 1, 2 };
        static const char* names[] = { "High", "Medium", "Low" };
        choice("qualityLevel", "Sky Quality Level", values, names, 3);
      }
      {
        static const int values[] = { 0, 1 };
        static const char* names[] = { "Quality", "Performance" };
        choice("cloudPerformancePreset", "Performance Preset", values, names, 2);
      }
      setting("cloudSecondaryLutEnable", "Fast Cloud Reflections");
      {
        static const int values[] = { 0, 1, 2, 3 };
        static const char* names[] = { "Auto", "Full", "Quarter", "Sixteenth" };
        choice("cloudMarchRate", "March Rate", values, names, 4);
        const uint32_t stride = g_numosOutputs ? g_numosOutputs->cloudMarchStride : 0u;
        ImGui::TextDisabled("In use: %s", stride >= 4u ? "Sixteenth (4x4)" : stride == 2u ? "Quarter (2x2)"
                                          : stride == 1u ? "Full" : "-");
        RemixGui::SetTooltipToLastWidgetOnHover("Auto marches sparsely only where it cannot show: native or DLSS Quality-like "
                                                "upscaling with at least 720 render lines get the quarter march, coarser "
                                                "upscaling the full rate.");
      }
      setting("cloudTemporalHistoryWeight", "Temporal History", 0.005f, 0.0f, 0.98f, "%.3f");
      {
        static const int values[] = { 0, 2, 1, 3 };
        static const char* names[] = { "One Pass", "Split", "Split, Cross-Tile", "Split, Cross-Tile, Full Precision" };
        choice("cloudQuarterMarchSchedule", "Quarter March Schedule", values, names, 4);
      }
      setting("cloudEmptyPixelSkip", "Skip Empty Pixels");
      setting("cloudMarchRayBudgetMillions", "March Ray Budget (Millions)", 0.05f, 0.0f, 8.0f, "%.2f");
      setting("cloudMarchEarlyExit", "Early Exit in Thick Cloud");
      setting("cloudTemporalStillSamples", "Still History (samples)", 0.5f, 1.0f, 1024.0f);
      setting("cloudSpatialDenoise", "Spatial Denoise", 0.01f, 0.0f, 1.0f, "%.2f");
      setting("cloudHistoryDepthTolerance", "Screen Reuse Depth Tolerance", 0.005f, 0.0f, 1.0f, "%.3f");
      {
        static const int values[] = { 0, 1, 2 };
        static const char* names[] = { "Every frame", "Half per frame", "Quarter per frame" };
        choice("cloudSecondaryLutInterleaveMode", "Cloud Reflection Interleave", values, names, 3);
      }
      {
        static const int values[] = { 0, 2 };
        static const char* names[] = { "Off", "Always" };
        choice("cloudDetailLodMode", "Cloud Detail LOD", values, names, 2);
      }
      setting("cloudDetailLodBias", "Detail LOD Bias", 0.05f, -3.0f, 3.0f, "%.2f");
      setting("cloudViewStepKm", "Cloud Sample Spacing", 0.01f, 0.0f, 4.0f, "%.2f km");
      setting("cloudViewSamplesMax", "Max Cloud Samples", 1.0f, 2.0f, 256.0f);
      setting("cloudViewSamples", "Base Cloud Samples", 1.0f, 1.0f, 32.0f);
      setting("nubis3AdaptiveStepKm", "Adaptive Step Floor", 0.001f, 0.0f, 0.2f, "%.3f km");
      setting("nvdfStepScale", "SDF Step Scale", 0.01f, 0.1f, 2.0f, "%.2f");
      setting("cloudLightingLodThreshold", "Lighting LOD", 0.002f, 0.0f, 0.25f, "%.3f");
      setting("cloudLightingLodByDistance", "Lighting LOD by Distance");
      setting("nubis3JitterAnimateKm", "Jitter Animation Distance", 1.0f, 0.0f, 1.0e9f, "%.0f km");
      setting("cloudVoxelGridRebakeGranularityKm", "Lighting Grid Re-bake Step", 0.01f, 0.0f, 10.0f, "%.2f km");
      if (ImGui::TreeNode("Profiling")) {
        {
          static const int values[] = { 0, 9 };
          static const char* names[] = { "Normal", "Reference path tracer" };
          choice("cloudProfilingMode", "Cloud Profiling", values, names, 2);
        }
        setting("cloudProfilingLog", "Log Cloud Timings");
        ImGui::TreePop();
      }
    }

    void showCloudsTab() {
      ImGui::TextWrapped("Shape the clouds here. Use placement to move the layer without shrinking its clouds.");
      setting("cloudEnabled", "Enable Clouds");
      if (!boolValue("cloudEnabled")) {
        ImGui::TextWrapped("Clouds are off. You can still prepare their settings below.");
      }
      setting("cloudCoverageMean", "Coverage", 0.01f, 0.0f, 1.0f, "%.2f");
      setting("cloudTypeMean", "Cloud Type (wispy - billowy)", 0.01f, 0.0f, 1.0f, "%.2f");
      setting("cloudDensity", "Density", 0.1f, 0.0f, 200.0f, "%.1f");
      color("cloudColor", "Cloud Color");

      if (ImGui::TreeNode("Placement & scale")) {
        setting("cloudBaseHeightMeters", "Layer Base Height", 10.0f, 0.0f, 12000.0f, "%.0f m");
        setting("cloudDepthMeters", "Layer Thickness", 50.0f, 50.0f, 8000.0f, "%.0f m");
        setting("worldScale", "World Scale", 0.005f, 0.01f, 10.0f, "%.3f");
        setting("cloudVerticalOffsetWorldUnits", "Cloud Vertical Offset (world units)", 10.0f, -1.0e8f, 1.0e8f, "%.1f");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Shape & edges")) {
        setting("cloudCellSizeKm", "Cloud Cell Size", 0.05f, 0.5f, 6.0f, "%.2f km");
        setting("nvdfProfileDepthKm", "Profile Depth", 0.01f, 0.01f, 3.0f, "%.2f km");
        setting("nubis3ErosionStrength", "Erosion Strength", 0.02f, 0.0f, 2.0f, "%.2f");
        setting("cloudBodyBillowStrength", "Billow Strength", 0.02f, 0.0f, 3.0f, "%.2f");
        setting("cloudBodyBillowSizeKm", "Billow Size", 0.02f, 0.2f, 4.0f, "%.2f km");
        setting("cloudBodyBillowGain", "Billow Gain", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudCurlStrengthKm", "Curl Strength", 0.005f, 0.0f, 1.0f, "%.3f km");
        setting("cloudColumnTopVariation", "Top Variation", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudColumnTopShape", "Top Shape", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudColumnBaseVariation", "Base Variation", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudColumnFeather", "Column Feather", 0.01f, 0.0f, 1.0f, "%.2f");
        if (ImGui::TreeNode("Coverage & type fields")) {
          setting("cloudCoverageSpread", "Coverage Spread", 0.01f, 0.0f, 1.0f, "%.2f");
          setting("cloudCoverageNoiseScale", "Coverage Noise Scale", 0.0001f, 0.0f, 0.1f, "%.5f");
          setting("cloudTypeSpread", "Type Spread", 0.01f, 0.0f, 1.0f, "%.2f");
          setting("cloudTypeNoiseScale", "Type Noise Scale", 0.0001f, 0.0f, 0.1f, "%.5f");
          setting("nvdfNominalCoverage", "Nominal Coverage", 0.01f, 0.0f, 1.0f, "%.2f");
          setting("nvdfCoverageOffsetKm", "Coverage Offset", 0.01f, 0.0f, 2.0f, "%.2f km");
          setting("cloudNoiseTileKm", "Noise Tile", 0.1f, 1.0f, 100.0f, "%.1f km");
          setting("cloudHexTilingEnable", "Hex Tiling");
          ImGui::TreePop();
        }
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Lighting")) {
        setting("cloudPhaseG1", "Forward Scatter", 0.01f, 0.0f, 0.99f, "%.2f");
        setting("cloudMultiScatterOctaves", "Multi-Scatter Octaves", 0.1f, 1.0f, 8.0f);
        setting("cloudDiffuseSunStrength", "Diffuse Sun", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudSkyAmbientCloudOcclusionStrength", "Sky Ambient Occlusion", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudDiskAureole", "Sun / Moon Aureole");
        setting("cloudDropletDiameter", "Droplet Diameter", 0.1f, 5.0f, 50.0f, "%.1f um");
        RemixGui::SetTooltipToLastWidgetOnHover("The glow around the sun and moons behind thin cloud (droplet diffraction): "
                                                "smaller droplets give a wider corona.");
        if (ImGui::TreeNode("Sun shadow octaves")) {
          setting("cloudSunOctaveCount", "Octave Count", 0.1f, 1.0f, 8.0f);
          setting("cloudSunOctaveA", "Octave A", 0.01f, 0.0f, 1.0f, "%.2f");
          setting("cloudSunOctaveB", "Octave B", 0.01f, 0.0f, 1.0f, "%.2f");
          setting("cloudSunOctaveC", "Octave C", 0.01f, 0.0f, 1.0f, "%.2f");
          ImGui::TreePop();
        }
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Ground shadows")) {
        setting("cloudShadowStrength", "Ground Shadow", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudVoxelShadowsEnable", "Cloud Shadows on Surfaces");
        setting("cloudShadowMapEnable", "Shadow Map");
        setting("cloudShadowMapWindowKm", "Shadow Map Window", 0.1f, 1.0f, 50.0f, "%.1f km");
        setting("cloudShadowMapRebakeDegrees", "Shadow Map Re-bake Step", 0.01f, 0.0f, 10.0f, "%.2f deg");
        setting("cloudShadowMapBakeRowsPerFrame", "Shadow Map Rows Per Frame", 1.0f, 8.0f, 256.0f);
        setting("cloudShadowMapMaxLagDegrees", "Shadow Map Max Lag", 0.01f, 0.0f, 10.0f, "%.2f deg");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Wind & motion")) {
        speedMs("cloudWindSpeed", "Wind Speed", 0.5f, 0.0f, 1000.0f);
        setting("cloudWindDirection", "Wind Direction", 1.0f, 0.0f, 360.0f, "%.1f deg");
        ImGui::Separator();
        speedMs("cloudEvolutionSpeed", "Rise Speed", 0.1f, 0.0f, 50.0f);
        speedMs("cloudBoilSpeed", "Shear Speed", 0.1f, 0.0f, 50.0f);
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Motion vectors")) {
        RemixGui::Checkbox("Cloud Motion Vectors###rtx.numos.cloudMotionVectors", &RtxNumos::cloudMotionVectorsObject());
        RemixGui::SetTooltipToLastWidgetOnHover("Pixels the clouds hide get the clouds' depth and motion vectors, so upscaling, "
                                                "ray reconstruction and frame generation move the clouds with the clouds rather "
                                                "than with the ground under them.");
        ImGui::BeginDisabled(!RtxNumos::cloudMotionVectors());
        RemixGui::DragFloat("Coverage Threshold###rtx.numos.cloudMotionVectorCoverage", &RtxNumos::cloudMotionVectorCoverageObject(),
                            0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::SetTooltipToLastWidgetOnHover("The cloud opacity from which a pixel takes the cloud's motion vectors and depth.");
        ImGui::EndDisabled();
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Second cloud layer")) {
        setting("cloudLayer2Enable", "Enable Layer 2");
        ImGui::BeginDisabled(!boolValue("cloudLayer2Enable"));
        setting("cloudLayer2BaseHeightMeters", "Layer 2 Altitude", 50.0f, 0.0f, 20000.0f, "%.0f m");
        setting("cloudLayer2DepthMeters", "Layer 2 Depth", 10.0f, 10.0f, 6000.0f, "%.0f m");
        setting("cloudLayer2CoverageMean", "Layer 2 Coverage", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudLayer2TypeMean", "Layer 2 Cloud Type", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudLayer2TypeSpread", "Layer 2 Type Spread", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("cloudLayer2DensityScale", "Layer 2 Density", 0.01f, 0.0f, 2.0f, "%.2f");
        color("cloudLayer2Albedo", "Layer 2 Albedo");
        setting("cloudLayer2FollowCamera", "Layer 2 Follows Camera");
        setting("cloudLayer2CameraOffsetMeters", "Layer 2 Camera Offset", 1.0f, -10000.0f, 10000.0f, "%.0f m");
        setting("cloudLayer2NoiseSeed", "Layer 2 Noise Seed", 1.0f, 0.0f, 100000.0f, "%.0f");
        ImGui::EndDisabled();
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Quality & performance")) {
        showCloudQuality();
        ImGui::TreePop();
      }
    }

    // These functions draw the Haze tab.

    // This button is the only place that writes the haze and the volumetrics options from one another.
    void showUseHazeButton() {
      if (!RtxGlobalVolumetrics::enable()) {
        return;
      }
      ImGui::TextWrapped("Remix volumetrics are on, so NUMOS haze is off.");
      if (ImGui::Button("Use NUMOS haze")) {
        RtxOption<bool>& volumetrics = RtxGlobalVolumetrics::enableObject();
        // Only one blocked edit popup can show at a time, so the haze is checked when volumetrics are not blocked.
        const bool blocked = RemixGui::CheckRtxOptionPopups(&volumetrics);
        volumetrics.setDeferred(false);

        const NumosSettings::Entry* pHaze = NumosSettings::find("rtx.atmosphere.aerialPerspective");
        if (pHaze != nullptr && pHaze->pBoolOption != nullptr && !pHaze->pBoolOption->get()) {
          if (!blocked) {
            RemixGui::CheckRtxOptionPopups(pHaze->pBoolOption);
          }
          pHaze->pBoolOption->setDeferred(true);
        }
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Turns Remix volumetrics off and NUMOS haze on. The two are never drawn together.");
      ImGui::Separator();
    }

    void showHazeTab() {
      showUseHazeButton();
      ImGui::TextWrapped("Aerial perspective adds haze and depth to distant terrain, buildings, and clouds.");
      setting("aerialPerspective", "Enable Distance Haze");
      if (!boolValue("aerialPerspective")) {
        return;
      }
      ImGui::Text("Units Per Meter  %8.2f", NumosSettings::resolved().unitsPerMeter > 0.0f
                    ? NumosSettings::resolved().unitsPerMeter : RtxOptions::getMeterToWorldUnitScale());
      RemixGui::SetTooltipToLastWidgetOnHover("The game's measured scale, configured in Setup.");
      setting("hazeWorldScale", "Haze World Scale", 0.005f, 0.01f, 10.0f, "%.3f");

      if (ImGui::TreeNode("Ground fog")) {
        setting("fogVisibilityMeters", "Fog Visibility", 10.0f, 0.0f, 100000.0f, "%.0f m");
        setting("fogBaseHeightMeters", "Fog Base Height", 1.0f, -10000.0f, 10000.0f, "%.0f m");
        setting("fogHeightFalloffMeters", "Fog Height Falloff", 1.0f, 1.0f, 10000.0f, "%.0f m");
        setting("fogAlbedo", "Fog Albedo", 0.01f, 0.0f, 1.0f, "%.2f");
        setting("fogAnisotropy", "Fog Anisotropy", 0.01f, -0.95f, 0.95f, "%.2f");
        setting("fogNearFadeMeters", "Fog Near Clear Zone", 1.0f, 0.0f, 1000.0f, "%.0f m");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Sun shadows")) {
        setting("aerialPerspectiveSceneShadow", "Scene Shadows");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Local lights")) {
        setting("aerialPerspectiveLocalLights", "Local Lights");
        ImGui::BeginDisabled(!boolValue("aerialPerspectiveLocalLights"));
        setting("aerialPerspectiveLocalLightIntensity", "Local Light Intensity", 0.05f, 0.0f, 100.0f, "%.2f");
        setting("aerialPerspectiveLocalLightWindow", "Local Light Range Window");
        setting("aerialPerspectiveLocalLightShadows", "Local Light Shadows");
        setting("aerialPerspectiveLocalLightCutoff", "Local Light Cutoff", 0.0005f, 0.0f, 1.0f, "%.4f");
        setting("aerialPerspectiveLocalLightMaxCount", "Local Light Budget", 1.0f, 0.0f, 4096.0f);
        ImGui::EndDisabled();
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Quality & scattering limits")) {
        {
          static const int values[] = { 0, 1, 2 };
          static const char* names[] = { "High", "Medium", "Low" };
          choice("qualityLevel", "Sky Quality Level", values, names, 3);
        }
        setting("aerialPerspectiveFroxelRangeMeters", "Near Field Range", 10.0f, 10.0f, 50000.0f, "%.0f m");
        setting("aerialPerspectiveFroxelSizePixels", "Froxel Size (pixels)", 0.1f, 2.0f, 64.0f);
        setting("aerialPerspectiveFroxelDepthSlices", "Depth Slices", 1.0f, 16.0f, 256.0f);
        setting("aerialPerspectiveHistoryFrames", "History Frames", 1.0f, 1.0f, 255.0f);
        setting("aerialPerspectiveMieAnisotropyMax", "Forward Scatter Cap", 0.01f, -1.0f, 1.0f, "%.2f");
        ImGui::TreePop();
      }
    }

    // These functions draw the Night tab.

    void showMoon(uint32_t index) {
      char title[32];
      std::snprintf(title, sizeof(title), "Moon %u", index);
      if (!ImGui::TreeNode(title)) {
        return;
      }
      char key[64];
      auto moonKey = [&](const char* name) {
        std::snprintf(key, sizeof(key), "moon%u.%s", index, name);
        return key;
      };
      setting(moonKey("enabled"), "Enabled");
      setting(moonKey("angularDiameter"), "Angular Diameter", 0.01f, 0.05f, 30.0f, "%.3f deg");
      color(moonKey("color"), "Albedo Tint");
      setting(moonKey("elevation"), "Elevation", 0.1f, -90.0f, 90.0f, "%.1f deg");
      setting(moonKey("rotation"), "Rotation", 0.1f, 0.0f, 360.0f, "%.1f deg");
      setting(moonKey("phase"), "Phase", 0.005f, -1.0f, 1.0f, "%.3f");
      ImGui::TextDisabled("0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter. A negative phase comes from the sun's place.");
      ImGui::TreePop();
    }

    void showNightTab() {
      ImGui::TextWrapped("The night is drawn at its physical brightness, times the Night Gain and the star and moon gains "
                         "below. How bright it looks is otherwise the exposure's job (auto-exposure limits and compensation "
                         "in the tonemapping settings).");
      setting("nightGainStops", "Night Gain", 0.05f, 0.0f, 16.0f, "%.2f stops");
      ImGui::TextDisabled("In force: x%.3g (fades in through twilight)", g_numosOutputs ? double(g_numosOutputs->nightGain) : 1.0);
      setting("starGainStops", "Star Gain", 0.05f, -16.0f, 16.0f, "%+.2f stops");
      setting("moonlightGainStops", "Moonlight Gain", 0.05f, -16.0f, 16.0f, "%+.2f stops");
      setting("moonDiscGainStops", "Moon Disc Gain", 0.05f, -16.0f, 16.0f, "%+.2f stops");
      ImGui::TextDisabled("In force: stars x%.3g, moonlight x%.3g, moon discs x%.3g",
                          g_numosOutputs ? double(g_numosOutputs->starGain) : 1.0,
                          g_numosOutputs ? double(g_numosOutputs->moonlightGain) : 1.0,
                          g_numosOutputs ? double(g_numosOutputs->moonDiscGain) : 1.0);
      if (ImGui::TreeNode("Stars")) {
        setting("nightSkyStars", "Stars and Milky Way");
        setting("starScintillation", "Twinkle", 0.01f, 0.0f, 4.0f, "%.2f");
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Moon")) {
        setting("realMoonEnabled", "Earth's Moon");
        setting("realMoonAngularDiameter", "Moon Size", 0.01f, 0.05f, 20.0f, "%.3f deg");
        ImGui::TextDisabled("Its place and phase follow the date: Sky > Day / night cycle > Season & location.");
        ImGui::TreePop();
      }
      if (ImGui::TreeNode("Game moons")) {
        for (uint32_t i = 0; i < numos::kMaxMoons; ++i) {
          showMoon(i);
        }
        ImGui::TreePop();
      }
    }

    // This draws the lightning settings of the Weather tab.

    void showLightning() {
      if (!ImGui::TreeNode("Lightning")) {
        return;
      }
      setting("lightningEnable", "Enable Lightning");
      ImGui::SameLine();
      if (ImGui::Button("Test Strike", ImVec2(120, 0))) {
        numos::Renderer::requestLightningStrike();
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Fires one strike now, even at 0 strikes per minute. It needs Enable Lightning. "
                                              "A daytime in-cloud flash is barely visible, as real lightning is.");
      setting("lightningStrikesPerMinute", "Strikes Per Minute", 0.1f, 0.0f, 60.0f, "%.1f");
      setting("lightningLuminousIntensity", "Flash Intensity", 1.0e6f, 0.0f, 1.0e11f, "%.3g cd");
      setting("lightningSceneLight", "Light the Scene");
      setting("lightningRangeKm", "Max Strike Distance", 0.1f, 1.1f, 50.0f, "%.1f km");
      color("lightningColor", "Flash Color");
      ImGui::TreePop();
    }

    // These functions draw the Setup tab.

    // Turns a key such as "cloudCoverageMean" into "Cloud coverage mean", with unit suffixes as units.
    std::string labelFor(const std::string& name) {
      static const std::pair<const char*, const char*> units[] = {
        { "WorldUnits", " (world units)" }, { "Meters", " (m)" }, { "Km", " (km)" }, { "Degrees", " (deg)" },
        { "Deg", " (deg)" }, { "Hours", " (h)" }, { "Minutes", " (min)" },
      };
      std::string base = name;
      std::string unit;
      for (const auto& [suffix, text] : units) {
        const size_t n = std::strlen(suffix);
        if (base.size() > n && base.compare(base.size() - n, n, suffix) == 0) {
          base.resize(base.size() - n);
          unit = text;
          break;
        }
      }
      std::string out;
      for (size_t i = 0; i < base.size(); ++i) {
        const char c = base[i];
        const bool boundary = i > 0 && std::isupper(uint8_t(c)) &&
          (std::islower(uint8_t(base[i - 1])) || (i + 1 < base.size() && std::islower(uint8_t(base[i + 1]))));
        if (boundary || (i > 0 && std::isdigit(uint8_t(c)) && !std::isdigit(uint8_t(base[i - 1])))) {
          out += ' ';
        }
        out += (i == 0) ? char(std::toupper(uint8_t(c))) : (boundary ? char(std::tolower(uint8_t(c))) : c);
      }
      return out + unit;
    }

    bool containsNoCase(const std::string& text, const std::string& needle) {
      if (needle.empty()) {
        return true;
      }
      auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                            [](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); });
      return it != text.end();
    }

    // Shows every NUMOS setting with a search, for the ones that no tab lists.
    void showAllSettings() {
      static char filter[128] = {};
      ImGui::InputText("Search###NumosSettingsFilter", filter, sizeof(filter));
      RemixGui::SetTooltipToLastWidgetOnHover("Shows only settings whose key, label or description contains this text.");
      const std::string needle = filter;
      for (size_t i = 0; i < NumosSettings::count(); ++i) {
        const NumosSettings::Entry& e = NumosSettings::entry(i);
        const numos::SettingInfo& info = *e.pInfo;
        if (e.option() == nullptr) {
          continue;
        }
        const std::string name = info.key.rfind(kKeyPrefix, 0) == 0 ? info.key.substr(std::strlen(kKeyPrefix)) : info.key;
        const std::string label = labelFor(name);
        if (!containsNoCase(info.key, needle) && !containsNoCase(label, needle) &&
            !(info.description && containsNoCase(info.description, needle))) {
          continue;
        }
        drawEntry(e, label.c_str(), kAuto, kAuto, kAuto, nullptr, Kind::Auto);
      }
    }

    void showSetupTab(DxvkContext* ctx) {
      setting("flipUpAxis", "Flip Up Axis");

      if (ImGui::TreeNode("Placement & scale")) {
        setting("unitsPerMeter", "Units Per Meter", 0.1f, 0.0f, 10000.0f, "%.2f");
        setting("worldScale", "World Scale", 0.005f, 0.01f, 10.0f, "%.3f");
        setting("hazeWorldScale", "Haze World Scale", 0.005f, 0.01f, 10.0f, "%.3f");
        setting("groundLevelAuto", "Ground Level: Auto");
        setting("groundSmoothingMeters", "Ground Smoothing", 50.0f, 100.0f, 50000.0f, "%.0f m");
        if (const numos::FrameOutputs* o = ctx->getCommonObjects()->metaNumos().outputs()) {
          ImGui::Text("Ground level in use: %.1f (%s)", o->groundLevelWorldUnits,
                      o->groundLevelMeasured ? "measured" : "configured");
        }
        setting("groundLevelWorldUnits", "Ground Level (world units)", 10.0f, -1.0e6f, 1.0e6f, "%.1f");
        if (ImGui::Button("Set to Here", ImVec2(120, 0))) {
          // This is the game's own camera without free camera, or the integration's position while it overrides it.
          Vector3 here = ctx->getCommonObjects()->getSceneManager().getCamera().getPosition(/*freecam=*/false);
          if (boolValue("useCameraWorldOverride")) {
            if (const NumosSettings::Entry* e = NumosSettings::find("rtx.atmosphere.cameraWorldOverride")) {
              if (e->pVec3Option) {
                here = e->pVec3Option->get();
              }
            }
          }
          if (const NumosSettings::Entry* e = NumosSettings::find("rtx.atmosphere.groundLevelWorldUnits")) {
            if (e->pFloatOption) {
              RemixGui::CheckRtxOptionPopups(e->pFloatOption);
              e->pFloatOption->setDeferred(RtxOptions::zUp() ? here.z : here.y);
            }
          }
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Use the current viewer height as atmospheric ground. Exit freecam and stand "
                                                "at normal ground level first.");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Camera override (advanced)")) {
        setting("useCameraWorldOverride", "Use Camera World Override");
        setting("cameraWorldOverride", "Camera World Override", 1.0f, -1.0e7f, 1.0e7f, "%.1f");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Seed")) {
        ImGui::TextWrapped("Copy, load, randomize and save seeds in the Weather Presets window.");
        if (ImGui::Button("Open Weather Presets")) {
          openPresetWindow();
        }
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("Diagnostics")) {
        if (ImGui::Button("Signal World Change")) {
          RtxNumos::worldChangeCountObject().setDeferred(RtxNumos::worldChangeCount() + 1u);
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Changes rtx.skyProvider.worldChangeCount, as a game integration does after a load. "
                                                "The automatic ground level is then measured again.");
        ImGui::SameLine();
        ImGui::Text("World changes: %u", RtxNumos::worldChangeCount());
        setting("cloudAnchorCutKm", "Anchor Cut Threshold", 0.05f, 0.0f, 50.0f, "%.2f km");
        setting("skyViewRebakeGranularityDeg", "Sky View Re-bake Step", 0.01f, 0.0f, 5.0f, "%.2f deg");
        setting("skyViewAltitudeRebakeGranularityKm", "Sky View Altitude Re-bake Step", 0.01f, 0.0f, 5.0f, "%.2f km");
        setting("skyLutCacheKeySplitEnable", "Split LUT Re-bake Keys");
        setting("cloudRenderRTEnable", "Screen Clouds");
        setting("debugDispatchCloudRender", "Dispatch Cloud Render");
        setting("debugDispatchCloudSkyTransmittance", "Dispatch Cloud Sky Transmittance");
        setting("debugDispatchSkyLuts", "Dispatch Sky LUTs");
        setting("debugEnableSkyMissShading", "Sky Miss Shading");
        ImGui::TreePop();
      }

      if (ImGui::TreeNode("All settings")) {
        showAllSettings();
        ImGui::TreePop();
      }
    }
  }

  void showNumosNotes(DxvkContext* ctx) {
    RtxNumos& numosObject = ctx->getCommonObjects()->metaNumos();
    const auto stateLock = numosObject.lockState();
    beginPage(ctx);
    if (numosObject.hasShaderLoadFailed()) {
      ImGui::TextWrapped("The NUMOS shaders failed to load (see the log). Install the whole build output, including the numos folder.");
    }
    if (g_weatherPreset != nullptr) {
      ImGui::TextWrapped("The preset \"%s\" sets the settings tagged \"weather\" while it shows. Edit them in the Weather "
                         "Presets window (Weather tab).",
                         g_weatherPreset->name.c_str());
    }
  }

  void showNumosTabs(DxvkContext* ctx) {
    const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
    beginPage(ctx);
    if (ImGui::BeginTabItem("Sky")) {
      showSkyTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Clouds")) {
      showCloudsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Haze")) {
      showHazeTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Night")) {
      showNightTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Weather")) {
      showWeatherPresetSummary(ctx);
      ImGui::Separator();
      showLightning();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Setup")) {
      showSetupTab(ctx);
      ImGui::EndTabItem();
    }
  }

  void showNumosUserHaze(DxvkContext* ctx) {
    const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
    beginPage(ctx);
    showUseHazeButton();
    weatherNote();
    userSetting("aerialPerspective", "Distance Haze",
                "Adds haze, fog and sun shafts to the air between you and distant terrain, buildings and clouds. "
                "It is the main sense of depth outdoors.");
    ImGui::BeginDisabled(!boolValue("aerialPerspective"));
    userSetting("aerosolDensity", "Haze Amount",
                "How much dust and haze is in the air. 1 is typical. Higher values give a hazier, whiter sky, thicker "
                "haze over distant land and a larger glow around the sun.",
                0.01f, 0.0f, 10.0f, "%.2f");
    userSetting("hazeWorldScale", "Haze World Scale",
                "How far away the haze treats the world as being. 1 is real size. Lower values thin the haze, as if "
                "the land were closer, and higher values thicken it, as if the land were farther away.",
                0.005f, 0.01f, 10.0f, "%.3f");
    userSetting("fogVisibilityMeters", "Ground Fog Visibility",
                "How far you can see through low-lying fog, in meters. Lower values give thicker fog: 50 is thick fog, "
                "1000 mist and 10000 light haze. 0 turns the ground fog off.",
                10.0f, 0.0f, 100000.0f, "%.0f m");
    userSetting("aerialPerspectiveSceneShadow", "Sun Shafts",
                "Lets buildings, trees and doorways shadow the haze. This gives sun shafts and keeps interiors from "
                "filling with sunlit haze. It costs some GPU time.");
    userSetting("aerialPerspectiveLocalLights", "Light Glow in Haze",
                "Lets lamps, fires, muzzle flashes and other lights glow in the nearby haze. It costs more GPU time "
                "where many lights are close together on screen.");
    ImGui::EndDisabled();
  }

  void showNumosUserNight(DxvkContext* ctx) {
    const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
    beginPage(ctx);
    userSetting("nightGainStops", "Night Brightness",
                "Brightens the night: moonlight, starlight and the night sky, together. 0 is physically correct, "
                "which looks darker on a screen than to your eyes at night. Each step of 1 doubles the brightness. "
                "Sunlight, sunsets and your own lights are not changed.",
                0.05f, 0.0f, 10.0f, "+%.1f");
    userSetting("starGainStops", "Star Brightness",
                "Brightens or dims the stars and the Milky Way on top of Night Brightness, without changing the moon. "
                "Each step of 1 doubles the brightness. Daylight is not changed.",
                0.05f, -8.0f, 8.0f, "%+.1f");
    userSetting("moonlightGainStops", "Moonlight Brightness",
                "Brightens or dims the moon's light on the world and the sky, on top of Night Brightness. Lower it if "
                "the moon is too strong next to the stars. Daylight is not changed.",
                0.05f, -8.0f, 8.0f, "%+.1f");
    userSetting("moonDiscGainStops", "Moon Disc Brightness",
                "Brightens or dims only the moon you see in the sky, not its light, so a blown-out moon can be tamed "
                "without darkening the night.",
                0.05f, -8.0f, 8.0f, "%+.1f");
  }

  void showNumosUserClouds(DxvkContext* ctx) {
    const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
    beginPage(ctx);
    weatherNote();
    userSetting("cloudEnabled", "Clouds", "Draws volumetric clouds in the sky.");
    ImGui::BeginDisabled(!boolValue("cloudEnabled"));
    userSetting("cloudCoverageMean", "Cloud Coverage",
                "How much of the sky is covered by cloud, from 0 (clear) to 1 (overcast).",
                0.01f, 0.0f, 1.0f, "%.2f");
    userSetting("cloudDensity", "Cloud Density",
                "How thick and opaque the clouds are. Higher values give darker, more solid clouds.",
                0.1f, 0.0f, 200.0f, "%.1f");
    userSetting("cloudTypeMean", "Cloud Type",
                "The kind of cloud, from 0 to 1: 0 is flat layered cloud, 0.5 broken lumpy cloud and 1 tall, puffy "
                "cumulus.",
                0.01f, 0.0f, 1.0f, "%.2f");
    userSetting("cloudShadowStrength", "Cloud Shadows",
                "How strongly clouds shade the ground and the air below them, from 0 (no cloud shadows) to 1 "
                "(physical).",
                0.01f, 0.0f, 1.0f, "%.2f");
    {
      static const int values[] = { 0, 1 };
      static const char* names[] = { "Quality", "Performance" };
      userChoice("cloudPerformancePreset", "Cloud Quality",
                 "Quality draws the clouds' fine detail and sun shadows in full. Performance is faster: it uses a "
                 "simpler sun shadow, so fine detail is softer and cloud tops look slightly brighter when the sun is low.",
                 values, names, 2);
    }
    {
      static const int values[] = { 0, 1, 2, 3 };
      static const char* names[] = { "Auto", "Full", "Quarter", "Sixteenth" };
      userChoice("cloudMarchRate", "Cloud Resolution",
                 "How many cloud pixels are traced each frame. The rest are rebuilt from their neighbors and earlier "
                 "frames. Auto chooses from your resolution and upscaling. Full traces every pixel and costs the most. "
                 "Quarter costs about a quarter and looks the same when the view is still. Sixteenth is the cheapest, "
                 "but noisy and soft in motion.",
                 values, names, 4);
    }
    ImGui::EndDisabled();
  }

  namespace {
    // Edits a preset's own value of a setting, which is not an option.
    bool editPresetValue(const numos::SettingInfo& info, numos::Settings& values, const std::string& label) {
      char* field = reinterpret_cast<char*>(&values) + info.offset;
      const float mn = info.minValue;
      const float mx = info.maxValue;
      const bool bounded = mn != numos::NUMOS_NO_MIN && mx != numos::NUMOS_NO_MAX;
      bool changed = false;
      switch (info.type) {
      case numos::SettingType::Bool:
        changed = ImGui::Checkbox(label.c_str(), reinterpret_cast<bool*>(field));
        break;
      case numos::SettingType::Int: {
        int32_t* v = reinterpret_cast<int32_t*>(field);
        changed = ImGui::DragInt(label.c_str(), v, 0.1f, mn == numos::NUMOS_NO_MIN ? INT_MIN : int(mn),
                                 mx == numos::NUMOS_NO_MAX ? INT_MAX : int(mx), "%d", ImGuiSliderFlags_AlwaysClamp);
        break;
      }
      case numos::SettingType::Uint: {
        int v = int(std::min(*reinterpret_cast<uint32_t*>(field), uint32_t(INT_MAX)));
        changed = ImGui::DragInt(label.c_str(), &v, 0.1f, mn == numos::NUMOS_NO_MIN ? 0 : int(mn),
                                 mx == numos::NUMOS_NO_MAX ? INT_MAX : int(std::min(mx, float(INT_MAX))), "%d",
                                 ImGuiSliderFlags_AlwaysClamp);
        if (changed) {
          *reinterpret_cast<uint32_t*>(field) = uint32_t(std::max(v, 0));
        }
        break;
      }
      case numos::SettingType::Float: {
        float* v = reinterpret_cast<float*>(field);
        const float speed = bounded ? std::max((mx - mn) / 1000.0f, 1e-5f) : std::max(std::abs(*v) * 0.01f, 1e-4f);
        changed = ImGui::DragFloat(label.c_str(), v, speed, mn, mx, "%.5g", ImGuiSliderFlags_AlwaysClamp);
        break;
      }
      case numos::SettingType::Vec3: {
        float* v = reinterpret_cast<float*>(field);
        const std::string name = info.key.substr(info.key.rfind('.') + 1);
        changed = isColorName(name) ? ImGui::ColorEdit3(label.c_str(), v, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)
                                     : ImGui::DragFloat3(label.c_str(), v, 0.01f, mn, mx, "%.4g");
        break;
      }
      }
      if (info.description && info.description[0]) {
        RemixGui::SetTooltipToLastWidgetOnHover(info.description);
      }
      return changed;
    }

    // The following functions draw the Weather Presets window.

    // Returns the theme's text color moved part way to its disabled text color.
    // Hints and read-only values use it, so they read as secondary and stay legible in every theme.
    ImVec4 hintTextColor() {
      const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
      const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
      constexpr float t = 0.4f;
      return ImVec4(text.x + (dim.x - text.x) * t, text.y + (dim.y - text.y) * t, text.z + (dim.z - text.z) * t, text.w);
    }

    // Draws wrapped text in the hint color.
    void hintText(const std::string& text) {
      ImGui::PushStyleColor(ImGuiCol_Text, hintTextColor());
      ImGui::TextWrapped("%s", text.c_str());
      ImGui::PopStyleColor();
    }

    struct PresetWindow {
      bool open = false;
      std::string selected;
      std::string status;
      char nameBuffer[128] = {};
      std::string nameFor;
      char addIdBuffer[256] = {};
      char settingFilter[128] = {};
      bool showAll = false;
      bool renaming = false;
      bool renameFocus = false;
      bool showTools = false;
      float listWidth = 0.0f;
      char newName[128] = {};
      char loadPath[512] = {};
      char seedText[8192] = {};
      std::string seedStatus;

      // This holds the last nonzero color of each color and strength row by setting index, normalized to a largest channel of 1.
      std::unordered_map<size_t, std::array<float, 3>> rowColors;
    };

    PresetWindow& presetWindow() {
      static PresetWindow window;
      return window;
    }

    void openPresetWindow() {
      presetWindow().open = true;
    }

    // Returns the first id of a comma separated list, which is the most specific one.
    std::string firstId(const std::string& ids) {
      const std::string first = ids.substr(0, ids.find(','));
      size_t b = 0, e = first.size();
      while (b < e && std::isspace(uint8_t(first[b]))) {
        ++b;
      }
      while (e > b && std::isspace(uint8_t(first[e - 1]))) {
        --e;
      }
      return first.substr(b, e - b);
    }

    bool hasBinding(const NumosWeather::Preset& p, const std::string& id) {
      return std::any_of(p.bindings.begin(), p.bindings.end(), [&](const std::string& b) {
        return b.size() == id.size() && std::equal(b.begin(), b.end(), id.begin(), [](char x, char y) {
          return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
        });
      });
    }

    // Returns how much of the sky the preset makes up now, from 0 to 1.
    float showingWeight(NumosWeather& weather, const NumosWeather::Preset& p) {
      const float gameBlend = NumosWeather::gameWeatherBlend();
      const bool gameBlending = weather.preview().empty() && !weather.tuning() && gameBlend >= 0.0f &&
                                NumosWeather::followGame() && weather.boundPreset(NumosWeather::gameWeather()) != nullptr;
      if (p.name == weather.activePreset()) {
        return gameBlending ? std::clamp(gameBlend, 0.0f, 1.0f) : weather.transitionProgress();
      }
      if (gameBlending && weather.boundPreset(NumosWeather::gameWeatherOutgoing()) == &p) {
        return 1.0f - std::clamp(gameBlend, 0.0f, 1.0f);
      }
      return 0.0f;
    }

    enum class RowKind { Number, SpeedMetersPerSecond, Switch, Color, ColorStrength };

    // Describes a curated row of a preset section, with the range of the matching Sky tab control.
    // `follow` is 1 for a row that shows only while layer 2 follows the camera.
    // `follow` is -1 for a row that shows only while layer 2 does not, and 0 for a row that always shows.
    // `fixed` marks a placement or scale setting, which Randomize leaves alone.
    struct PresetRow {
      const char* name;  // relative to rtx.atmosphere.
      const char* label;
      RowKind kind;
      float speed;
      float mn;
      float mx;
      const char* format;
      ImGuiSliderFlags flags;
      int follow;
      bool fixed = false;
    };

    struct PresetSection {
      const char* title;
      std::vector<PresetRow> rows;
    };

    const std::vector<PresetSection>& presetSections() {
      static const std::vector<PresetSection> sections = {
        { "Clouds", {
          { "cloudCoverageMean", "Coverage", RowKind::Number, 0.005f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudCoverageSpread", "Coverage spread", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudCoverageNoiseScale", "Coverage patch frequency", RowKind::Number, 0.0001f, 0.0f, 0.1f, "%.5f", 0, 0 },
          { "cloudTypeMean", "Cloud type (wispy - billowy)", RowKind::Number, 0.005f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudTypeSpread", "Type spread", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudDensity", "Density", RowKind::Number, 0.1f, 0.0f, 200.0f, "%.2f", 0, 0 },
          { "nvdfProfileDepthKm", "Profile depth", RowKind::Number, 0.005f, 0.01f, 3.0f, "%.3f km", 0, 0 },
          { "cloudBaseHeightMeters", "Layer base", RowKind::Number, 10.0f, 0.0f, 12000.0f, "%.0f m", 0, 0, true },
          { "cloudDepthMeters", "Layer depth", RowKind::Number, 10.0f, 50.0f, 8000.0f, "%.0f m", 0, 0 },
          { "nubis3ErosionStrength", "Detail erosion", RowKind::Number, 0.02f, 0.0f, 2.0f, "%.2f", 0, 0 },
          { "cloudBodyBillowSizeKm", "Billow size", RowKind::Number, 0.02f, 0.2f, 4.0f, "%.2f km", 0, 0 },
          { "cloudBodyBillowGain", "Billow gain", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudCurlStrengthKm", "Detail curl", RowKind::Number, 0.005f, 0.0f, 1.0f, "%.3f km", 0, 0 },
          { "cloudDiffuseSunStrength", "Diffuse sunlight", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudColor", "Cloud color", RowKind::Color, 0, 0, 0, nullptr, 0, 0 },
        } },
        { "Second layer (ground fog / dust)", {
          { "cloudLayer2Enable", "Enabled", RowKind::Switch, 0, 0, 0, nullptr, 0, 0 },
          { "cloudLayer2FollowCamera", "Follow the camera", RowKind::Switch, 0, 0, 0, nullptr, 0, 0, true },
          { "cloudLayer2CameraOffsetMeters", "Base relative to the camera", RowKind::Number, 1.0f, -10000.0f, 10000.0f, "%.0f m", 0, 1, true },
          { "cloudLayer2BaseHeightMeters", "Base height", RowKind::Number, 50.0f, 0.0f, 20000.0f, "%.0f m", 0, -1, true },
          { "cloudLayer2DepthMeters", "Depth", RowKind::Number, 10.0f, 10.0f, 6000.0f, "%.0f m", 0, 0 },
          { "cloudLayer2DensityScale", "Density", RowKind::Number, 0.01f, 0.0f, 2.0f, "%.2f", 0, 0 },
          { "cloudLayer2CoverageMean", "Coverage", RowKind::Number, 0.005f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudLayer2TypeMean", "Cloud type", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
          { "cloudLayer2Albedo", "Albedo", RowKind::Color, 0, 0, 0, nullptr, 0, 0 },
        } },
        { "Wind", {
          { "cloudWindSpeed", "Wind speed", RowKind::SpeedMetersPerSecond, 0.5f, 0.0f, 1000.0f, "%.1f m/s", 0, 0 },
          { "cloudWindDirection", "Wind direction", RowKind::Number, 1.0f, 0.0f, 360.0f, "%.1f deg", 0, 0 },
        } },
        { "Atmosphere", {
          { "airTint", "Air color", RowKind::Color, 0, 0, 0, nullptr, 0, 0 },
          { "airDensity", "Air density", RowKind::Number, 0.01f, 0.0f, 100.0f, "%.2f", 0, 0 },
          { "aerosolTint", "Haze color", RowKind::Color, 0, 0, 0, nullptr, 0, 0 },
          { "aerosolDensity", "Haze density", RowKind::Number, 0.05f, 0.0f, 100.0f, "%.2f", 0, 0 },
          { "hazeWorldScale", "Haze world scale", RowKind::Number, 0.005f, 0.01f, 10.0f, "%.3f", 0, 0, true },
        } },
        { "Sun", {
          { "sunIlluminance", "Sunlight", RowKind::ColorStrength, 0.1f, 0.0f, 100.0f, "%.1f", 0, 0 },
        } },
        { "Lightning", {
          { "lightningStrikesPerMinute", "Strikes per minute", RowKind::Number, 0.1f, 0.0f, 60.0f, "%.1f", 0, 0 },
          { "lightningColor", "Flash color", RowKind::Color, 0, 0, 0, nullptr, 0, 0 },
        } },
        { "Fog (height fog)", {
          { "fogVisibilityMeters", "Visibility", RowKind::Number, 10.0f, 0.0f, 100000.0f, "%.0f m", ImGuiSliderFlags_Logarithmic, 0 },
          { "fogBaseHeightMeters", "Base height", RowKind::Number, 1.0f, -10000.0f, 10000.0f, "%.0f m", 0, 0, true },
          { "fogHeightFalloffMeters", "Height falloff", RowKind::Number, 1.0f, 1.0f, 10000.0f, "%.0f m", 0, 0 },
          { "fogAlbedo", "Albedo", RowKind::Number, 0.01f, 0.0f, 1.0f, "%.2f", 0, 0 },
        } },
      };
      return sections;
    }

    const PresetRow* curatedRow(const std::string& key) {
      for (const PresetSection& section : presetSections()) {
        for (const PresetRow& row : section.rows) {
          if (key == std::string(kKeyPrefix) + row.name) {
            return &row;
          }
        }
      }
      return nullptr;
    }

    // Flags the settings that a curated row shows, in the order of numos::settingInfos().
    const std::vector<bool>& curatedKeys() {
      static const std::vector<bool> keys = [] {
        const auto& infos = numos::settingInfos();
        std::vector<bool> k(infos.size(), false);
        for (size_t i = 0; i < infos.size(); ++i) {
          k[i] = curatedRow(infos[i].key) != nullptr;
        }
        return k;
      }();
      return keys;
    }

    template <typename T>
    T& presetField(numos::Settings& s, const numos::SettingInfo& info) {
      return *reinterpret_cast<T*>(reinterpret_cast<char*>(&s) + info.offset);
    }

    template <typename T>
    const T& presetField(const numos::Settings& s, const numos::SettingInfo& info) {
      return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&s) + info.offset);
    }

    // Returns the label of a setting without a curated row, which is its key in words.
    std::string otherLabel(const numos::SettingInfo& info) {
      return labelFor(info.key.rfind(kKeyPrefix, 0) == 0 ? info.key.substr(std::strlen(kKeyPrefix)) : info.key);
    }

    // Returns the width of the longest row label of the preset, so every label shows in full.
    float presetLabelWidth(const NumosWeather::Preset& p) {
      float width = ImGui::CalcTextSize("Transition").x;
      for (const PresetSection& section : presetSections()) {
        for (const PresetRow& row : section.rows) {
          width = std::max(width, ImGui::CalcTextSize(row.label).x);
        }
      }
      const auto& infos = numos::settingInfos();
      for (size_t i = 0; i < infos.size(); ++i) {
        if (p.set[i] && !curatedKeys()[i]) {
          width = std::max(width, ImGui::CalcTextSize(otherLabel(infos[i]).c_str()).x);
        }
      }
      return width + ImGui::GetStyle().ItemSpacing.x;
    }

    // Returns the fixed width of every value control, so values line up close to their labels.
    float presetValueWidth() {
      return ImGui::GetFontSize() * 14.0f;
    }

    // Returns the color of marks that need attention, such as unsaved changes and tuning.
    ImVec4 attentionColor() {
      return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
    }

    // Puts the next item on the same line when `width` more pixels fit before the screen x `right`.
    void sameLineIfFits(float width, float right) {
      if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right) {
        ImGui::SameLine();
      }
    }

    // Returns the width that Button() and SmallButton() give a label.
    float buttonWidth(const char* label) {
      return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    // Begins a table with columns for a label, a value, an override button and a note.
    bool beginSettingTable(const char* id, float labelWidth) {
      if (!ImGui::BeginTable(id, 4, ImGuiTableFlags_SizingFixedFit)) {
        return false;
      }
      ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
      ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthFixed, presetValueWidth());
      ImGui::TableSetupColumn("##action", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
      ImGui::TableSetupColumn("##note", ImGuiTableColumnFlags_WidthStretch);
      return true;
    }

    // Shows the tooltip of a setting row, which also explains the click on an inherited row.
    void settingTooltip(bool own, const char* description) {
      if (own) {
        RemixGui::SetTooltipToLastWidgetOnHover(description);
        return;
      }
      const std::string tip = std::string(description) + (description[0] ? "\n\n" : "") +
                              "Inherited: the sky shows this value without the preset. Click to override it in this preset.";
      RemixGui::SetTooltipToLastWidgetOnHover(tip.c_str());
    }

    const numos::SettingInfo* rowInfo(const PresetRow& row) {
      return numos::findSetting(std::string(kKeyPrefix) + row.name);
    }

    size_t settingIndex(const numos::SettingInfo& info) {
      return size_t(&info - numos::settingInfos().data());
    }

    // Draws the table row of a curated setting.
    // An inherited row shows the sky's value as read-only, and a click on it adds the override.
    // A multiplier shows its result on the user's setting, and an edit stores that as an absolute value.
    void presetRow(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window, const PresetRow& row) {
      const numos::SettingInfo* info = rowInfo(row);
      const ImVec4 hint = hintTextColor();
      const float valueWidth = presetValueWidth();
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      if (info == nullptr) {
        ImGui::TextColored(hint, "%s", row.label);
        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(hint, "not in this NUMOS version");
        const std::string key = std::string(kKeyPrefix) + row.name;
        RemixGui::SetTooltipToLastWidgetOnHover(key.c_str());
        return;
      }
      const size_t i = settingIndex(*info);
      const char* description = info->description ? info->description : "";
      const bool own = p.set[i];
      const bool multiplier = own && p.relative[i];
      bool addOverride = false;
      ImGui::PushID(info->key.c_str());

      ImGui::TextColored(own ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : hint, "%s", row.label);
      addOverride |= !own && ImGui::IsItemClicked();
      settingTooltip(own, description);

      // An inherited control keeps full opacity and reads as read-only by its hint-colored text and faint field.
      ImGui::TableSetColumnIndex(1);
      if (!own) {
        ImVec4 field = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        field.w *= 0.4f;
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, hint);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, field);
      }
      const numos::Settings& base = weather.lastBase();
      const numos::Settings& sky = weather.current();
      bool edited = false;
      ImGui::BeginDisabled(!own);
      switch (row.kind) {
      case RowKind::Switch: {
        bool v = own ? presetField<bool>(*p.values, *info) : presetField<bool>(sky, *info);
        edited = ImGui::Checkbox("##value", &v);
        if (edited && own) {
          presetField<bool>(*p.values, *info) = v;
        }
        break;
      }
      case RowKind::Number:
      case RowKind::SpeedMetersPerSecond: {
        // Speeds are stored in km/s and shown in m/s.
        const float scale = row.kind == RowKind::SpeedMetersPerSecond ? 1000.0f : 1.0f;
        float v = !own ? presetField<float>(sky, *info)
                : multiplier ? presetField<float>(*p.values, *info) * presetField<float>(base, *info)
                : presetField<float>(*p.values, *info);
        v *= scale;
        ImGui::SetNextItemWidth(valueWidth);
        edited = ImGui::DragFloat("##value", &v, row.speed, row.mn, row.mx, row.format, row.flags | kClamp);
        if (edited && own) {
          presetField<float>(*p.values, *info) = v / scale;
        }
        break;
      }
      case RowKind::Color:
      case RowKind::ColorStrength: {
        numos::Vector3 v = presetField<numos::Vector3>(sky, *info);
        if (own) {
          v = presetField<numos::Vector3>(*p.values, *info);
          if (multiplier) {
            const numos::Vector3& b = presetField<numos::Vector3>(base, *info);
            v = numos::Vector3(v.x * b.x, v.y * b.y, v.z * b.z);
          }
        }
        if (row.kind == RowKind::Color) {
          float c[3] = { v.x, v.y, v.z };
          ImGui::SetNextItemWidth(valueWidth);
          edited = ImGui::ColorEdit3("##value", c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
          v = numos::Vector3(c[0], c[1], c[2]);
        } else {
          // An HDR value is split into a color and a strength, which is its largest channel.
          // A black value shows the row's last nonzero color or white, so a strength of zero keeps the hue.
          auto& hue = window.rowColors.try_emplace(i, std::array<float, 3>{ 1.0f, 1.0f, 1.0f }).first->second;
          const float strength = std::max(std::max(v.x, v.y), std::max(v.z, 0.0f));
          if (strength > 0.0f) {
            hue = { v.x / strength, v.y / strength, v.z / strength };
          }
          float c[3] = { hue[0], hue[1], hue[2] };
          float s = strength;
          edited = ImGui::ColorEdit3("##value", c, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);
          ImGui::SameLine();
          ImGui::SetNextItemWidth(std::max(valueWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x, 1.0f));
          const std::string format = std::string("strength ") + row.format;
          edited |= ImGui::DragFloat("##strength", &s, row.speed, row.mn, row.mx, format.c_str(), kClamp);
          const float cmax = std::max(std::max(c[0], c[1]), c[2]);
          if (cmax > 0.0f) {
            hue = { c[0] / cmax, c[1] / cmax, c[2] / cmax };
          }
          v = numos::Vector3(hue[0] * s, hue[1] * s, hue[2] * s);
        }
        if (edited && own) {
          presetField<numos::Vector3>(*p.values, *info) = v;
        }
        break;
      }
      }
      ImGui::EndDisabled();
      if (!own) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        addOverride |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
      }
      settingTooltip(own, description);
      if (edited && own) {
        p.relative[i] = false;
        p.dirty = true;
      }

      ImGui::TableSetColumnIndex(2);
      if (own) {
        if (ImGui::SmallButton("x")) {
          weather.removeKey(p, i);
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Stop overriding this setting. The sky goes back to the inherited value.");
      } else {
        addOverride |= ImGui::SmallButton("+");
        RemixGui::SetTooltipToLastWidgetOnHover("Override this setting in this preset, starting from the value shown.");
      }

      if (multiplier && !edited) {
        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(hint, "(x your setting)");
        RemixGui::SetTooltipToLastWidgetOnHover("Stored as a multiplier on your own value, so it follows your tuning. Editing it "
                                                "here stores the shown value as an absolute one (Revert undoes that).");
      }
      if (addOverride) {
        weather.addKey(p, i);
      }
      ImGui::PopID();
    }

    // Draws the table row of an overridden setting that has no curated row.
    // A multiplier shows and edits the stored factor.
    void otherRow(NumosWeather& weather, NumosWeather::Preset& p, size_t i) {
      const numos::SettingInfo& info = numos::settingInfos()[i];
      const bool relative = p.relative[i];
      ImGui::TableNextRow();
      ImGui::PushID(info.key.c_str());
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(otherLabel(info).c_str());
      RemixGui::SetTooltipToLastWidgetOnHover(info.key.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(presetValueWidth());
      if (editPresetValue(info, *p.values, "##value")) {
        p.dirty = true;
      }
      ImGui::TableSetColumnIndex(2);
      if (ImGui::SmallButton("x")) {
        weather.removeKey(p, i);
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Stop overriding this setting. The sky goes back to the inherited value.");
      if (relative) {
        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(hintTextColor(), "(multiplier)");
        RemixGui::SetTooltipToLastWidgetOnHover("This value multiplies your own setting, so the preset follows your tuning.");
      }
      ImGui::PopID();
    }

    // Draws a searchable list of the settings that the preset does not override.
    // A click adds the override.
    void addSettingPopup(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window) {
      if (!ImGui::BeginPopup("AddSettingPopup")) {
        return;
      }
      const float width = ImGui::GetFontSize() * 22.0f;
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SetNextItemWidth(width);
      ImGui::InputTextWithHint("##search", "Search settings", window.settingFilter, sizeof(window.settingFilter));
      const std::string needle = window.settingFilter;
      const ImVec4 hint = hintTextColor();
      size_t addIndex = SIZE_MAX;
      bool any = false;
      ImGui::BeginChild("settings", ImVec2(width, ImGui::GetTextLineHeightWithSpacing() * 16.0f), false);
      for (const PresetSection& section : presetSections()) {
        bool header = false;
        for (const PresetRow& row : section.rows) {
          const numos::SettingInfo* info = rowInfo(row);
          if (info == nullptr || p.set[settingIndex(*info)]) {
            continue;
          }
          if (!containsNoCase(row.label, needle) && !containsNoCase(section.title, needle) && !containsNoCase(info->key, needle)) {
            continue;
          }
          if (!header) {
            ImGui::TextColored(hint, "%s", section.title);
            header = true;
          }
          ImGui::PushID(info->key.c_str());
          ImGui::Indent();
          if (ImGui::Selectable(row.label)) {
            addIndex = settingIndex(*info);
          }
          ImGui::Unindent();
          if (info->description && info->description[0]) {
            RemixGui::SetTooltipToLastWidgetOnHover(info->description);
          }
          ImGui::PopID();
          any = true;
        }
      }
      const auto& infos = numos::settingInfos();
      bool header = false;
      for (size_t i = 0; i < infos.size(); ++i) {
        if (p.set[i] || curatedKeys()[i] || (infos[i].flags & numos::NUMOS_SETTING_NO_SAVE)) {
          continue;
        }
        const std::string label = otherLabel(infos[i]);
        if (!containsNoCase(label, needle) && !containsNoCase(infos[i].key, needle)) {
          continue;
        }
        if (!header) {
          ImGui::TextColored(hint, "Other settings");
          header = true;
        }
        ImGui::PushID(infos[i].key.c_str());
        ImGui::Indent();
        if (ImGui::Selectable(label.c_str())) {
          addIndex = i;
        }
        ImGui::Unindent();
        RemixGui::SetTooltipToLastWidgetOnHover(infos[i].key.c_str());
        ImGui::PopID();
        any = true;
      }
      if (!any) {
        hintText("No setting matches.");
      }
      ImGui::EndChild();
      if (addIndex != SIZE_MAX) {
        weather.addKey(p, addIndex);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Draws the preset's settings by section, and only the overridden ones by default.
    void presetSettings(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window, float labelWidth) {
      const auto& infos = numos::settingInfos();
      const size_t total = size_t(std::count(p.set.begin(), p.set.end(), true));
      if (ImGui::Button("Add setting...")) {
        window.settingFilter[0] = '\0';
        ImGui::OpenPopup("AddSettingPopup");
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Lists every setting this preset does not override yet. Pick one to override it.");
      addSettingPopup(weather, p, window);
      ImGui::SameLine();
      ImGui::Checkbox("Show all settings", &window.showAll);
      RemixGui::SetTooltipToLastWidgetOnHover("Also lists the settings this preset does not override, dimmed at the value the sky "
                                              "shows without the preset. Click one to override it.");
      ImGui::SameLine();
      ImGui::TextColored(hintTextColor(), "%d overridden", int(total));
      RemixGui::SetTooltipToLastWidgetOnHover("Edits apply live. Save writes the preset file.");

      if (total == 0 && !window.showAll) {
        hintText("This preset overrides no settings, so the sky shows the inherited ones. Add a setting, or show all settings.");
        return;
      }

      // The base of layer 2 is relative to the camera while it follows, and is a height otherwise.
      const numos::SettingInfo* follow = numos::findSetting("rtx.atmosphere.cloudLayer2FollowCamera");
      bool follows = false;
      if (follow != nullptr) {
        follows = p.set[settingIndex(*follow)] ? presetField<bool>(*p.values, *follow) : presetField<bool>(weather.current(), *follow);
      }

      for (const PresetSection& section : presetSections()) {
        size_t count = 0;
        for (const PresetRow& row : section.rows) {
          const numos::SettingInfo* info = rowInfo(row);
          count += info != nullptr && p.set[settingIndex(*info)];
        }
        if (count == 0 && !window.showAll) {
          continue;
        }
        const std::string title = count ? std::string(section.title) + " (" + std::to_string(count) + ")###" + section.title
                                        : std::string(section.title) + "###" + section.title;
        if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          continue;
        }
        if (beginSettingTable(section.title, labelWidth)) {
          for (const PresetRow& row : section.rows) {
            const numos::SettingInfo* info = rowInfo(row);
            const bool own = info != nullptr && p.set[settingIndex(*info)];
            const bool followMismatch = (row.follow > 0 && !follows) || (row.follow < 0 && follows);
            if (own || (window.showAll && !followMismatch)) {
              presetRow(weather, p, window, row);
            }
          }
          ImGui::EndTable();
        }
        ImGui::Spacing();
      }

      size_t others = 0;
      for (size_t i = 0; i < infos.size(); ++i) {
        others += p.set[i] && !curatedKeys()[i];
      }
      if (others == 0) {
        return;
      }
      const std::string title = "Other settings (" + std::to_string(others) + ")###Other settings";
      if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen) && beginSettingTable("Other settings", labelWidth)) {
        for (size_t i = 0; i < infos.size(); ++i) {
          if (p.set[i] && !curatedKeys()[i]) {
            otherRow(weather, p, i);
          }
        }
        ImGui::EndTable();
      }
    }

    // Draws the weathers of the preset as removable chips, with a button that adds one.
    void presetWeathers(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window) {
      const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
      for (size_t i = 0; i < p.bindings.size(); ++i) {
        const std::string label = p.bindings[i] + "  x";
        if (i > 0) {
          sameLineIfFits(buttonWidth(label.c_str()), right);
        }
        ImGui::PushID(int(i));
        const bool remove = ImGui::SmallButton(label.c_str());
        RemixGui::SetTooltipToLastWidgetOnHover("Remove this weather from the preset.");
        ImGui::PopID();
        if (remove) {
          p.bindings.erase(p.bindings.begin() + i);
          p.dirty = true;
          break;
        }
      }
      ImGui::PopStyleColor();
      if (p.bindings.empty()) {
        ImGui::TextColored(hintTextColor(), "None");
        RemixGui::SetTooltipToLastWidgetOnHover("This preset shows only when chosen or previewed.");
      }
      sameLineIfFits(buttonWidth("+ Add"), right);
      if (ImGui::SmallButton("+ Add")) {
        window.addIdBuffer[0] = '\0';
        ImGui::OpenPopup("AddWeatherPopup");
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Binds a game weather to this preset.");

      if (!ImGui::BeginPopup("AddWeatherPopup")) {
        return;
      }
      const std::string current = firstId(NumosWeather::gameWeather());
      NumosWeather::Preset* owner = current.empty() ? nullptr : weather.boundPreset(current);
      ImGui::BeginDisabled(current.empty() || hasBinding(p, current));
      const std::string addCurrent = (current.empty() ? std::string("Add the current weather") : "Add the current weather: " + current) +
                                     "###addCurrent";
      if (ImGui::Button(addCurrent.c_str())) {
        if (owner != nullptr && owner != &p) {
          // A weather belongs to one preset.
          owner->bindings.erase(std::remove_if(owner->bindings.begin(), owner->bindings.end(), [&](const std::string& b) {
            return b.size() == current.size() && std::equal(b.begin(), b.end(), current.begin(), [](char x, char y) {
              return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
            });
          }), owner->bindings.end());
          owner->dirty = true;
        }
        p.bindings.push_back(current);
        p.dirty = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      const std::string addTip = current.empty() ? std::string("The game reports no weather now.")
        : "Binds " + current + " (the most specific id the game reports)" +
          (owner != nullptr && owner != &p ? ", moving it here from " + owner->name + "." : ".");
      RemixGui::SetTooltipToLastWidgetOnHover(addTip.c_str());
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
      const bool enter = ImGui::InputTextWithHint("##addId", "weather id", window.addIdBuffer, sizeof(window.addIdBuffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      if (ImGui::Button("Add by id") || enter) {
        const std::string id = firstId(window.addIdBuffer);
        if (!id.empty() && !hasBinding(p, id)) {
          p.bindings.push_back(id);
          p.dirty = true;
        }
        window.addIdBuffer[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Binds any id the game reports, such as a number or a name.");
      ImGui::EndPopup();
    }

    // Draws a popup that asks for a name.
    // Returns true on the frame the name is confirmed, with the name in `out`.
    bool namePopup(const char* id, const char* prompt, PresetWindow& window, std::string& out) {
      bool confirmed = false;
      if (ImGui::BeginPopup(id)) {
        ImGui::TextUnformatted(prompt);
        if (ImGui::IsWindowAppearing()) {
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(260.0f);
        const bool enter = ImGui::InputText("##name", window.newName, sizeof(window.newName), ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("OK") || enter) && window.newName[0]) {
          out = window.newName;
          confirmed = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      return confirmed;
    }

    void openNamePopup(const char* id, PresetWindow& window, const std::string& suggestion) {
      std::snprintf(window.newName, sizeof(window.newName), "%s", suggestion.c_str());
      ImGui::OpenPopup(id);
    }

    // Saves a new preset and selects it.
    // A caller cannot use a reference into the preset list afterwards, because adding a preset moves the others.
    void saveNew(NumosWeather& weather, NumosWeather::Preset& created, PresetWindow& window, const char* what) {
      std::string error;
      window.selected = created.name;
      window.nameFor.clear();
      window.status = weather.save(created, &error) ? std::string(what) + " " + created.name + ".conf."
                                                    : "Save failed: " + error;
    }

    // Draws the preset's name, which a click turns into a text field.
    // The field renames the preset and its file when it loses focus, and Escape restores the old name.
    void presetName(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window) {
      if (!window.renaming) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(p.name.c_str());
        if (ImGui::IsItemClicked()) {
          window.renaming = true;
          window.renameFocus = true;
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Click to rename the preset and its file.");
        if (p.dirty) {
          ImGui::SameLine();
          ImGui::TextColored(attentionColor(), "unsaved");
        }
        return;
      }
      if (window.renameFocus) {
        ImGui::SetKeyboardFocusHere();
        window.renameFocus = false;
      }
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
      ImGui::InputText("##name", window.nameBuffer, sizeof(window.nameBuffer));
      if (ImGui::IsItemDeactivated()) {
        if (p.name != window.nameBuffer) {
          std::string error;
          window.status = weather.rename(p, window.nameBuffer, &error) ? "Renamed to " + p.name + "." : "Rename failed: " + error;
          window.selected = p.name;
        }
        window.renaming = false;
        window.nameFor.clear();
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Renames the preset and its file. Escape keeps the old name.");
    }

    // Draws the selected preset's editor.
    // Returns false when the preset list changed, so the caller stops drawing it this frame.
    bool presetEditor(NumosWeather& weather, NumosWeather::Preset& p, PresetWindow& window) {
      if (window.nameFor != p.name) {
        std::snprintf(window.nameBuffer, sizeof(window.nameBuffer), "%s", p.name.c_str());
        window.nameFor = p.name;
        window.renaming = false;
      }
      const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

      // The header line holds the name, the switches, the actions and the menu of rarer actions.
      presetName(weather, p, window);
      sameLineIfFits(ImGui::GetFrameHeight() + ImGui::CalcTextSize("Enabled").x + ImGui::GetStyle().ItemInnerSpacing.x, right);
      if (ImGui::Checkbox("Enabled", &p.enabled)) {
        p.dirty = true;
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Off: the preset ignores its weathers (it still shows when chosen or previewed).");
      sameLineIfFits(ImGui::GetFrameHeight() + ImGui::CalcTextSize("Preview").x + ImGui::GetStyle().ItemInnerSpacing.x, right);
      bool preview = weather.preview() == p.name;
      if (ImGui::Checkbox("Preview", &preview)) {
        weather.setPreview(preview ? p.name : std::string());
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Shows this preset whatever the weather, until unticked. Edits show live.");
      sameLineIfFits(buttonWidth("Revert"), right);
      ImGui::BeginDisabled(!weather.hasSavedCopy(p) || !p.dirty || weather.tuning());
      if (ImGui::Button("Revert")) {
        weather.revert(p);
        window.nameFor.clear();
        window.status = "Reverted " + p.name + " to its file.";
      }
      ImGui::EndDisabled();
      RemixGui::SetTooltipToLastWidgetOnHover("Back to this preset as last saved.");
      if (p.numosBuiltIn) {
        sameLineIfFits(buttonWidth("Reset to built-in"), right);
        if (ImGui::Button("Reset to built-in")) {
          weather.resetToNumos(p);
          window.status = "Reset " + p.name + " to the built-in definition (not saved).";
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Replaces the settings with the sky library's own definition of this preset. Weathers stay.");
      }
      sameLineIfFits(buttonWidth("..."), right);
      if (ImGui::Button("...###PresetMenuButton")) {
        ImGui::OpenPopup("PresetMenu");
      }
      RemixGui::SetTooltipToLastWidgetOnHover("More actions for this preset.");
      bool saveAs = false;
      if (ImGui::BeginPopup("PresetMenu")) {
        if (ImGui::MenuItem("Rename")) {
          window.renaming = true;
          window.renameFocus = true;
        }
        if (ImGui::MenuItem("Save as new file...", nullptr, false, !weather.tuning())) {
          saveAs = true;
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Saves this preset, edits included, as a new preset file under a new name. "
                                                "Weathers are not copied: a weather belongs to one preset.");
        ImGui::EndPopup();
      }
      if (saveAs) {
        openNamePopup("SaveAsPopup", window, weather.uniqueName(p.name + " copy"));
      }
      std::string newName;
      if (namePopup("SaveAsPopup", "Name of the new preset file:", window, newName)) {
        const NumosWeather::Preset copy = p;
        saveNew(weather, weather.create(weather.uniqueName(newName), &copy), window, "Saved as");
        return false;
      }

      // The weathers and the transition time are aligned with the setting rows below them.
      ImGui::Spacing();
      const float labelWidth = presetLabelWidth(p);
      if (ImGui::BeginTable("presetProperties", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Weathers");
        RemixGui::SetTooltipToLastWidgetOnHover("The game weathers this preset applies to.");
        ImGui::TableSetColumnIndex(1);
        presetWeathers(weather, p, window);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Transition");
        ImGui::TableSetColumnIndex(1);
        bool ownTransition = p.transitionSeconds >= 0.0f;
        if (ImGui::Checkbox("Own time", &ownTransition)) {
          p.transitionSeconds = ownTransition ? NumosWeather::transitionSeconds() : -1.0f;
          p.dirty = true;
        }
        RemixGui::SetTooltipToLastWidgetOnHover("How long the sky takes to blend into this preset. Off: the Ease time.");
        ImGui::SameLine();
        if (ownTransition) {
          ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
          if (ImGui::DragFloat("##PresetTransition", &p.transitionSeconds, 0.1f, 0.0f, 600.0f, "%.1f s", kClamp)) {
            p.dirty = true;
          }
        } else {
          ImGui::TextColored(hintTextColor(), "Ease time, %.1f s", NumosWeather::transitionSeconds());
        }
        ImGui::EndTable();
      }

      ImGui::Separator();
      presetSettings(weather, p, window, labelWidth);
      return true;
    }

    // Returns the name of a preset in the list, with its unsaved mark.
    std::string presetListName(const NumosWeather::Preset& p) {
      return p.name + (p.dirty ? " *" : "");
    }

    // Draws a row with the name on the left and the share of the sky it makes up on the right.
    // The share shows only while it is above zero, and a disabled preset's name uses the hint color.
    void presetListEntries(NumosWeather& weather, PresetWindow& window, bool builtIn) {
      const ImU32 hint = ImGui::GetColorU32(hintTextColor());
      for (const NumosWeather::Preset& p : weather.presets()) {
        if (p.numosBuiltIn != builtIn) {
          continue;
        }
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float right = pos.x + ImGui::GetContentRegionAvail().x;
        const std::string id = "###preset_" + p.name;
        if (ImGui::Selectable(id.c_str(), window.selected == p.name)) {
          window.selected = p.name;
        }
        const float weight = showingWeight(weather, p) * 100.0f;
        char percent[16] = {};
        if (weight >= 0.5f) {
          std::snprintf(percent, sizeof(percent), "%.0f%%", weight);
        }
        const float percentWidth = percent[0] ? ImGui::CalcTextSize(percent).x + ImGui::GetStyle().ItemSpacing.x : 0.0f;
        const std::string name = presetListName(p);
        const float nameRight = right - percentWidth;
        if (!p.enabled) {
          const std::string tip = name + " is off: it ignores its weathers.";
          RemixGui::SetTooltipToLastWidgetOnHover(tip.c_str());
        } else if (pos.x + ImGui::CalcTextSize(name.c_str()).x > nameRight) {
          RemixGui::SetTooltipToLastWidgetOnHover(name.c_str());
        }
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(pos, ImVec2(nameRight, pos.y + ImGui::GetTextLineHeight()), true);
        draw->AddText(pos, p.enabled ? ImGui::GetColorU32(ImGuiCol_Text) : hint, name.c_str());
        draw->PopClipRect();
        if (percent[0]) {
          draw->AddText(ImVec2(right - ImGui::CalcTextSize(percent).x, pos.y), hint, percent);
        }
      }
    }

    // Returns a width for the longest name, its share and the list's buttons, up to 40% of the window.
    float presetListWidth(NumosWeather& weather) {
      const ImGuiStyle& style = ImGui::GetStyle();
      float names = 0.0f;
      for (const NumosWeather::Preset& p : weather.presets()) {
        names = std::max(names, ImGui::CalcTextSize(presetListName(p).c_str()).x);
      }
      const float rows = names + style.ItemSpacing.x + ImGui::CalcTextSize("100%").x;
      const float buttons = buttonWidth("Add") + buttonWidth("Duplicate") + buttonWidth("Delete") + style.ItemSpacing.x * 2.0f;
      const float content = std::max(rows, buttons) + style.WindowPadding.x * 2.0f + style.ScrollbarSize;
      return std::min(content, ImGui::GetContentRegionAvail().x * 0.4f);
    }

    // Draws the custom presets, the built-in presets, and the buttons that change the list.
    void presetList(NumosWeather& weather, PresetWindow& window) {
      const ImVec4 hint = hintTextColor();
      ImGui::BeginGroup();
      ImGui::BeginChild("presetList", ImVec2(window.listWidth, -ImGui::GetFrameHeightWithSpacing()), true);
      const bool anyCustom = std::any_of(weather.presets().begin(), weather.presets().end(),
                                         [](const NumosWeather::Preset& p) { return !p.numosBuiltIn; });
      if (anyCustom) {
        ImGui::TextColored(hint, "Custom");
        RemixGui::SetTooltipToLastWidgetOnHover("Every preset that is not built in: the game's and your own.");
        ImGui::Separator();
        presetListEntries(weather, window, false);
        ImGui::Spacing();
      }
      ImGui::TextColored(hint, "Built-in");
      RemixGui::SetTooltipToLastWidgetOnHover("The sky library's own presets (Clear ... Altostratus), as files that can be edited and "
                                              "bound like any other.");
      ImGui::Separator();
      presetListEntries(weather, window, true);
      ImGui::EndChild();

      ImGui::BeginDisabled(weather.tuning());
      if (ImGui::SmallButton("Add")) {
        const NumosWeather::Preset empty;
        window.selected = weather.create(weather.uniqueName("New preset"), &empty).name;
        window.status = "Added " + window.selected + " (not saved).";
      }
      RemixGui::SetTooltipToLastWidgetOnHover("A new empty preset: add the settings it should override.");
      ImGui::SameLine();
      NumosWeather::Preset* selected = weather.find(window.selected);
      ImGui::BeginDisabled(selected == nullptr);
      if (ImGui::SmallButton("Duplicate") && selected != nullptr) {
        const NumosWeather::Preset copy = *selected;
        window.selected = weather.create(weather.uniqueName(copy.name + " copy"), &copy).name;
        window.status = "Duplicated as " + window.selected + " (not saved). Its weathers are not copied, because a weather belongs to one preset.";
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) {
        ImGui::OpenPopup("DeletePopup");
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Deletes the selected preset and its file.");
      ImGui::EndDisabled();
      ImGui::EndDisabled();

      selected = weather.find(window.selected);
      if (ImGui::BeginPopup("DeletePopup")) {
        if (selected == nullptr) {
          ImGui::CloseCurrentPopup();
        } else {
          ImGui::Text("Delete %s.conf?", selected->name.c_str());
          if (ImGui::Button("Delete the file")) {
            std::string error;
            const std::string name = selected->name;
            window.status = weather.remove(name, &error) ? "Deleted " + name + ".conf." : "Delete failed: " + error;
            window.selected.clear();
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Keep")) {
            ImGui::CloseCurrentPopup();
          }
        }
        ImGui::EndPopup();
      }
      ImGui::EndGroup();
    }

    // Draws the preset list and the selected preset's editor, split by a bar that can be dragged.
    void presetPanes(NumosWeather& weather, PresetWindow& window) {
      if (weather.find(window.selected) == nullptr) {
        window.selected = weather.presets().empty() ? std::string() : weather.presets().front().name;
      }
      const float avail = ImGui::GetContentRegionAvail().x;
      if (window.listWidth <= 0.0f) {
        window.listWidth = presetListWidth(weather);
      }
      const float minWidth = ImGui::GetFontSize() * 8.0f;
      window.listWidth = std::clamp(window.listWidth, minWidth, std::max(minWidth, avail * 0.5f));
      presetList(weather, window);

      ImGui::SameLine(0.0f, 0.0f);
      const float splitterWidth = std::max(ImGui::GetStyle().ItemSpacing.x * 2.0f, 6.0f);
      const ImVec2 splitterPos = ImGui::GetCursorScreenPos();
      const float splitterHeight = std::max(ImGui::GetContentRegionAvail().y, 1.0f);
      ImGui::InvisibleButton("##splitter", ImVec2(splitterWidth, splitterHeight));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = ImGui::IsItemActive();
      if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
      if (active) {
        window.listWidth += ImGui::GetIO().MouseDelta.x;
      }
      const ImU32 lineColor = ImGui::GetColorU32(active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
      const float lineX = std::floor(splitterPos.x + splitterWidth * 0.5f);
      ImGui::GetWindowDrawList()->AddLine(ImVec2(lineX, splitterPos.y), ImVec2(lineX, splitterPos.y + splitterHeight), lineColor,
                                          active || hovered ? 2.0f : 1.0f);

      ImGui::SameLine(0.0f, 0.0f);
      ImGui::BeginChild("presetEdit", ImVec2(0.0f, 0.0f), false);
      if (NumosWeather::Preset* p = weather.find(window.selected)) {
        presetEditor(weather, *p, window);
      } else {
        hintText("Select or add a preset.");
      }
      ImGui::EndChild();
    }

    // Draws the Files tab, which loads a preset file and sets the preset folder.
    void presetFiles(NumosWeather& weather, PresetWindow& window) {
      ImGui::BeginDisabled(weather.tuning());
      if (ImGui::Button("Load preset file...")) {
        ImGui::OpenPopup("LoadFilePopup");
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Loads a preset file from anywhere (a shared one, or a backup) into the preset folder.");
      if (ImGui::BeginPopup("LoadFilePopup")) {
        ImGui::TextUnformatted("Path of the preset file (.conf), absolute or relative to the game's folder:");
        if (ImGui::IsWindowAppearing()) {
          ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(420.0f);
        const bool enter = ImGui::InputText("##path", window.loadPath, sizeof(window.loadPath), ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Load") || enter) && window.loadPath[0]) {
          std::string path = window.loadPath;
          path.erase(std::remove(path.begin(), path.end(), '"'), path.end());
          std::string error;
          if (NumosWeather::Preset* loaded = weather.loadFile(path, &error)) {
            window.selected = loaded->name;
            window.nameFor.clear();
            window.status = "Loaded " + path + " as " + loaded->name + ".";
          } else {
            window.status = "Load failed: " + error;
          }
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      ImGui::EndDisabled();
      hintText("Folder in use: " + weather.directory());

      if (RemixGui::InputText("Preset folder", &NumosWeather::presetDirectoryObject())) {
        window.status = "Folder changed: press Reload to load it.";
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Folder of the preset files, relative to the game's folder unless absolute.");
    }

    // The following functions handle seeds.

    std::string currentSeed(DxvkContext* ctx, const numos::WeatherSnapshot* weather) {
      const numos::SimulationState simulation = ctx->getCommonObjects()->metaNumos().simulationState();
      return numos::encodeSeed(NumosSettings::resolved(), weather, &simulation);
    }

    // Returns a weather whose fields are drawn uniformly from their curated rows' slider ranges.
    // Switches are on or off with equal chance.
    // Colors, placement and every other setting keep their current values.
    numos::WeatherSnapshot randomWeather(const numos::Settings& current) {
      static std::mt19937 rng { std::random_device {}() };
      numos::WeatherSnapshot w = numos::weatherFromSettings(current);
      size_t count = 0;
      const numos::WeatherFieldInfo* fields = numos::weatherFields(count);
      for (size_t i = 0; i < count; ++i) {
        const PresetRow* row = curatedRow(std::string(kKeyPrefix) + fields[i].name);
        if (row == nullptr || row->fixed) {
          continue;
        }
        float& v = w.*(fields[i].member);
        if (fields[i].kind == numos::WeatherFieldKind::Flag) {
          v = std::bernoulli_distribution(0.5)(rng) ? 1.0f : 0.0f;
        } else if (row->mn < row->mx) {
          const float scale = row->kind == RowKind::SpeedMetersPerSecond ? 0.001f : 1.0f;
          v = std::uniform_real_distribution<float>(row->mn, row->mx)(rng) * scale;
        }
      }
      return w;
    }

    // Draws the Seed tab, which makes random weathers and seeds, and saves them as presets.
    void seedTools(DxvkContext* ctx, NumosWeather& weather, PresetWindow& window) {
      RtxNumos& numosObject = ctx->getCommonObjects()->metaNumos();
      const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
      ImGui::BeginDisabled(weather.tuning());
      if (ImGui::Button("Randomize")) {
        const numos::WeatherSnapshot w = randomWeather(weather.current());
        weather.setPreview(std::string());
        weather.holdSnapshot(w);
        std::snprintf(window.seedText, sizeof(window.seedText), "%s", currentSeed(ctx, &w).c_str());
        window.seedStatus = "A random weather is showing, and its seed is in the box. Save seed as preset keeps it.";
      }
      RemixGui::SetTooltipToLastWidgetOnHover(
        "Shows a new random weather: each weather setting of the editor's sections (clouds, second layer, wind, air and "
        "haze density, fog, lightning) is drawn from its slider range. Colors, heights, placement and scale stay as "
        "they are, and your own settings are not changed. It shows until a preset or the game's weather takes over.");
      sameLineIfFits(buttonWidth("Load from seed"), right);
      if (ImGui::Button("Load from seed")) {
        numos::SeedContents contents;
        std::string error;
        if (numos::decodeSeed(window.seedText, contents, &error)) {
          // A seed carries no quality level (NUMOS_SETTING_NO_SEED), so the graphics preset's stays in force.
          contents.settings.qualityLevel = NumosSettings::resolved().qualityLevel;
          {
            RtxOptionLayerTarget target(RtxOptionEditTarget::User);
            NumosSettings::write(contents.settings);
          }
          weather.setPreview(std::string());
          if (contents.hasWeather) {
            weather.holdSnapshot(contents.weather);
          }
          if (contents.hasSimulation) {
            numosObject.setSimulationState(contents.simulation);
          }
          window.seedStatus = contents.unknownEntries ? "Applied. " + std::to_string(contents.unknownEntries) +
                                                        " entries are unknown to this build and were skipped."
                                                      : "Applied.";
        } else {
          window.seedStatus = "Not a valid seed: " + error;
        }
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Applies the seed in the box: its settings become your settings (user layer), and its "
                                              "weather shows until a preset or the game's weather takes over.");
      sameLineIfFits(buttonWidth("Copy seed"), right);
      if (ImGui::Button("Copy seed")) {
        const std::string seed = currentSeed(ctx, weather.currentSnapshot());
        ImGui::SetClipboardText(seed.c_str());
        std::snprintf(window.seedText, sizeof(window.seedText), "%s", seed.c_str());
        window.seedStatus = "Copied the current sky to the clipboard.";
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Copies a text seed of the current sky: every sky setting, the weather and the clouds' "
                                              "accumulated motion. Paste it here (or in the NUMOS viewer) to get the same sky back.");
      sameLineIfFits(buttonWidth("Paste"), right);
      if (ImGui::Button("Paste")) {
        if (const char* clip = ImGui::GetClipboardText()) {
          std::snprintf(window.seedText, sizeof(window.seedText), "%s", clip);
        }
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Puts the clipboard's text in the seed box.");
      sameLineIfFits(buttonWidth("Save seed as preset..."), right);
      ImGui::BeginDisabled(window.seedText[0] == '\0');
      if (ImGui::Button("Save seed as preset...")) {
        openNamePopup("SeedPresetPopup", window, weather.uniqueName("Seed"));
      }
      ImGui::EndDisabled();
      RemixGui::SetTooltipToLastWidgetOnHover("Writes the look of the seed in the box to a new preset file: every weather setting and "
                                              "every setting of the editor's sections, as the seed has them.");
      std::string name;
      if (namePopup("SeedPresetPopup", "Name of the new preset file:", window, name)) {
        numos::SeedContents contents;
        std::string error;
        if (numos::decodeSeed(window.seedText, contents, &error)) {
          numos::Settings look = contents.settings;
          if (contents.hasWeather) {
            numos::applyWeather(contents.weather, look);
          }
          saveNew(weather, weather.createFromLook(weather.uniqueName(name), look, curatedKeys()), window, "Saved the seed as");
          window.seedStatus.clear();
        } else {
          window.seedStatus = "Not a valid seed: " + error;
        }
      }
      ImGui::EndDisabled();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputTextWithHint("##NumosSeed", "Seed: paste one here, or press Copy seed", window.seedText, sizeof(window.seedText));
      if (!window.seedStatus.empty()) {
        hintText(window.seedStatus);
      }
    }

    // Returns the last part of a path, for a short display of the preset folder.
    std::string folderName(const std::string& path) {
      const size_t end = path.find_last_not_of("/\\");
      if (end == std::string::npos) {
        return path;
      }
      const size_t start = path.find_last_of("/\\", end);
      return path.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1) + 1);
    }

    // Draws the header line with the preset folder, Save, Reload and the tools toggle.
    void presetHeader(NumosWeather& weather, PresetWindow& window) {
      const ImGuiStyle& style = ImGui::GetStyle();
      const int dirty = int(std::count_if(weather.presets().begin(), weather.presets().end(),
                                          [](const NumosWeather::Preset& p) { return p.dirty; }));
      const std::string directory = weather.directory();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(hintTextColor(), "Folder:");
      ImGui::SameLine();
      ImGui::TextUnformatted(folderName(directory).c_str());
      RemixGui::SetTooltipToLastWidgetOnHover(directory.c_str());
      ImGui::SameLine();
      if (dirty > 0) {
        ImGui::TextColored(attentionColor(), "%d unsaved", dirty);
      } else {
        ImGui::TextColored(hintTextColor(), "all saved");
      }

      const char* toolsLabel = "Files & seeds";
      const float buttons = buttonWidth("Save") + buttonWidth("Reload") + buttonWidth(toolsLabel) + style.ItemSpacing.x * 2.0f;
      const float rightX = ImGui::GetWindowContentRegionMax().x - buttons;
      const float minX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + style.ItemSpacing.x;
      ImGui::SameLine(std::max(rightX, minX));
      ImGui::BeginDisabled(weather.tuning());
      ImGui::BeginDisabled(dirty == 0);
      if (ImGui::Button("Save")) {
        size_t saved = 0;
        std::string errors;
        for (NumosWeather::Preset& p : weather.presets()) {
          std::string error;
          if (!p.dirty) {
            continue;
          }
          if (weather.save(p, &error)) {
            ++saved;
          } else {
            errors += " " + error;
          }
        }
        window.status = "Saved " + std::to_string(saved) + " presets." + (errors.empty() ? "" : " Failed:" + errors);
      }
      ImGui::EndDisabled();
      RemixGui::SetTooltipToLastWidgetOnHover("Writes every edited preset to its file.");
      ImGui::SameLine();
      if (ImGui::Button("Reload")) {
        weather.reload();
        window.status = "Reloaded the preset files (unsaved edits discarded).";
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Reads every preset file in the folder again. Unsaved edits are discarded.");
      ImGui::EndDisabled();
      ImGui::SameLine();
      const bool toolsOpen = window.showTools;
      if (toolsOpen) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      }
      if (ImGui::Button(toolsLabel)) {
        window.showTools = !window.showTools;
      }
      if (toolsOpen) {
        ImGui::PopStyleColor();
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Shows or hides the seed tools (random weathers, seeds) and the file tools (loading a "
                                              "preset file, the preset folder).");
    }

    // Draws the game's weather, what the sky shows, and how the presets follow the game.
    void presetStatus(NumosWeather& weather, PresetWindow& window) {
      const ImVec4 hint = hintTextColor();
      const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      const std::string gameIds = NumosWeather::gameWeather();
      NumosWeather::Preset* bound = gameIds.empty() ? nullptr : weather.boundPreset(gameIds);

      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(hint, "Game weather:");
      ImGui::SameLine();
      if (gameIds.empty()) {
        ImGui::TextUnformatted("none");
        RemixGui::SetTooltipToLastWidgetOnHover("The game reports no weather, or it has no integration that reports one.");
      } else {
        ImGui::Text("%s  ->  %s", gameIds.c_str(), bound ? bound->name.c_str() : "no preset");
        RemixGui::SetTooltipToLastWidgetOnHover("The ids the game reports, most specific first, and the preset bound to them.");
        const std::string outgoing = NumosWeather::gameWeatherOutgoing();
        if (!outgoing.empty() && outgoing != gameIds && NumosWeather::gameWeatherBlend() >= 0.0f) {
          const NumosWeather::Preset* out = weather.boundPreset(outgoing);
          char text[512];
          std::snprintf(text, sizeof(text), "(from %s  ->  %s, %.0f%% done)", outgoing.c_str(), out ? out->name.c_str() : "no preset",
                        NumosWeather::gameWeatherBlend() * 100.0f);
          sameLineIfFits(ImGui::CalcTextSize(text).x, right);
          ImGui::TextColored(hint, "%s", text);
        }
      }
      const std::string& active = weather.activePreset();
      const std::string showing = (active.empty() ? std::string("settings only") : active) + (weather.preview().empty() ? "" : " (preview)");
      sameLineIfFits(ImGui::CalcTextSize("Showing:").x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(showing.c_str()).x, right);
      ImGui::TextColored(hint, "Showing:");
      ImGui::SameLine();
      ImGui::TextUnformatted(showing.c_str());
      if (weather.transitionProgress() < 1.0f) {
        const float barWidth = ImGui::GetFontSize() * 6.0f;
        sameLineIfFits(barWidth, right);
        ImGui::ProgressBar(weather.transitionProgress(), ImVec2(barWidth, 0.0f), "blending");
      }

      // Follow game weather and Ease are options, so their edits go through the option layer checks.
      bool follow = NumosWeather::followGame();
      if (ImGui::Checkbox("Follow game weather", &follow)) {
        RemixGui::CheckRtxOptionPopups(&NumosWeather::followGameObject());
        NumosWeather::followGameObject().setDeferred(follow);
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Shows the preset bound to the weather the game reports. Off: the preset chosen for "
                                              "\"Without a game weather\" only.");
      const float easeWidth = ImGui::GetFontSize() * 5.0f;
      sameLineIfFits(ImGui::CalcTextSize("Ease").x + ImGui::GetStyle().ItemSpacing.x + easeWidth, right);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Ease");
      RemixGui::SetTooltipToLastWidgetOnHover("Blend time into a new weather, for presets without a transition time of their own. "
                                              "0 is instant.");
      ImGui::SameLine();
      float ease = NumosWeather::transitionSeconds();
      ImGui::SetNextItemWidth(easeWidth);
      if (ImGui::DragFloat("##Ease", &ease, 0.1f, 0.0f, 600.0f, "%.1f s", kClamp)) {
        RemixGui::CheckRtxOptionPopups(&NumosWeather::transitionSecondsObject());
        NumosWeather::transitionSecondsObject().setDeferred(ease);
      }
      RemixGui::SetTooltipToLastWidgetOnHover("Blend time into a new weather, for presets without a transition time of their own. "
                                              "0 is instant.");

      const std::string chosen = NumosWeather::preset();
      const float comboWidth = ImGui::GetFontSize() * 10.0f;
      sameLineIfFits(ImGui::CalcTextSize("Without a game weather").x + ImGui::GetStyle().ItemSpacing.x + comboWidth, right);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Without a game weather");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(comboWidth);
      if (ImGui::BeginCombo("##WithoutGameWeather", chosen.empty() ? "(settings only)" : chosen.c_str())) {
        if (ImGui::Selectable("(settings only)", chosen.empty())) {
          NumosWeather::presetObject().setDeferred(std::string());
        }
        for (const auto& p : weather.presets()) {
          if (ImGui::Selectable(p.name.c_str(), p.name == chosen)) {
            NumosWeather::presetObject().setDeferred(p.name);
          }
        }
        ImGui::EndCombo();
      }
      RemixGui::SetTooltipToLastWidgetOnHover("The preset shown while the game reports no weather bound to a preset (or Follow game "
                                              "weather is off).");

      // Tuning and saving the current look.
      const std::string weatherId = firstId(gameIds);
      if (!weather.tuning()) {
        sameLineIfFits(buttonWidth("Look..."), right);
        if (ImGui::Button("Look...")) {
          ImGui::OpenPopup("LookMenu");
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Tunes the current weather with the Sky sliders, or saves what is on screen to it.");
        if (ImGui::BeginPopup("LookMenu")) {
          NumosWeather::Preset* selected = weather.find(window.selected);
          const bool canTune = !weatherId.empty() || selected != nullptr;
          if (ImGui::MenuItem("Tune this weather with the Sky sliders", nullptr, false, canTune)) {
            weather.setPreview(std::string());
            if (!weatherId.empty()) {
              weather.beginTuning(bound ? bound->name : std::string(), weatherId);
            } else {
              weather.beginTuning(selected->name, std::string());
            }
            window.status = "Tuning: adjust the Sky tabs (Sky, Clouds, Haze), then press Save look.";
          }
          RemixGui::SetTooltipToLastWidgetOnHover(
            "Copies what you see now into the normal Sky tabs and pauses the presets, so the sliders show exactly what "
            "will be saved. Then press Save look. Your own settings are put back afterwards. Without a game weather it "
            "tunes the selected preset.");
          if (ImGui::MenuItem("Save current look to this weather", nullptr, false, canTune)) {
            NumosWeather::Preset* target = !weatherId.empty() ? bound : selected;
            NumosWeather::Preset& stored = weather.storeLook(target, weatherId);
            std::string error;
            window.selected = stored.name;
            window.status = weather.save(stored, &error) ? "Saved the current look to " + stored.name + "."
                                                         : "Save failed: " + error;
          }
          RemixGui::SetTooltipToLastWidgetOnHover(
            "One click: stores exactly what is on screen now (clouds, second layer, air and haze, fog, wind, lightning, and "
            "every other setting the preset already sets) into this weather's preset, a new one if the weather has none, "
            "and saves its file. Without a game weather it stores into the selected preset.");
          ImGui::EndPopup();
        }
      } else {
        const std::string& target = weather.tuningPreset();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(attentionColor(), "Tuning %s  ->  %s",
                           weather.tuningWeather().empty() ? "the selected preset" : weather.tuningWeather().c_str(),
                           target.empty() ? "a new preset" : target.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Save look")) {
          if (NumosWeather::Preset* stored = weather.finishTuning(true)) {
            window.selected = stored->name;
            window.status = "Saved the tuned look to " + stored->name + ", and your settings are restored.";
          }
        }
        RemixGui::SetTooltipToLastWidgetOnHover("Stores the tuned settings as this weather's look, restores your own settings and "
                                                "saves the file.");
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          weather.finishTuning(false);
          window.status = "Tuning canceled, and your settings are restored.";
        }
      }
    }

    void presetWindowBody(DxvkContext* ctx, NumosWeather& weather, PresetWindow& window) {
      presetHeader(weather, window);
      if (window.showTools) {
        if (ImGui::BeginTabBar("PresetTools")) {
          if (ImGui::BeginTabItem("Seed")) {
            seedTools(ctx, weather, window);
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem("Files")) {
            presetFiles(weather, window);
            ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }
      }
      ImGui::Separator();
      presetStatus(weather, window);
      if (!window.status.empty()) {
        hintText(window.status);
      }
      ImGui::Separator();
      presetPanes(weather, window);
    }
  }

  void showWeatherPresetSummary(DxvkContext* ctx) {
    const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
    NumosWeather& weather = ctx->getCommonObjects()->metaNumosWeather();
    PresetWindow& window = presetWindow();
    const std::string& active = weather.activePreset();
    ImGui::Text("Showing: %s", active.empty() ? "the settings alone (no preset)" : active.c_str());
    const std::string gameId = NumosWeather::gameWeather();
    if (gameId.empty()) {
      ImGui::TextDisabled("Game weather: none reported");
    } else {
      const NumosWeather::Preset* bound = weather.boundPreset(gameId);
      ImGui::TextDisabled("Game weather: %s -> %s", gameId.c_str(), bound ? bound->name.c_str() : "(no preset)");
    }
    if (ImGui::Button(window.open ? "Close Weather Presets" : "Open Weather Presets")) {
      window.open = !window.open;
    }
    RemixGui::SetTooltipToLastWidgetOnHover("The weather preset editor: loads, edits and saves preset files with a live preview, "
                                            "tunes a weather with the Sky sliders, and makes random weathers and seeds.");
  }

  void showWeatherPresetWindow(DxvkContext* ctx) {
    PresetWindow& window = presetWindow();
    if (!window.open) {
      return;
    }
    RtxOptionLayerTarget layerTarget(RtxOptionEditTarget::User);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 360.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Weather Presets", &window.open)) {
      ImGui::End();
      return;
    }
    if (!RtxNumos::enabled()) {
      ImGui::TextWrapped("Weather presets drive the NUMOS sky. Set Sky Mode to NUMOS Atmosphere under Sky Tuning.");
    } else {
      const auto stateLock = ctx->getCommonObjects()->metaNumos().lockState();
      beginPage(ctx);
      presetWindowBody(ctx, ctx->getCommonObjects()->metaNumosWeather(), window);
    }
    ImGui::End();
  }

}
