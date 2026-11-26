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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "numos/settings.h"
#include "numos/weather.h"

#include "rtx_numos_weather.h"
#include "rtx_numos_settings.h"
#include "rtx_option_layer.h"
#include "../../util/log/log.h"
#include "../../util/util_env.h"

namespace dxvk {

  namespace {
    namespace fs = std::filesystem;

    constexpr const char* kSeedTarget = "(seed)";

    size_t fieldSize(numos::SettingType type) {
      switch (type) {
      case numos::SettingType::Bool:  return sizeof(bool);
      case numos::SettingType::Int:   return sizeof(int32_t);
      case numos::SettingType::Uint:  return sizeof(uint32_t);
      case numos::SettingType::Float: return sizeof(float);
      case numos::SettingType::Vec3:  return sizeof(numos::Vector3);
      }
      return 0;
    }

    char* fieldPtr(numos::Settings& s, const numos::SettingInfo& info) {
      return reinterpret_cast<char*>(&s) + info.offset;
    }

    const char* fieldPtr(const numos::Settings& s, const numos::SettingInfo& info) {
      return reinterpret_cast<const char*>(&s) + info.offset;
    }

    float& floatField(numos::Settings& s, const numos::SettingInfo& info) {
      return *reinterpret_cast<float*>(fieldPtr(s, info));
    }

    numos::Vector3& vec3Field(numos::Settings& s, const numos::SettingInfo& info) {
      return *reinterpret_cast<numos::Vector3*>(fieldPtr(s, info));
    }

    bool sameField(const numos::Settings& a, const numos::Settings& b, const numos::SettingInfo& info) {
      return std::memcmp(fieldPtr(a, info), fieldPtr(b, info), fieldSize(info.type)) == 0;
    }

    void copyField(numos::Settings& dst, const numos::Settings& src, const numos::SettingInfo& info) {
      std::memcpy(fieldPtr(dst, info), fieldPtr(src, info), fieldSize(info.type));
    }

    // Multiplies the value in dst by the one in `factors`. Only floats and colors carry multipliers.
    void multiplyField(numos::Settings& dst, const numos::Settings& factors, const numos::SettingInfo& info) {
      if (info.type == numos::SettingType::Float) {
        floatField(dst, info) *= *reinterpret_cast<const float*>(fieldPtr(factors, info));
      } else if (info.type == numos::SettingType::Vec3) {
        numos::Vector3& v = vec3Field(dst, info);
        const numos::Vector3& m = *reinterpret_cast<const numos::Vector3*>(fieldPtr(factors, info));
        v = numos::Vector3(v.x * m.x, v.y * m.y, v.z * m.z);
      } else {
        copyField(dst, factors, info);
      }
    }

    size_t indexOf(const numos::SettingInfo& info) {
      return size_t(&info - numos::settingInfos().data());
    }

    std::string trim(const std::string& s) {
      size_t b = 0, e = s.size();
      while (b < e && std::isspace(uint8_t(s[b]))) {
        ++b;
      }
      while (e > b && std::isspace(uint8_t(s[e - 1]))) {
        --e;
      }
      return s.substr(b, e - b);
    }

    bool equalsNoCase(const std::string& a, const std::string& b) {
      return a.size() == b.size() &&
             std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
    }

    bool parseBool(const std::string& text) {
      return text == "1" || equalsNoCase(text, "true");
    }

    // Parses exactly `count` floats that are separated by commas.
    bool parseFloats(const std::string& text, float* pOut, int count) {
      const char* p = text.c_str();
      for (int i = 0; i < count; ++i) {
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p || !std::isfinite(v)) {
          return false;
        }
        pOut[i] = v;
        p = end;
        while (*p == ' ' || *p == '\t') {
          ++p;
        }
        if (i + 1 < count) {
          if (*p != ',') {
            return false;
          }
          ++p;
        }
      }
      while (std::isspace(uint8_t(*p))) {
        ++p;
      }
      return *p == '\0';
    }

    // Parses three floats, or one float for all three channels.
    bool parseVec3(const std::string& text, numos::Vector3& out) {
      float v[3];
      if (parseFloats(text, v, 3)) {
        out = numos::Vector3(v[0], v[1], v[2]);
        return true;
      }
      if (parseFloats(text, v, 1)) {
        out = numos::Vector3(v[0], v[0], v[0]);
        return true;
      }
      return false;
    }

