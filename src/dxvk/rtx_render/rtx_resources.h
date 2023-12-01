/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

  struct EventHandler {
    friend struct Resources;

    // Event get called when target or downscaled resolution is changed
    using ResizeEvent = std::function<void(Rc<DxvkContext>& ctx, const VkExtent3D&)>;
    // Event get called at the beginning of a frame, used for allocate or release resources
    using FrameBeginEvent = std::function<void(Rc<DxvkContext>& ctx, const VkExtent3D&, const VkExtent3D&)>;

    EventHandler(ResizeEvent&& onTargetResize, ResizeEvent&& onDownscaleResize, FrameBeginEvent&& onFrameBeginEvent) {
      if(onTargetResize)
        onTargetResolutionResize = std::make_shared<ResizeEvent>(onTargetResize);

      if (onDownscaleResize)
        onDownscaledResolutionResize = std::make_shared<ResizeEvent>(onDownscaleResize);

      if (onFrameBeginEvent)
        onFrameBegin = std::make_shared<FrameBeginEvent>(onFrameBeginEvent);
    }

  private:
    std::shared_ptr<ResizeEvent> onTargetResolutionResize = nullptr;
    std::shared_ptr<ResizeEvent> onDownscaledResolutionResize = nullptr;
    std::shared_ptr<FrameBeginEvent> onFrameBegin = nullptr;
  };

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

    struct MipMapResource {
      Rc<DxvkImage> image = nullptr;
      std::vector<Rc<DxvkImageView>> view;
      Rc<DxvkImageView> mipMapView;

      void reset() {
        image = nullptr;
        mipMapView = nullptr;
        view.clear();
      }
    };

    class SharedResource : public RcObject {
    public:
      SharedResource(Resource _resource) : resource(_resource) { }
#ifdef REMIX_DEVELOPMENT  
      std::weak_ptr<const AliasedResource*> owner;
#endif
      Resource resource;
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
                      const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);
      AliasedResource(const AliasedResource& otherAliasedResource,
                      Rc<DxvkContext>& ctx,
                      const VkExtent3D& extent,
                      const VkFormat format,
                      const char* name,
                      const uint32_t numLayers = 1,
                      const VkImageType imageType = VK_IMAGE_TYPE_2D,
                      const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);

      AliasedResource(const AliasedResource& other, const char* name = nullptr);
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

    private:
      int idx = 0;
    };

    struct RaytracingOutput {

      Resource m_volumeReservoirs[2];
      Resource m_volumeAccumulatedRadiance[2];
      Resource m_volumeFilteredRadiance;
      Resource m_volumePreintegratedRadiance;

      Resource m_sharedFlags;
      Resource m_sharedRadianceRG;
      Resource m_sharedRadianceB;
      AliasedResource m_sharedIntegrationSurfacePdf;
      Resource m_sharedMaterialData0;
      Resource m_sharedMaterialData1;
      Resource m_sharedMediumMaterialIndex;
      AliasedResource m_sharedBiasCurrentColorMask;
      Resource m_sharedSurfaceIndex;
      Resource m_sharedSubsurfaceData;

      Resource m_primaryAttenuation;
      Resource m_primaryWorldShadingNormal;
      Resource m_primaryWorldInterpolatedNormal;
      Resource m_primaryPerceptualRoughness;
      Resource m_primaryLinearViewZ;
      ResourceQueue m_primaryDepthQueue;
      Resource m_primaryDepth;  // points at a resource from m_primaryDepthQueue
      Resource m_primaryAlbedo;
      AliasedResource m_primaryBaseReflectivity;
      AliasedResource m_primarySpecularAlbedo;
      Resource m_primaryVirtualMotionVector;
      ResourceQueue m_primaryScreenSpaceMotionVectorQueue;
      Resource m_primaryScreenSpaceMotionVector;
      Resource m_primaryVirtualWorldShadingNormalPerceptualRoughness;
      Resource m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising;
      Resource m_primaryHitDistance;
      Resource m_primaryViewDirection;
      Resource m_primaryConeRadius;
      Resource m_primaryWorldPositionWorldTriangleNormal[2];
      Resource m_primaryPositionError;
      AliasedResource m_primaryRtxdiIlluminance[2];
      Resource m_primaryRtxdiTemporalPosition;
      Resource m_primarySurfaceFlags;
      Resource m_primaryDisocclusionThresholdMix; // for NRD
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
      AliasedResource m_decalMaterial;
      AliasedResource m_decalEmissiveRadiance;
      Resource m_alphaBlendGBuffer;
      AliasedResource m_alphaBlendRadiance;

      // Resource containing 1spp radiance from indirect pass - with each pixel containing {diffuse | specular} for a {primary | secondary} surface
      AliasedResource m_indirectRadianceHitDistance;

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

      AliasedResource m_gbufferPSRData[7];
      
      Resource m_bsdfFactor;
      Resource m_bsdfFactor2;

      VkExtent3D m_compositeOutputExtent;
      AliasedResource m_compositeOutput;
      AliasedResource m_lastCompositeOutput;

      Resource m_finalOutput;

      Resource m_postFxIntermediateTexture;

      Resource m_gbufferLast;
      Resource m_reprojectionConfidence;
      Resource m_rtxdiGradients;
      AliasedResource m_rtxdiConfidence[2];
      AliasedResource m_rtxdiBestLights;

      AliasedResource m_primarySurfaceFlagsIntermediateTexture1;
      AliasedResource m_primarySurfaceFlagsIntermediateTexture2;

      Rc<DxvkBuffer> m_rtxdiReservoirBuffer;

      AliasedResource m_restirGIRadiance;
      Resource m_restirGIHitGeometry;
      Rc<DxvkBuffer> m_restirGIReservoirBuffer;
      Rc<DxvkBuffer> m_neeCache;
      Rc<DxvkBuffer> m_neeCacheTask;
      Rc<DxvkBuffer> m_neeCacheSample;
      Resource m_neeCacheThreadTask;

      Resource m_sharedTextureCoord;

      VkExtent3D m_froxelVolumeExtent;
      uint32_t m_numFroxelVolumes;
      
      Rc<DxvkBuffer> m_gpuPrintBuffer;

      RaytraceArgs m_raytraceArgs;

      bool isReady() const { return m_primaryLinearViewZ.image != nullptr; }
      void onFrameEnd() { m_swapTextures = !m_swapTextures; }

      const AliasedResource& getCurrentRtxdiIlluminance() const { return m_primaryRtxdiIlluminance[m_swapTextures]; }
      const AliasedResource& getPreviousRtxdiIlluminance() const { return m_primaryRtxdiIlluminance[!m_swapTextures]; }
      const AliasedResource& getCurrentRtxdiConfidence() const { return m_rtxdiConfidence[m_swapTextures]; }
      const AliasedResource& getPreviousRtxdiConfidence() const { return m_rtxdiConfidence[!m_swapTextures]; }
      const Resource& getCurrentPrimaryWorldPositionWorldTriangleNormal() const { return m_primaryWorldPositionWorldTriangleNormal[m_swapTextures]; }
      const Resource& getPreviousPrimaryWorldPositionWorldTriangleNormal() const { return m_primaryWorldPositionWorldTriangleNormal[!m_swapTextures]; }
      const Resource& getCurrentVolumeReservoirs() const { return m_volumeReservoirs[m_swapTextures]; }
      const Resource& getPreviousVolumeReservoirs() const { return m_volumeReservoirs[!m_swapTextures]; }
      const Resource& getCurrentVolumeAccumulatedRadiance() const { return m_volumeAccumulatedRadiance[m_swapTextures]; }
      const Resource& getPreviousVolumeAccumulatedRadiance() const { return m_volumeAccumulatedRadiance[!m_swapTextures]; }

    private:
      bool m_swapTextures = false;
    };

    explicit Resources(DxvkDevice* device);

    void addEventHandler(const EventHandler& events) {
      // NOTE: Implicit conversion to weak ptr
      if(events.onTargetResolutionResize)
        m_onTargetResize.push_back(events.onTargetResolutionResize);
      
      if(events.onDownscaledResolutionResize)
        m_onDownscaleResize.push_back(events.onDownscaledResolutionResize);

      if(events.onFrameBegin)
        m_onFrameBegin.push_back(events.onFrameBegin);
    }

    // Message function called at the beginning of the frame, usually allocate or release resources based on each pass's status
    void onFrameBegin(Rc<DxvkContext> ctx, RtxTextureManager& textureManager, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    // Message function called when target or downscaled resolution is changed
    void onResize(Rc<DxvkContext> ctx, const VkExtent3D& downscaledExtents, const VkExtent3D& upscaledExtents);

    bool validateRaytracingOutput(const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent) const;

    Resources::RaytracingOutput& getRaytracingOutput() { return m_raytracingOutput; }

    Rc<DxvkBuffer> getConstantsBuffer();
    Rc<DxvkImageView> getBlueNoiseTexture(Rc<DxvkContext> ctx);
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

    Tlas& getTLAS(Tlas::Type type) { return m_tlas[type]; }

    void createConstantsBuffer();
    void createBlueNoiseTexture(Rc<DxvkContext> ctx);

    float getUpscaleRatio() const { return m_raytracingOutput.isReady() ? ((float)m_downscaledExtent.width / m_targetExtent.width) : 1.0f; }

    bool isResourceReady() const { return m_raytracingOutput.isReady(); }

    const VkExtent3D& getTargetDimensions() const { return m_targetExtent; }
    const VkExtent3D& getDownscaleDimensions() const { return m_downscaledExtent; }

    void requestObjectPickingImages(bool enable) {
      m_objectPickingImagesRequired = enable;
    }

    static const uint32_t kInvalidFormatCompatibilityCategoryIndex = UINT32_MAX;
    static uint32_t getFormatCompatibilityCategoryIndex(const VkFormat format);
    static bool areFormatsCompatible(const VkFormat format1, const VkFormat format2);
    static Rc<DxvkImageView> createImageView(Rc<DxvkContext>& ctx, const Rc<DxvkImage>& image, const VkFormat format,
                                             const uint32_t numLayers, const VkImageViewType imageViewType, 
                                             bool isColorAttachment = false);
    static Resource createImageResource(Rc<DxvkContext>& ctx, const char *name, const VkExtent3D& extent, const VkFormat format,
                                        const uint32_t numLayers = 1, const VkImageType imageType = VK_IMAGE_TYPE_2D,
                                        const VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D,
                                        const VkImageCreateFlags imageCreateFlags = 0, bool isColorAttachment = false);
    
    static MipMapResource createMipmapResource(Rc<DxvkContext> ctx, const VkExtent3D& extend, VkFormat format, int mipLevel, const char* name);

  private:
    Resources(Resources const&) = delete;
    Resources& operator=(Resources const&) = delete;

    RaytracingOutput m_raytracingOutput;

    Rc<DxvkBuffer> m_constants;
    Rc<DxvkImage> m_blueNoiseTex;
    Rc<DxvkImageView> m_blueNoiseTexView;
    Rc<DxvkImage> m_whiteTex;
    Rc<DxvkImageView> m_whiteTexView;

    Resources::Resource m_skyProbe;
    Resources::Resource m_skyMatte;

    fast_unordered_cache<Rc<DxvkSampler>> m_samplerCache;
    fast_unordered_cache<std::pair<Rc<DxvkImageView>, uint32_t>> m_viewCache;

    Tlas m_tlas[Tlas::Type::Count];

    VkExtent3D m_downscaledExtent = { 0, 0, 0 };
    VkExtent3D m_targetExtent = { 0, 0, 0 };

    bool m_objectPickingImagesRequired = false;

    using ResizeEventList = std::vector<std::weak_ptr<EventHandler::ResizeEvent>>;
    using FrameBeginEventList = std::vector<std::weak_ptr<EventHandler::FrameBeginEvent>>;
    ResizeEventList m_onTargetResize;
    ResizeEventList m_onDownscaleResize;
    FrameBeginEventList m_onFrameBegin;

    static void executeResizeEventList(ResizeEventList& eventList, Rc<DxvkContext>& ctx, const VkExtent3D& extent);
    static void executeFrameBeginEventList(FrameBeginEventList& eventList, Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    void createRaytracingOutput(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

    void createTargetResources(Rc<DxvkContext>& ctx);

    void createDownscaledResources(Rc<DxvkContext>& ctx);
  };

  class RtxPass {
  public:
    // The constructor will register callback functions to Resources class
    RtxPass(DxvkDevice* device);

    // In order to avoid potential race condition between imgui and injectRTX(), update m_shouldDispatch from rtx option at the beginning of injectRTX(),
    // then use the result to determine whether the pass should be dispatched.
    bool shouldDispatch() { return m_shouldDispatch; }

  protected:
    // Event callback functions
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent, const VkExtent3D& targetExtent);

  private:
    // Event callback functions
    void onTargetResize(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent);
    void onDownscaledResize(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent);

    bool m_shouldDispatch = false;
    EventHandler m_events;
  protected:
    // Interface to determine whether a pass is enabled.
    virtual bool isActive() = 0;

    // Resource management functions, should provide implementation if a pass has custom resources
    virtual void createTargetResource(Rc<DxvkContext>& ctx, const VkExtent3D& targetExtent) { }
    virtual void releaseTargetResource() { }
    virtual void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) { }
    virtual void releaseDownscaledResource() { }
  };

} // namespace dxvk

