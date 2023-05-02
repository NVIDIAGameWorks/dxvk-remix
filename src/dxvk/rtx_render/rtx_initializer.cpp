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

#include "rtx_initializer.h"
#include "rtx_options.h"
#include "../../util/thread.h"
#include "dxvk_context.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_io.h"
#include "dxvk_raytracing.h"

namespace dxvk {
  RtxInitializer::RtxInitializer(const Rc<DxvkDevice>& device)
  : m_device(device) { 
  }

  void RtxInitializer::initialize() {
    ShaderManager::getInstance()->setDevice(m_device.ptr());

#ifdef WITH_RTXIO
    if (RtxIo::enabled()) {
      RtxIo::get().initialize(m_device);
    }
#endif

    // Initialize RTX settings presets
    // Todo: Improve this preset override functionality with a more clear envionrment variable [REMIX-1482]
    if (env::getEnvVar("DXVK_TERMINATE_APP_FRAME") == "" ||
        env::getEnvVar("DXVK_GRAPHICS_PRESET_TYPE") != "0") {
      const DxvkDeviceInfo& deviceInfo = m_device->adapter()->devicePropertiesExt();

      RtxOptions::Get()->updateUpscalerFromDlssPreset();
      RtxOptions::Get()->updateGraphicsPresets(deviceInfo.core.properties.vendorID);
      RtxOptions::Get()->updateRaytraceModePresets(deviceInfo.core.properties.vendorID);
    } else {
      // Default, init to custom unless otherwise specified
      if(RtxOptions::Get()->graphicsPreset() == GraphicsPreset::Auto)
        RtxOptions::Get()->graphicsPresetRef() = GraphicsPreset::Custom;
    }

    // Configure shader manager to understand bindless layouts
    DxvkObjects* pCommon = m_device->getCommon();
    ShaderManager::getInstance()->addGlobalExtraLayout(pCommon->getSceneManager().getBindlessResourceManager().getGlobalBindlessTableLayout(BindlessResourceManager::Buffers));
    ShaderManager::getInstance()->addGlobalExtraLayout(pCommon->getSceneManager().getBindlessResourceManager().getGlobalBindlessTableLayout(BindlessResourceManager::Textures));

    // Kick off shader prewarming
    startPrewarmShaders();

    // Load assets (if any) as early as possible
    if (m_asyncAssetLoading.getValue()) {
      // Async asset loading (USD)
      dxvk::thread asyncAssetLoadThread([this] {
        env::setThreadName("rtx-initialize-assets");
        loadAssets();
      });

      // Note: Detach the thread to allow it to load asynchronously until it is finished.
      asyncAssetLoadThread.detach();
    } else {
      loadAssets();
    }
    pCommon->metaDLSS(); // Lazy allocator triggers init in ctor

    if (!m_asyncShaderFinalizing.getValue()) {
      // Wait for all prewarming to complete before calling "RTX initialized"
      waitForShaderPrewarm();
    }
  }

  void RtxInitializer::release() {
#ifdef WITH_RTXIO
    RtxIo::get().release();
#endif
  }

  void RtxInitializer::loadAssets() {
    Rc<DxvkContext> ctx = m_device->createContext();

    ctx->beginRecording(m_device->createCommandList());

    DxvkObjects* pCommon = m_device->getCommon();
    pCommon->getSceneManager().initialize(ctx);

    ctx->flushCommandList();

    pCommon->getTextureManager().start();
  }

  void RtxInitializer::startPrewarmShaders() {
    // If we want to run without async shader prewarm, then pipelines will be built inline with other GPU work (typically means stutter)
    if (!m_asyncShaderPrewarming.getValue() 
        || m_device->properties().core.properties.vendorID == static_cast<uint32_t>(DxvkGpuVendor::Amd)) // Apparantly causes deadlock on AMD
      return;

    DxvkObjects* pCommon = m_device->getCommon();

    // Prewarm all the shaders we'll need for RT by registering them (per-pass) with the driver
    pCommon->metaPathtracerGbuffer().prewarmShaders(pCommon->pipelineManager());
    pCommon->metaPathtracerIntegrateDirect().prewarmShaders(pCommon->pipelineManager());
    pCommon->metaPathtracerIntegrateIndirect().prewarmShaders(pCommon->pipelineManager());

    // Prewarm the rest of the pipelines that can be done automatically
    AutoShaderPipelinePrewarmer::prewarmComputePipelines(pCommon->pipelineManager());
  }

  void RtxInitializer::waitForShaderPrewarm() {
    if (m_warmupComplete) {
      return;
    }

    // Wait for all shader prewarming to complete
    while (m_device->getCommon()->pipelineManager().isCompilingShaders()) {
      Sleep(1);
    }

    DxvkRaytracingPipeline::releaseFinalizer();

    m_warmupComplete = true;
  }
}