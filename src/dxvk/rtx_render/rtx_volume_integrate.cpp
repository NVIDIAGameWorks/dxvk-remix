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
#include "rtx_volume_integrate.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/volumetrics/volume_integrate_binding_indices.h"

#include <rtx_shaders/volume_integrate_rayquery.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class VolumeIntegrateShader : public ManagedShader
    {
      SHADER_SOURCE(VolumeIntegrateShader, VK_SHADER_STAGE_COMPUTE_BIT, volume_integrate_rayquery)

      BINDLESS_ENABLED()

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER3D(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT)
        TEXTURE3D(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT)

        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT)
        RW_TEXTURE3D(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(VolumeIntegrateShader);
  }

  DxvkVolumeIntegrate::DxvkVolumeIntegrate(DxvkDevice* device) {
  }

  void DxvkVolumeIntegrate::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput, uint32_t numActiveFroxelVolumes) {

    ScopedGpuProfileZone(ctx, "Volume Integrate Raytracing");

    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view. Note this is fine here as the temporal reprojection lookups will ensure
    // their UVW coordinates are not out of the [0, 1] range before looking up the value.
    Rc<DxvkSampler> linearSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT, rtOutput.getPreviousVolumeAccumulatedRadiance().view, nullptr);
    ctx->bindResourceSampler(VOLUME_INTEGRATE_BINDING_PREV_ACCUMULATED_RADIANCE_INPUT, linearSampler);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_PREV_VOLUME_RESERVOIRS_INPUT, rtOutput.getPreviousVolumeReservoirs().view, nullptr);

    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_ACCUMULATED_RADIANCE_OUTPUT, rtOutput.getCurrentVolumeAccumulatedRadiance().view, nullptr);
    ctx->bindResourceView(VOLUME_INTEGRATE_BINDING_VOLUME_RESERVOIRS_OUTPUT, rtOutput.getCurrentVolumeReservoirs().view, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, VolumeIntegrateShader::getShader());

    // Dispatch rays
    auto numRaysExtent = rtOutput.m_froxelVolumeExtent;
    numRaysExtent.width *= numActiveFroxelVolumes;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 16, 8, 1 });

    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Todo: Implement TraceRay path if needed some day, currently not though.
    /*
    switch (RtxOptions::Get()->getRenderPassVolumeIntegrateRaytraceMode()) {
    case RaytraceMode::RayQuery:
      VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });
      ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader());
      ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
      break;
    case RaytraceMode::RayQueryRayGen:
      ctx.bindRaytracingPipelineShaders(getPipelineShaders(true));
      ctx.traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    case RaytraceMode::TraceRay:
      ctx.bindRaytracingPipelineShaders(getPipelineShaders(false));
      ctx.traceRays(rayDims.width, rayDims.height, rayDims.depth);
      break;
    }
    */
  }

  DxvkRaytracingPipelineShaders DxvkVolumeIntegrate::getPipelineShaders(bool useRayQuery) const {
    DxvkRaytracingPipelineShaders shaders;
    // Todo: Implement TraceRay path if needed some day, currently not though.
    /*
    if (useRayQuery) {
      shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_rayquery_raygen));
      shaders.debugName = "Volume Integrate RayQuery (RGS)";
    } else {
      if (RtxOptions::Get()->isShaderExecutionReorderingInVolumeIntegrateEnabled())
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_raygen_ser));
      else
        shaders.addGeneralShader(GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, VolumeIntegrateShader, volume_integrate_raygen));
      shaders.addGeneralShader(VolumeIntegrateMissShader::getShader());

      ADD_HIT_GROUPS(VolumeIntegrateClosestHitShader, volume_integrate);

      shaders.debugName = "Volume Integrate TraceRay (RGS)";
    }
    */
    return shaders;
  }
}
