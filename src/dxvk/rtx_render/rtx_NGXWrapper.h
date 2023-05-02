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

#include <nvsdk_ngx.h>
#include <memory>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_resources.h"

 // Forward declarations from NGX library.
struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace dxvk
{
  class DxvkDevice;
  class DxvkContext;
  class DxvkCommandList;

  // This is a wrapper around the NGX functionality for DLSS.  It is seperated to provide focus to the calls specific to NGX for code sample purposes.
  class NGXWrapper
  {
  public:
    struct OptimalSettings {
      float sharpness;
      uint32_t optimalRenderSize[2];
      uint32_t minRenderSize[2];
      uint32_t maxRenderSize[2];
    };

    static NGXWrapper* getInstance(DxvkDevice* device);
    static void releaseInstance();

    /** Query optimal DLSS settings for a given resolution and performance/quality profile.
    */
    OptimalSettings queryOptimalSettings(const uint32_t displaySize[2], NVSDK_NGX_PerfQuality_Value perfQuality) const;

    /** Initialize DLSS.
        Throws an exception if unable to initialize.
    */
    void initializeDLSS(
      Rc<DxvkContext> renderContext,
      Rc<DxvkCommandList> cmdList,
      uint32_t maxRenderSize[2],
      uint32_t displayOutSize[2],
      //Texture* pTarget,
      bool isContentHDR,
      bool depthInverted,
      bool autoExposure,
      bool sharpening,
      NVSDK_NGX_PerfQuality_Value perfQuality = NVSDK_NGX_PerfQuality_Value_MaxPerf);

    /** Release DLSS.
    */
    void releaseDLSS();

    /** Checks if DLSS is initialized.
    */
    bool isDLSSInitialized() const { return m_initialized && m_featureDLSS != nullptr; }

    /** Evaluate DLSS.
    */
    bool evaluateDLSS(
      Rc<DxvkCommandList> cmdList,
      Rc<DxvkContext> renderContext,
      const Resources::Resource* pUnresolvedColor,
      const Resources::Resource* pResolvedColor,
      const Resources::Resource* pMotionVectors,
      const Resources::Resource* pDepth,
      const Resources::Resource* pDiffuseAlbedo,
      const Resources::Resource* pSpecularAlbedo,
      const Resources::Resource* pExposure,
      const Resources::Resource* pPosition,
      const Resources::Resource* pNormals,
      const Resources::Resource* pRoughness,
      const Resources::Resource* pBiasCurrentColorMask,
      bool resetAccumulation,
      bool antiGhost,
      float sharpness,
      float preExposure,
      float jitterOffset[2],
      float motionVectorScale[2],
      bool autoExposure) const;

    bool supportsDLSS() const { return m_supportsDLSS; }

  private:
    NGXWrapper(DxvkDevice* device, const wchar_t* logFolder = L".");
    ~NGXWrapper();

    void initializeNGX(const wchar_t* logFolder);
    void shutdownNGX();


    DxvkDevice* m_device;
    bool m_initialized = false;
    bool m_supportsDLSS = false;

    NVSDK_NGX_Parameter* m_parameters = nullptr;
    NVSDK_NGX_Handle* m_featureDLSS = nullptr;

    static NGXWrapper* s_instance;
  };
}
