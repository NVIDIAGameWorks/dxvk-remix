/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_resources.h"
#include "rtx/pass/raytrace_args.h"

namespace dxvk {

  class DxvkDevice;
  enum class IntegrateIndirectMode;

  class DxvkPathtracerIntegrateIndirect : public CommonDeviceObject {
  public:
    enum class RaytraceMode {
      RayQuery = 0,
      RayQueryRayGen,
      TraceRay,
      Count
    };

    explicit DxvkPathtracerIntegrateIndirect(DxvkDevice* device);
    ~DxvkPathtracerIntegrateIndirect() = default;

    void prewarmShaders(DxvkPipelineManager& pipelineManager) const;

    void dispatch(class RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);

    void dispatchNEE(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput);

    static const char* raytraceModeToString(RaytraceMode raytraceMode);

  private:
    static DxvkRaytracingPipelineShaders getPipelineShaders(const bool useRayQuery, const bool serEnabled, const bool ommEnabled, const bool useNeeCache, const bool includePortals, const bool pomEnabled, const bool nrcEnabled, const bool wboitEnbaled);
    Rc<DxvkShader> getComputeShader(const bool useNeeCache, const bool nrcEnabled, const bool wboitEnabled) const;
    void logIntegrateIndirectMode();
    
    IntegrateIndirectMode m_integrateIndirectMode;
  };
}
