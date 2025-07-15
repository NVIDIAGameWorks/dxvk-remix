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

#include "rtx_lights.h"
#include "rtx_light_manager.h"
#include "rtx_light_utils.h"
#include "rtx_global_volumetrics.h"

namespace dxvk {

namespace {

// Validation parameters
// Note: Changing these may cause new assertions of Remix API failures, be careful when adjusting.

constexpr float kNormalizationThreshold = 0.01f;
constexpr float kOrthogonalityThreshold = 0.01f;

  Vector4 safeColorAndIntensity(const Vector3& radiance) {
    const float intensity = std::max(std::max(radiance[0], radiance[1]), radiance[2]);
    if (intensity < std::numeric_limits<float>::min()) {
      return Vector4{ 0,0,0,0 };
    }

    // Limit the intensity to prevent precision issues.
    constexpr float IntensityEpsilon = std::numeric_limits<float>::min();
    constexpr float IntensityMax = 1e+20f;

    static_assert(std::numeric_limits<float>::min() <= IntensityEpsilon);
    static_assert((IntensityEpsilon * IntensityMax) / IntensityEpsilon > std::numeric_limits<float>::min());
    static_assert((IntensityEpsilon * IntensityMax) / IntensityEpsilon < std::numeric_limits<float>::max());

    // Safer dividing for the case when 'intensity' is quite small,
    // so the result won't be denormalized.
    return Vector4{
      std::clamp(radiance[0], 0.f, intensity * IntensityMax) / intensity,
      std::clamp(radiance[1], 0.f, intensity * IntensityMax) / intensity,
      std::clamp(radiance[2], 0.f, intensity * IntensityMax) / intensity,
      intensity,
    };
  }

  // Note: This function is intended to be called whenever a light is constructed to allow for the volumetric radiance scale to be "disabled"
  // easily. Might be a bit costly to keep checking the option like this versus having some sort of static boolean holding the state, but
  // in theory option value lookup is not terribly expensive and it is put behind a short circut to avoid needing to disable the scale when it is
  // already effectively disabled (set to 1.0, which is the most common case usually).
  // Another approach to this would be to send the flag to disable the volumetric radiance scale to the GPU instead as that may allow for constant
  // folding to optimize out the decoding path when the feature is not in use, but generally the potential overhead of doing that check on the
  // GPU when constant folding is not in place (which is always a possibility) would be much more than just doing it here on the CPU.
  float adjustVolumetricRadianceScale(float volumetricRadianceScale) {
    // Note: Short circut on a check for if the volume radiance scale needs to be adjusted to avoid needing to check the option redundantly when
    // it is already effectively disabled.
    if (volumetricRadianceScale != 1.0 && RtxGlobalVolumetrics::debugDisableRadianceScaling()) {
      return 1.0f;
    }

    return volumetricRadianceScale;
  }

