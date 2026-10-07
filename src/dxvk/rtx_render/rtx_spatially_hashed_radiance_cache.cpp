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
// ToDo remove unneeded headers
#include <algorithm>
#include <cstring>

#include "dxvk_device.h"
#include "rtx.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx_camera.h"
#include "rtx_debug_view.h"
#include "rtx_sparse_rendering.h"

#include "rtx_spatially_hashed_radiance_cache.h"
#include "rtx/pass/sharc/sharc_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"

#include <rtx_shaders/sharc_resolve.h>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class SharcResolveShader : public ManagedShader {
      SHADER_SOURCE(SharcResolveShader, VK_SHADER_STAGE_COMPUTE_BIT, sharc_resolve)

      BEGIN_PARAMETER()
        CONSTANT_BUFFER(SHARC_BINDING_CONSTANTS_INPUT)

        RW_STRUCTURED_BUFFER(SHARC_BINDING_HASH_ENTRIES_INPUT_OUTPUT)
        RW_STRUCTURED_BUFFER(SHARC_BINDING_ACCUMULATION_OUTPUT)
        RW_STRUCTURED_BUFFER(SHARC_BINDING_RESOLVED_INPUT_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(SharcResolveShader);
  }

  SpatiallyHashedRadianceCache::SpatiallyHashedRadianceCache(dxvk::DxvkDevice* device) : RtxPass(device), m_device(device) { }

  SpatiallyHashedRadianceCache::~SpatiallyHashedRadianceCache() { }

  bool SpatiallyHashedRadianceCache::checkIsSupported(const DxvkDevice* device) {
    return device->features().core.features.shaderInt64
        && device->features().vulkan12Features.shaderBufferInt64Atomics;
  }


  bool SpatiallyHashedRadianceCache::isResettingHistory() {
    return m_resetHistory;
  }

  void SpatiallyHashedRadianceCache::setRaytraceArgs(RtxContext& ctx, RaytraceArgs& constants) {
    if (!isActive()) {
      return;
    }

    SceneManager& sceneManager = ctx.getSceneManager();
    const RtCamera& camera = sceneManager.getCamera();

    SharcArgs& sharcArgs = constants.sharcArgs;

    sharcArgs.cameraPosition = camera.getPosition();
    sharcArgs.cameraPositionPrev = camera.getPreviousPosition();

    sharcArgs.accumulationFrameNum = SharcOptions::maxNumFramesToAccumulate();
    sharcArgs.staleFrameNum = SharcOptions::staleFrameNumber();
    sharcArgs.radianceScale = 1e3f;
    sharcArgs.entriesNum = m_numEntries;
    sharcArgs.fireflyClampMultiplier = SharcOptions::fireflyClampMultiplier();

    sharcArgs.sceneScale = 1.f / std::max(SharcOptions::voxelSize(), 1e-6f);
    sharcArgs.isotropicRoughnessThreshold = pow(SharcOptions::perceptualRoughnessThreshold(), 2);
    // disabling russian roulette to match NRC
    sharcArgs.updateAllowRussianRoulette = false;

    // The update grid maps to query pixels the same way NRC's training grid does. See NeuralRadianceCache::setRaytraceArgs.
    const Vector2 numQueryPixelsPerUpdatePixel = Vector2 {
      m_renderDimensions.width / static_cast<float>(m_activeUpdateDimensions.width),
      m_renderDimensions.height / static_cast<float>(m_activeUpdateDimensions.height) };

    sharcArgs.activeUpdateDimensions = vec2 {
      static_cast<float>(m_activeUpdateDimensions.width),
      static_cast<float>(m_activeUpdateDimensions.height) };

    sharcArgs.numRowsForUpdate = isUpdateCombinedWithQuery() ? calculateNumRowsForUpdate() : 0;

    sharcArgs.queryToUpdateCoordinateSpace = vec2 {
      1.f / numQueryPixelsPerUpdatePixel.x,
      1.f / numQueryPixelsPerUpdatePixel.y };

    sharcArgs.updateToQueryCoordinateSpace = vec2 {
      numQueryPixelsPerUpdatePixel.x,
      numQueryPixelsPerUpdatePixel.y };

    // The jitter stays half a query pixel, plus a margin, away from the update cell's edges,
    // so the query pixel it selects maps back to the same update cell.
    const double halfQueryPixel = 0.5 + 0.001;
    const Vector2Base<double> updatePixelInnerBounds = Vector2Base<double> {
      (halfQueryPixel / m_renderDimensions.width) * m_activeUpdateDimensions.width,
      (halfQueryPixel / m_renderDimensions.height) * m_activeUpdateDimensions.height };

    if (SharcOptions::jitterSequenceLength()) {
      const uint32_t currentFrameIndex = ctx.getDevice()->getCurrentFrameId();
      const Vector2 jitter05 = calculateHaltonJitter(currentFrameIndex, SharcOptions::jitterSequenceLength());
      const Vector2 rand01 = Vector2 { jitter05.x + 0.5f, jitter05.y + 0.5f };
      const Vector2Base<double> jitterRange = Vector2Base<double> {
        1 - 2 * updatePixelInnerBounds.x,
        1 - 2 * updatePixelInnerBounds.y };

      sharcArgs.updatePixelJitter = vec2 {
        static_cast<float>(updatePixelInnerBounds.x + jitterRange.x * rand01.x),
        static_cast<float>(updatePixelInnerBounds.y + jitterRange.y * rand01.y) };
    } else {
      sharcArgs.updatePixelJitter = vec2 {
        static_cast<float>(updatePixelInnerBounds.x),
        static_cast<float>(updatePixelInnerBounds.y) };
    }
  }

  // ToDo: rename to calc*
  VkExtent3D SpatiallyHashedRadianceCache::calcRaytracingResolution(const VkExtent3D& raytracingResolution, bool isUpdate) const {
    assert(isActive() && "This requires SHaRC to be enabled and onFrameStart() to have been called prior.");

    if (isUpdate) {
      return VkExtent3D { m_activeUpdateDimensions.width, m_activeUpdateDimensions.height, 1 };
    }

    // Like NRC's combined dispatch, the update rows come before the query rows.
    if (isUpdateCombinedWithQuery()) {
      return VkExtent3D { m_renderDimensions.width, m_renderDimensions.height + calculateNumRowsForUpdate(), 1 };
    }

    return raytracingResolution;
  }

  bool SpatiallyHashedRadianceCache::isUpdateCombinedWithQuery() const {
    return isActive()
        && SharcOptions::combineUpdateAndQuery()
        && SharcOptions::enableUpdate();
  }

  uint32_t SpatiallyHashedRadianceCache::calculateNumRowsForUpdate() const {
    const uint32_t numUpdatePixels = m_activeUpdateDimensions.width * m_activeUpdateDimensions.height;

    // Rounding up to the integrate indirect workgroup height keeps update and query threads in separate workgroups.
    return align(divCeil(numUpdatePixels, m_renderDimensions.width), 8u);
  }

  VkExtent2D SpatiallyHashedRadianceCache::calculateMinUpdateDimensions() const {
    // An update cell may not span more query pixels than the update query reservoir can address by offset.
    return VkExtent2D {
      divCeil(m_renderDimensions.width, SHARC_MAX_QUERY_PIXELS_PER_UPDATE_PIXEL_PER_AXIS),
      divCeil(m_renderDimensions.height, SHARC_MAX_QUERY_PIXELS_PER_UPDATE_PIXEL_PER_AXIS) };
  }

  VkExtent2D SpatiallyHashedRadianceCache::calculateMaxUpdateDimensions() const {
    VkExtent2D maxUpdateDimensions;

    if (SharcOptions::targetNumUpdateRecords() > 0) {
      // The primary vertex adds one record per path, which the target may leave out.
      const float averageTargetedRecordsPerPath = SharcOptions::excludePrimaryVerticesFromTarget()
        ? SharcOptions::averageUpdateRecordsPerPath() - 1.f
        : SharcOptions::averageUpdateRecordsPerPath();

      // Scales both render dimensions by the same factor, down to the number of paths expected to generate the target.
      const float numPaths =
        SharcOptions::targetNumUpdateRecords() / std::max(averageTargetedRecordsPerPath, 0.01f);
      const float scale =
        std::min(sqrtf(numPaths / (static_cast<float>(m_renderDimensions.width) * m_renderDimensions.height)), 1.f);

      maxUpdateDimensions = VkExtent2D {
        static_cast<uint32_t>(ceil(scale * m_renderDimensions.width)),
        static_cast<uint32_t>(ceil(scale * m_renderDimensions.height)) };
    } else {
      const uint32_t downscaleFactor = std::max(SharcOptions::downscaleFactor(), 1u);

      maxUpdateDimensions = VkExtent2D {
        divCeil(m_renderDimensions.width, downscaleFactor),
        divCeil(m_renderDimensions.height, downscaleFactor) };
    }

    const VkExtent2D minUpdateDimensions = calculateMinUpdateDimensions();

    return VkExtent2D {
      std::clamp(maxUpdateDimensions.width, minUpdateDimensions.width, m_renderDimensions.width),
      std::clamp(maxUpdateDimensions.height, minUpdateDimensions.height, m_renderDimensions.height) };
  }

  void SpatiallyHashedRadianceCache::copyUpdateCounters(RtxContext& ctx) {
    const VkDeviceSize countersSize = SHARC_UPDATE_COUNTER_COUNT * sizeof(uint32_t);
    const uint32_t entryIdx = m_device->getCurrentFrameId() % kMaxFramesInFlight;

    ctx.copyBuffer(
      m_updateCountersReadback, entryIdx * countersSize,
      m_updateCounters, 0, countersSize);
  }

  void SpatiallyHashedRadianceCache::readAndResetUpdateCounters() {
    // The oldest entry is the one the GPU is guaranteed to have written by now.
    const VkDeviceSize countersSize = SHARC_UPDATE_COUNTER_COUNT * sizeof(uint32_t);
    const uint32_t entryIdx = m_device->getCurrentFrameId() % kMaxFramesInFlight;
    uint32_t* pCounters = reinterpret_cast<uint32_t*>(m_updateCountersReadback->mapPtr(entryIdx * countersSize));

    m_numberOfUpdateRecords.store(pCounters[SHARC_UPDATE_COUNTER_RECORDS], std::memory_order_relaxed);
    m_numberOfUpdatePaths.store(pCounters[SHARC_UPDATE_COUNTER_PATHS], std::memory_order_relaxed);

    memset(pCounters, 0, countersSize);
  }

  // Mirrors NeuralRadianceCache::calculateActiveTrainingDimensions.
  void SpatiallyHashedRadianceCache::calculateActiveUpdateDimensions(bool forceReset) {
    readAndResetUpdateCounters();

    // The primary vertex adds one record per path, so leaving primary vertices out subtracts the path count.
    const uint32_t numberOfUpdateRecords = SharcOptions::excludePrimaryVerticesFromTarget()
      ? getNumberOfUpdateRecords() - getNumberOfUpdatePaths()
      : getNumberOfUpdateRecords();

    const uint32_t frameIdx = m_device->getCurrentFrameId();
    const uint32_t numFramesToSmooth = SharcOptions::numFramesToSmoothOutUpdateDimensions();

    // Without a target, the update grid stays at the largest one, which downscaleFactor then sets.
    forceReset |= SharcOptions::targetNumUpdateRecords() == 0;
    forceReset |= m_resetHistory;
    forceReset |= numberOfUpdateRecords == 0;
    forceReset |= numFramesToSmooth <= 1;
    // Frames were skipped since the last reset.
    forceReset |= (frameIdx - m_smoothingResetFrameIdx + 1) > (numFramesToSmooth + kMaxFramesInFlight);

    if (forceReset) {
      // The largest grid usually generates more records than the target, which speeds up convergence after a reset.
      m_activeUpdateDimensions = m_maxUpdateDimensions;

      m_smoothingResetFrameIdx = frameIdx;
      m_smoothedNumberOfUpdateRecords = 0.f;
    } else if (frameIdx - m_smoothingResetFrameIdx >= kMaxFramesInFlight) {
      // Counts the frames whose record count was generated with the current grid, including this frame.
      const uint32_t numSmoothedFrames = frameIdx - m_smoothingResetFrameIdx - kMaxFramesInFlight + 1;

      m_smoothedNumberOfUpdateRecords =
        lerp<float>(m_smoothedNumberOfUpdateRecords, numberOfUpdateRecords, 1.f / numSmoothedFrames);

      if (numSmoothedFrames == numFramesToSmooth) {
        float workloadScale = m_smoothedNumberOfUpdateRecords / SharcOptions::targetNumUpdateRecords();

        // The record count does not scale linearly with the grid.
        // Below the target, the grid grows a little past it to avoid falling short,
        // and above the target, it shrinks slowly to avoid undershooting.
        if (workloadScale < 1.f) {
          workloadScale *= 0.9f;
        } else {
          workloadScale = std::max(1.f, workloadScale * 0.98f);
        }

        const float rcpPerDimensionWorkloadScale = 1.f / sqrtf(workloadScale);
        const VkExtent2D minUpdateDimensions = calculateMinUpdateDimensions();

        const VkExtent2D newActiveUpdateDimensions = VkExtent2D {
          std::clamp(static_cast<uint32_t>(ceil(rcpPerDimensionWorkloadScale * m_activeUpdateDimensions.width)),
                     minUpdateDimensions.width, m_maxUpdateDimensions.width),
          std::clamp(static_cast<uint32_t>(ceil(rcpPerDimensionWorkloadScale * m_activeUpdateDimensions.height)),
                     minUpdateDimensions.height, m_maxUpdateDimensions.height) };

        if (newActiveUpdateDimensions.width != m_activeUpdateDimensions.width ||
            newActiveUpdateDimensions.height != m_activeUpdateDimensions.height) {
          m_activeUpdateDimensions = newActiveUpdateDimensions;

          // The record counts read back over the next kMaxFramesInFlight frames still come from the previous grid.
          m_smoothingResetFrameIdx = frameIdx;
        } else {
          // Keeps the smoothing window the same length.
          m_smoothingResetFrameIdx++;
        }
      }
    }
  }

  bool SpatiallyHashedRadianceCache::isEnabled() const {
    return checkIsSupported(m_device)
        && RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::SHaRC;
  }

  void SpatiallyHashedRadianceCache::onFrameBegin(
    Rc<DxvkContext>& ctx,
    const FrameBeginContext& frameBeginCtx) {

    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    if (!isActive()) {
      return;
    }

    m_resetHistory = m_resetHistory || SharcOptions::resetHistory() || frameBeginCtx.resetHistory;

    // Size the update grid.
    {
      const VkExtent2D renderDimensions = VkExtent2D { frameBeginCtx.downscaledExtent.width, frameBeginCtx.downscaledExtent.height };

      if (renderDimensions.width != m_renderDimensions.width || renderDimensions.height != m_renderDimensions.height) {
        m_resetHistory = true;
        m_renderDimensions = renderDimensions;
      }

      const VkExtent2D prevMaxUpdateDimensions = m_maxUpdateDimensions;
      m_maxUpdateDimensions = calculateMaxUpdateDimensions();

      const bool haveMaxUpdateDimensionsChanged =
        m_maxUpdateDimensions.width != prevMaxUpdateDimensions.width ||
        m_maxUpdateDimensions.height != prevMaxUpdateDimensions.height;

      calculateActiveUpdateDimensions(haveMaxUpdateDimensionsChanged);
    }

    // Allocate resources if they are invalid or have stale dimensions.
    // They are sized for the largest update grid, so the active grid can change without reallocating them.
    const VkExtent3D maxUpdateExtent = VkExtent3D { m_maxUpdateDimensions.width, m_maxUpdateDimensions.height, 1 };
    const bool sparseUpdateResampling =
      SparseRendering::isEnabledByOptions() && SharcOptions::enableUpdate();

    if (m_trainingGBufferSurfaceRadianceRG.image == nullptr
        || m_trainingGBufferSurfaceRadianceRG.image->info().extent.width != maxUpdateExtent.width
        || m_trainingGBufferSurfaceRadianceRG.image->info().extent.height != maxUpdateExtent.height) {

      m_trainingGBufferSurfaceRadianceRG = Resources::createImageResource(ctx, "SHaRC update shared radiance RG", maxUpdateExtent, VK_FORMAT_R16G16_SFLOAT);
      m_trainingGBufferSurfaceRadianceB = Resources::createImageResource(ctx, "SHaRC update shared radiance B", maxUpdateExtent, VK_FORMAT_R16_SFLOAT);
    }

    if (!sparseUpdateResampling) {
      m_updateQueryReservoir.reset();
    } else if (m_updateQueryReservoir.image == nullptr
        || m_updateQueryReservoir.image->info().extent.width != maxUpdateExtent.width
        || m_updateQueryReservoir.image->info().extent.height != maxUpdateExtent.height) {
      m_updateQueryReservoir = Resources::createImageResource(ctx, "SHaRC Update Query Reservoir", maxUpdateExtent, VK_FORMAT_R32_UINT);
    }

    ctx->clearBuffer(m_updateCounters, 0, m_updateCounters->info().size, 0);

    if (m_updateQueryReservoir.image != nullptr) {
      VkImageSubresourceRange subRange = {};
      subRange.layerCount = 1;
      subRange.levelCount = 1;
      subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ctx->clearColorImage(m_updateQueryReservoir.image, VkClearColorValue {}, subRange);
    }

    if (m_resetHistory || !SharcOptions::enableResolve()) {
      ctx->clearBuffer(m_accumulation, 0, m_accumulation->info().size, 0);
    }

    if (m_resetHistory) {
      ctx->clearBuffer(m_resolved, 0, m_resolved->info().size, 0);
      ctx->clearBuffer(m_hashEntries, 0, m_hashEntries->info().size, 0);

      SharcOptions::resetHistory.setImmediately(false);
    }
  }

  void SpatiallyHashedRadianceCache::createSharcBuffers(Rc<DxvkContext>& ctx) {

    // Set up buffer create info
    DxvkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    {
      // VK_BUFFER_USAGE_TRANSFER_DST_BIT is for clear support
      bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      bufferCreateInfo.stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT;
      bufferCreateInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
   
    bufferCreateInfo.size = m_numEntries * sizeof(uint64_t);
    m_hashEntries = ctx->getDevice()->createBuffer(bufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "SHaRC Hash Entries Buffer");

    bufferCreateInfo.size = m_numEntries * sizeof(uvec4);
    m_accumulation = ctx->getDevice()->createBuffer(bufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "SHaRC Voxel Data Buffer 0");
    m_resolved = ctx->getDevice()->createBuffer(bufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "SHaRC Voxel Data Buffer 1");

    // The counters are copied out for readback, so they are also a transfer source.
    bufferCreateInfo.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.access |= VK_ACCESS_TRANSFER_READ_BIT;
    bufferCreateInfo.size = SHARC_UPDATE_COUNTER_COUNT * sizeof(uint32_t);
    m_updateCounters = ctx->getDevice()->createBuffer(bufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "SHaRC Update Counters");

    DxvkBufferCreateInfo readbackCreateInfo = {};
    readbackCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    readbackCreateInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    readbackCreateInfo.access = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    readbackCreateInfo.size = kMaxFramesInFlight * SHARC_UPDATE_COUNTER_COUNT * sizeof(uint32_t);
    m_updateCountersReadback = ctx->getDevice()->createBuffer(readbackCreateInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXBuffer, "SHaRC Update Counters Readback");
    memset(m_updateCountersReadback->mapPtr(0), 0, readbackCreateInfo.size);
  }

  bool SpatiallyHashedRadianceCache::onActivation(Rc<DxvkContext>& ctx) {

    m_resetHistory = true;
    createSharcBuffers(ctx);

    return true;
  }

  void SpatiallyHashedRadianceCache::onDeactivation() {
    m_trainingGBufferSurfaceRadianceRG.reset();
    m_trainingGBufferSurfaceRadianceB.reset();
    m_updateQueryReservoir.reset();

    m_hashEntries = nullptr;
    m_accumulation = nullptr;
    m_resolved = nullptr;
    m_updateCounters = nullptr;
    m_updateCountersReadback = nullptr;
  }

  void SpatiallyHashedRadianceCache::bindGBufferPathTracingResources(RtxContext& ctx) {
    if (isActive()) {
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_HASH_ENTRIES_INPUT_OUTPUT, DxvkBufferSlice(m_hashEntries));
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_ACCUMULATION_INPUT_OUTPUT, DxvkBufferSlice(m_accumulation));
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_RESOLVED_INPUT_OUTPUT, DxvkBufferSlice(m_resolved));

      ctx.bindResourceView(GBUFFER_BINDING_RADIANCE_CACHE_UPDATE_GBUFFER_SURFACE_RADIANCE_RG_OUTPUT, m_trainingGBufferSurfaceRadianceRG.view, nullptr);
      ctx.bindResourceView(GBUFFER_BINDING_RADIANCE_CACHE_UPDATE_GBUFFER_SURFACE_RADIANCE_B_OUTPUT, m_trainingGBufferSurfaceRadianceB.view, nullptr);
      ctx.bindResourceView(GBUFFER_BINDING_RADIANCE_CACHE_UPDATE_QUERY_RESERVOIR_INPUT, m_updateQueryReservoir.view, nullptr);
    } else {
      // If SHaRC is not active, we bind empty buffers to avoid binding issues
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_HASH_ENTRIES_INPUT_OUTPUT, DxvkBufferSlice());
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_ACCUMULATION_INPUT_OUTPUT, DxvkBufferSlice());
      ctx.bindResourceBuffer(GBUFFER_BINDING_BINDING_SHARC_RESOLVED_INPUT_OUTPUT, DxvkBufferSlice());
    }
  }

  void SpatiallyHashedRadianceCache::bindIntegrateIndirectPathTracingResources(RtxContext& ctx) {
    if (isActive()) {
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_HASH_ENTRIES_INPUT_OUTPUT, DxvkBufferSlice(m_hashEntries));
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_ACCUMULATION_INPUT_OUTPUT, DxvkBufferSlice(m_accumulation));
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_RESOLVED_INPUT_OUTPUT, DxvkBufferSlice(m_resolved));

      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RADIANCE_CACHE_UPDATE_GBUFFER_SURFACE_RADIANCE_RG_INPUT, m_trainingGBufferSurfaceRadianceRG.view, nullptr);
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RADIANCE_CACHE_UPDATE_GBUFFER_SURFACE_RADIANCE_B_INPUT, m_trainingGBufferSurfaceRadianceB.view, nullptr);
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_RADIANCE_CACHE_UPDATE_QUERY_RESERVOIR_INPUT, m_updateQueryReservoir.view, nullptr);
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_UPDATE_COUNTERS_INPUT_OUTPUT, DxvkBufferSlice(m_updateCounters));
    } else {
      // If SHaRC is not active, we bind empty buffers to avoid binding issues
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_HASH_ENTRIES_INPUT_OUTPUT, DxvkBufferSlice());
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_ACCUMULATION_INPUT_OUTPUT, DxvkBufferSlice());
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_RESOLVED_INPUT_OUTPUT, DxvkBufferSlice());
      ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_SHARC_UPDATE_COUNTERS_INPUT_OUTPUT, DxvkBufferSlice());
    }
  }

  void SpatiallyHashedRadianceCache::dispatch(
    RtxContext& ctx,
    const Resources::RaytracingOutput& rtOutput) {

    copyUpdateCounters(ctx);

    if (!SharcOptions::enableResolve()) {
      return;
    }

    ScopedGpuProfileZone(&ctx, "SHaRC Update");
    ctx.setFramePassStage(RtxFramePassStage::SHaRCUpdate);

    // Bind resources
    {
      Rc<DxvkBuffer> raytraceArgsBuffer = ctx.getResourceManager().getConstantsBuffer();

      // Inputs 
      ctx.bindResourceBuffer(SHARC_BINDING_CONSTANTS_INPUT, DxvkBufferSlice(raytraceArgsBuffer, 0, raytraceArgsBuffer->info().size));

      // Inputs / Outputs
      ctx.bindResourceBuffer(SHARC_BINDING_HASH_ENTRIES_INPUT_OUTPUT, DxvkBufferSlice(m_hashEntries));
      ctx.bindResourceBuffer(SHARC_BINDING_ACCUMULATION_OUTPUT, DxvkBufferSlice(m_accumulation));
      ctx.bindResourceBuffer(SHARC_BINDING_RESOLVED_INPUT_OUTPUT, DxvkBufferSlice(m_resolved));
    }

    VkExtent3D workgroups = util::computeBlockCount(VkExtent3D { m_numEntries, 1, 1 }, VkExtent3D { SHARC_COMPUTE_LINEAR_BLOCK_SIZE, 1, 1 });

    // Dispatch resolve
    {
      ScopedGpuProfileZone(&ctx, "SHaRC Resolve");
      ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, SharcResolveShader::getShader());
      ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
    }
  }
  
  void SpatiallyHashedRadianceCache::onFrameEnd(Resources::RaytracingOutput& rtOutput) {
    if (!isActive()) {
      return;
    }

    m_resetHistory = false;
  }
} // namespace dxvk
