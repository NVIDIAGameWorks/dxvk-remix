/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
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

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_materials.h"

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include "../../../src/lssusd/usd_include_end.h"

namespace dxvk {

  Logger Logger::s_instance("test_texture_transform.log");

  namespace {

    using PackedSurface = std::array<unsigned char, kSurfaceGPUSize>;

    // Byte offset of the two texture transform rows that RtSurface::writeGPUData packs.
    constexpr size_t kTextureTransformRowsOffset = 176;

    void require(bool condition, const char* pMessage) {
      if (!condition) {
        throw DxvkError(pMessage);
      }
    }

    void requireNear(float actual, float expected, const char* pMessage) {
      if (!std::isfinite(actual) || std::abs(actual - expected) > 0.000001f) {
        throw DxvkError(str::format(pMessage, ": expected ", expected, ", got ", actual));
      }
    }

    void requireNear(const Vector2& actual, const Vector2& expected, const char* pMessage) {
      requireNear(actual.x, expected.x, pMessage);
      requireNear(actual.y, expected.y, pMessage);
    }

    RtSurface makeSurface() {
      RtSurface surface;
      surface.positionBufferIndex = 1;
      surface.associatedGeometryHash = 0;
      surface.clipPlane = Vector4(0.f);
      return surface;
    }

    // Converts seconds to the microseconds that RtSurface takes for scrolling.
    uint64_t toMicroseconds(double seconds) {
      return static_cast<uint64_t>(std::llround(seconds * 1000000.0));
    }

    PackedSurface packSurface(const RtSurface& surface, double timeSeconds) {
      PackedSurface packed {};
      size_t offset = 0;
      surface.writeGPUData(packed.data(), offset, SIZE_MAX, toMicroseconds(timeSeconds));
      return packed;
    }

    void requireDefaultTransformData(const MaterialData& material) {
      Vector2 speed(19.f, -7.f);
      Vector2 scale(9.f, -3.f);
      Vector2 offset(5.f, -2.f);
      float rotation = 17.f;
      material.getTextureTransformData(speed, scale, offset, rotation);
      requireNear(speed, Vector2(0.f), "Default material scroll speed");
      requireNear(scale, Vector2(1.f), "Default material scale");
      requireNear(offset, Vector2(0.f), "Default material offset");
      requireNear(rotation, 0.f, "Default material rotation");
    }

