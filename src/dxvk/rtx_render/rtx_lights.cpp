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
#include "rtx_options.h"
#include "rtx_light_manager.h"
#include "rtx_light_utils.h"

namespace dxvk {
  namespace {
    Vector4 safeColorAndIntensity(const Vector3& radiance) {
      const float intensity = std::max(std::max(radiance[0], radiance[1]), radiance[2]);
      if (intensity < std::numeric_limits<float>::min()) {
        return { 0,0,0,0 };
      }

      // Limit the intensity to prevent precision issues.
      constexpr float IntensityEpsilon = std::numeric_limits<float>::min();
      constexpr float IntensityMax = 1e+20f;

      static_assert(std::numeric_limits<float>::min() <= IntensityEpsilon);
      static_assert((IntensityEpsilon * IntensityMax) / IntensityEpsilon > std::numeric_limits<float>::min());
      static_assert((IntensityEpsilon * IntensityMax) / IntensityEpsilon < std::numeric_limits<float>::max());

      // Safer dividing for the case when 'intensity' is quite small,
      // so the result won't be denormalized.
      return {
        std::clamp(radiance[0], 0.f, intensity * IntensityMax) / intensity,
        std::clamp(radiance[1], 0.f, intensity * IntensityMax) / intensity,
        std::clamp(radiance[2], 0.f, intensity * IntensityMax) / intensity,
        intensity,
      };
    }
  }

RtLightShaping::RtLightShaping(bool enabled, Vector3 primaryAxis, float cosConeAngle, float coneSoftness, float focusExponent)
  : m_enabled(enabled ? 1 : 0)
  , m_primaryAxis(primaryAxis)
  , m_cosConeAngle(cosConeAngle)
  , m_coneSoftness(coneSoftness)
  , m_focusExponent(focusExponent) {
  if (m_enabled) {
    // Note: Ensure the primary axis the shaping is constructed with is normalized is shaping is enabled. This check is also done
    // on GPU encoding, but checking it here as well ensures the shaping is always constructed properly. Do note this assumes that
    // shaping cannot be enabled/disabled at runtime due to only checking this enabled case.
    assert(isApproxNormalized(primaryAxis, 0.01f));
  }
}

XXH64_hash_t RtLightShaping::getHash() const {
  XXH64_hash_t h = 0;

  if (m_enabled) {
    h = XXH64(&m_primaryAxis[0], sizeof(m_primaryAxis), h);
    h = XXH64(&m_cosConeAngle, sizeof(m_cosConeAngle), h);
    h = XXH64(&m_coneSoftness, sizeof(m_coneSoftness), h);
    h = XXH64(&m_focusExponent, sizeof(m_focusExponent), h);
  }

  return h;
}

void RtLightShaping::applyTransform(Matrix3 transform) {
  // Note: Safe normalize used in case the transformation collapses the axis down to a zero vector (as the transform
  // is not validated to be "proper").
  m_primaryAxis = safeNormalize(transform * m_primaryAxis, Vector3(0.0f, 0.0f, 1.0f));

  // Note: Ensure the transformation resulted in a normalized primary axis as the shaping should not have
  // this property violated by a transformation.
  assert(isApproxNormalized(m_primaryAxis, 0.01f));
}

void RtLightShaping::writeGPUData(unsigned char* data, std::size_t& offset) const {
  // occupies 12 bytes
  if (m_enabled) {
    // Note: Ensure the normal vector is normalized as this is a requirement for the GPU encoding.
    assert(isApproxNormalized(m_primaryAxis, 0.01f));
    assert(m_primaryAxis < Vector3(FLOAT16_MAX));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_primaryAxis.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_primaryAxis.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(m_primaryAxis.z));

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

RtSphereLight::RtSphereLight(const Vector3& position, const Vector3& radiance, float radius, const RtLightShaping& shaping, const XXH64_hash_t forceHash)
  : m_position(position)
  , m_radiance(radiance)
  , m_radius(radius)
  , m_shaping(shaping) {
  if (forceHash == kEmptyHash) {
    updateCachedHash();
  } else {
    m_cachedHash = forceHash;
  }
}

void RtSphereLight::applyTransform(const Matrix4& lightToWorld) {
  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

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

  writeGPUPadding<28>(data, offset);

  // Note: Sphere light type (0) + shaping enabled flag
  uint32_t flags = lightTypeSphere << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtSphereLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

void RtSphereLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Sphere;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_radius, sizeof(m_radius), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());

  m_cachedHash = h;
}

RtRectLight::RtRectLight(const Vector3& position, const Vector2& dimensions, const Vector3& xAxis, const Vector3& yAxis, const Vector3& radiance, const RtLightShaping& shaping)
  : m_position(position)
  , m_dimensions(dimensions)
  , m_xAxis(xAxis)
  , m_yAxis(yAxis)
  , m_radiance(radiance)
  , m_shaping(shaping) {
  // Note: Normalization required as sometimes a cross product may not result in a normalized vector due to precision issues.
  m_normal = normalize(cross(m_yAxis, m_xAxis));

  updateCachedHash();
}

void RtRectLight::applyTransform(const Matrix4& lightToWorld) {
  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  const Matrix3 transform(lightToWorld);

  m_xAxis = transform * m_xAxis;
  const float xAxisLen = length(m_xAxis);
  m_xAxis = m_xAxis / xAxisLen;
  m_dimensions.x *= xAxisLen;
  
  m_yAxis = transform * m_yAxis;
  const float yAxisLen = length(m_yAxis);
  m_yAxis = m_yAxis / yAxisLen;
  m_dimensions.y *= yAxisLen;

  m_shaping.applyTransform(transform);
  
  m_normal = normalize(cross(m_yAxis, m_xAxis));
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
  assert(isApproxNormalized(m_xAxis, 0.01f));
  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  // Note: Ensure the Y axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_yAxis, 0.01f));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  // Note: Ensure the normal vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_normal, 0.01f));
  assert(m_normal < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.z));

