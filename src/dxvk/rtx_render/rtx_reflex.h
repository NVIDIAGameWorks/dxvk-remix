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

#include <cstdint>

#include "rtx_resources.h"
#include "rtx_options.h"

namespace dxvk {

  // A set of various Reflex-related stats. Note that duration values are floating point millisecond times (due to being mostly near 0),
  // wereas all other times are integer microsecond time values.
  struct LatencyStats {
    static constexpr std::size_t statFrames = 64;

    std::uint64_t frameID[statFrames];
    std::uint64_t frameIDMin;
    std::uint64_t frameIDMax;

    std::uint64_t inputSampleCurrentTime;
    std::uint64_t inputSampleTimeMin;
    std::uint64_t inputSampleTimeMax;

    std::uint64_t simCurrentStartTime;
    std::uint64_t simCurrentEndTime;
    float simDuration[statFrames];
    float simDurationMin;
    float simDurationMax;

    std::uint64_t renderSubmitCurrentStartTime;
    std::uint64_t renderSubmitCurrentEndTime;
    float renderSubmitDuration[statFrames];
    float renderSubmitDurationMin;
    float renderSubmitDurationMax;

    std::uint64_t presentCurrentStartTime;
    std::uint64_t presentCurrentEndTime;
    float presentDuration[statFrames];
    float presentDurationMin;
    float presentDurationMax;

    std::uint64_t driverCurrentStartTime;
    std::uint64_t driverCurrentEndTime;
    float driverDuration[statFrames];
    float driverDurationMin;
    float driverDurationMax;

    std::uint64_t osRenderQueueCurrentStartTime;
    std::uint64_t osRenderQueueCurrentEndTime;
    float osRenderQueueDuration[statFrames];
    float osRenderQueueDurationMin;
    float osRenderQueueDurationMax;

    std::uint64_t gpuRenderCurrentStartTime;
    std::uint64_t gpuRenderCurrentEndTime;
    float gpuRenderDuration[statFrames];
    float gpuRenderDurationMin;
    float gpuRenderDurationMax;

    // Note: The difference between renderSubmitCurrentStartTime and gpuRenderCurrentEndTime
    // as this is classified as the game to render latency. It has been observed that the driver
    // end time occasionally ends beyond the GPU render end time, but not sure if this should be
    // counted as latency or not (might just be a measuring artifact).
    float gameToRenderDuration[statFrames];
    float gameToRenderDurationMin;
    float gameToRenderDurationMax;

    // Note: Does not include input sampling time.
    std::uint64_t combinedCurrentTimeMin;
    std::uint64_t combinedCurrentTimeMax;
    // Note: Does not include the "total" GPU render duration, only the various other region durations.
    float combinedDurationMin;
    float combinedDurationMax;
  };

  class RtxReflex : public CommonDeviceObject {
  public:
    explicit RtxReflex(DxvkDevice* device);
    ~RtxReflex();

    /**
      * \brief: Performs a Reflex sleep, should be placed typically right before the present function finishes to block the
      * application from starting its next frame (since input sampling typically happens near the start of an application frame).
      */
    void sleep() const;
    /**
      * \brief: Adds a marker for the start of the simulation. Thread-safe with respect to Reflex.
      */
    void beginSimulation(std::uint64_t frameId) const;
    /**
      * \brief: Adds a marker for the end of the simulation. Thread-safe with respect to Reflex.
      */
    void endSimulation(std::uint64_t frameId) const;
    /**
      * \brief: Adds a marker for the start of render command submission. Thread-safe with respect to Reflex.
      */
    void beginRendering(std::uint64_t frameId) const;
    /**
      * \brief: Adds a marker for the end of render command submission. Thread-safe with respect to Reflex.
      */
    void endRendering(std::uint64_t frameId) const;
    /**
      * \brief: Adds a marker for the start of presentation. Thread-safe with respect to Reflex.
      */
    void beginPresentation(std::uint64_t frameId) const;
    /**
      * \brief: Adds a marker for the end of presentation. Thread-safe with respect to Reflex.
      */
    void endPresentation(std::uint64_t frameId) const;

    void updateMode();

    /**
      * \brief: Gets latency stats from Reflex. Stats are initialized to all zeros when Reflex has not been initialized (due to
      * failing to initialize or due to being disabled), if stats fail to be acquired, or if Reflex has not run for enough
      * frames to generate reliable stats.
      */
    LatencyStats getLatencyStats() const;

    /**
      * \brief: Returns true if Reflex is requested to be enabled. This does not mean Reflex is in use
      * as it may be using the None Reflex mode or was unable to initialize successfully.
      */
    bool reflexEnabled() const { return m_enabled; }
    /**
      * \brief: Returns true if Reflex is enabled and was initialized successfully. Much like the enabled
      * check this does not mean Reflex is in use as it may be using the None Reflex mode.
      */
    bool reflexInitialized() const { return m_initialized; }

  private:
    VkSemaphore m_lowLatencySemaphore;

    // Note: Cached from options determining this state on construction as Reflex currently only has 1
    // chance to be initialized, meaning this state cannot be changed at runtime past the point of construction.
    bool m_enabled = false;
    bool m_initialized = false;

    // Note: Cached mode to track mode changes. Set to None initially as presumably this is the state
    // Reflex starts in by default (low latency mode disabled and boost disabled, the documentation doesn't
    // say this anywhere but it is reasonable to assume).
    ReflexMode m_currentReflexMode = ReflexMode::None;

    void setMarker(std::uint64_t frameId, std::uint32_t marker) const;
  };

}
