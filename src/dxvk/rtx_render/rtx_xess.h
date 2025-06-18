/*
* Copyright (c) 2023-2025, NVIDIA CORPORATION. All rights reserved.
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
   * Simple implementation of Intel XeSS Super Resolution for generic GPU path
   */
  class DxvkXeSS : public RcObject {

  public:

    // Default constructor
    DxvkXeSS();

    // Constructor with device
    DxvkXeSS(DxvkDevice* device);

    ~DxvkXeSS();

    void onDestroy();
    void release();

    bool isEnabled() const { return m_enabled; }
    bool isActive() const { return m_enabled && m_device != nullptr; }
    
    // Methods to enable/disable XeSS when switching upscalers at runtime
    void enable();
    void disable();

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

  private:

    DxvkDevice* m_device = nullptr;
    bool m_enabled = false;
    bool m_initialized = false;

    xess_context_handle_t m_xessContext = nullptr;
    VkExtent3D m_targetExtent = { 0, 0, 0 };
    VkExtent3D m_inputExtent = { 0, 0, 0 };
    XeSSProfile m_currentProfile = XeSSProfile::Balanced;

    // Additional member variables needed by implementation
    xess_context_handle_t m_context = nullptr;
    XeSSProfile m_profile = XeSSProfile::Balanced;
    XeSSProfile m_actualProfile = XeSSProfile::Balanced;
    VkExtent2D m_inputSize = { 0, 0 };
    VkExtent2D m_xessOutputSize = { 0, 0 };
    bool m_recreate = false;
    float m_lastResolutionScale = -1.0f; // Track resolution scale changes for Custom profile

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
  };

} 