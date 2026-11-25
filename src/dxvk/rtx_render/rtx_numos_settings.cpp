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
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "numos/log.h"

#include "rtx_numos_settings.h"
#include "../../util/log/log.h"

namespace dxvk {

  namespace NumosSettings {

    namespace {

      template <typename T>
      T& field(numos::Settings& settings, const numos::SettingInfo& info) {
        return *reinterpret_cast<T*>(reinterpret_cast<char*>(&settings) + info.offset);
      }

      template <typename T>
      const T& field(const numos::Settings& settings, const numos::SettingInfo& info) {
        return *reinterpret_cast<const T*>(reinterpret_cast<const char*>(&settings) + info.offset);
      }

      Vector3 toRemix(const numos::Vector3& v) { return Vector3(v.x, v.y, v.z); }
      numos::Vector3 toNumos(const Vector3& v) { return numos::Vector3(v.x, v.y, v.z); }

      // The user menu shows these settings, so they save to user.conf.
      constexpr const char* kUserSettingKeys[] = {
        "rtx.atmosphere.aerialPerspective",
        "rtx.atmosphere.aerosolDensity",
        "rtx.atmosphere.hazeWorldScale",
        "rtx.atmosphere.fogVisibilityMeters",
        "rtx.atmosphere.aerialPerspectiveSceneShadow",
        "rtx.atmosphere.aerialPerspectiveLocalLights",
        "rtx.atmosphere.cloudEnabled",
        "rtx.atmosphere.cloudCoverageMean",
        "rtx.atmosphere.cloudDensity",
        "rtx.atmosphere.cloudTypeMean",
        "rtx.atmosphere.cloudShadowStrength",
        "rtx.atmosphere.cloudPerformancePreset",
        "rtx.atmosphere.cloudMarchRate",
        "rtx.atmosphere.nightGainStops",
        "rtx.atmosphere.starGainStops",
        "rtx.atmosphere.moonlightGainStops",
        "rtx.atmosphere.moonDiscGainStops",
      };

      bool isUserSetting(const numos::SettingInfo& info) {
        for (const char* key : kUserSettingKeys) {
          if (info.key == key) {
            return true;
          }
        }
        return false;
      }

      template <typename T>
      RtxOptionArgs<T> optionArgs(const numos::SettingInfo& info) {
        RtxOptionArgs<T> args;
        if (isUserSetting(info)) {
          args.flags |= RtxOptionFlags::UserSetting;
        }
        if constexpr (!std::is_same_v<T, bool>) {
          if (info.minValue != numos::NUMOS_NO_MIN) {
            if constexpr (std::is_same_v<T, Vector3>) {
              args.minValue = Vector3(info.minValue);
            } else {
              args.minValue = T(info.minValue);
            }
          }
          if (info.maxValue != numos::NUMOS_NO_MAX) {
            if constexpr (std::is_same_v<T, Vector3>) {
              args.maxValue = Vector3(info.maxValue);
            } else {
              args.maxValue = T(info.maxValue);
            }
          }
        }
        return args;
      }

      // RTX_OPTION's factory returns a prvalue, which constructs this member at its final address.
      template <typename T>
      struct Held {
        RtxOption<T> option;
        Held(const char* category, const char* name, const T& value, const char* description, RtxOptionArgs<T> args)
          : option(RtxOption<T>::privateMacroFactory(category, name, value, description, args)) { }
      };

      // The options have to register before the config files load. Registering must not read the option map,
      // because the registry copies it for every later registration once it has been read.
      // A deque never moves its elements, which the option registry points at for the life of the process.
      struct Registry {
        // The options keep pointers to these categories and names.
        std::deque<std::string> names;
        std::deque<Held<bool>> bools;
        std::deque<Held<int32_t>> ints;
        std::deque<Held<uint32_t>> uints;
        std::deque<Held<float>> floats;
        std::deque<Held<Vector3>> vec3s;
        std::vector<Entry> entries;

