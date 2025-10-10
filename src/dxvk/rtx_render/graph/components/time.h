/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "../rtx_graph_component_macros.h"
#include "../../../util/util_globaltime.h"

namespace dxvk {
namespace components {

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, true, enabled, "Enabled", "If true, time accumulation continues. If false, time is paused.", property.optional = true) \
  X(RtComponentPropertyType::Float, 1.0f, speedMultiplier, "Speed Multiplier", "Multiplier for time speed. 1.0 = normal speed, 2.0 = double speed, 0.5 = half speed.", property.minValue = 0.0f, property.optional = true)

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Float, 0.0f, accumulatedTime, "", "The accumulated time since component creation (in seconds).")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Float, 0.0f, currentTime, "Current Time", "The time in seconds since component creation.")

REMIX_COMPONENT( \
  /* the Component name */ Time, \
  /* the UI name */        "Time", \
  /* the UI categories */  "Sense", \
  /* the doc string */     "Outputs the time in seconds since the component was created. Can be paused and speed-adjusted.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

void Time::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  float deltaTime = GlobalTime::get().deltaTime();
  
  for (size_t i = start; i < end; i++) {
    // Only accumulate time if enabled, and apply speed multiplier
    if (m_enabled[i]) {
      m_accumulatedTime[i] += deltaTime * std::max(0.0f, m_speedMultiplier[i]);
    }
    
    // Output the accumulated time
    m_currentTime[i] = m_accumulatedTime[i];
  }
}

}  // namespace components
}  // namespace dxvk
