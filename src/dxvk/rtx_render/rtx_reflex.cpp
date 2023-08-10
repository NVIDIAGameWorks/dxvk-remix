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

#include <limits>
#include <cassert>
#include <Windows.h>

#include "NvLowLatencyVk.h"
#include "pclstats.h"
PCLSTATS_DEFINE();

// Note: Useful for validating that a frame's markers are being placed in reasonable locations and have consistent
// Frame ID numbering. Note though the dynamic markers used by Reflex are somewhat expensive so this should only be
// enabled if debugging is needed. Additionally due to the type of markers in use, they will only take effect when
// REMIX_DEVELOPMENT is defined as well, in addition to TRACY_ENABLE (required to build with Tracy to begin with
// though, so this is implicit).
// #define REFLEX_TRACY_MARKERS

namespace dxvk {

  const char* NvLLStatusToString(NvLL_VK_Status status) {
    // Note: Currently set to match the documentation in the NvLL_VK_Status enum. May need to be updated if more values
    // are added.
    switch (status) {
    case NvLL_VK_Status::NVLL_VK_OK: return "Success. Request is completed.";
    case NvLL_VK_Status::NVLL_VK_ERROR: return "Generic error.";
    case NvLL_VK_Status::NVLL_VK_LIBRARY_NOT_FOUND: return "NvLLVk support library cannot be loaded.";
    case NvLL_VK_Status::NVLL_VK_NO_IMPLEMENTATION: return "Not implemented in current driver installation.";
    case NvLL_VK_Status::NVLL_VK_API_NOT_INITIALIZED: return "NvLL_VK_Initialize has not been called (successfully).";
    case NvLL_VK_Status::NVLL_VK_INVALID_ARGUMENT: return "The argument/parameter value is not valid or NULL.";
    case NvLL_VK_Status::NVLL_VK_INVALID_HANDLE: return "Invalid handle.";
    case NvLL_VK_Status::NVLL_VK_INCOMPATIBLE_STRUCT_VERSION: return "An argument's structure version is not supported.";
    case NvLL_VK_Status::NVLL_VK_INVALID_POINTER: return "An invalid pointer, usually NULL, was passed as a parameter.";
    case NvLL_VK_Status::NVLL_VK_OUT_OF_MEMORY: return "Could not allocate sufficient memory to complete the call.";
    case NvLL_VK_Status::NVLL_VK_API_IN_USE: return "An API is still being called.";
    case NvLL_VK_Status::NVLL_VK_NO_VULKAN: return "No Vulkan support.";
    default: return "Unknown error.";
    }
  }

  // Reflex uses global variables for PCL init, so if a game uses multiple devices, we need to ensure we only do PCL init once.
  static std::atomic<std::uint32_t> s_initPclRefcount = 0;

  RtxReflex::RtxReflex(DxvkDevice* device) : CommonDeviceObject(device) {
    // Initialize PCL stats
    // Note: PCL stats are always desired even if Reflex itself is disabled, so this is done before any checks for Reflex enablement/support.

    m_instanceId = s_initPclRefcount++;

    if (m_instanceId == 0) {
      PCLSTATS_INIT(0);

      // Note: Currently PCLSTATS_INIT does not have error checking for if the creation of a stats window message fails, so we check it here
      // just to catch any potential issues with the API (as passing this 0 to the PeekMessage filters will not function correctly and will
      // cause PCL pings on WM_NULL messages).
      assert(g_PCLStatsWindowMessage != 0);
    } else {
      Logger::warn("Reflex PCL stats multiple initialization detected.");
    }

    // Determine Reflex enablement

    m_enabled = RtxOptions::Get()->isReflexEnabled();

    // Note: Skip initializing Reflex if it is globally disabled at the time of construction.
    if (!reflexEnabled()) {
      return;
    }

    // Initialize Reflex

    NvLL_VK_Status status = NvLL_VK_Initialize();

    if (status != NVLL_VK_OK) {
      Logger::err(str::format("Unable to initialize Reflex: ", NvLLStatusToString(status)));

      return;
    }

    // Initialize the Vulkan Device as a Low Latency device

    status = NvLL_VK_InitLowLatencyDevice(m_device->vkd()->device(), reinterpret_cast<HANDLE*>(&m_lowLatencySemaphore));

    if (status != NVLL_VK_OK) {
      Logger::err(str::format("Failed to initialize the Vulkan device as a Reflex low latency device: ", NvLLStatusToString(status)));

      // Clean up partial initialization on failure

      NvLL_VK_Unload();

      return;
    }

    updateMode();

    // Mark Reflex as initialized

    m_initialized = true;

    Logger::info("Reflex initialized successfully.");
  }

