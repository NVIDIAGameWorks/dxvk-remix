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

#include <exception>
#include <iostream>

#include "../../../src/dxvk/rtx_render/rtx_option_manager.h"
#include "../../../src/dxvk/rtx_render/rtx_resources.h"
#include "../../test_utils.h"

namespace dxvk {
  Logger Logger::s_instance("test_rtx_feature_options.log");

  namespace {
    void require(bool condition, const char* message) {
      if (!condition) {
        throw DxvkError(message);
      }
    }

    void testAovRequestsPreserveUserOptions() {
      Resources resources(nullptr);
      const auto* userLayer = RtxOptionLayer::getUserLayer();
      for (bool configured : { false, true }) {
        Resources::enableRawAovOutputs.setDeferred(configured, userLayer);
        Resources::enableCoverageAov.setDeferred(configured, userLayer);
        RtxOptionManager::applyPendingValues(nullptr, false);
        resources.setAovRequests(kRawAovNormal, false);
        require(resources.rawAovEnabled(kRawAovNormal)
                && resources.rawAovEnabled(kRawAovWorldPosition) == configured
                && resources.rawAovEnabled(kRawAovTexcoords) == configured,
                "Normal-only requests must not enable unused raw AOVs");
        resources.setAovRequests(kRawAovAll, true);
        require(resources.rawAovEnabled(kRawAovAll) && resources.coverageAovEnabled(), "Bound AOVs must be enabled");
        require(Resources::enableRawAovOutputs() == configured && Resources::enableCoverageAov() == configured,
                "AOV requests must not modify user settings");
        Resources::enableRawAovOutputs.setDeferred(!configured, userLayer);
        Resources::enableCoverageAov.setDeferred(!configured, userLayer);
        RtxOptionManager::applyPendingValues(nullptr, false);
        require(resources.rawAovEnabled(kRawAovAll) && resources.coverageAovEnabled(),
                "Bound AOVs must remain enabled when user settings change");
        resources.setAovRequests(0, true);
        require(resources.rawAovEnabled(kRawAovAll) == !configured && resources.coverageAovEnabled(),
                "Raw and coverage requests must be independent");
        resources.setAovRequests(0, false);
        require(resources.rawAovEnabled(kRawAovAll) == !configured && resources.coverageAovEnabled() == !configured,
                "Releasing AOVs must preserve configured outputs");
      }
      Resources::enableRawAovOutputsObject().disableLayerValue(userLayer);
      Resources::enableCoverageAovObject().disableLayerValue(userLayer);
      RtxOptionManager::applyPendingValues(nullptr, false);
      std::cout << "AOV requests preserve user settings: PASSED" << std::endl;
    }
  }
}

int main() {
  try {
    dxvk::RtxOptionLayer::initializeSystemLayers();
    dxvk::RtxOptionImpl::setInitialized(true);
    dxvk::RtxOptionManager::markOptionsWithCallbacksDirty();
    dxvk::RtxOptionManager::applyPendingValues(nullptr, true);
    dxvk::testAovRequestsPreserveUserOptions();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED: " << error.what() << std::endl;
    return -1;
  }
  return 0;
}
