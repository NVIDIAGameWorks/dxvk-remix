/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
      Finetuned
    };

    enum class RelaxSettingsPreset : uint32_t {
      Default,
      Finetuned
    };

    const static nrd::Denoiser sDefaultDenoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
    const static nrd::Denoiser sDefaultIndirectDenoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR;
    nrd::LibraryDesc m_libraryDesc;
    nrd::DenoiserDesc m_denoiserDesc = { UINT32_MAX, nrd::Denoiser::MAX_NUM };
    nrd::CommonSettings m_commonSettings;
    nrd::RelaxSettings m_relaxSettings;
    nrd::ReblurSettings m_reblurSettings;
    ReblurSettingsPreset m_reblurSettingsPreset = ReblurSettingsPreset::Finetuned;
    RelaxSettingsPreset m_relaxSettingsPreset = RelaxSettingsPreset::Finetuned;
    nrd::ReferenceSettings m_referenceSettings;
    uint32_t m_adaptiveMinAccumulatedFrameNum = 15;
    float m_adaptiveAccumulationLengthMs = 500.f;
    DenoiserType m_type;

    bool m_resetHistory = true;
    bool m_showAdvancedSettings = false;

    struct SettingsImpactingDenoiserOutput {
      bool calculateDirectionPdf = true;
      float maxDirectHitTContribution = 0.5f;
    };

    SettingsImpactingDenoiserOutput m_groupedSettings;

    struct InternalBlurRadius {
      float maxBlurRadius = 0.0f;
      float diffusePrepassBlurRadius = 0.0f;
      float specularPrepassBlurRadius = 0.0f;
    };

    InternalBlurRadius m_reblurInternalBlurRadius;
    InternalBlurRadius m_relaxInternalBlurRadius;

    NrdSettings() = default;
    ~NrdSettings() = default;

    void initialize(const nrd::LibraryDesc& libraryDesc, const dxvk::Config& config, DenoiserType type);
    void showImguiSettings();

    void updateAdaptiveAccumulation(float frameTimeMs);

  private:
    RTX_OPTION_ENV("rtx", nrd::Denoiser, denoiserMode, sDefaultDenoiser, "DXVK_DENOISER_NRD_MODE", "");
    RTX_OPTION_ENV("rtx", nrd::Denoiser, denoiserIndirectMode, sDefaultIndirectDenoiser, "DXVK_DENOISER_INDIRECT_NRD_MODE", "");
    RTX_OPTION("rtx.denoiser", float, maxDirectHitTContribution, -1.0f, "");
  };
} // namespace dxvk
