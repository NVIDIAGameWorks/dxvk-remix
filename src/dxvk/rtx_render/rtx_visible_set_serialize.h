/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>

#include "../../util/util_bounding_box.h"
#include "../../util/util_matrix.h"
#include "../../util/util_string.h"
#include "../../util/util_vector.h"

namespace dxvk {

  // These are the request flags a consumer sends in bits 32 to 39 of wParam.
  // kVisibleSetInViewOnly lists only what the main camera can see.
  // kVisibleSetSaveImage saves the frame's first-hit surface-index image to a file that the answer names.
  constexpr uint32_t kVisibleSetInViewOnly = 1u << 0;
  constexpr uint32_t kVisibleSetSaveImage = 1u << 1;

  // Is the most detailed level of InstanceDetailLevel, as a number.
  constexpr uint32_t kMaxVisibleSetDetailLevel = 2;

  // Describes one request, decoded from its window message.
  struct VisibleSetRequest {
    uint32_t id = 0;
    uint32_t flags = 0;
    // Holds the detail level the request asks for, as 1 for Summary, 2 for Standard and 3 for Full.
    // 0 uses rtx.visibleSetDump.detailLevel.
    uint32_t detailLevel = 0;
  };

  // Decodes a request from its window message.
  // A request is one PostMessage, so its parameters are packed into wParam.
  // It holds the id in bits 0 to 31, the flags in bits 32 to 39 and the detail level in bits 40 to 47.
  inline VisibleSetRequest decodeVisibleSetRequest(uint64_t wParam) {
    VisibleSetRequest request;
    request.id = static_cast<uint32_t>(wParam);
    request.flags = static_cast<uint32_t>(wParam >> 32) & 0xffu;
    request.detailLevel = static_cast<uint32_t>(wParam >> 40) & 0xffu;
    return request;
  }

  // Returns the InstanceDetailLevel a request asked for, as a number, capped at kMaxVisibleSetDetailLevel.
  // Returns optionValue when the request asked for none.
  inline uint32_t visibleSetDetailLevel(uint32_t requested, uint32_t optionValue) {
    return requested != 0 ? std::min(requested - 1, kMaxVisibleSetDetailLevel) : optionValue;
  }

  // Holds where one value's pixels are in an image, such as one surface's pixels in the first-hit surface-index image.
  struct DrawnCoverage {
    uint32_t pixels = 0;
    uint32_t minX = std::numeric_limits<uint32_t>::max();
    uint32_t minY = std::numeric_limits<uint32_t>::max();
    uint32_t maxX = 0;
    uint32_t maxY = 0;
  };

