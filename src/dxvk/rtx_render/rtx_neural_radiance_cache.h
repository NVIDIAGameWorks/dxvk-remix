/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_option.h"
#include "rtx_resources.h"
#include <random>

namespace nrc {
  struct ContextSettings;
}

namespace dxvk {
  class RtxContext;
  class DxvkDevice;
  class NrcContext;

  // Manages resources for and interactions with Neural Radiance Cache (NRC) context
  class NeuralRadianceCache : public RtxPass {
  public:

    enum class QualityPreset : uint32_t {
      Medium = 0,
      High,
      Ultra,

      Count
    };

    struct NrcOptions {
      friend class NeuralRadianceCache;
      friend class ImGUI;

      RTX_OPTION("rtx.neuralRadianceCache", bool, learnIrradiance, true, "");
      RTX_OPTION_ENV("rtx.neuralRadianceCache", bool, includeDirectLighting, true, "RTX_NRC_INCLUDE_DIRECT_LIGHTING", "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, resetHistory, false, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, allowRussianRouletteOnUpdate, false, "");
      RTX_OPTION("rtx.neuralRadianceCache", uint32_t, targetNumTrainingIterations, 4,
                 "This controls the target number of training iterations to perform each frame,\n"
                 "which in turn determines the ideal number of training records that\n"
                 "the training/update path tracing pass is expected to generate.\n"
                 "Each training batch contains 16K training records derived from path segments\n"
                 "in the the NRC update path tracing pass. For example, 4 training iterations results in 64K training records.\n"
                 "Higher number results in more responsive NRC cache at the cost of increased workload.\n");
      public: static void onMaxNumTrainingIterationsChanged(DxvkDevice* device);
      RTX_OPTION_ARGS("rtx.neuralRadianceCache", uint32_t, maxNumTrainingIterations, 16,
                 "This controls the max number of training iterations to perform in a frame.\n"
                 "When the pathtracer generates more training records than the ideal number of training records\n"
                 "based on \"targetNumTrainingIterations\", this parameter allows NRC SDK to use those records\n"
                 "to speed up the training at an increased cost instead of throwing the records away.\n"
                 "Generally the pathtracer will try to reach the ideal number training records,\n"
                 "but in cases when its calibrating the workload during \"numFramesToSmoothOutTrainingDimensions\" frames it can overshoot it.\n"
                 "This can happen when the cache resets on a level load or camera cut. In such cases it is preferrable to speed the training up\n"
                 "to make the indirect lighting inference more accurate faster.",
                args.onChangeCallback = &onMaxNumTrainingIterationsChanged);
      RTX_OPTION("rtx.neuralRadianceCache", float, averageTrainingBouncesPerPath, 2.15,
                 "Average number of bounces per path used to calculate max training dimensions.\n"
                 "Lower values result in higher max training dimensions and higher memory requirements by NRC.\n"
                 "Lower values boost training dimensions to allows for more training records to get generated in scenes with very few / 1-2 bounces\n"
                 "(i.e. most of the view being into the sky with small geometric footprint on-screen).\n");
      RTX_OPTION("rtx.neuralRadianceCache", int, trainingMaxPathBouncesBiasInQualityPresets, 0,
                 "This is a value added to the default \"trainingMaxPathBounces\" set by NRC quality presets.\n"
                 "Set to negative value to lower the max number of training bounces and to a higher value to increase it in each quality preset.");
      RTX_OPTION("rtx.neuralRadianceCache", uint32_t, numFramesToSmoothOutTrainingDimensions, 16, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, trainCache, true, "");
      static void onEnableDebugResolveModeChanged(DxvkDevice* device);
      RTX_OPTION_ARGS("rtx.neuralRadianceCache", bool, enableDebugResolveMode, false, "", args.environment="RTX_NRC_ENABLE_DEBUG_RESOLVE_MODE", args.onChangeCallback = &onEnableDebugResolveModeChanged);
      static void onDebugResolveModeChanged(DxvkDevice* device);
      inline static bool s_nrcPrevDebugResolveIsEnabled = false;
      inline static bool s_nrcDebugBufferIsRequired { false };
      RTX_OPTION_ARGS("rtx.neuralRadianceCache", NrcResolveMode, debugResolveMode, NrcResolveMode::AddQueryResultToOutput, "Debug Visualization Mode.", args.environment="RTX_NRC_DEBUG_RESOLVE_MODE", args.onChangeCallback = &onDebugResolveModeChanged);
      RTX_OPTION("rtx.neuralRadianceCache", uint32_t, jitterSequenceLength, 128,
                 "Halton sequence length to use for jittering training path to pixel mapping every frame.\n"
                 "The sequence length governs how often the jitter distribution repeats. Setting this to 0 disables jittering."
                 "Training paths are mapped to a subset of pixels every frame, generally there is 1 training path per 100 pixels.\n"
                 "The value to use should be same or slightly higher than the number of pixels per training path so that all pixels get cycled through over a smaller time window.");
      RTX_OPTION("rtx.neuralRadianceCache", uint8_t, trainingMaxPathBounces, 15,
                "The maximum number of indirect bounces the path will be allowed to complete during NRC Update path tracing pass. Must be < 16.\n"
                "NRC requires this value to be fairly high (8+) for best lighting signal reconstruction.\n"
                "This value can be lower in games with darker surfaces and higher in games with albedos close to 1\n"
                "and/or to reconstruct lighting phenomena requiring many bounces.\n"
                "Setting this parameter to a higher has no to minor performance and moderate memory requirements (~10MiB per bounce) impact.");
      RTX_OPTION("rtx.neuralRadianceCache", bool, clearBuffersOnFrameStart, false, "Clears buffers for NRC before they are written to by the the Pathtracer.");
      RTX_OPTION("rtx.neuralRadianceCache", bool, resolveAddPathTracedRadiance, true, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, resolveAddNrcQueriedRadiance, true, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, enableNrcResolver, false, "Enables NRC radiance resolve by NRC's resolver. Disable to use Remix's resolver.");
      RTX_OPTION("rtx.neuralRadianceCache", float, smallestResolvableFeatureSizeMeters, 0.01f, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, skipDeltaVertices, true, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, sceneBoundsWidthMeters, 200.f, 
                 "Minimum width of a 3D axis-aligned bounding box in meters that will cover any in-game scene.\n"
                 "The width must be large enough to cover any scene and its geometry to ensure NRC cache coverage.\n"
                 "The width must be large enough to contain any geometry that can be loaded during gameplay at any point.\n"
                 "Note, \"rtx.neuralRadianceCache.resetSceneBoundsOnCameraCut\" controls whether the bounding box coverage resets on camera cuts.\n"
                 "Note, larger values will result in increased memory and performance costs of NRC cache.");

      RTX_OPTION("rtx.neuralRadianceCache", bool, resetSceneBoundsOnCameraCut, true,
                 "Resets scene bounds used by NRC Cache when a camera cut occurs.\n"
                 "This is strongly recommended to leave enabled as otherwise the scene bounds would have to cover all geometry\n"
                 "across all camera cuts (i.e. when loading into a new level), which can be prohibitively expensive requiring\n"
                 "very large scene bounding box to cover geometry that can be loaded in from any level.\n");
      RTX_OPTION("rtx.neuralRadianceCache", float, terminationHeuristicThreshold, 0.1f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, trainingTerminationHeuristicThreshold, 0.25f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, proportionPrimarySegmentsToTrainOn, 0.02f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, proportionTertiaryPlusSegmentsToTrainOn, 1.f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, proportionUnbiasedToSelfTrain, 1.f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, proportionUnbiased, 0.06f, "");
      RTX_OPTION("rtx.neuralRadianceCache", float, selfTrainingAttenuation, 1.f, "");
      RTX_OPTION("rtx.neuralRadianceCache", bool, enableCalculateTrainingLoss, false, "Enables calculation of a training loss. Imposes a performance penalty.");
      RTX_OPTION("rtx.neuralRadianceCache", bool, enableAdaptiveTrainingDimensions, true, "Enables adaptive training dimensions that scale based off pathtracer's execution behavior on a given scene.");

      static void onQualityPresetChanged(DxvkDevice* device);
      RTX_OPTION_ARGS("rtx.neuralRadianceCache", QualityPreset, qualityPreset, QualityPreset::Ultra,
                      "Quality Preset: Medium (0), High (1), Ultra (2).\n"
                      "Adjusts quality of RTX Neural Radiance Cache (NRC):\n"
                      "  - How quickly path-tracer terminates paths into NRC cache. It terminates quicker on lower presets.\n"
                      "  - Granularity of the cache - i.e. the smallest resolvable feature size. The cache is less precise on lower presets.\n"
                      "  - Responsiveness of the cache. The cache is more responsive to dynamic lighting changes on higher presets.\n"
                      "Lower quality presets result in faster path-tracing with fewer bounces that may result in lower quality indirect lighting.\n"
                      "Higher quality presets result in more responsive and detailed indirect lighting.",
                      args.environment = "RTX_NRC_QUALITY_PRESET",
                      args.onChangeCallback = &onQualityPresetChanged);
      inline static QualityPreset s_prevQualityPreset = QualityPreset::Count;

      RTX_OPTION("rtx.neuralRadianceCache", float, luminanceClampMultiplier, 0.f,
                 "Luminance based clamp multiplier to use for clamping radiance passed to NRC during training.\n"
                 "0: disables clamping.\n"
                 "The clamp value is calculated as \"luminanceClampMultiplier\" * \"maxExpectedAverageRadianceValue\".");
      RTX_OPTION("rtx.neuralRadianceCache", float, maxExpectedAverageRadianceValue, 2.5f, 
                 "NRC works better when the radiance values it sees internally are in a 'friendly' range for it.\n"
                 "Applications often have quite different scales for their radiance units,\n"
                 "so we need to be able to scale these units in order to get that nice NRC - friendly range.\n"
                 "Set the value to an average radiance that you see in your bright scene (e.g.outdoors in daylight).");
    };