  // Note: This helper is used for writing the volumetric radiance scale to ensure it is in the proper location and within the proper
  // range without needing to duplicate this code between all lights (since it is common to them all).
  void writeGPUDataVolumetricRadianceScale(unsigned char* data, std::size_t oldOffset, std::size_t& offset, float volumetricRadianceScale) {
    assert((offset - oldOffset) == 3 * sizeof(Vector4i) + 2 * sizeof(uint32_t)); // data3.z
    // Note: Volumetric radiance scale effectively in the range [0, inf), but must fit within a 16 bit float in practice.
    assert(volumetricRadianceScale >= 0.0f && volumetricRadianceScale < FLOAT16_MAX);
    assert(!std::isnan(volumetricRadianceScale) && !std::isinf(volumetricRadianceScale));

    // Note: Using full 32 bit float here. Limits conservatively set to float 16 maximums however in case this ever needs to be packed down by
    // 2 bytes in the future.
    writeGPUHelper(data, offset, volumetricRadianceScale);
  }

}

RtLightShaping::RtLightShaping(bool enabled, Vector3 direction, float cosConeAngle, float coneSoftness, float focusExponent)
  : m_enabled(enabled ? 1 : 0)
  , m_direction(direction)
  , m_cosConeAngle(cosConeAngle)
  , m_coneSoftness(coneSoftness)
  , m_focusExponent(focusExponent) {
  // assert(validateParameters(enabled, direction, cosConeAngle, coneSoftness, focusExponent));
}

std::optional<RtLightShaping> RtLightShaping::tryCreate(
  bool enabled, Vector3 direction, float cosConeAngle, float coneSoftness, float focusExponent) {
  if (!validateParameters(enabled, direction, cosConeAngle, coneSoftness, focusExponent)) {
    return {};
  }

  return RtLightShaping{ enabled, direction, cosConeAngle, coneSoftness, focusExponent };
}

XXH64_hash_t RtLightShaping::getHash() const {
  XXH64_hash_t h = 0;

  if (m_enabled) {
    h = XXH64(&m_direction[0], sizeof(m_direction), h);
    h = XXH64(&m_cosConeAngle, sizeof(m_cosConeAngle), h);
    h = XXH64(&m_coneSoftness, sizeof(m_coneSoftness), h);
    h = XXH64(&m_focusExponent, sizeof(m_focusExponent), h);
  }

  return h;
}

void RtLightShaping::applyTransform(Matrix3 transform) {
  // Note: Safe normalize used in case the transformation collapses the direction down to a zero vector (as the transform
  // is not validated to be "proper").
  m_direction = safeNormalize(transform * m_direction, Vector3(0.0f, 0.0f, 1.0f));

  // Note: Ensure the transformation resulted in a normalized direction as the shaping should not have
  // this property violated by a transformation.
  assert(isApproxNormalized(m_direction, 0.01f));
}

void RtLightShaping::writeGPUData(unsigned char* data, std::size_t& offset) const {
  // occupies 12 bytes
  if (m_enabled) {
    // Note: Ensure the direction vector is normalized as this is a requirement for the GPU encoding.
    assert(isApproxNormalized(m_direction, kNormalizationThreshold));
    assert(m_direction < Vector3(FLOAT16_MAX));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.z));

    assert(m_cosConeAngle < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(1.0f - m_cosConeAngle));
    assert(m_coneSoftness < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_coneSoftness));
    assert(m_focusExponent < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(m_focusExponent));
  } else {
    writeGPUPadding<12>(data, offset);
  }
}

bool RtLightShaping::validateParameters(
  bool enabled, Vector3 direction, float cosConeAngle, float coneSoftness, float focusExponent) {
  // Early out if shaping is disabled, no need to validate disabled parameters
  // Note: By checking this here, this assumes that shaping cannot be enabled/disabled at runtime as otherwise parameters will not be validated.

  if (!enabled) {
    return true;
  }

  // Ensure the direction is normalized

  if (!isApproxNormalized(direction, kNormalizationThreshold)) {
    return false;
  }

  // Ensure shaping parameters are within the valid ranges

  // Note: Cosine angle should be within [-1, 1] always (otherwise it is not a valid cosine value).
  if (cosConeAngle < -1.0f || cosConeAngle > 1.0f) {
    return false;
  }

  // Todo: In the future potentially check that coneSoftness is within [0, pi] as it doesn't need to be outside of this range.
  if (coneSoftness < 0.0f) {
    return false;
  }

  if (focusExponent < 0.0f) {
    return false;
  }

  return true;
}

RtSphereLight::RtSphereLight(
  const Vector3& position, const Vector3& radiance, float radius,
  const RtLightShaping& shaping, float volumetricRadianceScale,
  const XXH64_hash_t forceHash)
  : m_position(position)
  , m_radiance(radiance)
  , m_radius(radius)
  , m_shaping(shaping)
  , m_volumetricRadianceScale(volumetricRadianceScale) {
  // assert(validateParameters(position, radiance, radius, shaping, volumetricRadianceScale, forceHash));

  m_volumetricRadianceScale = adjustVolumetricRadianceScale(m_volumetricRadianceScale);

  if (forceHash == kEmptyHash) {
    updateCachedHash();
  } else {
    m_cachedHash = forceHash;
  }
}

std::optional<RtSphereLight> RtSphereLight::tryCreate(
  const Vector3& position, const Vector3& radiance, float radius,
  const RtLightShaping& shaping, float volumetricRadianceScale,
  const XXH64_hash_t forceHash) {
  if (!validateParameters(position, radiance, radius, shaping, volumetricRadianceScale, forceHash)) {
    return {};
  }

  return RtSphereLight{ position, radiance, radius, shaping, volumetricRadianceScale, forceHash };
}

