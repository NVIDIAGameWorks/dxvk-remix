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
#include <vector>
#include <unordered_map>
#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx/utility/shader_types.h"
#include "rtx/concept/light/light_types.h"
#include "rtx_lights.h"
#include "rtx_cameramanager.h"

struct RaytraceArgs;

namespace dxvk
{
class RtxContext;
class DxvkDevice;

struct LightRange
{
  uint32_t offset;
  uint32_t count;
};

struct LightManager
{
public:
  LightManager(LightManager const&) = delete;
  LightManager& operator=(LightManager const&) = delete;

  LightManager(Rc<DxvkDevice> device);
  ~LightManager();

  const std::unordered_map<XXH64_hash_t, RtLight>& getLightTable() const { return m_lights; }
  const Rc<DxvkBuffer> getLightBuffer() const { return m_lightBuffer; }
  const Rc<DxvkBuffer> getPreviousLightBuffer() const { return m_previousLightBuffer.ptr() ? m_previousLightBuffer : m_lightBuffer; }
  const Rc<DxvkBuffer> getLightMappingBuffer() const { return m_lightMappingBuffer; }
  const uint32_t getActiveCount() const { return m_currentActiveLightCount; }

  void clear();

  void garbageCollection();

  void dynamicLightMatching();

  void prepareSceneData(Rc<RtxContext> ctx, CameraManager const& cameraManager);

  void addLight(const RtLight& light);
  void addLight(const RtLight& light, const DrawCallState& drawCallState);

  void setRaytraceArgs(RaytraceArgs& raytraceArgs, uint32_t rtxdiInitialLightSamples, uint32_t volumeRISInitialLightSamples, uint32_t risLightSamples) const;

private:
  std::unordered_map<XXH64_hash_t, RtLight> m_lights;
  // Note: A fallback light tracked seperately and handled specially to not be mixed up with
  // lights provided from the application.
  std::optional<RtLight> m_fallbackLight{};
  Rc<DxvkBuffer> m_lightBuffer;
  Rc<DxvkBuffer> m_previousLightBuffer;
  Rc<DxvkBuffer> m_lightMappingBuffer;
  Rc<DxvkDevice> m_device;

  uint32_t m_currentActiveLightCount = 0;
  std::array<LightRange, lightTypeCount> m_lightTypeRanges;
  // Note: The following vectors are included as members rather as local variables in the
  // prepareSceneData function where they are primarily used to prevent redundant allocations/frees
  // of the memory behind these buffers between each call (at the cost of slightly more persistent
  // memory usage, but these buffers are fairly small at only 4 MiB or so max with 2^16 lights present).
  std::vector<RtLight*> m_linearizedLights{};
  std::vector<unsigned char> m_lightsGPUData{};
  std::vector<uint16_t> m_lightMappingData{};

  // Similarity check.
  //  Returns -1 if not similar
  //  Returns 0~1 if similar, higher is more similar
  static float isSimilar(const RtLight& a, const RtLight& b, float distanceThreshold);
  static void updateLight(const RtLight& in, RtLight& out);
};

}  // namespace dxvk

