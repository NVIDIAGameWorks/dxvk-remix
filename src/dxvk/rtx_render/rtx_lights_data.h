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

#include <memory>
#include <mutex>
#include <type_traits>

#include "rtx_lights.h"

#define LIST_LIGHT_CONSTANTS(X) \
  /*Parameter Name,   USD Token String,       Type,    Min Value,    Max Value,    Default Value */ \
  X(Radius,                  radius,                    float,   0.f,          kLightFloat16Max, 0.f) \
  X(Width,                   width,                     float,   0.f,          FLT_MAX,      0.f) \
  X(Height,                  height,                    float,   0.f,          FLT_MAX,      0.f) \
  X(Length,                  length,                    float,   0.f,          FLT_MAX,      0.f) \
  X(AngleRadians,            angle,                     float,   -FLT_MAX,     FLT_MAX,      0.f) \
  X(EnableColorTemp,         enableColorTemperature,    bool,    false,        true,         false) \
  X(Color,                   color,                     Vector3, Vector3(0.f), Vector3(1.f), Vector3(1.f)) \
  X(ColorTemp,               colorTemperature,          float,   0.f,          FLT_MAX,      6500.f) \
  X(Exposure,                exposure,                  float,   -FLT_MAX,     FLT_MAX,      0.f) \
  X(Intensity,               intensity,                 float,   0.f,          FLT_MAX,      1.f) \
  X(ConeAngleRadians,        shaping:cone:angle,        float,   -FLT_MAX,     FLT_MAX,      180.f * kDegreesToRadians) \
  X(ConeSoftness,            shaping:cone:softness,     float,   0.f,          kLightFloat16Max, 0.f) \
  X(Focus,                   shaping:focus,             float,   0.f,          kLightFloat16Max, 0.f) \
  X(VolumetricRadianceScale, volumetric_radiance_scale, float,   0.f,          kLightFloat16Max, 1.f)

#define LIST_GOBO_CONSTANTS(X) \
  /*Parameter Name,        Name under inputs:shaping:gobo,    RtLightGobo member */ \
  X(Scale,                 scale,                             scale) \
  X(Offset,                offset,                            offset) \
  X(Rotation,              rotation,                          rotationRadians) \
  X(Fov,                   fov,                               fovRadians) \
  X(Aspect,                aspect,                            aspect) \
  X(WrapMode,              wrap_mode,                         wrapMode) \
  X(Strength,              strength,                          strength) \
  X(Invert,                invert,                            invert) \
  X(UseAlpha,              use_alpha,                         useAlpha) \
  X(ColorSpace,            color_space,                       colorSpace) \
  X(ScrollSpeed,           scroll_speed,                      scrollSpeed) \
  X(RotationSpeed,         rotation_speed,                    rotationSpeedRadians) \
  X(ShakeAngle,            shake_angle,                       shakeAngleRadians) \
  X(ShakeFrequency,        shake_frequency,                   shakeFrequency) \
  X(TimeOffset,            time_offset,                       timeOffset) \
  X(TimeOffsetRandom,      time_offset_random,                timeOffsetRandom) \
  X(SpriteSheetRows,       sprite_sheet_rows,                 spriteSheetRows) \
  X(SpriteSheetCols,       sprite_sheet_cols,                 spriteSheetCols) \
  X(SpriteSheetFps,        sprite_sheet_fps,                  spriteSheetFps) \
  X(SpriteSheetFirstFrame, sprite_sheet_first_frame,          spriteSheetFirstFrame) \
  X(SpriteSheetFrameCount, sprite_sheet_frame_count,          spriteSheetFrameCount) \
  X(SpriteSheetBlend,      sprite_sheet_blend,                spriteSheetBlend) \
  X(SpriteSheetLoopMode,   sprite_sheet_loop_mode,            spriteSheetLoopMode)

#define WRITE_PARAMETER_MEMBERS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type m_##name = defaultVal;

#define WRITE_DIRTY_FLAGS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      k_##name,

#define WRITE_GOBO_DIRTY_FLAGS(name, usd_attr, member) \
      k_Gobo##name,

struct remixapi_LightInfoUSDEXT;

namespace dxvk {
  // The largest float below FLOAT16_MAX, since light validation and GPU encoding require values strictly below it.
  // Floats near 65504 are spaced 2^-8 apart.
  constexpr float kLightFloat16Max = FLOAT16_MAX - 0.00390625f;

  struct LightData {
    static std::optional<LightData> tryCreate(const D3DLIGHT9& light);
    static std::optional<LightData> tryCreate(const pxr::UsdPrim& lightPrim, const pxr::GfMatrix4f* pLocalToRoot, const bool isOverrideLight, const bool absoluteTransform);
    static std::optional<LightData> tryCreate(const remixapi_LightInfoUSDEXT& src);

