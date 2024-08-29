/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_dlss.h"

namespace dxvk {
  class NGXRayReconstructionContext;
  class DxvkRayReconstruction : public DxvkDLSS {
  public:
    enum class RayReconstructionParticleBufferMode : uint32_t {
      None,
      RayReconstructionUpscaling,
    };

    explicit DxvkRayReconstruction(DxvkDevice* device);

    bool supportsRayReconstruction() const;

    void showRayReconstructionImguiSettings(bool showAdvancedSettings);

    RayReconstructionParticleBufferMode getParticleBufferMode();

    bool useParticleBuffer() {
      return getParticleBufferMode() != RayReconstructionParticleBufferMode::None;
    }

    void dispatch(
      Rc<RtxContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      bool resetHistory = false,
      float frameTimeMilliseconds = 16.0f);

    void release();

    bool useRayReconstruction();

    void setSettings(const uint32_t displaySize[2], const DLSSProfile profile, uint32_t outRenderSize[2]);

    virtual void onDestroy();

    RTX_OPTION("rtx.rayreconstruction", RayReconstructionParticleBufferMode, particleBufferMode,
               RayReconstructionParticleBufferMode::RayReconstructionUpscaling,
               "Use a separate particle buffer to handle particles.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableNRDForTraining, false, "Enable NRD. This option is only for training or debug purpose.\n");
    RTX_OPTION("rtx.rayreconstruction", PathTracerPreset, pathTracerPreset, PathTracerPreset::ReSTIR, "Path tracer preset. The \"ReSTIR Finetuned\" preset is preferred when DLSS-RR is on.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, useSpecularHitDistance, true, "Use specular hit distance to reduce ghosting.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, preserveSettingsInNativeMode, false, "Preserve settings when switched to native mode, otherwise the default preset will be applied.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, combineSpecularAlbedo, true, "Combine primary and secondary specular albedo to improve DLSS-RR reflection quality.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableDetailEnhancement, true, "Enable detail enhancement filter to enhance normal map details.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, demodulateRoughness, true, "Demodulate roughness to enhance roughness details.\n");
    RTX_OPTION("rtx.rayreconstruction", float, upscalerRoughnessDemodulationOffset, 1.5f, "Strength of upscaler roughness demodulation. Only used by DLSS-RR.");
    RTX_OPTION("rtx.rayreconstruction", float, upscalerRoughnessDemodulationMultiplier, 0.15f, "Multiplier of upscaler roughness demodulation to suppress noise. Only used by DLSS-RR.");
    RTX_OPTION("rtx.rayreconstruction", bool, demodulateAttenuation, true, "Demodulate attenuation to reduce ghosting when an object is behind textured translucent objects.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, filterHitT, true, "Filter hit distance to improve specular reflection quality.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, enableDLSSRRSurfaceReplacement, true, "Use DLSS-RR surface replacement. Translucent surfaces with significant refraction are excluded from surface replacement and its surface motion vector will be used.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, preprocessSecondarySignal, true, "Denoise secondary signal before passing to DLSS-RR. This option improves reflection on translucent objects.\n");
    RTX_OPTION("rtx.rayreconstruction", bool, compositeVolumetricLight, true, "Composite volumetric light and then input the result to DLSS-RR, otherwise volumetric light is in a separate layer. Disabling it may introduce flickering artifacts.\n");

  private:
    void initializeRayReconstruction(Rc<DxvkContext> pRenderContext);

    Resources::Resource         m_normals;
    bool                        m_useVirtualNormals = true;
    bool                        m_biasCurrentColorEnabled = true;

    Rc<DxvkBuffer> m_constants;
    std::unique_ptr<NGXRayReconstructionContext> m_rayReconstructionContext;
  };
} // namespace dxvk
