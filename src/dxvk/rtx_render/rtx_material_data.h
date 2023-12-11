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

#include "../../lssusd/mdl_helpers.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usd/prim.h>
#include "../../lssusd/usd_include_end.h"

// Note: These material ranges and defaults should be kept in sync with the MDL ranges to prevent mismatching between how data is clamped.

// clang-format off
#define LIST_OPAQUE_MATERIAL_TEXTURES(X) \
  /*Parameter Name,                          USD Token String,                     Type,       UNUSED...   Default Value */ \
  X(AlbedoOpacityTexture,                    diffuse_texture,                      TextureRef, void, void, {}) \
  X(NormalTexture,                           normalmap_texture,                    TextureRef, void, void, {}) \
  X(TangentTexture,                          tangent_texture,                      TextureRef, void, void, {}) \
  X(HeightTexture,                           height_texture,                       TextureRef, void, void, {}) \
  X(RoughnessTexture,                        reflectionroughness_texture,          TextureRef, void, void, {}) \
  X(MetallicTexture,                         metallic_texture,                     TextureRef, void, void, {}) \
  X(EmissiveColorTexture,                    emissive_mask_texture,                TextureRef, void, void, {}) \
  X(SubsurfaceTransmittanceTexture,          subsurface_transmittance_texture,     TextureRef, void, void, {}) \
  X(SubsurfaceThicknessTexture,              subsurface_thickness_texture,         TextureRef, void, void, {}) \
  X(SubsurfaceSingleScatteringAlbedoTexture, subsurface_single_scattering_texture, TextureRef, void, void, {})


#define LIST_OPAQUE_MATERIAL_CONSTANTS(X) \
  /*Parameter Name,                   USD Token String,                       Type,           Min Value,                  Max Value,                 Default Value */ \
  X(AnisotropyConstant,               anisotropy,                             float,          0.f,                        1.f,                       0.f) \
  /* Note: Maximum clamped to float 16 max due to GPU encoding requirements. */ \
  X(EmissiveIntensity,                emissive_intensity,                     float,          0.f,                        65504.0f,                  40.f) \
  X(AlbedoConstant,                   diffuse_color_constant,                 Vector3,        Vector3(0.f),               Vector3(1.f),              Vector3(0.2f, 0.2f, 0.2f)) \
  X(OpacityConstant,                  opacity_constant,                       float,          0.f,                        1.f,                       1.f) \
  X(RoughnessConstant,                reflection_roughness_constant,          float,          0.f,                        1.f,                       .5f) \
  X(MetallicConstant,                 metallic_constant,                      float,          0.f,                        1.f,                       0.f) \
  X(EmissiveColorConstant,            emissive_color_constant,                Vector3,        Vector3(0.f),               Vector3(1.f),              Vector3(1.0f, 0.1f, 0.1f)) \
  X(EnableEmission,                   enable_emission,                        bool,           false,                      true,                      false) \
  X(SpriteSheetRows,                  sprite_sheet_rows,                      uint8_t,        0,                          255,                       0) \
  X(SpriteSheetCols,                  sprite_sheet_cols,                      uint8_t,        0,                          255,                       0) \
  X(SpriteSheetFPS,                   sprite_sheet_fps,                       uint8_t,        0,                          255,                       0) \
  X(EnableThinFilm,                   enable_thin_film,                       bool,           false,                      true,                      false) \
  X(AlphaIsThinFilmThickness,         thin_film_thickness_from_albedo_alpha,  bool,           false,                      true,                      false) \
  /* Note: Thickness cannot be 0 so should be kept above this minimum small value (though in practice it'll likely be   */ \
  /* quantized to 0 with values this small anyways, but it's good to be careful about it for potential future changes). */ \
  /* Note: Max thickness constant be less than the float 16 max due to float 16 usage on the GPU.                       */ \
  X(ThinFilmThicknessConstant,        thin_film_thickness_constant,           float,          .001f,                      OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS, 200.f) \
  X(UseLegacyAlphaState,              use_legacy_alpha_state,                 bool,           false,                      true,                      true) \
  X(BlendEnabled,                     blend_enabled,                          bool,           false,                      true,                      false) \
  X(BlendType,                        blend_type,                             BlendType,      BlendType::kMinValue,       BlendType::kMaxValue,      BlendType::kAlpha) \
  X(InvertedBlend,                    inverted_blend,                         bool,           false,                      true,                      false) \
  X(AlphaTestType,                    alpha_test_type,                        AlphaTestType,  AlphaTestType::kMinValue,   AlphaTestType::kMaxValue,  AlphaTestType::kAlways) \
  X(AlphaTestReferenceValue,          alpha_test_reference_value,             uint8_t,        0,                          255,                       0) \
  /* Note: Maximum clamped to float 16 max due to GPU encoding requirements. */ \
  X(DisplaceIn,                       displace_in,                            float,          0.f,                        65504.0f,                  0.f) \
  X(SubsurfaceTransmittanceColor,     subsurface_transmittance_color,         Vector3,        Vector3(0.f),               Vector3(1.f),              Vector3(0.5f, 0.5f, 0.5f)) \
  X(SubsurfaceMeasurementDistance,    subsurface_measurement_distance,        float,          0.f,                        65504.0f,                  0.f) \
  X(SubsurfaceSingleScatteringAlbedo, subsurface_single_scattering_albedo,    Vector3,        Vector3(0.f),               Vector3(1.f),              Vector3(0.5f, 0.5f, 0.5f)) \
  X(SubsurfaceVolumetricAnisotropy,   subsurface_volumetric_anisotropy,       float,          -1.f,                       1.f,                       0.f) \
  /* Sampler State */ \
  X(FilterMode,                       filter_mode,                            uint8_t,        lss::Mdl::Filter::Nearest,  lss::Mdl::Filter::Linear,  lss::Mdl::Filter::Nearest)  \
  X(WrapModeU,                        wrap_mode_u,                            uint8_t,        lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat) \
  X(WrapModeV,                        wrap_mode_v,                            uint8_t,        lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat)

