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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" void testRenderTargetMotionIntegration(void*, void*, void*);

int main() {
  constexpr size_t kCount = 19 * 16;
  float results[kCount][4] = {};
  struct { uint32_t start[3]; uint32_t end[3]; } dispatch = {{0,0,0},{1,1,1}};
  struct { float (*pData)[4]; size_t count; } uniforms = {results,kCount};
  testRenderTargetMotionIntegration(&dispatch, nullptr, &uniforms);
  auto require = [](bool condition, size_t test, const char* message) {
    if (!condition) {
      std::cerr << "Case " << test << ": " << message << '\n';
    }
    return condition;
  };
  const char* names[] = {
    "visible primary", "primary camera-plane crossing", "primary on camera plane",
    "secondary camera-plane crossing", "visible secondary", "primary rejection with RR",
    "secondary rejection with RR", "log RR rejection mask", "forced static motion rejection",
    "two negative nested camera denominators", "conservative visible nested rejection",
    "inactive primary pixel with RR", "moving remote object", "invalid UV chain",
    "visible sky keeps miss sentinel", "rejected secondary sky", "moving and rotated viewing camera", "right-handed viewing camera",
  };
  for (size_t test = 0; test < 18; ++test) {
    const auto* values = results + test * 16;
    const bool valid = test == 0 || test == 4 || test == 12 || test == 14;
    const bool primary = !(test == 3 || test == 4 || test == 6 || test == 15);
    const bool rr = test == 5 || test == 6 || test == 7 || test == 9 || test == 11;
    const bool active = test != 11;
    const bool miss = test == 14 || test == 15;
    const size_t motionSlot = primary ? 1 : 7;
    if (active) {
      for (size_t component = 0; component < 4; ++component) {
        if (!require(std::isfinite(values[motionSlot][component]), test, "non-finite stored motion")) { return -1; }
      }
      if (!require(values[motionSlot][3] == (valid ? (primary && test != 12 ? 1.f : 0.f) : -1.f), test, "wrong validity/static marker")) { return -1; }
      if (!require(values[13][2] == float(valid), test, "NRD previous UV accepts rejected history or rejects visible control")) { return -1; }
      if (!require(values[14][0] == (valid ? 99.f : 7.f), test, "invalid correspondence reused a temporal reservoir")) { return -1; }
      if (!require(values[14][1] == (valid ? 1.f : 0.f), test, "same-pixel fallback was invoked for rejected history")) { return -1; }
      if (!require(values[14][2] == float(valid), test, "invalid correspondence marked similar")) { return -1; }
      if (!require(values[14][3] == float(test == 9 || test == 10 || test == 13), test, "camera entry lost chain invalidity")) { return -1; }
      if ((test == 9 || test == 10) && !require(values[15][2] == 1.f, test, "nested fixture did not reach the positive-final-w case")) { return -1; }
      if (!require(values[11][0] == (valid ? 0.625f : 0.f), test, "wrong reprojection confidence")) { return -1; }
      if (!valid) {
        uint32_t packedPosition;
        std::memcpy(&packedPosition, &values[12][0], sizeof(packedPosition));
        if (!require(packedPosition == 0xffffffffu, test, "gradient position retained stale correspondence")) { return -1; }
      }
      if (!require(values[primary ? 4 : 8][0] == (miss ? -99.f : 10.f), test, "hit distance or miss sentinel changed")) { return -1; }
    } else if (!require(values[1][0] == -99.f, test, "inactive pixel wrote a compacted guide")) { return -1; }
    if (primary) {
      if (!valid) {
        if (!require(values[0][0] + 31 < 0 && values[0][1] + 15 < 0, test, "screen motion did not reject correspondence")) { return -1; }
      }
      if (rr && !valid) {
        const float expectedMask = test == 7 ? 9.2103403719762f : 10000.f;
        if (!require(std::abs(values[5][0] - expectedMask) < 0.001f, test, "RR history-rejection mask missing")) { return -1; }
        if (!require(values[10][0] == values[0][0] && values[10][1] == values[0][1], test, "RR motion differs from rejected pixel motion")) { return -1; }
      } else if (!require(values[5][0] == -99.f, test, "NRD relies on relaxed disocclusion threshold")) { return -1; }
      if (!require(values[7][0] == -99.f, test, "primary wrote secondary guide")) { return -1; }
    } else if (!require(values[0][0] == -99.f && values[5][0] == -99.f && values[1][0] == -99.f, test, "secondary overwrote primary outputs")) { return -1; }
    std::cout << "PASS: " << names[test] << '\n';
  }
  const auto* sample = results + 18 * 16;
  if (!require(std::abs(sample[0][0] - 0.37f) < 0.001f && std::abs(sample[0][1] - 0.19f) < 0.001f && sample[0][2] < 0.001f && sample[0][3] == 1, 18, "POM-adjusted packed sample not preserved")) { return -1; }
  for (size_t i = 1; i <= 2; ++i) {
    if (!require(std::abs(sample[i][0]) < 0.0001f && std::abs(sample[i][1]) < 0.0001f && std::abs(sample[i][2] - 10.f) < 0.0001f && sample[i][3] == 1, 18, "restored UV does not anchor the traced sample")) { return -1; }
  }
  if (!require(sample[3][0] == 0 && sample[3][1] == 0 && sample[3][2] == 0, 18, "restored ray differs from the packed primary sample")) { return -1; }
  std::cout << "PASS: POM packing/restoration and quantized edge UV anchor\n";
  return 0;
}