  // Note: Unused space for rect lights
  writeGPUPadding<10>(data, offset);

  // Note: Rect light type (1) + shaping enabled flag
  uint32_t flags = lightTypeRect << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}


Vector4 RtRectLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

void RtRectLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Rect;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_dimensions[0], sizeof(m_dimensions), h);
  h = XXH64(&m_xAxis[0], sizeof(m_xAxis), h);
  h = XXH64(&m_yAxis[0], sizeof(m_yAxis), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());

  m_cachedHash = h;
}

RtDiskLight::RtDiskLight(const Vector3& position, const Vector2& halfDimensions, const Vector3& xAxis, const Vector3& yAxis, const Vector3& radiance, const RtLightShaping& shaping)
  : m_position(position)
  , m_halfDimensions(halfDimensions)
  , m_xAxis(xAxis)
  , m_yAxis(yAxis)
  , m_radiance(radiance)
  , m_shaping(shaping) {
  // Note: Cache a pre-computed normal vector to avoid doing it on the GPU since we have space in the Light to spare
  // Additionally, normalization required as sometimes a cross product may not result in a normalized vector due to precision issues.
  m_normal = normalize(cross(m_yAxis, m_xAxis));

  updateCachedHash();
}

void RtDiskLight::applyTransform(const Matrix4& lightToWorld) {
  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  const Matrix3 transform(lightToWorld);

  const Vector3 fullXAxis = Vector3(m_xAxis.x, m_xAxis.y, m_xAxis.z);
  m_xAxis = transform * fullXAxis;
  const float xAxisLen = length(m_xAxis);
  m_xAxis = m_xAxis / xAxisLen;
  m_halfDimensions.x *= xAxisLen;
  
  const Vector3 fullYAxis = Vector3(m_yAxis.x, m_yAxis.y, m_yAxis.z);
  m_yAxis = transform * fullYAxis;
  const float yAxisLen = length(m_yAxis);
  m_yAxis = m_yAxis / yAxisLen;
  m_halfDimensions.y *= yAxisLen;

  m_shaping.applyTransform(transform);

  m_normal = normalize(cross(m_yAxis, m_xAxis));
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
  assert(isApproxNormalized(m_xAxis, 0.01f));
  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  // Note: Ensure the Y axis vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_yAxis, 0.01f));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  assert(m_normal < Vector3(FLOAT16_MAX));
  // Note: Ensure the normal vector is normalized as this is a requirement for the GPU encoding.
  assert(isApproxNormalized(m_normal, 0.01f));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.z));

  // Note: Unused space for disk lights
  writeGPUPadding<10>(data, offset);

  // Note: Disk light type (2) + shaping enabled flag
  uint32_t flags = lightTypeDisk << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.getEnabled() ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtDiskLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

void RtDiskLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Disk;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_halfDimensions[0], sizeof(m_halfDimensions), h);
  h = XXH64(&m_xAxis[0], sizeof(m_xAxis), h);
  h = XXH64(&m_yAxis[0], sizeof(m_yAxis), h);
  h = XXH64(&h, sizeof(h), m_shaping.getHash());

  m_cachedHash = h;
}

RtCylinderLight::RtCylinderLight(const Vector3& position, float radius, const Vector3& axis, float axisLength, const Vector3& radiance)
  : m_position(position)
  , m_radius(radius)
  , m_axis(axis)
  , m_axisLength(axisLength)
  , m_radiance(radiance) {
  updateCachedHash();
}

void RtCylinderLight::applyTransform(const Matrix4& lightToWorld) {
  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  const Matrix3 transform(lightToWorld);

  m_axis = transform * m_axis;
  float axisScale = length(m_axis);
  m_axis = m_axis / axisScale;
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
  assert(isApproxNormalized(m_axis, 0.01f));
  assert(m_axis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_axis.z));

  // Note: Unused space for cylinder lights
  writeGPUPadding<22>(data, offset);

  // Note: Cylinder light type (3)
  writeGPUHelper(data, offset, static_cast<uint32_t>(lightTypeCylinder << 29));

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtCylinderLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

void RtCylinderLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Cylinder;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_position[0], sizeof(m_position), h);
  h = XXH64(&m_radius, sizeof(m_radius), h);
  h = XXH64(&m_axis[0], sizeof(m_axis), h);
  h = XXH64(&m_axisLength, sizeof(m_axisLength), h);

  m_cachedHash = h;
}

RtDistantLight::RtDistantLight(const Vector3& direction, float halfAngle, const Vector3& radiance, const XXH64_hash_t forceHash)
  // Note: Direction assumed to be normalized.
  : m_direction(direction)
  , m_halfAngle(halfAngle)
  , m_radiance(radiance) {

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

void RtDistantLight::applyTransform(const Matrix4& lightToWorld) {
  const Matrix3 transform(lightToWorld);

  m_direction = normalize(transform * m_direction);
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
  assert(isApproxNormalized(m_orientation, 0.01f));
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
  writeGPUPadding<20>(data, offset);

  // Note: Distant light type (4)
  // Todo: Ideally match this with GPU light type constants
  writeGPUHelper(data, offset, static_cast<uint32_t>(lightTypeDistant << 29));

  assert(offset - oldOffset == kLightGPUSize);
}

Vector4 RtDistantLight::getColorAndIntensity() const {
  return safeColorAndIntensity(m_radiance);
}

void RtDistantLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Distant;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_direction[0], sizeof(m_direction), h);
  h = XXH64(&m_halfAngle, sizeof(m_halfAngle), h);

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
    return m_sphereLight.getShaping().getPrimaryAxis();
  case RtLightType::Rect:
    return m_rectLight.getShaping().getPrimaryAxis();
  case RtLightType::Disk:
    return m_diskLight.getShaping().getPrimaryAxis();
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