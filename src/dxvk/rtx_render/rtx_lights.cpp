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
#include "rtx_lightmanager.h"

namespace dxvk {

static float leastSquareIntensity(float intensity, float attenuation2, float attenuation1, float attenuation0, float range) {
  // Calculate the distance where light intensity attenuates to 10%
  constexpr float kEpsilon = 0.000001f;
  float lowRange = 0.0f;
  const float lowThreshold = 0.1f;
  if (attenuation2 < kEpsilon) {
    if (attenuation1 > kEpsilon) {
      lowRange = (1.0f / lowThreshold - attenuation0) / attenuation1;
    }
  } else {
    float a = attenuation2;
    float b = attenuation1;
    float c = attenuation0 - 1.0f / lowThreshold;
    float discriminant = b * b - 4.0 * a * c;
    if (discriminant >= 0) {
      const float sqRoot = std::sqrt(discriminant);
      const float root1 = (-b + sqRoot) / (2 * a);
      const float root2 = (-b - sqRoot) / (2 * a);
      if (root1 > 0) {
        lowRange = root1;
      }
      if (root2 > 0) {
        lowRange = root2;
      }
    }
  }

  // Calculate the sample range
  if (lowRange > 0) {
    range = std::min(range, lowRange);
  }

  // Place 5 samples between [0, range]
  // Find newIntensity to minimize the error = Sigma((intensity / (a2*xi*xi + a1*xi + a0) - newIntensity / (xi * xi))^2)
  const int kSamples = 5;
  float numerator = 0;
  float denominator = 0;

  for (int i = 0; i < kSamples; i++) {
    float xi = float(i + 1) / kSamples * range;
    float xi2 = xi * xi;
    float xi4 = xi2 * xi2;
    float Ii = intensity / (attenuation2 * xi2 + attenuation1 * xi + attenuation0);
    numerator += Ii / xi2;
    denominator += 1 / xi4;
  }

  float newIntensity = numerator / denominator;
  return newIntensity;
}

static float intensityToEndDistance(float intensity) {
  float endDistanceSq = intensity / kLegacyLightEndValue;
  return sqrt(endDistanceSq);
}

static float solveQuadraticEndDistance(float originalBrightness, float attenuation2, float attenuation1, float attenuation0, float range) {
  const float a = attenuation2;
  const float b = attenuation1;
  const float c = attenuation0;
  float endDistance = 0.0f;

  // Solve for kLegacyLightEndValue using quadratic equation.
  // originalBrightness/(a*d*d+b*d+c) = kLegacyLightEndValue
  // a*d*d+b*d+c-originalBrightness/kLegacyLightEndValue = 0 
  const float newC = c - originalBrightness / kLegacyLightEndValue;
  const float discriminant = b * b - 4 * a * newC;

  if (discriminant < 0) {
    // Attenuation never reaches kLegacyLightEndValue.  Just use range.
    endDistance = range;
  } else if (discriminant == 0) {
    const float root = -b / (2 * a);
    if (root > 0) {
      endDistance = root;
    }
  } else {
    // Two roots, use the smaller positive root.
    const float sqRoot = std::sqrt(discriminant);
    const float root1 = (-b + sqRoot) / (2 * a);
    const float root2 = (-b - sqRoot) / (2 * a);
    if (root1 > 0) {
      endDistance = root1;
    }
    if (root2 > 0) {
      endDistance = root2;
    }
  }

  return endDistance;
}

// Function to calculate a radiance value from a light.
// This function will determine the distance from `light` that the brightness would fall below kLegacyLightEndValue, based on the attenuation function.
// If the light would never attenuate to less than kLegacyLightEndValue, light.Range will be used instead.
// It will then determine how bright the replacement light needs to be to have a brightness of kNewLightEndValue at the same distance.
// Finally, it will combine that brightness with the original light's diffuse color to determine the radiance.
static Vector3 calculateRadiance(const D3DLIGHT9& light, const float radius) {
  constexpr float kEpsilon = 0.000001f;

  // Calculate max distance based on attenuation.  We're looking to find when the light's attenuation is kLegacyLightEndValue.
  // Attenuation in D3D9 for lights is calculated as 1/(light.Attenuation2*d*d + light.Attenuation1*d + light.Attenuation).
  // This is calculated with respect to the max component of the light's 3 color components, and then is translated to RGB with the normalized color later.
  // Note that the calculated max distance may be greater than the Light's original "Range" value. This is because often times in older games the
  // Range was merely used in conjunction with a custom large color value and attenuation curve as an optimization to keep very bright lights from extending
  // across the entire level when only needed in a small area, but physical lights must reflect the "intended" full max distance as calculated by the attenuation.
  const float a = light.Attenuation2;
  const float b = light.Attenuation1;
  const float c = light.Attenuation0;

  const float originalBrightness = std::max(light.Diffuse.r, std::max(light.Diffuse.g, light.Diffuse.b));

  float endDistance = light.Range;

  if (c > 0 && originalBrightness / c < kLegacyLightEndValue) {
    // Light constant is already lower than our minimum right next to the light, so just set the radiance to 0.
    endDistance = 0.f;
  } else if (a < kEpsilon) {
    // No squared attenuation term
    if (b > kEpsilon) {
      // linear falloff
      if (LightManager::calculateLightIntensityUsingLeastSquares()) {
        endDistance = intensityToEndDistance(leastSquareIntensity(originalBrightness, light.Attenuation2, light.Attenuation1, light.Attenuation0, light.Range));
      } else {
        // 1/(b*d + c) = kLegacyLightEndValue
        endDistance = ((originalBrightness / kLegacyLightEndValue) - c) / b;
      }
    } else {
      // No falloff - the light is at full power * c until the range runs out
      // TODO may want to do something different here - the light is still fully bright at light.Range...
    }
  } else {
    if (LightManager::calculateLightIntensityUsingLeastSquares()) {
      endDistance = intensityToEndDistance(leastSquareIntensity(originalBrightness, light.Attenuation2, light.Attenuation1, light.Attenuation0, light.Range));
    } else {
      endDistance = solveQuadraticEndDistance(originalBrightness, light.Attenuation2, light.Attenuation1, light.Attenuation0, light.Range);
    }
  }

  // Calculate the radiance of the Sphere light to reach the desired perceptible radiance threshold at the calculated range of the D3D light.
  const float endDistanceSq = endDistance * endDistance;

  // Conversion factor from a desired distance squared to a radiance value based on a desired fixed light radius and the desired ending radiance value.
  // Derivation:
  // t = Threshold (ending) radiance value
  // i = Point Light Intensity
  // d = Distance
  // p = Power
  // r = Radiance
  // 
  // i / d^2 = t (Inverse square law for intensity, solving for d to find the intensity of a point light to reach this radiance threshold)
  // p = i * 4 * pi (Point Light Intensity to Power)
  // r = p / ((4 * pi * r^2) * pi) (Power to Sphere Light Radiance)
  // r = (d^2 * t) / (pi * r^2) (Solve and Substitute)
  const float kDistanceSqToRadiance = kNewLightEndValue / (kPi * radius * radius);

  const float radiance = kDistanceSqToRadiance * endDistanceSq;

  // Convert the max component radiance to RGB using the normalized color of the light.
  // Note: Many old games did their lighting entierly in gamma space (when sRGB textures and framebuffers were absent),
  // meaning while the normalized light color value should be converted from gamma to linear space to have the lighting look more
  // physically correct, this changes the look of lighting too much (which makes artists unhappy), so it is left unchanged.
  // In the future a conversion may be needed if gamma corrected framebuffers were used in the original game, but for now this is fine.
  Vector3 result;
  result[0] = light.Diffuse.r / originalBrightness * radiance;
  result[1] = light.Diffuse.g / originalBrightness * radiance;
  result[2] = light.Diffuse.b / originalBrightness * radiance;

  return result;
}


XXH64_hash_t RtLightShaping::getHash() const {
  XXH64_hash_t h = 0;

  if (enabled) {
    h = XXH64(&primaryAxis[0], sizeof(primaryAxis), h);
    h = XXH64(&cosConeAngle, sizeof(cosConeAngle), h);
    h = XXH64(&coneSoftness, sizeof(coneSoftness), h);
    h = XXH64(&focusExponent, sizeof(focusExponent), h);
  }

  return h;
}

void RtLightShaping::applyTransform(Matrix3 transform) {
  primaryAxis = normalize(transform * primaryAxis);
}

void RtLightShaping::writeGPUData(unsigned char* data, std::size_t& offset) const {
  // occupies 12 bytes
  if (enabled) {
    assert(primaryAxis < Vector3(FLOAT16_MAX));
    writeGPUHelper(data, offset, glm::packHalf1x16(primaryAxis.x));
    writeGPUHelper(data, offset, glm::packHalf1x16(primaryAxis.y));
    writeGPUHelper(data, offset, glm::packHalf1x16(primaryAxis.z));

    assert(cosConeAngle < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(1.0f - cosConeAngle));
    assert(coneSoftness < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(coneSoftness));
    assert(focusExponent < FLOAT16_MAX);
    writeGPUHelper(data, offset, glm::packHalf1x16(focusExponent));
  } else {
    writeGPUPadding<12>(data, offset);
  }
}

RtSphereLight::RtSphereLight(const Vector3& position, const Vector3& radiance, float radius, const RtLightShaping& shaping)
  : m_position(position)
  , m_radiance(radiance)
  , m_radius(radius)
  , m_shaping(shaping) {
  updateCachedHash();
}

RtSphereLight::RtSphereLight(const D3DLIGHT9& light) {
  m_position[0] = light.Position.x;
  m_position[1] = light.Position.y;
  m_position[2] = light.Position.z;

  m_radius = LightManager::lightConversionSphereLightFixedRadius() * RtxOptions::Get()->getSceneScale();
  m_radiance = calculateRadiance(light, m_radius);

  if (light.Type == D3DLIGHT_SPOT) {
    m_shaping.enabled = true;
    m_shaping.primaryAxis[0] = light.Direction.x;
    m_shaping.primaryAxis[1] = light.Direction.y;
    m_shaping.primaryAxis[2] = light.Direction.z;
    // cosConeAngle is the outer angle of the spotlight
    m_shaping.cosConeAngle = std::cosf(light.Phi / 2.0f);
    // coneSoftness is how far in the transition region reaches
    m_shaping.coneSoftness = std::cosf(light.Theta / 2.0f) - m_shaping.cosConeAngle;
    m_shaping.focusExponent = light.Falloff;
  }

  updateCachedHash();
}

void RtSphereLight::applyTransform(const Matrix4& lightToWorld) {
  const Vector4 fullPos = Vector4(m_position.x, m_position.y, m_position.z, 1.0f);
  m_position = (lightToWorld * fullPos).xyz();

  const Matrix3 transform(lightToWorld);

  // scale radius by average of the 3 axes.
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
  flags |= m_shaping.enabled ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

bool RtSphereLight::operator==(const RtSphereLight& rhs) const {
  return lengthSqr(m_position - rhs.m_position) < (LightManager::lightConversionEqualityDistanceThreshold() * LightManager::lightConversionEqualityDistanceThreshold());
}

Vector4 RtSphereLight::getColorAndIntensity() const {
  Vector4 out;
  out.w = std::max(std::max(m_radiance[0], m_radiance[1]), m_radiance[2]);
  out.x = m_radiance[0] / out.w;
  out.y = m_radiance[1] / out.w;
  out.z = m_radiance[2] / out.w;
  return out;
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
  m_normal = cross(m_yAxis, m_xAxis);
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
  
  m_normal = cross(m_yAxis, m_xAxis);
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

  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  assert(m_normal < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.z));

  // Note: Unused space for rect lights
  writeGPUPadding<10>(data, offset);

  // Note: Rect light type (1) + shaping enabled flag
  uint32_t flags = lightTypeRect << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.enabled ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

bool RtRectLight::operator==(const RtRectLight& rhs) const {
  return lengthSqr(m_position - rhs.m_position) < (LightManager::lightConversionEqualityDistanceThreshold() * LightManager::lightConversionEqualityDistanceThreshold());
}

Vector4 RtRectLight::getColorAndIntensity() const {
  Vector4 out;
  out.w = std::max(std::max(m_radiance[0], m_radiance[1]), m_radiance[2]);
  out.x = m_radiance[0] / out.w;
  out.y = m_radiance[1] / out.w;
  out.z = m_radiance[2] / out.w;
  return out;
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
  m_normal = cross(m_yAxis, m_xAxis);
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

  m_normal = cross(m_yAxis, m_xAxis);
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

  assert(m_xAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_xAxis.z));
  assert(m_yAxis < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_yAxis.z));
  assert(m_normal < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_normal.z));

  // Note: Unused space for disk lights
  writeGPUPadding<10>(data, offset);

  // Note: Disk light type (2) + shaping enabled flag
  uint32_t flags = lightTypeDisk << 29; // Light Type at bits 29,30,31.
  flags |= m_shaping.enabled ? 1 << 0 : 0 << 0; // Shaping enabled flag at bit 0
  writeGPUHelper(data, offset, flags);

  assert(offset - oldOffset == kLightGPUSize);
}