    template<typename T>
    void testMaterialTransform(MaterialDataType expectedType) {
      T authored;
      require(MaterialData(authored).getType() == expectedType, "Incorrect material variant in texture transform test.");
      requireDefaultTransformData(MaterialData(authored));

      struct ScrollCase {
        float m_rate;
        float m_angle;
        Vector2 m_expectedSpeed;
      };
      const ScrollCase cases[] {
        { 2.f, 0.f, Vector2(2.f, 0.f) },
        { 3.f, 90.f, Vector2(0.f, 3.f) },
        { 2.f, -90.f, Vector2(0.f, -2.f) },
        { 2.f, 180.f, Vector2(-2.f, 0.f) },
        { 2.f, 270.f, Vector2(0.f, -2.f) },
        { 2.f, -360.f, Vector2(2.f, 0.f) },
        { 2.f, 45.f, Vector2(1.41421356237f, 1.41421356237f) },
        { 2.f, 135.f, Vector2(-1.41421356237f, 1.41421356237f) },
        { -2.f, 90.f, Vector2(0.f, -2.f) },
        { 2.f, 450.f, Vector2(0.f, 2.f) },
        { 2.f, -810.f, Vector2(0.f, -2.f) },
        { 100000.f, 720.f, Vector2(65504.f, 0.f) },
      };
      Vector2 speed;
      Vector2 scale;
      Vector2 offset;
      float rotation = 0.f;
      for (const auto& testCase : cases) {
        authored.setTextureScrollRate(testCase.m_rate);
        authored.setTextureScrollAngle(testCase.m_angle);
        MaterialData(authored).getTextureTransformData(speed, scale, offset, rotation);
        requireNear(speed, testCase.m_expectedSpeed, "Authored material scroll speed");

        // Axis-aligned angles must leave the other axis exactly still, even after a long time.
        RtSurface surface = makeSurface();
        surface.textureScrollSpeed = speed;
        const Vector2 scrollOffset = surface.getTextureScrollOffset(toMicroseconds(1000.0));
        require(testCase.m_expectedSpeed.x != 0.f || scrollOffset.x == 0.f, "Axis-aligned scrolling must not move U.");
        require(testCase.m_expectedSpeed.y != 0.f || scrollOffset.y == 0.f, "Axis-aligned scrolling must not move V.");
      }

      authored.setTextureScale(Vector2(-100000.f, 100000.f));
      requireNear(authored.getTextureScale(), Vector2(-65504.f, 65504.f), "Per-axis scale limits");
      authored.setTextureOffset(Vector2(200000.f, -200000.f));
      requireNear(authored.getTextureOffset(), Vector2(65504.f, -65504.f), "Per-axis offset limits");
      authored.setTextureScrollRate(-100000.f);
      requireNear(authored.getTextureScrollRate(), -65504.f, "Negative scroll rate limit");
      authored.setTextureRotation(400.f);
      requireNear(authored.getTextureRotation(), 400.f, "Authored rotation must remain unchanged");

      for (float value : { -100000.f, 100000.f }) {
        const float expected = value < 0.f ? -65504.f : 65504.f;
        authored.setTextureScrollAngle(value);
        authored.setTextureRotation(value);
        requireNear(authored.getTextureScrollAngle(), expected, "Scroll angle limit");
        requireNear(authored.getTextureRotation(), expected, "Rotation limit");
      }
      authored.setTextureScrollRate(std::numeric_limits<float>::infinity());
      requireNear(authored.getTextureScrollRate(), 65504.f, "Infinite scroll rate must clamp to its limit");

      const auto stage = pxr::UsdStage::CreateInMemory();
      const auto shader = stage->DefinePrim(pxr::SdfPath("/Shader"), pxr::TfToken("Shader"));
      shader.CreateAttribute(pxr::TfToken("inputs:texture_scroll_rate"), pxr::SdfValueTypeNames->Float).Set(-0.375f);
      shader.CreateAttribute(pxr::TfToken("inputs:texture_scroll_angle"), pxr::SdfValueTypeNames->Float).Set(-90.f);
      shader.CreateAttribute(pxr::TfToken("inputs:texture_scale"), pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(-0.5f, 2.f));
      shader.CreateAttribute(pxr::TfToken("inputs:texture_offset"), pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(1.25f, -2.5f));
      shader.CreateAttribute(pxr::TfToken("inputs:texture_rotation"), pxr::SdfValueTypeNames->Float).Set(-90.f);
      const auto decoded = T::deserialize([](const pxr::UsdPrim&, const pxr::TfToken&) { return TextureRef{}; }, shader);
      MaterialData(decoded).getTextureTransformData(speed, scale, offset, rotation);
      requireNear(speed, Vector2(0.f, 0.375f), "USD material scroll speed");
      requireNear(scale, Vector2(-0.5f, 2.f), "USD material scale");
      requireNear(offset, Vector2(1.25f, -2.5f), "USD material offset");
      requireNear(rotation, -90.f, "USD material rotation");

      shader.GetAttribute(pxr::TfToken("inputs:texture_scroll_rate")).Set(100000.f);
      shader.GetAttribute(pxr::TfToken("inputs:texture_scroll_angle")).Set(450.f);
      shader.GetAttribute(pxr::TfToken("inputs:texture_scale")).Set(pxr::GfVec2f(-100000.f, 100000.f));
      shader.GetAttribute(pxr::TfToken("inputs:texture_offset")).Set(pxr::GfVec2f(200000.f, -200000.f));
      shader.GetAttribute(pxr::TfToken("inputs:texture_rotation")).Set(400.f);
      const auto largeDecoded = T::deserialize([](const pxr::UsdPrim&, const pxr::TfToken&) { return TextureRef{}; }, shader);
      MaterialData(largeDecoded).getTextureTransformData(speed, scale, offset, rotation);
      requireNear(speed, Vector2(0.f, 65504.f), "USD scroll rate limit with an angle beyond one turn");
      requireNear(scale, Vector2(-65504.f, 65504.f), "USD per-axis scale limits");
      requireNear(offset, Vector2(65504.f, -65504.f), "USD per-axis offset limits");
      requireNear(rotation, 400.f, "USD authored rotation must remain unchanged");

      shader.GetAttribute(pxr::TfToken("inputs:texture_scroll_rate")).Set(std::numeric_limits<float>::infinity());
      shader.GetAttribute(pxr::TfToken("inputs:texture_scale")).Set(pxr::GfVec2f(-std::numeric_limits<float>::infinity(), 2.f));
      const auto invalidDecoded = T::deserialize([](const pxr::UsdPrim&, const pxr::TfToken&) { return TextureRef{}; }, shader);
      requireNear(invalidDecoded.getTextureScrollRate(), 65504.f, "USD infinite scroll rate must clamp to its limit");
      requireNear(invalidDecoded.getTextureScale(), Vector2(-65504.f, 2.f), "USD infinite scale must clamp per component");
    }