  RtxReflex::~RtxReflex() {
    // Deinitialize PCL stats
    // Note: Deinitialize always even if Reflex was not initialized as PCL stats are initialized always.

    if (--s_initPclRefcount == 0) {
      PCLSTATS_SHUTDOWN();
    }

    // Early out if Reflex was not initialized

    if (!reflexInitialized()) {
      return;
    }

    // Deinitialize Reflex

    NvLL_VK_DestroyLowLatencyDevice(m_device->vkd()->device());
    NvLL_VK_Unload();
  }

  void RtxReflex::sleep() const {
    // Early out if Reflex was not initialized
    // Note: This Reflex sleep code is run even when the Reflex mode is set to None as this is the recommendation from the
    // Reflex team as the API expects the sleep function to be called even in this case. Do note however that this does have
    // a very slight performance cost which is why previously an early out was done here when the Reflex mode was set to None,
    // though it is nothing major though that'd affect the framerate (at least in current testing).

    if (!reflexInitialized()) {
      return;
    }

    Rc<vk::DeviceFn> vkd = m_device->vkd();

    // Query semaphore

    uint64_t signalValue = 0;
    vkd->vkGetSemaphoreCounterValue(vkd->device(), m_lowLatencySemaphore, &signalValue);
    signalValue += 1;

    // Sleep

    VkSemaphoreWaitInfo semaphoreWaitInfo;
    semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    semaphoreWaitInfo.pNext = NULL;
    semaphoreWaitInfo.flags = 0;
    semaphoreWaitInfo.semaphoreCount = 1;
    semaphoreWaitInfo.pSemaphores = &m_lowLatencySemaphore;
    semaphoreWaitInfo.pValues = &signalValue;

    NvLL_VK_Status status = NVLL_VK_OK;

    {
      ScopedCpuProfileZoneN("Reflex_Sleep");
      status = NvLL_VK_Sleep(vkd->device(), signalValue);
    }

    if (status == NVLL_VK_OK) {
      ScopedCpuProfileZoneN("Reflex_WaitSemaphore");
      vkd->vkWaitSemaphores(vkd->device(), &semaphoreWaitInfo, 500000000);
    } else {
      Logger::warn(str::format("Unable to invoke Reflex sleep function: ", NvLLStatusToString(status)));
    }
  }

  void RtxReflex::setLatencyPingThread() const {
    // Early out if this is not the first Reflex instance
    // Note: This is done so that PCL stats are only handled on a single Reflex instance if multiple exist in a thread-safe manner.

    if (m_instanceId != 0) {
      return;
    }

    // Set the PCL stats thread ID to the current thread

    [[maybe_unused]] const auto currentThread = ::GetCurrentThreadId();

    if (currentThread != 0) {
      PCLSTATS_SET_ID_THREAD(currentThread);
    }
  }

  void RtxReflex::latencyPing(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("Latency Ping ", frameId));
#endif

    // Early out if this is not the first Reflex instance

    if (m_instanceId != 0) {
      return;
    }

    // Ensure messages are being peeked on the intended thread

    // Note: Ensure the PCL stats thread ID has been set to begin with.
    assert(g_PCLStatsIdThread != 0);