  // Counts where each value appears in an image of width by height pixels, stored row by row from the top left.
  // Pixels holding invalidValue, which marks pixels without a hit, are skipped.
  inline std::unordered_map<uint32_t, DrawnCoverage> reduceDrawnCoverage(const uint32_t* pValues, uint32_t width,
                                                                         uint32_t height, uint32_t invalidValue) {
    std::unordered_map<uint32_t, DrawnCoverage> coverage;
    // Neighboring pixels usually share a value, so the last lookup is reused.
    uint32_t lastValue = invalidValue;
    DrawnCoverage* pLast = nullptr;
    for (uint32_t y = 0; y < height; ++y) {
      const uint32_t* pRow = pValues + size_t(y) * width;
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t value = pRow[x];
        if (value == invalidValue) {
          continue;
        }
        if (pLast == nullptr || value != lastValue) {
          pLast = &coverage[value];
          lastValue = value;
        }
        DrawnCoverage& c = *pLast;
        ++c.pixels;
        c.minX = std::min(c.minX, x);
        c.minY = std::min(c.minY, y);
        c.maxX = std::max(c.maxX, x);
        c.maxY = std::max(c.maxY, y);
      }
    }
    return coverage;
  }

  // Returns the bounds of a coverage's pixels in a width by height image, as x0, y0, x1 and y1 in NDC with +y up.
  inline Vector4 drawnRectNdc(const DrawnCoverage& c, uint32_t width, uint32_t height) {
    // Pixel rows run down the screen, and NDC y runs up.
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    return Vector4(
      c.minX / w * 2.f - 1.f,
      1.f - (c.maxY + 1) / h * 2.f,
      (c.maxX + 1) / w * 2.f - 1.f,
      1.f - c.minY / h * 2.f);
  }

  // Adds the pixels of one coverage to another, for an instance that spans several surfaces.
  inline void mergeDrawnCoverage(DrawnCoverage& into, const DrawnCoverage& from) {
    into.pixels += from.pixels;
    into.minX = std::min(into.minX, from.minX);
    into.minY = std::min(into.minY, from.minY);
    into.maxX = std::max(into.maxX, from.maxX);
    into.maxY = std::max(into.maxY, from.maxY);
  }

  // Returns the drawn object of a coverage in a width by height image.
  // An instance the frame did not draw gets pixels 0 and no rect.
  inline std::string drawnJson(const DrawnCoverage& c, uint32_t width, uint32_t height) {
    const bool drawn = c.pixels != 0 && width != 0 && height != 0;
    return str::asJsonObject({
      { "pixels", str::asJson(c.pixels) },
      { "rectNdc", drawn ? asJson(drawnRectNdc(c, width, height)) : std::string() },
    });
  }

  // clip.w must exceed this for a point to count as in front of the camera.
  constexpr float kMinClipW = 1e-4f;
  // Emitted NDC components are clamped to this magnitude.
  // A point grazing the camera plane projects to arbitrarily large NDC, and nothing past the [-1,1] frame is useful.
  constexpr float kMaxRectNdc = 2.f;

  inline float clampNdc(float v) {
    return std::min(std::max(v, -kMaxRectNdc), kMaxRectNdc);
  }

  // Projects an object-space AABB to an NDC rect with objectToProj, which is worldToProj * objectToWorld.
  // Edges that cross the camera plane are clipped at w = kMinClipW,
  // so a straddling box yields its true footprint rather than the hull of its in-front corners.
  // Returns false when the whole box is behind the camera.
  inline bool projectAabbToNdcRect(const Matrix4& objectToProj, const Vector3& mn, const Vector3& mx,
                                   Vector4& outRect) {
    Vector4 clip[8];
    for (int i = 0; i < 8; ++i) {
      clip[i] = objectToProj * Vector4((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1.f);
    }

    float x0 = std::numeric_limits<float>::max(), y0 = std::numeric_limits<float>::max();
    float x1 = std::numeric_limits<float>::lowest(), y1 = std::numeric_limits<float>::lowest();
    bool any = false;
    const auto accumulate = [&](const Vector4& c) {
      const float nx = c.x / c.w;
      const float ny = c.y / c.w;
      // std::min and std::max keep the incumbent when given a NaN.
      // Without this check, an all-NaN box would set `any`,
      // and the untouched sentinels would be emitted as the inverted rect [2,2,-2,-2].
      if (!std::isfinite(nx) || !std::isfinite(ny)) {
        return;
      }
      x0 = std::min(x0, nx); y0 = std::min(y0, ny);
      x1 = std::max(x1, nx); y1 = std::max(y1, ny);
      any = true;
    };

    for (int i = 0; i < 8; ++i) {
      if (clip[i].w > kMinClipW) {
        accumulate(clip[i]);
      }
    }

    // Lists the corner index pairs of the 12 box edges. Bit k of a corner index selects min or max on axis k.
    static const int kEdges[12][2] = {
      { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 }, { 1, 5 }, { 2, 3 },
      { 2, 6 }, { 3, 7 }, { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 },
    };
    for (const auto& e : kEdges) {
      const Vector4& a = clip[e[0]];
      const Vector4& b = clip[e[1]];
      const bool aFront = a.w > kMinClipW;
      const bool bFront = b.w > kMinClipW;
      if (aFront == bFront) {
        continue;
      }
      const float t = (kMinClipW - a.w) / (b.w - a.w);
      // The lerp can round w to zero, so the crossing point is pinned to the clip plane.
      Vector4 clipped = a + (b - a) * t;
      clipped.w = kMinClipW;
      accumulate(clipped);
    }

    if (!any) {
      return false;
    }
    outRect = Vector4(clampNdc(x0), clampNdc(y0), clampNdc(x1), clampNdc(y1));
    return true;
  }

  // Bounds a GPU-skinned draw by the union of its bind-pose box under each of its bones.
  // Skinning blends a vertex's positions under its bones with weights that sum to one,
  // so every skinned vertex lies inside that union.
  // Returns an invalid box when bindBox is invalid or there are no bones.
  inline AxisAlignedBoundingBox skinBoundingBox(const AxisAlignedBoundingBox& bindBox,
                                                const Matrix4* pBones, size_t boneCount) {
    AxisAlignedBoundingBox skinned;
    if (!bindBox.isValid()) {
      return skinned;
    }
    const Vector3& mn = bindBox.minPos;
    const Vector3& mx = bindBox.maxPos;
    for (size_t b = 0; b < boneCount; ++b) {
      for (int i = 0; i < 8; ++i) {
        const Vector3 p = (pBones[b] * Vector4((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1.f)).xyz();
        skinned.unionWith(AxisAlignedBoundingBox { p, p });
      }
    }
    return skinned;
  }

  // Holds the position of one camera that was valid on the snapshot's frame.
  struct VisibleSetCamera {
    const char* pName = "";
    Vector3 worldPos;
  };

  // Holds one instance of a snapshot.
  struct VisibleSetRecord {
    // Holds the instance as a JSON object, which RtInstance::asJson writes.
    std::string instanceJson;
    // Orders records that are equally far from the camera.
    uint32_t drawCallId = 0;
    bool traced = false;
    // Holds the distance from the camera to the center of the instance's bounding box,
    // or to the instance's position when the draw has no box.
    float distance = 0.f;
    // Holds the projected bounding box as x0, y0, x1 and y1 in NDC with +y up, when hasScreenRect is set.
    bool hasScreenRect = false;
    Vector4 screenRectNdc;
    bool inFrustum = false;
    // Holds the drawn object that drawnJson returns, once the readback is resolved.
    std::string drawnJson;
    // Lists the surface indices of the snapshot's frame that belong to the instance,
    // which are the values its pixels hold in the saved surface-index image.
    std::vector<uint32_t> surfaces;
    uint32_t drawnPixels = 0;
  };

  struct VisibleSetSnapshot {
    uint32_t schemaVersion = 1;
    // Holds the id of the request the snapshot answers.
    uint32_t requestId = 0;
    uint32_t frameId = 0;
    // Counts the instances listed before the cap.
    // A value larger than instances.size() means the list was truncated.
    uint32_t totalInstances = 0;
    bool inViewOnly = false;
    // Holds the InstanceDetailLevel the instances were written with, as a number.
    uint32_t detailLevel = 0;
    // Lists the main camera first.
    std::vector<VisibleSetCamera> cameras;
    std::vector<VisibleSetRecord> instances;
    // Is true when the frame's first-hit surface-index image was read back.
    // The center and drawn blocks are then written.
    bool hasReadback = false;
    // Holds the size of that image, which is the render resolution.
    uint32_t drawnSize[2] = { 0, 0 };
    // Is false when the center pixel shows no geometry, such as sky, and center's hit is then omitted.
    bool hasCenterHit = false;
    VisibleSetRecord centerHit;
    // Holds the path of the saved surface-index image, and is empty when the request did not ask for one.
    // The file holds the width and the height as two little-endian uint32 values,
    // then width * height uint32 surface indices, row by row from the top left.
    std::string surfaceImagePath;
    // Holds the value of a pixel without a hit in that image.
    uint32_t surfaceImageNoHit = 0;
  };

  // Returns whether a nearer than b, for sorting nearest first.
  // Records without a finite distance sort last, and ties are ordered by drawCallId.
  inline bool isNearerVisibleSetRecord(const VisibleSetRecord& a, const VisibleSetRecord& b) {
    const float da = std::isfinite(a.distance) ? a.distance : std::numeric_limits<float>::max();
    const float db = std::isfinite(b.distance) ? b.distance : std::numeric_limits<float>::max();
    if (da != db) {
      return da < db;
    }
    return a.drawCallId < b.drawCallId;
  }

  // Applies a request's flags and the cap to records, and sorts what is left nearest first.
  // A cap keeps drawn records first, then traced records in the frustum, then the rest, each nearest first.
  // Returns how many records the flags kept, before the cap.
  inline uint32_t selectVisibleSetRecords(std::vector<VisibleSetRecord>& records, uint32_t flags, uint32_t cap) {
    const bool inViewOnly = (flags & kVisibleSetInViewOnly) != 0;
    const auto keep = [&](const VisibleSetRecord& r) {
      return !inViewOnly || r.drawnPixels != 0 || (r.traced && r.inFrustum);
    };
    records.erase(std::remove_if(records.begin(), records.end(), [&](const VisibleSetRecord& r) { return !keep(r); }),
                  records.end());
    const uint32_t total = static_cast<uint32_t>(records.size());
    if (cap != 0 && records.size() > cap) {
      const auto rank = [](const VisibleSetRecord& r) {
        return r.drawnPixels != 0 ? 0 : (r.traced && r.inFrustum ? 1 : 2);
      };
      std::stable_sort(records.begin(), records.end(), [&](const VisibleSetRecord& a, const VisibleSetRecord& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : isNearerVisibleSetRecord(a, b);
      });
      records.resize(cap);
    }
    std::stable_sort(records.begin(), records.end(), isNearerVisibleSetRecord);
    return total;
  }

  // Returns one entry of a snapshot's instances: the instance, and where it is on the snapshot's frame.
  inline std::string asJson(const VisibleSetRecord& r, const VisibleSetSnapshot& snap) {
    return str::asJsonObject({
      { "instance", r.instanceJson },
      { "screen", str::asJsonObject({
          { "distance", str::asJson(r.distance) },
          { "rectNdc", r.hasScreenRect ? asJson(r.screenRectNdc) : std::string() },
          // Every instance an in-view request lists is in view, so the member says nothing there.
          { "inFrustum", snap.inViewOnly ? std::string() : str::asJson(r.inFrustum) },
          { "drawn", r.drawnJson },
          { "surfaces", snap.surfaceImagePath.empty() ? std::string() : str::asJson(r.surfaces) },
        }) },
    });
  }

  inline std::string serializeVisibleSet(const VisibleSetSnapshot& snap) {
    std::vector<str::JsonMember> cameras;
    cameras.reserve(snap.cameras.size());
    for (const VisibleSetCamera& camera : snap.cameras) {
      cameras.push_back({ camera.pName, str::asJsonObject({ { "worldPos", asJson(camera.worldPos) } }) });
    }

    // Each top-level member and each instance is on its own line,
    // so a line-based search or read returns whole entries.
    std::string instances = "[";
    for (const VisibleSetRecord& r : snap.instances) {
      instances += instances.size() > 1 ? ",\n" : "\n";
      instances += asJson(r, snap);
    }
    instances += snap.instances.empty() ? "]" : "\n]";

    const std::vector<str::JsonMember> members = {
      { "schemaVersion", str::asJson(snap.schemaVersion) },
      { "requestId", str::asJson(snap.requestId) },
      { "frameId", str::asJson(snap.frameId) },
      { "totalInstances", str::asJson(snap.totalInstances) },
      { "inViewOnly", str::asJson(snap.inViewOnly) },
      { "detailLevel", str::asJson(snap.detailLevel) },
      { "cameras", str::asJsonObject(cameras) },
      { "center", snap.hasReadback
        ? str::asJsonObject({ { "hit", snap.hasCenterHit ? asJson(snap.centerHit, snap) : std::string() } })
        : std::string() },
      { "drawn", snap.hasReadback
        ? str::asJsonObject({ { "size", str::asJson(std::vector<uint32_t> { snap.drawnSize[0], snap.drawnSize[1] }) } })
        : std::string() },
      { "surfaceImage", snap.surfaceImagePath.empty()
        ? std::string()
        : str::asJsonObject({
            { "path", str::asJson(snap.surfaceImagePath) },
            { "headerBytes", str::asJson(8u) },
            { "noHit", str::asJson(snap.surfaceImageNoHit) },
          }) },
      { "instances", instances },
    };

    std::string text = "{";
    for (const str::JsonMember& member : members) {
      // A member without a value is omitted, as asJsonObject omits it.
      if (member.second.empty()) {
        continue;
      }
      text += text.size() > 1 ? ",\n" : "";
      text += str::asJson(member.first);
      text += ':';
      text += member.second;
    }
    text += "}\n";
    return text;
  }

  // Each answer is announced by one log line of the form "<marker> id=<requestId> {json}".
  // The marker finds the line without parsing JSON, and the id matches it to a request.
  // The JSON holds "file", the path of the file the snapshot was written to,
  // or "error" when the file could not be written.
  // See documentation/AgenticRemix.md.
  constexpr const char* kVisibleSetLogMarker = "RemixVisibleSet";

  inline std::string formatVisibleSetLogLine(uint32_t requestId, const std::string& filePath, const std::string& error) {
    std::string s(kVisibleSetLogMarker);
    s += " id=";
    s += std::to_string(requestId);
    s += ' ';
    s += str::asJsonObject({
      { "file", error.empty() ? str::asJson(filePath) : std::string() },
      { "error", error.empty() ? std::string() : str::asJson(error) },
    });
    return s;
  }

}
