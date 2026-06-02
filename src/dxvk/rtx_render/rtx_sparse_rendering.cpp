/*
* Copyright (c) 2025-2026, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_sparse_rendering.h"

#include "dxvk_device.h"
#include "rtx_context.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_ray_reconstruction.h"
#include "rtx_options.h"
#include "rtx_shader_manager.h"
#include "rtx_imgui.h"

#include <algorithm>

#include "rtx/pass/sparse_rendering/active_pixel_mask_binding_indices.h"
#include "rtx/pass/sparse_rendering/compact_active_pixels_binding_indices.h"

#include <dxvk_scoped_annotation.h>
#include "../util/log/log.h"
#include "../util/util_once.h"

#include <rtx_shaders/active_pixel_mask.h>
#include <rtx_shaders/active_pixel_sampling_rate.h>
#include <rtx_shaders/compact_active_pixels.h>

namespace dxvk {

  namespace {
    RemixGui::ComboWithKey<PerPixelRateNoiseSource> s_perPixelRateNoiseSourceCombo {
      "Per-Pixel Rate Noise Source",
      RemixGui::ComboWithKey<PerPixelRateNoiseSource>::ComboEntries { {
          {PerPixelRateNoiseSource::WhiteNoise, "White Noise (Hash)", "Per-pixel wangHash / whiteNoise; cheap and stateless."},
          {PerPixelRateNoiseSource::BlueNoise128x128x64x8, "Blue Noise (R8 128x128x64)", "R8 128x128 blue noise 64 frame length."}
      } }
    };

    class ActivePixelMaskShader : public ManagedShader {
      SHADER_SOURCE(ActivePixelMaskShader, VK_SHADER_STAGE_COMPUTE_BIT, active_pixel_mask)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_INPUT)
        TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_INPUT)

        // Outputs
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_DIRECT_ACTIVE_PIXEL_MASK_OUTPUT)
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_OUTPUT)
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_UNION_ACTIVE_PIXEL_MASK_OUTPUT)
      END_PARAMETER()
    };

    class ActivePixelSamplingRateShader : public ManagedShader {
      SHADER_SOURCE(ActivePixelSamplingRateShader, VK_SHADER_STAGE_COMPUTE_BIT, active_pixel_sampling_rate)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_SHARED_FLAGS_INPUT)

        // Outputs
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_OUTPUT)
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_OUTPUT)
      END_PARAMETER()
    };

    class CompactActivePixelsShader : public ManagedShader {
      SHADER_SOURCE(CompactActivePixelsShader, VK_SHADER_STAGE_COMPUTE_BIT, compact_active_pixels)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_PIXEL_MASK_INPUT)
        TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_INPUT)

        // Outputs
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_UNION_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
      END_PARAMETER()
    };
  }

  SparseRendering::SparseRendering(dxvk::DxvkDevice* device)
    : CommonDeviceObject(device)
    , RtxPass(device) {
  }

  bool SparseRendering::isEnabled() const {
    if (!Options::enableSparseRendering()) {
      return false;
    }

    if (!device()->getCommon()->metaRayReconstruction().useRayReconstruction()) {
      // Reason:
      //   RR has been tested to be able reconstruct sparse signal.
      ONCE(Logger::warn("[RTX Sparse Rendering] DLSS Ray Reconstruction is disabled; sparse rendering will not run. "
                        "It will resume automatically when DLSS Ray Reconstruction is re-enabled."));
      return false;
    }

    // Return false if NRC is not active
    if (!device()->getCommon()->metaNeuralRadianceCache().isActive()) {
      // Reason:
      //  ReSTIR GI cannot be active at the same time since bsdfFactor2.x would have to be applied to inactive pixels as well.
      //  Importance Sampled mode has not been tested.
      ONCE(Logger::warn("[RTX Sparse Rendering] Neural Radiance Cache is disabled; sparse rendering will not run. "
                        "It will resume automatically when Neural Radiance Cache is re-enabled."));
      return false;
    }

    return true;
  }

  bool SparseRendering::onActivation(Rc<DxvkContext>& ctx) {
    return checkCompactActivePixelsRequirements();
  }

  void SparseRendering::onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) {
    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    if (!isActive()) {
      return;
    }

    // Force disable dithering as it adds to correlation artifacts when using RR
    if (RtxOptions::enableFirstBounceLobeProbabilityDithering()) {
      ONCE(Logger::warn("[RTX] First bounce lobe probability dithering is not supported with Sparse Rendering enabled to avoid conflicts with DLSS Ray Reconstruction. It will be automatically disabled."));
      RtxOptions::enableFirstBounceLobeProbabilityDithering.setImmediately(false);
    }

    // Sparse secondary surface lighting needs per-signal radiance scaling for the combined direct+indirect
    // PSR buffer; not implemented yet. Force off.
    Options::enableSparseSecondaryLighting.setImmediately(false);
  }

  bool SparseRendering::checkCompactActivePixelsRequirements() const {
    // compact_active_pixels.comp.slang relies on a 32-lane subgroup: the Phase-1 bitmap fill packs
    // WaveActiveBallot results into a single uint per word (ballot.x), and s_waveTotals is sized
    // as COMPACT_ACTIVE_PIXELS_GROUP_SIZE / 32. Other lane counts would either alias bits or
    // underflow s_waveTotals. Generalizing the shader would require writing both ballot.x/.y
    // (and sizing s_waveTotals by the runtime lane count) — until then, reject mismatched devices.
    const uint32_t subgroupSize = device()->properties().coreSubgroup.subgroupSize;
    if (subgroupSize != 32) {
      ONCE(Logger::warn(str::format(
        "[RTX Sparse Rendering] Device subgroup size is ", subgroupSize,
        ", but compact_active_pixels.comp.slang requires 32. Sparse rendering will be disabled.")));
      return false;
    }
    return true;
  }

  void SparseRendering::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    Resources::RaytracingOutput& rtOutput = device()->getCommon()->getResources().getRaytracingOutput();

    rtOutput.m_sparseRenderingDirectActiveLocalPixelCoords =
      Resources::createImageResource(ctx, "Sparse Rendering Direct Active Local Coords", downscaledExtent, VK_FORMAT_R16_UINT);
    rtOutput.m_sparseRenderingIndirectActiveLocalPixelCoords =
      Resources::createImageResource(ctx, "Sparse Rendering Indirect Active Local Coords", downscaledExtent, VK_FORMAT_R16_UINT);
    rtOutput.m_sparseRenderingUnionActiveLocalPixelCoords =
      Resources::createImageResource(ctx, "Sparse Rendering Union Active Local Coords", downscaledExtent, VK_FORMAT_R16_UINT);
    rtOutput.m_sparseRenderingDirectPixelSamplingRate =
      Resources::createImageResource(ctx, "Sparse Rendering Direct Pixel Sampling Rate", downscaledExtent, VK_FORMAT_R8_UNORM);
    rtOutput.m_sparseRenderingIndirectPixelSamplingRate =
      Resources::createImageResource(ctx, "Sparse Rendering Indirect Pixel Sampling Rate", downscaledExtent, VK_FORMAT_R8_UNORM);

    const VkExtent3D blockSize = { ACTIVE_PIXEL_MASK_BLOCK_WIDTH, ACTIVE_PIXEL_MASK_BLOCK_HEIGHT, 1u };
    const VkExtent3D maskExtent = util::computeBlockCount(downscaledExtent, blockSize);
    m_activePixelMaskExtent = maskExtent;
    rtOutput.m_sparseRenderingDirectActivePixelMask =
      Resources::createImageResource(ctx, "Sparse Rendering Direct Active Pixel Mask", maskExtent, VK_FORMAT_R8_UINT);
    rtOutput.m_sparseRenderingIndirectActivePixelMask =
      Resources::createImageResource(ctx, "Sparse Rendering Indirect Active Pixel Mask", maskExtent, VK_FORMAT_R8_UINT);
    rtOutput.m_sparseRenderingUnionActivePixelMask =
      Resources::createImageResource(ctx, "Sparse Rendering Union Active Pixel Mask", maskExtent, VK_FORMAT_R8_UINT);
  }

  void SparseRendering::releaseDownscaledResource() {
    Resources::RaytracingOutput& rtOutput = device()->getCommon()->getResources().getRaytracingOutput();
    rtOutput.m_sparseRenderingDirectActiveLocalPixelCoords.reset();
    rtOutput.m_sparseRenderingIndirectActiveLocalPixelCoords.reset();
    rtOutput.m_sparseRenderingUnionActiveLocalPixelCoords.reset();
    rtOutput.m_sparseRenderingDirectPixelSamplingRate.reset();
    rtOutput.m_sparseRenderingIndirectPixelSamplingRate.reset();
    rtOutput.m_sparseRenderingDirectActivePixelMask.reset();
    rtOutput.m_sparseRenderingIndirectActivePixelMask.reset();
    rtOutput.m_sparseRenderingUnionActivePixelMask.reset();
    m_activePixelMaskExtent = { 0u, 0u, 0u };
  }

  bool SparseRendering::isEnabledByOptions() {
    if (!Options::enableSparseRendering()) {
      return false;
    }
    // See SparseRendering::isEnabled() for explanation on why these checks are necessary.
    if (!RtxOptions::enableRayReconstruction()) {
      return false;
    }
    if (RtxOptions::integrateIndirectMode() != IntegrateIndirectMode::NeuralRadianceCache) {
      return false;
    }
    return true;
  }

  void SparseRendering::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    if (!isEnabledByOptions()) {
      return;
    }

    ActivePixelSamplingRateShader::getShader();
    ActivePixelMaskShader::getShader();
    CompactActivePixelsShader::getShader();
  }

  void SparseRendering::dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    if (!isActive()) {
      return;
    }

    ctx.setFramePassStage(RtxFramePassStage::SparseRendering);

    ctx.bindCommonRayTracingResources(rtOutput);

    dispatchActivePixelSamplingRate(ctx, rtOutput);
    dispatchActivePixelMask(ctx, rtOutput);
    dispatchCompactActivePixels(ctx, rtOutput);
  }

  void SparseRendering::dispatchActivePixelSamplingRate(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(&ctx, "Sparse Pixel Sampling Rate");

    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ActivePixelSamplingRateShader::getShader());

    // Inputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);

    // Outputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_OUTPUT, rtOutput.m_sparseRenderingDirectPixelSamplingRate.view, nullptr);
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_OUTPUT, rtOutput.m_sparseRenderingIndirectPixelSamplingRate.view, nullptr);

    const VkExtent3D& extent = rtOutput.m_compositeOutputExtent;
    const VkExtent3D groupSize = { ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1u };
    const VkExtent3D workgroups = util::computeBlockCount(extent, groupSize);
    ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void SparseRendering::dispatchActivePixelMask(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(&ctx, "Active Pixel Mask");

    // Build per-tile bit masks (direct, indirect, union) recording active pixels for each signal.
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ActivePixelMaskShader::getShader());

    // Inputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_DIRECT_PIXEL_SAMPLING_RATE_INPUT, rtOutput.m_sparseRenderingDirectPixelSamplingRate.view, nullptr);
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_PIXEL_SAMPLING_RATE_INPUT, rtOutput.m_sparseRenderingIndirectPixelSamplingRate.view, nullptr);

    // Outputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_DIRECT_ACTIVE_PIXEL_MASK_OUTPUT, rtOutput.m_sparseRenderingDirectActivePixelMask.view, nullptr);
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_OUTPUT, rtOutput.m_sparseRenderingIndirectActivePixelMask.view, nullptr);
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_UNION_ACTIVE_PIXEL_MASK_OUTPUT, rtOutput.m_sparseRenderingUnionActivePixelMask.view, nullptr);

    const VkExtent3D& maskExtent = m_activePixelMaskExtent;
    const VkExtent3D maskGroupSize = { ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1u };
    const VkExtent3D maskWorkgroups = util::computeBlockCount(maskExtent, maskGroupSize);
    ctx.dispatch(maskWorkgroups.width, maskWorkgroups.height, maskWorkgroups.depth);
  }

  void SparseRendering::dispatchCompactActivePixels(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(&ctx, "Compact Active Pixels");

    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompactActivePixelsShader::getShader());

    // Inputs
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_PIXEL_MASK_INPUT, rtOutput.m_sparseRenderingDirectActivePixelMask.view, nullptr);
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_PIXEL_MASK_INPUT, rtOutput.m_sparseRenderingIndirectActivePixelMask.view, nullptr);

    // Outputs
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_DIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT,
                         rtOutput.m_sparseRenderingDirectActiveLocalPixelCoords.view, nullptr);
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_INDIRECT_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT,
                         rtOutput.m_sparseRenderingIndirectActiveLocalPixelCoords.view, nullptr);
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_UNION_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT,
                         rtOutput.m_sparseRenderingUnionActiveLocalPixelCoords.view, nullptr);

    const VkExtent3D extent = rtOutput.m_compositeOutputExtent;
    const VkExtent3D tileSize = { COMPACT_ACTIVE_PIXELS_TILE_SIZE_X, COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y, 1 };
    const VkExtent3D workgroups = util::computeBlockCount(extent, tileSize);

    ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void SparseRendering::setSparseRenderingArgs(RtxContext& ctx, SparseRenderingArgs& args) const {
    args.mode = isActive() ? SparseRenderingMode::Uniform : SparseRenderingMode::Off;

    args.perPixelRateNoiseSource = Options::perPixelRateNoiseSource();
    args.enableSparsePrimaryRayMissComposition = Options::enableSparsePrimaryRayMissComposition();
    args.enableSparseSecondaryLighting = Options::enableSparseSecondaryLighting();
    args.enableRtxdiReuseForInactivePixels = Options::enableRtxdiReuseForInactivePixels() || args.mode == SparseRenderingMode::Off;
    args.enableSparseVolumetricsPrimaryHit = Options::enableSparseVolumetricsPrimaryHit();
    args.enableSparseVolumetricsPrimaryMiss = Options::enableSparseVolumetricsPrimaryMiss();
    args.enableSparsePrimarySpecularAlbedo = Options::enableSparsePrimarySpecularAlbedo();

    NeuralRadianceCache& nrc = ctx.getCommonObjects()->metaNeuralRadianceCache();
    args.forceNrcTrainingPixelsActive = nrc.isActive() && Options::forceNrcTrainingPixelsActive();

    args.directPixelSamplingRate = Options::directLightingSamplingRate();
    args.indirectPixelSamplingRate = Options::indirectLightingSamplingRate();

    args.activePixelMaskExtent = { m_activePixelMaskExtent.width, m_activePixelMaskExtent.height };
  }

  void SparseRendering::showImguiSettings() {
    // Sparse rendering relies on ray reconstruction for reconstructing inactive pixels,
    // so disable the UI when ray reconstruction is disabled to avoid confusion.
    ImGui::BeginDisabled(!device()->getCommon()->metaRayReconstruction().useRayReconstruction());

    constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
    constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;

    RemixGui::Checkbox("Enable Sparse Rendering", &Options::enableSparseRenderingObject());

    RemixGui::DragFloat("Direct Lighting Sampling Rate", &Options::directLightingSamplingRateObject(), 0.01f, 1.0f / 128.0f, 1.0f, "%.3f", sliderFlags);
    RemixGui::DragFloat("Indirect Lighting Sampling Rate", &Options::indirectLightingSamplingRateObject(), 0.01f, 1.0f / 128.0f, 1.0f, "%.3f", sliderFlags);

    if (RemixGui::CollapsingHeader("Experimental", collapsingHeaderClosedFlags)) {
      ImGui::Indent();
      ImGui::TextWrapped("The following options are experimental and for development only. Toggling them may cause visual issues.");
      RemixGui::Checkbox("Enable RTXDI Reuse For Inactive Pixels", &Options::enableRtxdiReuseForInactivePixelsObject());
      RemixGui::Checkbox("Sparse Primary Ray Miss Composition", &Options::enableSparsePrimaryRayMissCompositionObject());
      // ToDo: Secondary
      ImGui::BeginDisabled(true);
      RemixGui::Checkbox("Sparse Secondary Surface Lighting", &Options::enableSparseSecondaryLightingObject());
      ImGui::EndDisabled();
      RemixGui::Checkbox("Force NRC Training Pixels Active", &Options::forceNrcTrainingPixelsActiveObject());
      RemixGui::Checkbox("Sparse Volumetrics (Primary Hit)", &Options::enableSparseVolumetricsPrimaryHitObject());
      RemixGui::Checkbox("Sparse Volumetrics (Primary Miss)", &Options::enableSparseVolumetricsPrimaryMissObject());
      RemixGui::Checkbox("Sparse Primary Specular Albedo", &Options::enableSparsePrimarySpecularAlbedoObject());
      s_perPixelRateNoiseSourceCombo.getKey(&Options::perPixelRateNoiseSourceObject());

      ImGui::Unindent();
    }

    ImGui::EndDisabled();
  }

} // namespace dxvk
