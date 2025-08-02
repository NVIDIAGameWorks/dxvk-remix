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

#include "../dxvk_format.h"
#include "../dxvk_include.h"
#include "../util/rc/util_rc.h"
#include "../util/rc/util_rc_ptr.h"

#include "../../../submodules/xess/inc/xess/xess.h"
#include "../../../submodules/xess/inc/xess/xess_vk.h"

namespace dxvk {

  class DxvkDevice;

  /**
   * \brief XeSS upscaler
   * 
   * Intel XeSS Super Resolution implementation following RtxPass architecture
   */
  class DxvkXeSS : public RtxPass {

  public:

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

    static XeSSProfile getProfile() { return RtxOptions::xessProfile(); }

    // Public methods needed by RTX context
    void setSetting(const uint32_t displaySize[2], const XeSSProfile profile, uint32_t outRenderSize[2]);
    void getInputSize(uint32_t& width, uint32_t& height) const;
    
    // XeSS 2.1 public helper methods
    uint32_t getRecommendedJitterSequenceLength() const { return calculateRecommendedJitterSequenceLength(); }
    float getRecommendedMipBias() const { return calculateRecommendedMipBias(); }
    
    // Public accessor for external components
    bool isXeSSEnabled() const { return isEnabled(); }

  protected:

    // RtxPass interface
    virtual bool isEnabled() const override;
    virtual bool onActivation(Rc<DxvkContext>& ctx) override;
    virtual void onDeactivation() override;
    virtual void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) override;
    virtual void releaseTargetResource() override;

  private:

    // XeSS 2.1 helper functions
    uint32_t calculateRecommendedJitterSequenceLength() const;
    float calculateRecommendedMipBias() const;

    void createXeSSContext(const VkExtent3D& targetExtent);
    void destroyXeSSContext();
    bool validateXeSSSupport(DxvkDevice* device);
    
    // Additional methods needed by implementation
    XeSSProfile getAutoProfile() const;
    XeSSProfile getAutoProfile(uint32_t displayWidth, uint32_t displayHeight);
    XeSSProfile getCurrentProfile() const;
    void setSetting(const char* name, const char* value);
    void getOutputSize(uint32_t& width, uint32_t& height) const;
    xess_quality_settings_t profileToQuality(XeSSProfile profile) const;
    
    // Static helper to check if XeSS library is available at all
    static bool isXeSSLibraryAvailable();

    // Member variables - organized after methods per style guide
    DxvkDevice* m_device;  // Need direct device access for XeSS operations
    bool m_initialized;
    xess_context_handle_t m_xessContext;
    VkExtent3D m_targetExtent;
    VkExtent3D m_inputExtent;
    XeSSProfile m_currentProfile;
    
    bool m_isUsingInternalAutoExposure;

    // Additional member variables needed by implementation
    xess_context_handle_t m_context;
    XeSSProfile m_profile;
    XeSSProfile m_actualProfile;
    VkExtent2D m_inputSize;
    VkExtent2D m_xessOutputSize;
    bool m_recreate;
    float m_lastResolutionScale; // Track resolution scale changes for Custom profile
  };

}