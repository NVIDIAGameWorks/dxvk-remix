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

#include "rtx_lights.h"

#define LIST_LIGHT_CONSTANTS(X) \
  /*Parameter Name,   USD Token String,       Type,    Min Value,    Max Value,    Default Value */ \
  X(Radius,           radius,                 float,   0.f,          FLOAT16_MAX,  0.f) \
  X(Width,            width,                  float,   0.f,          FLT_MAX,      0.f) \
  X(Height,           height,                 float,   0.f,          FLT_MAX,      0.f) \
  X(Length,           length,                 float,   0.f,          FLT_MAX,      0.f) \
  X(AngleRadians,     angle,                  float,   -FLT_MAX,     FLT_MAX,      0.f) \
  X(EnableColorTemp,  enableColorTemperature, bool,    false,        true,         false) \
  X(Color,            color,                  Vector3, Vector3(0.f), Vector3(1.f), Vector3(1.f)) \
  X(ColorTemp,        colorTemperature,       float,   0.f,          FLT_MAX,      6500.f) \
  X(Exposure,         exposure,               float,   0.f,          FLT_MAX,      0.f) \
  X(Intensity,        intensity,              float,   0.f,          FLT_MAX,      1.f) \
  X(ConeAngleRadians, shaping:cone:angle,     float,   -FLT_MAX,     FLT_MAX,      180.f * kDegreesToRadians) \
  X(ConeSoftness,     shaping:cone:softness,  float,   0.f,          FLOAT16_MAX,  0.f) \
  X(Focus,            shaping:focus,          float,   0.f,          FLOAT16_MAX,  0.f) 

#define WRITE_PARAMETER_MEMBERS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type m_##name = defaultVal;

#define WRITE_DIRTY_FLAGS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      k_##name,

namespace dxvk {
  struct LightData {
    static std::optional<LightData> tryCreate(const D3DLIGHT9& light);
    static std::optional<LightData> tryCreate(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f& localToRoot);

    RtLight toRtLight() const;

    void merge(const D3DLIGHT9& light);

  private:
    LightData() = default;
    LightData(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f& localToRoot);

    static LightData createFromDirectional(const D3DLIGHT9& light);

    static LightData createFromPointSpot(const D3DLIGHT9& light);

    void merge(const LightData& input, const bool useInputTransform = false);

    static RtLightType getLightType(const pxr::UsdPrim& lightPrim);

    // USD transitioned from `intensity` to `inputs:intensity` for all its light attributes, we need to support content
    // authored before and after that change.
    const pxr::UsdAttribute getLightAttribute(const pxr::UsdPrim& prim, const pxr::TfToken& token, const pxr::TfToken& inputToken);

    void extractTransform(const pxr::GfMatrix4f& localToRoot);

    void deserialize(const pxr::UsdPrim& prim);

    void sanitizeData();

    Vector3 calculateRadiance() const;

    RtLightShaping getLightShaping(Vector3 zAxis) const;

    enum class DirtyFlags : uint32_t {
      LIST_LIGHT_CONSTANTS(WRITE_DIRTY_FLAGS)
      k_Transform,
      AllDirty = 0xFFFFffff
    };
    
    LIST_LIGHT_CONSTANTS(WRITE_PARAMETER_MEMBERS)

    Flags<DirtyFlags> m_dirty { 0 };
    RtLightType m_lightType;
    XXH64_hash_t m_cachedHash = kEmptyHash;
    // NOTE: Just add params for these without USD deserializer
    Vector3 m_position = Vector3(0.f);
    Vector3 m_xAxis = Vector3(1.f, 0.f, 0.f);
    Vector3 m_yAxis = Vector3(0.f, 1.f, 0.f);
    Vector3 m_zAxis = Vector3(0.f, 0.f, 1.f);
    float m_xScale = 1.f, m_yScale = 1.f, m_zScale = 1.f;
  };
} // namespace dxvk

#undef WRITE_PARAMETER_MEMBERS
#undef WRITE_DIRTY_FLAGS
