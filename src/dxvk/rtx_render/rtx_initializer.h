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
      waitForShaderPrewarm();

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

    bool getWarmupComplete() const {
      return m_warmupComplete;
    }

  private:
    bool m_warmupComplete = false;
    bool m_assetsLoaded = false;

    void loadAssets();
    void startPrewarmShaders();

    dxvk::thread m_asyncAssetLoadThread;

    RTX_OPTION_ENV("rtx.initializer", bool, asyncShaderPrewarming, true, "RTX_ASYNC_SHADER_PREWARMING",
                   "When set to true, shader prewarming will be enabled, allowing for Remix to start compiling shaders before their first use.\n"
                   "Typically shaders will only begin compilation on their first use, but this is generally undesirable from a user experience perspective as this often causes stalls or wait times while using the application until all shaders have been used at least once.\n"
                   "By prewarming permutations of potentially required shaders in advance this can be avoided by ensuring all required shaders are compiled before they are used.\n"
                   "Additionally, this prewarming work can often be overlapped with an application's existing startup sequence (e.g. the initial loading screen of a game), allowing Remix's shaders to be ready before they are actually used and avoiding any stalls or wait times.\n"
                   "As such this should generally be set to true and is often used in conjunction with rtx.initializer.asyncShaderFinalizing to avoid Remix blocking on initialization for the prewarming to complete, and rtx.shader.enableAsyncCompilation to avoid shaders from blocking if the application starts using Remix shaders before prewarming is complete.\n"
                   "Since prewarming uses shader permutation however a greater amount of shaders will need to be compiled when this option is enabled compared to the minimal required set (mainly to accomodate various runtime situations and user-facing options that may be altered). Setting this option to false may be useful in specific cases where minimizing this compilation cost is important over user experience (e.g. for automated testing).");
    RTX_OPTION("rtx.initializer", bool, asyncShaderFinalizing, true,
               "When set to true, shader prewarming will be finalized asynchronously rather than Remix's initializer blocking synchronously until it is finished.\n"
               "Do note that this only controls if Remix waits for prewarming to finish or not on startup, if shaders are not finished prewarming by the time they are first used by Remix (e.g. once ray tracing starts) they will still block synchronously until finished even with this option set. See rtx.shader.enableAsyncCompilation for true async shader compilation.\n"
               "This option should usually be set to true and is usually combined with async shader compilation to faciliate a better user experience, but can be to set to false to ensure all shaders are loaded to allow for slightly more deterministic behavior when debugging, or if prewarming all shaders before rendering is desired behavior (at the cost of blocking on startup for a while).\n"
               "Finally, this option only takes effect for the most part when shader prewarming is enabled (rtx.initializer.asyncShaderPrewarming) as otherwise there will be no prewarmed shaders to worry about finalizing.");
  };

} // namespace dxvk
