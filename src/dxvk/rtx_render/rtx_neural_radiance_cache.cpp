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

#include "rtx_neural_radiance_cache.h"
#include "dxvk_device.h"
#include "rtx.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx_render/rtx_shader_manager.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx/pass/raytrace_args.h"
#include "rtx_nrc_context.h"
#include <nrc\Include\NrcStructures.h>
#include "rtx_camera.h"
#include "rtx_debug_view.h"

#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/pass/integrate/integrate_indirect_binding_indices.h"
#include "rtx/pass/nrc/nrc_resolve_binding_indices.h"
#include <rtx_shaders/nrc_resolve.h>

namespace dxvk {
  RemixGui::ComboWithKey<NrcResolveMode> nrcDebugResolveModeCombo {
  "NRC Debug Visualization Mode",
  RemixGui::ComboWithKey<NrcResolveMode>::ComboEntries { {
      {NrcResolveMode::AddQueryResultToOutput, "Add Query Result To Output",
      "Takes the query result and adds it to the output buffer" },

      // Need to support accumulation in the debug view for this to work
      // {NrcResolveMode::AddQueryResultToOutput, "Add Query Result To Output"},
      {NrcResolveMode::ReplaceOutputWithQueryResult, "Replace Output With Query Result", 
       "Overwrites the output buffer with the query results" },
      {NrcResolveMode::TrainingBounceHeatMap, "Training Bounce Heat Map",
      "Shows a heatmap for the number of training bounces.\n"
      "You should see more bounces in corners, and from smooth surfaces.\n"
      "How the number of vertices in the training path translates to colors:\n"
      "          1 : Dark Red           ( 0.5, 0,   0   )\n"
      "          2 : Bright Red         ( 1,   0,   0   )\n"
      "          3 : Dark Yellow        ( 0.5, 0.5, 0   )\n"
      "          4 : Green              ( 0,   1,   0   )\n"
      "          5 : Dark Cyan          ( 0,   0.5, 0.5 )\n"
      "          6 : Blue               ( 0,   0,   1   )\n"
      "          7 : Bleugh (?)         ( 0.5, 0.5, 1   )\n"
      "Miss or > 8 : White              ( 1,   1,   1   )" },
      {NrcResolveMode::TrainingBounceHeatMapSmoothed, "Training Bounce Heat Map Smoothed",
      "Same as TrainingBounceHeatMap, but smoothed over time\n"
      "to give a result more like you would see with accumulation." },
      {NrcResolveMode::PrimaryVertexTrainingRadiance, "Primary Vertex Training Radiance",
      "Shows the training radiance for the primary ray segment.\n"
      "This should look like a low resolution version of the path-traced result, and it will be noisy.\n"
      "The radiance shown here will include 'self training', where cache\n"
      "lookups are injected at the tails of many of the paths.\n"
      "When debugging cache issues, it can sometimes be useful to disable\n"
      "this self training using nrc::FrameSettings::selfTrainingAttenuation." },
      {NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed, "Primary Vertex Training Radiance Smoothed",
      "The same as PrimaryVertexTrainingRadiance, but smoothed over time.\n"
      "to give a result more like you would see with accumulation" },
      {NrcResolveMode::SecondaryVertexTrainingRadiance, "Secondary Vertex Training Radiance",
      "As PrimaryVertexTrainingRadiance, but for the secondary ray segment." },
      {NrcResolveMode::SecondaryVertexTrainingRadianceSmoothed, "Secondary Vertex Training Radiance Smoothed",
      "The same as SecondaryVertexTrainingRadiance, but smoothed over time.\n" },
      {NrcResolveMode::QueryIndex, "Query Index",
      "shows a random colour that's a hash of the query index.\n"
      "When things are working correctly - this should look like colored noise." },
      {NrcResolveMode::TrainingQueryIndex, "Training Query Index",
      "Same as QueryIndex, but for the training pass's self-training records.\n"
      "When things are working correctly - this should look like colored noise." },
      {NrcResolveMode::DirectCacheView, "Direct Cache View",
      "Direct visualization of the cache (equivalent of querying at vertex zero).\n"
      "The recommended tool to assess correctness of integration, this debug view should\n"
      "capture features such as shadows and view-dependent specular highlights and display\n"
      "them in a low-detail, over-smoothed output." },
  } }
  };


  RemixGui::ComboWithKey<NeuralRadianceCache::QualityPreset> nrcQualityPresetCombo {
    "NRC Quality Preset",
    RemixGui::ComboWithKey<NeuralRadianceCache::QualityPreset>::ComboEntries { {
        {NeuralRadianceCache::QualityPreset::Ultra, "Ultra"},
        {NeuralRadianceCache::QualityPreset::High, "High"},
        {NeuralRadianceCache::QualityPreset::Medium, "Medium"}
    } }
  };

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class NrcResolveShader : public ManagedShader {
      SHADER_SOURCE(NrcResolveShader, VK_SHADER_STAGE_COMPUTE_BIT, nrc_resolve)

      PUSH_CONSTANTS(NrcResolvePushConstants)

      BEGIN_PARAMETER()
        STRUCTURED_BUFFER(NRC_RESOLVE_BINDING_NRC_QUERY_PATH_INFO_INPUT)
        STRUCTURED_BUFFER(NRC_RESOLVE_BINDING_NRC_QUERY_RADIANCE_INPUT)
        STRUCTURED_BUFFER(NRC_RESOLVE_BINDING_NRC_TRAINING_PATH_INFO_INPUT)
        
        TEXTURE2D(NRC_RESOLVE_BINDING_SHARED_FLAGS_INPUT)
        CONSTANT_BUFFER(NRC_RESOLVE_BINDING_RAYTRACE_ARGS_INPUT)

        RW_STRUCTURED_BUFFER(NRC_RESOLVE_BINDING_NRC_DEBUG_TRAINING_PATH_INFO_INPUT_OUTPUT)

        RW_TEXTURE2D(NRC_RESOLVE_BINDING_PRIMARY_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(NRC_RESOLVE_BINDING_PRIMARY_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)
        RW_TEXTURE2D(NRC_RESOLVE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT)

        RW_TEXTURE2D(NRC_RESOLVE_BINDING_DEBUG_VIEW_TEXTURE_OUTPUT)
        RW_STRUCTURED_BUFFER(NRC_RESOLVE_BINDING_GPU_PRINT_BUFFER_OUTPUT)
        END_PARAMETER()
      };

    PREWARM_SHADER_PIPELINE(NrcResolveShader);
  }

  void NeuralRadianceCache::NrcOptions::onMaxNumTrainingIterationsChanged(DxvkDevice* device) {
    targetNumTrainingIterations.setMaxValue(maxNumTrainingIterations());
  }

  namespace {
    bool nrcResolveModeRequiresDebugBuffer(NrcResolveMode resolveMode) {

      switch (resolveMode) {
      case NrcResolveMode::PrimaryVertexTrainingRadiance:
      case NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed:
      case NrcResolveMode::SecondaryVertexTrainingRadiance:
        [[fallthrough]];
      case NrcResolveMode::SecondaryVertexTrainingRadianceSmoothed:
        return true;
      case NrcResolveMode::AddQueryResultToOutput:
      case NrcResolveMode::TrainingBounceHeatMap:
      case NrcResolveMode::TrainingBounceHeatMapSmoothed:
      case NrcResolveMode::QueryIndex:
      case NrcResolveMode::TrainingQueryIndex:
      case NrcResolveMode::DirectCacheView:
        [[fallthrough]];
      case NrcResolveMode::ReplaceOutputWithQueryResult:
        return false;
      }
      return false;
    }

