/*
* Copyright (c) 2021-2026, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_buffer.h"
#include "../dxvk_image.h"
#include "../dxvk_sampler.h"
#include "rtx_types.h"
#include "vulkan/vulkan_core.h"
#include "rtx_utils.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx_common_object.h"
#include "rtx_geometry_utils.h"

namespace dxvk 
{
  class DxvkContext;
  class DxvkDevice;
  class SceneManager;
  class RtxTextureManager;
  struct FrameBeginContext;

  struct EventHandler {
    friend struct Resources;

    // Event get called when target or downscaled resolution is changed
    using ResizeEvent = std::function<void(Rc<DxvkContext>& ctx, const VkExtent3D&)>;
    // Event get called at the beginning of a frame, used for allocate or release resources
    using FrameBeginEvent = std::function<void(Rc<DxvkContext>& ctx, const FrameBeginContext&)>;

    EventHandler(ResizeEvent&& onTargetResize, ResizeEvent&& onDownscaleResize, FrameBeginEvent&& onFrameBeginEvent) {
      if (onTargetResize) {
        onTargetResolutionResize = std::make_shared<ResizeEvent>(onTargetResize);
      }

      if (onDownscaleResize) {
        onDownscaledResolutionResize = std::make_shared<ResizeEvent>(onDownscaleResize);
      }

      if (onFrameBeginEvent) {
        onFrameBegin = std::make_shared<FrameBeginEvent>(onFrameBeginEvent);
      }
    }

  private:
    std::shared_ptr<ResizeEvent> onTargetResolutionResize = nullptr;
    std::shared_ptr<ResizeEvent> onDownscaledResolutionResize = nullptr;
    std::shared_ptr<FrameBeginEvent> onFrameBegin = nullptr;
  };

  // Returns a slice for a buffer that may not be allocated.
  // Sparse rendering's compaction buffers only exist while the pass is active,
  // and DxvkBufferSlice's constructor dereferences its argument to read the size.
  inline DxvkBufferSlice optionalBufferSlice(const Rc<DxvkBuffer>& buffer) {
    return buffer != nullptr ? DxvkBufferSlice(buffer) : DxvkBufferSlice();
  }

  struct Resources : public CommonDeviceObject {
    class AliasedResource;

    struct Resource {
      Rc<DxvkImage> image = nullptr;
      Rc<DxvkImageView> view = nullptr;

      void reset() {
        image = nullptr;
        view = nullptr;
      }

      bool isValid() const {
        return image.ptr() && view.ptr();
      }
    };

    class SharedResource : public RcObject {
    public:
      SharedResource(Resource&& _resource);
      SharedResource(Rc<DxvkBuffer>&& _buffer);
#ifdef REMIX_DEVELOPMENT  
      std::weak_ptr<const AliasedResource*> owner;
#endif
      Resource resource;
      Rc<DxvkBuffer> buffer;
    };

    enum class AccessType {
      Read,
      Write,
      ReadWrite
    };

    // AliasedResource can share a SharedResource with different AliasedResource objects
    // Usage rules:
    //  - AliasedResource takes ownership of a resource on write
    //  - AliasedResource must own a resource (i.e. wrote to it) before it can read from it
    //  - Different AliasedResources can subsequently write to a resource 
    class AliasedResource {
    public:
      AliasedResource()
#ifdef REMIX_DEVELOPMENT
        : m_thisObjectAddress(std::make_shared<const AliasedResource*>(this))
#endif
      { }

      AliasedResource(Rc<DxvkContext>& ctx,
                      const VkExtent3D& extent,
                      const VkFormat format,
                      const char* name,
                      const bool allowCompatibleFormatAliasing = false,
                      const uint32_t numLayers = 1,
                      const VkImageType imageType = VK_IMAGE_TYPE_2D,
                      const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D,
                      VkImageCreateFlags imageCreateFlags = 0,
                      const VkImageUsageFlags extraUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT,
                      const VkClearColorValue clearValue = { 0.0f, 0.0f, 0.0f, 0.0f },
                      const uint32_t mipLevels = 1);
      AliasedResource(const AliasedResource& otherAliasedResource,
                      Rc<DxvkContext>& ctx,
                      const VkExtent3D& extent,
                      const VkFormat format,
                      const char* name,
                      const uint32_t numLayers = 1,
                      const VkImageType imageType = VK_IMAGE_TYPE_2D,
                      const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);

      AliasedResource(const AliasedResource& other, const char* name);
      AliasedResource(AliasedResource& other) = delete;   // Prevent shallow copy
      AliasedResource& operator=(AliasedResource&& other);

      // Returns a resource view for a given access type
      // bool isAccessedByGPU - input condition that controls if the resource is actually accessed by the GPU. 
      //  This is useful for cases when the resource gets bound but has a conditional access within a shader
      //  and the conditional access governs mutually exclusivity among AliasedResources
      Rc<DxvkImageView> view(AccessType accessType, bool isAccessedByGPU = true) const;

      // Returns a resource image for a given access type
      // bool isAccessedByGPU - input condition that controls if the resource is actually accessed by the GPU. 
      //  This is useful for cases when the resource gets bound but has a conditional access within a shader
      //  and the conditional access governs mutually exclusivity among AliasedResources
      Rc<DxvkImage> image(AccessType accessType, bool isAccessedByGPU = true) const;

      // Returns a resource for a given access type
      // bool isAccessedByGPU - input condition that controls if the resource is actually accessed by the GPU. 
      //  This is useful for cases when the resource gets bound but has a conditional access within a shader
      //  and the conditional access governs mutually exclusivity among AliasedResources
      const Resource& resource(AccessType accessType, bool isAccessedByGPU = true) const;

      void reset();

      const char* name() const;

      bool ownsResource() const;
      void registerAccess(AccessType accessType, bool isAccessedByGPU = true) const;

      bool sharesTheSameView(const AliasedResource& other) const;

      uint32_t getWriteFrameIdx() const {
        return m_writeFrameIdx;
      }

      bool matchesWriteFrameIdx(uint32_t frameIdx) const {
        return ownsResource() && m_writeFrameIdx == frameIdx;
      }

      const DxvkImageCreateInfo& imageInfo() const {
        return m_sharedResource->resource.image->info();
      }

      VkImageViewType imageViewType() const {
        return m_view->type();
      }

      const bool empty() const {
        return m_sharedResource == nullptr || m_view == nullptr;
      }

    protected:
      AliasedResource(DxvkDevice* device, Rc<DxvkBuffer>&& buffer, const char* name);

      const Rc<DxvkBuffer>& sharedBuffer() const {
        return m_sharedResource->buffer;
      }

    private:
      void takeOwnership() const;

      // Storing device ptr using rc ptr results in compilation error which surprisingly is hard to resolve
      // Using a native pointer instead, which is fine given the reource should be torn 
      // down before the device does
      DxvkDevice* m_device = nullptr;

      // Resource that may be shared among AliasedResource objects
      Rc<SharedResource> m_sharedResource;

      Rc<DxvkImageView> m_view = nullptr;

      // Index of a frame when the shared resource was written to via this AliasedResource
      // Initialized to a large value, but not UINT32_MAX so that it does not trigger false positive 
      // when checked against a previous frame index on frame index 0
      mutable uint32_t m_writeFrameIdx = 0x0fffffff;

#ifdef REMIX_DEVELOPMENT
      // A shared ptr to the current object's address only shared with weak_ptrs so that its destruction is forwarded the weak_ptrs
      std::shared_ptr<const AliasedResource*> m_thisObjectAddress;

      // Debug name for the aliased resource
      const char* m_name = nullptr;
#endif
    };

    class AliasedBuffer : private AliasedResource {
    public:
      AliasedBuffer() = default;
      AliasedBuffer(DxvkDevice* device, Rc<DxvkBuffer>&& buffer, const char* name);
      AliasedBuffer(const AliasedBuffer& other, const char* name);
      AliasedBuffer(AliasedBuffer& other) = delete;   // Prevent shallow copy
      AliasedBuffer& operator=(AliasedBuffer&& other) = default;

      DxvkBufferSlice slice(AccessType accessType, bool isAccessedByGPU = true) const;
    };

    // a queue of N resources used over N frames
    struct ResourceQueue : public std::array<Resource, kDLFGMaxGPUFramesInFlight> {
      Resource& get() {
        return (*this)[idx];
      }

      const Resource& get() const {
        return (*this)[idx];
      }

      void next() {
        idx = (idx + 1) % size();
      }

      void rewind() {
        idx = 0;
      }

    private:
      int idx = 0;
    };

    struct RaytracingOutput {
      // Note: resources are called 'shared' if they are written by both Primary and Secondary (PSR) passes
      Resource m_sharedFlags;
      // Holds every pixel's geometry flags when the GBuffer is compacted, because m_sharedFlags only has slots for active pixels.
      // The PSR and decal passes run at every pixel, and the RTXDI gradients pass reads flags at any pixel.
      // The DLSS-RR hit distance reuses this texture's memory later in the frame.
      AliasedResource m_sharedFlagsDense;

      // These textures hold reflection PSR data slots 1 and 2 for every pixel when the GBuffer is compacted,
      // because inactive pixels have no slot in m_gbufferPSRData.
      // Slot 0 shares the memory of the dense world position.
      // Slot 1 shares the secondary virtual motion vector.
      // Slot 2 shares the composite output, and at pixels without PSR it carries the decal blend inputs instead.
      // See guidesWriteFirstHit.
      AliasedResource m_gbufferPSRData1Dense;
      AliasedResource m_gbufferPSRData2Dense;

      Resource m_sharedRadianceRG;
      Resource m_sharedRadianceB;
      AliasedResource m_sharedIntegrationSurfacePdf;
      Resource m_sharedMaterialData0;
      Resource m_sharedMaterialData1;
      Resource m_sharedMediumMaterialIndex;
      AliasedResource m_sharedBiasCurrentColorMask;
      AliasedResource m_sharedSurfaceIndex;
      Resource m_sharedSubsurfaceData;
      Resource m_sharedSubsurfaceDiffusionProfileData;
      // Terminator offset image is written by Primary and overwritten by a single chosen Secondary pass -- so it's shared,
      // as a terminator fix not that visible if we have both reflections/refractions, so we can omit it in one of them.
      Resource m_sharedTerminatorFix[2];

      Resource m_primaryAttenuation;
      Resource m_primaryWorldShadingNormal;
      // Holds the primary shading normal at every pixel. It is allocated only while the GBuffer is compacted.
      Resource m_primaryWorldShadingNormalDense;
      Resource m_primaryPerceptualRoughness;
      Resource m_primaryLinearViewZ;
      ResourceQueue m_primaryDepthQueue;
      Resource m_primaryDepth;  // points at a resource from m_primaryDepthQueue
      Resource m_primaryAlbedo;
      AliasedResource m_primaryBaseReflectivity;
      AliasedResource m_primarySpecularAlbedo;

      // These are the DLSS-RR albedo guides, which combine the primary and PSR layers at every pixel.
      // The GBuffer writes them because inactive pixels keep no secondary surface data.
      // Without sparse rendering, the prepare pass combines the layers in place instead.
      Resource m_primaryAlbedoDLSSRR;
      Resource m_primarySpecularAlbedoDLSSRR;

      AliasedResource m_primaryVirtualMotionVector;
      ResourceQueue m_primaryScreenSpaceMotionVectorQueue;
      Resource m_primaryScreenSpaceMotionVector;
      Resource m_primaryVirtualWorldShadingNormalPerceptualRoughness;
      AliasedResource m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      Resource m_primaryHitDistance;
      Resource m_primaryViewDirection;
      Resource m_primaryConeRadius;
      AliasedResource m_primaryWorldPositionWorldTriangleNormal[2];
      // Stores the world position in compacted order,
      // so that the passes launched over the active-pixel list read it contiguously.
      // The dense pair above remains the per-pixel source,
      // because temporal passes read the previous frame's world position at any pixel.
      AliasedResource m_primaryWorldPositionWorldTriangleNormalCompacted;
      Resource m_primaryPositionError;
      AliasedResource m_primaryRtxdiIlluminance[2];
      AliasedResource m_primaryRtxdiTemporalPosition;
      Resource m_primarySurfaceFlags;
      Resource m_primaryDisocclusionThresholdMix;
      AliasedResource m_primaryDisocclusionMaskForRR;
      Resource m_primaryObjectPicking;

      Resource m_secondaryAttenuation;
      Resource m_secondaryWorldShadingNormal;
      AliasedResource m_secondaryPerceptualRoughness;
      Resource m_secondaryLinearViewZ;
      Resource m_secondaryAlbedo;
      AliasedResource m_secondaryBaseReflectivity;
      AliasedResource m_secondarySpecularAlbedo;
      AliasedResource m_secondaryVirtualMotionVector;
      Resource m_secondaryVirtualWorldShadingNormalPerceptualRoughness;
      Resource m_secondaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      Resource m_secondaryHitDistance;
      AliasedResource m_secondaryViewDirection;
      AliasedResource m_secondaryConeRadius;
      AliasedResource m_secondaryWorldPositionWorldTriangleNormal;
      AliasedResource m_secondaryPositionError;
      Resource m_alphaBlendGBuffer;
      AliasedResource m_alphaBlendRadiance;

      // Resource containing 1spp radiance from indirect pass - with each pixel containing {diffuse | specular} for a {primary | secondary} surface
      AliasedResource m_indirectRadianceHitDistance;
      AliasedResource m_rayReconstructionHitDistance;
      Resource m_sparseRenderingActivePixelMask;
      Resource m_sparseRenderingPixelSamplingRate;
      Resource m_sparseRenderingActiveLocalPixelCoords;
      // Holds each pixel's compacted index counted from its tile's base.
      // Holds kInvalidCompactedIndex for a pixel that has no slot.
      Resource m_sparseRenderingCompactedPixelIndices;
      // Holds one texel per 256x128 compaction tile.
      // x is the number of active pixels in the tile.
      // y is the compacted index of the tile's first active pixel.
      // Compaction packs each tile's active pixels to the front, so this one read tells a thread launched over the screen grid
      // whether it is active and where its GBuffer is stored, without a per-pixel lookup.
      // y also lets CompactedPixelIndices count from the tile's start, which keeps that table at 16 bits per pixel.
      Resource m_sparseRenderingTileActiveCounts;

      // The first buffer holds the coordinate of every active pixel, indexed by its global compacted index.
      // The second holds the number of active pixels.
      Rc<DxvkBuffer> m_sparseRenderingActivePixelCoords;
      Rc<DxvkBuffer> m_sparseRenderingActivePixelCount;

      // Ray tracing counts invocations while compute counts groups, so the two launches need separate args.
      Rc<DxvkBuffer> m_sparseRenderingTraceRaysIndirectArgs;
      Rc<DxvkBuffer> m_sparseRenderingDispatchIndirectArgs;

      AliasedResource m_primaryDirectDiffuseRadiance;
      AliasedResource m_primaryDirectSpecularRadiance;
      AliasedResource m_primaryIndirectDiffuseRadiance;
      AliasedResource m_primaryIndirectSpecularRadiance;
      AliasedResource m_secondaryCombinedDiffuseRadiance;
      AliasedResource m_secondaryCombinedSpecularRadiance;

      AliasedResource m_indirectRayOriginDirection;
      AliasedResource m_indirectThroughputConeRadius;
      AliasedResource m_indirectFirstSampledLobeData;
      AliasedResource m_indirectFirstHitPerceptualRoughness;

      AliasedResource m_gbufferPSRData[3];

      // DLSSRR data
      AliasedResource m_primaryDepthDLSSRR;
      AliasedResource m_primaryWorldShadingNormalDLSSRR;
      Resource m_primaryScreenSpaceMotionVectorDLSSRR;

      // DLSSNR Data
      AliasedResource m_neuralRenderingOutput;
      Resource m_controlMask;
      AliasedResource m_neuralRenderingInput;

      Resource m_bsdfFactor;

      VkExtent3D m_compositeOutputExtent;
      AliasedResource m_compositeOutput;

      VkExtent3D m_finalOutputExtent;
      AliasedResource m_finalOutput;

      AliasedResource m_postFxIntermediateTexture;

      Resource m_gbufferLast;
      Resource m_reprojectionConfidence;
      Resource m_rtxdiGradients;
      AliasedResource m_rtxdiConfidence[2];
      AliasedResource m_rtxdiBestLights;

      AliasedResource m_primarySurfaceFlagsIntermediateTexture1;
      AliasedResource m_primarySurfaceFlagsIntermediateTexture2;

      AliasedBuffer m_rtxdiReservoirBuffer;
      AliasedBuffer m_transmissionPSRData;

      Rc<DxvkBuffer> m_neeCache;
      Rc<DxvkBuffer> m_neeCacheTask;
      Rc<DxvkBuffer> m_neeCacheSample;
      Resource m_neeCacheThreadTask;

      Resource m_sharedTextureCoord;
    
      Rc<DxvkBuffer> m_gpuPrintBuffer;

      Rc<DxvkBuffer> m_samplerFeedbackDevice;
      Rc<DxvkBuffer> m_samplerFeedbackReadback[kMaxFramesInFlight];

      RaytraceArgs m_raytraceArgs;

      bool isReady() const { return m_primaryLinearViewZ.image != nullptr; }
      void onFrameEnd() { m_swapTextures = !m_swapTextures; }

      const AliasedResource& getCurrentRtxdiIlluminance() const { return m_primaryRtxdiIlluminance[m_swapTextures]; }
      const AliasedResource& getPreviousRtxdiIlluminance() const { return m_primaryRtxdiIlluminance[!m_swapTextures]; }
      const AliasedResource& getCurrentRtxdiConfidence() const { return m_rtxdiConfidence[m_swapTextures]; }
      const AliasedResource& getPreviousRtxdiConfidence() const { return m_rtxdiConfidence[!m_swapTextures]; }
      const AliasedResource& getCurrentPrimaryWorldPositionWorldTriangleNormal() const { return m_primaryWorldPositionWorldTriangleNormal[m_swapTextures]; }
      const AliasedResource& getPreviousPrimaryWorldPositionWorldTriangleNormal() const { return m_primaryWorldPositionWorldTriangleNormal[!m_swapTextures]; }
      // Returns the world position addressed by compacted coordinate.
      // With sparse rendering off, the compacted coordinate is the pixel itself.
      const AliasedResource& getCompactedPrimaryWorldPositionWorldTriangleNormal() const {
        return m_raytraceArgs.sparseRenderingArgs.mode != SparseRenderingMode::Off
          ? m_primaryWorldPositionWorldTriangleNormalCompacted
          : getCurrentPrimaryWorldPositionWorldTriangleNormal();
      }
      const Resource& getCurrentSharedTerminatorFix() const { return m_sharedTerminatorFix[m_swapTextures]; }
      const Resource& getPreviousSharedTerminatorFix() const { return m_sharedTerminatorFix[!m_swapTextures]; }

    private:
      bool m_swapTextures = false;
    };

    explicit Resources(DxvkDevice* device);

    void addEventHandler(const EventHandler& events) {
      // NOTE: Implicit conversion to weak ptr
      if (events.onTargetResolutionResize) {
        m_onTargetResize.push_back(events.onTargetResolutionResize);
      }

      if (events.onDownscaledResolutionResize) {
        m_onDownscaleResize.push_back(events.onDownscaledResolutionResize);
      }

      if (events.onFrameBegin) {
        m_onFrameBegin.push_back(events.onFrameBegin);
      }
    }

    // Message function called at the beginning of the frame, usually allocate or release resources based on each pass's status
    void onFrameBegin(Rc<DxvkContext> ctx, RtxTextureManager& textureManager, const SceneManager& sceneManager, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent, bool resetHistory, bool isCameraCut);

    // Message function called when target or downscaled resolution is changed
    void onResize(Rc<DxvkContext> ctx, const VkExtent3D& downscaledExtents, const VkExtent3D& upscaledExtents);

    bool validateRaytracingOutput(const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) const;

    Resources::RaytracingOutput& getRaytracingOutput() { return m_raytracingOutput; }

    Rc<DxvkBuffer> getConstantsBuffer();
    Rc<DxvkImageView> getBlueNoiseTexture(Rc<DxvkContext> ctx);
    Rc<DxvkImageView> getValueNoiseLut(Rc<DxvkContext> ctx);
    Rc<DxvkImageView> getWhiteTexture(Rc<DxvkContext> ctx);
    Resources::Resource getSkyProbe(Rc<DxvkContext> ctx, VkFormat format = VK_FORMAT_UNDEFINED);
    Resources::Resource getSkyMatte(Rc<DxvkContext> ctx, VkFormat format = VK_FORMAT_UNDEFINED);
    Rc<DxvkImageView> getCompatibleViewForView(const Rc<DxvkImageView>& view, VkFormat format);

    Rc<DxvkSampler> getSampler(const VkFilter filter, const VkSamplerMipmapMode mipFilter,
                               const VkSamplerAddressMode addressModeU,
                               const VkSamplerAddressMode addressModeV,
                               const VkSamplerAddressMode addressModeW,
                               const VkClearColorValue borderColor = VkClearColorValue(),
                               const float mipBias = 0, const bool useAnisotropy = false);
    Rc<DxvkSampler> getSampler(const VkFilter filter, const VkSamplerMipmapMode mipFilter, 
                               const VkSamplerAddressMode addrMode, 
                               const float mipBias = 0, const bool useAnisotropy = false);
    
    // XeSS: Get sampler with automatic XeSS-specific mip bias calculation
    Rc<DxvkSampler> getSamplerWithXeSSMipBias(const VkFilter filter, const VkSamplerMipmapMode mipFilter,
                                               const VkSamplerAddressMode addressModeU,
                                               const VkSamplerAddressMode addressModeV,
                                               const VkSamplerAddressMode addressModeW,
                                               const VkClearColorValue borderColor = VkClearColorValue(),
                                               const float additionalMipBias = 0, const bool useAnisotropy = false);
    Rc<DxvkSampler> getSamplerWithXeSSMipBias(const VkFilter filter, const VkSamplerMipmapMode mipFilter, 
                                               const VkSamplerAddressMode addrMode, 
                                               const float additionalMipBias = 0, const bool useAnisotropy = false);

    Tlas& getTLAS(Tlas::Type type) { return m_tlas[type]; }

    void createConstantsBuffer();
    void createBlueNoiseTexture(Rc<DxvkContext> ctx);
    void createValueNoiseLut(Rc<DxvkContext> ctx);

    float getUpscaleRatio() const { return m_raytracingOutput.isReady() ? ((float)m_downscaledExtent.width / m_targetExtent.width) : 1.0f; }

    bool isResourceReady() const { return m_raytracingOutput.isReady(); }

    const VkExtent3D& getTargetDimensions() const { return m_targetExtent; }
    const VkExtent3D& getDownscaleDimensions() const { return m_downscaledExtent; }

    // The first getter returns the capacity that the compacted resources were actually allocated with.
    // The second returns the row length that places a global compacted index in them.
    // The third returns the extent they were allocated at.
    // A zero capacity means the resources stayed at the render extent.
    // Callers read these values rather than recompute them from the options,
    // so that changing the options cannot make the shaders address a layout that was never allocated.
    uint32_t getCompactedStorageCapacity() const { return m_compactedStorageCapacity; }
    uint32_t getCompactedStorageSquaresPerRow() const { return m_compactedStorageSquaresPerRow; }
    const VkExtent3D& getCompactedStorageExtent() const { return m_compactedStorageExtent; }

    bool areRtxdiGradientResourcesAllocated() const { return m_rtxdiGradientResourcesAllocated; }
    bool needsRtxdiGradientResources() const;
    bool areRtxdiIlluminanceResourcesAllocated() const { return m_rtxdiIlluminanceResourcesAllocated; }
    bool needsRtxdiIlluminanceResources() const;
    bool needsDisocclusionMaskForRR(uint32_t compactedStorageCapacity) const;
    bool areNrdDenoisingGuideResourcesAllocated() const { return m_nrdDenoisingGuideResourcesAllocated; }
    bool needsNrdDenoisingGuideResources() const;
    void createNrdDenoisingGuideResources(Rc<DxvkContext>& ctx);
    bool needsPrimaryDenoisingNormal() const;
    void createDlfgResourceQueues(Rc<DxvkContext>& ctx);

    static RtxTextureFormatCompatibilityCategory getFormatCompatibilityCategory(const VkFormat format);
    static bool areFormatsCompatible(const VkFormat format1, const VkFormat format2);
    static Rc<DxvkImageView> createImageView(Rc<DxvkContext>& ctx, const Rc<DxvkImage>& image, const VkFormat format,
                                             const uint32_t numLayers, const VkImageViewType imageViewType, 
                                             const VkImageUsageFlags extraUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT, const uint32_t mipLevels = 1);
    static Resource createImageResource(Rc<DxvkContext>& ctx, const char *name, const VkExtent3D& extent, const VkFormat format,
                                        const uint32_t numLayers = 1, const VkImageType imageType = VK_IMAGE_TYPE_2D,
                                        const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D,
                                        const VkImageCreateFlags imageCreateFlags = 0, const VkImageUsageFlags extraUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT,
                                        const VkClearColorValue clearValue = { 0.0f, 0.0f, 0.0f, 0.0f }, const uint32_t mipLevels = 1);

#ifdef REMIX_DEVELOPMENT
    static std::unordered_map<const DxvkImageView*, std::string> s_resourcesViewMap;
    static std::unordered_set<const DxvkImageView*> s_dynamicAliasingResourcesSet;
    static bool s_queryAliasing;
    static std::string s_resourceAliasingQueryText;
    static bool s_startAliasingAnalyzer;
    static std::string s_aliasingAnalyzerResultText;
#endif

  private:
    Resources(Resources const&) = delete;
    Resources& operator=(Resources const&) = delete;

    RaytracingOutput m_raytracingOutput;

    Rc<DxvkBuffer> m_constants;
    Rc<DxvkImage> m_blueNoiseTex;
    Rc<DxvkImageView> m_blueNoiseTexView;
    Rc<DxvkImage> m_valueNoiseLut;
    Rc<DxvkImageView> m_valueNoiseLutView;
    Rc<DxvkImage> m_whiteTex;
    Rc<DxvkImageView> m_whiteTexView;

    Resources::Resource m_skyProbe;
    Resources::Resource m_skyMatte;

    fast_unordered_cache<Rc<DxvkSampler>> m_samplerCache;
    fast_unordered_cache<std::pair<Rc<DxvkImageView>, uint32_t>> m_viewCache;

    Tlas m_tlas[Tlas::Type::Count];

    VkExtent3D m_downscaledExtent = { 0, 0, 0 };
    uint32_t m_compactedStorageCapacity = 0;
    uint32_t m_compactedStorageSquaresPerRow = 1;
    // Holds the extent that the compacted resources were allocated at, which the per-frame aliased resources reuse.
    VkExtent3D m_compactedStorageExtent = { 0, 0, 0 };
    bool m_rtxdiGradientResourcesAllocated = true;
    bool m_rtxdiIlluminanceResourcesAllocated = true;
    bool m_compactedGBufferResourcesAllocated = false;
    bool m_disocclusionMaskForRRAllocated = true;
    bool m_nrdDenoisingGuideResourcesAllocated = false;
    bool m_primaryDenoisingNormalAllocated = false;
    bool m_dlfgResourceQueueAllocated = false;
    bool m_dlssNeuralRenderingResourcesAllocated = false;
    VkExtent3D m_targetExtent = { 0, 0, 0 };

    using ResizeEventList = std::vector<std::weak_ptr<EventHandler::ResizeEvent>>;
    using FrameBeginEventList = std::vector<std::weak_ptr<EventHandler::FrameBeginEvent>>;
    ResizeEventList m_onTargetResize;
    ResizeEventList m_onDownscaleResize;
    FrameBeginEventList m_onFrameBegin;

    static void executeResizeEventList(ResizeEventList& eventList, Rc<DxvkContext>& ctx, const VkExtent3D& extent);
    static void executeFrameBeginEventList(FrameBeginEventList& eventList, Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx);

    void createRaytracingOutput(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    void createTargetResources(Rc<DxvkContext>& ctx);

    void createNeuralRenderingOutput(Rc<DxvkContext>& ctx);
    void updateDlssNeuralRenderingResources(Rc<DxvkContext>& ctx);

    void createDownscaledResources(Rc<DxvkContext>& ctx);
    bool areDownscaledResourcesCurrent() const;
  };

  // State passed to RtxPass::onFrameBegin() callbacks
  struct FrameBeginContext {
    VkExtent3D downscaledExtent;
    VkExtent3D targetExtent;
    float frameTimeMilliseconds;
    bool resetHistory;
    bool isCameraCut;
  };

  class RtxPass {
  public:
    // The constructor will register callback functions to Resources class
    RtxPass(DxvkDevice* device);

    virtual ~RtxPass() = default;

    // In order to avoid potential race condition between imgui and injectRTX() running asynchronously, 
    // m_isActive is updated using derived class's isEnabled(), which generally keys off of an rtx option, in onFrameBegin() at start of the frame.
    // This way isActive() can be used to consistently check the pass being active or not during the rest of the frame 
    // Note: a protected isEnabled() method is used to check ImGUI or other enablement state that converts its result to m_isActive in onFrameBegin()
    // Note2: externall callers and internal checks for pass being active or not should strictly use isActive() to check pass being active or not
    //        in order to preserve consistent state during frame on render timeline
    bool isActive() const { return m_isActive; }

  protected:
    // Event callback functions.
    // Derived class must call this base class implementation first from its override. 
    // The function adjusts active state of the pass and thus the override must check isActive()
    // after calling the base class implementation
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx);

  private:
    // Event callback functions
    void onTargetResize(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent);
    void onDownscaledResize(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent);

    bool m_isActive = false;
    EventHandler m_events;
  protected:
    // Interface to determine whether a pass is enabled
    // Note: this state updates m_isActive state at onFrameBegin() time
    virtual bool isEnabled() const = 0;

    // Called from RtxPass::onFrameBegin() on RtxPass (de)activation events.
    // onActivation returns whether it succeeded.
    // When this function should fail, the derived class implementation needs to make any 
    // runtime adjustments to ensure valid runtime state from this point onwards. 
    // Note: it should not enable any other RtxPasses since it is not guaranteed their onFrameBegin() callback 
    // would be called within the same frame
    virtual bool onActivation(Rc<DxvkContext>& /* unused */) { return true; }
    virtual void onDeactivation() { }

    // Resource management functions, should provide implementation if a pass has custom resources
    virtual void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) { }
    virtual void releaseTargetResource() { }
    virtual void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) { }
    virtual void releaseDownscaledResource() { }
  };

} // namespace dxvk

