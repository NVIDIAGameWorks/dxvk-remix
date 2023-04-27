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
#include "rtx_volume_preintegrate.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/volumetrics/volume_preintegrate_binding_indices.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"

#include <rtx_shaders/volume_preintegrate.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class VolumePreintegrateShader : public ManagedShader {
      SHADER_SOURCE(VolumePreintegrateShader, VK_SHADER_STAGE_COMPUTE_BIT, volume_preintegrate)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        TEXTURE3D(VOLUME_PREINTEGRATE_BINDING_FILTERED_RADIANCE_INPUT)
        RW_TEXTURE3D(VOLUME_PREINTEGRATE_BINDING_PREINTEGRATED_RADIANCE_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumePreintegrateShader);
  }

  DxvkVolumePreintegrate::DxvkVolumePreintegrate(DxvkDevice* device) {
  }

  void DxvkVolumePreintegrate::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput, uint32_t numActiveFroxelVolumes) {
    auto numVoxelsExtent = rtOutput.m_froxelVolumeExtent;
    // Note: Only dispatch in two dimensions on the voxel grid
    VkExtent3D workgroups = util::computeBlockCount(VkExtent3D{ numVoxelsExtent.width, numVoxelsExtent.height, 1 }, VkExtent3D{ 16, 8, 1 });

    ScopedGpuProfileZone(ctx, "Volume Preintegration");

    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    ctx->bindResourceView(VOLUME_PREINTEGRATE_BINDING_FILTERED_RADIANCE_INPUT, rtOutput.m_volumeFilteredRadiance.view, nullptr);
    ctx->bindResourceView(VOLUME_PREINTEGRATE_BINDING_PREINTEGRATED_RADIANCE_OUTPUT, rtOutput.m_volumePreintegratedRadiance.view, nullptr);

    // Dispatch rays

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumePreintegrateShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }
}