    void onDebugResolveSettingsChanged(DxvkDevice* device) {

      NeuralRadianceCache::NrcOptions::s_nrcDebugBufferIsRequired = NeuralRadianceCache::NrcOptions::enableDebugResolveMode() && nrcResolveModeRequiresDebugBuffer(NeuralRadianceCache::NrcOptions::debugResolveMode());

      if (device == nullptr) {
        return;
      }

      // WAR for the onChanged callbacks getting called even if the resolved value for an option hasn't changed. Without this, the debug view will
      // get set to disabled on config load, eradicating any debug view that was set prior to config load (through environment settings, etc.)
      if (NeuralRadianceCache::NrcOptions::enableDebugResolveMode() != NeuralRadianceCache::NrcOptions::s_nrcPrevDebugResolveIsEnabled) {
        DebugView& debugView = device->getCommon()->metaDebugView();
        if (NeuralRadianceCache::NrcOptions::enableDebugResolveMode()) {
          debugView.setDebugViewIndex(DEBUG_VIEW_NRC_RESOLVE);
        }
        else {
          debugView.setDebugViewIndex(DEBUG_VIEW_DISABLED);
        }
      }
      NeuralRadianceCache::NrcOptions::s_nrcPrevDebugResolveIsEnabled = NeuralRadianceCache::NrcOptions::enableDebugResolveMode();
    }
  }

  void NeuralRadianceCache::NrcOptions::onDebugResolveModeChanged(DxvkDevice* device) {
    onDebugResolveSettingsChanged(device);
  }

  void NeuralRadianceCache::NrcOptions::onEnableDebugResolveModeChanged(DxvkDevice* device) {
    onDebugResolveSettingsChanged(device);
  }


  NeuralRadianceCache::NeuralRadianceCache(dxvk::DxvkDevice* device) : RtxPass(device) {
    m_nrcCtxSettings = std::make_unique<nrc::ContextSettings>();
    m_delayedEnableCustomNetworkConfig = NrcCtxOptions::enableCustomNetworkConfig();
  }

  NeuralRadianceCache::~NeuralRadianceCache() { }

  // Initializes state and resources that can be created once on initialization and do not depend on runtime state
  // Returns true on success
  bool NeuralRadianceCache::initialize(dxvk::DxvkDevice& device) {

    NrcContext::Configuration nrcContextCfg;
    nrcContextCfg.debugBufferIsRequired = NrcOptions::s_nrcDebugBufferIsRequired;
    m_nrcCtx = new NrcContext(device, nrcContextCfg);

    if (m_nrcCtx->initialize() != nrc::Status::OK) {
      return false;
    }

    // Create a buffer to track training records counts
    {
      DxvkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
      bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      bufferInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
      bufferInfo.access = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
      bufferInfo.size = kMaxFramesInFlight * sizeof(uint32_t);
      m_numberOfTrainingRecordsStaging = device.createBuffer(
        bufferInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
        DxvkMemoryStats::Category::RTXBuffer, "NRC training records");

      // Zero init the whole buffer
      uint32_t* gpuMappedUint = reinterpret_cast<uint32_t*>(m_numberOfTrainingRecordsStaging->mapPtr(0));
      for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        gpuMappedUint[i] = 0;
      }
    }

