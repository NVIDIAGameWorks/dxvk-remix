/*
* Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_pathtracer_gbuffer.h"
#include "dxvk_device.h"
#include "rtx_shader_manager.h"
#include "rtx_options.h"
#include "rtx_neural_radiance_cache.h"

#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/gbuffer/gbuffer_binding_indices.h"
#include "rtx/concept/surface_material/surface_material_hitgroup.h"

#include <rtx_shaders/gbuffer_variants.h>
#include <rtx_shaders/gbuffer_rayquery_variants.h>
#include <rtx_shaders/gbuffer_psr_prepare.h>
#include <rtx_shaders/gbuffer_decal_prepare_variants.h>

#include <rtx_shaders/gbuffer_miss.h>
#include <rtx_shaders/gbuffer_nrc_miss.h>
#include <rtx_shaders/gbuffer_psr_miss.h>
#include <rtx_shaders/gbuffer_psr_nrc_miss.h>

#include <rtx_shaders/gbuffer_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_nrc_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_nrc_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_psr_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_psr_material_rayPortal_closesthit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_opaque_translucent_closesthit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_rayPortal_closesthit.h>

#include <rtx_shaders/gbuffer_miss_wboit.h>
#include <rtx_shaders/gbuffer_nrc_miss_wboit.h>
#include <rtx_shaders/gbuffer_psr_miss_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_miss_wboit.h>

#include <rtx_shaders/gbuffer_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_nrc_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_nrc_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_material_rayPortal_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_opaque_translucent_closesthit_wboit.h>
#include <rtx_shaders/gbuffer_psr_nrc_material_rayPortal_closesthit_wboit.h>

#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_opacity_micromap_manager.h"

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class GbufferRayGenShader : public ManagedShader {
    public:
      BINDLESS_ENABLED()

      PUSH_CONSTANTS(GbufferPushConstants)

      BEGIN_PARAMETER()
        COMMON_RAYTRACING_BINDINGS

        SAMPLER(GBUFFER_BINDING_LINEAR_WRAP_SAMPLER)

        SAMPLER3D(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT)
        SAMPLER3D(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_FLAGS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_RADIANCE_RG_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_RADIANCE_B_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_INTEGRATION_SURFACE_PDF_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MATERIAL_DATA0_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MATERIAL_DATA1_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_TEXTURE_COORD_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SURFACE_INDEX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SUBSURFACE_DATA_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_ATTENUATION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_LINEAR_VIEW_Z_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_ALBEDO_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_BASE_REFLECTIVITY_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_MVEC_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_VIEW_DIRECTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_CONE_RADIUS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_POSITION_ERROR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_ATTENUATION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_LINEAR_VIEW_Z_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_ALBEDO_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_BASE_REFLECTIVITY_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_MVEC_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_HIT_DISTANCE_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_VIEW_DIRECTION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_CONE_RADIUS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_WORLD_POSITION_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SECONDARY_POSITION_ERROR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SURFACE_FLAGS_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DEPTH_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_SHARED_BIAS_CURRENT_COLOR_MASK_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_ALPHA_BLEND_GBUFFER_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PARTICLE_BUFFER_OUTPUT)
        SAMPLER2D(GBUFFER_BINDING_SKYMATTE)
        SAMPLERCUBE(GBUFFER_BINDING_SKYPROBE)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_0)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_WORLD_POSITION_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_1)

        RW_TEXTURE2D(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_2)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_0)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_1)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_2)
        RW_TEXTURE2D(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_3)

        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_DEPTH_DLSSRR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_NORMAL_DLSSRR_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_DLSSRR_OUTPUT)

        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_QUERY_PATH_INFO_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_TRAINING_PATH_INFO_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_TRAINING_PATH_VERTICES_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_QUERY_RADIANCE_PARAMS_OUTPUT)
        RW_STRUCTURED_BUFFER(GBUFFER_BINDING_NRC_COUNTERS_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_NRC_QUERY_PATH_DATA0_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_QUERY_PATH_DATA1_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_PATH_DATA1_OUTPUT)

        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_RG_OUTPUT)
        RW_TEXTURE2D(GBUFFER_BINDING_NRC_TRAINING_GBUFFER_SURFACE_RADIANCE_B_OUTPUT)

      END_PARAMETER()
    };

    class GbufferClosestHitShader : public ManagedShader {

      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    class GbufferMissShader : public ManagedShader {
      
      BEGIN_PARAMETER()
      END_PARAMETER()
    };

    constexpr uint32_t kGbufferVariantPSR = 1u << 0;
    constexpr uint32_t kGbufferVariantWBOIT = 1u << 1;
    constexpr uint32_t kGbufferVariantSER = 1u << 2;
    constexpr uint32_t kGbufferVariantPortals = 1u << 3;
    constexpr uint32_t kGbufferVariantLeanPSRFeatures = 1u << 4;
    constexpr uint32_t kGbufferVariantLeanNoPSRFeatures = 1u << 5;
    constexpr uint32_t kGbufferVariantDebugFeatures = 1u << 6;
    constexpr uint32_t kGbufferVariantNRC = 1u << 7;

    constexpr uint32_t kGbufferDecalPrepareVariantDebug = 1u << 0;

    constexpr uint32_t kGbufferRayQueryFeatureParticleLayer = 1u << 0;
    constexpr uint32_t kGbufferRayQueryFeatureRaytracedRenderTarget = 1u << 1;
    constexpr uint32_t kGbufferRayQueryFeatureStochasticAlpha = 1u << 2;
    constexpr uint32_t kGbufferRayQueryFeatureVariantPass = 1u << 8;
    constexpr uint32_t kGbufferRayQueryFeatureVariantWBOIT = 1u << 9;
    constexpr uint32_t kGbufferRayQueryFeatureVariantNRC = 1u << 10;
    constexpr uint32_t kGbufferRayQueryFeatureVariantCount =
      RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_DEBUG + 1u;

    static_assert(RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_NO_PSR == 0u);
    static_assert(RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_INLINE == 8u);
    static_assert(RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_PREPARE == 16u);
    static_assert(RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_DEBUG == 20u);

    constexpr uint32_t getGbufferFeatureVariantKey(
      const uint32_t features) {
      switch (features) {
      case RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_DEBUG:
        return kGbufferVariantDebugFeatures;
      case RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_PSR:
        return kGbufferVariantLeanPSRFeatures;
      case RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_NO_PSR:
        return kGbufferVariantLeanNoPSRFeatures;
      default:
        return 0u;
      }
    }

    constexpr uint32_t getGbufferMatrixVariantKey(
      const uint32_t features,
      const uint32_t psr,
      const uint32_t nrc,
      const uint32_t wboit) {
      return
        getGbufferFeatureVariantKey(features) |
        (psr == RTX_SHADER_VARIANT_MATRIX_GBUFFER_PSR_PSR ? kGbufferVariantPSR : 0u) |
        (nrc == RTX_SHADER_VARIANT_MATRIX_GBUFFER_NRC_NRC ? kGbufferVariantNRC : 0u) |
        (wboit == RTX_SHADER_VARIANT_MATRIX_GBUFFER_WBOIT_WBOIT ? kGbufferVariantWBOIT : 0u);
    }

    constexpr uint32_t getGbufferVariantKey(
      const uint32_t features,
      const bool isPSRPass,
      const bool nrcEnabled,
      const bool wboitEnabled) {
      return
        getGbufferFeatureVariantKey(features) |
        (isPSRPass ? kGbufferVariantPSR : 0u) |
        (nrcEnabled ? kGbufferVariantNRC : 0u) |
        (wboitEnabled ? kGbufferVariantWBOIT : 0u);
    }

    constexpr uint32_t getGbufferPassVariantKey(
      const bool isPSRPass,
      const bool nrcEnabled,
      const bool wboitEnabled) {
      return getGbufferVariantKey(
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_NONE,
        isPSRPass,
        nrcEnabled,
        wboitEnabled);
    }

    constexpr uint32_t selectGbufferFeatureVariant(
      const bool debugViewEnabled,
      const bool objectPickingEnabled,
      const bool psrEnabled,
      const bool useDefaultFeatureVariant) {
      if (debugViewEnabled || objectPickingEnabled) {
        return RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_DEBUG;
      }

      if (useDefaultFeatureVariant) {
        return RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_NONE;
      }

      return psrEnabled
        ? RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_PSR
        : RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_NO_PSR;
    }

    constexpr uint32_t getGbufferRayQueryFeatureMask(
      const bool particleLayerEnabled,
      const bool raytracedRenderTargetEnabled,
      const bool stochasticAlphaEnabled) {
      return
        (particleLayerEnabled ? kGbufferRayQueryFeatureParticleLayer : 0u) |
        (raytracedRenderTargetEnabled ? kGbufferRayQueryFeatureRaytracedRenderTarget : 0u) |
        (stochasticAlphaEnabled ? kGbufferRayQueryFeatureStochasticAlpha : 0u);
    }

    constexpr uint32_t getGbufferRayQueryPSRPrepareFeatureMask(
      const uint32_t featureMask) {
      return
        (featureMask & kGbufferRayQueryFeatureParticleLayer) |
        ((featureMask & kGbufferRayQueryFeatureStochasticAlpha) >> 1u);
    }

    constexpr uint32_t selectGbufferRayQueryFeatureVariant(
      const bool debugViewEnabled,
      const bool objectPickingEnabled,
      const bool psrEnabled,
      const bool usePSRPrepare,
      const bool particleLayerEnabled,
      const bool raytracedRenderTargetEnabled,
      const bool stochasticAlphaEnabled) {
      if (debugViewEnabled || objectPickingEnabled) {
        return RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_DEBUG;
      }

      const uint32_t featureMask = getGbufferRayQueryFeatureMask(
        particleLayerEnabled,
        raytracedRenderTargetEnabled,
        stochasticAlphaEnabled);

      if (!psrEnabled) {
        return RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_NO_PSR + featureMask;
      }

      if (usePSRPrepare) {
        return RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_PREPARE +
          getGbufferRayQueryPSRPrepareFeatureMask(featureMask);
      }

      return RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_INLINE + featureMask;
    }

    constexpr uint32_t getGbufferTraceRayGenVariantKey(
      const uint32_t features,
      const uint32_t psr,
      const uint32_t pipeline,
      const uint32_t nrc,
      const uint32_t wboit) {
      return getGbufferMatrixVariantKey(features, psr, nrc, wboit) |
        (pipeline == RTX_SHADER_VARIANT_MATRIX_GBUFFER_PIPELINE_RAYGEN_SER ? kGbufferVariantSER : 0u);
    }

    constexpr uint32_t getGbufferRayQueryMatrixVariantKey(
      const uint32_t features,
      const uint32_t pass,
      const uint32_t nrc,
      const uint32_t wboit) {
      return
        features |
        (pass == RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_PASS_PSR ? kGbufferRayQueryFeatureVariantPass : 0u) |
        (nrc == RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_NRC_NRC ? kGbufferRayQueryFeatureVariantNRC : 0u) |
        (wboit == RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_WBOIT_WBOIT ? kGbufferRayQueryFeatureVariantWBOIT : 0u);
    }

    constexpr uint32_t getGbufferRayQueryVariantKey(
      const uint32_t features,
      const bool isPSRPass,
      const bool nrcEnabled,
      const bool wboitEnabled) {
      return
        features |
        (isPSRPass ? kGbufferRayQueryFeatureVariantPass : 0u) |
        (nrcEnabled ? kGbufferRayQueryFeatureVariantNRC : 0u) |
        (wboitEnabled ? kGbufferRayQueryFeatureVariantWBOIT : 0u);
    }

    constexpr uint32_t getGbufferDecalPrepareMatrixVariantKey(
      const uint32_t debug) {
      return
        (debug == RTX_SHADER_VARIANT_MATRIX_GBUFFER_DECAL_PREPARE_DEBUG_DEBUG ? kGbufferDecalPrepareVariantDebug : 0u);
    }

    constexpr uint32_t getGbufferDecalPrepareVariantKey(
      const bool debugViewEnabled,
      const bool objectPickingEnabled) {
      return
        (debugViewEnabled || objectPickingEnabled ? kGbufferDecalPrepareVariantDebug : 0u);
    }

    Rc<DxvkShader> getGbufferRayQueryRayGenShader(const uint32_t key) {
      switch (key) {
#define GBUFFER_RAYQUERY_RAYGEN_CASE(features, psr, pipeline, nrc, wboit, code) \
        case getGbufferMatrixVariantKey(features, psr, nrc, wboit): \
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, code);
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_PIPELINE_RAYQUERY_RAYGEN_RGEN(GBUFFER_RAYQUERY_RAYGEN_CASE)
#undef GBUFFER_RAYQUERY_RAYGEN_CASE
        default:
          assert(false && "Invalid GBuffer RayQuery raygen shader variant");
          return nullptr;
      }
    }

    Rc<DxvkShader> getGbufferTraceRayGenShader(const uint32_t key) {
      switch (key) {
#define GBUFFER_TRACE_RAYGEN_CASE(features, psr, pipeline, nrc, wboit, code) \
        case getGbufferTraceRayGenVariantKey(features, psr, pipeline, nrc, wboit): \
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_RAYGEN_BIT_KHR, GbufferRayGenShader, code);
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_PIPELINE_RAYGEN_RGEN(GBUFFER_TRACE_RAYGEN_CASE)
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_PIPELINE_RAYGEN_SER_RGEN(GBUFFER_TRACE_RAYGEN_CASE)
#undef GBUFFER_TRACE_RAYGEN_CASE
        default:
          assert(false && "Invalid GBuffer TraceRay raygen shader variant");
          return nullptr;
      }
    }

    Rc<DxvkShader> getGbufferRayQueryComputeShader(const uint32_t key) {
      switch (key) {
#define GBUFFER_RAYQUERY_COMPUTE_CASE(features, pass, nrc, wboit, code) \
        case getGbufferRayQueryMatrixVariantKey(features, pass, nrc, wboit): \
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, code);
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_PASS_NONE_COMP(GBUFFER_RAYQUERY_COMPUTE_CASE)
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_PASS_PSR_COMP(GBUFFER_RAYQUERY_COMPUTE_CASE)
#undef GBUFFER_RAYQUERY_COMPUTE_CASE
        default:
          assert(false && "Invalid GBuffer RayQuery compute shader variant");
          return nullptr;
      }
    }

    Rc<DxvkShader> getGbufferPSRPrepareComputeShader() {
      return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, gbuffer_psr_prepare);
    }

    Rc<DxvkShader> getGbufferDecalPrepareComputeShader(const uint32_t key) {
      switch (key) {
#define GBUFFER_DECAL_PREPARE_CASE(debug, code) \
        case getGbufferDecalPrepareMatrixVariantKey(debug): \
          return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, GbufferRayGenShader, code);
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_DECAL_PREPARE_DEBUG_NONE_COMP(GBUFFER_DECAL_PREPARE_CASE)
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_DECAL_PREPARE_DEBUG_DEBUG_COMP(GBUFFER_DECAL_PREPARE_CASE)
#undef GBUFFER_DECAL_PREPARE_CASE
        default:
          assert(false && "Invalid GBuffer decal prepare shader variant");
          return nullptr;
      }
    }

    void prewarmGbufferDecalPrepareComputeShaders() {
      for (uint32_t key = 0; key < (1u << 1); key++) {
        getGbufferDecalPrepareComputeShader(key);
      }
    }

    void prewarmGbufferRayQueryComputeShaders(
      const bool allVariants,
      const bool nrcEnabled,
      const bool wboitEnabled) {
      if (allVariants) {
        for (uint32_t featureVariant = 0; featureVariant < kGbufferRayQueryFeatureVariantCount; featureVariant++) {
          for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
            for (int32_t useNRC = nrcEnabled; useNRC >= 0; useNRC--) {
              for (int32_t useWBOIT = 1; useWBOIT >= 0; useWBOIT--) {
                getGbufferRayQueryComputeShader(getGbufferRayQueryVariantKey(featureVariant, isPSRPass, useNRC, useWBOIT));
              }
            }
          }
        }

        return;
      }

      const uint32_t featureVariants[] = {
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_NO_PSR,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_NO_PSR + 7u,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_INLINE,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_INLINE + 7u,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_PREPARE,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_PSR_PREPARE + 3u,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_RAYQUERY_FEATURES_DEBUG,
      };

      for (const uint32_t featureVariant : featureVariants) {
        for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
          for (int32_t useNRC = nrcEnabled; useNRC >= 0; useNRC--) {
            getGbufferRayQueryComputeShader(getGbufferRayQueryVariantKey(featureVariant, isPSRPass, useNRC, wboitEnabled));
          }
        }
      }
    }

    Rc<DxvkShader> getGbufferMissShader(const uint32_t key) {
      switch (key) {
      case 0:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_miss);
      case kGbufferVariantPSR:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_miss);
      case kGbufferVariantNRC:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_nrc_miss);
      case kGbufferVariantPSR | kGbufferVariantNRC:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_nrc_miss);
      case kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_miss_wboit);
      case kGbufferVariantPSR | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_miss_wboit);
      case kGbufferVariantNRC | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_nrc_miss_wboit);
      case kGbufferVariantPSR | kGbufferVariantNRC | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_MISS_BIT_KHR, GbufferMissShader, gbuffer_psr_nrc_miss_wboit);
      default:
        assert(false && "Invalid GBuffer miss shader variant");
        return nullptr;
      }
    }

    Rc<DxvkShader> getGbufferClosestHitShader(const uint32_t key) {
      switch (key) {
      case 0:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_opaque_translucent_closestHit);
      case kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_rayportal_closestHit);
      case kGbufferVariantNRC:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_opaque_translucent_closestHit);
      case kGbufferVariantNRC | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_rayportal_closestHit);
      case kGbufferVariantPSR:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_opaque_translucent_closestHit);
      case kGbufferVariantPSR | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_rayportal_closestHit);
      case kGbufferVariantPSR | kGbufferVariantNRC:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_opaque_translucent_closestHit);
      case kGbufferVariantPSR | kGbufferVariantNRC | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_rayportal_closestHit);
      case kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_opaque_translucent_closestHit_wboit);
      case kGbufferVariantWBOIT | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_material_rayportal_closestHit_wboit);
      case kGbufferVariantNRC | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_opaque_translucent_closestHit_wboit);
      case kGbufferVariantNRC | kGbufferVariantWBOIT | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_nrc_material_rayportal_closestHit_wboit);
      case kGbufferVariantPSR | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_opaque_translucent_closestHit_wboit);
      case kGbufferVariantPSR | kGbufferVariantWBOIT | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_material_rayportal_closestHit_wboit);
      case kGbufferVariantPSR | kGbufferVariantNRC | kGbufferVariantWBOIT:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_opaque_translucent_closestHit_wboit);
      case kGbufferVariantPSR | kGbufferVariantNRC | kGbufferVariantWBOIT | kGbufferVariantPortals:
        return GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, GbufferClosestHitShader, gbuffer_psr_nrc_material_rayportal_closestHit_wboit);
      default:
        assert(false && "Invalid GBuffer closest hit shader variant");
        return nullptr;
      }
    }
  }

  DxvkPathtracerGbuffer::DxvkPathtracerGbuffer(DxvkDevice* device) : CommonDeviceObject(device) {
  }

  void DxvkPathtracerGbuffer::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    ScopedCpuProfileZoneN("Gbuffer Shader Prewarming");

    const bool isNrcSupported = NeuralRadianceCache::checkIsSupported(device());
    const bool isOpacityMicromapSupported = OpacityMicromapManager::checkIsOpacityMicromapSupported(*m_device);
    const bool isShaderExecutionReorderingSupported = 
      RtxContext::checkIsShaderExecutionReorderingSupported(*m_device) && 
      RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
    const bool portalsEnabled = RtxOptions::rayPortalModelTextureHashes().size() > 0;

    if (RtxOptions::Shader::prewarmAllVariants()) {
      const uint32_t featureVariants[] = {
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_NONE,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_DEBUG,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_PSR,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_NO_PSR,
      };

      for (const uint32_t featureVariant : featureVariants) {
        for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
          for (int32_t nrcEnabled = isNrcSupported; nrcEnabled >= 0; nrcEnabled--) {
            for (int32_t wboitEnabled = 1; wboitEnabled >= 0; wboitEnabled--) {
              for (int32_t includePortals = portalsEnabled; includePortals >= 0; includePortals--) {
                for (int32_t useRayQuery = 1; useRayQuery >= 0; useRayQuery--) {
                  for (int32_t serEnabled = isShaderExecutionReorderingSupported; serEnabled >= 0; serEnabled--) {
                    for (int32_t ommEnabled = isOpacityMicromapSupported; ommEnabled >= 0; ommEnabled--) {
                      pipelineManager.registerRaytracingShaders(getPipelineShaders(featureVariant, isPSRPass, useRayQuery, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
                    }
                  }
                }
              }
            }
          }
        }
      }

      prewarmGbufferRayQueryComputeShaders(true, isNrcSupported, false);
      getGbufferPSRPrepareComputeShader();
      prewarmGbufferDecalPrepareComputeShaders();
    } else {
      // Note: The getters for these SER/OMM enabled flags also check if SER/OMMs are supported, so we do not need to check for that manually.
      const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
      const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();
      const bool nrcEnabled = RtxOptions::integrateIndirectMode() == IntegrateIndirectMode::NeuralRadianceCache;
      const bool wboitEnabled = RtxOptions::wboitEnabled();

      const uint32_t featureVariants[] = {
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_NONE,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_DEBUG,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_PSR,
        RTX_SHADER_VARIANT_MATRIX_GBUFFER_FEATURES_LEAN_NO_PSR,
      };

      switch (RtxOptions::renderPassGBufferRaytraceMode()) {
      case RaytraceMode::RayQuery:
        prewarmGbufferRayQueryComputeShaders(false, nrcEnabled, wboitEnabled);
        break;
      case RaytraceMode::RayQueryRayGen:
      case RaytraceMode::TraceRay:
        // Need both PSR and non-PSR passes.
        for (const uint32_t featureVariant : featureVariants) {
          for (int32_t isPSRPass = 1; isPSRPass >= 0; isPSRPass--) {
            for (int32_t includePortals = portalsEnabled; includePortals >= 0; includePortals--) {
              pipelineManager.registerRaytracingShaders(getPipelineShaders(
                featureVariant,
                isPSRPass,
                RtxOptions::renderPassGBufferRaytraceMode() == RaytraceMode::RayQueryRayGen,
                serEnabled,
                ommEnabled,
                includePortals,
                nrcEnabled,
                wboitEnabled));
            }
          }
        }
        break;
      case RaytraceMode::Count:
        assert(false && "Invalid RaytraceMode in DxvkPathtracerGbuffer::prewarmShaders");
        break;
      }

      if (!portalsEnabled) {
        getGbufferPSRPrepareComputeShader();
      }

      prewarmGbufferDecalPrepareComputeShaders();
    }
  }

  void DxvkPathtracerGbuffer::dispatch(
    RtxContext* ctx, 
    const Resources::RaytracingOutput& rtOutput) {
    ScopedGpuProfileZone(ctx, "Gbuffer Raytracing");
    ctx->setFramePassStage(RtxFramePassStage::GBufferPrimaryRays);

    // Bind resources

    ctx->bindCommonRayTracingResources(rtOutput);

    // Note: Clamp to edge used to avoid interpolation to black on the edges of the view.
    Rc<DxvkSampler> linearClampSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    Rc<DxvkSampler> linearWrapSampler = ctx->getResourceManager().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    ctx->bindResourceSampler(GBUFFER_BINDING_LINEAR_WRAP_SAMPLER, linearWrapSampler);

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_Y_INPUT, linearClampSampler);
    ctx->bindResourceView(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_VOLUME_FILTERED_RADIANCE_CO_CG_INPUT, linearClampSampler);

    ctx->bindResourceView(GBUFFER_BINDING_SKYMATTE, ctx->getResourceManager().getSkyMatte(ctx).view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_SKYMATTE, linearClampSampler);

    // Requires the probe too for PSRR/T miss
    ctx->bindResourceView(GBUFFER_BINDING_SKYPROBE, ctx->getResourceManager().getSkyProbe(ctx).view, nullptr);
    ctx->bindResourceSampler(GBUFFER_BINDING_SKYPROBE, linearClampSampler);

    // Output resources

    ctx->bindResourceView(GBUFFER_BINDING_SHARED_FLAGS_OUTPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_RADIANCE_RG_OUTPUT, rtOutput.m_sharedRadianceRG.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_RADIANCE_B_OUTPUT, rtOutput.m_sharedRadianceB.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_INTEGRATION_SURFACE_PDF_OUTPUT, rtOutput.m_sharedIntegrationSurfacePdf.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MATERIAL_DATA0_OUTPUT, rtOutput.m_sharedMaterialData0.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MATERIAL_DATA1_OUTPUT, rtOutput.m_sharedMaterialData1.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_MEDIUM_MATERIAL_INDEX_OUTPUT, rtOutput.m_sharedMediumMaterialIndex.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_BIAS_CURRENT_COLOR_MASK_OUTPUT, rtOutput.m_sharedBiasCurrentColorMask.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_TEXTURE_COORD_OUTPUT, rtOutput.m_sharedTextureCoord.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SURFACE_INDEX_OUTPUT, rtOutput.m_sharedSurfaceIndex.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SUBSURFACE_DATA_OUTPUT, rtOutput.m_sharedSubsurfaceData.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SHARED_SUBSURFACE_DIFFUSION_PROFILE_DATA_OUTPUT, rtOutput.m_sharedSubsurfaceDiffusionProfileData.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_ATTENUATION_OUTPUT, rtOutput.m_primaryAttenuation.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_primaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_INTERPOLATED_NORMAL_OUTPUT, rtOutput.m_primaryWorldInterpolatedNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_PERCEPTUAL_ROUGHNESS_OUTPUT, rtOutput.m_primaryPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_LINEAR_VIEW_Z_OUTPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_ALBEDO_OUTPUT, rtOutput.m_primaryAlbedo.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_BASE_REFLECTIVITY_OUTPUT, rtOutput.m_primaryBaseReflectivity.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_MVEC_OUTPUT, rtOutput.m_primaryVirtualMotionVector.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_OUTPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughnessDenoising.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_HIT_DISTANCE_OUTPUT, rtOutput.m_primaryHitDistance.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_VIEW_DIRECTION_OUTPUT, rtOutput.m_primaryViewDirection.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_CONE_RADIUS_OUTPUT, rtOutput.m_primaryConeRadius.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_POSITION_ERROR_OUTPUT, rtOutput.m_primaryPositionError.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SURFACE_FLAGS_OUTPUT, rtOutput.m_primarySurfaceFlags.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DISOCCLUSION_THRESHOLD_MIX_OUTPUT, rtOutput.m_primaryDisocclusionThresholdMix.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DEPTH_OUTPUT, rtOutput.m_primaryDepth.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_OBJECT_PICKING_OUTPUT, rtOutput.m_primaryObjectPicking.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_ATTENUATION_OUTPUT, rtOutput.m_secondaryAttenuation.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_secondaryWorldShadingNormal.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_PERCEPTUAL_ROUGHNESS_OUTPUT, rtOutput.m_secondaryPerceptualRoughness.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_LINEAR_VIEW_Z_OUTPUT, rtOutput.m_secondaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_ALBEDO_OUTPUT, rtOutput.m_secondaryAlbedo.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_BASE_REFLECTIVITY_OUTPUT, rtOutput.m_secondaryBaseReflectivity.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_MVEC_OUTPUT, rtOutput.m_secondaryVirtualMotionVector.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_OUTPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIRTUAL_WORLD_SHADING_NORMAL_DENOISING_OUTPUT, rtOutput.m_secondaryVirtualWorldShadingNormalPerceptualRoughnessDenoising.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_HIT_DISTANCE_OUTPUT, rtOutput.m_secondaryHitDistance.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_VIEW_DIRECTION_OUTPUT, rtOutput.m_secondaryViewDirection.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_CONE_RADIUS_OUTPUT, rtOutput.m_secondaryConeRadius.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_POSITION_ERROR_OUTPUT, rtOutput.m_secondaryPositionError.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_SECONDARY_WORLD_POSITION_OUTPUT, rtOutput.m_secondaryWorldPositionWorldTriangleNormal.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_ALPHA_BLEND_GBUFFER_OUTPUT, rtOutput.m_alphaBlendGBuffer.view, nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PARTICLE_BUFFER_OUTPUT, rtOutput.m_rayReconstructionParticleBuffer.view, nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_0, rtOutput.m_gbufferPSRData[0].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_WORLD_POSITION_OUTPUT, rtOutput.getCurrentPrimaryWorldPositionWorldTriangleNormal().view(Resources::AccessType::Write), nullptr);

    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_1, rtOutput.m_gbufferPSRData[1].view(Resources::AccessType::Write), nullptr);

    // Note: m_gbufferPSRData[2..6] are aliased with various radiance textures that are used later as integrator outputs.
    ctx->bindResourceView(GBUFFER_BINDING_REFLECTION_PSR_DATA_STORAGE_2, rtOutput.m_gbufferPSRData[2].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_0, rtOutput.m_gbufferPSRData[3].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_1, rtOutput.m_gbufferPSRData[4].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_2, rtOutput.m_gbufferPSRData[5].view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_TRANSMISSION_PSR_DATA_STORAGE_3, rtOutput.m_gbufferPSRData[6].view(Resources::AccessType::Write), nullptr);

    // Bind necessary buffers for DLSS-RR. 
    // Note: RR uses different PSR rules compared to other uses, and its resolves are resolved in an another shader.
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_DEPTH_DLSSRR_OUTPUT, rtOutput.m_primaryDepthDLSSRR.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_NORMAL_DLSSRR_OUTPUT, rtOutput.m_primaryWorldShadingNormalDLSSRR.view(Resources::AccessType::Write), nullptr);
    ctx->bindResourceView(GBUFFER_BINDING_PRIMARY_SCREEN_SPACE_MOTION_DLSSRR_OUTPUT, rtOutput.m_primaryScreenSpaceMotionVectorDLSSRR.view, nullptr);

    // Bind necessary resources for Neural Radiance Cache
    NeuralRadianceCache& nrc = ctx->getCommonObjects()->metaNeuralRadianceCache();
    nrc.bindGBufferPathTracingResources(*ctx);    
  
    const VkExtent3D& rayDims = rtOutput.m_compositeOutputExtent;

    const bool nrcEnabled = nrc.isActive();
    const bool serEnabled = RtxOptions::isShaderExecutionReorderingInPathtracerGbufferEnabled();
    const bool ommEnabled = RtxOptions::getEnableOpacityMicromap();
    const bool includePortals = RtxOptions::rayPortalModelTextureHashes().size() > 0 || rtOutput.m_raytraceArgs.numActiveRayPortals > 0;
    const bool wboitEnabled = RtxOptions::wboitEnabled();
    const bool psrEnabled = rtOutput.m_raytraceArgs.enablePSRR || rtOutput.m_raytraceArgs.enablePSTR;
    const bool debugViewEnabled = rtOutput.m_raytraceArgs.debugView != 0;
    const bool usePipelineDefaultFeatureVariant =
      rtOutput.m_raytraceArgs.enableDLSSRR ||
      rtOutput.m_raytraceArgs.outputParticleLayer ||
      rtOutput.m_raytraceArgs.enableRaytracedRenderTarget ||
      rtOutput.m_raytraceArgs.enableStochasticAlphaBlend;
    const uint32_t pipelineFeatureVariant = selectGbufferFeatureVariant(
      debugViewEnabled,
      rtOutput.m_raytraceArgs.enableObjectPicking,
      psrEnabled,
      usePipelineDefaultFeatureVariant);
    const bool deferUnorderedDecals =
      rtOutput.m_raytraceArgs.enableSeparateUnorderedApproximations &&
      rtOutput.m_raytraceArgs.enableDecalMaterialBlending;
    const uint32_t decalPrepareVariantKey = getGbufferDecalPrepareVariantKey(
      debugViewEnabled,
      rtOutput.m_raytraceArgs.enableObjectPicking);
    // Keep NRC on the original inline PSR sampling path. NRC updates depend
    // on knowing whether the current GBuffer hit is the final integrated
    // surface, which PSR prepare defers to a later pass.
    const bool usePSRPrepare =
      psrEnabled &&
      !nrcEnabled &&
      !rtOutput.m_raytraceArgs.enableRaytracedRenderTarget;
    const uint32_t rayQueryFeatureVariant = selectGbufferRayQueryFeatureVariant(
      debugViewEnabled,
      rtOutput.m_raytraceArgs.enableObjectPicking,
      psrEnabled,
      usePSRPrepare,
      rtOutput.m_raytraceArgs.outputParticleLayer,
      rtOutput.m_raytraceArgs.enableRaytracedRenderTarget,
      rtOutput.m_raytraceArgs.enableStochasticAlphaBlend);
    const VkExtent3D workgroups = util::computeBlockCount(rayDims, VkExtent3D { 16, 8, 1 });

    GbufferPushConstants pushArgs = {};
    pushArgs.isTransmissionPSR = 0;
    pushArgs.usePSRPrepare = usePSRPrepare;
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);

    // The main GBuffer pass now leaves some uncommon work for small follow-up
    // passes. This keeps the hot Primary Rays shader variants from carrying
    // PSR sampling or unordered decal blending when those features are inactive
    // or rare.
    auto dispatchPSRPrepare = [&]() {
      if (!usePSRPrepare) {
        return;
      }

      // Classify PSR candidate pixels and serialize the PSR payloads used by
      // the Reflection/Transmission PSR passes. This avoids running the
      // material-heavy PSR sampling path for every primary hit.
      ScopedGpuProfileZone(ctx, "PSR Prepare");
      pushArgs.isTransmissionPSR = 0;
      pushArgs.usePSRPrepare = 0;
      ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getGbufferPSRPrepareComputeShader());
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    };

    auto dispatchDeferredDecals = [&]() {
      if (!deferUnorderedDecals) {
        return;
      }

      // Apply unordered decal material blending after the main GBuffer pass.
      // Decals are sparse, so keeping their sampling/blending out of Primary
      // Rays reduces register pressure in the common path.
      ScopedGpuProfileZone(ctx, "Unordered Decal Prepare");
      ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getGbufferDecalPrepareComputeShader(decalPrepareVariantKey));
      ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
    };

    switch (RtxOptions::renderPassGBufferRaytraceMode()) {
    case RaytraceMode::RayQuery:
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(rayQueryFeatureVariant, false, nrcEnabled, wboitEnabled));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      dispatchDeferredDecals();
      dispatchPSRPrepare();

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getComputeShader(rayQueryFeatureVariant, true, nrcEnabled, wboitEnabled));
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }
      break;

      case RaytraceMode::RayQueryRayGen:
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(pipelineFeatureVariant, false, true, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      dispatchDeferredDecals();
      dispatchPSRPrepare();

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(pipelineFeatureVariant, true, true, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }
      break;

      case RaytraceMode::TraceRay:
      {
        ScopedGpuProfileZone(ctx, "Primary Rays");
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(pipelineFeatureVariant, false, false, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      dispatchDeferredDecals();
      dispatchPSRPrepare();

      {
        // Warning: do not change the order of Reflection and Transmission PSR, that will break
        // PSR data dependencies due to resource aliasing.
        ScopedGpuProfileZone(ctx, "Reflection PSR");
        ctx->setFramePassStage(RtxFramePassStage::ReflectionPSR);
        ctx->bindRaytracingPipelineShaders(getPipelineShaders(pipelineFeatureVariant, true, false, serEnabled, ommEnabled, includePortals, nrcEnabled, wboitEnabled));
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }

      {
        ScopedGpuProfileZone(ctx, "Transmission PSR");
        ctx->setFramePassStage(RtxFramePassStage::TransmissionPSR);
        pushArgs.isTransmissionPSR = 1;
        ctx->pushConstants(0, sizeof(pushArgs), &pushArgs);
        ctx->traceRays(rayDims.width, rayDims.height, rayDims.depth);
      }
      break;
      case RaytraceMode::Count:
        assert(false && "Invalid RaytraceMode in DxvkPathtracerGbuffer::dispatch");
      break;
    }
  }

  DxvkRaytracingPipelineShaders DxvkPathtracerGbuffer::getPipelineShaders(
    const uint32_t featureVariant,
    const bool isPSRPass,
    const bool useRayQuery,
    const bool serEnabled,
    const bool ommEnabled,
    const bool includePortals,
    const bool nrcEnabled,
    const bool wboitEnabled) {
    ScopedCpuProfileZone();

    const uint32_t rayGenBaseKey = getGbufferVariantKey(featureVariant, isPSRPass, nrcEnabled, wboitEnabled);
    const uint32_t passKey = getGbufferPassVariantKey(isPSRPass, nrcEnabled, wboitEnabled);
    DxvkRaytracingPipelineShaders shaders;

    if (useRayQuery) {
      shaders.addGeneralShader(getGbufferRayQueryRayGenShader(rayGenBaseKey));
      shaders.debugName = isPSRPass ? "GBuffer PSR RayQuery (RGS)" : "GBuffer RayQuery (RGS)";
    } else {
      const uint32_t rayGenKey = rayGenBaseKey | (serEnabled ? kGbufferVariantSER : 0u);
      const uint32_t closestHitKey = passKey | (includePortals ? kGbufferVariantPortals : 0u);

      shaders.addGeneralShader(getGbufferTraceRayGenShader(rayGenKey));
      shaders.addGeneralShader(getGbufferMissShader(passKey));
      shaders.addHitGroup(getGbufferClosestHitShader(closestHitKey), nullptr, nullptr);
      shaders.debugName = isPSRPass ? "GBuffer PSR TraceRay (RGS)" : "GBuffer TraceRay (RGS)";
    }

    if (ommEnabled) {
      shaders.pipelineFlags |= VK_PIPELINE_CREATE_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT;
    }

    return shaders;
  }

  Rc<DxvkShader> DxvkPathtracerGbuffer::getComputeShader(
    const uint32_t featureVariant,
    const bool isPSRPass,
    const bool nrcEnabled,
    const bool wboitEnabled) const {
    return getGbufferRayQueryComputeShader(getGbufferRayQueryVariantKey(featureVariant, isPSRPass, nrcEnabled, wboitEnabled));
  }

  const char* DxvkPathtracerGbuffer::raytraceModeToString(RaytraceMode raytraceMode)
  {
    switch (raytraceMode)
    {
    case RaytraceMode::RayQuery:
      return "Ray Query [CS]";
    case RaytraceMode::RayQueryRayGen:
      return "Ray Query [RGS]";
    case RaytraceMode::TraceRay:
      return "Trace Ray [RGS]";
    default:
      return "Unknown";
    }
  }
}
