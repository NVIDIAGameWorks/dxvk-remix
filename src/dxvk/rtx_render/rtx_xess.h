/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_resources.h"
#include "rtx_options.h"
#include "rtx_common_object.h"

#include "../dxvk_format.h"
#include "../dxvk_include.h"
#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"

#include "xess/inc/xess/xess.h"
#include "xess/inc/xess/xess_vk.h"

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief XeSS upscaler
   * 
   * Intel XeSS Super Resolution implementation following RtxPass architecture
   */
  class DxvkXeSS : public RtxPass, public CommonDeviceObject {

  public:

    struct XessOptions {
      friend class DxvkXeSS;
      friend class ImGUI;

      RTX_OPTION("rtx.xess", XeSSPreset, preset, XeSSPreset::Balanced, "Adjusts XeSS scaling factor, trades quality for performance.");
      RTX_OPTION("rtx.xess", float, jitterScale, 1.0f, "Multiplier for XeSS jitter intensity. Values > 1.0 increase jitter, < 1.0 reduce it. Can help reduce aliasing or temporal artifacts.");
      RTX_OPTION("rtx.xess", bool, useOptimizedJitter, true, "Use XeSS-optimized jitter patterns and scaling. When disabled, uses the same jitter as other upscalers.");
      RTX_OPTION("rtx.xess", bool, useRecommendedJitterSequenceLength, true, "Use XeSS 2.1 recommended jitter sequence length calculation based on scaling factor. When disabled, uses the global cameraJitterSequenceLength setting.");
      RTX_OPTION("rtx.xess", float, responsivePixelMaskClampValue, 0.8f, "Maximum value to clamp responsive pixel mask to. XeSS 2.1 default is 0.8 to prevent aliasing artifacts.");
      RTX_OPTION("rtx.xess", float, scalingJitterDamping, 0.6f, "Additional jitter damping factor to reduce swimming artifacts. Lower values = less jitter.");
      RTX_OPTION("rtx.xess", bool, logJitterSequenceLength, false, "Log the current jitter sequence length being used for XeSS. Useful for debugging swimming artifacts.");
      RTX_OPTION("rtx.xess", uint32_t, minJitterSequenceLength, 8, "Minimum jitter sequence length for XeSS, even at low scaling factors.");
    };

    // Constructor with device
    DxvkXeSS(DxvkDevice* device);

    ~DxvkXeSS();

    void initialize(Rc<DxvkContext> renderContext, const VkExtent3D& targetExtent);

    VkExtent3D getInputSize(const VkExtent3D& targetExtent) const;
    
    void dispatch(
      Rc<DxvkContext> renderContext,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory);

    // Public methods needed by RTX context
    void setSetting(const uint32_t displaySize[2], const XeSSPreset preset, uint32_t outRenderSize[2]);
    void getInputSize(uint32_t& width, uint32_t& height) const;
    
    // XeSS 2.1 public helper methods
    uint32_t calcRecommendedJitterSequenceLength() const;
    float calcRecommendedMipBias() const;

  protected:

    // RtxPass interface
    virtual bool isEnabled() const override;
    virtual bool onActivation(Rc<DxvkContext>& ctx) override;
    virtual void onDeactivation() override;

  private:
    float calcUpscaleFactor() const;
    float getUpscaleFactor(xess_quality_settings_t quality) const;
    void createXeSSContext(const VkExtent3D& targetExtent);
    void destroyXeSSContext();
    bool validateXeSSSupport(DxvkDevice* device);
    
    // Additional methods needed by implementation
    void setSetting(const char* name, const char* value);
    void getOutputSize(uint32_t& width, uint32_t& height) const;
    xess_quality_settings_t presetToQuality(XeSSPreset preset) const;
    
    // Static helper to check if XeSS library is available at all
    static bool isXeSSLibraryAvailable();

    // Member variables - organized after methods per style guide
    bool m_initialized;
    xess_context_handle_t m_xessContext;
    VkExtent3D m_targetExtent;
    XeSSPreset m_currentPreset;

    // Additional member variables needed by implementation
    XeSSPreset m_preset;
    XeSSPreset m_actualPreset;
    VkExtent2D m_inputSize;
    VkExtent2D m_xessOutputSize;
    bool m_recreate;
    float m_lastResolutionScale; // Track resolution scale changes for Custom preset
  };

}