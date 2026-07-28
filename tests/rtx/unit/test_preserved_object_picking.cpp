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

#include <vector>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_preserved_object_picking.h"

namespace dxvk {

  Logger Logger::s_instance("test_preserved_object_picking.log");

  namespace {

    void require(bool condition, const char* message) {
      if (!condition) {
        throw DxvkError(message);
      }
    }

    void testRefreshesPickingValuesAndTracksMetadata() {
      constexpr ObjectPickingValue kPreviousPickingValue = 17;
      constexpr ObjectPickingValue kCurrentPickingValue = 83;

      RtInstance firstInstance(1, 0);
      RtInstance secondInstance(2, 1);
      firstInstance.surface.objectPickingValue = kPreviousPickingValue;
      secondInstance.surface.objectPickingValue = kPreviousPickingValue;

      const std::vector<PrimInstance> prims {
        PrimInstance(&firstInstance),
        PrimInstance(),
        PrimInstance(&secondInstance),
      };

      std::vector<RtInstance*> preservedInstances;
      uint32_t metadataTrackCount = 0;

      const bool hasInstance = preserveInstancesWithObjectPicking(
        prims,
        kCurrentPickingValue,
        [&](RtInstance& instance) {
          require(
            instance.surface.objectPickingValue == kCurrentPickingValue,
            "The current picking value must be assigned before preserving an instance.");
          preservedInstances.push_back(&instance);
        },
        [&] {
          require(
            preservedInstances.size() == 2,
            "Metadata must be tracked after every preserved instance is processed.");
          metadataTrackCount++;
        });

      require(hasInstance, "The preserved replacement should report live instances.");
      require(
        firstInstance.surface.objectPickingValue == kCurrentPickingValue,
        "The first preserved instance retained a stale picking value.");
      require(
        secondInstance.surface.objectPickingValue == kCurrentPickingValue,
        "The second preserved instance retained a stale picking value.");
      require(
        preservedInstances.size() == 2,
        "Only live mesh instances should be passed to the preserve callback.");
      require(
        metadataTrackCount == 1,
        "Texture metadata should be registered once per preserved draw.");
    }

    void testSkipsMetadataWithoutInstances() {
      const std::vector<PrimInstance> prims {
        PrimInstance(),
        PrimInstance(),
      };

      uint32_t preserveCount = 0;
      uint32_t metadataTrackCount = 0;

      const bool hasInstance = preserveInstancesWithObjectPicking(
        prims,
        83,
        [&](RtInstance&) {
          preserveCount++;
        },
        [&] {
          metadataTrackCount++;
        });

      require(!hasInstance, "An empty replacement should not report live instances.");
      require(preserveCount == 0, "Null prims should not be preserved.");
      require(
        metadataTrackCount == 0,
        "Texture metadata should not be registered when no mesh instance was preserved.");
    }

  }

}

int main() {
  try {
    dxvk::testRefreshesPickingValuesAndTracksMetadata();
    dxvk::testSkipsMetadataWithoutInstances();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED with exception: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All preserved object-picking tests passed." << std::endl;
  return 0;
}
