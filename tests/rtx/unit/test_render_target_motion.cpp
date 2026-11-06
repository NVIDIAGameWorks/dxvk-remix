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
#include <iostream>

// Uses Slang's CPU compute ABI to execute the production GBuffer projection helper.
extern "C" void testRenderTargetMotion(void* pVaryingInput, void* pEntryPointParams, void* pGlobalParams);

int main() {
  constexpr size_t kCaseCount = 21;
  float results[kCaseCount][4] = {};
  struct {
    uint32_t startGroup[3];
    uint32_t endGroup[3];
  } dispatch = {{0, 0, 0}, {1, 1, 1}};
  struct {
    float (*pData)[4];
    size_t count;
  } uniforms = {results, kCaseCount};
  testRenderTargetMotion(&dispatch, nullptr, &uniforms);

  // Independent image-space expectations: anchor + du * deltaU + dv * deltaV.
  // The nested case first projects onto the inner plane, giving (0.4, 0.5, 1.0125),
  // then projects that point through the outer camera and onto the outer plane.
  const float nestedU = 0.5f + 0.4f / 1.0125f - 0.7f;
  const float nestedV = 0.5f - 0.75f / 1.0125f - 0.2f;
  const float expected[kCaseCount][4] = {
    {10, 20, 30, 1},
    {10.1f, 21.8f, 29.3f, 1},
    {10.05f, 20.9f, 29.65f, 1},
    {9.6f, 20, 29.8f, 1},
    {10.5f, 21.8f, 29.5f, 1},
    {12, 23, 34, 1},
    {14, 20, 32, 1},
    {10, 20, 30, 1},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {10 + 4 * nestedU + nestedV, 20 - 6 * nestedV, 30 + 2 * nestedU + 3 * nestedV, 1},
    {10, 20, 30, 1},
    {10, 20, 30, 1},
    {8.2f, 20.1f, 29.3f, 1},
    {16.0f / 30.0f, 2, 0, 1},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
    {2, 0, 1, 1},
    {-1.f / 3.f, 2, -1, 1},
    {0, 0, 0, 0},
    {0, 0, 0, 0},
  };
  const char* names[kCaseCount] = {
    "static", "object translation", "depth-dependent motion", "camera translation",
    "FOV change", "screen translation", "tiled UV without history wrapping", "sky",
    "camera plane", "behind camera", "nested screens", "right-handed projection",
    "camera rotation", "screen rotation", "viewer translation", "infinite input", "division overflow",
    "previous vertex du", "previous vertex dv", "degenerate texture basis", "non-finite texture basis",
  };
  for (size_t test = 0; test < kCaseCount; ++test) {
    for (size_t component = 0; component < 4; ++component) {
      if (!std::isfinite(results[test][component]) ||
          std::abs(results[test][component] - expected[test][component]) > 0.0001f) {
        std::cerr << names[test] << " component " << component << ": expected "
          << expected[test][component] << ", got " << results[test][component] << '\n';
        return -1;
      }
    }
    std::cout << "PASS: " << names[test] << '\n';
  }
  return 0;
}
