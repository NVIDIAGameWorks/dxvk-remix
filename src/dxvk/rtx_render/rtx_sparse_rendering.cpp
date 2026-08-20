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
        TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_INPUT)

        // Outputs
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_ACTIVE_PIXEL_MASK_OUTPUT)
      END_PARAMETER()
    };

    class ActivePixelSamplingRateShader : public ManagedShader {
      SHADER_SOURCE(ActivePixelSamplingRateShader, VK_SHADER_STAGE_COMPUTE_BIT, active_pixel_sampling_rate)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_SHARED_FLAGS_INPUT)

        // Outputs
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_OUTPUT)
      END_PARAMETER()
    };

    class CompactActivePixelsShader : public ManagedShader {
      SHADER_SOURCE(CompactActivePixelsShader, VK_SHADER_STAGE_COMPUTE_BIT, compact_active_pixels)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        // Inputs
        TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_MASK_INPUT)

        // Outputs
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
      END_PARAMETER()
    };
  }

  SparseRendering::SparseRendering(dxvk::DxvkDevice* device)
    : CommonDeviceObject(device)
    , RtxPass(device) {
  }

  namespace {
    // Declining when the destination already holds a value on this layer keeps a rate the user set
    // explicitly intact, and keeps the migration idempotent - a transform that combined the two
    // values would fold its own previous result back in every time a deprecated rate is set again.
    bool migrateSamplingRate(const GenericValue& src, GenericValue& dest, bool destHasExistingValue) {
      if (destHasExistingValue) {
        return false;
      }

      dest.f = src.f;
      return true;
    }
  }

  // Both deprecated rates route here so the order they are attempted in is fixed: with a value on
  // the same layer for each, the direct rate is the one that migrates.
  void SparseRendering::Options::deprecatedSamplingRateOnChange(DxvkDevice* device) {
    bool migrated = directLightingSamplingRate.migrateValuesTo(&samplingRateObject(), migrateSamplingRate);
    migrated |= indirectLightingSamplingRate.migrateValuesTo(&samplingRateObject(), migrateSamplingRate);

    if (migrated) {
      directLightingSamplingRate.clearFromStrongerLayers(RtxOptionLayer::getDefaultLayer());
      indirectLightingSamplingRate.clearFromStrongerLayers(RtxOptionLayer::getDefaultLayer());

      Logger::info("[Deprecated Config] rtx.sparseRendering.directLightingSamplingRate and "
                   "rtx.sparseRendering.indirectLightingSamplingRate have been deprecated, we have migrated them to "
                   "rtx.sparseRendering.samplingRate, no further action is required from you. "
                   "Please re-save your rtx config to get rid of this message.");
    }
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

    // ReSTIR GI would have to apply bsdfFactor2.x to inactive pixels as well, which the sparse path does not do.
    if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
      ONCE(Logger::warn("[RTX Sparse Rendering] ReSTIR GI indirect illumination is not supported with sparse rendering; sparse rendering will not run. "
                        "It will resume automatically when another indirect illumination mode is selected."));
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

    // The secondary (PSR) path runs dense: demodulate scales only the primary signals by the sampling
    // rate, and composite reads secondary radiance regardless of the active-pixel mask, so letting
    // secondary pixels go sparse would leave that radiance uncompensated. Force off.
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

    rtOutput.m_sparseRenderingActiveLocalPixelCoords =
      Resources::createImageResource(ctx, "Sparse Rendering Active Local Coords", downscaledExtent, VK_FORMAT_R16_UINT);
    rtOutput.m_sparseRenderingPixelSamplingRate =
      Resources::createImageResource(ctx, "Sparse Rendering Pixel Sampling Rate", downscaledExtent, VK_FORMAT_R8_UNORM);

    const VkExtent3D blockSize = { ACTIVE_PIXEL_MASK_BLOCK_WIDTH, ACTIVE_PIXEL_MASK_BLOCK_HEIGHT, 1u };
    const VkExtent3D maskExtent = util::computeBlockCount(downscaledExtent, blockSize);
    m_activePixelMaskExtent = maskExtent;
    rtOutput.m_sparseRenderingActivePixelMask =
      Resources::createImageResource(ctx, "Sparse Rendering Active Pixel Mask", maskExtent, VK_FORMAT_R8_UINT);
  }

  void SparseRendering::releaseDownscaledResource() {
    Resources::RaytracingOutput& rtOutput = device()->getCommon()->getResources().getRaytracingOutput();
    rtOutput.m_sparseRenderingActiveLocalPixelCoords.reset();
    rtOutput.m_sparseRenderingPixelSamplingRate.reset();
    rtOutput.m_sparseRenderingActivePixelMask.reset();
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
    if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
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
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_OUTPUT, rtOutput.m_sparseRenderingPixelSamplingRate.view, nullptr);

    const VkExtent3D& extent = rtOutput.m_compositeOutputExtent;
    const VkExtent3D groupSize = { ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1u };
    const VkExtent3D workgroups = util::computeBlockCount(extent, groupSize);
    ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void SparseRendering::dispatchActivePixelMask(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(&ctx, "Active Pixel Mask");

    // Build per-tile bit masks recording the active pixels.
    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, ActivePixelMaskShader::getShader());

    // Inputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_PIXEL_SAMPLING_RATE_INPUT, rtOutput.m_sparseRenderingPixelSamplingRate.view, nullptr);

    // Outputs
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_ACTIVE_PIXEL_MASK_OUTPUT, rtOutput.m_sparseRenderingActivePixelMask.view, nullptr);

    const VkExtent3D& maskExtent = m_activePixelMaskExtent;
    const VkExtent3D maskGroupSize = { ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_WIDTH, ACTIVE_PIXEL_MASK_THREADGROUP_SIZE_HEIGHT, 1u };
    const VkExtent3D maskWorkgroups = util::computeBlockCount(maskExtent, maskGroupSize);
    ctx.dispatch(maskWorkgroups.width, maskWorkgroups.height, maskWorkgroups.depth);
  }

  void SparseRendering::dispatchCompactActivePixels(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(&ctx, "Compact Active Pixels");

    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, CompactActivePixelsShader::getShader());

    // Inputs
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_MASK_INPUT, rtOutput.m_sparseRenderingActivePixelMask.view, nullptr);

    // Outputs
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT,
                         rtOutput.m_sparseRenderingActiveLocalPixelCoords.view, nullptr);

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

    args.pixelSamplingRate = Options::samplingRate();

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

    RemixGui::DragFloat("Sampling Rate", &Options::samplingRateObject(), 0.01f, 1.0f / 128.0f, 1.0f, "%.3f", sliderFlags);

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