void RtSphereLight::applyTransform(const Matrix4& lightToWorld) {
  // Transform the light position

  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  // Adjust radius based on transformation

  const Matrix3 transform(lightToWorld);

  // Note: Scale radius by average of the 3 axes. For uniform scale all axis lengths will be the same, but for
  // non-uniform scale the average is needed to approximate a new radius.
  const float radiusFactor = (length(transform[0]) + length(transform[1]) + length(transform[2])) / 3.f;
  m_radius *= radiusFactor;

  m_shaping.applyTransform(transform);

  updateCachedHash();
}

void RtSphereLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  writeGPUHelper(data, offset, m_position.x);
  writeGPUHelper(data, offset, m_position.y);
  writeGPUHelper(data, offset, m_position.z);
  assert(m_radius < FLOAT16_MAX);
  writeGPUHelper(data, offset, glm::packHalf1x16(m_radius));
  writeGPUPadding<2>(data, offset);

  writeGPUHelper(data, offset, packLogLuv32(m_radiance));

  m_shaping.writeGPUData(data, offset);

  writeGPUPadding<24>(data, offset);

  writeGPUDataVolumetricRadianceScale(data, oldOffset, offset, m_volumetricRadianceScale);

  // Note: Sphere light type (0) + shaping enabled flag
  uint32_t flags = lightTypeSphere << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtSphereLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

bool RtSphereLight::validateParameters(
  const Vector3& position, const Vector3& radiance, float radius,
  const RtLightShaping& shaping, float volumetricRadianceScale,
  const XXH64_hash_t forceHash) {
  // Ensure the radius is positive and within the float16 range

  if (radius < 0.0f || radius >= FLOAT16_MAX) {
    return false;
  }

  // Ensure the radiance is positive

  if (radiance.x < 0.0f || radiance.y < 0.0f || radiance.z < 0.0f) {
    return false;
  }

  // Ensure the volumetric radiance scale is positive and within the float16 range

  if (volumetricRadianceScale < 0.0f || volumetricRadianceScale >= FLOAT16_MAX) {
    return false;
  }

  return true;
}

void RtSphereLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Sphere;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_radius, sizeof(m_radius), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());
  // Note: Volumetric radiance scale not included either for performance as it's likely not
  // much more identifying than position and generally is not used.

  m_cachedHash = h;
}

RtRectLight::RtRectLight(
  const Vector3& position, const Vector2& dimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping, float volumetricRadianceScale)
  : m_position(position)
  , m_dimensions(dimensions)
  , m_xAxis(xAxis)
  , m_yAxis(yAxis)
  , m_direction(direction)
  , m_radiance(radiance)
  , m_volumetricRadianceScale(volumetricRadianceScale)
  , m_shaping(shaping) {
  // assert(validateParameters(position, dimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale));

  m_volumetricRadianceScale = adjustVolumetricRadianceScale(m_volumetricRadianceScale);

  updateCachedHash();
}

std::optional<RtRectLight> RtRectLight::tryCreate(
  const Vector3& position, const Vector2& dimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping, float volumetricRadianceScale) {
  if (!validateParameters(position, dimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale)) {
    return {};
  }

  return RtRectLight{ position, dimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale };
}

void RtRectLight::applyTransform(const Matrix4& lightToWorld) {
  // Transform the light position

  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  // Transform various light direction axes

  const Matrix3 transform(lightToWorld);

  m_xAxis = transform * m_xAxis;
  m_yAxis = transform * m_yAxis;
  m_direction = transform * m_direction;

  float xAxisScale;
  float yAxisScale;

  m_xAxis = safeNormalizeGetLength(m_xAxis, Vector3(1.0f, 0.0f, 0.0f), xAxisScale);
  m_yAxis = safeNormalizeGetLength(m_yAxis, Vector3(0.0f, 1.0f, 0.0f), yAxisScale);
  m_direction = safeNormalize(m_direction, Vector3(0.0f, 0.0f, 1.0f));

  // Todo: In the future consider re-orthogonalizing these the X/Y/direction vectors as
  // transformations like this may cause compounding error in the orthogonalization properties.

  // Adjust dimensions based on new axis scales

  m_dimensions.x *= xAxisScale;
  m_dimensions.y *= yAxisScale;

  m_shaping.applyTransform(transform);
  
  updateCachedHash();
}

void RtRectLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  writeGPUHelper(data, offset, m_position.x);
  writeGPUHelper(data, offset, m_position.y);
  writeGPUHelper(data, offset, m_position.z);
  assert(m_dimensions < Vector2(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_dimensions.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_dimensions.y));

  writeGPUHelper(data, offset, packLogLuv32(m_radiance));

  m_shaping.writeGPUData(data, offset);

  // Note: Ensure the X axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_xAxis, kNormalizationThreshold));
  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  // Note: Ensure the Y axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_yAxis, kNormalizationThreshold));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  // Note: Ensure the direction vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_direction, kNormalizationThreshold));
  assert(m_direction < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.z));

  // Note: Unused space for rect lights
  writeGPUPadding<6>(data, offset);

  writeGPUDataVolumetricRadianceScale(data, oldOffset, offset, m_volumetricRadianceScale);

  // Note: Rect light type (1) + shaping enabled flag
  uint32_t flags = lightTypeRect << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtRectLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

bool RtRectLight::validateParameters(
  const Vector3& position, const Vector2& dimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping,
  float volumetricRadianceScale) {
  // Ensure dimensions are positive and within the float16 range

  if (
    dimensions.x < 0.0f || dimensions.x >= FLOAT16_MAX ||
    dimensions.y < 0.0f || dimensions.y >= FLOAT16_MAX
  ) {
    return false;
  }

  // Ensure axis/direction vectors are normalized

  if (
    !isApproxNormalized(xAxis, kNormalizationThreshold) ||
    !isApproxNormalized(yAxis, kNormalizationThreshold) ||
    !isApproxNormalized(direction, kNormalizationThreshold)
  ) {
    return false;
  }

  // Ensure X/Y/direction axes are approximately orthogonal

  if (
    (dot(xAxis, yAxis) > kOrthogonalityThreshold) ||
    (dot(xAxis, direction) > kOrthogonalityThreshold) ||
    (dot(yAxis, direction) > kOrthogonalityThreshold)
  ) {
    return false;
  }

  // Ensure the radiance is positive

  if (radiance.x < 0.0f || radiance.y < 0.0f || radiance.z < 0.0f) {
    return false;
  }

  // Ensure the volumetric radiance scale is positive and within the float16 range

  if (volumetricRadianceScale < 0.0f || volumetricRadianceScale >= FLOAT16_MAX) {
    return false;
  }

  return true;
}

void RtRectLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Rect;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_dimensions[0], sizeof(m_dimensions), h);
  h = XXH64(&m_xAxis[0], sizeof(m_xAxis), h);
  h = XXH64(&m_yAxis[0], sizeof(m_yAxis), h);
  h = XXH64(&m_direction[0], sizeof(m_direction), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());
  // Note: Volumetric radiance scale not included either for performance as it's likely not
  // much more identifying than position and generally is not used.

  m_cachedHash = h;
}

RtDiskLight::RtDiskLight(
  const Vector3& position, const Vector2& halfDimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping,
  float volumetricRadianceScale)
  : m_position(position)
  , m_halfDimensions(halfDimensions)
  , m_xAxis(xAxis)
  , m_yAxis(yAxis)
  , m_direction(direction)
  , m_radiance(radiance)
  , m_shaping(shaping)
  , m_volumetricRadianceScale(volumetricRadianceScale) {
  // assert(validateParameters(position, halfDimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale));

  m_volumetricRadianceScale = adjustVolumetricRadianceScale(m_volumetricRadianceScale);

  updateCachedHash();
}

std::optional<RtDiskLight> RtDiskLight::tryCreate(
  const Vector3& position, const Vector2& halfDimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping,
  float volumetricRadianceScale) {
  if (!validateParameters(position, halfDimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale)) {
    return {};
  }

  return RtDiskLight{ position, halfDimensions, xAxis, yAxis, direction, radiance, shaping, volumetricRadianceScale };
}

