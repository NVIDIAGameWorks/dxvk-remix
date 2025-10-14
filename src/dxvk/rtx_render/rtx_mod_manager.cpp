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
#include "rtx_mod_manager.h"

#include "rtx_asset_replacer.h"
#include "rtx_mod_usd.h"

#include "rtx_options.h"

#include "../../util/log/log.h"
#include "../../util/util_filesys.h"
#include "../../util/util_string.h"

#include <unordered_set>
#include <assert.h>
#include <regex>

namespace fs = std::filesystem;

namespace dxvk {

static const std::string kDefaultModFileName = "mod";

static const ModTypeInfo* kModTypeInfos[] = {
  &UsdMod::getTypeInfo(),
};

ModManager::ModManager() {
  refreshMods();
}

void ModManager::refreshMods() {
  auto updatedMods = enumerateAllMods();

  // Check if any of the current mods have been removed and destroy them
  for (auto it = m_mods.begin(); it != m_mods.end();) {
    if (updatedMods.count(*it) == 0) {
      it = m_mods.erase(it);
    } else {
      ++it;
    }
  }

  // Merge new mods if any
  m_mods.merge(updatedMods);
}

std::string ModManager::getBaseGameModPath(std::string baseGameModRegexStr, std::string baseGameModPathRegexStr) {
  std::string commandLine = GetCommandLineA();
  std::smatch match;
  std::string baseGameModPath = "";

  if (baseGameModRegexStr != "" && std::regex_search(commandLine, match, std::regex(baseGameModRegexStr, std::regex::icase))) {
    std::regex baseGameModPathRegex(baseGameModPathRegexStr, std::regex::icase);
    if (regex_search(commandLine, match, baseGameModPathRegex)) {
      baseGameModPath = match.str(1);
      std::replace(baseGameModPath.begin(), baseGameModPath.end(), '\\', '/');
    }
  }
  return baseGameModPath;
}

ModManager::Mods ModManager::enumerateAllMods() {
  Mods mods;
  
  const std::string modsPath = util::RtxFileSys::path(util::RtxFileSys::Mods).string();
  
  const Path defaultModsDir = Path(modsPath).lexically_normal();

  std::string baseGameModPath = getBaseGameModPath(RtxOptions::baseGameModRegex(), RtxOptions::baseGameModPathRegex());

  std::vector<Path> vecModsDirs; // [TODO] = GetAddtlModsSearchDirs();
  if (baseGameModPath != "") {
    vecModsDirs.push_back(baseGameModPath + "/rtx-remix/mods/");
  }
  vecModsDirs.push_back(defaultModsDir);
  for (const auto& modsDirPath : vecModsDirs) {
    if (!fs::exists(modsDirPath)) {
      static std::unordered_set<std::string> warnedOnce;
      if(warnedOnce.count(modsDirPath.string()) == 0) {
        Logger::warn(str::format("Cannot find ",modsDirPath.c_str()," under current directory: ", fs::current_path().c_str()));
        warnedOnce.insert(modsDirPath.string());
      }
      continue;
    }
    mods.merge(enumerateModsInDir(modsDirPath));
  }
  return mods;
}

struct ModDesc {
  Mod::Path filePath;
  const ModTypeInfo* typeInfo;
};

static std::tuple<bool, ModDesc> modExists(const Mod::Path& modDirPath) {
  for (auto const& modDirEntry : fs::directory_iterator { modDirPath }) {
    const auto potentialModPath = modDirEntry.path();
    if (potentialModPath.stem().string().compare(kDefaultModFileName) == 0) {
      for (auto modTypeInfo : kModTypeInfos) {
        if (modTypeInfo->isValidMod(potentialModPath)) {
          return { true, { potentialModPath, modTypeInfo } };
        }
      }
    }
  }
  return { false, {} };
}

ModManager::Mods ModManager::enumerateModsInDir(const Path& modsDirPath) {
  Mods mods;
  for (auto const& modsDirEntry : fs::directory_iterator{modsDirPath}) {
    if (modsDirEntry.is_directory()) {
      const auto modDirPath = modsDirEntry.path();
      const auto [bModExists, modDesc] = modExists(modDirPath);
      if (bModExists) {
        mods.emplace(modDesc.typeInfo->construct(modDesc.filePath));
      }
    }
  }
  return mods;
}

Mod::Mod(const Path& filePath)
  : m_filePath(filePath),
    m_name("default"),
    m_priority(0) {
  m_replacements = std::make_unique<AssetReplacements>();
}

Mod::~Mod() {
}

}
