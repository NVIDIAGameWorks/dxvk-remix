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
#include "util_math.h"

namespace dxvk {
  
  Metrics::Metrics() {
    auto path = getFileName();

    if (!path.empty())
      m_fileStream = std::ofstream(str::tows(path.c_str()).c_str());
  }
  
  Metrics::~Metrics() { }

  void Metrics::logRollingAverage(Metric metric, const float& value) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    const float& oldValue = s_instance.m_data[metric];

    const uint32_t kRollingAvgWindow = 30;
    s_instance.m_data[metric] = lerp(oldValue, value, 1.f / kRollingAvgWindow);
  }

  void Metrics::logFloat(Metric metric, const float& value) {
    std::lock_guard<dxvk::mutex> lock(s_instance.m_mutex);
    s_instance.m_data[metric] = value;
  }
  
  void Metrics::serialize() {
    for(uint32_t i=0 ; i<Metric::kCount ; i++)
      s_instance.emitMsg((Metric)i, s_instance.m_data[i]);
  }

  template<typename T>
  void Metrics::emitMsg(Metric metric, const T& value) {
    if(m_fileStream)
      m_fileStream << m_metricNames[(uint32_t)metric] << " " << value << std::endl;
  }
  
  std::string Metrics::getFileName() {
    std::string path = env::getEnvVar("DXVK_METRICS_PATH");
    
    if (path == "none")
      return "";

    if (!path.empty() && *path.rbegin() != '/')
      path += '/';

    path += "metrics.txt";
    return path;
  }
}
