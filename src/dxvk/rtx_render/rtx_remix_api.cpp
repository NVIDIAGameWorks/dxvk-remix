/*
* Copyright (c) 2021-2025, NVIDIA CORPORATION. All rights reserved.
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

#define RTX_REMIX_PNEXT_CHECK_STRUCTS

#include "rtx_asset_data_manager.h"
#include "rtx_asset_replacer.h"
#include "rtx_light_manager.h"
#include "rtx_objectpicking.h"
#include "rtx_option.h"
#include "rtx_globals.h"
#include "rtx_options.h"
#include "rtx_debug_view.h"

#include "../dxvk_device.h"
#include "rtx_texture_manager.h"

#include <remix/remix_c.h>
#include "rtx_remix_pnext.h"

#include "../dxvk_image.h"

#include "../../util/util_math.h"
#include "../../util/util_vector.h"
#include "../../util/util_string.h"

#include "../../d3d9/d3d9_swapchain.h"

#include "../../lssusd/usd_include_begin.h"
#include <src/usd-plugins/RemixParticleSystem/ParticleSystemAPI.h>
#include "../../lssusd/usd_include_end.h"

#include <windows.h>

#include <optional>

namespace dxvk {
  HRESULT CreateD3D9(
          bool           Extended,
          IDirect3D9Ex** ppDirect3D9Ex,
          bool           WithExternalSwapchain,
          bool           WithDrawCallConversion,
          bool           WithRemixAPI);

  extern bool g_allowSrgbConversionForOutput;
  extern bool g_forceKeepObjectPickingImage;

  extern std::array<uint8_t, 3> g_customHighlightColor;
}

namespace dxvk {
  // Because DrawCallState/LegacyMaterialData hide needed fields as private
  struct RemixAPIPrivateAccessor {
    static ExternalDrawState toRtDrawState(const remixapi_InstanceInfo& info);
  };
}

namespace {
  uint64_t s_apiVersion{ 0 };
  IDirect3D9Ex* s_dxvkD3D9 { nullptr };
  dxvk::D3D9DeviceEx* s_dxvkDevice { nullptr };
  dxvk::mutex s_mutex {};


  dxvk::D3D9DeviceEx* tryAsDxvk() {
    return s_dxvkDevice;
  }


  // from rtx_mod_usd.cpp
  XXH64_hash_t hack_getNextGeomHash() {
    static uint64_t s_id = UINT64_MAX;
    std::lock_guard lock { s_mutex };
    --s_id;
    return XXH64(&s_id, sizeof(s_id), 0);
  }


  template<typename T>
  size_t sizeInBytes(const T* values, size_t count) {
    return sizeof(T) * count;
  }


  bool isHResultAliasedWithRemixErrorCode(HRESULT hr) {
    switch (hr) {
    case REMIXAPI_ERROR_CODE_HRESULT_NO_REQUIRED_GPU_FEATURES:
    case REMIXAPI_ERROR_CODE_HRESULT_DRIVER_VERSION_BELOW_MINIMUM:
    case REMIXAPI_ERROR_CODE_HRESULT_DXVK_INSTANCE_EXTENSION_FAIL:
    case REMIXAPI_ERROR_CODE_HRESULT_VK_CREATE_INSTANCE_FAIL:
    case REMIXAPI_ERROR_CODE_HRESULT_VK_CREATE_DEVICE_FAIL: return true;
    default: return false;
    }
  }


  namespace convert {
    using namespace dxvk;

    std::string tostr(const remixapi_MaterialHandle& h) {
      static_assert(sizeof h == sizeof uint64_t);
      return std::to_string(reinterpret_cast<uint64_t>(h));
    }

    Matrix4 tomat4(const remixapi_Transform& transform) {
      const auto& m = transform.matrix;
      return Matrix4 {
        m[0][0], m[1][0], m[2][0], 0.f,
        m[0][1], m[1][1], m[2][1], 0.f,
        m[0][2], m[1][2], m[2][2], 0.f,
        m[0][3], m[1][3], m[2][3], 1.f
      };
    }

    Vector3 tovec3(const remixapi_Float3D& v) {
      return Vector3{ v.x, v.y, v.z };
    }

    Vector4 tovec4(const remixapi_Float4D& v) {
      return Vector4 { v.x, v.y, v.z, v.w };
    }

    Vector3d tovec3d(const remixapi_Float3D& v) {
      return Vector3d{ v.x, v.y, v.z };
    }

    constexpr bool tobool(remixapi_Bool b) {
      return !!b;
    }

    std::filesystem::path topath(remixapi_Path p) {
      if (!p) {
        return {};
      }
      return p;
    }

    // --

    struct PreloadSource {
      std::filesystem::path albedoTexture;
      std::filesystem::path normalTexture;
      std::filesystem::path tangentTexture;
      std::filesystem::path emissiveTexture;
      std::filesystem::path transmittanceTexture;
      std::filesystem::path roughnessTexture;
      std::filesystem::path metallicTexture;
      std::filesystem::path heightTexture;
      std::filesystem::path subsurfaceTransmittanceTexture;
      std::filesystem::path subsurfaceThicknessTexture;
      std::filesystem::path subsurfaceSingleScatteringAlbedoTexture;
      std::filesystem::path subsurfaceRadiusTexture;
    };

    PreloadSource makePreloadSource(const remixapi_MaterialInfo& info) {
      // TODO: C++20 designated initializers
      if (auto extOpaque = pnext::find<remixapi_MaterialInfoOpaqueEXT>(&info)) {
        auto extSubsurface = pnext::find<remixapi_MaterialInfoOpaqueSubsurfaceEXT>(&info);
        return PreloadSource {
          topath(info.albedoTexture),   // albedoTexture;
          topath(info.normalTexture),   // normalTexture;
          topath(info.tangentTexture),  // tangentTexture;
          topath(info.emissiveTexture), // emissiveTexture;
          {},                           // transmittanceTexture;
          topath(extOpaque->roughnessTexture),  // roughnessTexture;
          topath(extOpaque->metallicTexture),   // metallicTexture;
          topath(extOpaque->heightTexture),     // heightTexture;
          topath(extSubsurface ? extSubsurface->subsurfaceTransmittanceTexture          : nullptr), // subsurfaceTransmittanceTexture;
          topath(extSubsurface ? extSubsurface->subsurfaceThicknessTexture              : nullptr), // subsurfaceThicknessTexture;
          topath(extSubsurface ? extSubsurface->subsurfaceSingleScatteringAlbedoTexture : nullptr), // subsurfaceSingleScatteringAlbedoTexture;
          topath(s_apiVersion >= REMIXAPI_VERSION_MAKE(0, 5, 1) && extSubsurface ? extSubsurface->subsurfaceRadiusTexture : nullptr), // subsurfaceRadiusTexture;
        };
      }
      if (auto extTranslucent = pnext::find<remixapi_MaterialInfoTranslucentEXT>(&info)) {
        return PreloadSource {
          topath(info.albedoTexture),   // albedoTexture;
          topath(info.normalTexture),   // normalTexture;
          topath(info.tangentTexture),  // tangentTexture;
          topath(info.emissiveTexture), // emissiveTexture;
          topath(extTranslucent->transmittanceTexture), // transmittanceTexture;
          {}, // roughnessTexture;
          {}, // metallicTexture;
          {}, // heightTexture;
          {}, // subsurfaceTransmittanceTexture;
          {}, // subsurfaceThicknessTexture;
          {}, // subsurfaceSingleScatteringAlbedoTexture;
          {}, // subsurfaceRadiusTexture;
        };
      }
      if (auto extPortal = pnext::find<remixapi_MaterialInfoPortalEXT>(&info)) {
        return PreloadSource {
          topath(info.albedoTexture),   // albedoTexture;
          topath(info.normalTexture),   // normalTexture;
          topath(info.tangentTexture),  // tangentTexture;
          topath(info.emissiveTexture), // emissiveTexture;
          {}, // transmittanceTexture;
          {}, // roughnessTexture;
          {}, // metallicTexture;
          {}, // heightTexture;
          {}, // subsurfaceTransmittanceTexture;
          {}, // subsurfaceThicknessTexture;
          {}, // subsurfaceSingleScatteringAlbedoTexture;
          {}, // subsurfaceRadiusTexture;
        };
      }
      return {};
    }

    MaterialData toRtMaterialFinalized(dxvk::DxvkContext& ctx, const MaterialData& materialWithoutPreload, const PreloadSource& preload) {
      auto preloadTexture = [&ctx](const std::filesystem::path& path)->TextureRef {
        if (path.empty()) {
          return {};
        }
        auto assetData = AssetDataManager::get().findAsset(path.string());
        if (assetData == nullptr) {
          return {};
        }
        auto uploadedTexture = ctx.getCommonObjects()->getTextureManager()
          .preloadTextureAsset(assetData, dxvk::ColorSpace::AUTO, false);
        return TextureRef { uploadedTexture };
      };

      switch (materialWithoutPreload.getType()) {
      case MaterialDataType::Opaque:
      {
        const auto& src = materialWithoutPreload.getOpaqueMaterialData();
        return MaterialData { OpaqueMaterialData{
          preloadTexture(preload.albedoTexture),
          preloadTexture(preload.normalTexture),
          preloadTexture(preload.tangentTexture),
          preloadTexture(preload.heightTexture),
          preloadTexture(preload.roughnessTexture),
          preloadTexture(preload.metallicTexture),
          preloadTexture(preload.emissiveTexture),
          preloadTexture(preload.subsurfaceTransmittanceTexture),
          preloadTexture(preload.subsurfaceThicknessTexture),
          preloadTexture(preload.subsurfaceSingleScatteringAlbedoTexture),
          preloadTexture(preload.subsurfaceRadiusTexture),
          src.getAnisotropyConstant(),
          src.getEmissiveIntensity(),
          src.getAlbedoConstant(),
          src.getOpacityConstant(),
          src.getRoughnessConstant(),
          src.getMetallicConstant(),
          src.getEmissiveColorConstant(),
          src.getEnableEmission(),
          src.getSpriteSheetRows(),
          src.getSpriteSheetCols(),
          src.getSpriteSheetFPS(),
          src.getEnableThinFilm(),
          src.getAlphaIsThinFilmThickness(),
          src.getThinFilmThicknessConstant(),
          src.getUseLegacyAlphaState(),
          src.getBlendEnabled(),
          src.getBlendType(),
          src.getInvertedBlend(),
          src.getAlphaTestType(),
          src.getAlphaTestReferenceValue(),
          src.getDisplaceIn(),
          src.getDisplaceOut(),
          src.getSubsurfaceTransmittanceColor(),
          src.getSubsurfaceMeasurementDistance(),
          src.getSubsurfaceSingleScatteringAlbedo(),
          src.getSubsurfaceVolumetricAnisotropy(),
          src.getSubsurfaceDiffusionProfile(),
          src.getSubsurfaceRadius(),
          src.getSubsurfaceRadiusScale(),
          src.getSubsurfaceMaxSampleRadius(),
          src.getFilterMode(),
          src.getWrapModeU(),
          src.getWrapModeV()
        } };
      }
      case MaterialDataType::Translucent: 
      {
        const auto& src = materialWithoutPreload.getTranslucentMaterialData();
        return MaterialData { TranslucentMaterialData {
          preloadTexture(preload.normalTexture),
          preloadTexture(preload.transmittanceTexture),
          preloadTexture(preload.emissiveTexture),
          src.getRefractiveIndex(),
          src.getTransmittanceColor(),
          src.getTransmittanceMeasurementDistance(),
          src.getEnableEmission(),
          src.getEmissiveIntensity(),
          src.getEmissiveColorConstant(),
          src.getSpriteSheetRows(),
          src.getSpriteSheetCols(),
          src.getSpriteSheetFPS(),
          src.getEnableThinWalled(),
          src.getThinWallThickness(),
          src.getEnableDiffuseLayer(),
          src.getFilterMode(),
          src.getWrapModeU(),
          src.getWrapModeV()
        } };
      }
      case MaterialDataType::RayPortal:
      {
        const auto& src = materialWithoutPreload.getRayPortalMaterialData();
        return MaterialData { RayPortalMaterialData {
          preloadTexture(preload.emissiveTexture),
          preloadTexture(preload.albedoTexture), // portal mask
          src.getRayPortalIndex(),
          src.getSpriteSheetRows(),
          src.getSpriteSheetCols(),
          src.getSpriteSheetFPS(),
          src.getRotationSpeed(),
          src.getEnableEmission(),
          src.getEmissiveIntensity(),
          src.getFilterMode(),
          src.getWrapModeU(),
          src.getWrapModeV()
        } };
      }
      default: assert(0); return materialWithoutPreload;
      }
    }

    MaterialData toRtMaterialWithoutTexturePreload(const remixapi_MaterialInfo& info) {
      if (auto extOpaque = pnext::find<remixapi_MaterialInfoOpaqueEXT>(&info)) {
        auto extSubsurface = pnext::find<remixapi_MaterialInfoOpaqueSubsurfaceEXT>(&info);
        return MaterialData { OpaqueMaterialData {
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          {},
          extOpaque->anisotropy,
          info.emissiveIntensity,
          tovec3(extOpaque->albedoConstant),
          extOpaque->opacityConstant,
          extOpaque->roughnessConstant,
          extOpaque->metallicConstant,
          tovec3(info.emissiveColorConstant),
          info.emissiveIntensity > 0.f,
          info.spriteSheetRow,
          info.spriteSheetCol,
          info.spriteSheetFps,
          tobool(extOpaque->thinFilmThickness_hasvalue),
          tobool(extOpaque->alphaIsThinFilmThickness),
          extOpaque->thinFilmThickness_hasvalue ? extOpaque->thinFilmThickness_value : 200.f, // default OpaqueMaterial::ThinFilmThicknessConstant
          tobool(extOpaque->useDrawCallAlphaState), // OpaqueMaterial::UseLegacyAlphaState
          tobool(extOpaque->blendType_hasvalue),
          extOpaque->blendType_hasvalue ? static_cast<BlendType>(extOpaque->blendType_value) : BlendType::kAlpha,  // default OpaqueMaterial::BlendType
          tobool(extOpaque->invertedBlend),
          static_cast<AlphaTestType>(extOpaque->alphaTestType),
          extOpaque->alphaReferenceValue,
          extOpaque->displaceIn,
          (s_apiVersion >= REMIXAPI_VERSION_MAKE(0, 4, 2)) ? extOpaque->displaceOut : 0.f,
          extSubsurface ? tovec3(extSubsurface->subsurfaceTransmittanceColor) : Vector3{ 0.5f, 0.5f, 0.5f },
          extSubsurface ? extSubsurface->subsurfaceMeasurementDistance : 0.f,
          extSubsurface ? tovec3(extSubsurface->subsurfaceSingleScatteringAlbedo) : Vector3{ 0.5f, 0.5f, 0.5f },
          extSubsurface ? extSubsurface->subsurfaceVolumetricAnisotropy : 0.f,
          extSubsurface ? static_cast<bool>(extSubsurface->subsurfaceDiffusionProfile) : false,
          extSubsurface ? tovec3(extSubsurface->subsurfaceRadius) : Vector3{ 0.5f, 0.5f, 0.5f },
          extSubsurface ? extSubsurface->subsurfaceRadiusScale : 0.f,
          extSubsurface ? extSubsurface->subsurfaceMaxSampleRadius : 0.f,
          info.filterMode,
          info.wrapModeU,
          info.wrapModeV,
        } };
      }
      if (auto extTranslucent = pnext::find<remixapi_MaterialInfoTranslucentEXT>(&info)) {
        return MaterialData { TranslucentMaterialData {
          {},
          {},
          {},
          extTranslucent->refractiveIndex,
          tovec3(extTranslucent->transmittanceColor),
          extTranslucent->transmittanceMeasurementDistance,
          info.emissiveIntensity > 0.f,
          info.emissiveIntensity,
          tovec3(info.emissiveColorConstant),
          info.spriteSheetRow,
          info.spriteSheetCol,
          info.spriteSheetFps,
          tobool(extTranslucent->thinWallThickness_hasvalue),
          extTranslucent->thinWallThickness_hasvalue ? extTranslucent->thinWallThickness_value : 0.001f, // default TranslucentMaterial::ThinWallThickness
          tobool(extTranslucent->useDiffuseLayer),
          info.filterMode,
          info.wrapModeU,
          info.wrapModeV,
        } };
      }
      if (auto extPortal = pnext::find<remixapi_MaterialInfoPortalEXT>(&info)) {
        return MaterialData { RayPortalMaterialData {
          {},
          {}, // unused
          extPortal->rayPortalIndex,
          info.spriteSheetRow,
          info.spriteSheetCol,
          info.spriteSheetFps,
          extPortal->rotationSpeed,
          info.emissiveIntensity > 0.f,
          info.emissiveIntensity,
          info.filterMode,
          info.wrapModeU,
          info.wrapModeV,
        } };
      }

      assert(0);
      return MaterialData { OpaqueMaterialData {} };
    }

    // --
    CameraType::Enum toRtCameraType(remixapi_CameraType from) {
      switch (from) {
      case REMIXAPI_CAMERA_TYPE_WORLD: return CameraType::Main;
      case REMIXAPI_CAMERA_TYPE_VIEW_MODEL: return CameraType::ViewModel;
      case REMIXAPI_CAMERA_TYPE_SKY: return CameraType::Sky;
      default: assert(0); return CameraType::Main;
      }
    }

    struct ExternalCameraInfo {
      CameraType::Enum type {};
      Matrix4 worldToView {};
      Matrix4 viewToProjection {};
    };

    ExternalCameraInfo toRtCamera(const remixapi_CameraInfo& info) {
      if (auto params = pnext::find<remixapi_CameraInfoParameterizedEXT>(&info)) {
        auto result = ExternalCameraInfo {
          toRtCameraType(info.type),
        };
        {
          const auto newViewToWorld = Matrix4d {
           Vector4d{ normalize(tovec3d(params->right)), 0.0 },
           Vector4d{ normalize(tovec3d(params->up)), 0.0 },
           Vector4d{ normalize(tovec3d(params->forward)), 0.0 },
           Vector4d{ tovec3d(params->position), 1.0 },
          };
          result.worldToView = inverse(newViewToWorld);
        }
        {
          constexpr bool isLhs = true;
          auto proj = float4x4 {};
          proj.SetupByHalfFovy(
            DegToRad(params->fovYInDegrees) / 2,
            params->aspect,
            params->nearPlane,
            params->farPlane,
            isLhs ? PROJ_LEFT_HANDED : 0);
          static_assert(sizeof result.viewToProjection == sizeof proj);
          memcpy(&result.viewToProjection, &proj, sizeof float4x4);
        }
        return result;
      }
      return ExternalCameraInfo {
        toRtCameraType(info.type),
        Matrix4 { info.view },
        Matrix4 { info.projection },
      };
    }

    // --

    std::optional<RtLightShaping> toRtLightShaping(const remixapi_LightInfoLightShaping* info) {
      if (info) {
        return RtLightShaping::tryCreate(
          true,
          tovec3(info->direction),
          std::cos(DegToRad(info->coneAngleDegrees)),
          info->coneSoftness,
          info->focusExponent
        );
      }

      // Note: Default constructed Light Shaping returned when no info is provided to have a valid but disabled
      // Light Shaping object (different from returning an empty optional here, which means creation of a Light
      // Shaping failed).
      return RtLightShaping{};
    }

    std::optional<RtLight> toRtLight(const remixapi_LightInfo& info) {
      if (auto src = pnext::find<remixapi_LightInfoUSDEXT>(&info)) {
        if (auto lightData = LightData::tryCreate(*src)) {
          return lightData->toRtLight();
        }
        return {};
      }
      if (auto src = pnext::find<remixapi_LightInfoSphereEXT>(&info)) {
        const auto shaping = toRtLightShaping(src->shaping_hasvalue ? &src->shaping_value : nullptr);

        if (!shaping.has_value()) {
          return {};
        }

        return RtSphereLight::tryCreate(
          tovec3(src->position),
          tovec3(info.radiance),
          src->radius,
          *shaping,
          src->volumetricRadianceScale
        );
      }
      if (auto src = pnext::find<remixapi_LightInfoRectEXT>(&info)) {
        const auto shaping = toRtLightShaping(src->shaping_hasvalue ? &src->shaping_value : nullptr);

        if (!shaping.has_value()) {
          return {};
        }

        return RtRectLight::tryCreate(
          tovec3(src->position),
          Vector2{src->xSize, src->ySize},
          tovec3(src->xAxis),
          tovec3(src->yAxis),
          tovec3(src->direction),
          tovec3(info.radiance),
          *shaping,
          src->volumetricRadianceScale
        );
      }
      if (auto src = pnext::find<remixapi_LightInfoDiskEXT>(&info)) {
        const auto shaping = toRtLightShaping(src->shaping_hasvalue ? &src->shaping_value : nullptr);

        if (!shaping.has_value()) {
          return {};
        }

        return RtDiskLight::tryCreate(
          tovec3(src->position),
          Vector2{src->xRadius, src->yRadius},
          tovec3(src->xAxis),
          tovec3(src->yAxis),
          tovec3(src->direction),
          tovec3(info.radiance),
          *shaping,
          src->volumetricRadianceScale
        );
      }
      if (auto src = pnext::find<remixapi_LightInfoCylinderEXT>(&info)) {
        return RtCylinderLight::tryCreate(
          tovec3(src->position),
          src->radius,
          tovec3(src->axis),
          src->axisLength,
          tovec3(info.radiance),
          src->volumetricRadianceScale
        );
      }
      if (auto src = pnext::find<remixapi_LightInfoDistantEXT>(&info)) {
        return RtDistantLight::tryCreate(
          tovec3(src->direction),
          DegToRad(src->angularDiameterDegrees * 0.5f),
          tovec3(info.radiance),
          src->volumetricRadianceScale
        );
      }

      // Note: Return an empty optional if the LightInfo struct does not contain a supported
      // LightInfo extension struct.
      return {};
    }

    // --

    CameraType::Enum categoryToCameraType(remixapi_InstanceCategoryFlags flags) {
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_SKY) {
        return CameraType::Sky;
      }
      return CameraType::Main;
    }

    CategoryFlags toRtCategories(remixapi_InstanceCategoryFlags flags) {
      CategoryFlags result { 0 };
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_UI                 ){ result.set(InstanceCategories::WorldUI               ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_MATTE              ){ result.set(InstanceCategories::WorldMatte            ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_SKY                      ){ result.set(InstanceCategories::Sky                   ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE                   ){ result.set(InstanceCategories::Ignore                ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_LIGHTS            ){ result.set(InstanceCategories::IgnoreLights          ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_ANTI_CULLING      ){ result.set(InstanceCategories::IgnoreAntiCulling     ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_MOTION_BLUR       ){ result.set(InstanceCategories::IgnoreMotionBlur      ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_OPACITY_MICROMAP  ){ result.set(InstanceCategories::IgnoreOpacityMicromap ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_ALPHA_CHANNEL     ){ result.set(InstanceCategories::IgnoreAlphaChannel    ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_HIDDEN                   ){ result.set(InstanceCategories::Hidden                ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE                 ){ result.set(InstanceCategories::Particle              ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_BEAM                     ){ result.set(InstanceCategories::Beam                  ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_STATIC             ){ result.set(InstanceCategories::DecalStatic           ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_DYNAMIC            ){ result.set(InstanceCategories::DecalDynamic          ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_SINGLE_OFFSET      ){ result.set(InstanceCategories::DecalSingleOffset     ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_NO_OFFSET          ){ result.set(InstanceCategories::DecalNoOffset         ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_ALPHA_BLEND_TO_CUTOUT    ){ result.set(InstanceCategories::AlphaBlendToCutout    ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_TERRAIN                  ){ result.set(InstanceCategories::Terrain               ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_ANIMATED_WATER           ){ result.set(InstanceCategories::AnimatedWater         ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_MODEL){ result.set(InstanceCategories::ThirdPersonPlayerModel); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_BODY ){ result.set(InstanceCategories::ThirdPersonPlayerBody ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_BAKED_LIGHTING    ){ result.set(InstanceCategories::IgnoreBakedLighting   ); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_TRANSPARENCY_LAYER){ result.set(InstanceCategories::IgnoreTransparencyLayer); }
      if (flags & REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE_EMITTER)         { result.set(InstanceCategories::ParticleEmitter); }
      
      static_assert((int)InstanceCategories::Count == 24, "Instance categories changed, please update Remix SDK");
      return result;
    }

    RtxParticleSystemDesc toRtParticleDesc(const remixapi_InstanceInfoParticleSystemEXT& info) {
      RtxParticleSystemDesc desc {};

      // Lifetimes
      desc.minTtl = info.minTimeToLive;
      desc.maxTtl = info.maxTimeToLive;

      // Initial 
      desc.spawnRate = info.spawnRatePerSecond;
      desc.initialVelocityFromMotion = info.initialVelocityFromMotion;
      desc.initialVelocityFromNormal = info.initialVelocityFromNormal;
      desc.initialVelocityConeAngleDegrees = info.initialVelocityConeAngleDegrees;
      desc.gravityForce = info.gravityForce;
      desc.maxSpeed = info.maxSpeed;
      desc.motionTrailMultiplier = info.motionTrailMultiplier;

      // Turbulence
      desc.turbulenceFrequency = info.turbulenceFrequency;
      desc.turbulenceForce = info.turbulenceForce;

      // Spawn
      desc.minSpawnRotationSpeed = info.minSpawnRotationSpeed;
      desc.maxSpawnRotationSpeed = info.maxSpawnRotationSpeed;
      desc.minSpawnSize = info.minSpawnSize;
      desc.maxSpawnSize = info.maxSpawnSize;
      desc.minSpawnColor = tovec4(info.minSpawnColor);
      desc.maxSpawnColor = tovec4(info.maxSpawnColor);

      // Target
      desc.minTargetRotationSpeed = info.minTargetRotationSpeed;
      desc.maxTargetRotationSpeed = info.maxTargetRotationSpeed;
      desc.minTargetSize = info.minTargetSize;
      desc.maxTargetSize = info.maxTargetSize;
      desc.minTargetColor = tovec4(info.minTargetColor);
      desc.maxTargetColor = tovec4(info.maxTargetColor);

      // Collision
      desc.collisionThickness = info.collisionThickness;
      desc.collisionRestitution = info.collisionRestitution;

      // Counts/flags
      desc.maxNumParticles = info.maxNumParticles;
      desc.useTurbulence = static_cast<uint8_t>(info.useTurbulence);
      desc.alignParticlesToVelocity = static_cast<uint8_t>(info.alignParticlesToVelocity);
      desc.useSpawnTexcoords = static_cast<uint8_t>(info.useSpawnTexcoords);
      desc.enableCollisionDetection = static_cast<uint8_t>(info.enableCollisionDetection);
      desc.enableMotionTrail = static_cast<uint>(info.enableMotionTrail);
      desc.hideEmitter = static_cast<uint>(info.hideEmitter);
      desc.billboardType = static_cast<ParticleBillboardType>(info.billboardType);

      // If this assert fails a new particle system parameter added, please update here.
      assert(pxr::RemixParticleSystemAPI::GetSchemaAttributeNames(false).size() == 33);

      return desc;
    }

    ExternalDrawState toRtDrawState(const remixapi_InstanceInfo& info) {
      return RemixAPIPrivateAccessor::toRtDrawState(info);
    }
  }
}

dxvk::ExternalDrawState dxvk::RemixAPIPrivateAccessor::toRtDrawState(const remixapi_InstanceInfo& info)
{
  auto prototype = DrawCallState {};
  {
    prototype.cameraType = CameraType::Main;
    prototype.transformData.objectToWorld = convert::tomat4(info.transform);
    prototype.transformData.textureTransform = Matrix4 {};
    prototype.transformData.texgenMode = TexGenMode::None;
    prototype.materialData.colorTextures[0] = TextureRef {};
    prototype.materialData.colorTextures[1] = TextureRef {};
    prototype.categories = convert::toRtCategories(info.categoryFlags);
  }

  if (auto objectPicking = pnext::find<remixapi_InstanceInfoObjectPickingEXT>(&info)) {
    prototype.drawCallID = objectPicking->objectPickingValue;
  }

  if (auto extBones = pnext::find<remixapi_InstanceInfoBoneTransformsEXT>(&info)) {
    const uint32_t boneCount =
      extBones->boneTransforms_count < REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT ?
      extBones->boneTransforms_count : REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT;
    prototype.skinningData.minBoneIndex = 0;
    prototype.skinningData.numBones = boneCount;
    prototype.skinningData.numBonesPerVertex = prototype.geometryData.numBonesPerVertex;
    prototype.skinningData.pBoneMatrices.resize(boneCount);
    for (uint32_t boneIdx = 0; boneIdx < boneCount; boneIdx++) {
      prototype.skinningData.pBoneMatrices[boneIdx] = convert::tomat4(extBones->boneTransforms_values[boneIdx]);
    }
  }

  if (auto extBlend = pnext::find<remixapi_InstanceInfoBlendEXT>(&info)) {
    prototype.materialData.alphaTestEnabled = extBlend->alphaTestEnabled;
    prototype.materialData.alphaTestReferenceValue = extBlend->alphaTestReferenceValue;
    prototype.materialData.alphaTestCompareOp = (VkCompareOp) extBlend->alphaTestCompareOp;
    prototype.materialData.blendMode.enableBlending = extBlend->alphaBlendEnabled;
    prototype.materialData.textureColorOperation = (DxvkRtTextureOperation) extBlend->textureColorOperation;
    prototype.materialData.textureColorArg1Source = (RtTextureArgSource) extBlend->textureColorArg1Source;
    prototype.materialData.textureColorArg2Source = (RtTextureArgSource) extBlend->textureColorArg2Source;
    prototype.materialData.textureAlphaOperation = (DxvkRtTextureOperation) extBlend->textureAlphaOperation;
    prototype.materialData.textureAlphaArg1Source = (RtTextureArgSource) extBlend->textureAlphaArg1Source;
    prototype.materialData.textureAlphaArg2Source = (RtTextureArgSource) extBlend->textureAlphaArg2Source;
    prototype.materialData.tFactor = extBlend->tFactor;
    prototype.materialData.isTextureFactorBlend = extBlend->isTextureFactorBlend;
    prototype.materialData.isVertexColorBakedLighting = (s_apiVersion >= REMIXAPI_VERSION_MAKE(0, 5, 2)) ? extBlend->isVertexColorBakedLighting : RtxOptions::vertexColorIsBakedLighting();
    prototype.materialData.blendMode.colorSrcFactor = (VkBlendFactor) extBlend->srcColorBlendFactor;
    prototype.materialData.blendMode.colorDstFactor = (VkBlendFactor) extBlend->dstColorBlendFactor;
    prototype.materialData.blendMode.colorBlendOp = (VkBlendOp) extBlend->colorBlendOp;
    prototype.materialData.blendMode.alphaSrcFactor = (VkBlendFactor) extBlend->srcAlphaBlendFactor;
    prototype.materialData.blendMode.alphaDstFactor = (VkBlendFactor) extBlend->dstAlphaBlendFactor;
    prototype.materialData.blendMode.alphaBlendOp = (VkBlendOp) extBlend->alphaBlendOp;
    prototype.materialData.blendMode.writeMask = (VkColorComponentFlags) extBlend->writeMask;
  }

  std::optional<RtxParticleSystemDesc> optParticles;
  if (auto extParticles = pnext::find<remixapi_InstanceInfoParticleSystemEXT>(&info)) {
    optParticles.emplace(convert::toRtParticleDesc(*extParticles));
  }

  return ExternalDrawState {
    prototype,
    info.mesh,
    convert::categoryToCameraType(info.categoryFlags),
    convert::toRtCategories(info.categoryFlags),
    convert::tobool(info.doubleSided),
    optParticles
  };
}

namespace {
  remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMaterial(
    const remixapi_MaterialInfo* info,
    remixapi_MaterialHandle* out_handle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!out_handle || !info || info->sType != REMIXAPI_STRUCT_TYPE_MATERIAL_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    static_assert(sizeof(remixapi_MaterialHandle) == sizeof(info->hash));
    auto handle = reinterpret_cast<remixapi_MaterialHandle>(info->hash);
    if (!handle) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    // async load
    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([cHandle = handle,
                         cMaterialData = convert::toRtMaterialWithoutTexturePreload(*info),
                         cPreloadSrc = convert::makePreloadSource(*info)](dxvk::DxvkContext* ctx) {
      auto& assets = ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
      assets->makeMaterialWithTexturePreload(
        *ctx,
        cHandle,
        convert::toRtMaterialFinalized(*ctx, cMaterialData, cPreloadSrc));
    });

    *out_handle = handle;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMaterial(
    remixapi_MaterialHandle handle) {
    if (auto remixDevice = tryAsDxvk()) {
      std::lock_guard lock { s_mutex };
      remixDevice->EmitCs([cHandle = handle](dxvk::DxvkContext* ctx) {
        auto& assets = ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
        assets->destroyExternalMaterial(cHandle);
      });
      return REMIXAPI_ERROR_CODE_SUCCESS;
    }
    return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateMesh(
    const remixapi_MeshInfo* info,
    remixapi_MeshHandle* out_handle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!out_handle || !info || info->sType != REMIXAPI_STRUCT_TYPE_MESH_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    static_assert(sizeof(remixapi_MeshHandle) == sizeof(info->hash));
    auto handle = reinterpret_cast<remixapi_MeshHandle>(info->hash);
    if (!handle) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    auto allocatedSurfaces = std::vector<dxvk::RasterGeometry> {};

    for (size_t i = 0; i < info->surfaces_count; i++) {
      const remixapi_MeshInfoSurfaceTriangles& src = info->surfaces_values[i];

      const size_t vertexDataSize = sizeInBytes(src.vertices_values, src.vertices_count);
      const size_t indexDataSize = sizeInBytes(src.indices_values, src.indices_count);

      auto allocBuffer = [](dxvk::D3D9DeviceEx* device, size_t sizeInBytes) -> dxvk::Rc<dxvk::DxvkBuffer> {
        if (sizeInBytes == 0) {
          return {};
        }
        auto bufferInfo = dxvk::DxvkBufferCreateInfo {};
        {
          bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
          bufferInfo.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
          bufferInfo.access = VK_ACCESS_TRANSFER_WRITE_BIT;
          bufferInfo.size = dxvk::align(sizeInBytes, dxvk::CACHE_LINE_SIZE);
        }
        return device->GetDXVKDevice()->createBuffer(
            bufferInfo,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            dxvk::DxvkMemoryStats::Category::RTXBuffer,
           "Remix API mesh buffer");
      };

      dxvk::Rc<dxvk::DxvkBuffer> vertexBuffer = allocBuffer(remixDevice, vertexDataSize);
      dxvk::Rc<dxvk::DxvkBuffer> indexBuffer = allocBuffer(remixDevice, indexDataSize);
      dxvk::Rc<dxvk::DxvkBuffer> skinningBuffer = nullptr;

      auto vertexSlice = dxvk::DxvkBufferSlice { vertexBuffer };
      memcpy(vertexSlice.mapPtr(0), src.vertices_values, vertexDataSize);

      auto indexSlice = dxvk::DxvkBufferSlice {};
      if (indexDataSize > 0) {
        indexSlice = dxvk::DxvkBufferSlice { indexBuffer };
        memcpy(indexSlice.mapPtr(0), src.indices_values, indexDataSize);
      }

      auto blendWeightsSlice = dxvk::DxvkBufferSlice {};
      auto blendIndicesSlice = dxvk::DxvkBufferSlice {};
      if (src.skinning_hasvalue) {
        size_t wordsPerCompressedTuple = dxvk::divCeil(src.skinning_value.bonesPerVertex, 4u);
        size_t sizeInBytes_weights = sizeInBytes(src.skinning_value.blendWeights_values, src.skinning_value.blendWeights_count);
        size_t sizeInBytes_indices = src.vertices_count * wordsPerCompressedTuple * sizeof(uint32_t);

        skinningBuffer = allocBuffer(remixDevice, sizeInBytes_weights + sizeInBytes_indices);

        // Encode bone indices into compressed byte form
        auto compressedBlendIndices = std::vector<uint32_t> {};
        compressedBlendIndices.resize(src.vertices_count * wordsPerCompressedTuple);
        for (size_t vert = 0; vert < src.vertices_count; vert++) {
          const uint32_t* dstCompressed = &compressedBlendIndices[vert * wordsPerCompressedTuple];
          const uint32_t* blendIndicesStorage = &src.skinning_value.blendIndices_values[vert * src.skinning_value.bonesPerVertex];

          for (int j = 0; j < src.skinning_value.bonesPerVertex; j += 4) {
            uint32_t vertIndices = 0;
            for (int k = 0; k < 4 && j + k < src.skinning_value.bonesPerVertex; ++k) {
              vertIndices |= blendIndicesStorage[j + k] << 8 * k;
            }
            *(uint32_t*) &dstCompressed[j / 4] = vertIndices;
          }
        }

        assert(sizeInBytes_indices == compressedBlendIndices.size() * sizeof(compressedBlendIndices[0]));

        blendWeightsSlice = dxvk::DxvkBufferSlice { skinningBuffer, 0, sizeInBytes_weights };
        blendIndicesSlice = dxvk::DxvkBufferSlice { skinningBuffer, sizeInBytes_weights, sizeInBytes_indices };

        memcpy(blendWeightsSlice.mapPtr(0), src.skinning_value.blendWeights_values, sizeInBytes_weights);
        memcpy(blendIndicesSlice.mapPtr(0), compressedBlendIndices.data(), sizeInBytes_indices);
      }

      auto dst = dxvk::RasterGeometry {};
      {
        dst.externalMaterial = src.material;
        dst.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        dst.cullMode = VK_CULL_MODE_NONE; // this will be overwritten by the instance info at draw time
        dst.frontFace = VK_FRONT_FACE_CLOCKWISE;
        dst.vertexCount = src.vertices_count; assert(src.vertices_count < std::numeric_limits<uint32_t>::max());
        dst.positionBuffer = dxvk::RasterBuffer { vertexSlice, offsetof(remixapi_HardcodedVertex, position), sizeof(remixapi_HardcodedVertex), VK_FORMAT_R32G32B32_SFLOAT };
        dst.normalBuffer = dxvk::RasterBuffer { vertexSlice, offsetof(remixapi_HardcodedVertex, normal), sizeof(remixapi_HardcodedVertex), VK_FORMAT_R32G32B32_SFLOAT };
        dst.texcoordBuffer = dxvk::RasterBuffer { vertexSlice, offsetof(remixapi_HardcodedVertex, texcoord), sizeof(remixapi_HardcodedVertex), VK_FORMAT_R32G32_SFLOAT };
        dst.color0Buffer = dxvk::RasterBuffer { vertexSlice, offsetof(remixapi_HardcodedVertex, color), sizeof(remixapi_HardcodedVertex), VK_FORMAT_B8G8R8A8_UNORM };
        if (src.skinning_hasvalue) {
          dst.numBonesPerVertex = src.skinning_value.bonesPerVertex;
          dst.blendWeightBuffer = dxvk::RasterBuffer { blendWeightsSlice, 0, sizeof(float), VK_FORMAT_R32_SFLOAT };;
          dst.blendIndicesBuffer = dxvk::RasterBuffer { blendIndicesSlice, 0, sizeof(uint32_t), VK_FORMAT_R8G8B8A8_USCALED };
        }

        dst.indexCount = src.indices_count;
        static_assert(sizeof(src.indices_values[0]) == 4);
        dst.indexBuffer = dxvk::RasterBuffer { indexSlice, 0, sizeof(uint32_t), VK_INDEX_TYPE_UINT32 };
        // look comments in UsdMod::Impl::processMesh, rtx_mod_usd.cpp
        dst.hashes[dxvk::HashComponents::Indices] = dst.hashes[dxvk::HashComponents::VertexPosition] = hack_getNextGeomHash();
        dst.hashes[dxvk::HashComponents::VertexTexcoord] = hack_getNextGeomHash();
        dst.hashes[dxvk::HashComponents::GeometryDescriptor] = hack_getNextGeomHash();
        dst.hashes[dxvk::HashComponents::VertexLayout] = hack_getNextGeomHash();
        dst.hashes.precombine();
      }
      allocatedSurfaces.push_back(std::move(dst));
    }
    std::lock_guard lock { s_mutex };

    remixDevice->EmitCs([cHandle = handle, cSurfaces = std::move(allocatedSurfaces)](dxvk::DxvkContext* ctx) mutable {
      auto& assets = ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
      assets->registerExternalMesh(cHandle, std::move(cSurfaces));
    });

    *out_handle = handle;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyMesh(
    remixapi_MeshHandle handle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([cHandle = handle](dxvk::DxvkContext* ctx) {
      auto& assets = ctx->getCommonObjects()->getSceneManager().getAssetReplacer();
      assets->destroyExternalMesh(cHandle);
    });
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_SetupCamera(
    const remixapi_CameraInfo* info) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!info || info->sType != REMIXAPI_STRUCT_TYPE_CAMERA_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    std::lock_guard lock { s_mutex };
    // ensure that near plane is not modified, to keep the projection matrix
    // exactly as the client provided, so depth buffers would have expected results,
    // for a client to be able to reproject to world space using the projection matrices
    if (dxvk::RtxOptions::enableNearPlaneOverride()) {
      assert(0);
      const_cast<bool&>(dxvk::RtxOptions::enableNearPlaneOverride()) = false;
    }
    remixDevice->EmitCs([cRtCamera = convert::toRtCamera(*info)](dxvk::DxvkContext* ctx) {
      ctx->getCommonObjects()->getSceneManager().getCameraManager()
        .processExternalCamera(cRtCamera.type, cRtCamera.worldToView, cRtCamera.viewToProjection);
    });
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawInstance(
    const remixapi_InstanceInfo* info) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([cRtDrawState = convert::toRtDrawState(*info)](dxvk::DxvkContext* dxvkCtx) mutable {
      auto* ctx = static_cast<dxvk::RtxContext*>(dxvkCtx);
      ctx->commitExternalGeometryToRT(std::move(cRtDrawState));
    });
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_CreateLight(
    const remixapi_LightInfo* info,
    remixapi_LightHandle* out_handle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!out_handle || !info || info->sType != REMIXAPI_STRUCT_TYPE_LIGHT_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    static_assert(sizeof(remixapi_LightHandle) == sizeof(info->hash));
    auto handle = reinterpret_cast<remixapi_LightHandle>(info->hash);
    if (!handle) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    // async load
    std::lock_guard lock { s_mutex };
    if (auto src = pnext::find<remixapi_LightInfoDomeEXT>(info)) {
      // Special case for dome lights
      remixDevice->EmitCs([cHandle = handle, 
                          cRadiance = convert::tovec3(info->radiance), 
                          cTransform = convert::tomat4(src->transform), 
                          cTexturePath = convert::topath(src->colorTexture)]
                          (dxvk::DxvkContext* ctx) {
        auto preloadTexture = [&ctx](const std::filesystem::path& path)->dxvk::TextureRef {
          if (path.empty()) {
            return {};
          }
          auto assetData = dxvk::AssetDataManager::get().findAsset(path.string().c_str());
          if (assetData == nullptr) {
            return {};
          }
          auto uploadedTexture = ctx->getCommonObjects()->getTextureManager()
            .preloadTextureAsset(assetData, dxvk::ColorSpace::AUTO, true);
          return dxvk::TextureRef { uploadedTexture };
        };

        dxvk::DomeLight domeLight;
        domeLight.radiance = cRadiance;
        domeLight.worldToLight = inverse(cTransform);
        domeLight.texture = preloadTexture(cTexturePath);

        // Ensures a texture stays in VidMem
        uint32_t unused;
        ctx->getCommonObjects()->getSceneManager().trackTexture(domeLight.texture, unused, true, true);

        auto& lightMgr = ctx->getCommonObjects()->getSceneManager().getLightManager();
        lightMgr.addExternalDomeLight(cHandle, domeLight);
      });
    } else {
      // Regular analytical light handling
      const auto rtLight = convert::toRtLight(*info);

      // Note: If the toRtLight conversion process returns an empty optional, the specified LightInfo did
      // not contain the proper arguments to create a light with.
      if (!rtLight.has_value()) {
        return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
      }

      remixDevice->EmitCs([cHandle = handle, cRtLight = *rtLight](dxvk::DxvkContext* ctx) {
        auto& lightMgr = ctx->getCommonObjects()->getSceneManager().getLightManager();
        lightMgr.addExternalLight(cHandle, cRtLight);
      });
    }

    *out_handle = handle;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_DestroyLight(
    remixapi_LightHandle handle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([cHandle = handle](dxvk::DxvkContext* ctx) {
      auto& lightMgr = ctx->getCommonObjects()->getSceneManager().getLightManager();
      lightMgr.removeExternalLight(cHandle);
    });
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }


  remixapi_ErrorCode REMIXAPI_CALL remixapi_DrawLightInstance(
    remixapi_LightHandle lightHandle) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!lightHandle) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    // async load
    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([lightHandle](dxvk::DxvkContext* ctx) {
      auto& lightMgr = ctx->getCommonObjects()->getSceneManager().getLightManager();
      lightMgr.addExternalLightInstance(lightHandle);
    });

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_SetConfigVariable(
    const char* key,
    const char* value) {
    std::lock_guard lock { s_mutex };

    if (!key || key[0] == '\0' || !value) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    std::string strKey = std::string{ key };

    const auto& globalRtxOptions = dxvk::RtxOptionImpl::getGlobalRtxOptionMap();
    const XXH64_hash_t optionHash = dxvk::StringToXXH64(strKey, 0);
    auto found = globalRtxOptions.find(optionHash);
    if (found == globalRtxOptions.end()) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    dxvk::Config newSetting;
    newSetting.setOptionMove(std::move(strKey), std::string{ value });
    found->second->readOption(newSetting, dxvk::RtxOptionImpl::ValueType::PendingValue);

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_pick_RequestObjectPicking(
    const remixapi_Rect2D* pixelRegion,
    PFN_remixapi_pick_RequestObjectPickingUserCallback callback,
    void* callbackUserData) {
    if (!pixelRegion || !callback) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    std::lock_guard lock { s_mutex };

    dxvk::ObjectPicking& picking = remixDevice->GetDXVKDevice()->getCommon()
      ->metaDebugView().ObjectPicking;

    picking.request(
      dxvk::Vector2i{ pixelRegion->left, pixelRegion->top },
      dxvk::Vector2i{ pixelRegion->right, pixelRegion->bottom },
      // invoke user's callback on result
      [callback, callbackUserData](std::vector<dxvk::ObjectPickingValue>&& objectPickingValues, std::optional<XXH64_hash_t>) {
        callback(objectPickingValues.data(), uint32_t(objectPickingValues.size()), callbackUserData);
      }
    );
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_pick_HighlightObjects(
    const uint32_t* objectPickingValues_values,
    uint32_t objectPickingValues_count,
    uint8_t colorR,
    uint8_t colorG,
    uint8_t colorB) {
    if (!objectPickingValues_values) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    std::lock_guard lock { s_mutex };

    if (objectPickingValues_count > 0) {
      dxvk::g_customHighlightColor = { colorR, colorG, colorB };

      const auto frameId = remixDevice->GetDXVKDevice()->getCurrentFrameId();
      const auto values = std::vector< dxvk::ObjectPickingValue > {
        objectPickingValues_values,
        objectPickingValues_values + objectPickingValues_count,
      };
      remixDevice->GetDXVKDevice()->getCommon()->metaDebugView().Highlighting
        .requestHighlighting(
          &values,
          dxvk::HighlightColor::FromVariable,
          frameId);   // thread-safe
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_CreateD3D9(
    const remixapi_StartupInfo& info,
    IDirect3D9Ex** out_pD3D9) {
    IDirect3D9Ex* d3d9ex = nullptr;

    auto hr = dxvk::CreateD3D9(true, &d3d9ex, info.forceNoVkSwapchain, false, true);
    if (FAILED(hr) || !d3d9ex) {
      if (isHResultAliasedWithRemixErrorCode(hr)) {
        return static_cast<remixapi_ErrorCode>(hr);
      }
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    dxvk::g_allowSrgbConversionForOutput = !info.disableSrgbConversionForOutput;
    dxvk::g_allowMappingLegacyHashToObjectPickingValue = !info.editorModeEnabled;

    // slightly different initial settings for HdRemix
    if (info.editorModeEnabled) {
      const_cast<dxvk::LightManager::FallbackLightMode&>(dxvk::LightManager::fallbackLightMode()) = dxvk::LightManager::FallbackLightMode::Never;
      const_cast<bool&>(dxvk::DxvkPostFx::desaturateOthersOnHighlight()) = false;
    }

    *out_pD3D9 = d3d9ex;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  // HdRemix has editorModeEnabled=true
  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_CreateD3D9_legacy(
    remixapi_Bool editorModeEnabled,
    IDirect3D9Ex** out_pD3D9) {
    auto i = remixapi_StartupInfo {};
    {
      i.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
      i.disableSrgbConversionForOutput = editorModeEnabled;
      i.forceNoVkSwapchain = editorModeEnabled;
      i.editorModeEnabled = editorModeEnabled;
      static_assert(sizeof(remixapi_StartupInfo) == 40, "If changing, also set defaults here");
    }
    return remixapi_dxvk_CreateD3D9(i, out_pD3D9);
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_RegisterD3D9Device(
    IDirect3DDevice9Ex* d3d9Device) {
    if (!d3d9Device) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    auto dxvkDevice = dynamic_cast<dxvk::D3D9DeviceEx*>(d3d9Device);
    if (!dxvkDevice) {
      return REMIXAPI_ERROR_CODE_REGISTERING_NON_REMIX_D3D9_DEVICE;
    }
    IDirect3D9* dxvkD3d9 = nullptr;
    HRESULT hr = dxvkDevice->GetDirect3D(&dxvkD3d9);
    if (FAILED(hr) || !dxvkD3d9) {
      assert(0);
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }
    auto dxvkD3d9Ex = dynamic_cast<IDirect3D9Ex*>(dxvkD3d9);
    if (!dxvkD3d9Ex) {
      assert(0);
      return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
    }
    // if D3D9 already exists, check that user-provided D3D9 corresponds to our s_dxvkD3D9
    if (s_dxvkD3D9) {
      assert(s_dxvkD3D9 == dxvkD3d9Ex);
    }
    s_dxvkD3D9 = dxvkD3d9Ex;
    s_dxvkDevice = dxvkDevice;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_GetExternalSwapchain(
    uint64_t* out_vkImage,
    uint64_t* out_vkSemaphoreRenderingDone,
    uint64_t* out_vkSemaphoreResumeSemaphore) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!out_vkImage || !out_vkSemaphoreRenderingDone || !out_vkSemaphoreResumeSemaphore) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (auto pres = remixDevice->GetExternalPresenter()) {
      *out_vkImage = reinterpret_cast<uint64_t>(pres->GetVkImage(0));
      *out_vkSemaphoreRenderingDone = reinterpret_cast<uint64_t>(pres->GetFrameCompleteVkSemaphore());
      *out_vkSemaphoreResumeSemaphore = reinterpret_cast<uint64_t>(pres->GetFrameResumeVkSemaphore());
      return REMIXAPI_ERROR_CODE_SUCCESS;
    }
    return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_GetVkImage(
    IDirect3DSurface9* source,
    uint64_t* out_vkImage) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!source || !out_vkImage) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    dxvk::D3D9Surface* surface = static_cast<dxvk::D3D9Surface*>(source);
    dxvk::D3D9CommonTexture* texInfo = surface ? surface->GetCommonTexture() : nullptr;
    if (texInfo) {
      *out_vkImage = reinterpret_cast<uint64_t>(texInfo->GetImage()->handle());
      return REMIXAPI_ERROR_CODE_SUCCESS;
    }
    return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_CopyRenderingOutput(
    IDirect3DSurface9* destination,
    remixapi_dxvk_CopyRenderingOutputType type) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    if (!destination) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    dxvk::D3D9Surface* destSurface = static_cast<dxvk::D3D9Surface*>(destination);
    dxvk::D3D9CommonTexture* destTexInfo = destSurface ? destSurface->GetCommonTexture() : nullptr;
    if (!destTexInfo) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    if (type == REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING) {
      // suppress resource clean up
      if (!dxvk::g_forceKeepObjectPickingImage) {
        dxvk::g_forceKeepObjectPickingImage = true;
        return REMIXAPI_ERROR_CODE_SUCCESS;
      }
    }

#pragma warning(push)
#pragma warning(error : 4061) // all switch cases must be handled explicitly

    switch (type) {
    case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR:
    case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_DEPTH:
    case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_NORMALS:
    case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING:
      break;
    default:
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([cDest = destTexInfo->GetImage(), type = type](dxvk::DxvkContext* dxvkCtx) {
      auto* ctx = static_cast<dxvk::RtxContext*>(dxvkCtx);

      dxvk::Resources& resourceManager = ctx->getCommonObjects()->getResources();
      const dxvk::Resources::RaytracingOutput& rtOutput = resourceManager.getRaytracingOutput();

      dxvk::Rc<dxvk::DxvkImage> srcImage = nullptr;
      switch (type) {
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR:
        srcImage = rtOutput.m_finalOutput.resource(dxvk::Resources::AccessType::Read).image;
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_DEPTH:
        srcImage = rtOutput.m_primaryDepth.image;
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_NORMALS:
        srcImage = rtOutput.m_primaryWorldShadingNormal.image;
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING:
        srcImage = rtOutput.m_primaryObjectPicking.image;
        break;
      default:
        assert(!"unexpected remixapi_dxvk_CopyRenderingOutputType value");
        return;
      }

      if (srcImage.ptr()) {
        dxvk::RtxContext::blitImageHelper(ctx, srcImage, cDest, VkFilter::VK_FILTER_NEAREST);
      }
    });

#pragma warning(pop)

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }


  remixapi_ErrorCode REMIXAPI_CALL remixapi_dxvk_SetDefaultOutput(
    remixapi_dxvk_CopyRenderingOutputType type, const remixapi_Float4D* color) {
    if (!color) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }

    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }

    std::lock_guard lock { s_mutex };
    remixDevice->EmitCs([type, cColor = *color](dxvk::DxvkContext* ctx) {
      dxvk::RtxGlobals& globals = ctx->getCommonObjects()->getSceneManager().getGlobals();
      switch (type) {
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR:
        globals.clearColorFinalColor = vec3(cColor.x, cColor.y, cColor.z);
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_DEPTH:
        globals.clearColorDepth = cColor.x;
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_NORMALS:
        globals.clearColorNormal = vec3(cColor.x, cColor.y, cColor.z);
        break;
      case REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING:
        // converting binary value of color.x into uint to avoid losing precision.
        globals.clearColorPicking = reinterpret_cast<const uint&>(cColor.x);
        break;
      default:
        break;
      }
    });
    
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_Startup(const remixapi_StartupInfo* info) {
    if (!info || info->sType != REMIXAPI_STRUCT_TYPE_STARTUP_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    assert(!!(s_dxvkD3D9) == !!(s_dxvkDevice));
    if (s_dxvkD3D9 || s_dxvkDevice) {
      return REMIXAPI_ERROR_CODE_ALREADY_EXISTS;
    }

    IDirect3D9Ex* d3d9 = nullptr;
    {
      remixapi_ErrorCode status = remixapi_dxvk_CreateD3D9(*info, &d3d9);
      if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        return status;
      }
    }

    HWND hwnd = nullptr;
    uint32_t width = 0, height = 0;

    if (info->hwnd) {
      hwnd = info->hwnd;
      auto hwndRect = RECT {};
      GetClientRect(info->hwnd, &hwndRect);
      width = static_cast<uint32_t>(std::max(0l, hwndRect.right - hwndRect.left));
      height = static_cast<uint32_t>(std::max(0l, hwndRect.bottom - hwndRect.top));
    }

    IDirect3DDevice9Ex* d3d9Device = nullptr;
    {
      auto presInfo = D3DPRESENT_PARAMETERS {};
      {
        presInfo.BackBufferWidth = width;
        presInfo.BackBufferHeight = height;
        presInfo.BackBufferFormat = D3DFMT_UNKNOWN;
        presInfo.BackBufferCount = 0;
        presInfo.MultiSampleType = D3DMULTISAMPLE_NONE;
        presInfo.MultiSampleQuality = 0;
        presInfo.SwapEffect = D3DSWAPEFFECT_DISCARD;
        presInfo.hDeviceWindow = hwnd;
        presInfo.Windowed = true;
        presInfo.EnableAutoDepthStencil = false;
        presInfo.AutoDepthStencilFormat = D3DFMT_UNKNOWN;
        presInfo.Flags = 0;
        presInfo.FullScreen_RefreshRateInHz = 0;
        presInfo.PresentationInterval = 0;
      }

      HRESULT hr = d3d9->CreateDeviceEx(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &presInfo,
        nullptr,
        &d3d9Device);
      if (FAILED(hr) || !d3d9Device) {
        d3d9->Release();
        if (isHResultAliasedWithRemixErrorCode(hr)) {
          // return special aliased HRESULT
          return static_cast<remixapi_ErrorCode>(hr);
        }
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
      }
    }
    {
      remixapi_ErrorCode status = remixapi_dxvk_RegisterD3D9Device(d3d9Device);
      if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
        d3d9->Release();
        return status;
      }
      assert(s_dxvkD3D9 && s_dxvkDevice);
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_Shutdown(void) {
    if (s_dxvkDevice) {
      while (true) {
        ULONG left = s_dxvkDevice->Release();
        if (left == 0) {
          break;
        }
      }
      s_dxvkDevice = nullptr;
    }
    if (s_dxvkD3D9) {
      while (true) {
        ULONG left = s_dxvkD3D9->Release();
        if (left == 0) {
          break;
        }
      }
      s_dxvkD3D9 = nullptr;
    }
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  remixapi_ErrorCode REMIXAPI_CALL remixapi_Present(const remixapi_PresentInfo* info) {
    dxvk::D3D9DeviceEx* remixDevice = tryAsDxvk();
    if (!remixDevice) {
      return REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED;
    }
    HRESULT hr = remixDevice->Present(NULL, NULL, info ? info->hwndOverride : NULL, NULL);
    if (FAILED(hr)) {
      return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
    }

    UINT windowWidth = 0, windowHeight = 0;
    {
      HWND hwnd = info && info->hwndOverride ? info->hwndOverride : remixDevice->GetWindow();
      if (hwnd) {
        RECT hwndRect = {};
        GetClientRect(hwnd, &hwndRect);
        windowWidth = static_cast<UINT>(std::max(0l, hwndRect.right - hwndRect.left));
        windowHeight = static_cast<UINT>(std::max(0l, hwndRect.bottom - hwndRect.top));
      }
    }

    if (windowWidth > 0 && windowHeight > 0) {
      IDirect3DSwapChain9* swapchain = nullptr;
      hr = remixDevice->GetSwapChain(0, &swapchain);
      if (FAILED(hr) || !swapchain) {
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
      }
      D3DPRESENT_PARAMETERS presentParams{};
      hr = swapchain->GetPresentParameters(&presentParams);
      if (FAILED(hr)) {
        return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
      }

      // reset swapchain if window has changed
      if (presentParams.BackBufferWidth != windowWidth && //
          presentParams.BackBufferHeight != windowHeight) {
        presentParams.BackBufferWidth = windowWidth;
        presentParams.BackBufferHeight = windowHeight;
        remixDevice->ResetEx(&presentParams, nullptr);
      }
    }

    return REMIXAPI_ERROR_CODE_SUCCESS;
  }

  bool isVersionCompatible(uint64_t userVersion) {
    constexpr uint64_t compiledVersion = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);

    bool isDevelopment = 
      REMIXAPI_VERSION_GET_MAJOR(userVersion) == 0 &&
      REMIXAPI_VERSION_GET_MAJOR(compiledVersion) == 0;

    if (isDevelopment) {
      // each minor change is breaking
      return REMIXAPI_VERSION_GET_MINOR(userVersion) == REMIXAPI_VERSION_GET_MINOR(compiledVersion);
    }

    if (REMIXAPI_VERSION_GET_MAJOR(userVersion) == REMIXAPI_VERSION_GET_MAJOR(compiledVersion)) {
      // user version must be before the currently compiled version:
      // features that are requested by a user must be available in the current binary
      if (REMIXAPI_VERSION_GET_MINOR(userVersion) <= REMIXAPI_VERSION_GET_MINOR(compiledVersion)) {
        return true;
      }
    }

    return false;
  }
}

extern "C"
{
  REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(const remixapi_InitializeLibraryInfo* info,
                                                                       remixapi_Interface* out_result) {
    if (!info || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (!out_result) {
      return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
    }
    if (!isVersionCompatible(info->version)) {
      return REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION;
    }
    s_apiVersion = info->version;

    auto interf = remixapi_Interface {};
    {
      interf.Startup = remixapi_Startup;
      interf.Shutdown = remixapi_Shutdown;
      interf.Present = remixapi_Present;
      interf.CreateMaterial = remixapi_CreateMaterial;
      interf.DestroyMaterial = remixapi_DestroyMaterial;
      interf.CreateMesh = remixapi_CreateMesh;
      interf.DestroyMesh = remixapi_DestroyMesh;
      interf.SetupCamera = remixapi_SetupCamera;
      interf.DrawInstance = remixapi_DrawInstance;
      interf.CreateLight = remixapi_CreateLight;
      interf.DestroyLight = remixapi_DestroyLight;
      interf.DrawLightInstance = remixapi_DrawLightInstance;
      interf.SetConfigVariable = remixapi_SetConfigVariable;
      interf.dxvk_CreateD3D9 = remixapi_dxvk_CreateD3D9_legacy;
      interf.dxvk_RegisterD3D9Device = remixapi_dxvk_RegisterD3D9Device;
      interf.dxvk_GetExternalSwapchain = remixapi_dxvk_GetExternalSwapchain;
      interf.dxvk_GetVkImage = remixapi_dxvk_GetVkImage;
      interf.dxvk_CopyRenderingOutput = remixapi_dxvk_CopyRenderingOutput;
      interf.dxvk_SetDefaultOutput = remixapi_dxvk_SetDefaultOutput;
      interf.pick_RequestObjectPicking = remixapi_pick_RequestObjectPicking;
      interf.pick_HighlightObjects = remixapi_pick_HighlightObjects;
    }
    static_assert(sizeof(interf) == 168, "Add/remove function registration");

    *out_result = interf;
    return REMIXAPI_ERROR_CODE_SUCCESS;
  }
}