    enum class ResourceType : uint8_t {
      QueryPathInfo = 0,
      TrainingPathInfo,
      TrainingPathVertices,
      QueryRadianceParams,
      Counters,

      Count
    };

    NeuralRadianceCache(DxvkDevice* device);
    ~NeuralRadianceCache();

    // Returns if NRC is supported
    static bool checkIsSupported(const DxvkDevice* device);

    // To be called after both query and training PT passes completed to get a resolved radiance output
    void dispatchTrainingAndResolve(RtxContext& ctx,  const Resources::RaytracingOutput& rtOutput);

    // Invoked after the command list has been submitted.
    // The command queue must be the same one that was used to execute all the previous command lists
    void onFrameEnd(Resources::RaytracingOutput& rtOutput);

    void showImguiSettings(DxvkContext& ctx);

    // Updates NRC constants in raytraceArgs. 
    // This must be called after onFrameBegin() in a frame
    void setRaytraceArgs(RaytraceArgs& constants);

    DxvkBufferSlice getBufferSlice(RtxContext& ctx, ResourceType resourceType);

    VkExtent3D calcRaytracingResolution() const;

    void bindGBufferPathTracingResources(RtxContext& ctx);
    void bindIntegrateIndirectPathTracingResources(RtxContext& ctx);

    const Vector2& getNumQueryPixelsPerTrainingPixel() const;
    bool isUpdateResolveModeActive() const;

