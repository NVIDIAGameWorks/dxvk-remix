/*
* Copyright (c) 2024, AMD Corporation. All rights reserved.
*/
#pragma once

#include "../dxvk_include.h"
#include "rtx_resources.h"
#include "dxvk_image.h"
#include "ffx_api/include/FidelityFX/FSR3.h"

namespace dxvk {
  class DxvkCommandList;
  class DxvkBarrierSet;
  class DxvkContext;

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

  private:
    // FSR Context and State
    FfxFsr3Context m_fsrContext = {};
    bool m_contextInitialized = false;
    
    // Options
    FSRProfile m_profile = FSRProfile::Invalid;
    FSRProfile m_actualProfile = FSRProfile::Invalid;
    bool m_isHDR = true;
    float m_sharpness = 0.5f;
    bool m_enableFrameGeneration = false;

    bool m_recreate = true;
    uint32_t m_inputSize[2] = {};
    uint32_t m_outputSize[2] = {};

    // Resources
    Rc<DxvkImage> m_depthBuffer;
    Rc<DxvkImage> m_motionVectors;
    Rc<DxvkImage> m_exposure;
    Rc<DxvkImage> m_reactiveMap;
    Rc<DxvkImage> m_transparencyAndComposition;
  };
} 