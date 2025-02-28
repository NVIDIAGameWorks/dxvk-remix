#pragma once

#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx_common.h"
#include "rtx_pass.h"
#include "../ffx_api/ffx_upscale.h"
#include "../ffx_api/ffx_framegeneration.h"

namespace dxvk {

  enum class FSRProfile {
    UltraPerf = 0,
    MaxPerf,
    Balanced,
    MaxQuality,
    Auto,
    FullResolution,
    Invalid
  };

  const char* fsrProfileToString(FSRProfile fsrProfile);

  class DxvkFSR : public CommonDeviceObject, public RtxPass {
  public:
    explicit DxvkFSR(DxvkDevice* device);
    ~DxvkFSR();

    bool supportsFSR() const;

    void setSetting(const uint32_t displaySize[2], const FSRProfile profile, uint32_t outRenderSize[2]);
    FSRProfile getCurrentProfile() const;
    void getInputSize(uint32_t& width, uint32_t& height) const;
    void getOutputSize(uint32_t& width, uint32_t& height) const;

    void dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory = false);

    void showImguiSettings();

    void onDestroy();
    void release();

  protected:
    virtual bool isEnabled() const override;

    static FSRProfile getAutoProfile(uint32_t displayWidth, uint32_t displayHeight);
    void initializeFSR(Rc<DxvkContext> pRenderContext);

    // Options
    FSRProfile mProfile = FSRProfile::Invalid;
    FSRProfile mActualProfile = FSRProfile::Invalid;
    bool mIsHDR = true;
    float mPreExposure = 1.f;
    bool mAutoExposure = false;

    bool mRecreate = true;
    uint32_t mInputSize[2] = {};
    uint32_t mFSROutputSize[2] = {};

    // FSR context and resources
    FfxUpscaleContext mFsrContext;
    FfxFrameGenerationContext mFrameGenContext;
    bool mFrameGenEnabled = false;
  };

} // namespace dxvk 