    RtLight toRtLight() const;

    void merge(const D3DLIGHT9& light);

    static bool isSupportedUsdLight(const pxr::UsdPrim& lightPrim);

    // Do we transform this light relative to a game light?
    bool relativeTransform() const { return m_isRelativeTransform; }

    // Are we overriding an existing game light?
    bool lightOverride() const { return m_isOverrideLight; }

    void setTransform(const pxr::GfMatrix4f& localToObject) {
      extractTransform(&localToObject);
    }

    // The USD asset attribute holding the gobo texture. Gobo textures load through the replacement texture path,
    // so the texture is set separately from the other gobo attributes.
    static const pxr::TfToken& getGoboFileToken();

    void setGoboTexture(const TextureRef& texture);

  private:
    // Supported light data types
    enum LightType {
      Sphere,
      Rect,
      Disk,
      Cylinder,
      Distant,
      Unknown
    };

    explicit LightData(LightType lightType, bool isOverrideLight = false, bool absoluteTransform = true);

    static LightData createFromDirectional(const D3DLIGHT9& light);
    static LightData createFromPointSpot(const D3DLIGHT9& light);

    void merge(const LightData& input);

    static bool getLightType(const pxr::UsdPrim& lightPrim, LightType& typeOut);

    // USD transitioned from `intensity` to `inputs:intensity` for all its light attributes, we need to support content
    // authored before and after that change.
    const pxr::UsdAttribute getLightAttribute(const pxr::UsdPrim& prim, const pxr::TfToken& token, const pxr::TfToken& inputToken);

    void extractTransform(const pxr::GfMatrix4f* pLocalToRoot);

    void deserialize(const pxr::UsdPrim& prim);

    void deserializeGobo(const pxr::UsdPrim& prim);

    void sanitizeData();

    Vector3 calculateRadiance() const;

    bool isShapingEnabled() const;

    RtLightShaping getLightShaping(Vector3 zAxis) const;

    RtLight createRtLight() const;

    void attachGobo(RtLight& light) const;

    enum class DirtyFlags : uint64_t {
      LIST_LIGHT_CONSTANTS(WRITE_DIRTY_FLAGS)
      k_Transform,
      k_GoboTexture,
      LIST_GOBO_CONSTANTS(WRITE_GOBO_DIRTY_FLAGS)
      k_Count,
    };
    static_assert(static_cast<uint64_t>(DirtyFlags::k_Count) <= 64, "Too many light parameters for the dirty flags");

    // Note: Be very careful with what is passed to the Flags class's constructor. A bug previously existed where this all ones bit
    // pattern was a DirtyFlags enum value itself (e.g. AllDirty = 0xFFFFFFFF), causing it to call the wrong conversion constructor and being
    // interpreted as a bit index to set instead of a raw integer value to manually set the flags with. This caused great pain as shifting
    // by such a large number in the internal set function caused undefined behavior (as C++ does not allow shifting greater than the number
    // of bits in the type being shifted), which in turn caused all the flags to be cleared rather than all to be set.
    constexpr static Flags<DirtyFlags> m_allDirty{ ~static_cast<std::underlying_type_t<DirtyFlags>>(0) };

    LIST_LIGHT_CONSTANTS(WRITE_PARAMETER_MEMBERS)

    enum TransformType {
      Absolute,
      Relative
    };

    Flags<DirtyFlags> m_dirty{};
    LightType m_lightType = Unknown;
    XXH64_hash_t m_cachedHash = kEmptyHash;
    // NOTE: Just add params for these without USD deserializer
    Vector3 m_position = Vector3(0.f);
    Vector3 m_xAxis = Vector3(1.f, 0.f, 0.f);
    Vector3 m_yAxis = Vector3(0.f, 1.f, 0.f);
    Vector3 m_zAxis = Vector3(0.f, 0.f, 1.f);
    float m_xScale = 1.f, m_yScale = 1.f, m_zScale = 1.f;
    // Gobo parameters, see documentation/LightGobos.md.
    // Note: A field of view of zero means twice the shaping cone angle.
    RtLightGobo m_gobo;

    // The gobo that attachGobo last made, shared by the copies of this light data, so each conversion does not allocate one.
    // Note: Copies of the light data can convert on more than one thread, so the gobo is swapped under the mutex.
    struct GoboCache {
      std::mutex mutex;
      std::shared_ptr<const RtLightGobo> gobo;
    };
    std::shared_ptr<GoboCache> m_goboCache;

    const bool m_isOverrideLight = false;
    const bool m_isRelativeTransform = false;
  };
} // namespace dxvk

#undef WRITE_PARAMETER_MEMBERS
#undef WRITE_DIRTY_FLAGS
#undef WRITE_GOBO_DIRTY_FLAGS
