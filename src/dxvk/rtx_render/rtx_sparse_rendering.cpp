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
#include "rtx_scene_manager.h"
#include "rtx_light_manager.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_spatially_hashed_radiance_cache.h"
#include "rtx_ray_reconstruction.h"
#include "rtx_options.h"
#include "rtx_shader_manager.h"
#include "rtx_imgui.h"

#include <algorithm>
#include <cmath>

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

        // Input-Outputs
        RW_TEXTURE2D(ACTIVE_PIXEL_MASK_BINDING_RADIANCE_CACHE_UPDATE_QUERY_RESERVOIR_INPUT_OUTPUT)

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

        // Inputs/Outputs
        RW_STRUCTURED_BUFFER(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COUNT_INPUT_OUTPUT)

        // Outputs
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT)
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_COMPACTED_PIXEL_INDICES_OUTPUT)
        RW_TEXTURE2D(COMPACT_ACTIVE_PIXELS_BINDING_TILE_ACTIVE_COUNTS_OUTPUT)
        RW_STRUCTURED_BUFFER(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COORDS_OUTPUT)
        RW_STRUCTURED_BUFFER(COMPACT_ACTIVE_PIXELS_BINDING_TRACE_RAYS_ARGS_OUTPUT)
        RW_STRUCTURED_BUFFER(COMPACT_ACTIVE_PIXELS_BINDING_DISPATCH_ARGS_OUTPUT)
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

    // Runs before the capacity check, because unsupported devices also get zero capacity
    // and this check logs the real reason.
    if (!checkCompactActivePixelsRequirements()) {
      return false;
    }

    // The capacity is zero when the compacted storage cannot be packed into the render extent
    // or exceeds the device's launch limits. See calculateCompactedStorageLayout.
    if (device()->getCommon()->getResources().getCompactedStorageCapacity() == 0) {
      ONCE(Logger::warn("[RTX Sparse Rendering] The compacted GBuffer does not fit the render resolution at this sampling rate; sparse rendering will not run. "
                        "It will resume automatically at a lower sampling rate or a higher render resolution."));
      return false;
    }

    return true;
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

    // The RTXDI and integrate passes launch indirectly over the active-pixel list.
    if (!device()->features().khrDeviceRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect) {
      ONCE(Logger::warn("[RTX Sparse Rendering] Device does not support indirect ray dispatch. Sparse rendering will be disabled."));
      return false;
    }
    return true;
  }

  namespace {
    // Bounds how far the blue noise table departs from a perfect rank distribution.
    // The worst case over every level and render extent is 0.00254, which this rounds up.
    // Re-measure it if the blue noise table is replaced.
    constexpr double kBlueNoiseDistributionBound = 0.003;

    // Bounds the per-frame chance that a random selection overflows the capacity.
    // An overflow only drops an evenly spread subset of the active pixels.
    constexpr double kCapacityOverflowProbability = 1e-9;

    // Must match quantizeSamplingRate() in per_pixel_rate.slangh.
    uint32_t quantizeSamplingRate(const float samplingRate) {
      return uint32_t(std::round(std::clamp(samplingRate, 0.0f, 1.0f) * 255.0f));
    }

    // Returns the smallest delta that keeps P(count >= (1 + delta) * mu) under kCapacityOverflowProbability,
    // for a sum of independent Bernoulli trials whose expected total is at most mu.
    // Solves the Chernoff bound exp(-delta^2 * mu / (2 + delta)) for delta.
    double calculateChernoffMargin(const double mu) {
      if (mu <= 0.0) {
        return 0.0;
      }

      const double t = std::log(1.0 / kCapacityOverflowProbability);
      return (t + std::sqrt(t * t + 8.0 * mu * t)) / (2.0 * mu);
    }
  }

  float SparseRendering::calculateCapacityRate(const uint32_t pixelCount) {
    const bool blueNoise = Options::perPixelRateNoiseSource() == PerPixelRateNoiseSource::BlueNoise128x128x64x8;
    const uint32_t level = quantizeSamplingRate(Options::samplingRate());

    // With blue noise the rate is exact, because every pixel is tested against one level,
    // which passes level + 1 of the 256 byte thresholds.
    if (blueNoise) {
      return float(std::min(1.0, (level + 1u) / 256.0 + kBlueNoiseDistributionBound));
    }

    // White noise passes each pixel independently, with probability equal to its quantized rate.
    const double rate = level / 255.0;
    return float(std::min(1.0, rate * (1.0 + calculateChernoffMargin(pixelCount * rate))));
  }

  SparseRendering::CompactedStorageLayout SparseRendering::calculateCompactedStorageLayout(const VkExtent3D& downscaledExtent) const {
    CompactedStorageLayout layout;

    const uint32_t pixelCount = downscaledExtent.width * downscaledExtent.height;

    // Without sparse rendering a capacity of zero keeps the resources at the render extent,
    // and a row length of one keeps the storage mapping defined for any caller that reaches it regardless.
    layout.extent = downscaledExtent;
    layout.squaresPerRow = 1u;
    layout.capacity = 0u;

    if (!isEnabledAndSupported()) {
      return layout;
    }

    constexpr uint32_t kSquareSize = COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_SIZE;
    constexpr uint32_t kSquareArea = COMPACT_ACTIVE_PIXELS_STORAGE_SQUARE_AREA;

    // Quantized to whole squares so that nudging the capacity rate only changes the layout
    // when it actually changes the allocation. The resources are recreated whenever this function's result differs.
    const float capacityRate = calculateCapacityRate(pixelCount);
    const uint32_t requestedCapacity = uint32_t(std::ceil(pixelCount * capacityRate));
    const uint32_t usableSquares = std::max(1u, (requestedCapacity + kSquareArea - 1u) / kSquareArea);

    // Resources that stay at the render extent to alias are addressed at storage coordinates,
    // so the storage must not outgrow it in either axis.
    const uint32_t maxSquaresPerRow = downscaledExtent.width / kSquareSize;
    const uint32_t maxSquareRows = downscaledExtent.height / kSquareSize;

    // A square past the capacity absorbs stores from threads with no slot.
    const uint32_t requiredSquares = usableSquares + 1u;

    // When the render extent cannot hold them, packing would drop active pixels, so sparse rendering stays off instead.
    // See isEnabled. Only very high sampling rates or tiny render extents get here.
    if (requiredSquares > maxSquaresPerRow * maxSquareRows) {
      return layout;
    }

    // The loop finds the smallest rectangle of whole squares that holds the required squares.
    uint32_t squaresPerRow = 0u;
    uint32_t squareRows = 0u;

    for (uint32_t candidatePerRow = 1u; candidatePerRow <= maxSquaresPerRow; ++candidatePerRow) {
      const uint32_t candidateRows = (requiredSquares + candidatePerRow - 1u) / candidatePerRow;

      if (candidateRows > maxSquareRows) {
        continue;
      }

      if (squaresPerRow == 0u || candidatePerRow * candidateRows < squaresPerRow * squareRows) {
        squaresPerRow = candidatePerRow;
        squareRows = candidateRows;
      }
    }

    assert(squaresPerRow != 0u && "The full width always fits a square count within the render extent.");

    // The last square is the scratch one, and the padding before it is capacity like any other slot.
    const uint32_t capacity = std::min(pixelCount, (squaresPerRow * squareRows - 1u) * kSquareArea);

    // The compacted launches never exceed the capacity, so it bounds what the device has to dispatch.
    if (!checkCompactedLaunchLimits(capacity)) {
      return layout;
    }

    layout.extent = { squaresPerRow * kSquareSize, squareRows * kSquareSize, 1u };
    layout.squaresPerRow = squaresPerRow;
    layout.capacity = capacity;

    return layout;
  }

  void SparseRendering::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    Resources::RaytracingOutput& rtOutput = device()->getCommon()->getResources().getRaytracingOutput();

    rtOutput.m_sparseRenderingActiveLocalPixelCoords =
      Resources::createImageResource(ctx, "Sparse Rendering Active Local Coords", downscaledExtent, VK_FORMAT_R16_UINT);
    // Counted from the tile's base, so 16 bits hold it.
    rtOutput.m_sparseRenderingCompactedPixelIndices =
      Resources::createImageResource(ctx, "Sparse Rendering Compacted Pixel Indices", downscaledExtent, VK_FORMAT_R16_UINT);
    const VkExtent3D tileExtent = util::computeBlockCount(
      downscaledExtent, VkExtent3D { COMPACT_ACTIVE_PIXELS_TILE_SIZE_X, COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y, 1 });
    // The x channel holds the tile's active count, and the y channel holds the start of the tile's run in the compacted storage.
    // Consumers need both values and already pay for one tile-uniform read.
    rtOutput.m_sparseRenderingTileActiveCounts =
      Resources::createImageResource(ctx, "Sparse Rendering Tile Active Counts", tileExtent, VK_FORMAT_R32G32_UINT);
    rtOutput.m_sparseRenderingPixelSamplingRate =
      Resources::createImageResource(ctx, "Sparse Rendering Pixel Sampling Rate", downscaledExtent, VK_FORMAT_R8_UNORM);

    const VkExtent3D blockSize = { ACTIVE_PIXEL_MASK_BLOCK_WIDTH, ACTIVE_PIXEL_MASK_BLOCK_HEIGHT, 1u };
    const VkExtent3D maskExtent = util::computeBlockCount(downscaledExtent, blockSize);
    m_activePixelMaskExtent = maskExtent;
    rtOutput.m_sparseRenderingActivePixelMask =
      Resources::createImageResource(ctx, "Sparse Rendering Active Pixel Mask", maskExtent, VK_FORMAT_R8_UINT);

    allocateActivePixelListBuffers(ctx, rtOutput);
  }

  void SparseRendering::allocateActivePixelListBuffers(Rc<DxvkContext>& ctx, Resources::RaytracingOutput& rtOutput) const {
    DxvkBufferCreateInfo listInfo = {};
    listInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    listInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT;
    listInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

    // An entry holds only the pixel coordinate, because its position in the list is already the global compacted index,
    // which is all the storage coordinate needs. Pixels past the capacity get no entry.
    const uint32_t capacity = device()->getCommon()->getResources().getCompactedStorageCapacity();
    listInfo.size = VkDeviceSize(std::max(1u, capacity)) * sizeof(uint32_t);

    rtOutput.m_sparseRenderingActivePixelCoords = device()->createBuffer(listInfo,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
      "Sparse Rendering Active Pixel Coords");

    DxvkBufferCreateInfo countInfo = listInfo;
    countInfo.size = sizeof(uint32_t) * COMPACT_ACTIVE_PIXELS_COUNT_SLOT_COUNT;
    rtOutput.m_sparseRenderingActivePixelCount = device()->createBuffer(countInfo,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
      "Sparse Rendering Active Pixel Count");

    // The ray tracing args are read through a device address, so they additionally need the address usage bit.
    DxvkBufferCreateInfo argsInfo = {};
    argsInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    argsInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    argsInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                      VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    argsInfo.size = sizeof(VkTraceRaysIndirectCommandKHR);
    rtOutput.m_sparseRenderingTraceRaysIndirectArgs = device()->createBuffer(argsInfo,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
      "Sparse Rendering Trace Rays Indirect Args");

    argsInfo.size = sizeof(VkDispatchIndirectCommand);
    rtOutput.m_sparseRenderingDispatchIndirectArgs = device()->createBuffer(argsInfo,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer,
      "Sparse Rendering Dispatch Indirect Args");

    // A launch reading these before the args pass has written them would otherwise take uninitialized device memory
    // as its dimensions, which hangs the GPU rather than failing visibly.
    ctx->clearBuffer(rtOutput.m_sparseRenderingTraceRaysIndirectArgs, 0, rtOutput.m_sparseRenderingTraceRaysIndirectArgs->info().size, 0);
    ctx->clearBuffer(rtOutput.m_sparseRenderingDispatchIndirectArgs, 0, rtOutput.m_sparseRenderingDispatchIndirectArgs->info().size, 0);
    ctx->clearBuffer(rtOutput.m_sparseRenderingActivePixelCount, 0, rtOutput.m_sparseRenderingActivePixelCount->info().size, 0);
  }

  void SparseRendering::releaseDownscaledResource() {
    Resources::RaytracingOutput& rtOutput = device()->getCommon()->getResources().getRaytracingOutput();
    rtOutput.m_sparseRenderingActiveLocalPixelCoords.reset();
    rtOutput.m_sparseRenderingCompactedPixelIndices.reset();
    rtOutput.m_sparseRenderingTileActiveCounts.reset();
    rtOutput.m_sparseRenderingPixelSamplingRate.reset();
    rtOutput.m_sparseRenderingActivePixelMask.reset();
    rtOutput.m_sparseRenderingActivePixelCoords = nullptr;
    rtOutput.m_sparseRenderingActivePixelCount = nullptr;
    rtOutput.m_sparseRenderingTraceRaysIndirectArgs = nullptr;
    rtOutput.m_sparseRenderingDispatchIndirectArgs = nullptr;
    m_activePixelMaskExtent = { 0u, 0u, 0u };
  }

  // The compacted launches are sized from a GPU-side count, so the limits have to hold for the largest count possible.
  bool SparseRendering::checkCompactedLaunchLimits(const uint32_t maxLaunchPixels) const {
    const auto& limits = device()->properties().core.properties.limits;
    const uint32_t requiredGroups = dxvk::util::ceilDivide(maxLaunchPixels, uint32_t(COMPACT_ACTIVE_PIXELS_CONSUMER_GROUP_SIZE));

    if (requiredGroups > limits.maxComputeWorkGroupCount[0]) {
      ONCE(Logger::warn(str::format("[RTX Sparse Rendering] The compacted launch needs ", requiredGroups,
                                    " compute workgroups but the device allows ", limits.maxComputeWorkGroupCount[0],
                                    "; sparse rendering will not run.")));
      return false;
    }

    // Trace rays counts invocations, and its width is capped by the compute group count times the group size.
    const uint64_t maxTraceRaysWidth = uint64_t(limits.maxComputeWorkGroupCount[0]) * limits.maxComputeWorkGroupSize[0];
    if (uint64_t(maxLaunchPixels) > maxTraceRaysWidth) {
      ONCE(Logger::warn(str::format("[RTX Sparse Rendering] The compacted launch needs an indirect ray dispatch width of ", maxLaunchPixels,
                                    " but the device allows ", maxTraceRaysWidth, "; sparse rendering will not run.")));
      return false;
    }

    return true;
  }

  bool SparseRendering::isEnabledByOptions() {
    if (!Options::enableSparseRendering()) {
      return false;
    }
    // See SparseRendering::isEnabled() for explanation on why these checks are necessary.
    if (!RtxOptions::isRayReconstructionEnabled()) {
      return false;
    }
    if (RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI) {
      return false;
    }
    return true;
  }

  bool SparseRendering::isEnabledAndSupported() const {
    return isEnabledByOptions() &&
           device()->getCommon()->metaRayReconstruction().useRayReconstruction() &&
           checkCompactActivePixelsRequirements();
  }

  bool SparseRendering::resamplesNrcTrainingPaths(const bool nrcIsActive) const {
    // Deliberately not gated on NrcOptions::trainCache(): update rows are dispatched regardless of it, and they
    // rely on this staying true to resolve their GBuffer pixel through the query-pixel map.
    return nrcIsActive && isActive();
  }

  void SparseRendering::dispatchCompactedConsumer(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput, const VkExtent3D& workgroups) {
    if (rtOutput.m_raytraceArgs.sparseRenderingArgs.mode != SparseRenderingMode::Off) {
      ctx.bindDrawBuffers(
        DxvkBufferSlice(rtOutput.m_sparseRenderingDispatchIndirectArgs, 0, sizeof(VkDispatchIndirectCommand)),
        DxvkBufferSlice());
      ctx.dispatchIndirect(0);
    } else {
      ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
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

    // Input-Outputs
    // Radiance cache update query-pixel reservoir, accumulated over active pixels. Only allocated while update-path
    // resampling is on, which is also the only case the shader touches it.
    Rc<DxvkImageView> updateQueryReservoirView = nullptr;
    if (rtOutput.m_raytraceArgs.sparseRenderingArgs.resampledNrcTrainingPaths) {
      updateQueryReservoirView = ctx.getCommonObjects()->metaNeuralRadianceCache().getTrainingQueryReservoir().view;
    } else if (rtOutput.m_raytraceArgs.sparseRenderingArgs.resampledSharcUpdatePaths) {
      updateQueryReservoirView = ctx.getCommonObjects()->metaSpatiallyHashedRadianceCache().getUpdateQueryReservoir().view;
    }
    ctx.bindResourceView(ACTIVE_PIXEL_MASK_BINDING_RADIANCE_CACHE_UPDATE_QUERY_RESERVOIR_INPUT_OUTPUT, updateQueryReservoirView, nullptr);

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

    // Inputs/Outputs
    ctx.bindResourceBuffer(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COUNT_INPUT_OUTPUT,
                           optionalBufferSlice(rtOutput.m_sparseRenderingActivePixelCount));

    // Outputs
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_LOCAL_PIXEL_COORDS_OUTPUT,
                         rtOutput.m_sparseRenderingActiveLocalPixelCoords.view, nullptr);
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_COMPACTED_PIXEL_INDICES_OUTPUT,
                         rtOutput.m_sparseRenderingCompactedPixelIndices.view, nullptr);
    ctx.bindResourceView(COMPACT_ACTIVE_PIXELS_BINDING_TILE_ACTIVE_COUNTS_OUTPUT,
                         rtOutput.m_sparseRenderingTileActiveCounts.view, nullptr);

    ctx.bindResourceBuffer(COMPACT_ACTIVE_PIXELS_BINDING_ACTIVE_PIXEL_COORDS_OUTPUT,
                           optionalBufferSlice(rtOutput.m_sparseRenderingActivePixelCoords));
    ctx.bindResourceBuffer(COMPACT_ACTIVE_PIXELS_BINDING_TRACE_RAYS_ARGS_OUTPUT,
                           optionalBufferSlice(rtOutput.m_sparseRenderingTraceRaysIndirectArgs));
    ctx.bindResourceBuffer(COMPACT_ACTIVE_PIXELS_BINDING_DISPATCH_ARGS_OUTPUT,
                           optionalBufferSlice(rtOutput.m_sparseRenderingDispatchIndirectArgs));

    const VkExtent3D extent = rtOutput.m_compositeOutputExtent;
    const VkExtent3D tileSize = { COMPACT_ACTIVE_PIXELS_TILE_SIZE_X, COMPACT_ACTIVE_PIXELS_TILE_SIZE_Y, 1 };
    const VkExtent3D workgroups = util::computeBlockCount(extent, tileSize);

    ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void SparseRendering::setSparseRenderingArgs(RtxContext& ctx, SparseRenderingArgs& args) const {
    args.mode = isActive() ? SparseRenderingMode::Uniform : SparseRenderingMode::Off;

    args.perPixelRateNoiseSource = Options::perPixelRateNoiseSource();
    args.enableSparsePrimaryRayMissComposition = Options::enableSparsePrimaryRayMissComposition();
    args.enableSparseVolumetricsPrimaryHit = Options::enableSparseVolumetricsPrimaryHit();
    args.enableSparseVolumetricsPrimaryMiss = Options::enableSparseVolumetricsPrimaryMiss();
    args.enableLightIdentityResolution = ctx.getSceneManager().getLightManager().isLightIdentityTableBuilt() ? 1u : 0u;

    NeuralRadianceCache& nrc = ctx.getCommonObjects()->metaNeuralRadianceCache();
    args.resampledNrcTrainingPaths = resamplesNrcTrainingPaths(nrc.isActive());
    SpatiallyHashedRadianceCache& sharc = ctx.getCommonObjects()->metaSpatiallyHashedRadianceCache();
    args.resampledSharcUpdatePaths =
      isActive() && sharc.isActive() && SpatiallyHashedRadianceCache::SharcOptions::enableUpdate();

    args.pixelSamplingRate = Options::samplingRate();

    args.activePixelMaskExtent = { m_activePixelMaskExtent.width, m_activePixelMaskExtent.height };

    // The compacted GBuffer builds the albedo guides itself with prepare RR's settings.
    // A multiplier of 1 and an offset of 0 leave the roughness unchanged.
    args.guideCombineSpecularAlbedo = DxvkRayReconstruction::combineSpecularAlbedo();
    const bool demodulateRoughness = DxvkRayReconstruction::demodulateRoughness();
    args.guideRoughnessDemodulationMultiplier = demodulateRoughness
      ? DxvkRayReconstruction::upscalerRoughnessDemodulationMultiplier()
      : 1.0f;
    args.guideRoughnessDemodulationOffset = demodulateRoughness
      ? DxvkRayReconstruction::upscalerRoughnessDemodulationOffset()
      : 0.0f;

    // These fields describe the allocated storage layout. See getCompactedStorageCapacity.
    args.compactedStorageSquaresPerRow = ctx.getResourceManager().getCompactedStorageSquaresPerRow();
    args.compactedStorageCapacity = ctx.getResourceManager().getCompactedStorageCapacity();
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
      RemixGui::Checkbox("Sparse Primary Ray Miss Composition", &Options::enableSparsePrimaryRayMissCompositionObject());
      RemixGui::Checkbox("Sparse Volumetrics (Primary Hit)", &Options::enableSparseVolumetricsPrimaryHitObject());
      RemixGui::Checkbox("Sparse Volumetrics (Primary Miss)", &Options::enableSparseVolumetricsPrimaryMissObject());
      s_perPixelRateNoiseSourceCombo.getKey(&Options::perPixelRateNoiseSourceObject());

      ImGui::Unindent();
    }

    ImGui::EndDisabled();
  }

} // namespace dxvk
