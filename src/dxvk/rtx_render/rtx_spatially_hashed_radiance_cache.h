/*
* Copyright (c) 2024-2026, NVIDIA CORPORATION. All rights reserved.
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

#include <atomic>
#include <random>

#include "rtx_option.h"
#include "rtx_resources.h"

namespace dxvk {
  class RtxContext;
  class DxvkDevice;

  class SpatiallyHashedRadianceCache : public RtxPass {
  public:

    struct SharcOptions {
      friend class SpatiallyHashedRadianceCache;
      friend class ImGUI;

      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, resetHistory, false,
                 "Clears all SHaRC hash entries and accumulated and resolved radiance on the next frame.\n"
                 "The option automatically returns to false after the reset.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, enableClear, false,
                 "Reserved for SHaRC accumulation-buffer clearing; currently unused.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, enableUpdate, true,
                 "Enables reduced-resolution path-traced updates that submit new radiance samples to SHaRC.\n"
                 "Disabling updates stops new samples, while Resolve may continue aging and evicting existing entries.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, enableResolve, true,
                 "Enables the pass that combines current SHaRC update samples with cache history.\n"
                 "Disabling Resolve discards new accumulation while queries may continue reading the last resolved cache.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, combineUpdateAndQuery, false,
                 "Traces update paths in the same dispatch as query paths, like NRC traces its training paths, instead of in a separate update pass.\n"
                 "This removes the fixed cost of the separate pass, but queries then read the cache resolved in the previous frame, which delays lighting changes by a frame.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, downscaleFactor, 5,
                 "Controls SHaRC update resolution; a factor of N traces one update ray per N by N pixel block each frame.\n"
                 "Higher values reduce update cost but make cache coverage and lighting changes converge more slowly.\n"
                 "Only used when targetNumUpdateRecords is 0.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, targetNumUpdateRecords, 0,
                 "Target number of update records per frame. A record is a path vertex that an update path adds to the cache.\n"
                 "The update grid is resized to reach it the way NRC sizes its training grid, which keeps the update cost independent of resolution.\n"
                 "Higher values make the cache converge and respond to lighting changes faster at a higher update cost.\n"
                 "0 disables the target, and downscaleFactor sets a fixed update grid instead.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", bool, excludePrimaryVerticesFromTarget, false,
                 "Leaves the record that each update path adds at its primary vertex out of the count that targetNumUpdateRecords targets.\n"
                 "NRC's training records mostly leave out primary vertices as well, so this makes the target comparable to NRC's.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", float, averageUpdateRecordsPerPath, 1.5f,
                 "Average number of records per update path, including its primary vertex, assumed when sizing the largest update grid, which is also the grid used after a reset.\n"
                 "Lower values allow a larger grid, so views where update paths end early, such as views mostly into the sky, can still reach the target.\n"
                 "One record per path less is assumed when excludePrimaryVerticesFromTarget is enabled.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, numFramesToSmoothOutUpdateDimensions, 16,
                 "Number of frames that the record count is averaged over before the update grid is resized.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", float, voxelSize, 0.02f,
                 "Base world-space size of SHaRC voxels.\n"
                 "Higher values produce larger voxels with broader coverage but less spatial detail.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, maxNumFramesToAccumulate, 10,
                 "Maximum number of frames accumulated in each SHaRC entry.\n"
                 "Higher values reduce noise but respond more slowly to lighting changes.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, staleFrameNumber, 64,
                 "Maximum number of frames without new samples before a SHaRC entry is evicted.\n"
                 "Higher values retain coverage longer but can preserve stale data; SHaRC clamps the effective minimum to 8 frames.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", float, perceptualRoughnessThreshold, 0.4f,
                 "Minimum perceptual roughness applied to secondary vertices during SHaRC update tracing.\n"
                 "Higher values reduce sharp, view-dependent radiance in the cache but can lose glossy lighting detail.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", float, fireflyClampMultiplier, 10.f,
                 "Limits non-emissive lighting at SHaRC update hits and sky radiance on misses to this multiple of the resolved mean at the current or last hit cell.\n"
                 "Set to 0 to disable clamping. Cells without valid resolved radiance are not clamped.");
      RTX_OPTION("rtx.spatiallyHashedRadianceCache", uint32_t, jitterSequenceLength, 128,
                 "Length of the Halton sequence used to vary SHaRC update locations within downscaled pixel blocks.\n"
                 "Set to 0 to disable update jitter.");
    };

    SpatiallyHashedRadianceCache(DxvkDevice* device);
    ~SpatiallyHashedRadianceCache();

    static bool checkIsSupported(const DxvkDevice* device);

    // To be called between PT update and query passes
    void dispatch(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);

    void onFrameEnd(Resources::RaytracingOutput& rtOutput);

    // This must be called after onFrameBegin() in a frame
    void setRaytraceArgs(RtxContext& ctx, RaytraceArgs& constants);

    VkExtent3D calcRaytracingResolution(const VkExtent3D& raytracingResolution, bool isUpdate) const;
    const Resources::Resource& getUpdateQueryReservoir() const { return m_updateQueryReservoir; }

    // Returns whether the query dispatch also traces the update paths, which dispatch() must then follow.
    bool isUpdateCombinedWithQuery() const;

    void bindGBufferPathTracingResources(RtxContext& ctx);
    void bindIntegrateIndirectPathTracingResources(RtxContext& ctx);

    bool isResettingHistory();

    // Returns the number of update records generated in a recent frame.
    // The count is read back from the GPU with a kMaxFramesInFlight frame delay.
    uint32_t getNumberOfUpdateRecords() const {
      return m_numberOfUpdateRecords.load(std::memory_order_relaxed);
    }

    // Returns the number of update paths traced in the same frame as getNumberOfUpdateRecords.
    // Each path adds one record at its primary vertex.
    uint32_t getNumberOfUpdatePaths() const {
      return m_numberOfUpdatePaths.load(std::memory_order_relaxed);
    }

    const VkExtent2D& getActiveUpdateDimensions() const { return m_activeUpdateDimensions; }
    const VkExtent2D& getMaxUpdateDimensions() const { return m_maxUpdateDimensions; }

  private:
    // Overrides for inherited RtxPass methods
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;
    virtual bool onActivation(Rc<DxvkContext>& ctx) override;
    virtual void onDeactivation() override;

    void createSharcBuffers(Rc<DxvkContext>& ctx);

    bool isEnabled() const override;
    void releaseResources();
    void dispatchResolve(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);

    void copyUpdateCounters(RtxContext& ctx);
    void readAndResetUpdateCounters();
    VkExtent2D calculateMinUpdateDimensions() const;
    VkExtent2D calculateMaxUpdateDimensions() const;
    void calculateActiveUpdateDimensions(bool forceReset);
    uint32_t calculateNumRowsForUpdate() const;

    DxvkDevice*            m_device;
    Rc<DxvkBuffer>         m_hashEntries;
    Rc<DxvkBuffer>         m_accumulation;
    Rc<DxvkBuffer>         m_resolved;

    // Holds the counters that the SHARC_UPDATE_COUNTER_* indices address.
    Rc<DxvkBuffer>         m_updateCounters;
    // Receives m_updateCounters for each of the last kMaxFramesInFlight frames.
    Rc<DxvkBuffer>         m_updateCountersReadback;

    VkExtent2D             m_renderDimensions = { 0, 0 };
    VkExtent2D             m_maxUpdateDimensions = { 0, 0 };
    VkExtent2D             m_activeUpdateDimensions = { 0, 0 };
    std::atomic<uint32_t>  m_numberOfUpdateRecords = 0;
    std::atomic<uint32_t>  m_numberOfUpdatePaths = 0;
    float                  m_smoothedNumberOfUpdateRecords = 0.f;
    uint32_t               m_smoothingResetFrameIdx = UINT32_MAX;

    // Resources for integrated surface radiance for update rays in GBuffer pass.
    // These are reloaded on path continuation in indirect pass since SHaRC library interaction
    // is initiated at that point
    Resources::Resource    m_trainingGBufferSurfaceRadianceRG;
    Resources::Resource    m_trainingGBufferSurfaceRadianceB;
    Resources::Resource    m_updateQueryReservoir;

    bool                   m_resetHistory = false;

    // Number of entries represents the number of scene voxels used for radiance caching.
    // A solid baseline for most scenes can be the usage of 2^22 elements.
    // Commonly a power of 2 values are suggested.Higher element count can be used for scenes with high depth complexity, 
    // lower element count reduce memmory pressure, but can result in more hash collisions.
    uint32_t               m_numEntries = 4 * 1024 * 1024;      

   };
} // namespace dxvk
