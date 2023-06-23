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
#pragma once
#include "../../util/rc/util_rc_ptr.h"
#include "rtx_option.h"
#include "rtx_common_object.h"

namespace dxvk {
  class DxvkDevice;

  class RtxInitializer : public CommonDeviceObject {
  public:
    explicit RtxInitializer(DxvkDevice* device);

    void onDestroy() override {
      if (m_asyncAssetLoadThread.joinable()) {
        if (!m_assetsLoaded) {
          Logger::warn("Async asset loading thread is running while device is being destroyed! Attempting to join...");
        }
        m_asyncAssetLoadThread.join();
      }
    }

    void initialize();
    void release();

    void waitForShaderPrewarm();

  private:
    bool m_warmupComplete = false;
    bool m_assetsLoaded = false;

    void loadAssets();
    void startPrewarmShaders();

    dxvk::thread m_asyncAssetLoadThread;

    RTX_OPTION("rtx.initializer", bool, asyncAssetLoading, true, "");
    RTX_OPTION("rtx.initializer", bool, asyncShaderPrewarming, true, "");
    RTX_OPTION("rtx.initializer", bool, asyncShaderFinalizing, true, "");
  };

} // namespace dxvk
