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

#include <algorithm>
#include <clocale>
#include <limits>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_visible_set_serialize.h"

namespace dxvk {
  Logger Logger::s_instance("test_visible_set_serialize.log");
}

using namespace dxvk;

static Matrix4 makeIdentity() {
  Matrix4 m;
  m[0] = Vector4(1, 0, 0, 0);
  m[1] = Vector4(0, 1, 0, 0);
  m[2] = Vector4(0, 0, 1, 0);
  m[3] = Vector4(0, 0, 0, 1);
  return m;
}

// Returns a worldToProj matrix that maps (x, y, z, 1) to (x, y, z, z).
// clip.w then equals world z, so a point with z <= 0 is behind the camera.
static Matrix4 makeWEqualsZ() {
  Matrix4 m;
  m[0] = Vector4(1, 0, 0, 0);
  m[1] = Vector4(0, 1, 0, 0);
  m[2] = Vector4(0, 0, 1, 1); // z contributes to both clip.z and clip.w
  m[3] = Vector4(0, 0, 0, 0); // w has no constant term
  return m;
}

static void expect(bool cond, const char* pMsg) {
  if (!cond) {
    throw dxvk::DxvkError(str::format("test_visible_set_serialize FAIL: ", pMsg));
  }
}

static void testProjectAabbToNdcRect() {
  const Matrix4 id = makeIdentity();
  Vector4 rect;
  expect(projectAabbToNdcRect(id, Vector3(-0.2f, -0.1f, 1.f), Vector3(0.4f, 0.3f, 2.f), rect),
         "aabb should project");
  expect(std::abs(rect.x + 0.2f) < 1e-5f && std::abs(rect.y + 0.1f) < 1e-5f &&
         std::abs(rect.z - 0.4f) < 1e-5f && std::abs(rect.w - 0.3f) < 1e-5f, "aabb rect extents");

  // Under makeWEqualsZ, a box in front of the camera is divided by its depth.
  const Matrix4 p = makeWEqualsZ();
  Vector4 rectInFront;
  expect(projectAabbToNdcRect(p, Vector3(1.f, 2.f, 10.f), Vector3(2.f, 4.f, 10.f), rectInFront),
         "aabb in front projects");
  expect(std::abs(rectInFront.x - 0.1f) < 1e-5f && std::abs(rectInFront.w - 0.4f) < 1e-5f, "perspective divide");

  // A box whose 8 corners are all behind the camera (z <= 0 under makeWEqualsZ) returns false.
  Vector4 rectBehind;
  expect(!projectAabbToNdcRect(p, Vector3(-1.f, -1.f, -3.f), Vector3(1.f, 1.f, -1.f), rectBehind),
         "aabb fully behind returns false");

  // This box straddles the camera plane. The 4 edges that cross w = 0 are clipped at kMinClipW,
  // and their projections pass the frame on every side and clamp to +-kMaxRectNdc.
  // The footprint therefore reaches the frame edges instead of being the hull of the in-front corners.
  Vector4 rectMixed;
  expect(projectAabbToNdcRect(p, Vector3(-2.f, -2.f, -1.f), Vector3(4.f, 4.f, 2.f), rectMixed),
         "aabb straddling returns true");
  expect(std::abs(rectMixed.x + kMaxRectNdc) < 1e-5f && std::abs(rectMixed.y + kMaxRectNdc) < 1e-5f &&
         std::abs(rectMixed.z - kMaxRectNdc) < 1e-5f && std::abs(rectMixed.w - kMaxRectNdc) < 1e-5f,
         "straddling rect is clipped+clamped to the NDC bound");

  // On a box this deep, the edge lerp rounds the crossing point's w to exactly zero.
  // The crossing points must still count, so the footprint reaches the frame edges.
  Vector4 rectDeep;
  expect(projectAabbToNdcRect(p, Vector3(-1.f, -1.f, -8192.f), Vector3(1.f, 1.f, 8192.f), rectDeep),
         "deep straddling aabb returns true");
  expect(std::abs(rectDeep.x + kMaxRectNdc) < 1e-5f && std::abs(rectDeep.y + kMaxRectNdc) < 1e-5f &&
         std::abs(rectDeep.z - kMaxRectNdc) < 1e-5f && std::abs(rectDeep.w - kMaxRectNdc) < 1e-5f,
         str::format("deep straddling rect reaches the NDC bound, got [", rectDeep.x, ",", rectDeep.y, ",",
                     rectDeep.z, ",", rectDeep.w, "]").c_str());

  // A garbage transform must report no rect rather than an inverted one.
  // std::min and std::max keep the incumbent when given a NaN.
  // Without the guard, the untouched sentinels would be emitted as [2,2,-2,-2], which is well-formed but wrong.
  // clip.w must stay finite and in front, or the corners are rejected before accumulate() runs.
  // The matrix therefore puts NaN only in the x and y columns and takes w from the last column alone.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  Matrix4 nanProj;
  nanProj[0] = Vector4(nan, 0.f, 0.f, 0.f);
  nanProj[1] = Vector4(0.f, nan, 0.f, 0.f);
  nanProj[2] = Vector4(0.f, 0.f, 1.f, 0.f);
  nanProj[3] = Vector4(0.f, 0.f, 0.f, 1.f);   // clip.w is 1 for every corner
  Vector4 rectNan;
  expect(!projectAabbToNdcRect(nanProj, Vector3(-1.f, -1.f, 1.f), Vector3(1.f, 1.f, 2.f), rectNan),
         "a NaN projection reports no rect rather than an inverted one");
}

