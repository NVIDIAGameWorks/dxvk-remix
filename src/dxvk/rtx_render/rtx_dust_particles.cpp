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
#include "rtx_dust_particles.h"
#include "dxvk_device.h"
#include "rtx_render/rtx_shader_manager.h"

#include "rtx/pass/common_binding_indices.h"
#include "dxvk_scoped_annotation.h"
#include "dxvk_context.h"
#include "rtx_context.h"
#include "rtx_imgui.h"

#include <rtx_shaders/dust_particles_vertex.h>
#include <rtx_shaders/dust_particles_fragment.h>
#include "dxvk_context_state.h"
#include "../util/util_globaltime.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {

    class DustParticleVertexShader : public ManagedShader {
      SHADER_SOURCE(DustParticleVertexShader, VK_SHADER_STAGE_VERTEX_BIT, dust_particles_vertex)

      PUSH_CONSTANTS(DustParticleSystemConstants)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER3D(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_CO_CG_INPUT)
        RW_STRUCTURED_BUFFER(DUST_PARTICLES_BINDING_PARTICLES_BUFFER_INOUT)
      END_PARAMETER()

      // Particle color and center pos output
      INTERFACE_OUTPUT_SLOTS(2);
    };


    class DustParticlePixelShader: public ManagedShader {
      SHADER_SOURCE(DustParticlePixelShader, VK_SHADER_STAGE_FRAGMENT_BIT, dust_particles_fragment)

      PUSH_CONSTANTS(DustParticleSystemConstants)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS
        SAMPLER2D(DUST_PARTICLES_BINDING_DEPTH_INPUT)
      END_PARAMETER()

      // Color and center pos fetched from VS
      INTERFACE_INPUT_SLOTS(2);
      // Writing out of pixel shader to render target
      INTERFACE_OUTPUT_SLOTS(1);
    };
  }

  RtxDustParticles::RtxDustParticles(DxvkDevice* device) {
  }

  void RtxDustParticles::showImguiSettings() {
    if (RemixGui::CollapsingHeader("Dust Particles")) {
      ImGui::PushID("dustParticles");
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      RemixGui::Checkbox("Enable", &enableObject());
      ImGui::BeginDisabled(!enable());

      RemixGui::DragInt("Number of Particles", &numberOfParticlesObject(), 0.1f, 1, 100000000, "%d", ImGuiSliderFlags_AlwaysClamp);

      if (RemixGui::CollapsingHeader("Spawn")) {
        RemixGui::DragFloat("Min Spawn Distance", &minSpawnDistanceObject(), 0.01f, 0.01f, maxSpawnDistance(), "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Max Spawn Distance", &maxSpawnDistanceObject(), 0.01f, minSpawnDistance(), 10000.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Minimum Life", &minParticleLifeObject(), 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Maximum Life", &maxParticleLifeObject(), 0.01f, 0.01f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Minimum Size", &minParticleSizeObject(), 0.01f, 1.f, 50.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Maximum Size", &maxParticleSizeObject(), 0.01f, 1.f, 50.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      }

      if (RemixGui::CollapsingHeader("Looks")) {
        RemixGui::DragFloat("Opacity", &opacityObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Anisotropy", &anisotropyObject(), 0.01f, -1.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      }

      if (RemixGui::CollapsingHeader("Simulation")) {
        RemixGui::DragFloat("Time Scale", &timeScaleObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Gravity Force", &gravityForceObject(), 0.01f, -100.f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Rotation Speed", &rotationSpeedObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Max Speed", &maxSpeedObject(), 0.01f, 0.f, 100.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

        RemixGui::Checkbox("Simulate Turbulence", &useTurbulenceObject());
        ImGui::BeginDisabled(!useTurbulence());
        RemixGui::DragFloat("Turbulence Amplitude", &turbulenceAmplitudeObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        RemixGui::DragFloat("Turbulence Frequency", &turbulenceFrequencyObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
      }
      ImGui::Unindent();
      ImGui::EndDisabled();
      ImGui::PopID();
    }
  }


  void setupRasterizerState(RtxContext* ctx, const Resources::Resource& output) {
    DxvkInputAssemblyState iaState;
    iaState.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    iaState.primitiveRestart = VK_FALSE;
    iaState.patchVertexCount = 0;
    ctx->setInputAssemblyState(iaState);

    DxvkRasterizerState rsState;
    rsState.polygonMode = VK_POLYGON_MODE_FILL;
    rsState.cullMode = VK_CULL_MODE_NONE;
    rsState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsState.depthClipEnable = VK_FALSE;
    rsState.depthBiasEnable = VK_FALSE;
    rsState.conservativeMode = VK_CONSERVATIVE_RASTERIZATION_MODE_DISABLED_EXT;
    rsState.sampleCount = VK_SAMPLE_COUNT_1_BIT;
    ctx->setRasterizerState(rsState);

    DxvkMultisampleState msState;
    msState.sampleMask = 0xffffffff;
    msState.enableAlphaToCoverage = VK_FALSE;
    ctx->setMultisampleState(msState);

    VkStencilOpState stencilOp;
    stencilOp.failOp = VK_STENCIL_OP_KEEP;
    stencilOp.passOp = VK_STENCIL_OP_KEEP;
    stencilOp.depthFailOp = VK_STENCIL_OP_KEEP;
    stencilOp.compareOp = VK_COMPARE_OP_ALWAYS;
    stencilOp.compareMask = 0xFFFFFFFF;
    stencilOp.writeMask = 0xFFFFFFFF;
    stencilOp.reference = 0;

    DxvkDepthStencilState dsState;
    dsState.enableDepthTest = VK_FALSE;
    dsState.enableDepthWrite = VK_FALSE;
    dsState.enableStencilTest = VK_FALSE;
    dsState.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    dsState.stencilOpFront = stencilOp;
    dsState.stencilOpBack = stencilOp;
    ctx->setDepthStencilState(dsState);

    DxvkLogicOpState loState;
    loState.enableLogicOp = VK_FALSE;
    loState.logicOp = VK_LOGIC_OP_NO_OP;
    ctx->setLogicOpState(loState);

    DxvkBlendMode blendMode;
    blendMode.enableBlending = VK_TRUE;
    blendMode.colorSrcFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendMode.colorDstFactor = VK_BLEND_FACTOR_ONE;
    blendMode.colorBlendOp = VK_BLEND_OP_ADD;
    blendMode.alphaSrcFactor = VK_BLEND_FACTOR_ZERO;
    blendMode.alphaDstFactor = VK_BLEND_FACTOR_ONE;
    blendMode.alphaBlendOp = VK_BLEND_OP_ADD;
    blendMode.writeMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    ctx->setBlendMode(0, blendMode);

    VkViewport viewport;
    viewport.x = 0.f;
    viewport.y = 0.f;
    viewport.width = float(output.image->info().extent.width);
    viewport.height = float(output.image->info().extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D dstRect;
    dstRect.offset = { 0, 0 };
    dstRect.extent = {
      output.image->info().extent.width,
      output.image->info().extent.height };
    ctx->setViewports(1, &viewport, &dstRect);

    DxvkRenderTargets renderTargets;
    renderTargets.color[0].view = output.view;
    renderTargets.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;

    ctx->bindRenderTargets(renderTargets);

    ctx->setInputLayout(0, nullptr, 0, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, nullptr);
    ctx->bindShader(VK_SHADER_STAGE_GEOMETRY_BIT, nullptr);
  }


  void RtxDustParticles::setupConstants(RtxContext* ctx, const float frameTimeSecs, Resources& resourceManager, DustParticleSystemConstants& pushArgs) {
    pushArgs.minTtl = minParticleLife();
    pushArgs.maxTtl = maxParticleLife();
    pushArgs.minParticleSize = minParticleSize();
    pushArgs.maxParticleSize = maxParticleSize();
    pushArgs.opacity = opacity();
    pushArgs.anisotropy = anisotropy();
    pushArgs.cullDistanceFromCamera = RtxGlobalVolumetrics::froxelMaxDistanceMeters() * RtxOptions::getMeterToWorldUnitScale();
    pushArgs.gravityForce = gravityForce();
    pushArgs.maxSpeed = maxSpeed();
    pushArgs.useTurbulence = useTurbulence();
    pushArgs.turbulenceFrequency = turbulenceFrequency();
    pushArgs.turbulenceAmplitude = turbulenceAmplitude();
    pushArgs.rotationSpeed = rotationSpeed();
    pushArgs.upDirection.x = ctx->getSceneManager().getSceneUp().x;
    pushArgs.upDirection.y = ctx->getSceneManager().getSceneUp().y;
    pushArgs.upDirection.z = ctx->getSceneManager().getSceneUp().z;
    pushArgs.deltaTimeSecs = frameTimeSecs * timeScale();
    pushArgs.renderResolution.x = resourceManager.getTargetDimensions().width;
    pushArgs.renderResolution.y = resourceManager.getTargetDimensions().height;

    // Precalculate some variables to help with uniform distribution within a frustum
    const RtCamera& camera = ctx->getSceneManager().getCamera();

    const float minDistance = minSpawnDistance() + camera.getNearPlane();
    const float maxDistance = maxSpawnDistance() + camera.getNearPlane();

    pushArgs.nearH = tan(camera.getFov() * 0.5f) * minDistance;
    pushArgs.nearW = pushArgs.nearH * std::fabs(camera.getAspectRatio());
    pushArgs.farH = tan(camera.getFov() * 0.5f) * maxDistance;
    pushArgs.farW = pushArgs.farH * std::fabs(camera.getAspectRatio());
    pushArgs.frustumDepth = (maxDistance - minDistance);
    pushArgs.frustumMin = minDistance;

    pushArgs.frustumA = (pushArgs.farW - pushArgs.nearW) * (pushArgs.farH - pushArgs.nearH) / (pushArgs.frustumDepth * pushArgs.frustumDepth);
    pushArgs.frustumB = (pushArgs.nearH * (pushArgs.farW - pushArgs.nearW) + pushArgs.nearW * (pushArgs.farH - pushArgs.nearH)) / pushArgs.frustumDepth;
    pushArgs.frustumC = pushArgs.nearW * pushArgs.nearH;

    // Integral of area of a frustum with respect to Z (which is A(x) = ax^2+bx^2+c)
    pushArgs.frustumAreaAtZ = (pushArgs.frustumA * pow(pushArgs.frustumDepth, 3) / 3) + (pushArgs.frustumB * pow(pushArgs.frustumDepth, 2) / 2) + pushArgs.frustumC * pushArgs.frustumDepth;
    pushArgs.frustumDet = pushArgs.frustumB * pushArgs.frustumB - 4 * pushArgs.frustumA * pushArgs.frustumC;

    pushArgs.isCameraLhs = camera.isLHS();
  }

  static_assert(sizeof(GpuDustParticle) == 8 * 4, "Unexpected, please check perf");// be careful with performance when increasing this!

  void RtxDustParticles::simulateAndDraw(RtxContext* ctx, DxvkContextState& dxvkCtxState, const Resources::RaytracingOutput& rtOutput) {
    if (!enable()) { 
      return;
    }

    ScopedGpuProfileZone(ctx, "Dust Particles");
    ctx->setFramePassStage(RtxFramePassStage::DustParticles);

    if(m_particles == nullptr || m_particles->info().size != sizeof(GpuDustParticle) * numberOfParticles()) {
      const Rc<DxvkDevice>& device = ctx->getDevice();

      DxvkBufferCreateInfo info;
      info.size = sizeof(GpuDustParticle) * numberOfParticles();
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT
        | VK_ACCESS_TRANSFER_READ_BIT
        | VK_ACCESS_SHADER_WRITE_BIT
        | VK_ACCESS_SHADER_READ_BIT;
      m_particles = device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Dust Particles");

      ctx->clearBuffer(m_particles, 0, info.size, 0);
    }

    ctx->bindCommonRayTracingResources(rtOutput);

    DxvkContextState stateCopy = dxvkCtxState;

    Resources& resourceManager = ctx->getResourceManager();
    Rc<DxvkSampler> linearSampler = resourceManager.getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_Y_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_Y_INPUT, linearSampler);
    ctx->bindResourceView(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_CO_CG_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(DUST_PARTICLES_BINDING_FILTERED_RADIANCE_CO_CG_INPUT, linearSampler);
    ctx->bindResourceBuffer(DUST_PARTICLES_BINDING_PARTICLES_BUFFER_INOUT, DxvkBufferSlice(m_particles));
    ctx->bindResourceView(DUST_PARTICLES_BINDING_DEPTH_INPUT, rtOutput.m_primaryDepth.view, nullptr);
    ctx->bindResourceSampler(DUST_PARTICLES_BINDING_DEPTH_INPUT, linearSampler);
    
    setupRasterizerState(ctx, rtOutput.m_finalOutput.resource(Resources::AccessType::ReadWrite));
    
    ctx->bindShader(VK_SHADER_STAGE_VERTEX_BIT, DustParticleVertexShader::getShader());
    ctx->bindShader(VK_SHADER_STAGE_FRAGMENT_BIT, DustParticlePixelShader::getShader());

    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);

    DustParticleSystemConstants pushArgs;
    setupConstants(ctx, GlobalTime::get().deltaTime(), resourceManager, pushArgs);
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    ctx->draw(numberOfParticles(), 1, 0, 0);

    dxvkCtxState = stateCopy;
  }
}
