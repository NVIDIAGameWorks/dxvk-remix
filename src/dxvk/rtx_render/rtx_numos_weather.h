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
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rtx_option.h"

namespace numos {
  struct Settings;
  struct WeatherSnapshot;
}

namespace dxvk {

  class DxvkDevice;

  // Loads the weather preset files and blends them over the settings, see "Weather presets" in documentation/NumosSky.md.
  class NumosWeather {
  public:
    RTX_OPTION("rtx.weather", std::string, presetDirectory, std::string("rtx-remix/weather"),
      "The folder of the weather preset files, which are named <name>.conf. It is relative to the game's folder unless absolute.");
    RTX_OPTION("rtx.weather", std::string, preset, std::string(""),
      "The weather preset the sky shows while the game reports no weather that is bound to a preset, or while rtx.weather.followGame is off. "
      "The sky uses the NUMOS settings alone when this is empty.");
    RTX_OPTION("rtx.weather", bool, followGame, true,
      "Enables showing the preset that is bound to the weather the game reports in rtx.weather.gameWeather. "
      "The sky shows only rtx.weather.preset when this is off.");
    RTX_OPTION("rtx.weather", float, transitionSeconds, 10.0f,
      "The blend time into a new weather, in seconds, for presets without a weather.transitionSeconds of their own. 0 is instant.");
    RTX_OPTION_FLAG("rtx.weather", std::string, gameWeather, std::string(""), RtxOptionFlags::NoSave,
      "The game's current weather, set by a game integration. It holds one id, or several separated by commas with the most specific first. "
      "The first id that a preset's weather.bindings lists selects that preset.");
    RTX_OPTION_FLAG("rtx.weather", std::string, gameWeatherOutgoing, std::string(""), RtxOptionFlags::NoSave,
      "The weather the game is blending away from, set by a game integration along with rtx.weather.gameWeatherBlend.");
    RTX_OPTION_FLAG("rtx.weather", float, gameWeatherBlend, -1.0f, RtxOptionFlags::NoSave,
      "The game's own blend from rtx.weather.gameWeatherOutgoing to rtx.weather.gameWeather, from 0 to 1, set by a game integration. "
      "When negative, the runtime blends over the preset's transition time.");

    struct Preset {
      // The file name without .conf.
      std::string name;
      // The game weather ids this preset stands for.
      std::vector<std::string> bindings;
      // A negative time uses rtx.weather.transitionSeconds.
      float transitionSeconds = -1.0f;
      // A disabled preset's bindings are ignored.
      bool enabled = true;
      std::unique_ptr<numos::Settings> values;

      // These follow the order of numos::settingInfos().
      // Only the values flagged in `set` mean anything, and `relative` ones multiply the user's setting.
      std::vector<bool> set;
      std::vector<bool> relative;

      bool dirty = false;
      // A preset named like one of NUMOS's can be reset to it.
      bool numosBuiltIn = false;

      Preset();
      Preset(const Preset&);
      Preset& operator=(const Preset&);
      ~Preset();
    };

    explicit NumosWeather(DxvkDevice* pDevice);
    ~NumosWeather();

    // Returns the settings to render this frame, with the presets and any held seed weather blended over `base`.
    const numos::Settings& update(const numos::Settings& base, float deltaSeconds);

    // Returns the weather fields of what update() last returned, which seeds store.
    const numos::WeatherSnapshot* currentSnapshot() const;
    // Shows a decoded seed's weather until the preset or the game weather changes.
    void holdSnapshot(const numos::WeatherSnapshot& weather);

    // Editing a preset changes the sky at once while it is the one showing.
    std::vector<Preset>& presets() { return m_presets; }
    std::string directory() const;
    void reload();
    bool save(Preset& preset, std::string* pError = nullptr);
    // Adds an unsaved preset with the keys of `pFrom`.
    // Without one, the preset gets the weather fields of the current sky.
    Preset& create(const std::string& name, const Preset* pFrom);
    // Adds an unsaved preset that sets every weather field and every key in `keys` to its value in `look`.
    Preset& createFromLook(const std::string& name, const numos::Settings& look, const std::vector<bool>& keys);
    // Copies a preset file from any path into the preset folder under an unused name.
    // Returns null, with `error` set, when the file cannot be read or written.
    Preset* loadFile(const std::string& path, std::string* pError = nullptr);
    bool rename(Preset& preset, const std::string& newName, std::string* pError = nullptr);
    bool remove(const std::string& name, std::string* pError = nullptr);
    // Resets a NUMOS preset to its definition in NUMOS, without saving it.
    void resetToNumos(Preset& preset);
    // Restores the preset as it was last loaded or saved.
    // Returns false when it has never been saved.
    bool revert(Preset& preset);
    bool hasSavedCopy(const Preset& preset) const;
    // Sets the key in the preset to the sky's current value.
    void addKey(Preset& preset, size_t settingIndex);
    void removeKey(Preset& preset, size_t settingIndex);
    Preset* find(const std::string& name);
    // Returns the enabled preset that is bound to the first matching id of a comma separated list.
    // Returns null when none is.
    Preset* boundPreset(const std::string& gameIds);
    // Returns a name that no preset has, by numbering `base` when needed.
    std::string uniqueName(const std::string& base);

    // Shows the named preset whatever the game reports.
    // An empty name ends the preview.
    void setPreview(const std::string& name) { m_preview = name; }
    const std::string& preview() const { return m_preview; }

    // Stores what the sky shows as absolute values, for every weather field and every key the preset sets.
    // A null preset creates one that is bound to `weatherId`. Nothing is saved.
    Preset& storeLook(Preset* pPreset, const std::string& weatherId);

    // Tuning copies the current look into the user's settings and pauses the presets, so the sliders show what is stored.
    // finishTuning restores the user's settings, and stores the tuned ones into the preset when `keep` is set.
    void beginTuning(const std::string& presetName, const std::string& weatherId);
    Preset* finishTuning(bool keep);
    bool tuning() const { return m_tuning; }
    const std::string& tuningPreset() const { return m_tuningPreset; }
    const std::string& tuningWeather() const { return m_tuningWeather; }

    // These report the status to the developer menu.
    const std::string& activePreset() const { return m_targetName; }
    float transitionProgress() const;
    const numos::Settings& lastBase() const;
    const numos::Settings& current() const;

  private:
    void ensureLoaded();
    void writeMissingNumosPresets();
    void applyPreset(const Preset* pPreset, numos::Settings& settings) const;
    void rememberSaved(const Preset& preset);
    static void blend(const numos::Settings& a, const numos::Settings& b, float t, numos::Settings& out);

    struct State;
    std::unique_ptr<State> m_state;
    std::vector<Preset> m_presets;
    // Holds each preset as it was last loaded or saved.
    std::vector<Preset> m_saved;
    bool m_loaded = false;
    // Names the preset the sky shows or blends to.
    std::string m_targetName;
    std::string m_preview;
    bool m_tuning = false;
    std::string m_tuningPreset;
    std::string m_tuningWeather;
  };

}