static bool nearVec(const Vector3& a, const Vector3& b) {
  return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f && std::abs(a.z - b.z) < 1e-5f;
}

static void testSkinBoundingBox() {
  const AxisAlignedBoundingBox unitBox { Vector3(-1.f, -1.f, -1.f), Vector3(1.f, 1.f, 1.f) };

  // The bound covers the bind box under each bone, so a bone that moves the mesh 10 units widens it to reach there.
  Matrix4 bones[2] = { makeIdentity(), makeIdentity() };
  bones[1][3] = Vector4(10.f, 0.f, 0.f, 1.f);
  const AxisAlignedBoundingBox moved = skinBoundingBox(unitBox, bones, 2);
  expect(moved.isValid(), "two-bone bound is valid");
  expect(nearVec(moved.minPos, Vector3(-1.f, -1.f, -1.f)) && nearVec(moved.maxPos, Vector3(11.f, 1.f, 1.f)),
         "two-bone bound spans both bone placements");

  // A vertex blended half and half between the two bones lands inside the bound.
  const Vector3 corner(1.f, 1.f, 1.f);
  const Vector3 blended = ((bones[0] * Vector4(corner, 1.f)).xyz() + (bones[1] * Vector4(corner, 1.f)).xyz()) * 0.5f;
  expect(blended.x <= moved.maxPos.x && blended.x >= moved.minPos.x, "a blended vertex lies inside the bound");

  // A bone that rotates 90 degrees about z maps x to y and y to -x, so the bound is the rotated box.
  Matrix4 rotate = makeIdentity();
  rotate[0] = Vector4(0.f, 1.f, 0.f, 0.f);
  rotate[1] = Vector4(-1.f, 0.f, 0.f, 0.f);
  const AxisAlignedBoundingBox box { Vector3(0.f, 0.f, 0.f), Vector3(2.f, 1.f, 1.f) };
  const AxisAlignedBoundingBox rotated = skinBoundingBox(box, &rotate, 1);
  expect(nearVec(rotated.minPos, Vector3(-1.f, 0.f, 0.f)) && nearVec(rotated.maxPos, Vector3(0.f, 2.f, 1.f)),
         "a rotating bone rotates the bound");

  // With no bones, or no bind box, there is nothing to bound.
  expect(!skinBoundingBox(unitBox, bones, 0).isValid(), "no bones gives an invalid bound");
  expect(!skinBoundingBox(AxisAlignedBoundingBox {}, bones, 2).isValid(), "an invalid bind box gives an invalid bound");
}

static bool contains(const std::string& hay, const char* pNeedle) {
  return hay.find(pNeedle) != std::string::npos;
}

