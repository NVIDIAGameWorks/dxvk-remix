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

#include <cstddef>

#include "numos/settings.h"

#include "rtx_option.h"

namespace dxvk {

  // Exposes the settings table of NUMOS as RTX options with the same keys, see documentation/NumosSky.md.
  // The options register during static initialization, like every RTX_OPTION.
  namespace NumosSettings {

    // Exactly one option pointer is set, selected by the setting's type.
    // All are null for a deprecated setting.
    struct Entry {
      const numos::SettingInfo* pInfo = nullptr;
      RtxOption<bool>* pBoolOption = nullptr;
      RtxOption<int32_t>* pIntOption = nullptr;
      RtxOption<uint32_t>* pUintOption = nullptr;
      RtxOption<float>* pFloatOption = nullptr;
      RtxOption<Vector3>* pVec3Option = nullptr;

      RtxOptionImpl* option() const;
    };

    // The entries follow the order of numos::settingInfos().
    size_t count();
    const Entry& entry(size_t index);
    const Entry* find(const char* key);

    numos::Settings resolved();

    // Writes each option whose value differs through the current RtxOptionLayerTarget, like a menu edit.
    void write(const numos::Settings& settings);

    // Routes NUMOS's log to the Remix log, which gets each distinct message once.
    void installLog();
  }

}