    void testPortalDefaults() {
      requireDefaultTransformData(MaterialData(RayPortalMaterialData{}));
    }

    void testTextureTransform() {
      struct TransformCase {
        Vector2 m_scale;
        Vector2 m_speed;
        double m_time;
        Vector2 m_expected;
        Vector2 m_offset = Vector2(0.f);
        float m_rotation = 0.f;
      };
      const TransformCase cases[] {
        { Vector2(0.5f, 2.f), Vector2(0.25f, 0.5f), 1.f, Vector2(0.4375f, 1.75f) },
        { Vector2(-0.5f, 2.f), Vector2(0.25f, -0.375f), 1.f, Vector2(0.0625f, 0.875f) },
        { Vector2(2.f, -0.5f), Vector2(0.5f, 0.25f), 3.f, Vector2(0.25f, 0.4375f) },
        { Vector2(-0.5f, -2.f), Vector2(-0.25f, 0.125f), 3.f, Vector2(-0.9375f, -0.875f) },
        { Vector2(1.f), Vector2(0.5f, -0.25f), 2.5f, Vector2(-0.375f, 0.f) },
        { Vector2(0.f, 2.f), Vector2(0.f), 10.f, Vector2(0.f, 1.25f) },
        { Vector2(1.f), Vector2(0.25f, -0.375f), 0.f, Vector2(0.375f, 0.625f) },
        { Vector2(1.f), Vector2(0.25f, -0.375f), 1.f, Vector2(0.625f, 0.25f) },
        { Vector2(1.f), Vector2(0.25f, -0.375f), 3.f, Vector2(1.125f, 1.5f) },
        { Vector2(1.f), Vector2(0.f), 0.f, Vector2(-0.625f, 0.375f), Vector2(0.f), 90.f },
        { Vector2(1.f), Vector2(0.f), 0.f, Vector2(0.625f, -0.375f), Vector2(0.f), -90.f },
        { Vector2(0.5f, 2.f), Vector2(0.f), 0.f, Vector2(-1.25f, 0.1875f), Vector2(0.f), 90.f },
        { Vector2(-0.5f, 2.f), Vector2(0.f), 0.f, Vector2(-1.25f, -0.1875f), Vector2(0.f), 90.f },
        { Vector2(2.f, -0.5f), Vector2(0.f), 0.f, Vector2(-0.3125f, -0.75f), Vector2(0.f), -90.f },
        { Vector2(-0.5f, -2.f), Vector2(0.f), 0.f, Vector2(0.1875f, 1.25f), Vector2(0.f), 180.f },
        { Vector2(2.f, 0.5f), Vector2(0.f), 0.f, Vector2(0.3093592168f, 0.751300955f), Vector2(0.f), 45.f },
        { Vector2(1.f), Vector2(0.f), 7.f, Vector2(1.625f, -1.875f), Vector2(1.25f, -2.5f), 0.f },
        { Vector2(0.5f, 2.f), Vector2(0.25f, -0.375f), 1.f, Vector2(0.25f, -2.6875f), Vector2(1.25f, -2.5f), 90.f },
        { Vector2(0.5f, 2.f), Vector2(0.25f, -0.375f), 3.f, Vector2(0.75f, -1.4375f), Vector2(1.25f, -2.5f), 90.f },
      };
      RtSurface surface = makeSurface();
      require(surface.getTextureTransform(toMicroseconds(7.0)) == Matrix4(), "Default surface transform must remain identity.");
      const Vector2 uv(0.375f, 0.625f);
      for (const auto& testCase : cases) {
        surface.textureTransform = composeTextureTransform(Matrix4(), testCase.m_scale, testCase.m_offset, testCase.m_rotation);
        surface.textureScrollSpeed = testCase.m_speed;
        requireNear(transformTexcoord(surface.getTextureTransform(toMicroseconds(testCase.m_time)), uv), testCase.m_expected, "CPU texture transform");
      }

      const Matrix4 original(
        Vector4(2.f, 0.5f, 0.f, 0.2f),
        Vector4(-0.25f, 3.f, 0.f, -0.3f),
        Vector4(0.75f, -0.5f, 1.f, 0.4f),
        Vector4(0.125f, -0.625f, 0.f, 2.f));
      require(composeTextureTransform(original, Vector2(1.f), Vector2(0.f), 0.f) == original, "Identity material parameters must keep the captured transform.");
      require(composeTextureTransform(original, Vector2(1.f), Vector2(0.f), 400.f) ==
              composeTextureTransform(original, Vector2(1.f), Vector2(0.f), 40.f), "Rotations beyond one turn must wrap.");
      require(composeTextureTransform(original, Vector2(1.f), Vector2(0.f), -810.f) ==
              composeTextureTransform(original, Vector2(1.f), Vector2(0.f), -90.f), "Negative rotations beyond one turn must wrap.");
      surface.textureTransform = composeTextureTransform(original, Vector2(-0.5f, 2.f), Vector2(0.f), 0.f);
      surface.textureScrollSpeed = Vector2(0.25f, -0.375f);
      requireNear(surface.getTextureScrollOffset(toMicroseconds(1.0)), Vector2(0.25f, -0.375f), "Wrapped scroll offset");
      requireNear(surface.getTextureScrollOffset(0), Vector2(0.f), "Scroll offset at time 0");

      // The offset wraps with a period of 2, so Mirrored_Repeat stays seamless.
      requireNear(surface.getTextureScrollOffset(toMicroseconds(3.0)), Vector2(0.75f, 0.875f), "Scroll offset wrapped across -1");
      requireNear(surface.getTextureScrollOffset(toMicroseconds(5.0)), Vector2(-0.75f, 0.125f), "Scroll offset wrapped across 1");

      const Vector4 source(0.2f, 0.4f, 0.7f, 1.f);
      const Vector4 transformed = surface.getTextureTransform(toMicroseconds(1.0)) * source;
      requireNear(transformed.xy(), Vector2(-0.225f, 0.275f), "Material transform must follow the existing game transform");
      requireNear(transformed.z, (original * source).z, "Original texture transform Z row");
      requireNear(transformed.w, (original * source).w, "Original texture transform W row");
      requireNear((surface.getTextureTransform(toMicroseconds(3.0)) * source).xy(), Vector2(0.275f, 1.525f), "Later CPU animation offset");

      surface.textureTransform = composeTextureTransform(original, Vector2(-0.5f, 2.f), Vector2(1.25f, -2.5f), 90.f);
      const Vector4 rotated = surface.getTextureTransform(toMicroseconds(1.0)) * source;
      requireNear(rotated.xy(), Vector2(0.85f, -3.35f), "Material TRS must follow the captured transform");
      requireNear(rotated.z, (original * source).z, "Rotating UVs must preserve the captured Z row");
      requireNear(rotated.w, (original * source).w, "Rotating UVs must preserve the captured W row");
      requireNear((surface.getTextureTransform(toMicroseconds(3.0)) * source).xy(), Vector2(1.35f, -2.1f), "Scroll translation must remain independent of rotation and scale");
    }

