/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#include "../../util/rc/util_rc.h"
#include "../../util/rc/util_rc_ptr.h"
#include "../../util/xxHash/xxhash.h"
#include "rtx_render/rtx_asset_replacer.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>

namespace dxvk {
  class DxvkContext;
  class SecretMeshes;

  class ImGuiAbout : public RcObject {
  public:
    ImGuiAbout() {
    }
    ImGuiAbout(const ImGuiAbout& other) = delete;
    ImGuiAbout(const ImGuiAbout&& other) = delete;
    void update(const Rc<DxvkContext>& ctx);
    void show(const Rc<DxvkContext>& ctx);

  private:
    std::chrono::time_point<std::chrono::steady_clock> m_copiedNotificationTimeout{};

    class Credits {
    public:
      Credits();
      Credits(const Credits& other) = delete;
      Credits(const Credits&& other) = delete;
      void show();
    private:
      struct Section {
        const char* const sectionName;
        const std::vector<const char*> names;
      };
      const std::vector<Section> m_sections;
    } m_credits;

    class Secrets {
    public:
      Secrets() {
      }
      Secrets(const Secrets& other) = delete;
      Secrets(const Secrets&& other) = delete;
      void update(const Rc<DxvkContext>& ctx);
      void show(const Rc<DxvkContext>& ctx);
    private:
      void showCodeHashEntry();
      struct Secret {
        SecretReplacement replacement;
        bool bEnabled = false;
        bool bUnlocked = false;
      };
      using HeaderToSecrets =
        std::unordered_map<std::string, std::vector<Secret>>;
      using HashToSecretPtrs =
        std::unordered_map<XXH64_hash_t, std::vector<Secret*>>;
      HeaderToSecrets m_organizedSecrets;
      HashToSecretPtrs m_codeHashesToSecretPtrs;
      HashToSecretPtrs m_assetHashesToSecretPtrs;
      std::unordered_map<std::string,bool> m_visibleHeaders;
      std::unordered_set<XXH64_hash_t> m_validCodeHashesEntered;
    } m_secrets;
  };
}