#define LIST_OPAQUE_MATERIAL_PARAMS(X)\
  LIST_OPAQUE_MATERIAL_TEXTURES(X) \
  LIST_OPAQUE_MATERIAL_CONSTANTS(X)


#define LIST_TRANSLUCENT_MATERIAL_TEXTURES(X) \
  /*Parameter Name,       USD Token String,       Type,       UNUSED...   Default Value */ \
  X(NormalTexture,        normalmap_texture,      TextureRef, void, void, {}) \
  X(TransmittanceTexture, transmittance_texture,  TextureRef, void, void, {}) \
  X(EmissiveColorTexture, emissive_mask_texture,  TextureRef, void, void, {})

#define LIST_TRANSLUCENT_MATERIAL_CONSTANTS(X) \
  /* Note: IoR values less than 1 are physically impossible for typical translucent materials. */ \
  /* Note: 3 chosen due to virtually no physical materials having an IoR greater to this, and because this */ \
  /* is currently the maximum IoR value the GPU supports encoding of as well.                              */ \
  /*Parameter Name,                   USD Token String,                   Type,     Min Value,                  Max Value,                 Default Value */ \
  X(RefractiveIndex,                  ior_constant,                       float,    1.f,                        3.f,                       1.3f) \
  X(TransmittanceColor,               transmittance_color,                Vector3,  Vector3(0.f),               Vector3(1.f),              Vector3(0.97f, 0.97f, 0.97f)) \
  X(TransmittanceMeasurementDistance, transmittance_measurement_distance, float,    .001f,                      65504.0f,                  1.f) \
  X(EnableEmission,                   enable_emission,                    bool,     false,                      true,                      false) \
  /* Note: Maximum clamped to float 16 max due to GPU encoding requirements. */ \
  X(EmissiveIntensity,                emissive_intensity,                 float,    0.f,                        65504.0f,                  40.f) \
  X(EmissiveColorConstant,            emissive_color_constant,            Vector3,  Vector3(0.f),               Vector3(1.f),              Vector3(1.0f, 0.1f, 0.1f)) \
  X(SpriteSheetRows,                  sprite_sheet_rows,                  uint8_t,  0,                          255,                       0) \
  X(SpriteSheetCols,                  sprite_sheet_cols,                  uint8_t,  0,                          255,                       0) \
  X(SpriteSheetFPS,                   sprite_sheet_fps,                   uint8_t,  0,                          255,                       0) \
  X(EnableThinWalled,                 thin_walled,                        bool,     false,                      true,                      false) \
  /* Note: 0.001 to be safe around the minimum of float16 values, as well as due to the fact that we cut off */ \
  /* 2 bits of the value in some cases.                                                                      */ \
  /* Note: Maximum clamped to float 16 max due to GPU encoding requirements. */ \
  X(ThinWallThickness,                thin_wall_thickness,                float,    .001f,                      65504.0f,                  .001f) \
  X(EnableDiffuseLayer,               use_diffuse_layer,                  bool,     false,                      true,                      false) \
  /* Sampler State */ \
  X(FilterMode,                       filter_mode,                        uint8_t,  lss::Mdl::Filter::Nearest,  lss::Mdl::Filter::Linear,  lss::Mdl::Filter::Nearest)  \
  X(WrapModeU,                        wrap_mode_u,                        uint8_t,  lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat) \
  X(WrapModeV,                        wrap_mode_v,                        uint8_t,  lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat)