        Registry() {
          const numos::Settings defaults {};
          const std::vector<numos::SettingInfo>& infos = numos::settingInfos();
          entries.resize(infos.size());
          for (size_t i = 0; i < infos.size(); ++i) {
            const numos::SettingInfo& info = infos[i];
            Entry& e = entries[i];
            e.pInfo = &info;
            if (numos::isDeprecated(info)) {
              continue;
            }
            const size_t dot = info.key.rfind('.');
            const char* category = names.emplace_back(info.key.substr(0, dot)).c_str();
            const char* name = names.emplace_back(info.key.substr(dot + 1)).c_str();
            const char* description = info.description ? info.description : "";
            switch (info.type) {
            case numos::SettingType::Bool:
              e.pBoolOption = &bools.emplace_back(category, name, field<bool>(defaults, info), description, optionArgs<bool>(info)).option;
              break;
            case numos::SettingType::Int:
              e.pIntOption = &ints.emplace_back(category, name, field<int32_t>(defaults, info), description, optionArgs<int32_t>(info)).option;
              break;
            case numos::SettingType::Uint:
              e.pUintOption = &uints.emplace_back(category, name, field<uint32_t>(defaults, info), description, optionArgs<uint32_t>(info)).option;
              break;
            case numos::SettingType::Float:
              e.pFloatOption = &floats.emplace_back(category, name, field<float>(defaults, info), description, optionArgs<float>(info)).option;
              break;
            case numos::SettingType::Vec3:
              e.pVec3Option = &vec3s.emplace_back(category, name, toRemix(field<numos::Vector3>(defaults, info)), description, optionArgs<Vector3>(info)).option;
              break;
            }
          }
        }
      };

      Registry& registry() {
        static Registry r;
        return r;
      }

      // Registers the options during static initialization.
      const Registry& g_registry = registry();

    }

    RtxOptionImpl* Entry::option() const {
      if (pBoolOption) {
        return pBoolOption;
      }
      if (pIntOption) {
        return pIntOption;
      }
      if (pUintOption) {
        return pUintOption;
      }
      if (pFloatOption) {
        return pFloatOption;
      }
      return pVec3Option;
    }

    size_t count() {
      return registry().entries.size();
    }

    const Entry& entry(size_t index) {
      return registry().entries[index];
    }

    const Entry* find(const char* key) {
      const numos::SettingInfo* pInfo = numos::findSetting(key);
      if (pInfo == nullptr) {
        return nullptr;
      }
      const size_t index = size_t(pInfo - numos::settingInfos().data());
      return index < count() ? &entry(index) : nullptr;
    }

    numos::Settings resolved() {
      numos::Settings settings {};
      for (size_t i = 0; i < count(); ++i) {
        const Entry& e = entry(i);
        if (e.pBoolOption) {
          field<bool>(settings, *e.pInfo) = e.pBoolOption->get();
        } else if (e.pIntOption) {
          field<int32_t>(settings, *e.pInfo) = e.pIntOption->get();
        } else if (e.pUintOption) {
          field<uint32_t>(settings, *e.pInfo) = e.pUintOption->get();
        } else if (e.pFloatOption) {
          field<float>(settings, *e.pInfo) = e.pFloatOption->get();
        } else if (e.pVec3Option) {
          field<numos::Vector3>(settings, *e.pInfo) = toNumos(e.pVec3Option->get());
        }
      }
      return settings;
    }

    void write(const numos::Settings& settings) {
      for (size_t i = 0; i < count(); ++i) {
        const Entry& e = entry(i);
        if (e.pBoolOption) {
          const bool value = field<bool>(settings, *e.pInfo);
          if (e.pBoolOption->get() != value) {
            e.pBoolOption->setDeferred(value);
          }
        } else if (e.pIntOption) {
          const int32_t value = field<int32_t>(settings, *e.pInfo);
          if (e.pIntOption->get() != value) {
            e.pIntOption->setDeferred(value);
          }
        } else if (e.pUintOption) {
          const uint32_t value = field<uint32_t>(settings, *e.pInfo);
          if (e.pUintOption->get() != value) {
            e.pUintOption->setDeferred(value);
          }
        } else if (e.pFloatOption) {
          const float value = field<float>(settings, *e.pInfo);
          if (e.pFloatOption->get() != value) {
            e.pFloatOption->setDeferred(value);
          }
        } else if (e.pVec3Option) {
          const Vector3 value = toRemix(field<numos::Vector3>(settings, *e.pInfo));
          if (e.pVec3Option->get() != value) {
            e.pVec3Option->setDeferred(value);
          }
        }
      }
    }

    void installLog() {
      static std::once_flag once;
      std::call_once(once, [] {
        numos::setLogCallback([](numos::LogLevel level, const char* message, void*) {
          // Some NUMOS messages repeat every frame.
          static std::mutex mutex;
          static std::unordered_set<std::string> seen;
          {
            std::lock_guard<std::mutex> lock(mutex);
            if (!seen.insert(message ? message : "").second) {
              return;
            }
          }
          const std::string text = std::string("[NUMOS] ") + (message ? message : "");
          switch (level) {
          case numos::LogLevel::Info:  Logger::info(text); break;
          case numos::LogLevel::Warn:  Logger::warn(text); break;
          case numos::LogLevel::Error: Logger::err(text); break;
          }
        }, nullptr);
      });
    }
  }

}