    std::string fileNameFor(const std::string& name) {
      std::string out = trim(name);
      for (char& c : out) {
        if (std::strchr("\\/:*?\"<>|", c) != nullptr) {
          c = '_';
        }
      }
      return out.empty() ? std::string("Preset") : out;
    }

    // Returns the defaults with every weather field moved off its default value.
    numos::Settings perturbedWeather() {
      const numos::Settings defaults {};
      numos::WeatherSnapshot w = numos::weatherFromSettings(defaults);
      size_t count = 0;
      const numos::WeatherFieldInfo* pFields = numos::weatherFields(count);
      for (size_t i = 0; i < count; ++i) {
        float& v = w.*(pFields[i].member);
        v = pFields[i].kind == numos::WeatherFieldKind::Flag ? (v != 0.0f ? 0.0f : 1.0f) : v * 1.37f + 0.731f;
      }
      numos::Settings s = defaults;
      numos::applyWeather(w, s);
      return s;
    }

    // Flags the settings that are weather fields, which perturbedWeather moves.
    const std::vector<bool>& weatherKeys() {
      static const std::vector<bool> keys = [] {
        const numos::Settings defaults {};
        const numos::Settings moved = perturbedWeather();
        const auto& infos = numos::settingInfos();
        std::vector<bool> k(infos.size());
        for (size_t i = 0; i < infos.size(); ++i) {
          k[i] = !sameField(defaults, moved, infos[i]);
        }
        return k;
      }();
      return keys;
    }

    // Builds a preset's keys from one of NUMOS's, over the defaults.
    // It sets the fields that NUMOS's preset fixes and those it leaves different from the defaults.
    void numosPresetKeys(numos::WeatherPreset which, numos::Settings& values, std::vector<bool>& set) {
      const numos::Settings defaults {};
      const numos::Settings moved = perturbedWeather();
      numos::Settings overDefaults = defaults;
      numos::applyWeather(numos::weatherPreset(which, defaults), overDefaults);
      numos::Settings overMoved = defaults;
      numos::applyWeather(numos::weatherPreset(which, moved), overMoved);
      const auto& infos = numos::settingInfos();
      set.assign(infos.size(), false);
      for (size_t i = 0; i < infos.size(); ++i) {
        const bool changed = !sameField(overDefaults, defaults, infos[i]);
        const bool fixed = weatherKeys()[i] && sameField(overDefaults, overMoved, infos[i]);
        set[i] = changed || fixed;
      }
      values = overDefaults;
    }