void RtDiskLight::applyTransform(const Matrix4& lightToWorld) {
  // Transform the light position

  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  // Transform various light direction axes

  const Matrix3 transform(lightToWorld);

  m_xAxis = transform * m_xAxis;
  m_yAxis = transform * m_yAxis;
  m_direction = transform * m_direction;

  float xAxisScale;
  float yAxisScale;

  m_xAxis = safeNormalizeGetLength(m_xAxis, Vector3(1.0f, 0.0f, 0.0f), xAxisScale);
  m_yAxis = safeNormalizeGetLength(m_yAxis, Vector3(0.0f, 1.0f, 0.0f), yAxisScale);
  m_direction = safeNormalize(m_direction, Vector3(0.0f, 0.0f, 1.0f));

  // Todo: In the future consider re-orthogonalizing these the X/Y/direction vectors as
  // transformations like this may cause compounding error in the orthogonalization properties.

  // Adjust half dimensions based on new axis scales

  m_halfDimensions.x *= xAxisScale;
  m_halfDimensions.y *= yAxisScale;

  m_shaping.applyTransform(transform);

  updateCachedHash();
}

void RtDiskLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  writeGPUHelper(data, offset, m_position.x);
  writeGPUHelper(data, offset, m_position.y);
  writeGPUHelper(data, offset, m_position.z);
  assert(m_halfDimensions < Vector2(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_halfDimensions.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_halfDimensions.y));

  writeGPUHelper(data, offset, packLogLuv32(m_radiance));

  m_shaping.writeGPUData(data, offset);

  // Note: Ensure the X axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_xAxis, kNormalizationThreshold));
  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  // Note: Ensure the Y axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_yAxis, kNormalizationThreshold));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  // Note: Ensure the direction vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_direction, kNormalizationThreshold));
  assert(m_direction < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.z));

  // Note: Unused space for disk lights
  writeGPUPadding<6>(data, offset);

  writeGPUDataVolumetricRadianceScale(data, oldOffset, offset, m_volumetricRadianceScale);

  // Note: Disk light type (2) + shaping enabled flag
  uint32_t flags = lightTypeDisk << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtDiskLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

bool RtDiskLight::validateParameters(
  const Vector3& position, const Vector2& halfDimensions,
  const Vector3& xAxis, const Vector3& yAxis, const Vector3& direction,
  const Vector3& radiance, const RtLightShaping& shaping, float volumetricRadianceScale) {
  // Ensure half dimensions are positive and within the float16 range

  if (
    halfDimensions.x < 0.0f || halfDimensions.x >= FLOAT16_MAX ||
    halfDimensions.y < 0.0f || halfDimensions.y >= FLOAT16_MAX
  ) {
    return false;
  }

  // Ensure axis/direction vectors are normalized

  if (
    !isApproxNormalized(xAxis, kNormalizationThreshold) ||
    !isApproxNormalized(yAxis, kNormalizationThreshold) ||
    !isApproxNormalized(direction, kNormalizationThreshold)
  ) {
    return false;
  }

  // Ensure X/Y/direction axes are approximately orthogonal

  if (
    (dot(xAxis, yAxis) > kOrthogonalityThreshold) ||
    (dot(xAxis, direction) > kOrthogonalityThreshold) ||
    (dot(yAxis, direction) > kOrthogonalityThreshold)
  ) {
    return false;
  }

  // Ensure the radiance is positive

  if (radiance.x < 0.0f || radiance.y < 0.0f || radiance.z < 0.0f) {
    return false;
  }

  return true;
}

void RtDiskLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Disk;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_halfDimensions[0], sizeof(m_halfDimensions), h);
  h = XXH64(&m_xAxis[0], sizeof(m_xAxis), h);
  h = XXH64(&m_yAxis[0], sizeof(m_yAxis), h);
  h = XXH64(&m_direction[0], sizeof(m_direction), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());
  // Note: Volumetric radiance scale not included either for performance as it's likely not
  // much more identifying than position and generally is not used.

  m_cachedHash = h;
}

RtCylinderLight::RtCylinderLight(
  const Vector3& position, float radius, const Vector3& axis, float axisLength,
  const Vector3& radiance, float volumetricRadianceScale)
  : m_position(position)
  , m_radius(radius)
  , m_axis(axis)
  , m_axisLength(axisLength)
  , m_radiance(radiance)
  , m_volumetricRadianceScale(volumetricRadianceScale) {
  // assert(validateParameters(position, radius, axis, axisLength, radiance, volumetricRadianceScale));

  m_volumetricRadianceScale = adjustVolumetricRadianceScale(m_volumetricRadianceScale);

  updateCachedHash();
}

