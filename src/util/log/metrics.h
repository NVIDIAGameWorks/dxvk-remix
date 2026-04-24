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

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../thread.h"

namespace dxvk {
  enum Metric {
    dxvk_average_frame_time_ms = 0,  // In milliseconds
    dxvk_vid_memory_usage_mb,        // In MB
    dxvk_sys_memory_usage_mb,        // In MB
    dxvk_gpu_idle_time_ms,           // In milliseconds
    dxvk_total_time_ms,              // In milliseconds
    dxvk_frame_count,                // Count of rendered frames

    kCount
  };

  /**
   * \brief Metrics
   * 
   * Metrics for one DLL. Creates a text file and
   * writes all metrics messages to that file.
   */
  class Metrics {
  public:
    struct TestTraceConfig {
      bool enabled = false;
      bool screenshotFrameEnabled = false;
      uint32_t screenshotFrameNum = 0;
      uint32_t terminateAppFrameNum = 0;
    };

    struct TestTraceSample {
      uint32_t frameId = 0;
      float effectiveDeltaMs = 0.0f;
      float realWallDeltaMs = 0.0f;
      float gpuIdleTimeMs = 0.0f;
      uint32_t surfaceCount = 0;
      uint32_t shaderCompileInflightCount = 0;
      uint32_t debugViewMode = 0;
      uint32_t compositeDebugViewMode = 0;
      bool raytracingEnabled = false;
      bool cameraValid = false;
      bool asyncShaderPrewarming = false;
      bool asyncCompilationEnabled = false;
      bool asyncCompilationActive = false;
      bool surfaceBufferAvailable = false;
    };

    Metrics();
    ~Metrics();

    // Will log the rolling average of the past 30 frames when the app closes.
    static void logRollingAverage(Metric metric, const float& value);
    // Will log the passed in value when the app closes.
    static void logFloat(Metric metric, const float& value);
    static void serialize();

    static void configureTestTrace(const TestTraceConfig& config);
    static void setTestTraceScreenshotFrameEnabled(bool enabled);
    static void recordTestTrace(const TestTraceSample& sample);

  private:
    struct TestTraceEvent {
      uint32_t frameId = 0;
      float effectiveDeltaMs = 0.0f;
      float realWallDeltaMs = 0.0f;
      float gpuIdleTimeMs = 0.0f;
      uint32_t screenshotTargetFrame = 0;
      uint32_t terminateTargetFrame = 0;
      uint32_t surfaceCount = 0;
      uint32_t shaderCompileInflightCount = 0;
      uint32_t debugViewMode = 0;
      uint32_t compositeDebugViewMode = 0;
      bool raytracingEnabled = false;
      bool cameraValid = false;
      bool asyncShaderPrewarming = false;
      bool asyncCompilationEnabled = false;
      bool asyncCompilationActive = false;
      bool surfaceBufferAvailable = false;
    };

    inline static const std::string m_metricNames[] = {
      "dxvk_average_frame_time_ms",
      "dxvk_vid_memory_usage_mb",
      "dxvk_sys_memory_usage_mb",
      "dxvk_gpu_idle_time_ms",
      "dxvk_total_time_ms",
      "dxvk_frame_count",
    };

    static constexpr uint32_t kTestTraceMaxEvents = 16;
    static_assert(std::size(m_metricNames) == kCount, "m_metricNames must have an entry for every Metric enum value");

    std::array<float, Metric::kCount> m_data = {};
    TestTraceConfig m_testTraceConfig = {};
    bool m_testTraceFlushed = false;
    uint32_t m_testTraceEventCount = 0;
    std::array<TestTraceEvent, kTestTraceMaxEvents> m_testTraceEvents = {};

    static Metrics s_instance;
    
    dxvk::mutex   m_mutex;
    std::ofstream m_fileStream;
    
    template<typename T>
    void emitMsg(Metric metric, const T& value);

    void emitTestTraceArtifacts();
    bool shouldCaptureTestTrace(uint32_t frameId) const;
    
    static std::string getFileName();
    static std::string getOutputPath(const char* fileName);
  };
}