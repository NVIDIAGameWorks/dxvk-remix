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

#include <optional>
#include <limits>

#include "rtx_utils.h"
#include "rtx/utility/shader_types.h"
#include "rtx/concept/light/light_types.h"
#include "rtx_types.h"

namespace dxvk 
{

namespace {

// Todo: Compute size directly from sizeof of GPU structure (by including it), for now computed by sum of members manually
constexpr std::size_t kLightGPUSize = 4 * 4 * 4;
// The brightness the original light has to fall to in perceptual space in order to 'end'.
// Note: 1/255 stays in sRGB here to run distance/attenuation fitting against the "incorrect" gamma space lighting of legacy games.
constexpr float kLegacyLightEndValue = 1.0f / 255.0f;
// The brightness the new light will have in at the same distance the original light ended.
// Note: Linear space lighting value to represent a "dark" physically based value. This is completely arbitrary as the exposure and
// tonemapping we use will change how this maps to the output space, so unlike the legacy light end value we cannot make assumptions
// about where it will reach entierly black. Currently set to -3.6ish EV100 (0.01 in luminance terms).
constexpr float kNewLightEndValue = 0.01f;

constexpr uint16_t kNewLightIdx = 0xFFFF; // Special index used to identify new lights

}  // namespace

enum class RtLightType {
  Sphere = lightTypeSphere,
  Rect = lightTypeRect,
  Disk = lightTypeDisk,
  Cylinder = lightTypeCylinder,
  Distant = lightTypeDistant,
};

enum class RtLightAntiCullingType {
  Ignore,
  GameLight,
  LightReplacement,
  MeshReplacement
};

constexpr uint64_t kInvalidExternallyTrackedLightId = std::numeric_limits<uint64_t>::max();

struct RtLightShaping {
public:
  RtLightShaping(
    bool enabled = kEnabledDefaultValue, Vector3 direction = kDirectionDefaultValue,
    float cosConeAngle = kCosConeAngleDefaultValue, float coneSoftness = kConeSoftnessDefaultValue, float focusExponent = kFocusExponentDefaultValue);
  // A function to try to create the Light Shaping. If creation fails, an empty optional is returned.
  static std::optional<RtLightShaping> tryCreate(
    bool enabled = kEnabledDefaultValue, Vector3 direction = kDirectionDefaultValue,
    float cosConeAngle = kCosConeAngleDefaultValue, float coneSoftness = kConeSoftnessDefaultValue, float focusExponent = kFocusExponentDefaultValue);

  XXH64_hash_t getHash() const;

  void applyTransform(Matrix3 transform);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool getEnabled() const {
    return m_enabled != 0;
  }

  const Vector3& getDirection() const {
    return m_direction;
  }

  float getCosConeAngle() const {
    return m_cosConeAngle;
  }

  float getConeSoftness() const {
    return m_coneSoftness;
  }

  float getFocusExponent() const {
    return m_focusExponent;
  }

private:
  static constexpr bool kEnabledDefaultValue{ false };
  static constexpr Vector3 kDirectionDefaultValue{ 0.0f, 0.0f, 1.0f };
  static constexpr float kCosConeAngleDefaultValue{ 0.0f };
  static constexpr float kConeSoftnessDefaultValue{ 0.0f };
  static constexpr float kFocusExponentDefaultValue{ 0.0f };

  static bool validateParameters(
    bool enabled, Vector3 direction,
    float cosConeAngle, float coneSoftness, float focusExponent);

  uint32_t m_enabled; // Note: using uint instead of bool to avoid having unused padded memory, 
                      // which is an issue when calculating a hash from the struct's object
  Vector3 m_direction;
  float m_cosConeAngle;
  float m_coneSoftness;
  float m_focusExponent;
};

struct RtSphereLight {
  RtSphereLight(const Vector3& position, const Vector3& radiance, float radius,
                const RtLightShaping& shaping, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue,
                const XXH64_hash_t forceHash = kForceHashDefaultValue);
  // A function to try to create the Sphere Light. If creation fails, an empty optional is returned.
  static std::optional<RtSphereLight> tryCreate(
    const Vector3& position, const Vector3& radiance, float radius,
    const RtLightShaping& shaping, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue,
    const XXH64_hash_t forceHash = kForceHashDefaultValue);

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool operator==(const RtSphereLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  Vector3 getPosition() const {
    return m_position;
  }

  Vector3 getRadiance() const {
    return m_radiance;
  }

  float getRadius() const {
    return m_radius;
  }

  const RtLightShaping& getShaping() const {
    return m_shaping;
  }

  float getVolumetricRadianceScale() const {
    return m_volumetricRadianceScale;
  }

private:
  static constexpr float kVolumetricRadianceScaleDefaultValue{ 1.0f };
  static constexpr XXH64_hash_t kForceHashDefaultValue{ kEmptyHash };

  static bool validateParameters(
    const Vector3& position, const Vector3& radiance, float radius,
    const RtLightShaping& shaping, float volumetricRadianceScale,
    const XXH64_hash_t forceHash);
  void updateCachedHash();

  Vector3 m_position;
  Vector3 m_radiance;
  float m_radius;
  RtLightShaping m_shaping;
  float m_volumetricRadianceScale;

  XXH64_hash_t m_cachedHash;
};

struct RtRectLight {
  RtRectLight(const Vector3& position, const Vector2& dimensions,
              const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
              const Vector3& radiance, const RtLightShaping& shaping,
              float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);

