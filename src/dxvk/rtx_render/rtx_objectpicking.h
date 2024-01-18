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

#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <variant>

#include "rtx_constants.h"
#include "rtx_utils.h"
#include "../util/util_vector.h"

namespace dxvk {
  using ObjectPickingValue = uint32_t;

  enum class HighlightColor {
    World,
    UI,
    FromVariable,
  };

  extern bool g_allowMappingLegacyHashToObjectPickingValue;

  class SceneManager;

  struct ObjectPicking {

    using Callback = std::function<
      void(std::vector<ObjectPickingValue>&& /* objectPickingValues */,
           std::optional<XXH64_hash_t>       /* legacyTextureHash -- corresponding to objectPickingValues[0] */)
    >;

    struct Request {
      Vector2i pixelFrom { 0,0 };
      Vector2i pixelTo { 0,0 };
      Callback callback {};

      Request() = default;
      ~Request() = default;
      // allow only move
      Request(Request&& other) noexcept {
        pixelFrom = other.pixelFrom;
        pixelTo = other.pixelTo;
        callback = std::move(other.callback);
      }
      Request& operator=(Request&& other)noexcept {
        pixelFrom = other.pixelFrom;
        pixelTo = other.pixelTo;
        callback = std::move(other.callback);
        return *this;
      }
      // and error on copy
      Request(const Request&) = delete;
      Request& operator=(const Request&) = delete;
    };

    void request(const Vector2i& pixelFrom,
                 const Vector2i& pixelTo,
                 Callback callback) {
      auto newRequest = Request {};
      {
        newRequest.pixelFrom = Vector2i { std::min(pixelFrom.x, pixelTo.x), std::min(pixelFrom.y, pixelTo.y) };
        newRequest.pixelTo = Vector2i { std::max(pixelFrom.x, pixelTo.x), std::max(pixelFrom.y, pixelTo.y) };
        newRequest.callback = std::move(callback);
      }

      std::lock_guard lock { m_mutex };
      m_requests.push(std::move(newRequest));
    }

    std::optional<Request> popRequest() {
      std::lock_guard lock { m_mutex };
      if (!m_requests.empty()) {
        Request r = std::move(m_requests.front());
        m_requests.pop();
        return std::optional { std::move(r) };
      }
      return {};
    }

    bool containsRequests() const {
      std::lock_guard lock { m_mutex };
      return !m_requests.empty();
    }

  private:
    mutable dxvk::mutex m_mutex {};
    std::queue<Request> m_requests {};
  };


  struct Highlighting {
    void requestHighlighting(std::variant<XXH64_hash_t, Vector2i, const std::vector<ObjectPickingValue>*> type,
                             HighlightColor color,
                             uint32_t frameId);

    std::optional<std::pair<Vector2i, HighlightColor>> accessPixelToHighlight(uint32_t frameId);
    std::pair<std::vector<ObjectPickingValue>, HighlightColor> accessObjectPickingValueToHighlight(SceneManager &sceneManager, uint32_t frameId);

    bool active(uint32_t currentFrameId) const;
  private:
    mutable dxvk::mutex m_mutex {};
    uint32_t m_lastUpdateFrameId { kInvalidFrameIndex };
    HighlightColor m_color {};
    std::variant<
      std::monostate,   // None
      XXH64_hash_t,     // Legacy texture hash
      Vector2i,         // If 'objectPickingValue' need to be fetched from this pixel at GPU time,
                        // so there's no GPU->CPU->GPU latency
      std::vector<ObjectPickingValue> // Object picking values
    > m_type {};
  };
}
