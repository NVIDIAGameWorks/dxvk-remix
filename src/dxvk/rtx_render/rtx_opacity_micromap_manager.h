/*
* Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/rc/util_rc_ptr.h"
#include "rtx_types.h"
#include "rtx_geometry_utils.h"
#include "rtx_option.h"
#include <vector>
#include <list>
#include <unordered_map>

namespace dxvk {
  class DxvkContext;
  class DxvkDevice;
  class DxvkCommandList;
  class DxvkBarrierSet;
  class RtInstance;
  class InstanceManager;
  struct InstanceEventHandler;

  // Encapsulates built opacity micromap buffers used in BLAS
  class DxvkOpacityMicromap : public DxvkResource {
  public:
    VkAccelerationStructureTrianglesOpacityMicromapEXT blasDesc {}; // Desc passed to BLAS
    Rc<DxvkBuffer> opacityMicromapTriangleIndexBuffer;
    Rc<DxvkBuffer> opacityMicromapBuffer;
    VkMicromapEXT opacityMicromap = VK_NULL_HANDLE;      // Built micromap handle

    DxvkOpacityMicromap(Rc<DxvkDevice> device);
    ~DxvkOpacityMicromap();

  private:
    Rc<DxvkDevice> m_device;
  };

  struct OpacityMicromapSettings {
    bool showAdvancedOptions = false;
    bool enableBinding = true;
    bool enableBakingArrays = true;
    bool enableBuilding = true;
    bool enableResetEveryFrame = false;
    bool enableVertexAndTextureOperations = true;
    bool allow2StateOpacityMicromaps = true;
    bool force2StateOpacityMicromaps = false;
    bool customBuildRequestFilteringForBillboards = true;

    // Scene
    bool splitBillboardGeometry = true;
    uint32_t maxAllowedBillboardsPerInstanceToSplit = 16;   // Max billboards per instance to consider for splitting (high # cause a CPU strain on BLAS builds)

    // Cache
    float maxVidmemSizePercentage = 0.5;    // max % of vidmem size that can be allocated for the budget
    uint32_t minBudgetSizeMB;         // min budget size 
    uint32_t maxBudgetSizeMB;         
    uint32_t minFreeVidmemMBToNotAllocate = 1024; // min of vidmem memory free to not to allocate for the budget

    uint32_t minUsageFrameAgeBeforeEviction = 60 * 15;

    // Build Requests
    uint32_t maxOmmBuildRequests = 5 * 1000;
    uint32_t ommBuildRequest_minInstanceFrameAge = 1;
    uint32_t ommBuildRequest_minNumFramesRequested = 5;
    uint32_t ommBuildRequest_maxRequestFramesAge = 300;
    uint32_t ommBuildRequest_minNumRequests = 10;
    bool enableAnimatedInstances = false;
    bool enableParticles = true;

    uint32_t subdivisionLevel = 8;
    float decalsMinResolveTransparencyThreshold = 0.0f; // Set to 0 as higher values cause bullet decals on a glass panels to miss parts in Portal RTX
    bool enableConservativeEstimation = true;
    // Sets a max number of texel taps per microtriangle. Optimally, a mictriangle resolution should be similar to that of the opacity
    // texture, but, currently, a global subdivision level is used instead for implementation simplicity and, thus, a microtriangle can overlap multiple texels.
    // To handle this case we allow Opacity Micromap baking shader to do N taps per microtriangle to resolve the opacity state. 
    // This value must not be too high, however, as it can lead to threads in the baking shader taking too long and causing timeouts. 
    // 512 taps has been found to cause a timeout. Also if a microtriangle has much smaller resolution that the source texture, there's a higher chance
    // of the microtriangles covering both fully opaque and fully transparent texels, and being classified in unknown opaque state which eliminates Opacity Micromap
    // performance benefits. Therefore allowing for high taps baking scenarios has a diminishing return.
    // FutureWork: perform a configuration optimization on host or GPU processing texture coordinates for each triangle to determine an appropriate Opacity Micromap resolution
    // per triangle. 
    uint32_t conservativeEstimationMaxTexelTapsPerMicroTriangle;

    // Disabled for now as camera cuts occur even on non-camera movements, i.e. when
    // a plasma ball hits a sink, to avoid increased workload during those moments/gameplay
    uint32_t numFramesAtStartToBuildWithHighWorkload = 0;
    uint32_t workloadHighWorkloadMultiplier = 20;

    // Parameterized to <1% FPS overhead on 4090
    uint32_t maxMicroTrianglesToBakeMillionPerSecond = 60 * 1;    // 2 mil ~ 0.15 ms on 4090
    uint32_t maxMicroTrianglesToBuildMillionPerSecond = 60 * 5;  // 10 mil ~ 0.04ms on 4090
  };

  enum class OpacityMicromapCacheState {
    eStep0_Unprocessed = 0, // Cache items that use OMMs but yet to have any data generated for them
    eStep1_Baking,          // Cache items with OMM arrays being baked but not all baking tasks have been submitted
    eStep2_Baked,           // Cache items with baked OMM arrays
    eStep3_Built,           // Cache items with built OMMs, but require barrier sync before use in a BLAS build
    eStep4_Ready,           // Cache items with built OMMs

    eUnknown
  };

  class OmmRequest {
  public:
    static const uint32_t kInvalidIndex = UINT32_MAX;
    const RtInstance& instance;
    const uint32_t quadSliceIndex = kInvalidIndex;    // Index of instance's quad tracked by this object
    XXH64_hash_t ommSrcHash = kEmptyHash;
    uint32_t numTriangles = UINT32_MAX;
    VkOpacityMicromapFormatEXT ommFormat = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT;

    OmmRequest(const RtInstance& _instance, const InstanceManager& instanceManager, const OpacityMicromapSettings& settings, uint32_t _quadSliceIndex = kInvalidIndex);
    OmmRequest(OmmRequest&&) = default;
    OmmRequest(const OmmRequest&) = default;

    bool isBillboardOmmRequest() const {
      return quadSliceIndex != kInvalidIndex;
    }
  };

  class OpacityMicromapCacheItem : public DxvkResource {
  public:
    OpacityMicromapCacheState cacheState = OpacityMicromapCacheState::eUnknown;

    Rc<DxvkOpacityMicromap> blasOmmBuffers;
    bool useVertexAndTextureOperations = false;
    uint32_t lastUseFrameIndex = kInvalidFrameIndex;
    uint16_t subdivisionLevel = UINT16_MAX;
    uint32_t numTriangles = UINT32_MAX;
    VkOpacityMicromapFormatEXT ommFormat = VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    std::list<XXH64_hash_t>::iterator leastRecentlyUsedListIter;

    // Needed during baking
    Rc<DxvkBuffer> ommArrayBuffer;   // Per micro triangle
    RtxGeometryUtils::BakeOpacityMicromapState bakingState;

    // Preallocated device sizes from Opacity Micromap Manager
    VkDeviceSize blasOmmBuffersDeviceSize = 0;
    VkDeviceSize arrayBufferDeviceSize = 0;

    OpacityMicromapCacheItem();
    OpacityMicromapCacheItem(Rc<DxvkDevice> device, OpacityMicromapCacheState _cacheState, const uint32_t subdivisionLevel, const bool enableVertexAndTextureOperations, uint32_t currentFrameIndex, std::list<XXH64_hash_t>::iterator _mostRecentlyUsedListIter, const OmmRequest& ommRequest);
    OpacityMicromapCacheItem(const OpacityMicromapCacheItem& src) 
    : cacheState(src.cacheState)
    , blasOmmBuffers(src.blasOmmBuffers)
    , ommArrayBuffer(src.ommArrayBuffer)
    , useVertexAndTextureOperations(src.useVertexAndTextureOperations)
    , subdivisionLevel(src.subdivisionLevel)
    , ommFormat(src.ommFormat)
    , leastRecentlyUsedListIter(src.leastRecentlyUsedListIter) { }

    VkDeviceSize getDeviceSize() const;

    bool isCompatibleWithOmmRequest(const OmmRequest& ommRequest);
  };

  class OpacityMicromapMemoryManager {
  public:
    OpacityMicromapMemoryManager(const Rc<DxvkDevice>& device);

    void onFrameStart();
    void updateMemoryBudget(Rc<DxvkContext> ctx, const OpacityMicromapSettings& settings);

    bool allocate(VkDeviceSize size);
    VkDeviceSize getAvailable() const;
    void release(VkDeviceSize size);
    void releaseAll();

    VkDeviceSize getBudget() const { return m_budget; }
    VkDeviceSize getUsed() const { return m_used; }
    float calculateUsageRatio() const;
    VkDeviceSize calculatePendingAvailableSize() const;
    VkDeviceSize calculatePendingReleasedSize() const;
    VkDeviceSize getNextPendingReleasedSize() const;

  private:
    VkDeviceSize m_used = 0;
    VkDeviceSize m_budget;

    Rc<DxvkDevice>                    m_device;
    VkPhysicalDeviceMemoryProperties  m_memoryProperties;

    // Stores amount of memory released per frame. The release memory is added back to the pool
    // with a delay accounting for lifetime management of resources in flight
    std::list<VkDeviceSize> m_pendingReleaseSize;
  };

  // OpacityMicromapManager generates and manages Opacity Micromap data
  class OpacityMicromapManager {
  public:
    enum class OmmResult {
      Success,
      Failure,
      OutOfMemory,
      OutOfBudget,
      DependenciesUnavailable
    };

    OpacityMicromapManager(Rc<DxvkDevice> device);
    ~OpacityMicromapManager() { }

    // Calculates hash that distinguishes source data used for Opacity Micromap generation
    static XXH64_hash_t calculateSourceHash(XXH64_hash_t geometryHash, XXH64_hash_t materialHash, const RtSurface::AlphaState& alphaState);
    
    InstanceEventHandler getInstanceEventHandler();

    // Registers Opacity Micromap build request for an instance. 
    // Call this when tryBindOpacityMicromap() is not called for the instance within a frame 
    // but it is still desired to add the request to the build queue (i.e. in ViewModel reference 
    // instance case).
    // Returns true if the Opacity Micromap build request has been added now or previously. 
    // Returns false if the Opacity Micromap build  request was rejected
    bool registerOpacityMicromapBuildRequest(RtInstance& instance, const InstanceManager& instanceManager, const std::vector<TextureRef>& textures);

    // Tries to bind an opacity micromap for a given instance to the target, 
    // If the instance uses an opacity micromap and the Opacity Micromap data is generated, 
    // it binds it as well updates VK instance flags in the instance.
    // Returns bound opacity micromap source hash
    // Returns kEmptyHash otherwise
    XXH64_hash_t tryBindOpacityMicromap(Rc<DxvkCommandList> cmdList, const RtInstance& instance, uint32_t billboardIndex, 
                                        VkAccelerationStructureGeometryKHR& targetGeometry, const InstanceManager& instanceManager);

    // Called once per frame to build pending Opacity Micromap items in Opacity Micromap Manager
    void buildOpacityMicromaps(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, const std::vector<TextureRef>& textures, uint32_t lastCameraCutFrameId, float frameTimeSecs);

    // Called once per frame before any calls to Opacity Micromap Manager
    void onFrameStart(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList);

    // Clears all built data and tracked instance state
    void clear();

    void showImguiSettings() const;

    const OpacityMicromapSettings& getSettings() const { return m_settings; }

    static bool checkIsOpacityMicromapSupported(Rc<DxvkDevice> device);

    bool doesInstanceUseOpacityMicromap(const RtInstance& instance) const;

    // Must be called before a BLAS build is kicked off if an opacity micromap may have been bound for it
    // Should be called sparingly/once a frame after opacity micromaps have been bound to BLASes
    // and corresponding batched BLASes are about to be built. 
    // It is OK for batched BLASes to contain a mix of BLASes with and without bound opacity micromaps
    void onBlasBuild(Rc<DxvkCommandList> cmdList);

  private:
    typedef std::unordered_map<XXH64_hash_t, OpacityMicromapCacheItem> OpacityMicromapCache;

    struct InstanceOmmRequests {
      uint32_t numActiveRequests = 0;
      std::vector<OmmRequest> ommRequests;
    };

    class CachedSourceData {
    public:
      uint32_t triangleOffset = 0;
      uint32_t numTriangles = 0;

      ~CachedSourceData();

      void initialize(const OmmRequest& ommRequest, std::unordered_map<XXH64_hash_t, InstanceOmmRequests>& instanceOmmRequests);
      void setInstance(const RtInstance* _instance, std::unordered_map<XXH64_hash_t, InstanceOmmRequests>& instanceOmmRequests, bool deleteParentInstanceIfEmpty = true);

      const RtInstance* getInstance() const {
        return instance; 
      }
    private:
      const RtInstance* instance = nullptr;
    };

    void initSettings();

    XXH64_hash_t bindOpacityMicromap(Rc<DxvkCommandList> cmdList, const RtInstance& instance, uint32_t billboardIndex, VkAccelerationStructureGeometryKHR& targetGeometry, const InstanceManager& instanceManager);

    void updateMemoryBudget();
    void generateInstanceOmmRequests(RtInstance& instance, const InstanceManager& instanceManager, std::vector<OmmRequest>& ommRequests);

    bool registerOmmRequestInternal(RtInstance& instance, const OmmRequest& ommRequest);
    bool addNewOmmBuildRequest(RtInstance& instance, const OmmRequest& ommRequest);
    std::unordered_map<XXH64_hash_t, CachedSourceData>::iterator registerCachedSourceData(const OmmRequest& ommRequest);
    void deleteCachedSourceData(std::unordered_map<XXH64_hash_t, CachedSourceData>::iterator sourceDataIter, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer);
    void deleteCachedSourceData(XXH64_hash_t ommSrcHash, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer);
    bool insertToUnprocessedList(const OmmRequest& ommRequest);
    void destroyOmmData(const OpacityMicromapCache::iterator& ommCacheIterator, bool destroyParentInstanceOmmRequestContainer = true);
    void destroyOmmData(XXH64_hash_t ommSrcHash);

    // Destroys references to an instance, but retains associated cached baked/built OMM data that doesn't depend on lifetime of the instance
    void destroyInstance(const RtInstance& instance, bool forceDestroy = false);
    uint32_t calculateNumMicroTriangles(uint16_t subdivisionLevel);

    // Called whenever a new instance has been added to the database
    void onInstanceAdded(const RtInstance& instance);
    // Called whenever instance metadata is updated
    void onInstanceUpdated(const RtInstance& instance, const RtSurfaceMaterial& material, const bool hasTransformChanged, const bool hasVerticesChanged);
    // Called whenever an instance has been removed from the database
    void onInstanceDestroyed(const RtInstance& instance);

    // Needs to be called before a BLAS build with any bound OMMs
    // Will do nothing if no synchronization is needed
    void addBarriersForBuiltOMMs(Rc<DxvkCommandList> cmdList);

    bool areInstanceTexturesResident(const RtInstance& instance, const std::vector<TextureRef>& textures) const;

    void updateSourceHash(RtInstance& instance, XXH64_hash_t ommSrcHash);

    void calculateMicromapBuildInfo(VkMicromapUsageEXT& ommUsageGroup, VkMicromapBuildInfoEXT& ommBuildInfo, VkMicromapBuildSizesInfoEXT& sizeInfo);

    void calculateRequiredVRamSize(uint32_t numTriangles, uint16_t subdivisionLevel, VkOpacityMicromapFormatEXT ommFormat, VkIndexType triangleIndexType, VkDeviceSize& arrayBufferDeviceSize, VkDeviceSize& blasOmmBuffersDeviceSize);

    OmmResult bakeOpacityMicromapArray(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, XXH64_hash_t ommSrcHash,
                                  OpacityMicromapCacheItem& ommCacheItem, CachedSourceData& sourceData,
                                  const std::vector<TextureRef>& textures, uint32_t& maxMicroTrianglesToBake);
    OmmResult buildOpacityMicromap(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, XXH64_hash_t ommSrcHash, OpacityMicromapCacheItem& ommCacheItem, VkMicromapUsageEXT& ommUsageGroup, VkMicromapBuildInfoEXT& ommBuildInfo, uint32_t& maxMicroTrianglesToBuild, bool forceBuild);
    void bakeOpacityMicromapArrays(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList,
                                   const std::vector<TextureRef>& textures, uint32_t& maxMicroTrianglesToBake);
    void buildOpacityMicromapsInternal(Rc<DxvkContext> ctx, Rc<DxvkCommandList> cmdList, uint32_t& maxMicroTrianglesToBuild);

    Rc<DxvkDevice> m_device;

    // Active baking state
    bool m_opacityMicromapUseConservativeEstimation = false;
    uint32_t m_opacityMicromapConservativeEstimationMaxTexelTapsPerMicroTriangle = 0;
    uint32_t m_subdivisionLevel = 0;
    bool m_enableVertexAndTextureOperations = false;

    // Bound built OMMs need to be synchronized once before being used. 
    // This tracks if any such OMMs have been bound
    bool m_boundOmmsRequireSynchronization = false;
    uint32_t m_numBoundOMMs = 0;
    uint32_t m_numRequestedOMMBindings = 0;


    std::unordered_map<XXH64_hash_t, InstanceOmmRequests> m_instanceOmmRequests;

    OpacityMicromapCache m_ommCache; 
    std::unordered_map<XXH64_hash_t, CachedSourceData> m_cachedSourceData;
    std::vector<Rc<DxvkOpacityMicromap>> m_boundOMMs; // OMMs bound in a frame

    // Ordered lists starting with oldest and/or smallest inserted items 
    std::list<XXH64_hash_t> m_unprocessedList;   // Contains OMM data requests that are yet to be baked
    std::list<XXH64_hash_t> m_bakedList;         // Contains OMM items with baked OMM arrays
    std::list<XXH64_hash_t> m_builtList;         // Contains OMM items with built OMMs but require synchronization
    std::list<XXH64_hash_t> m_readyList;         // Contains OMM items with built OMM data};

    std::unordered_set<XXH64_hash_t> m_blackListedList;// Contains OMM surface hashes that failed to get baked or built (in time)
                                                 // and helps avoid wasting resources for such cases
    
    // Tracks statistics for OMM build requests to be able to filter less frequest requests out
    struct OMMBuildRequestStatistics {
      uint16_t numFramesRequested = 0;
      uint16_t numTimesRequested = 0;
      uint32_t lastRequestFrameId = kInvalidFrameIndex;
    };

    uint32_t m_numMicroTrianglesBaked = 0;    // Per frame
    uint32_t m_numMicroTrianglesBuilt = 0;    // Per frame

    // LRU management
    std::list<XXH64_hash_t> m_leastRecentlyUsedList;  // Items stored in their usage order starting with least recently used item

    std::unordered_map<XXH64_hash_t, OMMBuildRequestStatistics> m_ommBuildRequestStatistics;

    VkDeviceSize m_amountOfMemoryMissing = 0;    // Records how much memory was missing in a frame
    OpacityMicromapMemoryManager m_memoryManager;
    DxvkStagingDataAlloc m_scratchAllocator;

    mutable OpacityMicromapSettings m_settings;

    struct RtxOptionSettings {
      RTX_OPTION("rtx.opacityMicromap", int, maxOmmBuildRequests, 5 * 1000, "");
      RTX_OPTION_ENV("rtx.opacityMicromap", int, numFramesAtStartToBuildWithHighWorkload, 0, "DXVK_OPACITY_MICROMAP_NUM_FRAMES_AT_START_TO_BUILD_WITH_HIGH_WORKLOAD", "");
      RTX_OPTION_ENV("rtx.opacityMicromap", int, workloadHighWorkloadMultiplier, 20, "DXVK_OPACITY_MICROMAP_HIGH_WORKLOAD_MULTIPLIER", "");
      RTX_OPTION("rtx.opacityMicromap", float, maxVidmemSizePercentage, 0.15, "");
      RTX_OPTION("rtx.opacityMicromap", int, minFreeVidmemMBToNotAllocate, 2560, "");
      RTX_OPTION("rtx.opacityMicromap", int, minBudgetSizeMB, 512, "");
      RTX_OPTION("rtx.opacityMicromap", int, maxBudgetSizeMB, 1536, "");
      RTX_OPTION_ENV("rtx.opacityMicromap", int, ommBuildRequest_minInstanceFrameAge, 1, "DXVK_OPACITY_MICROMAP_MIN_INSTANCE_FRAME_AGE", "");
      RTX_OPTION_ENV("rtx.opacityMicromap", int, ommBuildRequest_minNumFramesRequested, 5, "DXVK_OPACITY_MICROMAP_OMM_BUILD_REQUESTED_MIN_NUM_FRAMES_REQUESTED", "");
      RTX_OPTION_ENV("rtx.opacityMicromap", int, ommBuildRequest_minNumRequests, 10, "DXVK_OPACITY_MICROMAP_OMM_BUILD_REQUESTED_MIN_NUM_REQUESTS", "");
      RTX_OPTION("rtx.opacityMicromap", int, subdivisionLevel, 8, "");
      RTX_OPTION("rtx.opacityMicromap", bool, enableResetEveryFrame, false, "");
      RTX_OPTION("rtx.opacityMicromap", int, conservativeEstimationMaxTexelTapsPerMicroTriangle, 64, "Set to 64 as a safer cap. 512 has been found to cause a timeout.");
      RTX_OPTION_ENV("rtx.opacityMicromap", bool, enableParticles, true, "DXVK_OPACITY_MICROMAP_ENABLE_PARTICLES", "");
    };
  };

}  // namespace dxvk

