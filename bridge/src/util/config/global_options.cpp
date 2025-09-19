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
#include "global_options.h"

#include <windows.h>

GlobalOptions GlobalOptions::instance;

void GlobalOptions::initSharedHeapPolicy() {
  // Parse shared heap policy config only when we actually use shared heap
  if (useSharedHeap) {
    const auto sharedHeapPolicyStr =
      bridge_util::Config::getOption<std::vector<std::string>>("sharedHeapPolicy", std::vector<std::string>{});

    if(sharedHeapPolicyStr.empty()) {
      // Use shared heap for everything other than dynamic buffers by default
      static constexpr auto kDefaultSharedHeapPolicy = (SharedHeapPolicy::Textures | SharedHeapPolicy::StaticBuffers);
      sharedHeapPolicy = kDefaultSharedHeapPolicy;
    } else {
      sharedHeapPolicy = SharedHeapPolicy::None;
      for (auto& policyStr : sharedHeapPolicyStr) {
        if (policyStr == "Textures") {
          sharedHeapPolicy |= SharedHeapPolicy::Textures;
        } else if (policyStr == "DynamicBuffers") {
          sharedHeapPolicy |= SharedHeapPolicy::DynamicBuffers;
        } else if (policyStr == "StaticBuffers") {
          sharedHeapPolicy |= SharedHeapPolicy::StaticBuffers;
        } else {
          bridge_util::Logger::warn("Unknown shared heap policy string: " + policyStr);
        }
      }
    }

    const bool bPolicyTextures = sharedHeapPolicy & SharedHeapPolicy::Textures;
          bool bPolicyDynamicBufs = sharedHeapPolicy & SharedHeapPolicy::DynamicBuffers;
    const bool bPolicyStaticBufs = sharedHeapPolicy & SharedHeapPolicy::StaticBuffers;
    
    if(bridge_util::Config::isOptionDefined("useShadowMemoryForDynamicBuffers")) {
      const bool bUseShadowMemoryForDynamicBuffers =
        bridge_util::Config::getOption<bool>("useShadowMemoryForDynamicBuffers");
      if(bUseShadowMemoryForDynamicBuffers == bPolicyDynamicBufs) {
        std::stringstream conflictSS;
        conflictSS << "SharedHeap dynamic buffer policy: [" << ((bPolicyDynamicBufs) ? "True" : "False") << "] ";
        conflictSS << "superceded by useShadowMemoryForDynamicBuffers config setting: [" << ((bUseShadowMemoryForDynamicBuffers) ? "True" : "False") << "]";
        bridge_util::Logger::info(conflictSS.str());
        sharedHeapPolicy ^= SharedHeapPolicy::DynamicBuffers; // If 1, becomes 0; If 0, becomes 1
        bPolicyDynamicBufs = sharedHeapPolicy & SharedHeapPolicy::DynamicBuffers;
      }
    }

    std::stringstream policySS;
    policySS << "SharedHeap policy: ";
    if(sharedHeapPolicy == SharedHeapPolicy::None) {
      policySS << "NONE";
    } else {
      if(bPolicyTextures) {
        policySS << "TEXTURES, ";
      }
      if(bPolicyDynamicBufs) {
        policySS << "DYNAMIC BUFFERS, ";
      }
      if(bPolicyStaticBufs) {
        policySS << "STATIC BUFFERS";
      }
    }

    bridge_util::Logger::info(policySS.str());
  } else {
    sharedHeapPolicy = SharedHeapPolicy::None;
  }
}