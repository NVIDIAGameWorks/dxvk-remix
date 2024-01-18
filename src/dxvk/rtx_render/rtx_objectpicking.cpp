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

#include "rtx_objectpicking.h"

#include "rtx_scene_manager.h"

namespace {
  bool keepHighlightRequest(uint32_t frameIdOfRequest, uint32_t curFrameId) {
    constexpr auto numFramesConsiderHighlighting = dxvk::kMaxFramesInFlight * 2;
    return std::abs(int64_t { frameIdOfRequest } - int64_t { curFrameId }) < numFramesConsiderHighlighting;
  }

  int frameDiff(uint32_t pastFrame, uint32_t currentFrame) {
    return static_cast<int>(std::abs(int64_t { currentFrame } - int64_t { pastFrame }));
  }
}

namespace dxvk {
  bool g_allowMappingLegacyHashToObjectPickingValue = true;
}

void dxvk::Highlighting::requestHighlighting(std::variant<XXH64_hash_t, Vector2i, const std::vector<ObjectPickingValue>*> type,
                         HighlightColor color,
                         uint32_t frameId) {
  std::lock_guard lock { m_mutex };
  if (auto objectPickingValues = std::get_if<const std::vector<ObjectPickingValue>*>(&type)) {
    m_type = std::vector<ObjectPickingValue> { (*objectPickingValues)->begin(), (*objectPickingValues)->end() };
    m_lastUpdateFrameId = frameId;
  } else if (auto legacyTextureHash = std::get_if<XXH64_hash_t>(&type)) {
    m_type = *legacyTextureHash;
    m_lastUpdateFrameId = frameId;
  } else if (auto pixel = std::get_if<Vector2i>(&type)) {
    m_type = *pixel;
    m_lastUpdateFrameId = frameId;
  }
  m_color = color;
}

std::optional<std::pair<dxvk::Vector2i, dxvk::HighlightColor>> dxvk::Highlighting::accessPixelToHighlight(uint32_t frameId) {
  std::lock_guard lock { m_mutex };
  if (keepHighlightRequest(m_lastUpdateFrameId, frameId)) {
    if (auto pixel = std::get_if<Vector2i>(&m_type)) {
      return std::make_pair(*pixel, m_color);
    }
  }
  return {};
}


std::pair<std::vector<dxvk::ObjectPickingValue>, dxvk::HighlightColor>
dxvk::Highlighting::accessObjectPickingValueToHighlight(SceneManager& sceneManager, uint32_t frameId) {
  std::lock_guard lock { m_mutex };
  if (!keepHighlightRequest(m_lastUpdateFrameId, frameId)) {
    return {};
  }

  if (auto objectPickingValues = std::get_if<std::vector<ObjectPickingValue>>(&m_type)) {
    return std::make_pair(*objectPickingValues, m_color);
  }
  if (auto texHashToFind = std::get_if<XXH64_hash_t>(&m_type)) {
    if (!g_allowMappingLegacyHashToObjectPickingValue) {
      assert(0);
      return {};
    }
    if (*texHashToFind == kEmptyHash) {
      return std::make_pair(std::vector<ObjectPickingValue>{}, m_color);
    }
    return std::make_pair(sceneManager.gatherObjectPickingValuesByTextureHash(*texHashToFind), m_color);
  }
  return {};
}

bool dxvk::Highlighting::active(uint32_t currentFrameId) const {
  std::lock_guard lock { m_mutex };
  if (m_lastUpdateFrameId == kInvalidFrameIndex) {
    return false;
  }
  // enough frames has passed since highlighting request
  return frameDiff(m_lastUpdateFrameId, currentFrameId) < 128;
}