static void testJsonValues() {
  expect(str::asJson(true) == "true" && str::asJson(false) == "false", "bools are true and false");
  expect(str::asJson(uint32_t(412)) == "412" && str::asJson(uint64_t(18446744073709551615ull)) == "18446744073709551615",
         "integers are written in full");
  expect(str::asJson(uint8_t(75)) == "75", "a uint8_t is written as a number, not a character");
  expect(str::asJson(0.3f) == "0.3" && str::asJson(1.19248805e-10f) == "1.192488e-10" && str::asJson(-512.f) == "-512",
         "a float is written as the shortest text that reads back as the same float");
  // A non-finite value would otherwise be written as a token no JSON parser accepts.
  expect(str::asJson(std::numeric_limits<float>::quiet_NaN()) == "null" &&
         str::asJson(std::numeric_limits<float>::infinity()) == "null", "a non-finite float is null");

  // A path that reached JSON unescaped could end the string early or split the single log line.
  const std::string path = "/RootNode/meshes/mesh_E29C846546B4B268/Gun\"Body\\\n\x01";
  expect(str::asJson(path) == "\"/RootNode/meshes/mesh_E29C846546B4B268/Gun\\\"Body\\\\\\n\\u0001\"",
         "a string escapes quotes, backslashes and every control character");
  expect(str::asJson("Main") == "\"Main\"", "a string literal is written as a string, not as a bool");

  expect(str::asJsonHash(0xA1B2C3D4E5F60718ull) == "\"a1b2c3d4e5f60718\"" && str::asJsonHash(0xABCDull) == "\"000000000000abcd\"",
         "hashes are 16 lowercase hex digits");
  expect(asJson(Vector4(-0.3f, -0.4f, 0.3f, 0.4f)) == "[-0.3,-0.4,0.3,0.4]" &&
         asJson(makeIdentity()) == "[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]", "compact vectors and matrices are flat arrays");
  expect(asJson(Vector2(1.f, 2.f), 0) == "[1, 2]" && asJson(Vector3(1.f, 2.f, 3.f), 2) == "[1, 2, 3]",
         "an indented vector stays on one line, with spaces");
  expect(asJson(makeIdentity(), 1) == "[\n    1, 0, 0, 0,\n    0, 1, 0, 0,\n    0, 0, 1, 0,\n    0, 0, 0, 1\n  ]",
         "an indented matrix is the same flat array with one column per line");
  expect(str::asJson(0.5f, str::kJsonCompact, 6) == "0.500000" && asJson(Vector2(1.f, -2.f), 0, 6) == "[1.000000, -2.000000]",
         "decimalPlaces writes a fixed number of digits after the period");
  expect(str::asJson(std::vector<const char*> { "Main", "Sky" }) == "[\"Main\",\"Sky\"]" &&
         str::asJson(std::vector<uint32_t> { 4, 5 }, 0) == "[4, 5]" && str::asJson(std::vector<uint32_t> {}) == "[]",
         "a vector of basic values is an array");

  // An object is built from members that are already JSON text, and nests by building the inner object first.
  const auto describe = [](int indentLevel) {
    const int nested = indentLevel == str::kJsonCompact ? str::kJsonCompact : indentLevel + 1;
    return str::asJsonObject({
      { "drawCallId", str::asJson(1u) },
      // A member with an empty value is left out.
      { "replacement", std::string() },
      { "hashInfo", str::asJsonObject({ { "meshHash", str::asJsonHash(0xABCDull) } }, nested) },
      { "hidden", str::asJson(true) },
    }, indentLevel);
  };
  expect(describe(str::kJsonCompact) == "{\"drawCallId\":1,\"hashInfo\":{\"meshHash\":\"000000000000abcd\"},\"hidden\":true}",
         "a compact object has no spaces, and leaves out members without a value");
  expect(describe(0) == "{\n  \"drawCallId\": 1,\n  \"hashInfo\": {\n    \"meshHash\": \"000000000000abcd\"\n  },\n  \"hidden\": true\n}",
         "an indented object writes one member per line, and a nested object one level deeper");
  expect(str::asJsonObject({}) == "{}" && str::asJsonObject({}, 0) == "{}", "an object without members is empty braces");
  expect(str::asJsonObject(std::vector<str::JsonMember> { { "Main", "1" } }) == "{\"Main\":1}",
         "an object can be built from a list of members");
}

// Returns a record whose instance stands in for the object RtInstance::asJson writes.
static VisibleSetRecord makeRecord(uint32_t drawCallId, float distance, bool traced, bool inFrustum, uint32_t drawnPixels) {
  VisibleSetRecord r;
  r.instanceJson = str::format("{\"instanceId\":", 40 + drawCallId, ",\"drawCallId\":", drawCallId, "}");
  r.drawCallId = drawCallId;
  r.distance = distance;
  r.traced = traced;
  r.inFrustum = inFrustum;
  r.drawnPixels = drawnPixels;
  return r;
}