bool RtDiskLight::operator==(const RtDiskLight& rhs) const {
  return lengthSqr(m_position - rhs.m_position) < (LightManager::lightConversionEqualityDistanceThreshold() * LightManager::lightConversionEqualityDistanceThreshold());
}

Vector4 RtDiskLight::getColorAndIntensity() const {
  Vector4 out;
  out.w = std::max(std::max(m_radiance[0], m_radiance[1]), m_radiance[2]);
  out.x = m_radiance[0] / out.w;
  out.y = m_radiance[1] / out.w;
  out.z = m_radiance[2] / out.w;
  return out;
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

bool RtCylinderLight::operator==(const RtCylinderLight& rhs) const {
  return lengthSqr(m_position - rhs.m_position) < (LightManager::lightConversionEqualityDistanceThreshold() * LightManager::lightConversionEqualityDistanceThreshold());
}

Vector4 RtCylinderLight::getColorAndIntensity() const {
  Vector4 out;
  out.w = std::max(std::max(m_radiance[0], m_radiance[1]), m_radiance[2]);
  out.x = m_radiance[0] / out.w;
  out.y = m_radiance[1] / out.w;
  out.z = m_radiance[2] / out.w;
  return out;
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

RtDistantLight::RtDistantLight(const Vector3& direction, float halfAngle, const Vector3& radiance)
  // Note: Direction assumed to be normalized.
  : m_direction(direction)
  , m_halfAngle(halfAngle)
  , m_radiance(radiance) {

  // Note: Cache a pre-computed orientation quaternion to avoid doing it on the GPU since we have space in the Light to spare
  m_orientation = getOrientation(Vector3(0.0f, 0.0f, 1.0f), m_direction);

  // Note: Cache sine and cosine of the half angle to avoid doing it on the GPU as well
  m_cosHalfAngle = std::cos(m_halfAngle);
  m_sinHalfAngle = std::sin(m_halfAngle);

  updateCachedHash();
}

RtDistantLight::RtDistantLight(const D3DLIGHT9& light) {
  // Note: Assumed to be normalized
  m_direction[0] = light.Direction.x;
  m_direction[1] = light.Direction.y;
  m_direction[2] = light.Direction.z;

  m_halfAngle = LightManager::lightConversionDistantLightFixedAngle() / 2.f;

  const float fixedIntensity = LightManager::lightConversionDistantLightFixedIntensity();

  m_radiance[0] = light.Diffuse.r * fixedIntensity;
  m_radiance[1] = light.Diffuse.g * fixedIntensity;
  m_radiance[2] = light.Diffuse.b * fixedIntensity;

  // Note: Cache a pre-computed orientation quaternion to avoid doing it on the GPU since we have space in the Light to spare
  m_orientation = getOrientation(Vector3(0.0f, 0.0f, 1.0f), m_direction);

  // Note: Cache sine and cosine of the half angle to avoid doing it on the GPU as well
  m_cosHalfAngle = std::cos(m_halfAngle);
  m_sinHalfAngle = std::sin(m_halfAngle);

  updateCachedHash();
}

void RtDistantLight::applyTransform(const Matrix4& lightToWorld) {
  const Matrix3 transform(lightToWorld);
  m_direction = normalize(transform * m_direction);
  updateCachedHash();
}

void RtDistantLight::writeGPUData(unsigned char* data, std::size_t& offset) const {
  [[maybe_unused]] const std::size_t oldOffset = offset;

  assert(m_direction < Vector3(FLOAT16_MAX));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.x));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.y));
  writeGPUHelper(data, offset, glm::packHalf1x16(m_direction.z));
  assert(m_orientation < Vector4(FLOAT16_MAX));
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