std::optional<RtCylinderLight> RtCylinderLight::tryCreate(
  const Vector3& position, float radius, const Vector3& axis, float axisLength,
  const Vector3& radiance, float volumetricRadianceScale) {
  if (!validateParameters(position, radius, axis, axisLength, radiance, volumetricRadianceScale)) {
    return {};
  }

  return RtCylinderLight{ position, radius, axis, axisLength, radiance, volumetricRadianceScale };
}

void RtCylinderLight::applyTransform(const Matrix4& lightToWorld) {
  // Transform the light position

  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  // Transform various light direction axes

  const Matrix3 transform(lightToWorld);

  m_axis = transform * m_axis;

  float axisScale;

  m_axis = safeNormalizeGetLength(m_axis, Vector3(1.0f, 0.0f, 0.0f), axisScale);

  // Adjust axis length based on new axis scale

  m_axisLength *= axisScale;
  
  // Scale radius by average scale after factoring out axis aligned scale.

  const float averageScale = (length(transform[0]) + length(transform[1]) + length(transform[2])) / 3.f;
  m_radius *= ((averageScale * 3.f) - axisScale) / 2.f;

  updateCachedHash();
}

void RtCylinderLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  writeGPUHelper(data, offset, m_position.x);
  writeGPUHelper(data, offset, m_position.y);
  writeGPUHelper(data, offset, m_position.z);
  assert(m_radius < FLOAT16_MAX);
  writeGPUHelper(data, offset, glm::packHalf1x16(m_radius));
  assert(m_axisLength < FLOAT16_MAX);
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axisLength));

  writeGPUHelper(data, offset, packLogLuv32(m_radiance));
  writeGPUPadding<12>(data, offset); // no shaping

  // Note: Ensure the axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_axis, kNormalizationThreshold));
  assert(m_axis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.z));

  // Note: Unused space for cylinder lights
  writeGPUPadding<18>(data, offset);

  writeGPUDataVolumetricRadianceScale(data, oldOffset, offset, m_volumetricRadianceScale);

  // Note: Cylinder light type (3)
  writeGPUHelper(data, offset, static_cast<uint32_t>(lightTypeCylinder << 29));

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtCylinderLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

bool RtCylinderLight::validateParameters(
  const Vector3& position, float radius, const Vector3& axis, float axisLength,
  const Vector3& radiance, float volumetricRadianceScale) {
  // Ensure the radius and axis length are positive and within the float16 range

  if (
    radius < 0.0f || radius >= FLOAT16_MAX ||
    axisLength < 0.0f || axisLength >= FLOAT16_MAX
  ) {
    return false;
  }

  // Ensure the axis vector is normalized

  if (!isApproxNormalized(axis, kNormalizationThreshold)) {
    return false;
  }

  // Ensure the radiance is positive

  if (radiance.x < 0.0f || radiance.y < 0.0f || radiance.z < 0.0f) {
    return false;
  }

  // Ensure the volumetric radiance scale is positive and within the float16 range

  if (volumetricRadianceScale < 0.0f || volumetricRadianceScale >= FLOAT16_MAX) {
    return false;
  }

  return true;
}

void RtCylinderLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Cylinder;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_radius, sizeof(m_radius), h);
  h = XXH64(&m_axis[0], sizeof(m_axis), h);
  h = XXH64(&m_axisLength, sizeof(m_axisLength), h);
  // Note: Volumetric radiance scale not included either for performance as it's likely not
  // much more identifying than position and generally is not used.

  m_cachedHash = h;
}

RtDistantLight::RtDistantLight(
  const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale,
  const XXH64_hash_t forceHash)
  // Note: Direction assumed to be normalized.
  : m_direction(direction)
  , m_halfAngle(halfAngle)
  , m_radiance(radiance)
  , m_volumetricRadianceScale(volumetricRadianceScale) {
  // assert(validateParameters(direction, halfAngle, radiance, volumetricRadianceScale, forceHash));

  m_volumetricRadianceScale = adjustVolumetricRadianceScale(m_volumetricRadianceScale);

  // Note: Cache a pre-computed orientation quaternion to avoid doing it on the GPU since we have space in the Light to spare
  m_orientation = getOrientation(Vector3(0.0f, 0.0f, 1.0f), m_direction);

  // Note: Cache sine and cosine of the half angle to avoid doing it on the GPU as well
  m_cosHalfAngle = std::cos(m_halfAngle);
  m_sinHalfAngle = std::sin(m_halfAngle);

  if (forceHash == kEmptyHash) {
    updateCachedHash();
  } else {
    m_cachedHash = forceHash;
  }
}