    void setQualityPreset(QualityPreset nrcQualityPreset);

    bool isResettingHistory();

  private:
    // Overrides for inherited RtxPass methods
    virtual void onFrameBegin(Rc<DxvkContext>& ctx, const FrameBeginContext& frameBeginCtx) override;
    virtual bool onActivation(Rc<DxvkContext>& ctx) override;
    virtual void onDeactivation() override;
    virtual void releaseDownscaledResource() override;
    virtual void createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) override;

    bool isEnabled() const override;
    bool initialize(DxvkDevice& device);
    void releaseResources();
    void dispatchResolve(RtxContext& ctx, const Resources::RaytracingOutput& rtOutput);
    void copyNumberOfTrainingRecords(RtxContext& ctx);
    void readAndResetNumberOfTrainingRecords();
    void calculateActiveTrainingDimensions(float frameTimeMilliseconds, bool forceReset);
    uint32_t calculateNumTrainingIterations();
    uint8_t calculateTrainingMaxPathBounces() const;

    uint32_t calculateTargetNumTrainingRecords() const;

    // Delaying definition to cpp so that nrc header does not need to be included in this scope
    std::unique_ptr<nrc::ContextSettings> m_nrcCtxSettings;

    Rc<NrcContext>         m_nrcCtx;
    Rc<DxvkBuffer>         m_numberOfTrainingRecordsStaging;   // CPU visible buffer keeping a track of number of NRC training records

    // Resources for integrated surface radiance for training path rays in GBuffer pass.
    // These are reloaded on path continuation in indirect pass since NRC library interaction
    // is initiated at that point
    Resources::Resource    m_trainingGBufferSurfaceRadianceRG;
    Resources::Resource    m_trainingGBufferSurfaceRadianceB;

    // Query path data 0 is only allocated when include direct lighting is disabled,
    // which is not the default behavior, so there's no real need to have to alias it.
    Resources::Resource           m_queryPathData0;
    Resources::AliasedResource    m_queryPathData1;
    Resources::AliasedResource    m_trainingPathData1;

    nrc_uint2              m_activeTrainingDimensions = nrc_uint2 { UINT32_MAX, UINT32_MAX };
    uint32_t               m_numberOfTrainingRecords = 0;   // Number of training records reported by NRC - reported with kMaxFramesInFlight frame delay
    float                  m_smoothedNumberOfTrainingRecords = 0.f;
    uint32_t               m_smoothingResetFrameIdx = UINT32_MAX;

    uint32_t               m_numFramesAccumulatedForResolveMode = 0;
    bool                   m_initSceneBounds = true;

    bool                   m_resetHistory = false;
    VkDeviceSize           m_nrcVideoMemoryUsage = 0;

    float                  m_trainingLoss = 0.f;

    Vector2                m_numQueryPixelsPerTrainingPixel = Vector2 { 0.f, 0.f };
    bool                   m_enableDebugBuffers = false;
    bool                   m_delayedEnableCustomNetworkConfig;

    Vector3                m_sceneBoundsMin = Vector3 { FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3                m_sceneBoundsMax = Vector3 { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    // Each training iteration processes 16K training records
    static constexpr uint32_t     kNumTrainingRecordsPerIteration = 16 * 1024;
   };
} // namespace dxvk
