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
#include "rtx_reflex.h"
#include "dxvk_device.h"

#include "NvLowLatencyVk.h"
#include "pclstats.h"
PCLSTATS_DEFINE();

namespace dxvk {
  // Reflex uses global variables for PCL init, so if a game uses multiple devices, we need to ensure we only do PCL init once.
  static std::atomic<uint32_t> s_initPclRefcount = 0;

  RtxReflex::RtxReflex(DxvkDevice* device) : m_device(device) {
    // Initialize Reflex
    NvLL_VK_Status status = NvLL_VK_Initialize();
    VkSemaphoreCreateInfo semaphoreInfo;
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;
    semaphoreInfo.flags = 0;
    Rc<vk::DeviceFn>vkd = m_device->vkd();
    if (vkd->vkCreateSemaphore(vkd->device(), &semaphoreInfo, nullptr, &m_lowLatencySemaphore) != VK_SUCCESS)
      throw DxvkError("DxvkDevice: Failed to allocate low latency semaphore");
    VkSemaphore* pSemaphore = &m_lowLatencySemaphore;
    status = NvLL_VK_InitLowLatencyDevice(m_device->vkd()->device(), (HANDLE*) pSemaphore);
    updateConstants();
    NVLL_VK_GET_SLEEP_STATUS_PARAMS getParams = {};
    status = NvLL_VK_GetSleepStatus(vkd->device(), &getParams);
    Logger::info(str::format("Reflex enable attempt, mode=", getParams.bLowLatencyMode ? "true" : "false"));

    ++s_initPclRefcount;

    if (s_initPclRefcount == 1) {
      // Initialize PCL Stats
      PCLSTATS_SET_ID_THREAD(-1);
      PCLSTATS_INIT(0);
    }
  }

  RtxReflex::~RtxReflex() {
    Rc<vk::DeviceFn> vkd = m_device->vkd();

    // Close Reflex
    vkd->vkDestroySemaphore(vkd->device(), m_lowLatencySemaphore, nullptr);
    NvLL_VK_DestroyLowLatencyDevice(vkd->device());
    NvLL_VK_Unload();

    --s_initPclRefcount;
    if (s_initPclRefcount == 0) {
      // Close PCL Stats
      PCLSTATS_SHUTDOWN();
    }
  }

  void RtxReflex::updateConstants() {
    static ReflexMode oldMode = ReflexMode::None;
    ReflexMode newMode = RtxOptions::Get()->reflexMode();
    if (newMode == oldMode) {
      return;
    }

    NVLL_VK_SET_SLEEP_MODE_PARAMS sleepParams = {};
    switch (newMode) {
    case ReflexMode::None:
      sleepParams.bLowLatencyMode = false;
      sleepParams.bLowLatencyBoost = false;
      break;
    case ReflexMode::LowLatency:
      sleepParams.bLowLatencyMode = true;
      sleepParams.bLowLatencyBoost = false;
      break;
    case ReflexMode::LowLatencyBoost:
      sleepParams.bLowLatencyMode = true;
      sleepParams.bLowLatencyBoost = true;
      break;
    }

    NvLL_VK_SetSleepMode(m_device->vkd()->device(), &sleepParams);
    oldMode = newMode;
  }

  void RtxReflex::beforePresent(int frameId) {
    if (RtxOptions::Get()->isReflexSupported()) {
      setMarker(frameId, VK_RENDERSUBMIT_END);
    }
  }
  
  void RtxReflex::afterPresent(int frameId) {
    if (RtxOptions::Get()->isReflexSupported()) {
      // Query semaphore
      uint64_t signalValue = 0;
      Rc<vk::DeviceFn> vkd = m_device->vkd();
      vkGetSemaphoreCounterValue(vkd->device(), m_lowLatencySemaphore, &signalValue);
      signalValue += 1;

      // Sleep
      {
        VkSemaphoreWaitInfo semaphoreWaitInfo;
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.flags = 0;
        semaphoreWaitInfo.semaphoreCount = 1;
        semaphoreWaitInfo.pSemaphores = &m_lowLatencySemaphore;
        semaphoreWaitInfo.pValues = &signalValue;
        {
          ScopedCpuProfileZoneN("Reflex_Sleep");
          NvLL_VK_Status status = NvLL_VK_Sleep(vkd->device(), signalValue);
        }
        {
          ScopedCpuProfileZoneN("Reflex_WaitSemaphore");
          vkWaitSemaphores(vkd->device(), &semaphoreWaitInfo, 500000000);
        }
      }

      // Place simulation start marker
      // With DLFG, should put this marker before sleep code so that presents can overlap with the start of the next frame
      setMarker(frameId, VK_SIMULATION_START); // cannot find the counter part of sl::eReflexMarkerSleep

      // Place latency ping marker
      MSG msg;
      const HWND kCurrentThreadId = (HWND) (-1);
      bool sendPclPing = false;
      while (PeekMessage(&msg, kCurrentThreadId, g_PCLStatsWindowMessage, g_PCLStatsWindowMessage, PM_REMOVE)) {
        sendPclPing = true;
      }
      if (sendPclPing) {
        setMarker(frameId, VK_PC_LATENCY_PING);
      }
    }
  }
  
  void RtxReflex::setMarker(int frameId, uint32_t marker) {
    if (!RtxOptions::Get()->isReflexSupported()) {
      return;
    }

    // Set PCL markers
    if (g_PCLStatsIdThread == -1) {
      PCLSTATS_SET_ID_THREAD(::GetCurrentThreadId());
    }
    PCLSTATS_MARKER(marker, frameId);

    // Set reflex markers
    Rc<vk::DeviceFn>vkd = m_device->vkd();
    NvLL_VK_Status status = NVLL_VK_OK;
    NVLL_VK_LATENCY_MARKER_PARAMS params = { };
    params.frameID = frameId;
    params.markerType = static_cast<NVLL_VK_LATENCY_MARKER_TYPE>(marker);
    status = NvLL_VK_SetLatencyMarker(vkd->device(), &params);
    if (status != NVLL_VK_OK) {
      Logger::warn("Failed to set reflex marker");
    }
  }
}
