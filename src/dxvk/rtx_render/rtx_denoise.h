/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_include.h"
#include "../dxvk_context.h"
#include "rtx_resources.h"

#include "rtx_denoise_type.h"

struct NrdArgs;

namespace dxvk {

  class NRDContext;
  class DxvkDevice;

  class DxvkDenoise : public CommonDeviceObject {
  public:
    /**
       * \brief Input
       *
       * Input resources and parameters to the denoiser.
       */
    struct Input
    {
      // Resources packed in format required by NRD
      // See NRD.hlsli and NRD Readme.md for descriptions
      const Resources::Resource* diffuse_hitT;      // [RGBA16f+], radiance & hit t
      const Resources::Resource* specular_hitT;     // [RGBA16f+], radiance & hit t
      const Resources::Resource* normal_roughness;  // [RGBA8+], world normal & roughness
      const Resources::Resource* linearViewZ;       // [R32f] linear view Z
      const Resources::Resource* motionVector;      // [RGBA16f+ or RG16f+] 
      const Resources::Resource* reference;         // [RGBA16f+], radiance for reference mode
      const Resources::Resource* confidence;        // [R16f+], confidence for history shortening
      const Resources::Resource* disocclusionThresholdMix; // [R8], geometric test relaxation mask
      bool reset;
    };

    /**
     * \brief Output
     *
     * Output denoised resources from the denoiser.
     * Must be preallocated by a caller.
     */
    struct Output
    {
      // Same format as input diffuse and specular resources
      const Resources::Resource* diffuse_hitT;
      const Resources::Resource* specular_hitT;
      const Resources::Resource* reference;         // [RGBA16f+], radiance for reference mode
    };


    DxvkDenoise(DxvkDevice* device, DenoiserType type);
    ~DxvkDenoise();

    void dispatch(
      Rc<DxvkContext> ctx,
      DxvkBarrierSet& barriers,
      const Resources::RaytracingOutput& rtOutput,
      const Input& inputs,
      Output& outputs);

    void releaseResources();
    
    void showImguiSettings();
    NrdArgs getNrdArgs() const;
    bool isReferenceDenoiserEnabled() const;
    void copyNrdSettingsFrom(const DxvkDenoise& refDenoiser);
    const NRDContext& getNrdContext() const;

    void onDestroy();

  private:
    std::unique_ptr<NRDContext> m_nrdContext;

  };
}  // namespace dxvk