    float smoothstep01(float t) {
      t = std::clamp(t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    // Stores a value that is written as x<factors>.
    // Returns false for a bad value and for a setting that cannot be multiplied.
    bool setMultiplier(NumosWeather::Preset& p, const numos::SettingInfo& info, const std::string& text) {
      const size_t i = indexOf(info);
      if (info.type == numos::SettingType::Float) {
        float v;
        if (!parseFloats(trim(text), &v, 1)) {
          return false;
        }
        floatField(*p.values, info) = v;
      } else if (info.type == numos::SettingType::Vec3) {
        numos::Vector3 v;
        if (!parseVec3(trim(text), v)) {
          return false;
        }
        vec3Field(*p.values, info) = v;
      } else {
        return false;
      }
      p.set[i] = true;
      p.relative[i] = true;
      return true;
    }

    // Reads one preset file. The preset is named after the file.
    // Returns false when the file cannot be read.
    bool readPresetFile(const fs::path& path, NumosWeather::Preset& p) {
      std::ifstream f(path, std::ios::binary);
      if (!f) {
        return false;
      }
      std::stringstream text;
      text << f.rdbuf();
      p.name = path.stem().string();
      numos::WeatherPreset which;
      p.numosBuiltIn = numos::weatherPresetFromName(p.name.c_str(), which);

      // The preset's own keys and the multipliers are read here, and every other line goes to NUMOS's parser.
      std::string numosText;
      std::istringstream lines(text.str());
      std::string line;
      while (std::getline(lines, line)) {
        std::string code = line;
        const size_t hash = code.find('#');
        if (hash != std::string::npos) {
          code.resize(hash);
        }
        const size_t eq = code.find('=');
        if (eq != std::string::npos) {
          const std::string key = trim(code.substr(0, eq));
          const std::string value = trim(code.substr(eq + 1));
          if (key == "weather.bindings") {
            std::istringstream ids(value);
            std::string id;
            while (std::getline(ids, id, ',')) {
              id = trim(id);
              if (!id.empty()) {
                p.bindings.push_back(id);
              }
            }
            continue;
          }
          if (key == "weather.transitionSeconds") {
            try {
              p.transitionSeconds = std::stof(value);
            } catch (...) {
              Logger::warn("[NUMOS weather] " + path.string() + ": weather.transitionSeconds is not a number");
            }
            continue;
          }
          if (key == "weather.enabled") {
            p.enabled = parseBool(value);
            continue;
          }
          if (!value.empty() && (value[0] == 'x' || value[0] == 'X')) {
            const numos::SettingInfo* pInfo = numos::findSetting(key);
            if (pInfo == nullptr || !setMultiplier(p, *pInfo, value.substr(1))) {
              Logger::warn("[NUMOS weather] " + path.string() + ": " + key + " = " + value +
                           " is not a multiplier on a number or a color, so it is ignored");
            }
            continue;
          }
        }
        numosText += line;
        numosText += '\n';
      }
      numos::ExplicitSettings explicitlySet;
      numos::applyConfig(*p.values, numosText, &explicitlySet);
      for (size_t i = 0; i < p.set.size() && i < explicitlySet.size(); ++i) {
        if (explicitlySet[i]) {
          p.set[i] = true;
          p.relative[i] = false;
        }
      }
      return true;
    }

  }

  NumosWeather::Preset::Preset()
    : values(std::make_unique<numos::Settings>()),
      set(numos::settingInfos().size(), false),
      relative(numos::settingInfos().size(), false) { }
  NumosWeather::Preset::Preset(const Preset& o)
    : name(o.name), bindings(o.bindings), transitionSeconds(o.transitionSeconds), enabled(o.enabled),
      values(std::make_unique<numos::Settings>(*o.values)), set(o.set), relative(o.relative), dirty(o.dirty),
      numosBuiltIn(o.numosBuiltIn) { }
  NumosWeather::Preset& NumosWeather::Preset::operator=(const Preset& o) {
    if (this != &o) {
      name = o.name;
      bindings = o.bindings;
      transitionSeconds = o.transitionSeconds;
      enabled = o.enabled;
      values = std::make_unique<numos::Settings>(*o.values);
      set = o.set;
      relative = o.relative;
      dirty = o.dirty;
      numosBuiltIn = o.numosBuiltIn;
    }
    return *this;
  }
  NumosWeather::Preset::~Preset() = default;

  struct NumosWeather::State {
    numos::Settings base;
    numos::Settings from;
    numos::Settings current;
    numos::WeatherSnapshot snapshot;
    float elapsed = 0.0f;
    float duration = 0.0f;
    bool hasCurrent = false;
    bool held = false;
    // The next change of preset shows at once.
    bool snap = false;
    Preset heldPreset;
    std::string lastGameWeather;
    std::string lastManualPreset;
    // Tuning restores the baseline, which holds the user's settings.
    numos::Settings tuneBaseline;
    numos::Settings tuneStart;
  };

  NumosWeather::NumosWeather(DxvkDevice*) : m_state(std::make_unique<State>()) { }
  NumosWeather::~NumosWeather() = default;

  std::string NumosWeather::directory() const {
    fs::path p(presetDirectory());
    if (p.is_relative()) {
      p = fs::path(env::getExePath()).parent_path() / p;
    }
    return p.lexically_normal().string();
  }

  void NumosWeather::ensureLoaded() {
    if (m_loaded) {
      return;
    }
    m_loaded = true;
    writeMissingNumosPresets();
    reload();
  }

  void NumosWeather::writeMissingNumosPresets() {
    std::error_code ec;
    fs::create_directories(directory(), ec);
    // The marker lists the NUMOS presets that were written, so one that the user deleted or renamed stays gone.
    const fs::path marker = fs::path(directory()) / ".numos-presets-written";
    std::vector<std::string> written;
    {
      std::ifstream in(marker);
      std::string line;
      while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
          written.push_back(line);
        }
      }
    }
    bool markerChanged = false;
    for (uint32_t i = 0; i < uint32_t(numos::WeatherPreset::Count); ++i) {
      const numos::WeatherPreset which = numos::WeatherPreset(i);
      Preset p;
      p.name = numos::weatherPresetName(which);
      if (std::find(written.begin(), written.end(), p.name) != written.end()) {
        continue;
      }
      written.push_back(p.name);
      markerChanged = true;
      if (fs::exists(fs::path(directory()) / (p.name + ".conf"), ec)) {
        // A file that the mod or the user provides is never replaced.
        continue;
      }
      numosPresetKeys(which, *p.values, p.set);
      p.numosBuiltIn = true;
      std::string error;
      if (!save(p, &error)) {
        Logger::warn("[NUMOS weather] " + error);
      }
    }
    if (markerChanged) {
      std::ofstream out(marker, std::ios::trunc);
      for (const std::string& name : written) {
        out << name << '\n';
      }
    }
  }

  void NumosWeather::reload() {
    m_presets.clear();
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory(), ec)) {
      if (entry.is_regular_file(ec) && entry.path().extension() == ".conf") {
        files.push_back(entry.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files) {
      Preset p;
      if (readPresetFile(path, p)) {
        m_presets.push_back(std::move(p));
      }
    }
    m_saved = m_presets;
    Logger::info(str::format("[NUMOS weather] ", m_presets.size(), " presets in ", directory()));
  }

  bool NumosWeather::save(Preset& p, std::string* pError) {
    std::ostringstream text;
    text << "# NUMOS weather preset \"" << p.name << "\". Edit it here or in the developer menu (Sky Tuning > Weather > Weather Presets).\n"
         << "# While it shows, exactly the settings listed below override the user's. Any rtx.atmosphere.* key may appear.\n"
         << "# A value written x<value> multiplies the user's own setting (per channel for colors).\n"
         << "# weather.bindings: the game weather ids it stands for (rtx.weather.gameWeather), comma separated.\n"
         << "weather.bindings = ";
    for (size_t i = 0; i < p.bindings.size(); ++i) {
      text << (i ? ", " : "") << p.bindings[i];
    }
    text << "\n";
    if (p.transitionSeconds >= 0.0f) {
      text << "weather.transitionSeconds = " << p.transitionSeconds << "\n";
    }
    if (!p.enabled) {
      text << "weather.enabled = false\n";
    }
    text << "\n";
    const auto& infos = numos::settingInfos();
    for (size_t i = 0; i < infos.size(); ++i) {
      if (p.set[i]) {
        text << infos[i].key << " = " << (p.relative[i] ? "x" : "") << numos::formatSetting(*p.values, infos[i]) << "\n";
      }
    }
    std::error_code ec;
    fs::create_directories(directory(), ec);
    const fs::path path = fs::path(directory()) / (p.name + ".conf");
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
      if (pError) {
        *pError = "cannot write " + path.string();
      }
      return false;
    }
    f << text.str();
    p.dirty = false;
    rememberSaved(p);
    return true;
  }

  void NumosWeather::rememberSaved(const Preset& p) {
    for (Preset& s : m_saved) {
      if (s.name == p.name) {
        s = p;
        return;
      }
    }
    m_saved.push_back(p);
  }

  NumosWeather::Preset& NumosWeather::create(const std::string& name, const Preset* pFrom) {
    Preset p;
    if (pFrom != nullptr) {
      p = *pFrom;
    } else {
      *p.values = m_state->current;
      p.set = weatherKeys();
    }
    p.name = name;
    p.bindings.clear();
    numos::WeatherPreset which;
    p.numosBuiltIn = numos::weatherPresetFromName(name.c_str(), which);
    p.dirty = true;
    m_presets.push_back(std::move(p));
    return m_presets.back();
  }

  NumosWeather::Preset& NumosWeather::createFromLook(const std::string& name, const numos::Settings& look,
                                                     const std::vector<bool>& keys) {
    const Preset empty;
    Preset& p = create(name, &empty);
    const auto& infos = numos::settingInfos();
    *p.values = look;
    for (size_t i = 0; i < infos.size(); ++i) {
      p.set[i] = weatherKeys()[i] || (i < keys.size() && keys[i]);
    }
    return p;
  }

  NumosWeather::Preset* NumosWeather::loadFile(const std::string& path, std::string* pError) {
    ensureLoaded();
    fs::path file(path);
    if (file.is_relative()) {
      file = fs::path(env::getExePath()).parent_path() / file;
    }
    Preset p;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !readPresetFile(file, p)) {
      if (pError) {
        *pError = "cannot read " + file.string();
      }
      return nullptr;
    }

    // The file is copied into the preset folder under a name no preset has, so loading never replaces one.
    p.name = uniqueName(p.name);
    numos::WeatherPreset which;
    p.numosBuiltIn = numos::weatherPresetFromName(p.name.c_str(), which);
    m_presets.push_back(std::move(p));
    Preset& loaded = m_presets.back();
    if (!save(loaded, pError)) {
      m_presets.pop_back();
      return nullptr;
    }
    return &loaded;
  }

  bool NumosWeather::rename(Preset& p, const std::string& newName, std::string* pError) {
    if (newName.empty() || newName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
      if (pError) {
        *pError = "not a valid file name";
      }
      return false;
    }
    if (find(newName) != nullptr) {
      if (pError) {
        *pError = "a preset with that name exists";
      }
      return false;
    }
    std::error_code ec;
    const fs::path oldPath = fs::path(directory()) / (p.name + ".conf");
    if (fs::exists(oldPath, ec)) {
      fs::rename(oldPath, fs::path(directory()) / (newName + ".conf"), ec);
      if (ec) {
        if (pError) {
          *pError = ec.message();
        }
        return false;
      }
    }
    if (preset() == p.name) {
      presetObject().setDeferred(newName);
    }
    if (m_preview == p.name) {
      m_preview = newName;
    }
    for (Preset& s : m_saved) {
      if (s.name == p.name) {
        s.name = newName;
      }
    }
    p.name = newName;
    numos::WeatherPreset which;
    p.numosBuiltIn = numos::weatherPresetFromName(newName.c_str(), which);
    return true;
  }

  bool NumosWeather::remove(const std::string& name, std::string* pError) {
    std::error_code ec;
    fs::remove(fs::path(directory()) / (name + ".conf"), ec);
    if (ec) {
      if (pError) {
        *pError = ec.message();
      }
      return false;
    }
    const auto named = [&](const Preset& p) { return p.name == name; };
    m_presets.erase(std::remove_if(m_presets.begin(), m_presets.end(), named), m_presets.end());
    m_saved.erase(std::remove_if(m_saved.begin(), m_saved.end(), named), m_saved.end());
    if (m_preview == name) {
      m_preview.clear();
    }
    return true;
  }

  void NumosWeather::resetToNumos(Preset& p) {
    numos::WeatherPreset which;
    if (!numos::weatherPresetFromName(p.name.c_str(), which)) {
      return;
    }
    numosPresetKeys(which, *p.values, p.set);
    p.relative.assign(p.set.size(), false);
    p.dirty = true;
  }

  bool NumosWeather::revert(Preset& p) {
    for (const Preset& s : m_saved) {
      if (s.name == p.name) {
        p = s;
        p.dirty = false;
        return true;
      }
    }
    return false;
  }

  bool NumosWeather::hasSavedCopy(const Preset& p) const {
    return std::any_of(m_saved.begin(), m_saved.end(), [&](const Preset& s) { return s.name == p.name; });
  }

  void NumosWeather::addKey(Preset& p, size_t index) {
    const auto& infos = numos::settingInfos();
    if (index < infos.size() && !p.set[index]) {
      copyField(*p.values, m_state->current, infos[index]);
      p.set[index] = true;
      p.relative[index] = false;
      p.dirty = true;
    }
  }

  void NumosWeather::removeKey(Preset& p, size_t index) {
    if (index < p.set.size() && p.set[index]) {
      p.set[index] = false;
      p.relative[index] = false;
      p.dirty = true;
    }
  }

  NumosWeather::Preset* NumosWeather::find(const std::string& name) {
    for (Preset& p : m_presets) {
      if (p.name == name) {
        return &p;
      }
    }
    return nullptr;
  }

  NumosWeather::Preset* NumosWeather::boundPreset(const std::string& gameIds) {
    std::istringstream ids(gameIds);
    std::string id;
    while (std::getline(ids, id, ',')) {
      id = trim(id);
      if (id.empty()) {
        continue;
      }
      for (Preset& p : m_presets) {
        if (!p.enabled) {
          continue;
        }
        for (const std::string& b : p.bindings) {
          if (equalsNoCase(b, id)) {
            return &p;
          }
        }
      }
    }
    return nullptr;
  }

  std::string NumosWeather::uniqueName(const std::string& base) {
    const std::string name = fileNameFor(base);
    if (find(name) == nullptr) {
      return name;
    }
    for (int n = 2;; ++n) {
      const std::string candidate = name + " " + std::to_string(n);
      if (find(candidate) == nullptr) {
        return candidate;
      }
    }
  }

  NumosWeather::Preset& NumosWeather::storeLook(Preset* pPreset, const std::string& weatherId) {
    if (pPreset == nullptr) {
      const Preset empty;
      pPreset = &create(uniqueName(weatherId.empty() ? std::string("Custom") : "Custom " + weatherId), &empty);
      if (!weatherId.empty()) {
        pPreset->bindings.push_back(weatherId);
      }
    }
    const auto& infos = numos::settingInfos();
    for (size_t i = 0; i < infos.size(); ++i) {
      if (pPreset->set[i] || weatherKeys()[i]) {
        copyField(*pPreset->values, m_state->current, infos[i]);
        pPreset->set[i] = true;
        pPreset->relative[i] = false;
      }
    }
    pPreset->dirty = true;
    return *pPreset;
  }

  void NumosWeather::beginTuning(const std::string& presetName, const std::string& weatherId) {
    if (m_tuning) {
      return;
    }
    State& s = *m_state;
    s.tuneBaseline = NumosSettings::resolved();
    s.tuneStart = s.current;
    {
      RtxOptionLayerTarget target(RtxOptionEditTarget::User);
      NumosSettings::write(s.tuneStart);
    }
    m_tuning = true;
    m_tuningPreset = presetName;
    m_tuningWeather = weatherId;
    s.snap = true;
  }

  NumosWeather::Preset* NumosWeather::finishTuning(bool keep) {
    if (!m_tuning) {
      return nullptr;
    }
    State& s = *m_state;
    Preset* pStored = nullptr;
    if (keep) {
      const numos::Settings live = NumosSettings::resolved();
      pStored = m_tuningPreset.empty() ? nullptr : find(m_tuningPreset);
      if (pStored == nullptr) {
        const Preset empty;
        pStored = &create(uniqueName(m_tuningWeather.empty() ? std::string("Custom") : "Custom " + m_tuningWeather), &empty);
        if (!m_tuningWeather.empty()) {
          pStored->bindings.push_back(m_tuningWeather);
        }
      }
      // The preset takes its own keys, every weather field and anything else that moved while tuning.
      const auto& infos = numos::settingInfos();
      for (size_t i = 0; i < infos.size(); ++i) {
        const bool moved = !sameField(live, s.tuneStart, infos[i]) && !numos::isDeprecated(infos[i]);
        if (pStored->set[i] || weatherKeys()[i] || moved) {
          copyField(*pStored->values, live, infos[i]);
          pStored->set[i] = true;
          pStored->relative[i] = false;
        }
      }
      pStored->dirty = true;
    }
    {
      RtxOptionLayerTarget target(RtxOptionEditTarget::User);
      NumosSettings::write(s.tuneBaseline);
    }
    m_tuning = false;
    m_tuningPreset.clear();
    m_tuningWeather.clear();
    s.snap = true;
    if (pStored != nullptr) {
      std::string error;
      if (!save(*pStored, &error)) {
        Logger::warn("[NUMOS weather] " + error);
      }
    }
    return pStored;
  }

  void NumosWeather::applyPreset(const Preset* pPreset, numos::Settings& s) const {
    if (pPreset == nullptr) {
      return;
    }
    const auto& infos = numos::settingInfos();
    for (size_t i = 0; i < infos.size() && i < pPreset->set.size(); ++i) {
      if (!pPreset->set[i]) {
        continue;
      }
      if (pPreset->relative[i]) {
        multiplyField(s, *pPreset->values, infos[i]);
      } else {
        copyField(s, *pPreset->values, infos[i]);
      }
    }
  }

  void NumosWeather::blend(const numos::Settings& a, const numos::Settings& b, float t, numos::Settings& out) {
    out = a;
    for (const numos::SettingInfo& info : numos::settingInfos()) {
      switch (info.type) {
      case numos::SettingType::Float: {
        float& o = *reinterpret_cast<float*>(fieldPtr(out, info));
        const float x = *reinterpret_cast<const float*>(fieldPtr(a, info));
        const float y = *reinterpret_cast<const float*>(fieldPtr(b, info));
        o = x + (y - x) * t;
        break;
      }
      case numos::SettingType::Vec3: {
        numos::Vector3& o = *reinterpret_cast<numos::Vector3*>(fieldPtr(out, info));
        const numos::Vector3& x = *reinterpret_cast<const numos::Vector3*>(fieldPtr(a, info));
        const numos::Vector3& y = *reinterpret_cast<const numos::Vector3*>(fieldPtr(b, info));
        o = numos::Vector3(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t);
        break;
      }
      default:
        if (t >= 0.5f) {
          copyField(out, b, info);
        }
        break;
      }
    }
    // The weather fields blend by NUMOS's own rules.
    numos::applyWeather(numos::blendWeather(numos::weatherFromSettings(a), numos::weatherFromSettings(b), t), out);
  }

  void NumosWeather::holdSnapshot(const numos::WeatherSnapshot& weather) {
    State& s = *m_state;
    s.heldPreset = Preset();
    s.heldPreset.name = kSeedTarget;
    numos::applyWeather(weather, *s.heldPreset.values);
    s.heldPreset.set = weatherKeys();
    s.held = true;
  }

  const numos::Settings& NumosWeather::update(const numos::Settings& base, float deltaSeconds) {
    ensureLoaded();
    State& s = *m_state;
    s.base = base;

    // While tuning, the sliders are the look, so no preset applies.
    if (m_tuning) {
      s.current = base;
      s.hasCurrent = true;
      s.elapsed = s.duration = 0.0f;
      m_targetName.clear();
      s.snapshot = numos::weatherFromSettings(s.current);
      return s.current;
    }

    const std::string gameId = gameWeather();
    const std::string manual = preset();
    if (gameId != s.lastGameWeather || manual != s.lastManualPreset) {
      // A new weather from the game or the menu replaces a held seed.
      s.held = false;
      s.lastGameWeather = gameId;
      s.lastManualPreset = manual;
    }

    Preset* pPreview = m_preview.empty() ? nullptr : find(m_preview);
    Preset* pGamePreset = followGame() ? boundPreset(gameId) : nullptr;
    const Preset* pTarget = nullptr;
    if (pPreview != nullptr) {
      pTarget = pPreview;
    } else if (s.held) {
      pTarget = &s.heldPreset;
    } else if (pGamePreset != nullptr) {
      pTarget = pGamePreset;
    } else {
      pTarget = find(manual);
    }
    const std::string targetName = pTarget ? pTarget->name : std::string();

    // A game that blends weathers itself has its blend factor followed exactly.
    const float gameBlend = gameWeatherBlend();
    if (pPreview == nullptr && !s.held && pGamePreset != nullptr && gameBlend >= 0.0f) {
      numos::Settings outgoing = base;
      applyPreset(boundPreset(gameWeatherOutgoing()), outgoing);
      numos::Settings incoming = base;
      applyPreset(pGamePreset, incoming);
      blend(outgoing, incoming, std::clamp(gameBlend, 0.0f, 1.0f), s.current);
      s.hasCurrent = true;
      s.elapsed = s.duration = 0.0f;
      s.snap = false;
      m_targetName = targetName;
      s.snapshot = numos::weatherFromSettings(s.current);
      return s.current;
    }

    numos::Settings goal = base;
    applyPreset(pTarget, goal);
    if (!s.hasCurrent || targetName != m_targetName) {
      s.from = s.hasCurrent ? s.current : goal;
      s.duration = !s.hasCurrent || s.snap ? 0.0f
                 : (pTarget && pTarget->transitionSeconds >= 0.0f ? pTarget->transitionSeconds : transitionSeconds());
      s.elapsed = 0.0f;
      s.hasCurrent = true;
      s.snap = false;
      m_targetName = targetName;
    }
    s.elapsed += std::max(deltaSeconds, 0.0f);
    const float t = s.duration > 0.0f ? s.elapsed / s.duration : 1.0f;
    if (t >= 1.0f) {
      // Live edits show at once after the blend has settled.
      s.current = goal;
    } else {
      blend(s.from, goal, smoothstep01(t), s.current);
    }
    s.snapshot = numos::weatherFromSettings(s.current);
    return s.current;
  }

  const numos::WeatherSnapshot* NumosWeather::currentSnapshot() const {
    return m_state->hasCurrent ? &m_state->snapshot : nullptr;
  }

  float NumosWeather::transitionProgress() const {
    return m_state->duration > 0.0f ? std::min(m_state->elapsed / m_state->duration, 1.0f) : 1.0f;
  }

  const numos::Settings& NumosWeather::lastBase() const {
    return m_state->base;
  }

  const numos::Settings& NumosWeather::current() const {
    return m_state->current;
  }

}