  // A function to try to create the Rect Light. If creation fails, an empty optional is returned.
  static std::optional<RtRectLight> tryCreate(
    const Vector3& position, const Vector2& dimensions,
    const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
    const Vector3& radiance, const RtLightShaping& shaping,
    float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool operator==(const RtRectLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  Vector3 getPosition() const {
    return m_position;
  }

  Vector2 getDimensions() const {
    return m_dimensions;
  }

  Vector3 getXAxis() const {
    return m_xAxis;
  }

  Vector3 getYAxis() const {
    return m_yAxis;
  }

  Vector3 getRadiance() const {
    return m_radiance;
  }

  const RtLightShaping& getShaping() const {
    return m_shaping;
  }

  float getVolumetricRadianceScale() const {
    return m_volumetricRadianceScale;
  }

private:
  static constexpr float kVolumetricRadianceScaleDefaultValue{ 1.0f };

  static bool validateParameters(
    const Vector3& position, const Vector2& dimensions,
    const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
    const Vector3& radiance, const RtLightShaping& shaping,
    float volumetricRadianceScale);
  void updateCachedHash();

  Vector3 m_position;
  Vector2 m_dimensions;
  Vector3 m_xAxis;
  Vector3 m_yAxis;
  // Note: Previously derived from X/Y axes via a cross product, now specified directly to be more consistent with
  // how the light shaping direction axis is specified, and USD replacement logic in general (handling inverse scale for instance).
  // Do note this allows for the X/Y/direction vectors to represent coordinate systems with both handednesses,
  // so do not rely on cross products with the X/Y axes anymore.
  Vector3 m_direction;
  Vector3 m_radiance;
  RtLightShaping m_shaping;
  float m_volumetricRadianceScale;

  XXH64_hash_t m_cachedHash;
};

struct RtDiskLight {
  RtDiskLight(const Vector3& position, const Vector2& halfDimensions,
              const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
              const Vector3& radiance, const RtLightShaping& shaping,
              float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);
  // A function to try to create the Disk Light. If creation fails, an empty optional is returned.
  static std::optional<RtDiskLight> tryCreate(
    const Vector3& position, const Vector2& halfDimensions,
    const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
    const Vector3& radiance, const RtLightShaping& shaping,
    float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool operator==(const RtDiskLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  Vector3 getPosition() const {
    return m_position;
  }

  Vector2 getHalfDimensions() const {
    return m_halfDimensions;
  }

  Vector3 getXAxis() const {
    return m_xAxis;
  }

  Vector3 getYAxis() const {
    return m_yAxis;
  }

  Vector3 getRadiance() const {
    return m_radiance;
  }

  const RtLightShaping& getShaping() const {
    return m_shaping;
  }

  float getVolumetricRadianceScale() const {
    return m_volumetricRadianceScale;
  }

private:
  static constexpr float kVolumetricRadianceScaleDefaultValue{ 1.0f };

  static bool validateParameters(
    const Vector3& position, const Vector2& halfDimensions,
    const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
    const Vector3& radiance, const RtLightShaping& shaping,
    float volumetricRadianceScale);
  void updateCachedHash();

  Vector3 m_position;
  Vector2 m_halfDimensions;
  Vector3 m_xAxis;
  Vector3 m_yAxis;
  // Note: Previously derived from X/Y axes via a cross product, now specified directly to be more consistent with
  // how the light shaping direction axis is specified, and USD replacement logic in general (handling inverse scale for instance).
  // Do note this allows for the X/Y/direction vectors to represent coordinate systems with both handednesses,
  // so do not rely on cross products with the X/Y axes anymore.
  Vector3 m_direction;
  Vector3 m_radiance;
  RtLightShaping m_shaping;
  float m_volumetricRadianceScale;

  XXH64_hash_t m_cachedHash;
};

struct RtCylinderLight {
  RtCylinderLight(
    const Vector3& position, float radius, const Vector3& axis, float axisLength,
    const Vector3& radiance, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);
  // A function to try to create the Cylinder Light. If creation fails, an empty optional is returned.
  static std::optional<RtCylinderLight> tryCreate(
    const Vector3& position, float radius, const Vector3& axis, float axisLength,
    const Vector3& radiance, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue);

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool operator==(const RtCylinderLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  Vector3 getPosition() const {
    return m_position;
  }

  float getRadius() const {
    return m_radius;
  }

  Vector3 getAxis() const {
    return m_axis;
  }

  float getAxisLength() const {
    return m_axisLength;
  }

  Vector3 getRadiance() const {
    return m_radiance;
  }

  float getVolumetricRadianceScale() const {
    return m_volumetricRadianceScale;
  }
private:
  static constexpr float kVolumetricRadianceScaleDefaultValue{ 1.0f };

  static bool validateParameters(
    const Vector3& position, float radius, const Vector3& axis, float axisLength,
    const Vector3& radiance, float volumetricRadianceScale);
  void updateCachedHash();

  Vector3 m_position;
  float m_radius;
  Vector3 m_axis;
  float m_axisLength;
  Vector3 m_radiance;
  float m_volumetricRadianceScale;

  XXH64_hash_t m_cachedHash;
};

struct RtDistantLight {
  RtDistantLight(
    const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue,
    const XXH64_hash_t forceHash = kForceHashDefaultValue);
  // A function to try to create the Cylinder Light. If creation fails, an empty optional is returned.
  static std::optional<RtDistantLight> tryCreate(
    const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale = kVolumetricRadianceScaleDefaultValue,
    const XXH64_hash_t forceHash = kForceHashDefaultValue);

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  bool operator==(const RtDistantLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  XXH64_hash_t getHash() const {
    return m_cachedHash;
  }

  Vector3 getDirection() const {
    return m_direction;
  }

  float getHalfAngle() const {
    return m_halfAngle;
  }

  Vector3 getRadiance() const {
    return m_radiance;
  }

  float getVolumetricRadianceScale() const {
    return m_volumetricRadianceScale;
  }
private:
  static constexpr float kVolumetricRadianceScaleDefaultValue{ 1.0f };
  static constexpr XXH64_hash_t kForceHashDefaultValue{ kEmptyHash };

  static bool validateParameters(
    const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale,
    const XXH64_hash_t forceHash);
  void updateCachedHash();

  Vector3 m_direction;
  float m_halfAngle;
  Vector3 m_radiance;
  float m_volumetricRadianceScale;

  // Note: Computed from direction, not meant to be modified directly
  Vector4 m_orientation;
  // Note: Computed from half angle, not meant to be modified directly
  float m_cosHalfAngle;
  float m_sinHalfAngle;

  XXH64_hash_t m_cachedHash;
};

struct DomeLight {
  Vector3 radiance;
  TextureRef texture;
  Matrix4 worldToLight;
};

struct RtLight {
  RtLight(const RtSphereLight& light);

  RtLight(const RtSphereLight& light, const RtSphereLight& originalSphereLight);

  RtLight(const RtRectLight& light);

  RtLight(const RtDiskLight& light);

  RtLight(const RtCylinderLight& light);

  RtLight(const RtDistantLight& light);

  RtLight(const RtLight& light);

  ~RtLight();

  void applyTransform(const Matrix4& lightToWorld);

  void writeGPUData(unsigned char* data, std::size_t& offset) const;

  RtLight& operator=(const RtLight& rtLight);
  
  bool operator==(const RtLight& rhs) const = delete;

  Vector4 getColorAndIntensity() const;

  Vector3 getPosition() const;

  Vector3 getDirection() const;

  XXH64_hash_t getInitialHash() const {
    return m_cachedInitialHash;
  }

  XXH64_hash_t getTransformedHash() const;

  Vector3 getRadiance() const;

  RtLightType getType() const {
    return m_type;
  }

  const RtSphereLight& getSphereLight() const {
    assert(m_type == RtLightType::Sphere);

    return m_sphereLight;
  }

  const RtRectLight& getRectLight() const {
    assert(m_type == RtLightType::Rect);

    return m_rectLight;
  }

  const RtDiskLight& getDiskLight() const {
    assert(m_type == RtLightType::Disk);

    return m_diskLight;
  }

  const RtCylinderLight& getCylinderLight() const {
    assert(m_type == RtLightType::Cylinder);

    return m_cylinderLight;
  }

  const RtDistantLight& getDistantLight() const {
    assert(m_type == RtLightType::Distant);

    return m_distantLight;
  }

  uint32_t getFrameLastTouched() const {
    return m_frameLastTouched;
  }

  const bool getIsInsideFrustum() const {
    return m_isInsideFrustum;
  }

  const RtLightAntiCullingType getLightAntiCullingType() const {
    return m_anticullingType;
  }

  const Matrix4& getMeshReplacementTransform() const {
    return m_originalMeshTransform;
  }

  const AxisAlignedBoundingBox& getMeshReplacementBoundingBox() const {
    return m_originalMeshBoundingBox;
  }

  const Vector3& getSphereLightReplacementOriginalPosition() const {
    return m_originalPosition;
  }

  const float getSphereLightReplacementOriginalRadius() const {
    return m_originalLightRadius;
  }

  void setFrameLastTouched(const uint32_t frame) const {
    m_frameLastTouched = frame;
  }

  uint32_t getBufferIdx() const {
    return m_bufferIdx;
  }

  void setBufferIdx(const uint32_t idx) const {
    m_bufferIdx = idx;
  }

  PrimInstanceOwner& getPrimInstanceOwner() {
    return m_primInstanceOwner;
  }

  const PrimInstanceOwner& getPrimInstanceOwner() const {
    return m_primInstanceOwner;
  }

  void markAsInsideFrustum() const {
    m_isInsideFrustum = true;
  }

  void markAsOutsideFrustum() const {
    m_isInsideFrustum = false;
  }

  void setLightAntiCullingType(const RtLightAntiCullingType antiCullingType) const {
    m_anticullingType = antiCullingType;
  }

  void cacheMeshReplacementAntiCullingProperties(
    const Matrix4& meshTransform,
    const AxisAlignedBoundingBox& boundingBox) const {
    m_originalMeshTransform = meshTransform;
    m_originalMeshBoundingBox = boundingBox;
  }

  void cacheLightReplacementAntiCullingProperties(const RtSphereLight& sphereLight) const {
    m_originalPosition = sphereLight.getPosition();
    m_originalLightRadius = sphereLight.getRadius();
  }

  float getVolumetricRadianceScale() const {
    switch (m_type) {
      case RtLightType::Sphere:
        return m_sphereLight.getVolumetricRadianceScale();
      case RtLightType::Rect:
        return m_rectLight.getVolumetricRadianceScale();
      case RtLightType::Disk:
        return m_diskLight.getVolumetricRadianceScale();
      case RtLightType::Cylinder:
        return m_cylinderLight.getVolumetricRadianceScale();
      case RtLightType::Distant:
        return m_distantLight.getVolumetricRadianceScale();
    }
    return 0.f;
  }

  void markForGarbageCollection() {
    m_isMarkedForGarbageCollection = true;
  }

  bool isMarkedForGarbageCollection() const {
    return m_isMarkedForGarbageCollection;
  }

  uint64_t getExternallyTrackedLightId() const {
    return m_externallyTrackedLightId;
  }

  void setExternallyTrackedLightId(const uint64_t id) {
    m_externallyTrackedLightId = id;
  }

  uint32_t isStaticCount = 0;
  bool isDynamic = false;

private:
  // Type-specific Light Information

  RtLightType m_type;
  union {
    RtSphereLight m_sphereLight;
    RtRectLight m_rectLight;
    RtDiskLight m_diskLight;
    RtCylinderLight m_cylinderLight;
    RtDistantLight m_distantLight;
  };

  XXH64_hash_t m_cachedInitialHash = 0;

  // Used to associate parts of a replacement heirarchy.
  PrimInstanceOwner m_primInstanceOwner;

  uint64_t m_externallyTrackedLightId = kInvalidExternallyTrackedLightId;

  // Shared Light Information
  mutable uint32_t m_frameLastTouched = kInvalidFrameIndex;
  mutable uint32_t m_bufferIdx = kNewLightIdx; // index into the light list (RTX-DI needs to understand how light indices change over time)

  // Anti-Culling Properties
  mutable bool m_isInsideFrustum = true;

  // If set to true, the light will be removed at the end of the current frame.
  bool m_isMarkedForGarbageCollection = false;

  mutable RtLightAntiCullingType m_anticullingType = RtLightAntiCullingType::Ignore;
  union {
    // Mesh->Light(s) Replacement Anti-Culling
    struct {
      // Note: Don't revert these 2 variables! Or the bbox may be polluted by sphere light data.
      mutable Matrix4 m_originalMeshTransform;
      mutable AxisAlignedBoundingBox m_originalMeshBoundingBox;
    };
    // Light->Light(s) Replacement Anti-Culling
    struct {
      mutable Vector3 m_originalPosition;
      mutable float m_originalLightRadius;
    };
  };

  void copyFrom(const RtLight& light);
};

} // namespace dxvk