#define LIST_TRANSLUCENT_MATERIAL_PARAMS(X)\
  LIST_TRANSLUCENT_MATERIAL_TEXTURES(X) \
  LIST_TRANSLUCENT_MATERIAL_CONSTANTS(X)


#define LIST_PORTAL_MATERIAL_TEXTURES(X) \
  /*Parameter Name, USD Token String,       Type,       UNUSED...   Default Value */ \
  X(MaskTexture,    emissive_mask_texture,  TextureRef, void, void, {}) \
  X(MaskTexture2,   unused_in_usd_so_dont,  TextureRef, void, void, {})

#define LIST_PORTAL_MATERIAL_CONSTANTS(X) \
  /*Parameter Name,     USD Token String,   Type,     Min Value,                  Max Value,                 Default Value */ \
  X(RayPortalIndex,     portal_index,       uint8_t,  0,                          255,                       0) \
  X(SpriteSheetRows,    sprite_sheet_rows,  uint8_t,  0,                          255,                       0) \
  X(SpriteSheetCols,    sprite_sheet_cols,  uint8_t,  0,                          255,                       0) \
  X(SpriteSheetFPS,     sprite_sheet_fps,   uint8_t,  0,                          255,                       0) \
  X(RotationSpeed,      rotation_speed,     float,    0.f,                        65504.0f,                  0.f) \
  X(EnableEmission,     enable_emission,    bool,     false,                      true,                      false) \
  X(EmissiveIntensity,  emissive_intensity, float,    0.f,                        65504.0f,                  40.f) \
    /* Sampler State */ \
  X(FilterMode,         filter_mode,        uint8_t,  lss::Mdl::Filter::Nearest,  lss::Mdl::Filter::Linear,  lss::Mdl::Filter::Nearest) \
  X(WrapModeU,          wrap_mode_u,        uint8_t,  lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat) \
  X(WrapModeV,          wrap_mode_v,        uint8_t,  lss::Mdl::WrapMode::Clamp,  lss::Mdl::WrapMode::Clip,  lss::Mdl::WrapMode::Repeat)


#define LIST_PORTAL_MATERIAL_PARAMS(X)\
  LIST_PORTAL_MATERIAL_TEXTURES(X) \
  LIST_PORTAL_MATERIAL_CONSTANTS(X)
// clang-format on

#define WRITE_CTOR_ARGS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      const type& name,

#define WRITE_CTOR_INIT(name, usd_attr, type, minVal, maxVal, defaultVal) \
      m_##name{name},

#define WRITE_CONSTANT_MEMBER_FUNC(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type& get##name() { return m_##name; } \
      const type& get##name() const { return m_##name; } \
      static pxr::TfToken get##name##Token() { return pxr::TfToken("inputs:"#usd_attr); }

