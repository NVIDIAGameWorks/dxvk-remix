/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "../util/util_error.h"

#include <bitset>
#include <cstddef>

#include "usd_include_begin.h"
#include <pxr/pxr.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include "../usd-plugins/RemixParticleSystem/particleSystemAPI.h"
#include "usd_include_end.h"
#include "../../public/include/remix/remix_c.h"
#include "../dxvk/shaders/rtx/pass/particles/particle_system_enums.h"

namespace lss {
  template<typename TDest, typename TSrc>
  inline TDest AssignFromPrimvar(const TSrc& src) {
    return static_cast<TDest>(src);
  }

  template<>
  inline remixapi_Float4D AssignFromPrimvar<remixapi_Float4D, pxr::GfVec4f>(const pxr::GfVec4f& src) {
    return { src[0], src[1], src[2], src[3] };
  }
  
  template<>
  inline remixapi_Float3D AssignFromPrimvar<remixapi_Float3D, pxr::GfVec3f>(const pxr::GfVec3f& src) {
    return { src[0], src[1], src[2] };
  }

  template<>
  inline remixapi_Float2D AssignFromPrimvar<remixapi_Float2D, pxr::GfVec2f>(const pxr::GfVec2f& src) {
    return { src[0], src[1] };
  }

  template<>
  inline ParticleBillboardType AssignFromPrimvar<ParticleBillboardType, pxr::TfToken>(const pxr::TfToken& token) {
    static pxr::RemixTokensType tokens;
    if (token == tokens.faceCamera_UpAxisLocked) {
      return FaceCamera_UpAxisLocked;
    }
    if (token == tokens.faceCamera_Position) {
      return FaceCamera_Position;
    }
    if (token == tokens.faceWorldUp) {
      return FaceWorldUp;
    }
    return FaceCamera_Spherical;
  }

  template<>
  inline ParticleSpriteSheetMode AssignFromPrimvar<ParticleSpriteSheetMode, pxr::TfToken>(const pxr::TfToken& token) {
    static pxr::RemixTokensType tokens;
    if (token == tokens.overrideMaterial_Lifetime) {
      return OverrideMaterial_Lifetime;
    }
    if (token == tokens.overrideMaterial_Random) {
      return OverrideMaterial_Random;
    }
    return UseMaterialSpriteSheet;
  }

  template<>
  inline ParticleCollisionMode AssignFromPrimvar<ParticleCollisionMode, pxr::TfToken>(const pxr::TfToken& token) {
    static pxr::RemixTokensType tokens;
    if (token == tokens.stop) {
      return ParticleCollisionMode::Stop;
    }
    if (token == tokens.kill) {
      return ParticleCollisionMode::Kill;
    }
    return ParticleCollisionMode::Bounce;
  }

  template<>
  inline ParticleRandomFlipAxis AssignFromPrimvar<ParticleRandomFlipAxis, pxr::TfToken>(const pxr::TfToken& token) {
    static pxr::RemixTokensType tokens;
    if (token == tokens.vertical) {
      return ParticleRandomFlipAxis::Vertical;
    }
    if (token == tokens.horizontal) {
      return ParticleRandomFlipAxis::Horizontal;
    }
    if (token == tokens.both) {
      return ParticleRandomFlipAxis::Both;
    }
    return ParticleRandomFlipAxis::None;
  }

  template<typename T>
  static bool ConvertPrimvarValue(const pxr::VtValue& v, T& out) {
    using namespace pxr;

    if (v.IsEmpty()) {
      return false;
    }

    if constexpr (std::is_same_v<T, GfVec2f>) {
      if (v.IsHolding<GfVec2f>()) {
        out = v.UncheckedGet<GfVec2f>();
        return true;
      }
      if (v.IsHolding<float>()) {
        float s = v.UncheckedGet<float>();
        out = GfVec2f(s, s);
        return true;
      }
      return false;
    } else if constexpr (std::is_same_v<T, GfVec3f>) {
      if (v.IsHolding<GfVec3f>()) {
        out = v.UncheckedGet<GfVec3f>();
        return true;
      }
      if (v.IsHolding<float>()) {
        float s = v.UncheckedGet<float>();
        out = GfVec3f(s, s, s);
        return true;
      }
      return false;
    } else if constexpr (std::is_same_v<T, GfVec4f>) {
      if (v.IsHolding<GfVec4f>()) {
        out = v.UncheckedGet<GfVec4f>();
        return true;
      }
      if (v.IsHolding<float>()) {
        float s = v.UncheckedGet<float>();
        out = GfVec4f(s, s, s, s);
        return true;
      }
      return false;
    } else {
      if (!v.IsHolding<T>()) {
        return false;
      }
      out = v.UncheckedGet<T>();
      return true;
    }
  }

  #define _SafeGetParticlePrimvar(t, id, name, container)                                      \
    {                                                                                          \
      t temp{};                                                                                \
      bool result = _SafeGetPrimvar(sceneDelegate, id, pxr::TfToken("particle:" #name), temp); \
      if (result) {                                                                            \
        using DestType = std::remove_reference_t<decltype(container name)>;                    \
        DestType dst = lss::AssignFromPrimvar<DestType, t>(temp);                              \
        container name = dst;                                                                  \
      }                                                                                        \
      anyExists = anyExists || result;                                                         \
    } counter++
}
