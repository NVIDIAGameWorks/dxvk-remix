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

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_instance_manager.h"

namespace dxvk {

  Logger Logger::s_instance("test_instance_description.log");

  namespace {

    void require(bool condition, const char* message) {
      if (!condition) {
        throw DxvkError(message);
      }
    }

    // Player-model instances with billboards have a geometric mask of 0, and rays hit only their billboards.
    void testBillboardsMakeZeroMaskInstanceTraced() {
      RtInstance instance(1, 0);
      instance.getVkInstance() = {};
      instance.setFrameLastUpdated(1);

      require(!instance.isTraced(), "An instance with a zero mask and no submitted billboards is not traced.");

      instance.markTracedThroughBillboards();
      require(instance.isTraced(), "An instance whose billboards were submitted is traced although its mask is 0.");

      instance.setFrameLastUpdated(1);
      require(instance.isTraced(), "Another update on the same frame must keep the billboard record.");

      instance.setFrameLastUpdated(2);
      require(!instance.isTraced(), "The billboard record must not carry over to the next frame.");

      instance.markTracedThroughBillboards();
      instance.setHidden(true);
      require(!instance.isTraced(), "A hidden instance is not traced.");
    }

    bool contains(const std::string& text, const char* pPart) {
      return text.find(pPart) != std::string::npos;
    }

    // Each detail level adds members to the level below it, grouped into nested objects.
    void testInstanceJsonDetailLevels() {
      RtInstance instance(42, 0);
      instance.getVkInstance() = {};
      instance.setHidden(true);
      instance.billboardIndices = { 4, 5, 6, 7, 8, 9, 10 };

      const std::string summary = instance.asJson(InstanceDetailLevel::Summary, nullptr, str::kJsonCompact);
      require(summary.rfind("{\"instanceId\":42,\"drawCallId\":0,\"hashInfo\":{\"meshHash\":\"", 0) == 0 && summary.back() == '}',
              "The instance is one object that opens with its identity.");
      require(contains(summary, "\"materialHash\":\"") && contains(summary, "\"flags\":{\"hidden\":true,\"traced\":false}"),
              "The summary holds both original hashes and the hidden and traced flags.");
      require(!contains(summary, "\"replacement\""), "The replacement object is left out without an asset replacer.");
      require(!contains(summary, "\"transform\"") && !contains(summary, "\"cameras\"") && !contains(summary, "\"vulkan\""),
              "The summary holds no members of the higher levels.");
      require(!contains(summary, "distance") && !contains(summary, "\"screen\""), "The instance holds no frame data.");
      require(summary.find('\n') == std::string::npos, "A compact instance stays on one line.");

      const std::string standard = instance.asJson(InstanceDetailLevel::Standard, nullptr, str::kJsonCompact);
      require(contains(standard, "\"ageFrames\":") &&
              contains(standard, "\"transform\":{\"worldPosition\":[0,0,0],\"prevWorldPosition\":[") &&
              contains(standard, ",\"world\":[0,0,0,0,"),
              "The standard level adds the age, the current and previous positions and the transform.");
      require(!contains(standard, "\"prevWorld\":"), "The previous transform's matrix is left to the full level.");
      require(contains(standard, "\"material\":{\"alphaBlend\":") && contains(standard, "\"viewModel\":{\"isViewModel\":false}"),
              "The standard level adds the material and view-model groups.");
      require(contains(standard, "\"cameras\":[]"), "An instance no camera drew this frame has an empty camera list.");
      require(contains(standard, "\"isPlayerModel\":false") && contains(standard, "\"categoryFlags\":"),
              "The standard level adds the remaining flags and the categories.");
      require(contains(standard, "\"geometry\":{\"linkedBlas\":false}"),
              "An instance without a BLAS says so, and leaves out the texture hash and the counts.");
      require(!contains(standard, "\"vulkan\"") && !contains(standard, "\"surface\"") && !contains(standard, "isMarkedForGC"),
              "The standard level holds none of the renderer's bookkeeping.");

      const std::string full = instance.asJson(InstanceDetailLevel::Full, nullptr, str::kJsonCompact);
      require(contains(full, "\"vectorIndex\":0,\"frames\":{\"created\":") && contains(full, "\"vulkan\":{\"mask\":0,"),
              "The full level adds the renderer's bookkeeping in its own groups.");
      require(contains(full, "\"instanceMaterialHash\":\"") && contains(full, "\"geometry\":{\"linkedBlas\":false,\"flags\":") &&
              contains(full, ",\"prevWorld\":["),
              "The full level adds members to the groups of the lower levels.");
      require(contains(full, "\"indicesCount\":7,\"firstIndices\":[4,5,6,7,8]"),
              "A long list gives its size and its first five entries.");
      require(contains(full, "\"isMarkedForGC\":false"), "The full level holds the instance's own flags.");
      require(contains(full, "\"surface\":{\"previousIndex\":"),
              "An instance that is not in the frame's surfaces leaves out its surface index.");
    }

    // printDebugInfo logs the full level across lines.
    void testInstanceJsonIndented() {
      RtInstance instance(42, 0);
      instance.getVkInstance() = {};
      const std::string json = instance.asJson(InstanceDetailLevel::Full, nullptr, 0);

      require(json.rfind("{\n  \"instanceId\": 42,\n  \"drawCallId\": 0,\n", 0) == 0, "Each member is on its own indented line.");
      require(contains(json, "\n  \"transform\": {\n    \"worldPosition\": [0, 0, 0],\n") &&
              contains(json, "\n    \"world\": [\n      0, 0, 0, 0,\n      0, 0, 0, 0,\n"),
              "A nested object indents one level deeper, and a matrix writes one column per line.");
      require(contains(json, "\n    ]\n  },\n") && json.substr(json.size() - 2) == "\n}",
              "Each nested value closes at the indentation of the member that opened it.");
    }

  }

}

int main() {
  try {
    dxvk::testBillboardsMakeZeroMaskInstanceTraced();
    dxvk::testInstanceJsonDetailLevels();
    dxvk::testInstanceJsonIndented();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED with exception: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All instance description tests passed." << std::endl;
  return 0;
}