    [[maybe_unused]] const auto currentThread = ::GetCurrentThreadId();

    if (currentThread != 0) {
      assert(g_PCLStatsIdThread == currentThread);
    }

    // Place latency ping marker when requested

    MSG msg;
    // Note: A HWND of -1 indicates that PeekMessage should only peek messages on the current thread.
    const HWND kCurrentThreadId = reinterpret_cast<HWND>(-1);
    bool sendPclPing = false;

    // Note: This peek will remove messages from the queue so it should be allowed to go over all of them rather than breaking early.
    while (PeekMessage(&msg, kCurrentThreadId, g_PCLStatsWindowMessage, g_PCLStatsWindowMessage, PM_REMOVE)) {
      // Note: PeekMessage even with wMsgFilterMin and wMsgFilterMax set can still return messages outside this range, specifically WM_QUIT,
      // so a check here is required.
      if (PCLSTATS_IS_PING_MSG_ID(msg.message)) {
        sendPclPing = true;
      }
    }

    if (sendPclPing) {
      setMarker(frameId, VK_PC_LATENCY_PING);
    }
  }

  void RtxReflex::beginSimulation(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("Begin Simulation ", frameId));
#endif

    // Place simulation start marker

    setMarker(frameId, VK_SIMULATION_START);
  }

  void RtxReflex::endSimulation(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("End Simulation ", frameId));
#endif

    // Note: Reflex initialization not checked here as setMarker checks internally and needs to be called even when Reflex is not
    // initialized for PCL stats.
    setMarker(frameId, VK_SIMULATION_END);
  }

  void RtxReflex::beginRendering(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("Begin Rendering ", frameId));
#endif

    // Note: Reflex initialization not checked here as setMarker checks internally and needs to be called even when Reflex is not
    // initialized for PCL stats.
    setMarker(frameId, VK_RENDERSUBMIT_START);
  }

  void RtxReflex::endRendering(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("End Rendering ", frameId));
#endif

    // Note: Reflex initialization not checked here as setMarker checks internally and needs to be called even when Reflex is not
    // initialized for PCL stats.
    setMarker(frameId, VK_RENDERSUBMIT_END);
  }

  void RtxReflex::beginPresentation(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("Begin Presentation ", frameId));
