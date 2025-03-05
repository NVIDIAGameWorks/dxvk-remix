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
#include <cassert>

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

  enum ProgressState : std::uint8_t {
    // Set when the stage has yet to be loaded.
    Unloaded = 0,

    // Set during the first phase of stage loading when opening the USD file.
    OpeningUSD,
    // Set during the next phases of stage loading when material, mesh or light processing is currently underway.
    // When these are set the progress count may be read from for progress information.
    ProcessingMaterials,
    ProcessingMeshes,
    ProcessingLights,

    // Set when the stage is completely loaded without errors.
    Loaded,
  };

  struct State {
    ProgressState progressState;
    // Note: This progress value should only be read when the stage state is LoadingMaterials, LoadingMeshes or LoadingLights, as it will
    // be uninitialized otherwise.
    std::uint32_t progressCount;
  };

  virtual ~Mod();

  // Loads the mod and initializes the replacements.
  virtual void load(const Rc<DxvkContext>& context) = 0;
  // Unloads the mod and destroys the replacements.
  virtual void unload() = 0;
  // Updates the replacements if mod changed.
  virtual bool checkForChanges(const Rc<DxvkContext>& context) = 0;

  State state() const {
    const auto encodedState{ m_state.load() };
    State decodedState;

    decodedState.progressState = static_cast<ProgressState>((encodedState >> 29ull) & progressStateMask);
    decodedState.progressCount = static_cast<std::uint32_t>((encodedState >> 0ull) & progressCountMask);

    return decodedState;
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

  constexpr static std::uint32_t progressStateMask{ (1ull << 3ull) - 1ull };
  constexpr static std::uint32_t progressCountMask{ (1ull << 29ull) - 1ull };

  constexpr static std::uint32_t encodeProgressState(ProgressState progressState) {
    return static_cast<std::uint32_t>(progressState) << 29ull;
  }

  // Sets the Mod progress state, only to be used when setting to Unloaded, OpeningUSD or Loaded. Use setStateWithProgress for other states.
  void setState(ProgressState progressState) {
    // Note: This set state function is only intended to be used with states that do not have progress associated with them.
    assert(
      progressState == ProgressState::Unloaded ||
      progressState == ProgressState::OpeningUSD ||
      progressState == ProgressState::Loaded
    );

    m_state = encodeProgressState(progressState);
  }

  // Sets the Mod progress state with a progress count value, only to be used when setting to ProcessingMaterials/Meshes/Lights. Use setState for other states.
  void setStateWithCount(ProgressState progressState, std::uint32_t progressCount) {
    // Note: This set state function is only intended to be used with states that do not have progress associated with them.
    assert(
      progressState == ProgressState::ProcessingMaterials ||
      progressState == ProgressState::ProcessingMeshes ||
      progressState == ProgressState::ProcessingLights
    );
    // Note: Ensure the progress count falls within the expected range. The progress count isare masked during encoding for
    // safety as it is in theory possible for values this large to be passed in at runtime, but it likely indicates a bug as there
    // probably should not be 500 million of any sort of asset loading.
    assert(progressCount < (1u << 29u));

    m_state =
      encodeProgressState(progressState) |
      (static_cast<std::uint64_t>(progressCount) & progressCountMask);
  }

  const Path m_filePath;
  std::string m_name;
  size_t m_priority;

  // Note: This uses a 32 bit atomic so that the state as well as the progress count can all be encoded into it together. This ensures all the data
  // can be read at the same time atomically to ensure all the values are in sync without the use of mutexes. This is somewhat important as the progress
  // count will be updated fairly rapidly depending on how fast assets load and avoiding any sort of overhead is ideal.
  // Current encoding: [3 bit progress state] [29 bit progress count]
  std::atomic<std::uint32_t> m_state{ encodeProgressState(ProgressState::Unloaded) };
  std::string m_status = "Unloaded";

  static_assert(decltype(m_state)::is_always_lock_free, "Mod state atomic should be lock-free for performance.");

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