static void testSnapshot() {
  VisibleSetSnapshot snap;
  snap.requestId = 4242;
  snap.frameId = 7;
  snap.totalInstances = 3;   // This exceeds instances.size(), as if the cap truncated the list.
  snap.inViewOnly = true;
  snap.detailLevel = 1;
  snap.cameras.push_back({ "Main", Vector3(1.f, 2.f, 3.f) });
  snap.cameras.push_back({ "Sky", Vector3(0.f, 0.f, 0.f) });
  snap.instances.push_back(makeRecord(2, 10.f, true, true, 0));

  const std::string j = serializeVisibleSet(snap);
  expect(contains(j, "{\"schemaVersion\":1,\n\"requestId\":4242,\n\"frameId\":7,\n\"totalInstances\":3,\n\"inViewOnly\":true,\n\"detailLevel\":1,\n"
                     "\"cameras\":{\"Main\":{\"worldPos\":[1,2,3]},\"Sky\":{\"worldPos\":[0,0,0]}},\n"),
         "the header holds the schema version, request, frame, pre-cap count, filter, detail level and cameras");
  expect(!contains(j, "\"center\"") && !contains(j, "\"drawn\""), "center and drawn are omitted without a readback");
  expect(contains(j, "\"instances\":[\n{\"instance\":{\"instanceId\":42,\"drawCallId\":2},\"screen\":{\"distance\":10}}\n]}\n"),
         "each entry holds the instance and its screen data, without inFrustum for an in-view request");
  expect(std::count(j.begin(), j.end(), '\n') == 10, "each of the eight members, the instance and the closing bracket end a line");

  snap.instances.push_back(makeRecord(3, 20.f, true, true, 0));
  const std::string jTwo = serializeVisibleSet(snap);
  expect(contains(jTwo, "}},\n{\"instance\":"), "a second instance starts its own line");
  expect(std::count(jTwo.begin(), jTwo.end(), '\n') == 11, "each further instance adds one line");
  snap.instances.pop_back();

  VisibleSetSnapshot empty;
  expect(contains(serializeVisibleSet(empty), "\"instances\":[]"), "empty instances array");

  snap.inViewOnly = false;
  snap.hasReadback = true;
  snap.drawnSize[0] = 1280;
  snap.drawnSize[1] = 720;
  snap.instances[0].hasScreenRect = true;
  snap.instances[0].screenRectNdc = Vector4(-0.5f, -0.5f, 0.5f, 0.5f);
  snap.instances[0].drawnJson = drawnJson(DrawnCoverage {}, 1280, 720);
  snap.hasCenterHit = true;
  snap.centerHit = snap.instances[0];
  const std::string jr = serializeVisibleSet(snap);
  const char* pEntry = "{\"instance\":{\"instanceId\":42,\"drawCallId\":2},\"screen\":{\"distance\":10,"
                       "\"rectNdc\":[-0.5,-0.5,0.5,0.5],\"inFrustum\":true,\"drawn\":{\"pixels\":0}}}";
  expect(contains(jr, str::format("\"center\":{\"hit\":", pEntry, "},").c_str()), "center carries the hit's entry");
  expect(contains(jr, "\"drawn\":{\"size\":[1280,720]}"), "drawn reports the image size");
  expect(contains(jr, str::format("\"instances\":[\n", pEntry, "\n]}").c_str()),
         "screen holds the rect, the frustum test and the drawn object");

  // A request that saved the surface-index image names the file, and each entry lists its surfaces in it.
  expect(!contains(jr, "surfaceImage") && !contains(jr, "\"surfaces\""), "nothing names an image that was not saved");
  snap.surfaceImagePath = "C:\\logs\\image.bin";
  snap.surfaceImageNoHit = 2097151;
  snap.instances[0].surfaces = { 4, 5 };
  const std::string ji = serializeVisibleSet(snap);
  expect(contains(ji, "\"surfaceImage\":{\"path\":\"C:\\\\logs\\\\image.bin\",\"headerBytes\":8,\"noHit\":2097151}"),
         "the answer names the saved image and the value of a pixel without a hit");
  expect(contains(ji, "\"drawn\":{\"pixels\":0},\"surfaces\":[4,5]}"), "an entry lists its surfaces in the image");
  snap.surfaceImagePath.clear();

  // A center pixel showing no geometry, such as sky, still says the center was read.
  snap.hasCenterHit = false;
  expect(contains(serializeVisibleSet(snap), "\"center\":{},"), "center without a hit is empty");
}

