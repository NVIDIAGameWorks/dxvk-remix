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

#include "rtx_lights_data.h"
#include "rtx_light_utils.h"
#include "rtx_light_manager.h"
#include "rtx_options.h"

#include <d3d9types.h>
#include <regex>

#include "../../lssusd/game_exporter_common.h"
#include "../../lssusd/game_exporter_paths.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/blackbody.h>
#include "pxr/usd/usdLux/lightapi.h"
#include "../../lssusd/usd_include_end.h"

#define WRITE_CTOR_INIT(name, usd_attr, type, minVal, maxVal, defaultVal) \
      m_##name(defaultVal),

#define WRITE_CONSTANT_DESERIALIZER(name, usd_attr, type, minVal, maxVal, defaultVal) \
      { \
        pxr::VtValue val; \
        getLightAttribute(prim, pxr::TfToken(#usd_attr), pxr::TfToken("inputs:"#usd_attr)).Get(&val); \
        if(!val.IsEmpty()) { \
          static_assert(uint32_t(DirtyFlags::k_##name) < 32); \
          m_dirty.set(DirtyFlags::k_##name); \
          m_##name = val.UncheckedGet<type>(); \
        } \
      }

#define WRITE_PARAMETER_MERGE(name, usd_attr, type, minVal, maxVal, defaultVal) \
      if(!m_dirty.test(DirtyFlags::k_##name)) { \
        m_##name = input.m_##name; \
      }

#define WRITE_CONSTANT_SANITIZATION(name, usd_attr, type, minVal, maxVal, defaultVal) \
      m_##name = clamp(m_##name, minVal, maxVal);

namespace dxvk {
  LightData::LightData(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f& localToRoot, const bool absoluteTransform) :
    LIST_LIGHT_CONSTANTS(WRITE_CTOR_INIT)
    m_lightType(LightType::Unknown),
    m_transformType(absoluteTransform ? TransformType::Absolute : TransformType::Relative),
    m_dirty(absoluteTransform ? (1u << (uint32_t)DirtyFlags::k_Transform) : 0){
    getLightType(lightPrim, m_lightType);
    extractTransform(localToRoot);
    deserialize(lightPrim);
    sanitizeData();
  }

  RtLight LightData::toRtLight(const RtLight* const originalLight) const {
    switch (m_lightType) {
    case LightType::Sphere:
    {
      if (!originalLight || originalLight->getType() != RtLightType::Sphere) {
        return RtLight(RtSphereLight(m_position, calculateRadiance(), m_Radius, getLightShaping(m_zAxis), m_cachedHash));
      } else {
        return RtLight(RtSphereLight(m_position, calculateRadiance(), m_Radius, getLightShaping(m_zAxis), m_cachedHash), originalLight->getSphereLight());
      }
    }
    case LightType::Rect:
    {
      const Vector2 dimensions(m_Width * m_xScale, m_Height * m_yScale);
      return RtLight(RtRectLight(m_position, dimensions, m_xAxis, m_yAxis, calculateRadiance(), getLightShaping(m_zAxis)));
    }
    case LightType::Disk:
    {
      const Vector2 halfDimensions(m_Radius * m_xScale, m_Radius * m_yScale);
      return RtLight(RtDiskLight(m_position, halfDimensions, m_xAxis, m_yAxis, calculateRadiance(), getLightShaping(m_zAxis)));
    }
    case LightType::Cylinder:
    {
      return RtLight(RtCylinderLight(m_position, m_Radius, m_xAxis, m_Length * m_xScale, calculateRadiance()));
    }
    case LightType::Distant:
    {
      const float halfAngle = m_AngleRadians / 2.0f;
      return RtLight(RtDistantLight(m_zAxis, halfAngle, calculateRadiance(), m_cachedHash));
    }
    default:
      throw;
    }
  }

  void LightData::merge(const D3DLIGHT9& light) {
    // Special case, dont do any merging if we know we dont need to
    if (m_dirty != DirtyFlags::AllDirty) {
      std::optional<LightData> input = tryCreate(light);
      if (input.has_value()) {
        merge(input.value()); // when converting from legacy lights, we always use the games transform
      }
    }

    // Merge in the light type if it's currently unknown
    if (m_lightType == LightType::Unknown) {
      switch (light.Type) {
      case D3DLIGHT_POINT:
      case D3DLIGHT_SPOT:
        m_lightType = LightType::Sphere;
        break;
      case D3DLIGHT_DIRECTIONAL:
        m_lightType = LightType::Distant;
        break;
      }
    }
  }

  bool LightData::isShapingEnabled() const {
    return m_ConeAngleRadians != (180.f * kDegreesToRadians) || m_ConeSoftness != 0.0f || m_Focus != 0.0f;
  }

  void LightData::merge(const LightData& input) {
    LIST_LIGHT_CONSTANTS(WRITE_PARAMETER_MERGE);

    if (!m_dirty.test(DirtyFlags::k_Transform)) {
      m_position = input.m_position;
      m_xAxis = input.m_xAxis;
      m_yAxis = input.m_yAxis;
      m_zAxis = input.m_zAxis;
      m_xScale = input.m_xScale;
      m_yScale = input.m_yScale;
      m_zScale = input.m_zScale;
    }
  }

  std::optional<LightData> LightData::tryCreate(const D3DLIGHT9& light) {
    // Ensure the D3D9 Light is of a valid type
    // Note: This is done as some games will pass invalid data to various D3D9 calls and since the RtLight
    // requires a valid light type for construction it needs to be checked in advance to avoid issues.

    if (light.Type < D3DLIGHT_POINT || light.Type > D3DLIGHT_DIRECTIONAL) {
      Logger::err(str::format(
        "Attempted to convert a fixed function light with invalid light type: ",
        light.Type
      ));
      ONCE(assert(false));

      return {};
    }

    // Construct and return the light

    switch (light.Type) {
    case D3DLIGHT_POINT:
    case D3DLIGHT_SPOT:
      return std::optional<LightData>(std::in_place, createFromPointSpot(light));
    case D3DLIGHT_DIRECTIONAL:
      return std::optional<LightData>(std::in_place, createFromDirectional(light));
    }
    return {};
  }
  
  std::optional<LightData> LightData::tryCreate(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f& localToRoot, const bool absoluteTransform) {
    // Ensure the USD light is a supported type

    if (!isSupportedUsdLight(lightPrim)) {
      return {};
    }

    // Construct and return the light

    return std::optional<LightData>(std::in_place, LightData(lightPrim, localToRoot, absoluteTransform));
  }

  LightData LightData::createFromDirectional(const D3DLIGHT9& light) {
    LightData output;

    output.m_lightType = LightType::Distant;

    const Vector3 originalDirection { light.Direction.x, light.Direction.y, light.Direction.z };

    // Note: D3D9 Directional lights have no requirement on if the direction is normalized or not,
    // so it must be normalized here for usage in the rendering (as m_direction is assumed to be normalized).
    // Additionally, the direction may be the zero vector (even though D3D9 disallows this), so fall back to the
    // Z axis in this case.
    output.m_zAxis = safeNormalize(originalDirection, Vector3(0.0f, 0.0f, 1.0f));
    output.m_AngleRadians = LightManager::lightConversionDistantLightFixedAngle();
    output.m_Color = { light.Diffuse.r, light.Diffuse.g, light.Diffuse.b };
    output.m_Intensity = LightManager::lightConversionDistantLightFixedIntensity();

    // Note: Changing this code will alter "stable" light hashes from D3D9 and potentially break replacement assets.
      
    // Note: Stable version used for D3D9 light conversion path to ensure stable hashing regardless of code changes.
    output.m_cachedHash = (XXH64_hash_t) RtLightType::Rect;

    // Note: A constant half angle is used due to a legacy artifact of accidentally including half angle value in the
    // hash for lights translated from D3D9 to Remix (which always inherited a value from the
    // lightConversionDistantLightFixedAngle option, divided by 2.
    const float legacyStableHalfAngle = 0.0349f / 2.0f;

    // Note: Takes specific arguments to calculate a stable hash which does not change due to other changes in the light's code.
    // Expects an un-altered direction directly from the D3DLIGHT9 Direction (a legacy artifact caused by not normalizing this in
    // our initial implementation).
    // Note: Radiance not included to somewhat uniquely identify lights when constructed from D3D9 Lights.
    output.m_cachedHash = XXH64(&originalDirection[0], sizeof(originalDirection), output.m_cachedHash);
    output.m_cachedHash = XXH64(&legacyStableHalfAngle, sizeof(legacyStableHalfAngle), output.m_cachedHash);

    return output;
  }

  LightData LightData::createFromPointSpot(const D3DLIGHT9& light) {
    LightData output;
    output.m_lightType = LightType::Sphere;

    const Vector3 originalPosition { light.Position.x, light.Position.y, light.Position.z };
    const float originalBrightness = std::max(light.Diffuse.r, std::max(light.Diffuse.g, light.Diffuse.b));

    output.m_position = originalPosition;
    output.m_Radius = LightManager::lightConversionSphereLightFixedRadius() * RtxOptions::sceneScale();
    output.m_Intensity = LightUtils::calculateIntensity(light, output.m_Radius);
    output.m_Color = Vector3(light.Diffuse.r, light.Diffuse.g, light.Diffuse.b) / originalBrightness;

    RtLightShaping originalLightShaping {};

    if (light.Type == D3DLIGHT_SPOT) {
      const Vector3 originalDirection { light.Direction.x, light.Direction.y, light.Direction.z };

      // Set the Sphere Light's shaping

      // Note: D3D9 Spot light directions have no requirement on if the direction is normalized or not,
      // so it must be normalized here for usage in the rendering (as the m_shaping primaryAxis is assumed to be normalized).
      // Additionally, the direction may be the zero vector (even though D3D9 disallows this), so fall back to the
      // Z axis in this case.
      output.m_zAxis = safeNormalize(originalDirection, Vector3(0.0f, 0.0f, 1.0f));

      // ConeAngle is the outer angle of the spotlight
      output.m_ConeAngleRadians = light.Phi / 2.0f;
      // ConeSoftness is how far in the transition region reaches
      output.m_ConeSoftness = std::cos(light.Theta / 2.0f) - std::cos(output.m_ConeAngleRadians);
      output.m_Focus = light.Falloff;

      // Set the Stable Light Shaping
      originalLightShaping = output.getLightShaping(originalDirection);
    }

    // Note: Stable version used for D3D9 light conversion path to ensure stable hashing regardless of code changes.
    output.m_cachedHash = (XXH64_hash_t) RtLightType::Sphere;

    // Note: A constant radius of 4.0f is used due to a legacy artifact of accidently including radius value in the
    // hash for lights translated from D3D9 to Remix (which always inherited a value from the
    // lightConversionSphereLightFixedRadius option.
    const float legacyStableRadius = 4.0f;

    // Note: Takes specific arguments to calculate a stable hash which does not change due to other changes in the light's code.
    // Expects an un-altered position directly from the D3DLIGHT9 Position, and a Stable Light Shaping structure with its primaryAxis member
    // directly derived from the D3DLIGHT9 Direction (again a legacy artifact caused by not normalizing this in our initial implementation).
    // Note: Radiance not included to somewhat uniquely identify lights when constructed from D3D9 Lights.
    output.m_cachedHash = XXH64(&originalPosition[0], sizeof(originalPosition), output.m_cachedHash);
    output.m_cachedHash = XXH64(&legacyStableRadius, sizeof(legacyStableRadius), output.m_cachedHash);
    output.m_cachedHash = XXH64(&output.m_cachedHash, sizeof(output.m_cachedHash), originalLightShaping.getHash());

    return output;
  }

  // When a light is being overridden in USD, we may not always get the light type.
  // For these lights we rely on the prim path (which is standardized for captured lights)
  //  and use the light determined by the game at runtime [See: merge(D3DLIGHT9)]
  // Expanded: ^/RootNode/lights/light_[0-9A-Fa-f]{16}$
  static const std::regex s_unknownLightPattern("^" + lss::gRootNodePath.GetAsString() + "/" + lss::gTokLights.GetString() + "/" + lss::prefix::light + "[0-9A-Fa-f]{16}$");

  bool LightData::getLightType(const pxr::UsdPrim& lightPrim, LightData::LightType& typeOut) {
    if (lightPrim.IsA<pxr::UsdLuxSphereLight>()) {
      typeOut = LightType::Sphere;
      return true;
    } else if (lightPrim.IsA<pxr::UsdLuxRectLight>()) {
      typeOut = LightType::Rect;
      return true;
    } else if (lightPrim.IsA<pxr::UsdLuxDiskLight>()) {
      typeOut = LightType::Disk;
      return true;
    } else if (lightPrim.IsA<pxr::UsdLuxCylinderLight>()) {
      typeOut = LightType::Cylinder;
      return true;
    } else if (lightPrim.IsA<pxr::UsdLuxDistantLight>()) {
      typeOut = LightType::Distant;
      return true;
    } else if (std::regex_match(lightPrim.GetPath().GetAsString(), s_unknownLightPattern)) {
      typeOut = LightType::Unknown;
      return true;
    }

    return false;
  }

  bool LightData::isSupportedUsdLight(const pxr::UsdPrim& lightPrim) {
    LightType unused;
    return getLightType(lightPrim, unused);
  }

  // USD transitioned from `intensity` to `inputs:intensity` for all its light attributes, we need to support content
  // authored before and after that change.
  const pxr::UsdAttribute LightData::getLightAttribute(const pxr::UsdPrim& prim, const pxr::TfToken& token, const pxr::TfToken& inputToken) {
    const pxr::UsdAttribute& attr = prim.GetAttribute(inputToken);
    if (!attr.HasValue()) {
      const pxr::UsdAttribute& old_attr = prim.GetAttribute(token);
      if (old_attr.HasValue()) {
        ONCE(Logger::warn(str::format("Legacy light attribute detected: ", old_attr.GetPath())));
      }
      return old_attr;
    }
    return attr;
  }

  Vector3 LightData::calculateRadiance() const {
    Vector3 temperature(1.f);
    if (m_EnableColorTemp) {
      const pxr::GfVec3f vec = pxr::UsdLuxBlackbodyTemperatureAsRgb(m_ColorTemp);
      temperature = Vector3(vec.data());
    }
    return m_Color * m_Intensity * pow(2, m_Exposure) * temperature;
  }

  RtLightShaping LightData::getLightShaping(Vector3 zAxis) const {
    RtLightShaping shaping;
    shaping.primaryAxis = zAxis;
    shaping.cosConeAngle = cos(m_ConeAngleRadians);
    shaping.coneSoftness = m_ConeSoftness;
    shaping.focusExponent = m_Focus;
    shaping.enabled = isShapingEnabled();
    return shaping;
  }

  void LightData::extractTransform(const pxr::GfMatrix4f& localToRoot) {
    pxr::GfVec3f xVecUsd = localToRoot.TransformDir(pxr::GfVec3f(1.0f, 0.0f, 0.0f));
    pxr::GfVec3f yVecUsd = localToRoot.TransformDir(pxr::GfVec3f(0.0f, 1.0f, 0.0f));
    pxr::GfVec3f zVecUsd = localToRoot.TransformDir(pxr::GfVec3f(0.0f, 0.0f, 1.0f));

    // Note: These calls both normalize the X/Y/Z vectors and return their previous length. This is mandatory as the axis
    // vectors used to construct lights with must be normalized.
    m_xScale = xVecUsd.Normalize();
    m_yScale = yVecUsd.Normalize();
    m_zScale = zVecUsd.Normalize();

    m_position = localToRoot.ExtractTranslation().data();
    m_xAxis = xVecUsd.GetArray();
    m_yAxis = yVecUsd.GetArray();
    m_zAxis = zVecUsd.GetArray();

    // NOTE: this negative on m_zAxis is clearly indicating a problem somewhere, but just preserving the existing behavior for now
    if (m_lightType == LightType::Sphere) {
      m_zAxis = -m_zAxis;
    }
  }

  void LightData::deserialize(const pxr::UsdPrim& prim) {
    LIST_LIGHT_CONSTANTS(WRITE_CONSTANT_DESERIALIZER);

    // Note: USD specifies angles in degrees, but we prefer radians
    if (m_dirty.test(DirtyFlags::k_ConeAngleRadians)) {
      m_ConeAngleRadians *= kDegreesToRadians;
    }
    if (m_dirty.test(DirtyFlags::k_AngleRadians)) {
      m_AngleRadians *= kDegreesToRadians;
    }

    // If this light is fully defined (i.e. a child light) then we need to use all attributes
    if (prim.GetSpecifier() == pxr::SdfSpecifier::SdfSpecifierDef) {
      m_dirty = DirtyFlags::AllDirty;
    } 
  }

  void LightData::sanitizeData() {
    LIST_LIGHT_CONSTANTS(WRITE_CONSTANT_SANITIZATION)
  }

} // namespace dxvk

#undef WRITE_CTOR_INIT
#undef WRITE_CONSTANT_DESERIALIZER
#undef WRITE_PARAMETER_MERGE
#undef WRITE_CONSTANT_SANITIZATION
