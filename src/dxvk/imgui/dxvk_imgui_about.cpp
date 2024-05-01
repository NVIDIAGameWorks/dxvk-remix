/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "dxvk_imgui_about.h"

#include <version.h>

#include "imgui.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_objects.h"
#include "rtx_render/rtx_scene_manager.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dxvk {
  constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
  constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;

  void ImGuiAbout::update(const Rc<DxvkContext>& ctx) {
    m_secrets.update(ctx);
  }

  void ImGuiAbout::show(const Rc<DxvkContext>& ctx) {
    ImGui::PushItemWidth(250);

    // Remix Version Information

    ImGui::TextUnformatted("RTX Remix Version: " DXVK_VERSION);
    ImGui::SameLine();

    const auto currentTime = std::chrono::steady_clock::now();
    const char* copyText;

    // Note: Somewhat wasteful to be checking the clock every frame like this when the copied timeout is not even
    // active, but simpler than keeping duplicate state around, and this code is only invoked when the About menu
    // is open anyways so it will have no performance impact on actual games using Remix.
    if (currentTime < m_copiedNotificationTimeout) {
      copyText = "Copied!";
    } else {
      copyText = "Copy to clipboard";
    }

    if (ImGui::SmallButton(copyText)) {
      ImGui::SetClipboardText(DXVK_VERSION);

      // Set a 2 second timeout for the copied notification
      m_copiedNotificationTimeout = currentTime + std::chrono::seconds(2);
    }

    // Remix Credits

    if (ImGui::CollapsingHeader("Credits", collapsingHeaderFlags)) {
      ImGui::TextUnformatted("Produced by NVIDIA Lightspeed Studios");
      ImGui::TextUnformatted("Based on the DXVK project");

      ImGui::Separator();

      m_credits.show();
    }

    // Secret Code Section

    if (ImGui::CollapsingHeader("Secrets", collapsingHeaderClosedFlags)) {
      m_secrets.show(ctx);
    }

    ImGui::PopItemWidth();
  }

  ImGuiAbout::Credits::Credits()
    : m_sections({
      { "Github Contributors",
        { "Alexander 'xoxor4d' Engel",
          "Leonardo Leotte",
          "Nico Rodrigues-McKenna",
          "James 'jdswebb' Webb",
          "James Horsley 'mmdanggg2'",
          "Friedrich 'pixelcluster' Vock",
          "Dayton 'watbulb'",
      }},
      { "Engineering",
        { "Riley Alston",
          "Xiangshun Bei",
          "Damien Bataille",
          "Sam Bourne",
          "David Driver-Gomm",
          "Alex Dunn",
          "Nicholas Freybler",
          "Shona Gillard",
          "Mark Henderson",
          "Alexander Jaus",
          "Nicolas Kendall-Bar",
          "Peter Kristof",
          "Zachary Kupu",
          "Ed Leafe",
          "Lindsay Lutz",
          "Dmitriy Marshak",
          "Yaobin Ouyang",
          "Alexey Panteleev",
          "Jerran Schmidt",
          "Sascha Sertel",
          "Nuno Subtil",
          "Ilya Terentiev",
          "Sunny Thakkar",
          "Pierre-Olivier Trottier",
          "Sultim Tsyrendashiev",
          "Lakshmi Vengesanam"}},
      { "Art",
        { "Vernon Andres-Quentin",
          "Filippo Baraccani",
          "Kelsey Blanton",
          "Stan Brown",
          "Rafael Chies",
          "Derk Elshof",
          "Ivan Filipchenko",
          "Hunter Hazen",
          "Fred Hooper",
          "Vadym Kovalenko",
          "Max Kozhevnikov",
          "Gabriele Leone",
          "Evgeny Leonov",
          "Emmanuel Marshall",
          "Aleksey Semenov",
          "Ilya Shelementsev",
          "Dmytro Siromakha",
          "Oleksandr Smirnov",
          "Mostafa Sobhi",
          "Chase Telegin",
          "Oleksii Tronchuk"}},
      { "Production",
        { "Kelsey Blanton",
          "Wendy Gram",
          "Jaakko Haapasalo",
          "Nyle Usmani"}},
      { "PR/Marketing",
        { "Tim Adams",
          "Brian Burke",
          "Andrew Iain Burnes",
          "Dave Janssen",
          "Jessie Lawrence",
          "Randy Niu",
          "Mike Pepe",
          "Mark Religioso",
          "Kris Rey",
          "Suroosh Taeb",
          "Chris Turner",
          "Keoki Young",
          "Jakob Zamora"}},
      { "Special Thanks",
        { "Alex Hyder",
          "Keith Li",
          "Jarvis McGee",
          "Liam Middlebrook",
          "Adam Moss",
          "Jason Paul",
          "Seth Schneider",
          "Mike Songy",
          "John Spitzer",
          "Sylvain Trottier",
          "--",
          "Everyone contributing to #ct-lss-classic-rtx",
          "Valve"}},
      { "In Memory",
        { "Landon Montgomery" }}
    }) {}

  void ImGuiAbout::Credits::show() {
    for (const auto& creditSection : m_sections) {
      ImGui::TextUnformatted(creditSection.sectionName);
      ImGui::Indent();
      for (const auto& name : creditSection.names) {
        ImGui::TextUnformatted(name);
      }
      ImGui::Unindent();
    }
  }

  void ImGuiAbout::Secrets::update(const Rc<DxvkContext>& ctx) {
    auto& iAssetReplacer =
      ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
    if (iAssetReplacer->hasNewSecretReplacementInfo()) {
      const auto& secretReplacements = iAssetReplacer->getSecretReplacementInfo();
      m_organizedSecrets.clear();
      m_codeHashesToSecretPtrs.clear();
      m_assetHashesToSecretPtrs.clear();
      m_visibleHeaders.clear();
      m_validCodeHashesEntered.clear();
      for(auto [hash, secretReplacements] : secretReplacements) {
        for(auto& secretReplacement : secretReplacements) {
          const bool bUnlocked = 
            (m_validCodeHashesEntered.count(secretReplacement.unlockHash) > 0) ||
            (secretReplacement.unlockHash == 0x0);
          m_organizedSecrets[secretReplacement.header].push_back(Secret{
            secretReplacement, false, bUnlocked });
        }
      }
      for (auto& [header, secrets] : m_organizedSecrets) {
        m_visibleHeaders[header] = false;
        for (auto& secret : secrets) {
          if (secret.bUnlocked || secret.replacement.bDisplayBeforeUnlocked) {
            m_visibleHeaders[header] = true;
          }
          m_codeHashesToSecretPtrs[secret.replacement.unlockHash].push_back(&secret);
          m_assetHashesToSecretPtrs[secret.replacement.assetHash].push_back(&secret);
        }
      }
    }
    for(const auto validCodeHash : m_validCodeHashesEntered) {
      for(const auto* pSecret : m_codeHashesToSecretPtrs[validCodeHash]) {
      }
    }
  }

  void ImGuiAbout::Secrets::show(const Rc<DxvkContext>& ctx) {
    auto& iAssetReplacer =
      ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
    showCodeHashEntry();
    for (auto& [header, secrets] : m_organizedSecrets) {
      if (m_visibleHeaders[header]) {
        ImGui::Indent();
        if (ImGui::CollapsingHeader(header.c_str(), collapsingHeaderFlags)) {
          for (auto& secret : secrets) {
            if (secret.bUnlocked) {
              if(ImGui::Checkbox(secret.replacement.name.c_str(), &secret.bEnabled)) {
                if(secret.bEnabled && secret.replacement.bExclusiveReplacement) {
                  for(auto* const pOtherSecret : m_assetHashesToSecretPtrs[secret.replacement.assetHash]) {
                    pOtherSecret->bEnabled = (&secret == pOtherSecret);
                  }
                }
                iAssetReplacer->markVariantStatus(secret.replacement.assetHash,
                                                  secret.replacement.variantId,
                                                  secret.bEnabled);
              }
            } else if (secret.replacement.bDisplayBeforeUnlocked) {
              ImGui::Indent();
              ImGui::TextUnformatted(secret.replacement.name.c_str());
              ImGui::Unindent();
            }
          }
        }
        ImGui::Unindent();
      }
    }
  }

  void ImGuiAbout::Secrets::showCodeHashEntry() {
    ImGui::TextUnformatted("Codeword:");
    ImGui::SameLine();
    static char codewordBuf[32] = "";
    static auto sameLineAndButton = [&]() {
      ImGui::SameLine();
      return ImGui::Button("Enter");
    };
    if (ImGui::InputText(" " /*Cannot be empty string or else weird ImGUI assert*/,
                          codewordBuf,
                          IM_ARRAYSIZE(codewordBuf),
                          ImGuiInputTextFlags_EnterReturnsTrue)
        || sameLineAndButton()) {
      const XXH64_hash_t hashedCodeword = XXH3_64bits(&codewordBuf[0], strnlen(codewordBuf,IM_ARRAYSIZE(codewordBuf)));
      if((m_validCodeHashesEntered.count(hashedCodeword) == 0) &&
          (m_codeHashesToSecretPtrs.count(hashedCodeword) > 0)) {
        m_validCodeHashesEntered.insert(hashedCodeword);
        for(auto& pSecret : m_codeHashesToSecretPtrs.at(hashedCodeword)) {
          pSecret->bUnlocked = true;
        }
      }
    }
  }
}