    void testTexcoordConvention() {
      const Vector2 uv(0.25f, 0.5f);
      RtSurface surface = makeSurface();
      const auto checkTransform = [&](double timeSeconds, const Vector2& expected) {
        const uint64_t timeUs = toMicroseconds(timeSeconds);
        const Vector2 cpuUv = transformTexcoord(surface.getTextureTransform(timeUs), uv);
        requireNear(cpuUv, expected, "CPU texture coordinate convention");

        // Evaluate the uploaded rows with the (u, v, 1, 1) input used by both GPU consumers.
        const auto packed = packSurface(surface, timeSeconds);
        float rows[2][4];
        std::memcpy(rows, packed.data() + kTextureTransformRowsOffset, sizeof(rows));
        const Vector2 shaderUv(
          rows[0][0] * uv.x + rows[0][1] * uv.y + rows[0][2] + rows[0][3],
          rows[1][0] * uv.x + rows[1][1] * uv.y + rows[1][2] + rows[1][3]);
        requireNear(shaderUv, expected, "Packed shader texture coordinate convention");

        // Billboards add scrolling after the static UV transform used for OMM hashing.
        const Vector2 billboardUv = transformTexcoord(surface.textureTransform, uv) + surface.getTextureScrollOffset(timeUs);
        requireNear(billboardUv, shaderUv, "Billboard and packed shader UV parity");
      };

      checkTransform(0.0, uv);
      surface.textureTransform.data[3].xy() = Vector2(-0.125f, 0.25f);
      checkTransform(0.0, Vector2(0.125f, 0.75f));

      surface.textureTransform.data[3].xy() = Vector2(0.f);
      surface.textureTransform.data[2].xy() = Vector2(0.25f, 0.125f);
      checkTransform(0.0, Vector2(0.5f, 0.625f));
      surface.textureTransform.data[3].xy() = Vector2(-0.125f, 0.25f);
      checkTransform(0.0, Vector2(0.375f, 0.875f));

      surface.textureTransform = composeTextureTransform(surface.textureTransform, Vector2(-0.5f, 2.f), Vector2(0.25f, -0.5f), 90.f);
      checkTransform(0.0, Vector2(-1.5f, -0.6875f));
      surface.textureScrollSpeed = Vector2(0.25f, -0.375f);
      checkTransform(1.0, Vector2(-1.25f, -1.0625f));
      checkTransform(5.0, Vector2(-2.25f, -0.5625f));

      // OMM baking uses time zero even when the surface scrolls.
      checkTransform(0.0, Vector2(-1.5f, -0.6875f));
    }

