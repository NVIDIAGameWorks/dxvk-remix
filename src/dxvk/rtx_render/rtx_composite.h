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
    void showStochasticAlphaBlendImguiSettings();

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

    RW_RTX_OPTION("rtx", bool, enableStochasticAlphaBlend, true, "Use stochastic alpha blend.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendOpacityThreshold, 0.95f, "Max opacity to use stochastic alpha blend.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendUseNeighborSearch, true, "Get radiance from neighbor opaque pixels.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendSearchTheSameObject, true, "Only use radiance samples from the same object.");
    RTX_OPTION("rtx", int, stochasticAlphaBlendSearchIteration, 6, "Search iterations.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendInitialSearchRadius, 10.0f, "Initial search radius.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendRadiusExpandFactor, 1.6f, "Multiply radius by this factor if cannot find a good neighbor.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendShareNeighbors, true, "Share result with other pixels to accelerate search.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendNormalSimilarity, 0.9f, "Min normal similarity for a valid neighbor.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendDepthDifference, 0.1f, "Max depth difference for a valid neighbor.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendPlanarDifference, 0.2f, "Max planar difference for a valid neighbor.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendUseRadianceVolume, true, "Get radiance from radiance volume.");
    RTX_OPTION("rtx", float, stochasticAlphaBlendRadianceVolumeMultiplier, 1.0, "Radiance volume multiplier.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendEnableFilter, true, "Filter samples to suppress noise.");
    RTX_OPTION("rtx", bool, stochasticAlphaBlendDiscardBlackPixel, false, "Discard black pixels.");

    RTX_OPTION("rtx", bool, usePostFilter, true, "Uses post filter to remove fireflies in the denoised result.");
    RTX_OPTION("rtx", float, postFilterThreshold, 3.0f, "Clamps a pixel when its luminance exceeds x times of the average.");

    RTX_OPTION("rtx", float, noiseMixRatio, 0.2f, "");
    RTX_OPTION("rtx", float, noiseNormalPower, 0.5f, "");
    RTX_OPTION("rtx", float, noiseClampLow, 0.5f, "");
    RTX_OPTION("rtx", float, noiseClampHigh, 2.0f, "");
    RTX_OPTION("rtx", bool,  enableDLSSEnhancement, true, "Enhances lighting details when DLSS is on.");
    RTX_OPTION("rtx", float, dlssEnhancementDirectLightPower, 0.7f, "The overall strength of direct lighting enhancement.");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightPower, 1.0f, "The overall strength of indirect lighting enhancement.");
    RTX_OPTION("rtx", float, dlssEnhancementDirectLightMaxValue, 10.0f, "The maximum strength of direct lighting enhancement.");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightMaxValue, 1.5f, "The maximum strength of indirect lighting enhancement.");
    RTX_OPTION("rtx", float, dlssEnhancementIndirectLightMinRoughness, 0.3f, "The reference roughness in indirect lighting enhancement.");
    RTX_OPTION("rtx", EnhancementMode, dlssEnhancementMode, EnhancementMode::NormalDifference,
      "The enhancement filter type. Valid values: <Normal Difference=1, Laplacian=0>. Normal difference mode provides more normal detail at the cost of some noise. Laplacian mode is less aggressive.");
    RTX_OPTION("rtx", float, pixelHighlightReuseStrength, 0.5, "The specular portion when we reuse last frame's pixel value.");

    Rc<DxvkBuffer> getCompositeConstantsBuffer();
    void createConstantsBuffer();
  };
} // namespace dxvk