    return true;
  }

  bool NeuralRadianceCache::isResettingHistory() {
    return m_resetHistory;
  }

  void NeuralRadianceCache::showImguiSettings(DxvkContext& ctx) {
    // Ensure the NRC has been initialized since Imgui thread may call this before the initialization occus
    if (!isActive()) {
      return;
    }

    // Display number of training records info
    {
      const ImVec4 kWhite = ImVec4(1.f, 1.f, 1.f, 1.f);
      const ImVec4 kRed = ImVec4(1.f, 0.f, 0.f, 1.f);
      const ImVec4 kYellow = ImVec4(1.f, 1.f, 0.f, 1.f);

      if (m_numberOfTrainingRecords > 0) {

        ImVec4 textColor;

        if (m_numberOfTrainingRecords >= calculateTargetNumTrainingRecords()) {
          const float kTargetMaxTolerance = 1.1f;
          if (m_numberOfTrainingRecords <= kTargetMaxTolerance * calculateTargetNumTrainingRecords()) {
            textColor = kWhite;
          } else {
            textColor = kYellow;
          }
        } else {  // < calculateTargetNumTrainingRecords()
          textColor = kRed;
        }
           
        ImGui::TextColored(textColor, "Number of Training Records: %u", m_numberOfTrainingRecords);
      } else {
        ImGui::TextColored(kRed, "Number of Training Records: Not Available");
      }
    }

    ImGui::Text("Video Memory Usage: %u MiB", m_nrcCtx->getCurrentMemoryConsumption() >> 20);

    nrcQualityPresetCombo.getKey(&NrcOptions::qualityPresetObject());

    RemixGui::Checkbox("Reset History", &NrcOptions::resetHistoryObject());
    RemixGui::Checkbox("Train Cache", &NrcOptions::trainCacheObject());
    RemixGui::Checkbox("Use Custom Network Config \"CustomNetworkConfig.json\"", &m_delayedEnableCustomNetworkConfig);

    if (RemixGui::CollapsingHeader("Training", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();

      RemixGui::Checkbox("Learn Irradiance", &NrcOptions::learnIrradianceObject());
      RemixGui::Checkbox("Include Direct Lighting", &NrcOptions::includeDirectLightingObject());
      
      RemixGui::DragInt("Max Number of Training Iterations", &NrcOptions::maxNumTrainingIterationsObject(), 1.f, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragInt("Target Number of Training Iterations", &NrcOptions::targetNumTrainingIterationsObject(), 1.f, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);

      RemixGui::Checkbox("Adaptive Training Dimensions", &NrcOptions::enableAdaptiveTrainingDimensionsObject());
      RemixGui::DragFloat("Average Number of Vertices Per Path", &NrcOptions::averageTrainingBouncesPerPathObject(), 0.01f, 0.5f, 8.f, "%.1f");
      RemixGui::DragInt("Max Path Bounces", &NrcOptions::trainingMaxPathBouncesObject(), 0.1f, 0, 15, "%d", ImGuiSliderFlags_AlwaysClamp);
      RemixGui::DragInt("Max Path Bounces Bias for Quality Presets", &NrcOptions::trainingMaxPathBouncesBiasInQualityPresetsObject(), 0.1f, -15, 15, "%d", ImGuiSliderFlags_AlwaysClamp);

      RemixGui::DragInt("Jitter Sequence Length", &NrcOptions::jitterSequenceLengthObject());
      RemixGui::Checkbox("Allow Russian Roulette Usage", &NrcOptions::allowRussianRouletteOnUpdateObject());

      ImGui::Unindent();
    }

    RemixGui::Checkbox("Clear Nrc Buffers On Frame Start", &NrcOptions::clearBuffersOnFrameStartObject());

    if (RemixGui::CollapsingHeader("Scene Bounds", ImGuiTreeNodeFlags_DefaultOpen)) {
      RemixGui::DragFloat("Scene Axis Aligned Bounding Box's Width [m]", &NrcOptions::sceneBoundsWidthMetersObject(), 1.f, 0.f, 100000.f, "%f");
      RemixGui::Checkbox("Reset the scene bounds on a camera cut", &NrcOptions::resetSceneBoundsOnCameraCutObject());
      if (ImGui::Button("Reset the scene bounds")) {
        m_initSceneBounds = true;
      }
    }

    if (RemixGui::CollapsingHeader("Resolve")) {
      ImGui::Indent();
      RemixGui::Checkbox("NRC Resolver", &NrcOptions::enableNrcResolverObject());
      RemixGui::Checkbox("Add Path Traced Radiance", &NrcOptions::resolveAddPathTracedRadianceObject());
      RemixGui::Checkbox("Add Nrc Queried Radiance", &NrcOptions::resolveAddNrcQueriedRadianceObject());
      RemixGui::Checkbox("Enable Debug Resolve Mode", &NrcOptions::enableDebugResolveModeObject());

      nrcDebugResolveModeCombo.getKey(&NrcOptions::debugResolveModeObject());

      DebugView& debugView = ctx.getCommonObjects()->metaDebugView();
      if (NrcOptions::enableDebugResolveMode() && debugView.getDebugViewIndex() != DEBUG_VIEW_NRC_RESOLVE) {
        // Disable debug resolve mode when debug view selection changes to another mode
        NrcOptions::enableDebugResolveMode.setImmediately(false);

        // Update previous state too so that it does not trigger any action next frame
        NrcOptions::s_nrcPrevDebugResolveIsEnabled = NrcOptions::enableDebugResolveMode();
      }

      ImGui::Unindent();
    }

    RemixGui::DragFloat("Smallest Resolvable Feature Size [meters]", &NrcOptions::smallestResolvableFeatureSizeMetersObject(), 0.0001f, 0.f, 10.f, "%.4f");
    
    RemixGui::Checkbox("Skip Delta Vertices", &NrcOptions::skipDeltaVerticesObject());

    RemixGui::DragFloat("Termination Heuristic Threshold", &NrcOptions::terminationHeuristicThresholdObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Training Termination Heuristic Threshold", &NrcOptions::trainingTerminationHeuristicThresholdObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Proportion Primary Segments To Train On", &NrcOptions::proportionPrimarySegmentsToTrainOnObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Proportion Tertiary Segments To Train On", &NrcOptions::proportionTertiaryPlusSegmentsToTrainOnObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Proportion Unbiased To Self Train On", &NrcOptions::proportionUnbiasedToSelfTrainObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Proportion Unbiased", &NrcOptions::proportionUnbiasedObject(), 0.001f, 0.f, 1.f, "%.3f");
    RemixGui::DragFloat("Self Training Attenuation", &NrcOptions::selfTrainingAttenuationObject(), 0.001f, 0.f, 1.f, "%.3f");

    RemixGui::Checkbox("Calculate Training Loss", &NrcOptions::enableCalculateTrainingLossObject());
    if (!NrcOptions::enableCalculateTrainingLoss()) {
      ImGui::Text("Training Loss: ", m_trainingLoss);
    }

    RemixGui::DragFloat("Max Expected Average Radiance", &NrcOptions::maxExpectedAverageRadianceValueObject(), 1.f, 0.f, 64 * 1024.f, "%.1f");
    RemixGui::DragFloat("Luminance Clamp Multiplier (0: disabled)", &NrcOptions::luminanceClampMultiplierObject(), 0.1f, 0.f, 10000.f, "%.1f");

    RemixGui::DragInt("Number of Frames To Smooth Training Dimensions (0 ~ Disabled)", &NrcOptions::numFramesToSmoothOutTrainingDimensionsObject(), 1.f, 0, 1024, "%d", ImGuiSliderFlags_AlwaysClamp);

    ImGui::Text("Training Dimension Width Active (Max): %u (%u)", m_activeTrainingDimensions.x, m_nrcCtxSettings->trainingDimensions.x);
    ImGui::Text("Training Dimension Height Active (Max): %u (%u)", m_activeTrainingDimensions.y, m_nrcCtxSettings->trainingDimensions.y);
  }

  static bool checkIsSupported(const DxvkDevice* device) {
    return NrcContext::checkIsSupported(device);
  }

  void NeuralRadianceCache::NrcOptions::onQualityPresetChanged(DxvkDevice* device) {
    // Note: This function is called during onChange handler for quality preset option and 
    // all the NRC calls have been issued, so it's safe to set the new settings using immediately.
    // In addition, this ensures the settings being applied immediately on start, rather than being delayed to the next frame
    
    // onChange handler is called everytime quality preset is set even if it's the same value, so early exit if a same value is set
    if (NrcOptions::qualityPreset() == NrcOptions::s_prevQualityPreset) {
      return;
    }

    NrcOptions::s_prevQualityPreset = NrcOptions::qualityPreset();
    
    if (NrcOptions::qualityPreset() == QualityPreset::Ultra) {
      Logger::info("[RTX Neural Radiance Cache] Selected Ultra preset mode.");
      NrcOptions::terminationHeuristicThreshold.setImmediately(0.1f);
      NrcOptions::smallestResolvableFeatureSizeMeters.setImmediately(0.01f);
      NrcOptions::targetNumTrainingIterations.setImmediately(4);
      // 9 and higher resulted in no scene illumination loss in Portal RTX
      trainingMaxPathBounces.setImmediately(9);

    } else if (NrcOptions::qualityPreset() == QualityPreset::High) {
      Logger::info("[RTX Neural Radiance Cache] Selected High preset mode.");
      NrcOptions::terminationHeuristicThreshold.setImmediately(0.03f);
      NrcOptions::smallestResolvableFeatureSizeMeters.setImmediately(0.04f);
      NrcOptions::targetNumTrainingIterations.setImmediately(3);
      // 7 results in tiny scene illumination decrease in comparison to 9
      trainingMaxPathBounces.setImmediately(7);

    } else if (NrcOptions::qualityPreset() == QualityPreset::Medium) {
      Logger::info("[RTX Neural Radiance Cache] Selected Medium preset mode.");
      NrcOptions::terminationHeuristicThreshold.setImmediately(0.001f);

      // Using a higher cache resolution to speed up NRC's Query and Train pass at a cost of some IQ fidelity. 
      // 0.01 -> 0.06 resolution results in in 0.2ms cost reduction
      // Values above 6cm start to produce considerably more pronounced IQ differences in specular reflections in Portal.
      NrcOptions::smallestResolvableFeatureSizeMeters.setImmediately(0.06f);

      // Using only 2 iterations vs default 4 can result in reduced responsiveness, but it saves 0.4ms from NRC and PT passes
      NrcOptions::targetNumTrainingIterations.setImmediately(2);

      // Longer training paths require more memory (~5-8+ MB per bounce) and have a slight performance impact (particularly when SER is disabled).
      trainingMaxPathBounces.setImmediately(6);
    }
  }

  uint32_t NeuralRadianceCache::calculateTargetNumTrainingRecords() const {
    return NrcOptions::targetNumTrainingIterations() * kNumTrainingRecordsPerIteration;
  }

  void NeuralRadianceCache::setRaytraceArgs(RaytraceArgs& constants) {
    if (!isActive()) {
      return;
    }

    NrcArgs& nrcArgs = constants.nrcArgs;
    m_nrcCtx->populateShaderConstants(nrcArgs.nrcConstants);

    nrcArgs.updatePathMaxBounces = calculateTrainingMaxPathBounces();

    // Russian roulette is disabled due to bias in NRC SDK when it is enabled
    nrcArgs.updateAllowRussianRoulette = false;

    const uint numUpdatePixels = m_activeTrainingDimensions.x * m_activeTrainingDimensions.y;
    nrcArgs.numRowsForUpdate = divCeil(numUpdatePixels, m_nrcCtxSettings->frameDimensions.x);

    // Note: last training path may have query space pixel coordinates outside of valid query resolution bounds.
    // Such paths will be skipped.
    m_numQueryPixelsPerTrainingPixel = Vector2 {
      m_nrcCtxSettings->frameDimensions.x / static_cast<float>(m_activeTrainingDimensions.x),
      m_nrcCtxSettings->frameDimensions.y / static_cast<float>(m_activeTrainingDimensions.y) };

    nrcArgs.activeTrainingDimensions = vec2 {
      static_cast<float>(m_activeTrainingDimensions.x),
      static_cast<float>(m_activeTrainingDimensions.y) };

    nrcArgs.rcpActiveTrainingDimensions = vec2 {
      1.f / m_activeTrainingDimensions.x,
      1.f / m_activeTrainingDimensions.y };

    nrcArgs.queryToTrainingCoordinateSpace = vec2 {
      1.f / m_numQueryPixelsPerTrainingPixel.x,
      1.f / m_numQueryPixelsPerTrainingPixel.y };

    nrcArgs.trainingToQueryCoordinateSpace = vec2 {
      m_numQueryPixelsPerTrainingPixel.x,
      m_numQueryPixelsPerTrainingPixel.y };

    nrcArgs.sceneBoundsMin = m_sceneBoundsMin;
    nrcArgs.sceneBoundsMax = m_sceneBoundsMax;

    // Calculate half query pixel's offset in update pixel space
    const double epsilon = 0.001;  // A slight bump to bounds to guard more against boundary trailing aliasing
    const double halfPixel = 0.5 + epsilon;
    Vector2Base<double> trainingPixelInnerBounds = Vector2Base<double> {
      (halfPixel / m_nrcCtxSettings->frameDimensions.x) * m_activeTrainingDimensions.x,
      (halfPixel / m_nrcCtxSettings->frameDimensions.y) * m_activeTrainingDimensions.y };

    if (NrcOptions::jitterSequenceLength()) {
      uvec2 numQueryPixelsPerTrainingPixel = uvec2 {
        static_cast<uint>(ceil(m_numQueryPixelsPerTrainingPixel.x)),
        static_cast<uint>(ceil(m_numQueryPixelsPerTrainingPixel.y)) };

      const uint32_t currentFrameIndex = m_nrcCtx->device()->getCurrentFrameId();
      const Vector2 jitter05 = calculateHaltonJitter(currentFrameIndex, NrcOptions::jitterSequenceLength());
      const Vector2 rand01 = Vector2 { jitter05.x + 0.5f, jitter05.y + 0.5f };

      // Constrain jitter to prevent aliasing when going from query to training and back to query space.
      // This ensures that the starting and ending query coordinate doesn't end up in a different training pixel.
      // Otherwise, some training pixels will get skipped as they don't have matching starting and ending query points.
      // Note: the range is multiplied by 2 since we need to contract bounds on both sides and jitter offset is offseted by trainingPixelInnerBounds
      const Vector2Base<double> rngRange = Vector2Base<double> {
        1 - 2 * trainingPixelInnerBounds.x,
        1 - 2 * trainingPixelInnerBounds.y };

      nrcArgs.updatePixelJitter = vec2 {
          static_cast<float>(trainingPixelInnerBounds.x + rngRange.x * rand01.x),
          static_cast<float>(trainingPixelInnerBounds.y + rngRange.y * rand01.y) };
    } else {
      nrcArgs.updatePixelJitter = vec2 { 
        static_cast<float>(trainingPixelInnerBounds.x),
        static_cast<float>(trainingPixelInnerBounds.y)};
    }

    nrcArgs.trainingLuminanceClamp = NrcOptions::luminanceClampMultiplier() * NrcOptions::maxExpectedAverageRadianceValue();
  }

  const Vector2& NeuralRadianceCache::getNumQueryPixelsPerTrainingPixel() const {
    return m_numQueryPixelsPerTrainingPixel;
  }

  bool NeuralRadianceCache::isUpdateResolveModeActive() const {
    if (NrcOptions::enableDebugResolveMode()) {
      switch (NrcOptions::debugResolveMode()) {
        case NrcResolveMode::TrainingBounceHeatMap:
        case NrcResolveMode::TrainingBounceHeatMapSmoothed:
        case NrcResolveMode::PrimaryVertexTrainingRadiance:
        case NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed:
        case NrcResolveMode::TrainingQueryIndex:
          return true;
        default:
          break;
      }
    }

    return false;
  }

  DxvkBufferSlice NeuralRadianceCache::getBufferSlice(
    RtxContext& ctx,
    ResourceType resourceType) {
    if (!isActive()) {
      return DxvkBufferSlice();
    }
    switch (resourceType) {
    case ResourceType::QueryPathInfo:
      return m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::QueryPathInfo);
    case ResourceType::TrainingPathInfo:
      return m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::TrainingPathInfo);
    case ResourceType::TrainingPathVertices:
      return m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::TrainingPathVertices);
    case ResourceType::QueryRadianceParams:
      return m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::QueryRadianceParams);
    case ResourceType::Counters:
      return m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::Counter);
    default:
      assert(!"Invalid argument");
      return DxvkBufferSlice();
    }
  }

  VkExtent3D NeuralRadianceCache::calcRaytracingResolution() const {
    assert(isActive() && "This requires NRC to be enabled and onFrameStart() to have been called prior.");
    
    // NRC Query and Update pixels are executed in a single dispatch for performance.
    // Calculate raytracing resolution to cover both.
    // Update pixels are executed first / start at row 0 since they have longer path tails due to
    // them not using Russian Roulette. This along with using NRC update/query SER coherence hint makes it faster.

    const uint numUpdatePixels = m_activeTrainingDimensions.x * m_activeTrainingDimensions.y;
    const uint numRowsForUpdate = divCeil(numUpdatePixels, m_nrcCtxSettings->frameDimensions.x);

    return VkExtent3D {
      m_nrcCtxSettings->frameDimensions.x,
      m_nrcCtxSettings->frameDimensions.y + numRowsForUpdate,
      1 };
  }

  bool NeuralRadianceCache::checkIsSupported(const dxvk::DxvkDevice* device) {
    return NrcContext::checkIsSupported(device);
  }

  void NeuralRadianceCache::copyNumberOfTrainingRecords(RtxContext& ctx) {
    const VkDeviceSize elementSize = sizeof(uint32_t);
    const uint32_t frameIdx = m_nrcCtx->device()->getCurrentFrameId();
    const uint32_t entryIdx = frameIdx % kMaxFramesInFlight;
    const VkDeviceSize dstOffset = entryIdx * elementSize;
    const VkDeviceSize srcOffset = static_cast<uint32_t>(NrcCounter::TrainingRecords) * elementSize;

    ctx.copyBuffer(
      m_numberOfTrainingRecordsStaging, dstOffset,
      m_nrcCtx->getBuffer(nrc::BufferIdx::Counter), srcOffset, elementSize);
  }

  bool NeuralRadianceCache::isEnabled() const {
    return RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache;
  }

  void NeuralRadianceCache::onFrameBegin(
    Rc<DxvkContext>& ctx,
    const FrameBeginContext& frameBeginCtx) {

    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    if (!isActive()) {
      return;
    }

    const bool reinitializeNrcContext =
      m_nrcCtx->isDebugBufferRequired() != NrcOptions::s_nrcDebugBufferIsRequired
      || m_delayedEnableCustomNetworkConfig != NrcCtxOptions::enableCustomNetworkConfig()
      // [REMIX-3810] WAR to fully recreate NRC when resolution changes to avoid occasional corruption
      // when changing resolutions
      || frameBeginCtx.downscaledExtent.width != m_nrcCtxSettings->frameDimensions.x
      || frameBeginCtx.downscaledExtent.height != m_nrcCtxSettings->frameDimensions.y;

    if (reinitializeNrcContext) {

      NrcCtxOptions::enableCustomNetworkConfig.setDeferred(m_delayedEnableCustomNetworkConfig);

      NrcContext::Configuration nrcContextCfg;
      nrcContextCfg.debugBufferIsRequired = NrcOptions::s_nrcDebugBufferIsRequired;
      m_nrcCtx = new NrcContext(*ctx->getDevice(), nrcContextCfg);

      if (m_nrcCtx->initialize() != nrc::Status::OK) {
        Logger::err(str::format("[RTX Neural Radiance Cache] Failed to initialize NRC context"));
        return;
      }
    }

    const VkExtent3D& downscaledExtent = ctx->getCommonObjects()->getResources().getDownscaleDimensions();

    bool hasNrcSetupSucceeded = true;
   
    // Check supported limits
    if (frameBeginCtx.downscaledExtent.width > NRC_MAX_RAYTRACING_RESOLUTION_X ||
        frameBeginCtx.downscaledExtent.height > NRC_MAX_RAYTRACING_RESOLUTION_Y) {
      // NRC can't handle this
      // Note: the resolution limits are very large to accomodate practical gaming resolutions 
      // so this should almost never happen. But the limits can be potentially bumped, if necessary in the future 

      ONCE(Logger::err(str::format(
        "[RTX Neural Radiance Cache] Unsupported ray tracing resolution. The resolution is too large. Disabling NRC!\n"
        "  Max supported resolution: ", NRC_MAX_RAYTRACING_RESOLUTION_X, " x ", NRC_MAX_RAYTRACING_RESOLUTION_Y, "\n"
        "  Requested resolution: ", frameBeginCtx.downscaledExtent.width, " x ", frameBeginCtx.downscaledExtent.height)));

      // Fallback to Basic mode if NRC setup failed.
      // Note: it would be preferable to fallback to ReSTIRGI, but that would require delaying that change to the beginning of the next frame
      // to ensure consistent mode state in the frame. That is something to consider in the future. For now this will do for the sake of simpler logic
      Logger::warn(str::format("[RTX Neural Radiance Cache] Neural Radiance Cache per frame setup failed. Switching to importance sampled indirect illumination mode."));
      RtxOptions::integrateIndirectMode.setDeferred(IntegrateIndirectMode::ImportanceSampled);
      
      return;
    }

    m_resetHistory = m_resetHistory || NrcOptions::resetHistory() || frameBeginCtx.resetHistory;

    // Set up NRC context settings
    {
      m_nrcCtxSettings->learnIrradiance = NrcOptions::learnIrradiance();
      m_nrcCtxSettings->includeDirectLighting = NrcOptions::includeDirectLighting();
      m_nrcCtxSettings->requestReset = m_resetHistory;

      // Calculate NRC resolution limits
      {
        m_nrcCtxSettings->frameDimensions = nrc_uint2 {
          frameBeginCtx.downscaledExtent.width,
          frameBeginCtx.downscaledExtent.height
        };

        // Calculate an upper bound for training dimensions where we have N path vertices per pixel on average
        const nrc_uint2 prevTrainingDimensions = m_nrcCtxSettings->trainingDimensions;
        m_nrcCtxSettings->trainingDimensions = nrc::ComputeIdealTrainingDimensions(m_nrcCtxSettings->frameDimensions, NrcOptions::targetNumTrainingIterations(), NrcOptions::averageTrainingBouncesPerPath());

        // Constrain the dimensions to the RT output resolution because training resolution cannot be larger 
        // due to primary rays being aliased for both query and training
        if (m_nrcCtxSettings->trainingDimensions.x > m_nrcCtxSettings->frameDimensions.x ||
            m_nrcCtxSettings->trainingDimensions.y > m_nrcCtxSettings->frameDimensions.y) {
          ONCE(Logger::warn(str::format("[RTX Neural Radiance Cache] Requested NRC training resolution was clamped by active pathtracing resolution. NRC may update slower because of that.\n",
                                        "Requested: (", m_nrcCtxSettings->trainingDimensions.x, ", ", m_nrcCtxSettings->trainingDimensions.y,")\n",
                                        "Clamped: (", m_nrcCtxSettings->frameDimensions.x, ", ", m_nrcCtxSettings->frameDimensions.y, ")")));
          m_nrcCtxSettings->trainingDimensions = nrc_uint2 {
            std::min(m_nrcCtxSettings->trainingDimensions.x, m_nrcCtxSettings->frameDimensions.x),
            std::min(m_nrcCtxSettings->trainingDimensions.y, m_nrcCtxSettings->frameDimensions.y),
          };
        }

        // Integrator expects the width of training dimensions not to be larger than that of target resolution. 
        // In practice, this should always be the case unless in case of contrived tiny frame dimensions.
        // Therefore we clamp it to ensure the constraint
        m_nrcCtxSettings->trainingDimensions.x = std::min(m_nrcCtxSettings->trainingDimensions.x, m_nrcCtxSettings->frameDimensions.x);

        const bool haveMaxTrainingDimensionsChanged = memcmp(&m_nrcCtxSettings->trainingDimensions, &prevTrainingDimensions, sizeof(prevTrainingDimensions)) != 0;
      
        calculateActiveTrainingDimensions(frameBeginCtx.frameTimeMilliseconds, haveMaxTrainingDimensionsChanged);
      }

      m_nrcCtxSettings->maxPathVertices = NrcOptions::trainingMaxPathBounces();
      m_nrcCtxSettings->samplesPerPixel = 1;
      assert(m_nrcCtxSettings->samplesPerPixel <= NRC_MAX_SAMPLES_PER_PIXEL);
      m_nrcCtxSettings->smallestResolvableFeatureSize = NrcOptions::smallestResolvableFeatureSizeMeters() *  RtxOptions::getMeterToWorldUnitScale();

      // Set scene bounds

      if (frameBeginCtx.isCameraCut && NrcOptions::resetSceneBoundsOnCameraCut()) {
        m_initSceneBounds = true;
      }

      // Note: this is set around initial camera for now, REMIX-3186 will generalize this 
      if (m_initSceneBounds) {
        const Vector3 cameraPos = ctx->getCommonObjects()->getSceneManager().getCamera().getPosition();
        AxisAlignedBoundingBox sceneAabb;

        // Note, the maximum span is doubled, i.e. added around the camera 
        // as the bounding box is formed around the original camera position
        // rather than from the minimum position of the actual AABB of the world,
        // because we don't currently have that position.
        const Vector3 halfRelativeBBOX = Vector3{
          NrcOptions::sceneBoundsWidthMeters(),
          NrcOptions::sceneBoundsWidthMeters(),
          NrcOptions::sceneBoundsWidthMeters() }
          * RtxOptions::getMeterToWorldUnitScale();

        sceneAabb.minPos = cameraPos - halfRelativeBBOX;
        sceneAabb.maxPos = cameraPos + halfRelativeBBOX;

        m_sceneBoundsMin = Vector3{ sceneAabb.minPos.x, sceneAabb.minPos.y, sceneAabb.minPos.z };
        m_sceneBoundsMax = Vector3{ sceneAabb.maxPos.x, sceneAabb.maxPos.y, sceneAabb.maxPos.z };

        m_nrcCtxSettings->sceneBoundsMin = nrc_float3{ sceneAabb.minPos.x, sceneAabb.minPos.y, sceneAabb.minPos.z };
        m_nrcCtxSettings->sceneBoundsMax = nrc_float3{ sceneAabb.maxPos.x, sceneAabb.maxPos.y, sceneAabb.maxPos.z };

        m_initSceneBounds = false;
      }
    }

    // Settings expected to change frequently that do not require instance reset
    nrc::FrameSettings nrcFrameSettings;
    {
      nrcFrameSettings.maxExpectedAverageRadianceValue = NrcOptions::maxExpectedAverageRadianceValue();
      
      nrcFrameSettings.skipDeltaVertices = NrcOptions::skipDeltaVertices();
      nrcFrameSettings.terminationHeuristicThreshold = NrcOptions::terminationHeuristicThreshold();
      nrcFrameSettings.trainingTerminationHeuristicThreshold = NrcOptions::trainingTerminationHeuristicThreshold();
      nrcFrameSettings.resolveMode = NrcOptions::enableDebugResolveMode() ? NrcOptions::debugResolveMode() : NrcResolveMode::AddQueryResultToOutput;
      nrcFrameSettings.trainTheCache = NrcOptions::trainCache();

      nrcFrameSettings.usedTrainingDimensions = m_activeTrainingDimensions;

      nrcFrameSettings.proportionPrimarySegmentsToTrainOn = NrcOptions::proportionPrimarySegmentsToTrainOn();
      nrcFrameSettings.proportionTertiaryPlusSegmentsToTrainOn = NrcOptions::proportionTertiaryPlusSegmentsToTrainOn();
      nrcFrameSettings.proportionUnbiasedToSelfTrain = NrcOptions::proportionUnbiasedToSelfTrain();
      nrcFrameSettings.proportionUnbiased = NrcOptions::proportionUnbiased();
      nrcFrameSettings.selfTrainingAttenuation = NrcOptions::selfTrainingAttenuation();

      nrcFrameSettings.numTrainingIterations = calculateNumTrainingIterations();
    }

    // Allocate resources dependent on runtime settings
    {
      // Allocate query path data only when include direct lighting option is disabled. 
      // In this case queryPathData resolved in gbuffer is needed in indirect pass (i.e. direct lighting is resolved).
      // Note: this is done here since indirect lighting option can change after createDownscaledResource() was called
      if (!NrcOptions::includeDirectLighting() && m_queryPathData0.image == nullptr) {
        m_queryPathData0 = Resources::createImageResource(ctx, "NRC Query Path Data 0", downscaledExtent, VK_FORMAT_R32G32_UINT);
      } else if (NrcOptions::includeDirectLighting() && m_queryPathData0.image != nullptr) {
        m_queryPathData0.reset();
      }

      // Allocate resources if they are invalid or have stale dimensions
      if (m_trainingGBufferSurfaceRadianceRG.image == nullptr 
          || m_trainingGBufferSurfaceRadianceRG.image->info().extent.width != m_nrcCtxSettings->trainingDimensions.x 
          || m_trainingGBufferSurfaceRadianceRG.image->info().extent.height != m_nrcCtxSettings->trainingDimensions.y) {

        VkExtent3D newImageExtent = VkExtent3D { m_nrcCtxSettings->trainingDimensions.x, m_nrcCtxSettings->trainingDimensions.y, 1 };
        m_trainingGBufferSurfaceRadianceRG = Resources::createImageResource(ctx, "NRC Training shared radiance RG", newImageExtent, VK_FORMAT_R16G16_SFLOAT);
        m_trainingGBufferSurfaceRadianceB = Resources::createImageResource(ctx, "NRC Training shared radiance B", newImageExtent, VK_FORMAT_R16_SFLOAT);
      }
    }
    
    bool hasCacheBeenReset;
    m_nrcCtx->onFrameBegin(*ctx, *m_nrcCtxSettings, nrcFrameSettings, &hasCacheBeenReset);

    // Propagate the cache reset, since the runtime queries this after the onFrameBegin calls
    if (hasCacheBeenReset) {
      m_resetHistory = hasCacheBeenReset;
    }

    if (NrcOptions::clearBuffersOnFrameStart()) {
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::QueryPathInfo, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::TrainingPathInfo, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::TrainingPathVertices, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::TrainingRadiance, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::TrainingRadianceParams, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::QueryRadiance, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::QueryRadianceParams, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT);
      // onFrameBegin() above already clears the counter resource
      if (m_nrcCtx->isDebugBufferRequired()) {
        m_nrcCtx->clearBuffer(*ctx, nrc::BufferIdx::DebugTrainingPathInfo, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
      }
    }
  }

  bool NeuralRadianceCache::onActivation(Rc<DxvkContext>& ctx) {

    // Fallback to Importance Sampled mode if NRC setup failed.
    // Note: it would be preferable to fallback to ReSTIRGI, but that would require delaying that change to the beginning of the next frame
    // to ensure consistent mode state in the frame. That is something to consider in the future. For now this will do for sake of simpler logic

    if (!checkIsSupported(ctx->getDevice().ptr())) {
      ONCE(Logger::warn("[RTX Neural Radiance Cache] Neural Radiance Cache is not supported. Switching to importance sampled indirect illumination mode."));
      RtxOptions::integrateIndirectMode.setDeferred(IntegrateIndirectMode::ImportanceSampled);
      return false;
    }

    if (!initialize(*ctx->getDevice())) {
      Logger::err("[RTX Neural Radiance Cache] Neural Radiance Cache failed to get initialized. Switching to importance sampled indirect illumination mode.");
      RtxOptions::integrateIndirectMode.setDeferred(IntegrateIndirectMode::ImportanceSampled);
      return false;
    }

    m_initSceneBounds = true;

    return true;
  }

  void NeuralRadianceCache::onDeactivation() {
    m_nrcCtx = nullptr;
    m_numberOfTrainingRecordsStaging = nullptr;
  }

  void NeuralRadianceCache::createDownscaledResource(
    Rc<DxvkContext>& ctx,
    const VkExtent3D& downscaledExtent) {

    Resources::RaytracingOutput& rtOutput = ctx->getCommonObjects()->getResources().getRaytracingOutput();

    m_queryPathData1 = Resources::AliasedResource(rtOutput.m_compositeOutput, ctx, downscaledExtent, VK_FORMAT_R16G16B16A16_UINT, "NRC Query Path Data 1");

    // Explicit constant to make it clear where cross format aliasing occurs
    const bool allowCompatibleFormatAliasing = true;

    // Note: technically we only need m_nrcCtxSettings->trainingDimensions, which is often smaller than the m_finalOutputExtent, but the resource is available to alias with so might as well
    m_trainingPathData1 = Resources::AliasedResource(rtOutput.m_finalOutput, ctx, rtOutput.m_finalOutputExtent, VK_FORMAT_R16G16B16A16_UINT, "NRC Training Path Data 1", allowCompatibleFormatAliasing);
  }

  void NeuralRadianceCache::releaseDownscaledResource() {
    m_trainingGBufferSurfaceRadianceRG.reset();
    m_trainingGBufferSurfaceRadianceB.reset();

    m_queryPathData1.reset();
    m_trainingPathData1.reset();
  }

  void NeuralRadianceCache::bindGBufferPathTracingResources(RtxContext& ctx) {
    ctx.bindResourceBuffer(GBUFFER_BINDING_NRC_QUERY_PATH_INFO_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::QueryPathInfo));
    ctx.bindResourceBuffer(GBUFFER_BINDING_NRC_TRAINING_PATH_INFO_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::TrainingPathInfo));
    ctx.bindResourceBuffer(GBUFFER_BINDING_NRC_TRAINING_PATH_VERTICES_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::TrainingPathVertices));
    ctx.bindResourceBuffer(GBUFFER_BINDING_NRC_QUERY_RADIANCE_PARAMS_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::QueryRadianceParams));
    ctx.bindResourceBuffer(GBUFFER_BINDING_NRC_COUNTERS_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::Counters));

    ctx.bindResourceView(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_RG_OUTPUT, m_trainingGBufferSurfaceRadianceRG.view, nullptr);
    ctx.bindResourceView(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_B_OUTPUT, m_trainingGBufferSurfaceRadianceB.view, nullptr);
    ctx.bindResourceView(GBUFFER_BINDING_NRC_QUERY_PATH_DATA0_OUTPUT, m_queryPathData0.view, nullptr);

    // Aliased resource methods must not be called when the resource is invalid
    if (isActive()) {
      ctx.bindResourceView(GBUFFER_BINDING_NRC_QUERY_PATH_DATA1_OUTPUT, m_queryPathData1.view(Resources::AccessType::Write), nullptr);
      ctx.bindResourceView(GBUFFER_BINDING_NRC_TRAINING_PATH_DATA1_OUTPUT, m_trainingPathData1.view(Resources::AccessType::Write), nullptr);
    } else {
      ctx.bindResourceView(GBUFFER_BINDING_NRC_QUERY_PATH_DATA1_OUTPUT, nullptr, nullptr);
      ctx.bindResourceView(GBUFFER_BINDING_NRC_TRAINING_PATH_DATA1_OUTPUT, nullptr, nullptr);
    }
  }

  void NeuralRadianceCache::bindIntegrateIndirectPathTracingResources(RtxContext& ctx) {
    ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NRC_QUERY_PATH_INFO_INPUT_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::QueryPathInfo));
    ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_PATH_INFO_INPUT_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::TrainingPathInfo));
    ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_PATH_VERTICES_INPUT_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::TrainingPathVertices));
    ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NRC_QUERY_RADIANCE_PARAMS_INPUT_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::QueryRadianceParams));
    ctx.bindResourceBuffer(INTEGRATE_INDIRECT_BINDING_NRC_COUNTERS_INPUT_OUTPUT, getBufferSlice(ctx, NeuralRadianceCache::ResourceType::Counters));

    ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_RG_INPUT, m_trainingGBufferSurfaceRadianceRG.view, nullptr);
    ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_B_INPUT, m_trainingGBufferSurfaceRadianceB.view, nullptr);
    ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_PATH_DATA0_INPUT, m_queryPathData0.view, nullptr);

    // Aliased resource methods must not be called when the resource is invalid
    if (isActive()) {
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_PATH_DATA1_INPUT, m_queryPathData1.view(Resources::AccessType::Read), nullptr);
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_UPDATE_PATH_DATA1_INPUT, m_trainingPathData1.view(Resources::AccessType::Read), nullptr);
    } else {
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_PATH_DATA1_INPUT, nullptr, nullptr);
      ctx.bindResourceView(INTEGRATE_INDIRECT_BINDING_NRC_UPDATE_PATH_DATA1_INPUT, nullptr, nullptr);

    }
  }

  void NeuralRadianceCache::readAndResetNumberOfTrainingRecords() {
    const uint32_t frameIdx = m_nrcCtx->device()->getCurrentFrameId();

    // Read from the oldest element as it is guaranteed to be written out to by the GPU by now
    VkDeviceSize offset = (frameIdx % kMaxFramesInFlight) * sizeof(uint32_t);
    uint32_t* gpuMappedUint = reinterpret_cast<uint32_t*>(m_numberOfTrainingRecordsStaging->mapPtr(offset));

    m_numberOfTrainingRecords = *gpuMappedUint;

    *gpuMappedUint = 0;
  }

  void NeuralRadianceCache::calculateActiveTrainingDimensions(
    float frameTimeMilliseconds, 
    bool forceReset) {

    readAndResetNumberOfTrainingRecords();

    const uint32_t frameIdx = m_nrcCtx->device()->getCurrentFrameId();

    forceReset |= m_resetHistory;
    forceReset |= m_numberOfTrainingRecords == 0;
    forceReset |= NrcOptions::numFramesToSmoothOutTrainingDimensions() <= 1;
    // We skipped frame(s), reset
    forceReset |= (frameIdx - m_smoothingResetFrameIdx + 1) > (NrcOptions::numFramesToSmoothOutTrainingDimensions() + kMaxFramesInFlight);

    if (!NrcOptions::enableAdaptiveTrainingDimensions() || forceReset) {
      // Max training dimensions will generally produce more training records than needed,
      // but it will cover scenarios with less bounces on average as well as having higher number records than what's needed for targetNumTrainingIterations.
      // will boost NRC convergence.
      // The active training dimensions will converge on a target targetNumTrainingIterations in NrcOptions::numFramesToSmoothOutTrainingDimensions() + kMaxFramesInFlight frames.
      m_activeTrainingDimensions = nrc_uint2 {
        static_cast<uint32_t>(m_nrcCtxSettings->trainingDimensions.x),
        static_cast<uint32_t>(m_nrcCtxSettings->trainingDimensions.y) };

      m_smoothingResetFrameIdx = frameIdx;
      m_smoothedNumberOfTrainingRecords = 0;

      // Start smoothing once kMaxFramesInFlight frames passed since the reset
    } else if (frameIdx - m_smoothingResetFrameIdx >= kMaxFramesInFlight) {

      // numSmoothedFrames calculated as inclusive of current frame
      const uint32_t numSmoothedFrames = frameIdx - m_smoothingResetFrameIdx - kMaxFramesInFlight + 1;

      // Calculate smoothed number of training record statistic
      m_smoothedNumberOfTrainingRecords =
        lerp<float>(m_smoothedNumberOfTrainingRecords, m_numberOfTrainingRecords, 1.f / numSmoothedFrames);

      assert(numSmoothedFrames <= NrcOptions::numFramesToSmoothOutTrainingDimensions());

      // Dynamically adjust training dimensions every N frames using smoothed statistics over the last N frames 
      if (numSmoothedFrames == NrcOptions::numFramesToSmoothOutTrainingDimensions()) {
        // Adjust previous training dimension value to get closer to the target number of training records.
        // Note: m_numberOfTrainingRecords was issued at a frame corresponding to a frame where m_activeTrainingDimensions was calculated
        float prevWorkloadScale = static_cast<float>(m_smoothedNumberOfTrainingRecords) / calculateTargetNumTrainingRecords();

        // Number of training records doesn't fully linearly scale with the workload scale,
        // so we speed it up if it's below the target and slow it down if it's over since the goal is to generate
        // at least the target number of training records, but preferably very close to it for performance reasons.
        // The goal here is to minimize underestimating needed training dimensions
        if (prevWorkloadScale < 1.f) {
          prevWorkloadScale *= 0.9f;  // Increase the distance from the 1.f target to speed up the adjustment
        } else {
          // Bring the reference workload scale closer to the target to slow down the training dimensions adjustment to minimize underestimating it
          prevWorkloadScale = std::max(1.f, prevWorkloadScale * 0.98f);
        }

        // Adjust the training dimensions to get closer to the target number of records
        const float rcpPerDimensionWorkloadScale = 1.f / sqrtf(prevWorkloadScale);

        nrc_uint2 newActiveTrainingDimensions = nrc_uint2 {
          std::min(static_cast<uint32_t>(ceil(rcpPerDimensionWorkloadScale * m_activeTrainingDimensions.x)),
                    m_nrcCtxSettings->trainingDimensions.x),
          std::min(static_cast<uint32_t>(ceil(rcpPerDimensionWorkloadScale * m_activeTrainingDimensions.y)),
                    m_nrcCtxSettings->trainingDimensions.y),
        };

        // Active training dimensions changed
        if (0 != memcmp(&newActiveTrainingDimensions, &m_activeTrainingDimensions, sizeof(newActiveTrainingDimensions))) {
          m_activeTrainingDimensions = newActiveTrainingDimensions;

          // We need to reset the counter due to the delay of m_numberOfTrainingRecords being retrieved from the GPUettings->trainingDimensions;
          m_smoothingResetFrameIdx = frameIdx;
        } else {
          // Keep the smoothing window length the same => offset the start frameIdx
          m_smoothingResetFrameIdx++;
        }
      }
    }
  }

  uint32_t NeuralRadianceCache::calculateNumTrainingIterations() {
    // Pathtracer will generally generate more training records
    // until it gets calibrated, since we don't have actual count until m_smoothedNumberOfTrainingRecords is calculated
    // assume plenty have been generated which is usually the case and it allows to to speed up the training.
    // The SDK will pad training iterations with records should the pathtracer not generate enough
    if (m_smoothedNumberOfTrainingRecords == 0) {
      return NrcOptions::maxNumTrainingIterations();
    }

    // Taking a ceiling value of number of iterations since NRC will pad the missing training records in a training iteration.
    // But avoid issuing training iterations with not enough actual training records to save on performance cost,
    // so subtract the set minimum from the actual number of training records since we're taking a ceiling value 
    // when calculating number of training iterations
    const uint32_t minNumTrainingRecordsForAnIteration = static_cast<uint32_t>(0.5f * kNumTrainingRecordsPerIteration);
    const uint32_t adjustedNumberOfTrainingRecords =
      std::max(minNumTrainingRecordsForAnIteration, static_cast<uint32_t>(m_smoothedNumberOfTrainingRecords)) - minNumTrainingRecordsForAnIteration;
    const uint32_t numTrainingIterations = divCeil(adjustedNumberOfTrainingRecords, kNumTrainingRecordsPerIteration);

    return std::min(numTrainingIterations, NrcOptions::maxNumTrainingIterations());
  }

  uint8_t NeuralRadianceCache::calculateTrainingMaxPathBounces() const {
    return static_cast<uint8_t>(
      std::clamp(NrcOptions::trainingMaxPathBounces() + NrcOptions::trainingMaxPathBouncesBiasInQualityPresets(),
                 1, 15));
  }

  void NeuralRadianceCache::setQualityPreset(QualityPreset nrcQualityPreset) {
    NrcOptions::qualityPreset.setDeferred(nrcQualityPreset);
  }

  // Resolves radiance for the queried paths during path tracing
  void NeuralRadianceCache::dispatchResolve(
    RtxContext& ctx,
    const Resources::RaytracingOutput& rtOutput) {

    ScopedGpuProfileZone(&ctx, "NRC: Resolve");

    DebugView& debugView = ctx.getCommonObjects()->metaDebugView();

    // Run a debug resolve mode when enabled
    if (NrcOptions::enableDebugResolveMode()) {

      // Run NRC's resolve
      if (debugView.getDebugOutput() != nullptr) {
        m_nrcCtx->resolve(ctx, debugView.getDebugOutput());
      }
    }

    // Add pre-resolve barriers
    {
      // Setup stage and access masks
      VkPipelineStageFlagBits srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      VkAccessFlags srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

      // Create barrier batch infos
      std::vector<VkBufferMemoryBarrier> barriers;
      barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::QueryPathInfo, srcAccessMask, VK_ACCESS_SHADER_READ_BIT));
      barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::QueryRadiance, srcAccessMask, VK_ACCESS_SHADER_READ_BIT));
      barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::TrainingPathInfo, srcAccessMask, VK_ACCESS_SHADER_READ_BIT));
      if (m_nrcCtx->isDebugBufferRequired()) {
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::DebugTrainingPathInfo, srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT));
      }

      // Create the barrier batch
      vkCmdPipelineBarrier(ctx.getCmdBuffer(DxvkCmdBuffer::ExecBuffer), srcStageMask, dstStageMask, 0, 0, NULL, barriers.size(), barriers.data(), 0, NULL);
    }

    // Bind resources
    {
      Rc<DxvkBuffer> raytraceArgsBuffer = ctx.getResourceManager().getConstantsBuffer();

      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_NRC_QUERY_PATH_INFO_INPUT, m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::QueryPathInfo));
      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_NRC_QUERY_RADIANCE_INPUT, m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::QueryRadiance));
      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_NRC_TRAINING_PATH_INFO_INPUT, m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::TrainingPathInfo));
      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_NRC_DEBUG_TRAINING_PATH_INFO_INPUT_OUTPUT, m_nrcCtx->getBufferSlice(ctx, nrc::BufferIdx::DebugTrainingPathInfo));

      ctx.bindResourceView(NRC_RESOLVE_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_RAYTRACE_ARGS_INPUT, DxvkBufferSlice(raytraceArgsBuffer, 0, raytraceArgsBuffer->info().size));

      ctx.bindResourceView(NRC_RESOLVE_BINDING_PRIMARY_DIFFUSE_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectDiffuseRadiance.view(Resources::AccessType::ReadWrite), nullptr);
      ctx.bindResourceView(NRC_RESOLVE_BINDING_PRIMARY_SPECULAR_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_primaryIndirectSpecularRadiance.view(Resources::AccessType::ReadWrite), nullptr);
      ctx.bindResourceView(NRC_RESOLVE_BINDING_INDIRECT_RADIANCE_HIT_DISTANCE_INPUT_OUTPUT, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::ReadWrite), nullptr);

      ctx.bindResourceView(NRC_RESOLVE_BINDING_DEBUG_VIEW_TEXTURE_OUTPUT, debugView.getDebugOutput(), nullptr);
      ctx.bindResourceBuffer(NRC_RESOLVE_BINDING_GPU_PRINT_BUFFER_OUTPUT, DxvkBufferSlice(rtOutput.m_gpuPrintBuffer, 0, rtOutput.m_gpuPrintBuffer.ptr() ? rtOutput.m_gpuPrintBuffer->info().size : 0));
    }

    const uint32_t kMaxFramesToAccumulate = 300;

    // Push constants
    NrcResolvePushConstants pushArgs = {};
    pushArgs.resolution = uvec2 {
      m_nrcCtxSettings->frameDimensions.x,
      m_nrcCtxSettings->frameDimensions.y };
    pushArgs.addPathtracedRadiance = NrcOptions::resolveAddPathTracedRadiance();
    pushArgs.addNrcRadiance = NrcOptions::resolveAddNrcQueriedRadiance();
    pushArgs.resolveMode = NrcOptions::enableDebugResolveMode() ? NrcOptions::debugResolveMode() : NrcResolveMode::AddQueryResultToOutput;
    pushArgs.samplesPerPixel = m_nrcCtxSettings->samplesPerPixel;
    pushArgs.resolveModeAccumulationWeight = 0.f;
    pushArgs.debugBuffersAreEnabled = NrcOptions::s_nrcDebugBufferIsRequired;

    // Calculate the smoothing factor when smoothed resolve mode is enabled
    if (pushArgs.resolveMode == NrcResolveMode::TrainingBounceHeatMapSmoothed ||
        pushArgs.resolveMode == NrcResolveMode::PrimaryVertexTrainingRadianceSmoothed) {

      RtCamera& camera = ctx.getSceneManager().getCamera();
      const Matrix4d prevWorldToProjection = camera.getPreviousViewToProjection() * camera.getPreviousWorldToView();
      const Matrix4d worldToProjection = camera.getViewToProjection() * camera.getWorldToView();
      const bool hasCameraChanged = memcmp(&prevWorldToProjection, &worldToProjection, sizeof(Matrix4d)) != 0;

      if (hasCameraChanged) {
        m_numFramesAccumulatedForResolveMode = 0;
      }
   
      m_numFramesAccumulatedForResolveMode = std::min(m_numFramesAccumulatedForResolveMode + 1, kMaxFramesToAccumulate);

      pushArgs.resolveModeAccumulationWeight = 1.f / m_numFramesAccumulatedForResolveMode;

    } else {
      m_numFramesAccumulatedForResolveMode = 0;
    }

    pushArgs.useNrcResolvedRadianceResult = NrcOptions::enableNrcResolver();

    if (pushArgs.useNrcResolvedRadianceResult) {
      m_nrcCtx->resolve(ctx, rtOutput.m_indirectRadianceHitDistance.view(Resources::AccessType::ReadWrite));
    }

    ctx.setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx.pushConstants(0, sizeof(pushArgs), &pushArgs);

    // Dispatch
    const VkExtent3D& numRaysExtent = VkExtent3D {
      m_nrcCtxSettings->frameDimensions.x,
      m_nrcCtxSettings->frameDimensions.y,
      1 };
    VkExtent3D workgroups = util::computeBlockCount(numRaysExtent, VkExtent3D { 16, 8, 1 });

    ctx.bindShader(VK_SHADER_STAGE_COMPUTE_BIT, NrcResolveShader::getShader());
    ctx.dispatch(workgroups.width, workgroups.height, workgroups.depth);
  }

  void NeuralRadianceCache::dispatchTrainingAndResolve(
    RtxContext& ctx,
    const Resources::RaytracingOutput& rtOutput) {
    if (!isActive()) {
      return;
    }

    ScopedGpuProfileZone(&ctx, "NRC: Training and Resolve");
    ctx.setFramePassStage(RtxFramePassStage::NRC);

    // NRC training pass
    {
      // Add pre-training barriers
      {
        // Setup stage and access masks
        VkPipelineStageFlagBits srcStageMask =
          RtxOptions::renderPassIntegrateIndirectRaytraceMode() 
          == RenderPassIntegrateIndirectRaytraceMode::RayQuery
          ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
          : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        VkPipelineStageFlagBits dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

        VkAccessFlags srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        VkAccessFlags destAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        // Create barrier batch infos
        // ToDo - check if all these are needed - NRC also adds barriers
        std::vector<VkBufferMemoryBarrier> barriers;
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::QueryPathInfo, srcAccessMask, destAccessMask));
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::TrainingPathInfo, srcAccessMask, destAccessMask));
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::TrainingPathVertices, srcAccessMask, destAccessMask));
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::QueryRadianceParams, srcAccessMask, destAccessMask));
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::QueryRadiance, srcAccessMask, destAccessMask));
        barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::Counter, srcAccessMask, destAccessMask));
        if (m_nrcCtx->isDebugBufferRequired()) {
          barriers.push_back(m_nrcCtx->createVkBufferMemoryBarrier(nrc::BufferIdx::DebugTrainingPathInfo, srcAccessMask, destAccessMask));
        }

        // Create the barrier batch
        vkCmdPipelineBarrier(ctx.getCmdBuffer(DxvkCmdBuffer::ExecBuffer), srcStageMask, dstStageMask, 0, 0, NULL, barriers.size(), barriers.data(), 0, NULL);
      }

      // Dispatch SDK's query and train
      {
        ScopedGpuProfileZone(&ctx, "NRC SDK: Query and Train");
        m_trainingLoss = m_nrcCtx->queryAndTrain(ctx, NrcOptions::enableCalculateTrainingLoss());
      }

      // NrcCtx::queryAndTrain() generated training records, so query them now
      copyNumberOfTrainingRecords(ctx);
    }

    dispatchResolve(ctx, rtOutput);
  }

  void NeuralRadianceCache::onFrameEnd(Resources::RaytracingOutput& rtOutput) {
    if (!isActive()) {
      return;
    }

    if (m_resetHistory) {
      NrcOptions::resetHistory.setDeferred(false);
      m_resetHistory = false;
    }

    m_nrcCtx->endFrame();
  }
} // namespace dxvk