std::optional<RtDistantLight> RtDistantLight::tryCreate(
  const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale,
  const XXH64_hash_t forceHash) {
  if (!validateParameters(direction, halfAngle, radiance, volumetricRadianceScale, forceHash)) {
    return {};
  }

  return RtDistantLight{ direction, halfAngle, radiance, volumetricRadianceScale, forceHash };
}

void RtDistantLight::applyTransform(const Matrix4& lightToWorld) {
  // Transform the direction

  const Matrix3 transform(lightToWorld);

  m_direction = safeNormalize(transform * m_direction, Vector3(0.0f, 0.0f, 1.0f));
  m_orientation = getOrientation(Vector3(0.0f, 0.0f, 1.0f), m_direction);

  updateCachedHash();
}

void RtDistantLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  assert(m_direction < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.z));

  // Note: Ensure the orientation quaternion is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_orientation, kNormalizationThreshold));
  assert(m_orientation < Vector4(FLOAT16_MAX));
  // Note: Orientation could be more heavily packed (down to snorms, or even other quaternion memory encodings), but
  // there is enough space that no fancy encoding which would just waste performance on the GPU side is needed.
  writeGPUHelper(data, offset, glm::packHalf1x16(m_orientation.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_orientation.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_orientation.z));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_orientation.w));

  writeGPUPadding<2>(data, offset);

  writeGPUHelper(data, offset, packLogLuv32(m_radiance));
  writeGPUPadding<12>(data, offset); // no shaping

  writeGPUHelper(data, offset, m_cosHalfAngle);
  writeGPUHelper(data, offset, m_sinHalfAngle);

  // Note: Unused space for distant lights
  writeGPUPadding<16>(data, offset);

  writeGPUDataVolumetricRadianceScale(data, oldOffset, offset, m_volumetricRadianceScale);

  // Note: Distant light type (4)
  // Todo: Ideally match this with GPU light type constants
  writeGPUHelper(data, offset, static_cast<uint32_t>(lightTypeDistant << 29));

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtDistantLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

bool RtDistantLight::validateParameters(
  const Vector3& direction, float halfAngle, const Vector3& radiance, float volumetricRadianceScale,
  const XXH64_hash_t forceHash) {
  // Ensure direction is normalized

  if (!isApproxNormalized(direction, kNormalizationThreshold)) {
    return false;
  }

  // Ensure half angle is positive

  if (halfAngle < 0.0f) {
    return false;
  }

  // Ensure the radiance is positive

  if (radiance.x < 0.0f || radiance.y < 0.0f || radiance.z < 0.0f) {
    return false;
  }

  // Ensure the volumetric radiance scale is positive and within the float16 range

  if (volumetricRadianceScale < 0.0f || volumetricRadianceScale >= FLOAT16_MAX) {
    return false;
  }

  return true;
}

void RtDistantLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Distant;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_direction[0], sizeof(m_direction), h);
  h = XXH64(&m_halfAngle, sizeof(m_halfAngle), h);
  // Note: Volumetric radiance scale not included either for performance as it's likely not
  // much more identifying than position and generally is not used.

  m_cachedHash = h;
}

RtLight::RtLight(const RtSphereLight& light) {
  m_type = RtLightType::Sphere;
  new (&m_sphereLight) RtSphereLight(light);
  m_cachedInitialHash = m_sphereLight.getHash();
}

RtLight::RtLight(const RtSphereLight& light, const RtSphereLight& originalSphereLight) {
  m_type = RtLightType::Sphere;
  new (&m_sphereLight) RtSphereLight(light);
  m_cachedInitialHash = m_sphereLight.getHash();

  cacheLightReplacementAntiCullingProperties(originalSphereLight);
}

RtLight::RtLight(const RtRectLight& light) {
  m_type = RtLightType::Rect;
  new (&m_rectLight) RtRectLight(light);
  m_cachedInitialHash = m_rectLight.getHash();
}

RtLight::RtLight(const RtDiskLight& light) {
  m_type = RtLightType::Disk;
  new (&m_diskLight) RtDiskLight(light);
  m_cachedInitialHash = m_diskLight.getHash();
}

RtLight::RtLight(const RtCylinderLight& light) {
  m_type = RtLightType::Cylinder;
  new (&m_cylinderLight) RtCylinderLight(light);
  m_cachedInitialHash = m_cylinderLight.getHash();
}

