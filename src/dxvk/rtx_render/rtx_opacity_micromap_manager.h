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
#include "rtx_common_object.h"
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

  struct OpacityMicromapOptions {
    friend class OpacityMicromapManager;

    RTX_OPTION("rtx.opacityMicromap", bool, showAdvancedOptions, false, "Shows advanced options.");

    RTX_OPTION("rtx.opacityMicromap", bool, enableBinding, true, "Enables binding of built Opacity Micromaps to bottom level acceleration structures.");
    RTX_OPTION("rtx.opacityMicromap", bool, enableBakingArrays, true, "Enables baking of opacity textures into Opacity Micromap arrays per triangle.");
    RTX_OPTION("rtx.opacityMicromap", bool, enableBuilding, true, "Enables building of Opacity Micromap arrays.");
    RTX_OPTION("rtx.opacityMicromap", bool, enableResetEveryFrame, false, "Debug: resets Opacity Micromap runtime data every frame. ");


    struct Cache {
      friend class OpacityMicromapManager;

      RTX_OPTION("rtx.opacityMicromap.cache", int, minFreeVidmemMBToNotAllocate, 2560, "Min Video Memory [MB] to keep free before allocating any for Opacity Micromaps.");
      RTX_OPTION("rtx.opacityMicromap.cache", int, minBudgetSizeMB, 512, "Budget: Min Video Memory [MB] required.\n"
                                                                         "If the min amount is not available, then the budget will be set to 0.");
      RTX_OPTION("rtx.opacityMicromap.cache", int, maxBudgetSizeMB, 1536, "Budget: Max Allowed Size [MB].");
      RTX_OPTION("rtx.opacityMicromap.cache", float, maxVidmemSizePercentage, 0.15, "Budget: Max Video Memory Size %.");
      RTX_OPTION("rtx.opacityMicromap.cache", int, minUsageFrameAgeBeforeEviction, 60 * 15, 
                 "Min Opacity Micromap usage frame age before eviction.\n"
                 "Opacity Micromaps unused longer than this can be evicted when freeing up memory for new Opacity Micromaps.");
      RTX_OPTION("rtx.opacityMicromap.cache", bool, hashInstanceIndexOnly, false,
                 "Uses instance index as an Opacity Micromap hash.");

    };


    struct BuildRequests {
      friend class OpacityMicromapManager;

      RTX_OPTION("rtx.opacityMicromap.buildRequests", bool, filtering, true, "Enables filtering of Opacity Micromap requests. Filtering reduces and slows down acceptance of Opacity Micromap requests to maximize resources to requests that are more likely to be reused across instances and frames.");
      RTX_OPTION("rtx.opacityMicromap.buildRequests", int, maxRequests, 5 * 1000,
                 "Max number of staged unique Opacity Micromap build requests.\n"
                 "Any further requests will simply be discarded until the number of staged requests decreases below this threshold.\n"
                 "Once a staged request passes filters for building, it is removed from the staging list.");
      RTX_OPTION("rtx.opacityMicromap.buildRequests", bool, enableParticles, true, "Enables Opacity Micromaps for particles.");
      RTX_OPTION("rtx.opacityMicromap.buildRequests", bool, enableAnimatedInstances, false, "Enables Opacity Micromaps for animated instances.");

      RTX_OPTION_ENV("rtx.opacityMicromap.buildRequests", int, minInstanceFrameAge, 1, "DXVK_OPACITY_MICROMAP_MIN_INSTANCE_FRAME_AGE", "Min instance's frame age which to allow building Opacity Micromaps for.");
      RTX_OPTION("rtx.opacityMicromap.buildRequests", int, maxRequestFrameAge, 300, "Max request frame age to allow building Opacity Micromaps for. Any requests older than this are purged.");
      RTX_OPTION_ENV("rtx.opacityMicromap.buildRequests", int, minNumFramesRequested, 5, "DXVK_OPACITY_MICROMAP_OMM_BUILD_REQUESTED_MIN_NUM_FRAMES_REQUESTED",
                     "Min number of frames for a staged Opacity Micromap request before it is allowed to be built.");
      RTX_OPTION_ENV("rtx.opacityMicromap.buildRequests", int, minNumRequests, 10, "DXVK_OPACITY_MICROMAP_OMM_BUILD_REQUESTED_MIN_NUM_REQUESTS",
                     "Min number of Opacity Micromap usage requests for a staged Opacity Micromap request before it is allowed to be built.");

      RTX_OPTION("rtx.opacityMicromap.buildRequests", bool, customFiltersForBillboards, true, "Applies custom filters for staged Billboard requests.");
    };


    struct Building {
      friend class OpacityMicromapManager;

      RTX_OPTION("rtx.opacityMicromap.building", bool, splitBillboardGeometry, true,
                 "Splits billboard geometry and corresponding Opacity Micromaps to quads for higher reuse.\n"
                 "Games often batch instanced geometry that reuses same geometry and textures, such as for particles.\n"
                 "Splitting such batches into unique subgeometries then allows higher reuse of build Opacity Micromaps.");
      RTX_OPTION("rtx.opacityMicromap.building", int, maxAllowedBillboardsPerInstanceToSplit, 16, "Max billboards per instance to consider for splitting (large value results in increased CPU costs on BLAS builds).");

      RTX_OPTION("rtx.opacityMicromap.building", bool, enableVertexAndTextureOperations, true, "Applies vertex and texture operations during baking.");
      RTX_OPTION("rtx.opacityMicromap.building", bool, allow2StateOpacityMicromaps, true, "Allows generation of two state Opacity Micromaps.");
      RTX_OPTION("rtx.opacityMicromap.building", bool, force2StateOpacityMicromaps, false, "Forces generation of two state Opacity Micromaps.");
      RTX_OPTION("rtx.opacityMicromap.building", int, subdivisionLevel, 8, "Opacity Micromap subdivision level per triangle. ");
      // Set to 0 as higher values cause bullet decals on a glass panels to miss parts in Portal RTX
      RTX_OPTION("rtx.opacityMicromap.building", float, decalsMinResolveTransparencyThreshold, 0.f, "Min resolve transparency threshold for decals.");

      // Parameterized to <1% FPS overhead on 4090
      // Baking: 2 mil ~ 0.15 ms
      // Building: 10 mil ~ 0.04 ms
      RTX_OPTION("rtx.opacityMicromap.building", int, maxMicroTrianglesToBakeMillionPerSecond, 60 * 1, "Max Micro Triangles to bake [Million/Second].");
      RTX_OPTION("rtx.opacityMicromap.building", int, maxMicroTrianglesToBuildMillionPerSecond, 60 * 5, "Max Micro Triangles to build [Million/Second].");

      // Disabled for now as camera cuts occur even on non-camera movements, i.e. when
      // a plasma ball hits a sink, to avoid increased workload during those moments/gameplay
      RTX_OPTION_ENV("rtx.opacityMicromap.building", int, numFramesAtStartToBuildWithHighWorkload, 0, "DXVK_OPACITY_MICROMAP_NUM_FRAMES_AT_START_TO_BUILD_WITH_HIGH_WORKLOAD",
                     "Number of frames at start to to bake and build Opacity Micromaps with high workload multiplier.\n"
                     "This is used for testing to decrease frame latency for Opacity Micromaps being ready.");
      RTX_OPTION_ENV("rtx.opacityMicromap.building", int, highWorkloadMultiplier, 20, "DXVK_OPACITY_MICROMAP_HIGH_WORKLOAD_MULTIPLIER",
                     "High workload multiplier that is applied to number of Opacity Micromaps to bake and build per frame.\n"
                     "This is used for testing to decrease frame latency for Opacity Micromaps being ready.");


      struct ConservativeEstimation {
        friend class OpacityMicromapManager;

        RTX_OPTION("rtx.opacityMicromap.building.conservativeEstimation", bool, enable, true, "Enables Conservative Estimation of micro triangle opacities.");
        // Sets a max number of texel taps per microtriangle. Optimally, a mictriangle resolution should be similar to that of the opacity
        // texture, but, currently, a global subdivision level is used instead for implementation simplicity and, thus, a microtriangle can overlap multiple texels.
        // To handle this case we allow Opacity Micromap baking shader to do N taps per microtriangle to resolve the opacity state. 
        // This value must not be too high, however, as it can lead to threads in the baking shader taking too long and causing timeouts. 
        // 512 taps has been found to cause a timeout. Also if a microtriangle has much smaller resolution that the source texture, there's a higher chance
        // of the microtriangles covering both fully opaque and fully transparent texels, and being classified in unknown opaque state which eliminates Opacity Micromap
        // performance benefits. Therefore allowing for high taps baking scenarios has a diminishing return.
        // FutureWork: perform a configuration optimization on host or GPU processing texture coordinates for each triangle to determine an appropriate Opacity Micromap resolution
        // per triangle. 
        RTX_OPTION("rtx.opacityMicromap.building.conservativeEstimation", int, maxTexelTapsPerMicroTriangle, 64,
                   "Max number of texel taps per micro triangle when Conservative Estimation is enabled.\n"
                   "Set to 64 as a safer cap. 512 has been found to cause a timeout.\n"
                   "Any microtriangles requiring more texel taps will be tagged as Opaque Unknown.");
      };
    };
  };

  // Encapsulates built opacity micromap buffers used in BLAS
  class DxvkOpacityMicromap : public DxvkResource {
  public:
    VkAccelerationStructureTrianglesOpacityMicromapEXT blasDesc {}; // Desc passed to BLAS
    Rc<DxvkBuffer> opacityMicromapTriangleIndexBuffer;
    Rc<DxvkBuffer> opacityMicromapBuffer;
    VkMicromapEXT opacityMicromap = VK_NULL_HANDLE;      // Built micromap handle

    explicit DxvkOpacityMicromap(DxvkDevice& device);
    ~DxvkOpacityMicromap();

  private:
    Rc<vk::DeviceFn> m_vkd;
  };

  enum class OpacityMicromapCacheState {
    eStep0_Unprocessed = 0, // Cache items that use OMMs but yet to have any data generated for them
    eStep1_Baking,          // Cache items with OMM arrays being baked but not all baking tasks have been submitted
    eStep2_Baked,           // Cache items with baked OMM arrays
    eStep3_Built,           // Cache items with built OMMs, but require barrier sync before use in a BLAS build
    eStep4_Ready,           // Cache items with built OMMs

    eUnknown
  };

  // All parameters contributing to an OmmSrcHash
  // Ensure the struct is fully padded and default initialized
  struct OpacityMicromapHashSourceData {

    Matrix4 textureTransform = {};        // 16B alignment

    RtSurface::AlphaState alphaState = {};
    RtTextureArgSource textureColorArg1Source = RtTextureArgSource::None;
    RtTextureArgSource textureColorArg2Source = RtTextureArgSource::None;
    DxvkRtTextureOperation textureColorOperation = DxvkRtTextureOperation::Disable;
    RtTextureArgSource textureAlphaArg1Source = RtTextureArgSource::None;
    RtTextureArgSource textureAlphaArg2Source = RtTextureArgSource::None;
    DxvkRtTextureOperation textureAlphaOperation = DxvkRtTextureOperation::Disable;

    XXH64_hash_t materialHash = kEmptyHash;
    XXH64_hash_t texCoordHash = kEmptyHash;

    XXH64_hash_t vertexOpacityHash = kEmptyHash;
    VkOpacityMicromapFormatEXT ommFormat = VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;         // 4B
    uint32_t numTriangles = 0;

    uint8_t tFactorAlpha = 0;
    uint8_t pad8[3] = {};
    uint32_t pad32[3] = {};
  };

  // Static validation to detect any changes that require OmmHashData alignment re-check
  static_assert(sizeof(OpacityMicromapHashSourceData) == 128);
  static_assert(sizeof(RtSurface::AlphaState) == 10);

  class OmmRequest {
  public:
    static const uint32_t kInvalidIndex = UINT32_MAX;
    const RtInstance& instance;
    const uint32_t quadSliceIndex = kInvalidIndex;    // Index of instance's quad tracked by this object
    XXH64_hash_t ommSrcHash = kEmptyHash;
    uint32_t numTriangles = UINT32_MAX;
    VkOpacityMicromapFormatEXT ommFormat = VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT;

    OmmRequest(const RtInstance& _instance, const InstanceManager& instanceManager, uint32_t _quadSliceIndex = kInvalidIndex);
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

    // Iterator to a cache state list for the current cacheState.
    // Since the iterator is moved between the lists, it is initalized only once
    // and remains valid until it's removed from a list
    std::list<XXH64_hash_t>::iterator cacheStateListIter;

    // Whether cacheStateListIter is valid when it corresponds to m_unprocessedList.
    // This is to handle the iterator state when an OMM cache item is in unprocessed or baking state
    // but the source data has been unlinked and cacheStateList item was removed
    bool isUnprocessedCacheStateListIterValid = false;  

    // Needed during baking
    Rc<DxvkBuffer> ommArrayBuffer;   // Per micro triangle
    RtxGeometryUtils::BakeOpacityMicromapState bakingState;

    // Preallocated device sizes from Opacity Micromap Manager
    VkDeviceSize blasOmmBuffersDeviceSize = 0;
    VkDeviceSize arrayBufferDeviceSize = 0;

    OpacityMicromapCacheItem();
    OpacityMicromapCacheItem(DxvkDevice& device, OpacityMicromapCacheState _cacheState, const uint32_t subdivisionLevel, const bool enableVertexAndTextureOperations,     
                             uint32_t currentFrameIndex, std::list<XXH64_hash_t>::iterator _mostRecentlyUsedListIter, std::list<XXH64_hash_t>::iterator _cacheStateListIter,
                             const OmmRequest& ommRequest);
    OpacityMicromapCacheItem(const OpacityMicromapCacheItem& src) 
    : cacheState(src.cacheState)
    , blasOmmBuffers(src.blasOmmBuffers)
    , ommArrayBuffer(src.ommArrayBuffer)
    , useVertexAndTextureOperations(src.useVertexAndTextureOperations)
    , subdivisionLevel(src.subdivisionLevel)
    , ommFormat(src.ommFormat)
    , leastRecentlyUsedListIter(src.leastRecentlyUsedListIter)
    , cacheStateListIter(src.cacheStateListIter)
    , isUnprocessedCacheStateListIterValid(src.isUnprocessedCacheStateListIterValid) { }

    VkDeviceSize getDeviceSize() const;

    bool isCompatibleWithOmmRequest(const OmmRequest& ommRequest);
  };

  class OpacityMicromapMemoryManager : public CommonDeviceObject {
  public:
    explicit OpacityMicromapMemoryManager(DxvkDevice* device);

    void onFrameStart();
    void updateMemoryBudget(Rc<DxvkContext> ctx);

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

    VkPhysicalDeviceMemoryProperties  m_memoryProperties;

    // Stores amount of memory released per frame. The release memory is added back to the pool
    // with a delay accounting for lifetime management of resources in flight
    std::list<VkDeviceSize> m_pendingReleaseSize;
  };

  // OpacityMicromapManager generates and manages Opacity Micromap data
  class OpacityMicromapManager : public CommonDeviceObject {
  public:
    enum class OmmResult {
      Success,
      Failure,
      OutOfMemory,
      OutOfBudget,
      DependenciesUnavailable
    };

    explicit OpacityMicromapManager(DxvkDevice* device);
    ~OpacityMicromapManager() { }

    void onDestroy();

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

    static bool checkIsOpacityMicromapSupported(DxvkDevice& device);

    bool doesInstanceUseOpacityMicromap(const RtInstance& instance) const;

    // Must be called before a BLAS build is kicked off if an opacity micromap may have been bound for it
    // Should be called sparingly/once a frame after opacity micromaps have been bound to BLASes
    // and corresponding batched BLASes are about to be built. 
    // It is OK for batched BLASes to contain a mix of BLASes with and without bound opacity micromaps
    void onBlasBuild(Rc<DxvkCommandList> cmdList);

  private:
    typedef fast_unordered_cache<OpacityMicromapCacheItem> OpacityMicromapCache;

    struct InstanceOmmRequests {
      uint32_t numActiveRequests = 0;
      std::vector<OmmRequest> ommRequests;
    };

    class CachedSourceData {
    public:
      uint32_t triangleOffset = 0;
      uint32_t numTriangles = 0;

      ~CachedSourceData();

      void initialize(const OmmRequest& ommRequest, fast_unordered_cache<InstanceOmmRequests>& instanceOmmRequests);
      void setInstance(const RtInstance* _instance, fast_unordered_cache<InstanceOmmRequests>& instanceOmmRequests, bool deleteParentInstanceIfEmpty = true);

      const RtInstance* getInstance() const {
        return instance; 
      }
    private:
      const RtInstance* instance = nullptr;
    };

    XXH64_hash_t bindOpacityMicromap(Rc<DxvkCommandList> cmdList, const RtInstance& instance, uint32_t billboardIndex, VkAccelerationStructureGeometryKHR& targetGeometry, const InstanceManager& instanceManager);

    void updateMemoryBudget();
    void generateInstanceOmmRequests(RtInstance& instance, const InstanceManager& instanceManager, std::vector<OmmRequest>& ommRequests);

    bool registerOmmRequestInternal(RtInstance& instance, const OmmRequest& ommRequest);
    bool addNewOmmBuildRequest(RtInstance& instance, const OmmRequest& ommRequest);
    fast_unordered_cache<CachedSourceData>::iterator registerCachedSourceData(const OmmRequest& ommRequest);
    void deleteCachedSourceData(fast_unordered_cache<CachedSourceData>::iterator sourceDataIter, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer);
    void deleteCachedSourceData(XXH64_hash_t ommSrcHash, OpacityMicromapCacheState ommCacheState, bool destroyParentInstanceOmmRequestContainer);
    bool insertToUnprocessedList(const OmmRequest& ommRequest, std::list<XXH64_hash_t>::iterator& cacheStateListIter);
    void destroyOmmData(OpacityMicromapCache::iterator& ommCacheIterator, bool destroyParentInstanceOmmRequestContainer = true);
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

    // Bound built OMMs need to be synchronized once before being used. 
    // This tracks if any such OMMs have been bound
    bool m_boundOmmsRequireSynchronization = false;
    uint32_t m_numBoundOMMs = 0;
    uint32_t m_numRequestedOMMBindings = 0;

    fast_unordered_cache<InstanceOmmRequests> m_instanceOmmRequests;

    OpacityMicromapCache m_ommCache; 
    fast_unordered_cache<CachedSourceData> m_cachedSourceData;
    std::vector<Rc<DxvkOpacityMicromap>> m_boundOMMs; // OMMs bound in a frame

    // Ordered lists starting with oldest and/or smallest inserted items 
    std::list<XXH64_hash_t> m_unprocessedList;   // Contains OMM data requests that are yet to be baked
    std::list<XXH64_hash_t> m_bakedList;         // Contains OMM items with baked OMM arrays
    std::list<XXH64_hash_t> m_builtList;         // Contains OMM items with built OMMs but require synchronization

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

    fast_unordered_cache<OMMBuildRequestStatistics> m_ommBuildRequestStatistics;

    VkDeviceSize m_amountOfMemoryMissing = 0;    // Records how much memory was missing in a frame
    OpacityMicromapMemoryManager m_memoryManager;
    std::unique_ptr<DxvkStagingDataAlloc> m_scratchAllocator;
  };
}  // namespace dxvk