bool RtDistantLight::operator==(const RtDistantLight& rhs) const {
  return dot(m_direction, rhs.m_direction) > (LightManager::lightConversionEqualityDistanceThreshold() * LightManager::lightConversionEqualityDistanceThreshold());
}

Vector4 RtDistantLight::getColorAndIntensity() const {
  Vector4 out;
  out.w = std::max(std::max(m_radiance[0], m_radiance[1]), m_radiance[2]);
  out.x = m_radiance[0] / out.w;
  out.y = m_radiance[1] / out.w;
  out.z = m_radiance[2] / out.w;
  return out;
}

void RtDistantLight::updateCachedHash() {
  XXH64_hash_t h = (XXH64_hash_t)RtLightType::Distant;

  // Note: Radiance not included to somewhat uniquely identify lights when constructed
  // from D3D9 Lights.
  h = XXH64(&m_direction[0], sizeof(m_direction), h);
  h = XXH64(&m_halfAngle, sizeof(m_halfAngle), h);

  m_cachedHash = h;
}

std::optional<RtLight> RtLight::TryCreate(const D3DLIGHT9& light) {
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

  return std::optional<RtLight>(std::in_place, light);
}

RtLight::RtLight() { 
  // Dummy light - this is used when lights are removed from the list to keep persistent ordering.
  m_type = RtLightType::Sphere;
  new (&m_sphereLight) RtSphereLight();
  m_cachedInitialHash = m_sphereLight.getHash();
}