RtLight::RtLight(const RtDistantLight& light) {
  m_type = RtLightType::Distant;
  new (&m_distantLight) RtDistantLight(light);
  m_cachedInitialHash = m_distantLight.getHash();
}

RtLight::~RtLight() {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    m_sphereLight.~RtSphereLight();
    break;
  case RtLightType::Rect:
    m_rectLight.~RtRectLight();
    break;
  case RtLightType::Disk:
    m_diskLight.~RtDiskLight();
    break;
  case RtLightType::Cylinder:
    m_cylinderLight.~RtCylinderLight();
    break;
  case RtLightType::Distant:
    m_distantLight.~RtDistantLight();
    break;
  }

  if (m_primInstanceOwner.getReplacementInstance() != nullptr) {
    m_primInstanceOwner.setReplacementInstance(nullptr, ReplacementInstance::kInvalidReplacementIndex, this, PrimInstance::Type::Light);
  }
}

void RtLight::applyTransform(const Matrix4& lightToWorld) {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    m_sphereLight.applyTransform(lightToWorld);
    break;
  case RtLightType::Rect:
    m_rectLight.applyTransform(lightToWorld);
    break;
  case RtLightType::Disk:
    m_diskLight.applyTransform(lightToWorld);
    break;
  case RtLightType::Cylinder:
    m_cylinderLight.applyTransform(lightToWorld);
    break;
  case RtLightType::Distant:
    m_distantLight.applyTransform(lightToWorld);
    break;
  }
}

void RtLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    m_sphereLight.writeGPUData(data, offset);
    break;
  case RtLightType::Rect:
    m_rectLight.writeGPUData(data, offset);
    break;
  case RtLightType::Disk:
    m_diskLight.writeGPUData(data, offset);
    break;
  case RtLightType::Cylinder:
    m_cylinderLight.writeGPUData(data, offset);
    break;
  case RtLightType::Distant:
    m_distantLight.writeGPUData(data, offset);
    break;
  }
}

Vector4 RtLight::getColorAndIntensity() const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    return m_sphereLight.getColorAndIntensity();
  case RtLightType::Rect:
    return m_rectLight.getColorAndIntensity();
  case RtLightType::Disk:
    return m_diskLight.getColorAndIntensity();
  case RtLightType::Cylinder:
    return m_cylinderLight.getColorAndIntensity();
  case RtLightType::Distant:
    return m_distantLight.getColorAndIntensity();
  }
}

Vector3 RtLight::getPosition() const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    return m_sphereLight.getPosition();
  case RtLightType::Rect:
    return m_rectLight.getPosition();
  case RtLightType::Disk:
    return m_diskLight.getPosition();
  case RtLightType::Cylinder:
    return m_cylinderLight.getPosition();
  case RtLightType::Distant:
    return Vector3();  // Distant lights, don't have a position.  Using 0 for position.
  }
}

Vector3 RtLight::getDirection() const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    return m_sphereLight.getShaping().getDirection();
  case RtLightType::Rect:
    return m_rectLight.getShaping().getDirection();
  case RtLightType::Disk:
    return m_diskLight.getShaping().getDirection();
  case RtLightType::Cylinder:
    return Vector3(0.f, 0.f, 1.f);
  case RtLightType::Distant:
    return m_distantLight.getDirection();
  }
}

XXH64_hash_t RtLight::getTransformedHash() const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    return m_sphereLight.getHash();
  case RtLightType::Rect:
    return m_rectLight.getHash();
  case RtLightType::Disk:
    return m_diskLight.getHash();
  case RtLightType::Cylinder:
    return m_cylinderLight.getHash();
  case RtLightType::Distant:
    return m_distantLight.getHash();
  }
}

Vector3 RtLight::getRadiance() const {
  switch (m_type) {
  default:
    assert(false);

    [[fallthrough]];
  case RtLightType::Sphere:
    return m_sphereLight.getRadiance();
  case RtLightType::Rect:
    return m_rectLight.getRadiance();
  case RtLightType::Disk:
    return m_diskLight.getRadiance();
  case RtLightType::Cylinder:
    return m_cylinderLight.getRadiance();
  case RtLightType::Distant:
    return m_distantLight.getRadiance();
  }
}

} // namespace dxvk 