    void testSurfacePacking() {
      RtSurface surface = makeSurface();
      surface.textureTransform = composeTextureTransform(Matrix4(), Vector2(-1.0006f, 1.0006f), Vector2(1.25f, -2.5f), 90.f);
      surface.textureScrollSpeed = Vector2(0.25f, -0.375f);
      const Matrix4 transform = surface.getTextureTransform(toMicroseconds(1.0));
      const auto packed = packSurface(surface, 1.0);
      for (size_t row = 0; row < 2; ++row) {
        for (size_t column = 0; column < 4; ++column) {
          float value;
          std::memcpy(&value, packed.data() + kTextureTransformRowsOffset + sizeof(float) * (row * 4 + column), sizeof(value));
          requireNear(value, transform[column][row], "Packed CPU texture transform");
        }
      }

      surface.eyeParams = RtEyeParams{};
      RtSurface identity = makeSurface();
      identity.eyeParams = surface.eyeParams;
      require(packSurface(surface, 1.0) == packSurface(identity, 3.0), "Texture transforms must not modify the aliased eye data.");
    }

  }

}

int main() {
  try {
    dxvk::testMaterialTransform<dxvk::OpaqueMaterialData>(dxvk::MaterialDataType::Opaque);
    dxvk::testMaterialTransform<dxvk::TranslucentMaterialData>(dxvk::MaterialDataType::Translucent);
    dxvk::testPortalDefaults();
    dxvk::testTextureTransform();
    dxvk::testTexcoordConvention();
    dxvk::testSurfacePacking();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED with exception: " << error.what() << std::endl;
    return -1;
  }

  std::cout << "All texture transform tests passed." << std::endl;
  return 0;
}
