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

#include "rtx_initializer.h"
#include "rtx_options.h"
#include "../../util/thread.h"
#include "dxvk_context.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_render/rtx_texture_manager.h"
#include "rtx_io.h"
#include "dxvk_raytracing.h"
#include "rtx_debug_view.h"

namespace dxvk {
  RtxInitializer::RtxInitializer(DxvkDevice* device)
  : CommonDeviceObject(device) { 
  }

  void RtxInitializer::initialize() {
    ShaderManager::getInstance()->setDevice(m_device);

#ifdef WITH_RTXIO
    if (RtxIo::enabled()) {
      RtxIo::get().initialize(m_device);
    }
    // start async before starting asset loading
    DxvkObjects* pCommon = m_device->getCommon();
    pCommon->getTextureManager().startAsync();
#endif

    // Initialize RTX settings presets
    // Todo: Improve this preset override functionality [REMIX-1482]
    // Currently this logic is very confusing and is intended to skip preset initialization from overriding options, but only results in weird behavior
    // when a termination frame is not set (due to running a test locally in a more open-ended way), or due to how the ultra preset is being used but
    // it is being treated more as a custom preset in practice (except it's not fully custom either due to other preset initialization happening in dxvk_imgui.cpp,
    // though to be fair this logic is not acutally invoked I think unless the Remix menu is opened, but it still shouldn't be split out like this especially if a user
    // is debugging tests and opens the menu only for all the graphics settings to change).
    // Additionally, skipping this logic skips the DLSS preset initialization which is also probably wrong (though the tests will have to explicitly ask for DLSS
    // to be disabled if this is changed).
    if (env::getEnvVar("DXVK_TERMINATE_APP_FRAME") == "" ||
        env::getEnvVar("DXVK_GRAPHICS_PRESET_TYPE") != "0") {
      const DxvkDeviceInfo& deviceInfo = m_device->adapter()->devicePropertiesExt();

      RtxOptions::updateUpscalerFromDlssPreset();
      RtxOptions::updateGraphicsPresets(m_device);
      RtxOptions::updateRaytraceModePresets(deviceInfo.core.properties.vendorID, deviceInfo.khrDeviceDriverProperties.driverID);
    } else {
      // Default, init to custom unless otherwise specified
      if (RtxOptions::graphicsPreset() == GraphicsPreset::Auto) {
        RtxOptions::graphicsPreset.setDeferred(GraphicsPreset::Custom);
      }

      // Need to initialize DLSS-RR settings in test cases.
      // Warning: this will override multiple global options, including any values set by the test workflow.
      if (env::getEnvVar("DXVK_RAY_RECONSTRUCTION") != "0") {
        RtxOptions::updateLightingSetting();
      }
    }

    // Configure shader manager to understand bindless layouts
    ShaderManager::getInstance()->addGlobalExtraLayout(pCommon->getSceneManager().getBindlessResourceManager().getGlobalBindlessTableLayout(BindlessResourceManager::Buffers));
    ShaderManager::getInstance()->addGlobalExtraLayout(pCommon->getSceneManager().getBindlessResourceManager().getGlobalBindlessTableLayout(BindlessResourceManager::Textures));
    ShaderManager::getInstance()->addGlobalExtraLayout(pCommon->getSceneManager().getBindlessResourceManager().getGlobalBindlessTableLayout(BindlessResourceManager::Samplers));

    // Need to promote all of the hardware support Options before prewarming shaders.
    RtxOptionManager::applyPendingValues(m_device, /* forceOnChange */ true);

    // Kick off shader prewarming
    startPrewarmShaders();

    // Load assets (if any) as early as possible
    if (RtxOptions::asyncAssetLoading()) {
      // Async asset loading (USD)
      m_asyncAssetLoadThread = dxvk::thread([this] {
        env::setThreadName("rtx-initialize-assets");
        loadAssets();
      });
    } else {
      loadAssets();
    }
    pCommon->metaDLSS(); // Lazy allocator triggers init in ctor
    pCommon->metaDLFG();

    if (!asyncShaderFinalizing()) {
      // Wait for all prewarming to complete before calling "RTX initialized"
      waitForShaderPrewarm();
    }
  }

  void RtxInitializer::release() {
    if (asyncShaderFinalizing()) {
      // Wait for all prewarming to complete 
      waitForShaderPrewarm();
    }

    ShaderManager::destroyInstance();
#ifdef WITH_RTXIO
    RtxIo::get().release();
#endif
  }

  void RtxInitializer::loadAssets() {
    m_assetsLoaded = false;

    Rc<DxvkContext> ctx = m_device->createContext();

    ctx->beginRecording(m_device->createCommandList());

    DxvkObjects* pCommon = m_device->getCommon();
    pCommon->getSceneManager().initialize(ctx);

    ctx->flushCommandList();

    m_assetsLoaded = true;
  }

  void RtxInitializer::startPrewarmShaders() {
    // If we want to run without shader prewarming, then pipelines will be built inline with other GPU work on first use (typically means
    // long stutters whenever a yet to be compiled pipeline comes into use).
    if (!asyncShaderPrewarming()
        // WAR: Shader prewarming caused a deadlock on AMD in the past so it is forcibly disabled, should re-evaluate this at some point.
        || m_device->properties().core.properties.vendorID == static_cast<uint32_t>(DxvkGpuVendor::Amd)) {
      return;
    }

    DxvkObjects* pCommon = m_device->getCommon();

    // Prewarm all the shaders we'll need for RT by registering them (per-pass) with the driver
    pCommon->metaPathtracerGbuffer().prewarmShaders(pCommon->pipelineManager());
    pCommon->metaPathtracerIntegrateDirect().prewarmShaders(pCommon->pipelineManager());
    pCommon->metaPathtracerIntegrateIndirect().prewarmShaders(pCommon->pipelineManager());

    pCommon->metaDebugView().prewarmShaders(pCommon->pipelineManager());

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