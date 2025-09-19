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

#include "rtx_lights_data.h"
#include "rtx_light_utils.h"
#include "rtx_light_manager.h"
#include "rtx_options.h"

#include <remix/remix_c.h>

#include <d3d9types.h>
#include <regex>
#include <cassert>

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
#include <pxr/usd/usdLux/lightAPI.h>
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

  RtLight LightData::toRtLight(const RtLight* const originalLight) const {
    switch (m_lightType) {
    // Note: This default case should never be hit as an Unknown light type must be merged before it should be converted to LightData,
    // the assert is here just for debugging to assert when an unexpected light type is passed in (so this is an "unreachable"-style assert).
    default:
      assert(false);

      [[fallthrough]];
    case LightType::Sphere:
    {
      // Note: To match Omniverse's Sphere light scaling behavior, chose the largest of the 3 scale axes to scale the radius of the sphere by. Note that
      // really all the scale factors should be the same for a sphere light, but in case they are not this is how it should be done to match
      // the existing behavior.
      const auto radiusScale = std::max(std::max(m_xScale, m_yScale), m_zScale);

      if (!originalLight || originalLight->getType() != RtLightType::Sphere) {
        return RtLight(RtSphereLight(m_position, calculateRadiance(), m_Radius * radiusScale, getLightShaping(m_zAxis), m_VolumetricRadianceScale, m_cachedHash));
      } else {
        return RtLight(RtSphereLight(m_position, calculateRadiance(), m_Radius * radiusScale, getLightShaping(m_zAxis), m_VolumetricRadianceScale, m_cachedHash), originalLight->getSphereLight());
      }
    }
    case LightType::Rect:
    {
      const Vector2 dimensions(m_Width * m_xScale, m_Height * m_yScale);
      return RtLight(RtRectLight(m_position, dimensions, m_xAxis, m_yAxis, m_zAxis, calculateRadiance(), getLightShaping(m_zAxis), m_VolumetricRadianceScale));
    }
    case LightType::Disk:
    {
      const Vector2 halfDimensions(m_Radius * m_xScale, m_Radius * m_yScale);
      return RtLight(RtDiskLight(m_position, halfDimensions, m_xAxis, m_yAxis, m_zAxis, calculateRadiance(), getLightShaping(m_zAxis), m_VolumetricRadianceScale));
    }
    case LightType::Cylinder:
    {
      // Note: To match Omniverse's Cylinder light scaling behavior, chose the largest of the 2 scale axes to scale the radius of the circular
      // profile of the cylinder by (similar to how this is done for the Sphere light). Since the cylinder's length is done with respect to the
      // X axis (and scaled by the X axis scale), the Y and Z axes are used here for its circular cross section.
      const auto radiusScale = std::max(m_yScale, m_zScale);

      // Note: Unlike light shaping the Cylinder light is based around the X axis for its directionality aspect, not the Z axis.
      return RtLight(RtCylinderLight(m_position, m_Radius * radiusScale, m_xAxis, m_Length * m_xScale, calculateRadiance(), m_VolumetricRadianceScale));
    }
    case LightType::Distant:
    {
      const float halfAngle = m_AngleRadians / 2.0f;
      return RtLight(RtDistantLight(m_zAxis, halfAngle, calculateRadiance(), m_VolumetricRadianceScale, m_cachedHash));
    }
    }
  }

  void LightData::merge(const D3DLIGHT9& light) {
    // Special case, dont do any merging if we know we dont need to
    if (m_dirty != m_allDirty) {
      std::optional<LightData> input = tryCreate(light);
      if (input.has_value()) {
        merge(input.value()); // when converting from legacy lights, we always use the games transform
      }
    }

    // Merge in the light type if it's currently unknown
    if (m_lightType == LightType::Unknown) {
      switch (light.Type) {
      // Note: An invalid light type may be passed in and may not be sanitized properly by DXVK (unsure, it may actually be
      // handled properly), so this case ensures it can be caught for debugging purposes and that it falls back to some other
      // light type. Do note since this case is potentially expected at runtime this is no an "unreachable"-style assert, more
      // of a __debugbreak (C++ does not have "hard" asserts though like other languages that actually assert that he condition
      // can never happen to the compiler, so using an assert as a standardized debug break is fine).
      default:
        assert(false);

        [[fallthrough]];
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

  // Note: This can only be called after LightData::deserialize has been called due to relying on deserialized values.
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
    // Note: This case is checked for before, but just to make sure this default case is asserted against as it is
    // intended to be unreachable.
    default:
      assert(false);

      [[fallthrough]];
    case D3DLIGHT_POINT:
    case D3DLIGHT_SPOT:
      return std::optional<LightData>(std::in_place, createFromPointSpot(light));
    case D3DLIGHT_DIRECTIONAL:
      return std::optional<LightData>(std::in_place, createFromDirectional(light));
    }
    return {};
  }

  namespace {
    // Helpers for casting remixapi types to dxvk ones
    template<typename DxvkType, typename ApiType>
               DxvkType readMemberAs(const ApiType& v) = delete;
    template<> float    readMemberAs(const float& v)            { return v; }
    template<> bool     readMemberAs(const remixapi_Bool& v)    { return v; }
    template<> Vector3  readMemberAs(const remixapi_Float3D& v) { return Vector3{ v.x, v.y, v.z }; }

    template<typename T> bool hasNan(const T& v) = delete;
    template<>           bool hasNan(const float& v)            { return std::isnan(v); }
    template<>           bool hasNan(const remixapi_Float3D& v) { return hasNan(v.x) || hasNan(v.y) || hasNan(v.z); }
    template<>           bool hasNan(const remixapi_Bool& v)    { return false; }
    template<>           bool hasNan(const Vector3& v)          { return hasNan(v.x) || hasNan(v.y) || hasNan(v.z); }
    template<>           bool hasNan(const bool& v)             { return false; }

    bool isUsdLightTransformValid(const pxr::GfMatrix4f& transform) {
      // Ignore lights with a 0 scale transform on any axis
      // Note: Currently in Omniverse lights with a 0 scale on all 3 axes are considered valid and are simply ignored. Since this is "valid" behavior and not a bug (supposedly),
      // we match that here by ignoring creation of such lights. We however go further by ignoring a light with any of its 3 axes scaled by 0 due to how this can affect derivation
      // of required direction vectors on some light types as well as scale dimension or radii of lights to 0. Notably shaping when enabled requires the Z axis to be valid, the
      // rect/disk lights require the Z axis for their direction, and finally the cylinder light requires the X axis for its direction. Rather than checking all these cases
      // individually it is more simple to ignore lights with a transform like this in general as doing otherwise is likely confusing niche behavior anyways that should not be relied on.
      // It should also be noted that currently we still allow lights to have a radius or dimensions of 0 (pre-scale), this is not optimal as such lights essentially contribute nothing to the
      // scene and only increase sampling costs, but at least setting these scalar dimensions to 0 does not break the fundemental aspects of the light like how zero scale transforms do.
      // In the future though these 0 radius/dimension lights may be fine to also ignore too in this tryCreate function.

      // Note: The last row of the light's transform should always be 0, 0, 0, 1 for a typical affine matrix when column-major, since this matrix
      // is row major though we get the last column instead.
      assert(transform.GetColumn(3) == pxr::GfVec4f(0.0f, 0.0f, 0.0f, 1.0f));

      constexpr auto zeroVec3 = pxr::GfVec3f { 0.0f, 0.0f, 0.0f };

      // Note: USD's matrices are row major so to get the scale vectors we need to get the columns instead of the rows of the matrix.
      if (
        pxr::GfVec3f(transform[0][0], transform[1][0], transform[2][0]) == zeroVec3 ||
        pxr::GfVec3f(transform[0][1], transform[1][1], transform[2][1]) == zeroVec3 ||
        pxr::GfVec3f(transform[0][2], transform[1][2], transform[2][2]) == zeroVec3
      ) {
        return false;
      }

      // Transform must have finite floats
      if (
        hasNaNInf(transform[0][0]) || hasNaNInf(transform[0][1]) || hasNaNInf(transform[0][2]) ||
        hasNaNInf(transform[1][0]) || hasNaNInf(transform[1][1]) || hasNaNInf(transform[1][2]) ||
        hasNaNInf(transform[2][0]) || hasNaNInf(transform[2][1]) || hasNaNInf(transform[2][2]) ||
        hasNaNInf(transform[3][0]) || hasNaNInf(transform[3][1]) || hasNaNInf(transform[3][2])
      ) {
        return false;
      }

      return true;
    }
  } // anonymous namespace
  
  std::optional<LightData> LightData::tryCreate(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f* pLocalToRoot, const bool isOverrideLight, const bool absoluteTransform) {
    // Ensure the USD light is a supported type
    if (!isSupportedUsdLight(lightPrim)) {
      return {};
    }

    // Handle logic specific to lights with a transform set
    if (pLocalToRoot && !isUsdLightTransformValid(*pLocalToRoot)) {
      return {};
    }

    // Note: Retrieval of light type and deserialization of constants must happen before other operations to ensure all members
    // are set from their initial USD values (before say sanitation and other adjustment of said light members).
    LightType lightType = Unknown;
    if (!getLightType(lightPrim, lightType)) {
      Logger::warn(str::format("Failed to recognize a light type on \'", lightPrim.GetName().GetString(), "\'"));
      return {};
    }
    // Note: LightType::Unknown is a valid case, as it's meant to be replaced by a corresponding D3DLIGHT9

    auto l = LightData { lightType, isOverrideLight, absoluteTransform };
    {
      l.deserialize(lightPrim);
      l.extractTransform(pLocalToRoot);
      l.sanitizeData();
    }
    return l;
  }

  std::optional<LightData> LightData::tryCreate(const remixapi_LightInfoUSDEXT& src) {
    LightType lightType = Unknown;
    switch (src.lightType) {
    case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT: lightType = Distant; break;
    case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT: lightType = Cylinder; break;
    case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT: lightType = Disk; break;
    case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT: lightType = Rect; break;
    case REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT: lightType = Sphere; break;
    default: return {};
    }

    const auto gfTransform = pxr::GfMatrix4f {
      src.transform.matrix[0][0], src.transform.matrix[1][0], src.transform.matrix[2][0], 0.f,
      src.transform.matrix[0][1], src.transform.matrix[1][1], src.transform.matrix[2][1], 0.f,
      src.transform.matrix[0][2], src.transform.matrix[1][2], src.transform.matrix[2][2], 0.f,
      src.transform.matrix[0][3], src.transform.matrix[1][3], src.transform.matrix[2][3], 1.f,
    };
    if (!isUsdLightTransformValid(gfTransform)) {
      return {};
    }

    // If any field contains even one NaN, ignore a light source.
    // Inf is valid, as it can be std::clamp to a finite number.
    {
      #define FAIL_ON_INF_NAN(name, usd_attr, type, minVal, maxVal, defaultVal) \
        if ( src.p##name != nullptr ) {       \
          if ( hasNan( *( src.p##name ) ) ) { \
            return {};                        \
          }                                   \
        }
      LIST_LIGHT_CONSTANTS(FAIL_ON_INF_NAN);
      #undef FAIL_ON_INF_NAN
    }

    auto l = LightData { lightType, false, true };
    {
      #define READ_FROM_PTR(name, usd_attr, type, minVal, maxVal, defaultVal) \
      if ( src.p##name != nullptr ) {                           \
        l.m_##name = readMemberAs< type >( *( src.p##name ) );  \
        l.m_dirty.set( DirtyFlags::k_##name );                  \
      }
      LIST_LIGHT_CONSTANTS(READ_FROM_PTR);
      #undef READ_FROM_PTR

      l.extractTransform(&gfTransform);
      l.sanitizeData();
    }
    return l;
  }

  LightData::LightData(LightType lightType, bool isOverrideLight, bool absoluteTransform) :
    LIST_LIGHT_CONSTANTS(WRITE_CTOR_INIT)
    m_lightType { lightType },
    m_isOverrideLight { isOverrideLight },
    m_isRelativeTransform { !absoluteTransform && !isOverrideLight } {
  }

  LightData LightData::createFromDirectional(const D3DLIGHT9& light) {
    auto output = LightData{ Distant };

    const Vector3 originalDirection { light.Direction.x, light.Direction.y, light.Direction.z };

    // Note: D3D9 Directional lights have no requirement on if the direction is normalized or not,
    // so it must be normalized here for usage in the rendering (as m_direction is assumed to be normalized).
    // Additionally, the direction may be the zero vector (even though D3D9 disallows this), so fall back to the
    // Z axis in this case.
    output.m_zAxis = safeNormalize(originalDirection, Vector3(0.0f, 0.0f, 1.0f));
    output.m_AngleRadians = LightManager::lightConversionDistantLightFixedAngle();
    output.m_Color = Vector3{ light.Diffuse.r, light.Diffuse.g, light.Diffuse.b };
    output.m_Intensity = LightManager::lightConversionDistantLightFixedIntensity();

    // Note: Changing this code will alter "stable" light hashes from D3D9 and potentially break replacement assets.

    // Note: Stable version used for D3D9 light conversion path to ensure stable hashing regardless of code changes.
    // Also note the Rect Light type is intentionally used here instead of the Distant Light type. This is done due to
    // a mistake originating from a refactoring on 09-26-2023. Little to no previous content was affected by this bug
    // as directional light replacements are not common and were not used in Portal RTX, plus the lack of public usage
    // of Remix. As such, it is left this way to not break replacements created by users after the toolkit launch
    // (since the public launch included this bug and "fixing" it would probably be cause more harm than good).
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
    auto output = LightData{ Sphere };

    const Vector3 originalPosition { light.Position.x, light.Position.y, light.Position.z };
    const float originalBrightness = std::max(light.Diffuse.r, std::max(light.Diffuse.g, light.Diffuse.b));

    output.m_position = originalPosition;
    output.m_Radius = LightManager::lightConversionSphereLightFixedRadius() * RtxOptions::sceneScale();
    output.m_Intensity = LightUtils::calculateIntensity(light, output.m_Radius);
    output.m_Color = Vector3(light.Diffuse.r, light.Diffuse.g, light.Diffuse.b) / originalBrightness;

    XXH64_hash_t shapingHash = 0;

    if (light.Type == D3DLIGHT_SPOT) {
      const Vector3 originalDirection { light.Direction.x, light.Direction.y, light.Direction.z };

      // Set the Sphere Light's shaping

      // Note: D3D9 Spot light directions have no requirement on if the direction is normalized or not,
      // so it must be normalized here for usage in the rendering (as the m_shaping primaryAxis is assumed to be normalized).
      // Additionally, the direction may be the zero vector (even though D3D9 disallows this), so fall back to the
      // Z axis in this case.
      output.m_zAxis = safeNormalize(originalDirection, Vector3(0.0f, 0.0f, 1.0f));
      assert(isApproxNormalized(output.m_zAxis, 0.01f));

      // Todo: The Phi and Theta values from the D3D9 Light may need to be clamped to reasonable ranges here or sanitized in the
      // future if issues emerge from bad values being passed from games. For now though we do not as hashing relies on the shaping
      // being constructed with the current unsanitized values (and clamping may cause slight deviations in hashing which would require
      // duplicating the light shaping to get a variant with the "original" stable hash).

      // ConeAngle is the outer angle of the spotlight
      output.m_ConeAngleRadians = light.Phi / 2.0f;
      // ConeSoftness is how far in the transition region reaches
      output.m_ConeSoftness = std::cos(light.Theta / 2.0f) - std::cos(output.m_ConeAngleRadians);
      output.m_Focus = light.Falloff;

      // Set the Stable Light Shaping Hash
      // NOTE: This is broken out of the original light shaping hash code to maintain hash stability with
      // input values that lightShaping now rejects (specifically non-normalized direction vectors).
      float cosConeAngle = cos(output.m_ConeAngleRadians);
      shapingHash = XXH64(&originalDirection[0], sizeof(originalDirection), shapingHash);
      shapingHash = XXH64(&cosConeAngle, sizeof(cosConeAngle), shapingHash);
      shapingHash = XXH64(&output.m_ConeSoftness, sizeof(output.m_ConeSoftness), shapingHash);
      shapingHash = XXH64(&output.m_Focus, sizeof(output.m_Focus), shapingHash);
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
    output.m_cachedHash = XXH64(&output.m_cachedHash, sizeof(output.m_cachedHash), shapingHash);

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
    if (!attr.IsAuthored()) {
      const pxr::UsdAttribute& old_attr = prim.GetAttribute(token);
      if (old_attr.IsAuthored()) {
        Logger::warn(str::format("Legacy light attribute detected: ", old_attr.GetPath()));
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
    const auto enabled = isShapingEnabled();
    const auto primaryAxis = zAxis;
    const auto cosConeAngle = cos(m_ConeAngleRadians);
    const auto coneSoftness = m_ConeSoftness;
    const auto focusExponent = m_Focus;

    return RtLightShaping(enabled, primaryAxis, cosConeAngle, coneSoftness, focusExponent);
  }

  void LightData::extractTransform(const pxr::GfMatrix4f* pLocalToRoot) {
    // Ensure a transform exists to extract data from

    if (pLocalToRoot == nullptr) {
      return;
    }

    auto& localToRoot = *pLocalToRoot;

    // Load and sanitize transform-related light values

    // Note: Rows of a row-major matrix represent the axis vectors (just like columns of a column-major matrix do).
    const auto xVecUsd = localToRoot.GetRow3(0);
    const auto yVecUsd = localToRoot.GetRow3(1);
    const auto zVecUsd = localToRoot.GetRow3(2);

    m_position = Vector3{ localToRoot.ExtractTranslation().data() };
    m_xAxis = Vector3{ xVecUsd.GetArray() };
    m_yAxis = Vector3{ yVecUsd.GetArray() };
    m_zAxis = Vector3{ zVecUsd.GetArray() };

    // Note: Remix Lights expect normalized direction vectors (for the light shaping direction and directions related to Cylinder/Rect/Disk and Directional lights),
    // so these vectors must be normalized here. Additionally, these vectors must not be the zero vector so a fallback vector is provided. While zero scale transforms
    // are guarded against, there are still other transformations which may result in zero vectors, so this sanitization must be done regardless.
    m_xAxis = safeNormalizeGetLength(m_xAxis, Vector3(1.0f, 0.0f, 0.0f), m_xScale);
    m_yAxis = safeNormalizeGetLength(m_yAxis, Vector3(0.0f, 1.0f, 0.0f), m_yScale);
    m_zAxis = safeNormalizeGetLength(m_zAxis, Vector3(0.0f, 0.0f, 1.0f), m_zScale);

    // Todo: Possibly re-orthogonalize the axis vectors here if one has to be sanitized to a fallback vector as the X/Y/Z axes are required to be orthogonal in some cases.
    // Unsure if this is needed though as a transform that collapses one axis to the zero vector may do the same for all 3.

    // Convert directionality from USD to Remix conventions
    // Note: USD lights with directionality (Sphere lights with shaping, Disk/Rect/Distant lights in all cases) emit light in the direction of the -Z axis by default,
    // whereas Remix lights emit light in the +Z axis. As such to convert a USD light to a Remix light, the Z axis must be flipped. Cylinder lights and sphere lights with
    // shaping disabled are symmetric in their light emission (and cylinder lights use the X axis anyways rather than the Z axis for directionality), so this flip can be
    // done blindly without checking the light type for now as it will not affect these distributions.
    // See this for more info: https://openusd.org/release/user_guides/render_user_guide.html
    // "By convention, most lights with a primary axis (except CylinderLight) emit along the -Z axis. Area lights are centered in the XY plane and are 1 unit in diameter."

    // Note: This causes a handedness swap due to being an improper rotation. Do not rely on cross products between the X/Y/Z axes past this point without taking this fact
    // into account.
    m_zAxis = -m_zAxis;

    // Flip required axes on negative scale and sanitize scales
    // Note: This is once again done to match how Omniverse behaves somewhat, some negative scale transforms will change the direction typically
    // directional-esque lights (so shaped lights, rect, disk and distant) will point, and this should be reflected here. Note that Omniverse
    // actually doesn't handle this properly with rect and disk lights, only shaping and distant lights. We generalize this behavior to work properly
    // on all directional-esque lights by always inverting the axis when an negative scale is sanitized away.

    if (m_xScale < 0.0f) {
      m_xScale = -m_xScale;
      m_xAxis = -m_xAxis;
    }

    if (m_yScale < 0.0f) {
      m_yScale = -m_yScale;
      m_yAxis = -m_yAxis;
    }

    if (m_zScale < 0.0f) {
      m_zScale = -m_zScale;
      m_zAxis = -m_zAxis;
    }

    // Validate derived axes and scales

    // Note: Ensure the axes are normalized in the way we expect after normalization in USD and our own sanitation/adjustments.
    assert(isApproxNormalized(m_xAxis, 0.01f));
    assert(isApproxNormalized(m_yAxis, 0.01f));
    assert(isApproxNormalized(m_zAxis, 0.01f));

    // Note: Since the light transform is guarded against having zero scale transforms on any axis during LightData creation,
    // the scales here should not be zero in any case. This in addition to ensuring light axes can always be derived prevents
    // weird behavior with most light types as zero scales can lead to the light collapsing into a punctual light and being poorly
    // handled by Remix (due to not having special cases for such infinitesimal lights).
    // In addition, negative scales should not be allowed as actually part of the Light Data, rather if a negative scale exists
    // it may be converted to a positive scale for symmetric lights (and a directionality flip can be applied to lights using it
    // to scale an axis instead).
    assert(m_xScale > 0.0f && m_yScale > 0.0f && m_zScale > 0.0f);

    // Set the dirty bit now that the Light Data's transform has been updated

    m_dirty.set(DirtyFlags::k_Transform);
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
      m_dirty = m_allDirty;
    }

    // Warn about all fields that contain NaN.
    // Inf is valid, as it can be std::clamp to a finite number.
    {
      #define WARN_ON_NAN(name, usd_attr, type, minVal, maxVal, defaultVal) \
        if ( hasNan( m_##name ) ) { \
          Logger::warn(str::format("Invalid value (NaN) detected on USD attribute \'" #usd_attr "\' on \'", prim.GetName().GetString(), "\'")); \
        }
      LIST_LIGHT_CONSTANTS(WARN_ON_NAN);
      #undef WARN_ON_NAN
    }
    // Backward compatibility: the exporter had a division by 0 for color/intensity,
    // so they might be NaN in USD, so suppress to 0.
    {
      if (hasNan(m_Color)) {
        m_Color = Vector3 { 0,0,0 };
      }
      if (hasNan(m_Intensity)) {
        m_Intensity = 0.0f;
      }
      if (hasNan(m_Exposure)) {
        m_Exposure = 0.0f;
      }
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
