/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_demodulate.h"
#include "dxvk_device.h"
#include "rtx.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/demodulate/demodulate_binding_indices.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_render/rtx_restir_gi_rayquery.h"
#include "rtx_debug_view.h"

#include <rtx_shaders/demodulate.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class DemodulateShader : public ManagedShader {
      SHADER_SOURCE(DemodulateShader, VK_SHADER_STAGE_COMPUTE_BIT, demodulate)

      PUSH_CONSTANTS(VkExtent2D)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(DEMODULATE_BINDING_CONSTANTS)
        TEXTURE2D(DEMODULATE_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_PRIMARY_ALBEDO_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_SECONDARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_SECONDARY_ALBEDO_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT)
        TEXTURE2D(DEMODULATE_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_DIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_DIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_COMBINED_DIFFUSE_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_COMBINED_SPECULAR_RADIANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_PRIMARY_SPECULAR_ALBEDO_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_SECONDARY_SPECULAR_ALBEDO_OUTPUT)
        RW_TEXTURE2D(DEMODULATE_BINDING_DEBUG_VIEW_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DemodulateShader);
  }

  DemodulatePass::DemodulatePass(dxvk::DxvkDevice* device)
    : m_vkd(device->vkd()), m_device(device) {
  }

  DemodulatePass::~DemodulatePass() { }

  void DemodulatePass::showImguiSettings() {
    RemixGui::Checkbox("Demodulate Roughness", &demodulateRoughnessObject());
    RemixGui::DragFloat("NRD Roughness sensitivity", &demodulateRoughnessOffsetObject(), 0.01f, 0.0f, 5.0f, "%.3f");
    RemixGui::Checkbox("Direct Light Boiling Filter", &enableDirectLightBoilingFilterObject());
    RemixGui::DragFloat("Direct Light Boiling Threshold", &directLightBoilingThresholdObject(), 0.01f, 1.f, 500.f, "%.1f");
  }

  void DemodulatePass::dispatch(RtxContext* ctx, const Resources::RaytracingOutput& rtOutput) {
    const auto& numRaysExtent = rtOutput.m_compositeOutputExtent;
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D{ 16, 8, 1 });

    ScopedGpuProfileZone(ctx, "Demodulate");
    ctx->setFramePassStage(RtxFramePassStage::Demodulate);

    Rc<DxvkBuffer> constantsBuffer = ctx->getResourceManager().getConstantsBuffer();
    DebugView& debugView = ctx->getDevice()->getCommon()->metaDebugView();

    // Bind resources
    
    // Note: Base reflectivity rewritten to be specular albedo at this point, hence the dual-purpose
    // input/output bindings for both quantities.

    ctx->bindResourceBuffer(DEMODULATE_BINDING_CONSTANTS, DxvkBufferSlice(constantsBuffer, 0, constantsBuffer->info().size));
    ctx->bindResourceView(DEMODULATE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_ALBEDO_INPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_INPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_secondaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_ALBEDO_INPUT, rtOutput.m_secondaryAlbedo.view, nullptr);

    // m_indirectRadianceHitDistance and m_primaryIndirectDiffuseRadiance are aliased
    // RestirGI already updated m_primaryIndirectDiffuseRadiance for primary surface pixels
    // For secondary surfaces pixels m_indirectRadianceHitDistance is still valid
    // Therefore we suppress the alias check for m_indirectRadianceHitDistance 
    // since m_primaryIndirectDiffuseRadiance already took ownership of the shared resource
    const bool isPrimaryIndirectRadianceResourceRead = ctx->getCommonObjects()->metaReSTIRGIRayQuery().isActive();
    const bool suppressIndirectRadianceAliasCheck = isPrimaryIndirectRadianceResourceRead;

    ctx->bindResourceView(DEMODULATE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::Read, !suppressIndirectRadianceAliasCheck), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_BASE_REFLECTIVITY_INPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_BASE_REFLECTIVITY_INPUT, rtOutput.m_secondaryBaseReflectivity.view(Resources::AccessType::Read), nullptr);
    
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_DIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_DIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);

    const bool isPrimaryIndirectRadianceResourceWritten = rtOutput.m_raytraceArgs.enableSeparatedDenoisers;
    const bool isPrimaryIndirectRadianceResourceUsed = isPrimaryIndirectRadianceResourceRead || isPrimaryIndirectRadianceResourceWritten;
    Resources::AccessType primaryIndirectRadianceAccessType;

    if (isPrimaryIndirectRadianceResourceRead && isPrimaryIndirectRadianceResourceWritten)
      primaryIndirectRadianceAccessType = Resources::AccessType::ReadWrite;
    else if (isPrimaryIndirectRadianceResourceRead)
      primaryIndirectRadianceAccessType = Resources::AccessType::Read;
    else
      primaryIndirectRadianceAccessType = Resources::AccessType::Write;

    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_INDIRECT_DIFFUSE_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(primaryIndirectRadianceAccessType, isPrimaryIndirectRadianceResourceUsed), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_INDIRECT_SPECULAR_RADIANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(primaryIndirectRadianceAccessType, isPrimaryIndirectRadianceResourceUsed), nullptr);

    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_COMBINED_DIFFUSE_RADIANCE_INPUT_OUTPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_COMBINED_SPECULAR_RADIANCE_INPUT_OUTPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_PRIMARY_SPECULAR_ALBEDO_OUTPUT, rtOutput.m_primarySpecularAlbedo.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_SECONDARY_SPECULAR_ALBEDO_OUTPUT, rtOutput.m_secondarySpecularAlbedo.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(DEMODULATE_BINDING_DEBUG_VIEW_OUTPUT, debugView.getDebugOutput(), nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DemodulateShader::getShader());
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }
  
} // namespace dxvk