#define WRITE_TEXTURE_MEMBER_FUNC(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type& get##name() { return m_##name; } \
      const type& get##name() const { return m_##name; } \
      static pxr::TfToken get##name##Token() { return pxr::TfToken("inputs:"#usd_attr); }

#define WRITE_CONSTANT_DESERIALIZER(name, usd_attr, type, minVal, maxVal, defaultVal) \
      if(shader.HasAttribute(get##name##Token())) { \
        static_assert(uint64_t(DirtyFlags::k_##name) < 64); \
        target.m_dirty.set(DirtyFlags::k_##name); \
        pxr::VtValue val; \
        shader.GetAttribute(get##name##Token()).Get(&val); \
        if(!val.IsEmpty()) \
          target.m_##name = val.UncheckedGet<type>(); \
      }

#define WRITE_TEXTURE_DESERIALIZER(name, usd_attr, type, minVal, maxVal, defaultVal) \
      if(shader.HasAttribute(get##name##Token())) { \
        static_assert(uint64_t(DirtyFlags::k_##name) < 64); \
        target.m_dirty.set(DirtyFlags::k_##name); \
        target.m_##name = TextureRef(getTexture(shader, get##name##Token())); \
      }

#define WRITE_PARAMETER_MERGE(name, usd_attr, type, minVal, maxVal, defaultVal) \
      if(!m_dirty.test(DirtyFlags::k_##name)) { \
        m_##name = input.m_##name; \
      }

#define WRITE_CONSTANT_RANGES(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type Min##name = minVal; \
      type Max##name = maxVal;

#define WRITE_CONSTANT_SANITIZATION(name, usd_attr, type, minVal, maxVal, defaultVal) \
      m_##name = clamp(m_##name, ranges.Min##name, ranges.Max##name);

#define WRITE_TEXTURE_HASH(name, usd_attr, type, minVal, maxVal, defaultVal) \
      h ^= m_##name.getImageHash();

#define WRITE_CONSTANT_HASH(name, usd_attr, type, minVal, maxVal, defaultVal) \
      h = XXH64(&m_##name, sizeof(m_##name), h);

#define WRITE_PARAMETER_MEMBERS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      type m_##name = defaultVal;

#define WRITE_DIRTY_FLAGS(name, usd_attr, type, minVal, maxVal, defaultVal) \
      k_##name,


#define REMIX_MATERIAL(name, X_CONSTANTS, X_TEXTURES, X_PARAMS)                                      \
struct name##Data {                                                                                  \
  /* Instantiates a material with all parameters set to default values */                            \
  name##Data() = default;                                                                            \
  /* Instantiates a material, must explicitly set all parameters */                                  \
  name##Data(                                                                                        \
    X_PARAMS(WRITE_CTOR_ARGS)                                                                        \
    uint64_t dirtyFlags = 0                                                                          \
  )                                                                                                  \
    : X_PARAMS(WRITE_CTOR_INIT)                                                                      \
    m_dirty {dirtyFlags}                                                                             \
  {                                                                                                  \
    sanitizeData();                                                                                  \
    /* Note: Called after data is sanitized to have the hashed value reflect the adjusted values. */ \
    updateCachedHash();                                                                              \
  }                                                                                                  \
                                                                                                     \
  X_CONSTANTS(WRITE_CONSTANT_MEMBER_FUNC)                                                            \
  X_TEXTURES(WRITE_TEXTURE_MEMBER_FUNC)                                                              \
                                                                                                     \
  template<typename F>                                                                               \
  static name##Data deserialize(const F& getTexture, const pxr::UsdPrim& shader) {                   \
    name##Data target;                                                                               \
    X_CONSTANTS(WRITE_CONSTANT_DESERIALIZER)                                                         \
    X_TEXTURES(WRITE_TEXTURE_DESERIALIZER)                                                           \
    target.sanitizeData();                                                                           \
    /* Note: Called after data is sanitized to have the hashed value reflect the adjusted values. */ \
    target.updateCachedHash();                                                                       \
    return target;                                                                                   \
  }                                                                                                  \
                                                                                                     \
  void merge(const name##Data& input)  {                                                             \
    X_PARAMS(WRITE_PARAMETER_MERGE)                                                                  \
    updateCachedHash();                                                                              \
  }                                                                                                  \
                                                                                                     \
  const XXH64_hash_t getHash() const {                                                               \
    return m_cachedHash;                                                                             \
  }                                                                                                  \
                                                                                                     \
private:                                                                                             \
                                                                                                     \
  struct Ranges {                                                                                    \
    X_CONSTANTS(WRITE_CONSTANT_RANGES)                                                               \
  };                                                                                                 \
                                                                                                     \
  /* Note: Ensures the data falls within the desired valid ranges in */                              \
  /*       case its source was malformed (e.g. manual USD editing).  */                              \
  void sanitizeData() {                                                                              \
    constexpr Ranges ranges = {};                                                                    \
    X_CONSTANTS(WRITE_CONSTANT_SANITIZATION)                                                         \
  }                                                                                                  \
                                                                                                     \
  void updateCachedHash() {                                                                          \
    XXH64_hash_t h = 0;                                                                              \
    X_TEXTURES(WRITE_TEXTURE_HASH)                                                                   \
    X_CONSTANTS(WRITE_CONSTANT_HASH)                                                                 \
    m_cachedHash = h;                                                                                \
  }                                                                                                  \
                                                                                                     \
  X_PARAMS(WRITE_PARAMETER_MEMBERS)                                                                  \
                                                                                                     \
  enum class DirtyFlags : uint64_t {                                                                 \
    X_PARAMS(WRITE_DIRTY_FLAGS)                                                                      \
  };                                                                                                 \
                                                                                                     \
  Flags<DirtyFlags> m_dirty { 0 };                                                                   \
  XXH64_hash_t m_cachedHash { 0 };                                                                   \
};

namespace dxvk {
  REMIX_MATERIAL(OpaqueMaterial, LIST_OPAQUE_MATERIAL_CONSTANTS, LIST_OPAQUE_MATERIAL_TEXTURES, LIST_OPAQUE_MATERIAL_PARAMS)
  REMIX_MATERIAL(TranslucentMaterial, LIST_TRANSLUCENT_MATERIAL_CONSTANTS, LIST_TRANSLUCENT_MATERIAL_TEXTURES, LIST_TRANSLUCENT_MATERIAL_PARAMS)
  REMIX_MATERIAL(RayPortalMaterial, LIST_PORTAL_MATERIAL_CONSTANTS, LIST_PORTAL_MATERIAL_TEXTURES, LIST_PORTAL_MATERIAL_PARAMS)
}

#undef LIST_OPAQUE_MATERIAL_TEXTURES
#undef LIST_OPAQUE_MATERIAL_CONSTANTS
#undef LIST_OPAQUE_MATERIAL_PARAMS
#undef LIST_TRANSLUCENT_MATERIAL_TEXTURES
#undef LIST_TRANSLUCENT_MATERIAL_CONSTANTS
#undef LIST_TRANSLUCENT_MATERIAL_PARAMS
#undef LIST_PORTAL_MATERIAL_TEXTURES
#undef LIST_PORTAL_MATERIAL_CONSTANTS
#undef LIST_PORTAL_MATERIAL_PARAMS

#undef WRITE_CTOR_ARGS
#undef WRITE_CTOR_INIT
#undef WRITE_MEMBER_FUNC
#undef WRITE_CONSTANT_DESERIALIZER
#undef WRITE_TEXTURE_DESERIALIZER
#undef WRITE_PARAMETER_MERGE
#undef WRITE_CONSTANT_RANGES
#undef WRITE_CONSTANT_SANITIZATION
#undef WRITE_TEXTURE_HASH
#undef WRITE_CONSTANT_HASH
#undef WRITE_PARAMETER_MEMBERS
#undef WRITE_DIRTY_FLAGS

#undef REMIX_MATERIAL