RtLight::RtLight(const RtSphereLight& light) { 
  m_type = RtLightType::Sphere;
  new (&m_sphereLight) RtSphereLight(light);
  m_cachedInitialHash = m_sphereLight.getHash();
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

RtLight::RtLight(const D3DLIGHT9& light) {
  switch (light.Type) {
  default:
    assert(false);
  case D3DLIGHT_POINT:
  case D3DLIGHT_SPOT:
    new (&m_sphereLight) RtSphereLight{ light };
    m_cachedInitialHash = m_sphereLight.getHash();
    m_type = RtLightType::Sphere;
    break;
  case D3DLIGHT_DIRECTIONAL:
    new (&m_distantLight) RtDistantLight{ light };
    m_cachedInitialHash = m_distantLight.getHash();
    m_type = RtLightType::Distant;
    break;
  }
}

RtLight::RtLight(const RtLight& light)
  : m_type(light.m_type)
  , m_cachedInitialHash(light.m_cachedInitialHash)
  , m_rootInstanceId(light.m_rootInstanceId)
  , m_frameLastTouched(light.m_frameLastTouched)
  , m_bufferIdx(light.m_bufferIdx)
  , isDynamic(light.isDynamic) {
  switch (m_type) {
  default:
    assert(false);
  case RtLightType::Sphere:
    new (&m_sphereLight) RtSphereLight{ light.m_sphereLight };
    break;
  case RtLightType::Rect:
    new (&m_rectLight) RtRectLight{ light.m_rectLight };
    break;
  case RtLightType::Disk:
    new (&m_diskLight) RtDiskLight{ light.m_diskLight };
    break;
  case RtLightType::Cylinder:
    new (&m_cylinderLight) RtCylinderLight{ light.m_cylinderLight };
    break;
  case RtLightType::Distant:
    new (&m_distantLight) RtDistantLight{ light.m_distantLight };
    break;
  }
}

RtLight::~RtLight() {
  switch (m_type) {
  default:
    assert(false);
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

RtLight& RtLight::operator=(const RtLight& rtLight) {
  if (this != &rtLight) {
    m_type = rtLight.m_type;

    switch (rtLight.m_type) {
    default:
      assert(false);
    case RtLightType::Sphere:
      m_sphereLight = rtLight.m_sphereLight;
      break;
    case RtLightType::Rect:
      m_rectLight = rtLight.m_rectLight;
      break;
    case RtLightType::Disk:
      m_diskLight = rtLight.m_diskLight;
      break;
    case RtLightType::Cylinder:
      m_cylinderLight = rtLight.m_cylinderLight;
      break;
    case RtLightType::Distant:
      m_distantLight = rtLight.m_distantLight;
      break;
    }

    m_frameLastTouched = rtLight.m_frameLastTouched;
    m_cachedInitialHash = rtLight.m_cachedInitialHash;
    m_rootInstanceId = rtLight.m_rootInstanceId;
    m_bufferIdx = rtLight.m_bufferIdx;
    isDynamic = rtLight.isDynamic;
  }

  return *this;
}

bool RtLight::operator==(const RtLight& rhs) const {
  // Note: Different light types are not the same light so comparison can return false
  if (m_type != rhs.m_type) {
    return false;
  }

  switch (m_type) {
  default:
    assert(false);
  case RtLightType::Sphere:
    return m_sphereLight == rhs.m_sphereLight;
  case RtLightType::Rect:
    return m_rectLight == rhs.m_rectLight;
  case RtLightType::Disk:
    return m_diskLight == rhs.m_diskLight;
  case RtLightType::Cylinder:
    return m_cylinderLight == rhs.m_cylinderLight;
  case RtLightType::Distant:
    return m_distantLight == rhs.m_distantLight;
  }
}

Vector4 RtLight::getColorAndIntensity() const {
  switch (m_type) {
  default:
    assert(false);
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
  case RtLightType::Sphere:
  case RtLightType::Rect:
  case RtLightType::Disk:
  case RtLightType::Cylinder:
    return Vector3();
  case RtLightType::Distant:
    return m_distantLight.getDirection();
  }
}

XXH64_hash_t RtLight::getTransformedHash() const {
  switch (m_type) {
  default:
    assert(false);
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