static void testRequest() {
  // The id is the low half of wParam, then come the flags and the detail level.
  const uint64_t wParam = (uint64_t(3) << 40) | (uint64_t(kVisibleSetInViewOnly | kVisibleSetSaveImage) << 32) | 4242u;
  const VisibleSetRequest request = decodeVisibleSetRequest(wParam);
  expect(request.id == 4242u && request.flags == (kVisibleSetInViewOnly | kVisibleSetSaveImage) && request.detailLevel == 3,
         "id, flags and detail level decode");
  // A consumer that only sends an id gets no flags, and leaves the detail level to the option.
  const VisibleSetRequest idOnly = decodeVisibleSetRequest(7u);
  expect(idOnly.id == 7u && idOnly.flags == 0 && idOnly.detailLevel == 0, "an id alone carries no flags or detail level");
  expect(visibleSetDetailLevel(1, 2) == 0 && visibleSetDetailLevel(3, 0) == 2 && visibleSetDetailLevel(0, 1) == 1 &&
         visibleSetDetailLevel(200, 0) == kMaxVisibleSetDetailLevel,
         "a requested level overrides the option, up to the most detailed level");
}

static void testDrawnCoverage() {
  constexpr uint32_t kWidth = 12;
  constexpr uint32_t kHeight = 8;
  constexpr uint32_t kInvalid = 0xFFFFFFFFu;
  std::vector<uint32_t> image(kWidth * kHeight, kInvalid);
  auto at = [&](uint32_t x, uint32_t y) -> uint32_t& { return image[y * kWidth + x]; };

  // Surface 0 fills 3 pixels at the top left, surface 7 is the bottom-right pixel,
  // and surface 3 spans the rest of the second pixel row.
  at(0, 0) = 0; at(1, 0) = 0; at(0, 1) = 0;
  for (uint32_t x = 1; x < kWidth; ++x) {
    at(x, 1) = 3;
  }
  at(11, 7) = 7;

  const std::unordered_map<uint32_t, DrawnCoverage> coverage = reduceDrawnCoverage(image.data(), kWidth, kHeight, kInvalid);
  expect(coverage.size() == 3, "coverage has an entry per surface shown, and pixels without a hit are skipped");
  expect(coverage.at(0).pixels == 3 && coverage.at(7).pixels == 1 && coverage.at(3).pixels == 11,
         "pixels are counted per surface, including surface 0");
  expect(coverage.at(3).minX == 1 && coverage.at(3).maxX == 11 && coverage.at(3).minY == 1 && coverage.at(3).maxY == 1,
         "bounds hold the first and last pixel in each direction");

  // Pixels 0 and 1 of 12 columns and 0 and 1 of 8 rows, with rows running down and NDC y running up.
  const Vector4 rect = drawnRectNdc(coverage.at(0), kWidth, kHeight);
  expect(std::abs(rect.x + 1.f) < 1e-6f && std::abs(rect.z - (2.f / 12.f * 2.f - 1.f)) < 1e-6f &&
         std::abs(rect.w - 1.f) < 1e-6f && std::abs(rect.y - (1.f - 2.f / 8.f * 2.f)) < 1e-6f,
         "the drawn rect spans the pixels in NDC with +y up");

  const std::string drawn = drawnJson(coverage.at(7), kWidth, kHeight);
  expect(drawn.rfind("{\"pixels\":1,\"rectNdc\":[", 0) == 0 && drawn.back() == '}',
         "the drawn object holds the pixel count and the rect");

  // An instance that no pixel shows still gets the drawn object, with nothing drawn.
  expect(drawnJson(DrawnCoverage {}, kWidth, kHeight) == "{\"pixels\":0}", "an undrawn instance reports 0 pixels and no rect");

  // An instance that spans surfaces 0 and 7 gets the pixels of both.
  DrawnCoverage merged;
  mergeDrawnCoverage(merged, coverage.at(0));
  mergeDrawnCoverage(merged, coverage.at(7));
  expect(merged.pixels == 4 && merged.minX == 0 && merged.maxX == 11 && merged.minY == 0 && merged.maxY == 7,
         "merged coverage adds the pixels and spans both bounds");
}

static std::vector<VisibleSetRecord> makeRecords() {
  return {
    makeRecord(1, 40.f, true, false, 500),     // Drawn although its box is out of the frustum.
    makeRecord(2, 30.f, true, true, 0),        // Traced and in the frustum, but hidden behind something.
    makeRecord(3, 10.f, false, true, 0),       // Not traced, such as a sky-cubemap draw.
    makeRecord(4, 20.f, true, false, 0),       // Out of view.
    makeRecord(5, 50.f, true, true, 200),      // Drawn.
  };
}

