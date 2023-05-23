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

#include <NRD.h>
#include "../util/config/config.h"
#include "rtx_denoise_type.h"
#include "rtx_option.h"

namespace dxvk {
  class NrdSettings {
  public:
    enum class ReblurSettingsPreset : uint32_t
    {
      Default,
      Finetuned,
      RTXDISample
    };

    enum class RelaxSettingsPreset : uint32_t {
      Default,
      Finetuned,
      FinetunedStable,
      RTXDISample
    };

    const static nrd::Method sDefaultMethod = nrd::Method::RELAX_DIFFUSE_SPECULAR;
    const static nrd::Method sDefaultIndirectMethod = nrd::Method::RELAX_DIFFUSE_SPECULAR;
    nrd::MethodDesc m_methodDesc = { nrd::Method::MAX_NUM, 0, 0 };
    nrd::CommonSettings m_commonSettings;
    nrd::RelaxDiffuseSpecularSettings m_relaxSettings;
    nrd::ReblurSettings m_reblurSettings;
    ReblurSettingsPreset m_reblurSettingsPreset = ReblurSettingsPreset::Finetuned;
    RelaxSettingsPreset m_relaxSettingsPreset = RelaxSettingsPreset::FinetunedStable;
    nrd::ReferenceSettings m_referenceSettings;
    uint32_t m_adaptiveMinAccumulatedFrameNum = 15;
    float m_adaptiveAccumulationLengthMs = 500.f;
    DenoiserType m_type;

    bool m_resetHistory = true;
    bool m_showAdvancedSettings = false;

    struct SettingsImpactingDenoiserOutput {
      bool calculateDirectionPdf = true;
      float timeDeltaBetweenFrames = 0; // 0 == use frame time delta
      float maxDirectHitTContribution = 0.5f;
    };

    SettingsImpactingDenoiserOutput m_groupedSettings;

    struct InternalBlurRadius {
      float blurRadius = 0.0f;
      float diffusePrepassBlurRadius = 0.0f;
      float specularPrepassBlurRadius = 0.0f;
    };

    // Copy from lagecy Reblur settings (which is removed in 5e283dba)
    // Optional specular lobe trimming = A * smoothstep( B, C, roughness )
    // Recommended settings if lobe trimming is needed = { 0.85f, 0.04f, 0.11f }
    struct InternalSpecularLobeTrimmingParameters {
      // [0; 1] - main level  (0 - GGX dominant direction, 1 - full lobe)
      float A = 1.0f;

      // [0; 1] - max trimming if roughness is less than this threshold
      float B = 0.0f;

      // [0; 1] - main level if roughness is greater than this threshold
      float C = 0.0001f;
    } m_specularLobeTrimmingParameters;

    InternalBlurRadius m_reblurInternalBlurRadius;
    InternalBlurRadius m_relaxInternalBlurRadius;

    NrdSettings() = default;
    ~NrdSettings() = default;

    void initialize(const dxvk::Config& config, DenoiserType type);
    void showImguiSettings();

    static float getTimeDeltaBetweenFrames();
    void updateAdaptiveAccumulation(float frameTimeMs);

  private:
    RTX_OPTION_ENV("rtx.denoiser.nrd", float, timeDeltaBetweenFrames, 0.f, "DXVK_DENOISER_NRD_FRAME_TIME_MS", "Frame time to use for denoising. Setting this to 0 will use actual frame time for a given frame. 0 is primarily used for automation to ensure image output determinism.");    
    RTX_OPTION_ENV("rtx", nrd::Method, denoiserMode, sDefaultMethod, "DXVK_DENOISER_NRD_MODE", "");
    RTX_OPTION_ENV("rtx", nrd::Method, denoiserIndirectMode, sDefaultIndirectMethod, "DXVK_DENOISER_INDIRECT_NRD_MODE", "");
    RTX_OPTION("rtx.denoiser", float, maxDirectHitTContribution, -1.0f, "");
  };
} // namespace dxvk