#endif

    // Note: Reflex initialization not checked here as setMarker checks internally and needs to be called even when Reflex is not
    // initialized for PCL stats.
    setMarker(frameId, VK_PRESENT_START);
  }

  void RtxReflex::endPresentation(std::uint64_t frameId) const {
#ifdef REFLEX_TRACY_MARKERS
    ScopedCpuProfileZoneDynamic(str::format("End Presentation ", frameId));
#endif

    // Note: Reflex initialization not checked here as setMarker checks internally and needs to be called even when Reflex is not
    // initialized for PCL stats.
    setMarker(frameId, VK_PRESENT_END);
  }
  
  LatencyStats RtxReflex::getLatencyStats() const {
    // Note: Initialize all stats to zero in case Reflex is not initialized or getting latency params fails.
    LatencyStats latencyStats{};

    // Early out if Reflex was not initialized

    if (!reflexInitialized()) {
      return latencyStats;
    }

    // Get Reflex latency information

    NVLL_VK_LATENCY_RESULT_PARAMS latencyResultParams = {};

    const auto status = NvLL_VK_GetLatency(m_device->vkd()->device(), &latencyResultParams);

    if (status != NVLL_VK_OK) {
      // Note: Only logged once to avoid log spam as this function may be called every frame to get stats.
      ONCE(Logger::warn(str::format("Unable to get Reflex latency stats: ", NvLLStatusToString(status))));
    }

    // Transform data into custom latency stats struct
    // Note: This transformation is done primairly to allow for easier graphing of the data compared to its
    // standard memory layout.

    constexpr float microsecondsPerMillisecond{ 1000.0f };

    std::uint64_t frameIDMin = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t frameIDMax = 0;
    std::uint64_t inputSampleTimeMin = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t inputSampleTimeMax = 0;
    float simDurationMin = std::numeric_limits<float>::max();
    float simDurationMax = 0.0f;
    float renderSubmitDurationMin = std::numeric_limits<float>::max();
    float renderSubmitDurationMax = 0.0f;
    float presentDurationMin = std::numeric_limits<float>::max();
    float presentDurationMax = 0.0f;
    float driverDurationMin = std::numeric_limits<float>::max();
    float driverDurationMax = 0.0f;
    float osRenderQueueDurationMin = std::numeric_limits<float>::max();
    float osRenderQueueDurationMax = 0.0f;
    float gpuRenderDurationMin = std::numeric_limits<float>::max();
    float gpuRenderDurationMax = 0.0f;
    float gameToRenderDurationMin = std::numeric_limits<float>::max();
    float gameToRenderDurationMax = 0.0f;

    for (unsigned int i = 0; i < LatencyStats::statFrames; ++i) {
      const auto& currentFrameReport = latencyResultParams.frameReport[i];
      
      // Note: Guard duration calculation against out of order times (even though this shouldn't be possible in
      // normal operation).
      const auto simDuration =
        currentFrameReport.simEndTime >= currentFrameReport.simStartTime ?
        static_cast<float>(currentFrameReport.simEndTime - currentFrameReport.simStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto renderSubmitDuration =
        currentFrameReport.renderSubmitEndTime >= currentFrameReport.renderSubmitStartTime ?
        static_cast<float>(currentFrameReport.renderSubmitEndTime - currentFrameReport.renderSubmitStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto presentDuration =
        currentFrameReport.presentEndTime >= currentFrameReport.presentStartTime ?
        static_cast<float>(currentFrameReport.presentEndTime - currentFrameReport.presentStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto driverDuration =
        currentFrameReport.driverEndTime >= currentFrameReport.driverStartTime ?
        static_cast<float>(currentFrameReport.driverEndTime - currentFrameReport.driverStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto osRenderQueueDuration =
        currentFrameReport.osRenderQueueEndTime >= currentFrameReport.osRenderQueueStartTime ?
        static_cast<float>(currentFrameReport.osRenderQueueEndTime - currentFrameReport.osRenderQueueStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto gpuRenderDuration =
        currentFrameReport.gpuRenderEndTime >= currentFrameReport.gpuRenderStartTime ?
        static_cast<float>(currentFrameReport.gpuRenderEndTime - currentFrameReport.gpuRenderStartTime) / microsecondsPerMillisecond :
        0.0f;
      const auto gameToRenderDuration =
        currentFrameReport.gpuRenderEndTime >= currentFrameReport.simStartTime ?
        static_cast<float>(currentFrameReport.gpuRenderEndTime - currentFrameReport.simStartTime) / microsecondsPerMillisecond :
        0.0f;

      latencyStats.frameID[i] = currentFrameReport.frameID;
      latencyStats.simDuration[i] = simDuration;
      latencyStats.renderSubmitDuration[i] = renderSubmitDuration;
      latencyStats.presentDuration[i] = presentDuration;
      latencyStats.driverDuration[i] = driverDuration;
      latencyStats.osRenderQueueDuration[i] = osRenderQueueDuration;
      latencyStats.gpuRenderDuration[i] = gpuRenderDuration;
      latencyStats.gameToRenderDuration[i] = gameToRenderDuration;

      frameIDMin = std::min(frameIDMin, currentFrameReport.frameID);
      frameIDMax = std::max(frameIDMax, currentFrameReport.frameID);
      inputSampleTimeMin = std::min(inputSampleTimeMin, currentFrameReport.inputSampleTime);
      inputSampleTimeMax = std::max(inputSampleTimeMax, currentFrameReport.inputSampleTime);
      simDurationMin = std::min(simDurationMin, simDuration);
      simDurationMax = std::max(simDurationMax, simDuration);
      renderSubmitDurationMin = std::min(renderSubmitDurationMin, renderSubmitDuration);
      renderSubmitDurationMax = std::max(renderSubmitDurationMax, renderSubmitDuration);
      presentDurationMin = std::min(presentDurationMin, presentDuration);
      presentDurationMax = std::max(presentDurationMax, presentDuration);
      driverDurationMin = std::min(driverDurationMin, driverDuration);
      driverDurationMax = std::max(driverDurationMax, driverDuration);
      osRenderQueueDurationMin = std::min(osRenderQueueDurationMin, osRenderQueueDuration);
      osRenderQueueDurationMax = std::max(osRenderQueueDurationMax, osRenderQueueDuration);
      gpuRenderDurationMin = std::min(gpuRenderDurationMin, gpuRenderDuration);
      gpuRenderDurationMax = std::max(gpuRenderDurationMax, gpuRenderDuration);
      gameToRenderDurationMin = std::min(gameToRenderDurationMin, gameToRenderDuration);
      gameToRenderDurationMax = std::max(gameToRenderDurationMax, gameToRenderDuration);
    }

    // Note: The last element of the frame report array will be the most recent frame's latency information.
    const auto& currentFrameReport = latencyResultParams.frameReport[63];

    latencyStats.frameIDMin = frameIDMin;
    latencyStats.frameIDMax = frameIDMax;
    latencyStats.inputSampleCurrentTime = currentFrameReport.inputSampleTime;
    latencyStats.inputSampleTimeMin = inputSampleTimeMin;
    latencyStats.inputSampleTimeMax = inputSampleTimeMax;
    latencyStats.simCurrentStartTime = currentFrameReport.simStartTime;
    latencyStats.simCurrentEndTime = currentFrameReport.simEndTime;
    latencyStats.simDurationMin = simDurationMin;
    latencyStats.simDurationMax = simDurationMax;
    latencyStats.renderSubmitCurrentStartTime = currentFrameReport.renderSubmitStartTime;
    latencyStats.renderSubmitCurrentEndTime = currentFrameReport.renderSubmitEndTime;
    latencyStats.renderSubmitDurationMin = renderSubmitDurationMin;
    latencyStats.renderSubmitDurationMax = renderSubmitDurationMax;
    latencyStats.presentCurrentStartTime = currentFrameReport.presentStartTime;
    latencyStats.presentCurrentEndTime = currentFrameReport.presentEndTime;
    latencyStats.presentDurationMin = presentDurationMin;
    latencyStats.presentDurationMax = presentDurationMax;
    latencyStats.driverCurrentStartTime = currentFrameReport.driverStartTime;
    latencyStats.driverCurrentEndTime = currentFrameReport.driverEndTime;
    latencyStats.driverDurationMin = driverDurationMin;
    latencyStats.driverDurationMax = driverDurationMax;
    latencyStats.osRenderQueueCurrentStartTime = currentFrameReport.osRenderQueueStartTime;
    latencyStats.osRenderQueueCurrentEndTime = currentFrameReport.osRenderQueueEndTime;
    latencyStats.osRenderQueueDurationMin = osRenderQueueDurationMin;
    latencyStats.osRenderQueueDurationMax = osRenderQueueDurationMax;
    latencyStats.gpuRenderCurrentStartTime = currentFrameReport.gpuRenderStartTime;
    latencyStats.gpuRenderCurrentEndTime = currentFrameReport.gpuRenderEndTime;
    latencyStats.gpuRenderDurationMin = gpuRenderDurationMin;
    latencyStats.gpuRenderDurationMax = gpuRenderDurationMax;
    latencyStats.gameToRenderDurationMin = gameToRenderDurationMin;
    latencyStats.gameToRenderDurationMax = gameToRenderDurationMax;
    latencyStats.combinedCurrentTimeMin = std::min({
      latencyStats.simCurrentStartTime, latencyStats.simCurrentEndTime,
      latencyStats.renderSubmitCurrentStartTime, latencyStats.renderSubmitCurrentEndTime,
      latencyStats.presentCurrentStartTime, latencyStats.presentCurrentEndTime,
      latencyStats.driverCurrentStartTime, latencyStats.driverCurrentEndTime,
      latencyStats.osRenderQueueCurrentStartTime, latencyStats.osRenderQueueCurrentEndTime,
      latencyStats.gpuRenderCurrentStartTime, latencyStats.gpuRenderCurrentEndTime,
    });
    latencyStats.combinedCurrentTimeMax = std::max({
      latencyStats.simCurrentStartTime, latencyStats.simCurrentEndTime,
      latencyStats.renderSubmitCurrentStartTime, latencyStats.renderSubmitCurrentEndTime,
      latencyStats.presentCurrentStartTime, latencyStats.presentCurrentEndTime,
      latencyStats.driverCurrentStartTime, latencyStats.driverCurrentEndTime,
      latencyStats.osRenderQueueCurrentStartTime, latencyStats.osRenderQueueCurrentEndTime,
      latencyStats.gpuRenderCurrentStartTime, latencyStats.gpuRenderCurrentEndTime,
    });
    latencyStats.combinedDurationMin = std::min({
      simDurationMin, simDurationMax,
      renderSubmitDurationMin, renderSubmitDurationMax,
      presentDurationMin, presentDurationMax,
      driverDurationMin, driverDurationMax,
      osRenderQueueDurationMin, osRenderQueueDurationMax,
      gpuRenderDurationMin, osRenderQueueDurationMax,
    });
    latencyStats.combinedDurationMax = std::max({
      simDurationMin, simDurationMax,
      renderSubmitDurationMin, renderSubmitDurationMax,
      presentDurationMin, presentDurationMax,
      driverDurationMin, driverDurationMax,
      osRenderQueueDurationMin, osRenderQueueDurationMax,
      gpuRenderDurationMin, osRenderQueueDurationMax,
    });

    return latencyStats;
  }

  void RtxReflex::updateMode() {
    if (!reflexInitialized()) {
      return;
    }

    // Check the current Reflex Mode

    const auto newMode = RtxOptions::Get()->reflexMode();

    if (newMode == m_currentReflexMode) {
      return;
    }

    // Update Reflex's sleep mode based on the specified mode

    NVLL_VK_SET_SLEEP_MODE_PARAMS sleepParams = {};

    // Note: No framerate limit.
    sleepParams.minimumIntervalUs = 0;

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

    const auto status = NvLL_VK_SetSleepMode(m_device->vkd()->device(), &sleepParams);

    if (status != NVLL_VK_OK) {
      Logger::warn(str::format("Unable to set Reflex sleep mode: ", NvLLStatusToString(status)));

      // Note: A return early here could be done to avoid setting the current Reflex mode so that it can be attempted to be set
      // again the next time this function is called. This may not be a good idea however if the mode refuses to be set
      // as it will just attempt to be set every frame which may be wasteful, instead just log a warning and allow the user to
      // try to set the mode to something else.
    }

    m_currentReflexMode = newMode;
  }

  void RtxReflex::setMarker(std::uint64_t frameId, std::uint32_t marker) const {
    // Set PCL markers

    PCLSTATS_MARKER(marker, frameId);

    // Early out if Reflex was not initialized

    if (!reflexInitialized()) {
      return;
    }

    Rc<vk::DeviceFn> vkd = m_device->vkd();

    // Set reflex markers

    NVLL_VK_LATENCY_MARKER_PARAMS params = {};
    params.frameID = frameId;
    params.markerType = static_cast<NVLL_VK_LATENCY_MARKER_TYPE>(marker);

    const auto status = NvLL_VK_SetLatencyMarker(vkd->device(), &params);

    if (status != NVLL_VK_OK) {
      Logger::warn(str::format("Unable to set Reflex marker: ", NvLLStatusToString(status)));
    }
  }

}
