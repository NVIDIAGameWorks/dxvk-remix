/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "dxvk_context.h"
#include "rtx_resources.h"
#include "rtx_types.h"
#include "../dxvk_include.h"
#include "rtx_options.h"

struct RaytraceArgs;

namespace dxvk {
  class Config;
  class SceneManager;
  class DxvkDevice;

  class CompositePass {

  public:
    struct Settings {
      FogState fog;
      bool isNRDPreCompositionDenoiserEnabled;
      bool useUpscaler;
      bool useDLSS;
      bool demodulateRoughness;
      float roughnessDemodulationOffset;
    };

    enum class EnhancementMode{
      Laplacian,
      NormalDifference,
    };

    CompositePass(dxvk::DxvkDevice* device);
    ~CompositePass();

    void dispatch(
      Rc<DxvkCommandList> cmdList,
      Rc<RtxContext> ctx,
      SceneManager& sceneManager,
      const Resources::RaytracingOutput& rtOutput,
      const Settings& setting);

    void showImguiSettings();
    void showDenoiseImguiSettings();

  private:
    dxvk::DxvkDevice* m_device;
    Rc<DxvkBuffer> m_compositeConstants;
    Rc<vk::DeviceFn> m_vkd;

    RTX_OPTION("rtx", bool, enableFog, true, "");
    RTX_OPTION("rtx", float, fogColorScale, 0.25f, "");
    RTX_OPTION("rtx", float, maxFogDistance, 65504.f, "");

    RTX_OPTION("rtx", bool, compositePrimaryDirectDiffuse, true, "");
    RTX_OPTION("rtx", bool, compositePrimaryDirectSpecular, true, "");
    RTX_OPTION("rtx", bool, compositePrimaryIndirectDiffuse, true, "");
    RTX_OPTION("rtx", bool, compositePrimaryIndirectSpecular, true, "");
    RTX_OPTION("rtx", bool, compositeSecondaryCombinedDiffuse, true, "");
    RTX_OPTION("rtx", bool, compositeSecondaryCombinedSpecular, true, "");

    RTX_OPTION("rtx", bool, usePostFilter, true, "");
    RTX_OPTION("rtx", float, postFilterThreshold, 3.0f, "");

    RTX_OPTION("rtx", float, noiseMixRatio, 0.2f, "");
    RTX_OPTION("rtx", float, noiseNormalPower, 0.5f, "");
    RTX_OPTION("rtx", float, noiseClampLow, 0.5f, "");
    RTX_OPTION("rtx", float, noiseClampHigh, 2.0f, "");
    RTX_OPTION("rtx", bool,  enableDLSSEnhancement, true, "");
    RTX_OPTION("rtx", float, dlssEnhancementDirectLightPower, 0.7f, "");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightPower, 1.0f, "");
    RTX_OPTION("rtx", float, dlssEnhancementDirectLightMaxValue, 10.0f, "");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightMaxValue, 1.5f, "");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightMinRoughness, 0.3f, "");
    RTX_OPTION("rtx", EnhancementMode, dlssEnhancementMode, EnhancementMode::NormalDifference, "");
    RTX_OPTION("rtx", float, pixelHighlightReuseStrength, 0.5, "");

    Rc<DxvkBuffer> getCompositeConstantsBuffer();
    void createConstantsBuffer();
  };
} // namespace dxvk