static uint32_t drawCallIdAt(const std::vector<VisibleSetRecord>& records, size_t i) {
  return records[i].drawCallId;
}

static void testSelectRecords() {
  // Without flags, every record is kept, nearest first.
  std::vector<VisibleSetRecord> all = makeRecords();
  expect(selectVisibleSetRecords(all, 0, 0) == 5 && all.size() == 5, "no flags keeps every record");
  expect(drawCallIdAt(all, 0) == 3 && drawCallIdAt(all, 1) == 4 && drawCallIdAt(all, 4) == 5, "records are nearest first");

  // The filter keeps what the frame drew, and what is traced and in the frustum.
  std::vector<VisibleSetRecord> inView = makeRecords();
  expect(selectVisibleSetRecords(inView, kVisibleSetInViewOnly, 0) == 3, "the filter keeps three records");
  expect(drawCallIdAt(inView, 0) == 2 && drawCallIdAt(inView, 1) == 1 && drawCallIdAt(inView, 2) == 5,
         "the filter drops the untraced and out-of-view records");

  // The cap keeps drawn records first, then traced records in the frustum, and the result is still nearest first.
  std::vector<VisibleSetRecord> capped = makeRecords();
  expect(selectVisibleSetRecords(capped, 0, 3) == 5, "the count is taken before the cap");
  expect(capped.size() == 3 && drawCallIdAt(capped, 0) == 2 && drawCallIdAt(capped, 1) == 1 && drawCallIdAt(capped, 2) == 5,
         "the cap keeps the drawn and in-view records over nearer ones");

  // A record without a finite distance sorts last.
  std::vector<VisibleSetRecord> nan = makeRecords();
  nan[0].distance = std::numeric_limits<float>::quiet_NaN();
  selectVisibleSetRecords(nan, 0, 0);
  expect(drawCallIdAt(nan, 4) == 1, "a non-finite distance sorts last");
}

// Each answer is announced by one log line that names the snapshot's file.
// Everything asserted here is part of the out-of-process wire contract,
// so a change that breaks any of it breaks every consumer.
static void testLogLine() {
  const std::string line = formatVisibleSetLogLine(4242u, "C:\\logs\\remix-visible-set.json", "");
  expect(line == "RemixVisibleSet id=4242 {\"file\":\"C:\\\\logs\\\\remix-visible-set.json\"}",
         "the line holds the marker, the request id and the file's path as JSON");
  // The id must precede the payload, because a consumer matches its request before parsing JSON.
  expect(line.rfind(kVisibleSetLogMarker, 0) == 0 && line.find("id=4242") < line.find('{'), "the id comes before the JSON");

  // A snapshot that could not be written says so, and names no file.
  expect(formatVisibleSetLogLine(7u, "C:\\logs\\x.json", "The snapshot file could not be written.") ==
           "RemixVisibleSet id=7 {\"error\":\"The snapshot file could not be written.\"}",
         "a failed write reports an error instead of a file");

  // A zero id is a legitimate request id and must round-trip like any other.
  expect(contains(formatVisibleSetLogLine(0u, "x", ""), "id=0 "), "zero request id is emitted");
}

// JSON requires '.' as the decimal separator, but printf follows LC_NUMERIC, and games do call setlocale.
// A comma-decimal locale would corrupt the whole payload if str::asJson did not ignore it.
static void testLocaleIndependence() {
  const char* pCurrent = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = pCurrent != nullptr ? pCurrent : "C";
  // Either spelling resolves on Windows.
  // The test is skipped where no comma-decimal locale is installed.
  if (std::setlocale(LC_NUMERIC, "de-DE") == nullptr && std::setlocale(LC_NUMERIC, "German") == nullptr) {
    return;
  }

  VisibleSetSnapshot snap;
  snap.cameras.push_back({ "Main", Vector3(1.25f, 0.f, 0.f) });
  const std::string j = serializeVisibleSet(snap);
  std::setlocale(LC_NUMERIC, saved.c_str());   // The locale is restored before any throw.

  expect(contains(j, "\"worldPos\":[1.25,0,0]"), "decimal separator stays '.' under a comma-decimal locale");
}

int main() {
  try {
    testProjectAabbToNdcRect();
    testSkinBoundingBox();
    testJsonValues();
    testSnapshot();
    testRequest();
    testDrawnCoverage();
    testSelectRecords();
    testLogLine();
    testLocaleIndependence();
  } catch (const dxvk::DxvkError& e) {
    std::cerr << e.message() << std::endl;
    return -1;
  }
  return 0;
}
