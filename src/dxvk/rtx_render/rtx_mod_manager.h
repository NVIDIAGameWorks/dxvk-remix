/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../../util/rc/util_rc_ptr.h"
#include "../../lssusd/game_exporter_paths.h"

#include <string>
#include <filesystem>
#include <set>

namespace dxvk {

class DxvkContext;
class AssetReplacements;

/**
 * \brief Mod base class
 *
 * Base class and interface for all mod types. Owns common Mod data
 * and state, as well as the replacements.
 */
class Mod {
public:
  typedef std::filesystem::path Path;

  enum State : uint32_t {
    Unloaded = 0,
    Loading,
    Loaded,
    Error,
  };

  virtual ~Mod();

  // Loads the mod and initializes the replacements.
  virtual void load(const Rc<DxvkContext>& context) = 0;
  // Unloads the mod and destroys the replacements.
  virtual void unload() = 0;
  // Updates the replacements if mod changed.
  virtual bool checkForChanges(const Rc<DxvkContext>& context) = 0;

  State state() const {
    return m_state.load();
  }

  const std::string& status() const {
    return m_status;
  }

  AssetReplacements& replacements() {
    return *m_replacements.get();
  }

  const Path& path() const {
    return m_filePath;
  }

  struct ComparePtrs {
    bool operator()(const std::unique_ptr<Mod>& lhs,
                    const std::unique_ptr<Mod>& rhs) const {
      if (lhs->m_filePath == rhs->m_filePath) {
        return false;
      } else if (lhs->m_priority != rhs->m_priority) {
        return lhs->m_priority < rhs->m_priority;
      }
      return lhs->m_filePath < rhs->m_filePath;
    }
  };

protected:
  explicit Mod(const Path& filePath);

  void setState(State state) {
    m_state = state;
  }

  const Path m_filePath;
  std::string m_name;
  size_t m_priority;

  std::atomic<State> m_state;
  std::string m_status = "Unloaded";

  std::unique_ptr<AssetReplacements> m_replacements;
};

/**
 * \brief Mod type information base class
 *
 * Contains Mod type information, implements path validation query
 * and Mod object factory.
 */
struct ModTypeInfo {
  virtual std::unique_ptr<Mod> construct(const Mod::Path& modFilePath) const = 0;
  virtual bool isValidMod(const Mod::Path& modFilePath) const = 0;
};

/**
 * \brief Mod Manager
 *
 * Discovers and manages the Mods.
 */
class ModManager {
public:
  typedef std::set<std::unique_ptr<Mod>, Mod::ComparePtrs> Mods;

  ModManager();

  // Refresh mods, create the newly discovered mods and
  // destroy the removed mods.
  void refreshMods();

  const Mods& mods() const {
    return m_mods;
  }

  static std::string getBaseGameModPath(std::string baseGameModRegexStr, std::string baseGameModPathRegexStr);
private:
  using Path = Mod::Path;

  Mods enumerateAllMods();
  Mods enumerateModsInDir(const Path& modsDirPath);

  Mods m_mods;
};

}
