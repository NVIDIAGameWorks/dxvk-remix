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
#include "metrics.h"

#include "../util_env.h"
#include "../util_string.h"

#include <algorithm>

namespace dxvk {
  Metrics::Metrics() {
    auto path = getFileName();

    if (!path.empty())
      m_fileStream = std::ofstream(str::tows(path.c_str()).c_str());

    const std::string metricsFolder = env::getEnvVar("DXVK_METRICS_PATH");
    if (!metricsFolder.empty() && metricsFolder != "none") {
      ShaderCompileScope::setProgressPath(getOutputPath("shader_compile_progress.txt"));
    }
  }
  
  Metrics::~Metrics() { }

  void Metrics::logAverage(Metric metric, const float& value) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    const uint32_t sampleCount = ++s_instance.m_sampleCounts[metric];
    s_instance.m_data[metric] += (value - s_instance.m_data[metric]) / static_cast<float>(sampleCount);
  }

  void Metrics::logFloat(Metric metric, const float& value) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    s_instance.m_data[metric] = value;
    s_instance.m_sampleCounts[metric] = 1;
  }

  void Metrics::configureTestTrace(const TestTraceConfig& config) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    s_instance.m_testTraceConfig = config;
    s_instance.m_testTraceFlushed = false;
    s_instance.m_testTraceEventCount = 0;
    s_instance.m_testTraceEvents = {};
  }

  void Metrics::setTestTraceScreenshotFrameEnabled(bool enabled) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    s_instance.m_testTraceConfig.screenshotFrameEnabled = enabled;
  }

  void Metrics::recordTestTrace(const TestTraceSample& sample) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);

    if (!s_instance.shouldCaptureTestTrace(sample.frameId))
      return;

    if (s_instance.m_testTraceEventCount >= kTestTraceMaxEvents)
      return;

    TestTraceEvent& event = s_instance.m_testTraceEvents[s_instance.m_testTraceEventCount++];
    event.frameId = sample.frameId;
    event.effectiveDeltaMs = sample.effectiveDeltaMs;
    event.realWallDeltaMs = sample.realWallDeltaMs;
    event.gpuIdleTimeMs = sample.gpuIdleTimeMs;
    event.screenshotTargetFrame = s_instance.m_testTraceConfig.screenshotFrameNum;
    event.terminateTargetFrame = s_instance.m_testTraceConfig.terminateAppFrameNum;
    event.surfaceCount = sample.surfaceCount;
    event.shaderCompileInflightCount = sample.shaderCompileInflightCount;
    event.debugViewMode = sample.debugViewMode;
    event.compositeDebugViewMode = sample.compositeDebugViewMode;
    event.raytracingEnabled = sample.raytracingEnabled;
    event.cameraValid = sample.cameraValid;
    event.asyncShaderPrewarming = sample.asyncShaderPrewarming;
    event.asyncCompilationEnabled = sample.asyncCompilationEnabled;
    event.asyncCompilationActive = sample.asyncCompilationActive;
    event.surfaceBufferAvailable = sample.surfaceBufferAvailable;
  }
  
  void Metrics::serialize() {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);

    s_instance.m_data[Metric::dxvk_shader_compile_time_ms] = static_cast<float>(ShaderCompileScope::totalMs());
    s_instance.m_sampleCounts[Metric::dxvk_shader_compile_time_ms] = 1;

    for (uint32_t i = 0; i < Metric::kCount; i++) {
      if (s_instance.m_sampleCounts[i]) {
        s_instance.emitMsg((Metric)i, s_instance.m_data[i]);
      }
    }

    s_instance.emitTestTraceArtifacts();
  }

  template<typename T>
  void Metrics::emitMsg(Metric metric, const T& value) {
    if (m_fileStream)
      m_fileStream << m_metricNames[(uint32_t)metric] << " " << value << std::endl;
  }

  void Metrics::emitTestTraceArtifacts() {
    if (!m_testTraceConfig.enabled || m_testTraceFlushed)
      return;

    const TestTraceEvent* captureEvent = nullptr;
    float effectiveDeltaMin = 0.0f;
    float effectiveDeltaMax = 0.0f;
    float realWallDeltaMin = 0.0f;
    float realWallDeltaMax = 0.0f;
    float gpuIdleMin = 0.0f;
    float gpuIdleMax = 0.0f;

    if (m_testTraceEventCount > 0) {
      captureEvent = &m_testTraceEvents[m_testTraceEventCount - 1];
      effectiveDeltaMin = m_testTraceEvents[0].effectiveDeltaMs;
      effectiveDeltaMax = m_testTraceEvents[0].effectiveDeltaMs;
      realWallDeltaMin = m_testTraceEvents[0].realWallDeltaMs;
      realWallDeltaMax = m_testTraceEvents[0].realWallDeltaMs;
      gpuIdleMin = m_testTraceEvents[0].gpuIdleTimeMs;
      gpuIdleMax = m_testTraceEvents[0].gpuIdleTimeMs;

      for (uint32_t i = 0; i < m_testTraceEventCount; i++) {
        const TestTraceEvent& event = m_testTraceEvents[i];
        if (event.frameId == m_testTraceConfig.screenshotFrameNum)
          captureEvent = &event;

        effectiveDeltaMin = std::min(effectiveDeltaMin, event.effectiveDeltaMs);
        effectiveDeltaMax = std::max(effectiveDeltaMax, event.effectiveDeltaMs);
        realWallDeltaMin = std::min(realWallDeltaMin, event.realWallDeltaMs);
        realWallDeltaMax = std::max(realWallDeltaMax, event.realWallDeltaMs);
        gpuIdleMin = std::min(gpuIdleMin, event.gpuIdleTimeMs);
        gpuIdleMax = std::max(gpuIdleMax, event.gpuIdleTimeMs);
      }
    }

    bool traceFileWritten = false;
    const std::string tracePath = getOutputPath("frame_trace.jsonl");
    if (!tracePath.empty()) {
      std::ofstream out(str::tows(tracePath.c_str()).c_str(), std::ios::out | std::ios::trunc);
      if (out.is_open()) {
        for (uint32_t i = 0; i < m_testTraceEventCount; i++) {
          const TestTraceEvent& event = m_testTraceEvents[i];
          out
            << "{\"frame_id\":" << event.frameId
            << ",\"effective_delta_ms\":" << event.effectiveDeltaMs
            << ",\"real_wall_delta_ms\":" << event.realWallDeltaMs
            << ",\"gpu_idle_time_ms\":" << event.gpuIdleTimeMs
            << ",\"screenshot_target_frame\":" << event.screenshotTargetFrame
            << ",\"terminate_target_frame\":" << event.terminateTargetFrame
            << ",\"surface_count\":" << event.surfaceCount
            << ",\"shader_compile_inflight_count\":" << event.shaderCompileInflightCount
            << ",\"debug_view_mode\":" << event.debugViewMode
            << ",\"composite_debug_view_mode\":" << event.compositeDebugViewMode
            << ",\"raytracing_enabled\":" << (event.raytracingEnabled ? "true" : "false")
            << ",\"camera_valid\":" << (event.cameraValid ? "true" : "false")
            << ",\"async_shader_prewarming\":" << (event.asyncShaderPrewarming ? "true" : "false")
            << ",\"async_compilation_enabled\":" << (event.asyncCompilationEnabled ? "true" : "false")
            << ",\"async_compilation_active\":" << (event.asyncCompilationActive ? "true" : "false")
            << ",\"surface_buffer_available\":" << (event.surfaceBufferAvailable ? "true" : "false")
            << "}\n";
        }
        out.close();
        traceFileWritten = true;
      }
    }

    if (m_fileStream) {
      m_fileStream << "dxvk_trace_enabled 1\n";
      m_fileStream << "dxvk_trace_file_written " << (traceFileWritten ? 1 : 0) << "\n";
      m_fileStream << "dxvk_trace_event_count " << m_testTraceEventCount << "\n";

      if (captureEvent != nullptr) {
        m_fileStream << "dxvk_trace_first_frame " << m_testTraceEvents[0].frameId << "\n";
        m_fileStream << "dxvk_trace_last_frame " << m_testTraceEvents[m_testTraceEventCount - 1].frameId << "\n";
        m_fileStream << "dxvk_trace_capture_frame " << captureEvent->frameId << "\n";
        m_fileStream << "dxvk_trace_effective_delta_ms_at_capture " << captureEvent->effectiveDeltaMs << "\n";
        m_fileStream << "dxvk_trace_real_wall_delta_ms_at_capture " << captureEvent->realWallDeltaMs << "\n";
        m_fileStream << "dxvk_trace_gpu_idle_time_ms_at_capture " << captureEvent->gpuIdleTimeMs << "\n";
        m_fileStream << "dxvk_trace_surface_count_at_capture " << captureEvent->surfaceCount << "\n";
        m_fileStream << "dxvk_trace_shader_compile_inflight_at_capture " << captureEvent->shaderCompileInflightCount << "\n";
        m_fileStream << "dxvk_trace_debug_view_mode_at_capture " << captureEvent->debugViewMode << "\n";
        m_fileStream << "dxvk_trace_composite_debug_view_mode_at_capture " << captureEvent->compositeDebugViewMode << "\n";
        m_fileStream << "dxvk_trace_raytracing_enabled_at_capture " << (captureEvent->raytracingEnabled ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_camera_valid_at_capture " << (captureEvent->cameraValid ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_async_shader_prewarming " << (captureEvent->asyncShaderPrewarming ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_async_compilation_enabled " << (captureEvent->asyncCompilationEnabled ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_async_compilation_active_at_capture " << (captureEvent->asyncCompilationActive ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_surface_buffer_available_at_capture " << (captureEvent->surfaceBufferAvailable ? 1 : 0) << "\n";
        m_fileStream << "dxvk_trace_effective_delta_ms_min " << effectiveDeltaMin << "\n";
        m_fileStream << "dxvk_trace_effective_delta_ms_max " << effectiveDeltaMax << "\n";
        m_fileStream << "dxvk_trace_real_wall_delta_ms_min " << realWallDeltaMin << "\n";
        m_fileStream << "dxvk_trace_real_wall_delta_ms_max " << realWallDeltaMax << "\n";
        m_fileStream << "dxvk_trace_gpu_idle_time_ms_min " << gpuIdleMin << "\n";
        m_fileStream << "dxvk_trace_gpu_idle_time_ms_max " << gpuIdleMax << "\n";
      }

      m_fileStream.flush();
    }

    m_testTraceFlushed = true;
  }

  Metrics::ShaderCompileScope::SharedState& Metrics::ShaderCompileScope::state() {
    static SharedState s;
    return s;
  }

  void Metrics::ShaderCompileScope::setProgressPath(const std::string& path) {
    SharedState& s = state();
    std::lock_guard<dxvk::mutex> lock(s.mutex);
    s.progressPath = path;
    writeProgress(s);
  }

  double Metrics::ShaderCompileScope::totalMs() {
    SharedState& s = state();
    std::lock_guard<dxvk::mutex> lock(s.mutex);
    double total = s.totalMs;
    if (!s.activeCompiles.empty()) {
      total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.activeStart).count();
    }
    return total;
  }

  Metrics::ShaderCompileScope::ShaderCompileScope(const std::string& name)
    : m_name(name) {
    std::replace(m_name.begin(), m_name.end(), '\n', ' ');
    std::replace(m_name.begin(), m_name.end(), '\r', ' ');

    m_startEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    SharedState& s = state();
    std::lock_guard<dxvk::mutex> lock(s.mutex);
    m_id = s.nextId++;

    if (s.activeCompiles.empty()) {
      s.activeStart = std::chrono::steady_clock::now();
      s.activeStartEpochMs = m_startEpochMs;
    }

    s.activeCompiles.emplace(m_id, this);
    writeProgress(s);
  }

  Metrics::ShaderCompileScope::~ShaderCompileScope() {
    SharedState& s = state();
    std::lock_guard<dxvk::mutex> lock(s.mutex);
    s.activeCompiles.erase(m_id);

    if (s.activeCompiles.empty()) {
      s.totalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s.activeStart).count();
      s.activeStartEpochMs = 0;
    }

    writeProgress(s);
  }

  void Metrics::ShaderCompileScope::writeProgress(const SharedState& s) {
    if (s.progressPath.empty()) {
      return;
    }

    std::ofstream out(str::tows(s.progressPath.c_str()).c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      return;
    }

    out << "shader_compile_total_ms " << static_cast<int64_t>(s.totalMs) << "\n";
    out << "shader_compile_active_since_epoch_ms " << s.activeStartEpochMs << "\n";
    for (const auto& [id, pScope] : s.activeCompiles) {
      out << "shader_compile_active " << pScope->m_startEpochMs << " " << pScope->m_name << "\n";
    }
    out << "end\n";
  }

  bool Metrics::shouldCaptureTestTrace(uint32_t frameId) const {
    if (!m_testTraceConfig.enabled || !m_testTraceConfig.screenshotFrameEnabled)
      return false;

    const uint32_t screenshotFrame = m_testTraceConfig.screenshotFrameNum;
    const uint32_t windowStart = screenshotFrame > 5 ? screenshotFrame - 5 : 0;
    const uint32_t windowEnd = screenshotFrame + 1;
    return frameId >= windowStart && frameId <= windowEnd;
  }
  
  std::string Metrics::getFileName() {
    return getOutputPath("metrics.txt");
  }

  std::string Metrics::getOutputPath(const char* fileName) {
    std::string path = env::getEnvVar("DXVK_METRICS_PATH");

    if (path == "none")
      return "";

    if (!path.empty() && *path.rbegin() != '/')
      path += '/';

    path += fileName;
    return path;
  }
}