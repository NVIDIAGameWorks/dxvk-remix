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
#include "rtx_denoise.h"
#include "dxvk_device.h"
#include "rtx_nrd_context.h"
#include "rtx/pass/nrd_args.h"

namespace dxvk {
  
  DxvkDenoise::DxvkDenoise(DxvkDevice* device, DenoiserType type)
  : CommonDeviceObject(device) {
    m_nrdContext = std::make_unique<NRDContext>(device, type);
  }

  DxvkDenoise::~DxvkDenoise() {
  }

  void DxvkDenoise::onDestroy() {
    m_nrdContext->onDestroy();
  }

  void DxvkDenoise::dispatch(
    Rc<DxvkContext> ctx,
    DxvkBarrierSet& barriers,
    const Resources::RaytracingOutput& rtOutput,
    const Input& inputs,
    Output& outputs) 
  {
    const SceneManager& sceneManager = device()->getCommon()->getSceneManager();

    m_nrdContext->dispatch(ctx, barriers, sceneManager, rtOutput, inputs, outputs);
  }

  void DxvkDenoise::copyNrdSettingsFrom(
    const DxvkDenoise& refDenoiser) {
    m_nrdContext->setNrdSettings(refDenoiser.getNrdContext().getNrdSettings());
  }

  const NRDContext& DxvkDenoise::getNrdContext() const {
    return *m_nrdContext.get();
  }

  void DxvkDenoise::showImguiSettings() {
    m_nrdContext->showImguiSettings();
  }

  NrdArgs DxvkDenoise::getNrdArgs() const {
    return m_nrdContext->getNrdArgs();
  }

  bool DxvkDenoise::isReferenceDenoiserEnabled() const {
    return m_nrdContext->isReferenceDenoiserEnabled();
  }
}
