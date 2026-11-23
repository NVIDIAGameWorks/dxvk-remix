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
#include <cstring>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>

#include "../../test_utils.h"
#include "rtx_render/rtx_asset_replacer.h"
#include "rtx_render/rtx_light_utils.h"
#include "rtx_render/rtx_lights_data.h"
#include "rtx_render/rtx_option_manager.h"
#include "../../../src/util/util_global_time.h"

#include "../../../src/lssusd/usd_include_begin.h"
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include "../../../src/lssusd/usd_include_end.h"

namespace dxvk {
  Logger Logger::s_instance("test_light_gobo.log");

  namespace {
    // The path of a captured light, which an untyped override prim must use to be merged with the game light.
    const pxr::SdfPath kCapturedLightPath("/RootNode/lights/light_0123456789ABCDEF");

    const pxr::TfToken kGoboScaleToken("inputs:shaping:gobo:scale");
    const pxr::TfToken kGoboOffsetToken("inputs:shaping:gobo:offset");
    const pxr::TfToken kGoboRotationToken("inputs:shaping:gobo:rotation");
    const pxr::TfToken kGoboFovToken("inputs:shaping:gobo:fov");

    const pxr::TfToken kGoboAspectToken("inputs:shaping:gobo:aspect");
    const pxr::TfToken kGoboWrapModeToken("inputs:shaping:gobo:wrap_mode");
    const pxr::TfToken kGoboStrengthToken("inputs:shaping:gobo:strength");
    const pxr::TfToken kGoboInvertToken("inputs:shaping:gobo:invert");
    const pxr::TfToken kGoboUseAlphaToken("inputs:shaping:gobo:use_alpha");
    const pxr::TfToken kGoboColorSpaceToken("inputs:shaping:gobo:color_space");
    const pxr::TfToken kGoboScrollSpeedToken("inputs:shaping:gobo:scroll_speed");
    const pxr::TfToken kGoboRotationSpeedToken("inputs:shaping:gobo:rotation_speed");
    const pxr::TfToken kGoboShakeAngleToken("inputs:shaping:gobo:shake_angle");
    const pxr::TfToken kGoboShakeFrequencyToken("inputs:shaping:gobo:shake_frequency");
    const pxr::TfToken kGoboTimeOffsetToken("inputs:shaping:gobo:time_offset");
    const pxr::TfToken kGoboTimeOffsetRandomToken("inputs:shaping:gobo:time_offset_random");
    const pxr::TfToken kGoboRowsToken("inputs:shaping:gobo:sprite_sheet_rows");
    const pxr::TfToken kGoboColsToken("inputs:shaping:gobo:sprite_sheet_cols");
    const pxr::TfToken kGoboFpsToken("inputs:shaping:gobo:sprite_sheet_fps");
    const pxr::TfToken kGoboFirstFrameToken("inputs:shaping:gobo:sprite_sheet_first_frame");
    const pxr::TfToken kGoboFrameCountToken("inputs:shaping:gobo:sprite_sheet_frame_count");
    const pxr::TfToken kGoboBlendToken("inputs:shaping:gobo:sprite_sheet_blend");
    const pxr::TfToken kGoboLoopModeToken("inputs:shaping:gobo:sprite_sheet_loop_mode");

    constexpr size_t kTestTextureKey = 0x1234;

    void require(bool condition, const char* message) {
      if (!condition) {
        throw DxvkError(message);
      }
    }

    bool approxEqual(float a, float b, float epsilon = 1e-4f) {
      return std::abs(a - b) <= epsilon;
    }

    // Creates a texture reference that is valid but has no image, which is enough for the CPU side of a gobo.
    TextureRef makeTestTexture() {
      Rc<ManagedTexture> texture = new ManagedTexture();
      texture->m_uniqueKey = kTestTextureKey;
      return TextureRef(texture);
    }

    // Creates a D3D9 spot light pointing along +Z with a 1 radian outer cone, so its shaping cone angle is 0.5 radians.
    D3DLIGHT9 makeSpotLight(const Vector3& direction = Vector3(0.0f, 0.0f, 1.0f)) {
      D3DLIGHT9 light = {};
      light.Type = D3DLIGHT_SPOT;
      light.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
      light.Position = { 0.0f, 0.0f, 0.0f };
      light.Direction = { direction.x, direction.y, direction.z };
      light.Range = 100.0f;
      light.Falloff = 1.0f;
      light.Attenuation0 = 1.0f;
      light.Theta = 0.5f;
      light.Phi = 1.0f;
      return light;
    }

    D3DLIGHT9 makePointLight() {
      D3DLIGHT9 light = makeSpotLight();
      light.Type = D3DLIGHT_POINT;
      return light;
    }

    pxr::UsdPrim makeOverridePrim(const pxr::UsdStageRefPtr& stage) {
      return stage->OverridePrim(kCapturedLightPath);
    }

    // Parses the override, gives it the test texture, merges it with the game light and converts it like the scene manager does.
    RtLight createMergedLight(const pxr::UsdPrim& prim, const D3DLIGHT9& gameLight, bool withTexture = true) {
      std::optional<LightData> lightData = LightData::tryCreate(prim, nullptr, true, false);
      require(lightData.has_value(), "An override prim on a captured light path must create light data");

      if (withTexture) {
        lightData->setGoboTexture(makeTestTexture());
      }

      lightData->merge(gameLight);
      return lightData->toRtLight();
    }

    // Tests that every gobo attribute authored on an override survives the merge with the captured game light.
    void testOverrideSurvivesMerge() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboScaleToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(2.0f, 0.5f));
      prim.CreateAttribute(kGoboOffsetToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(0.25f, -0.25f));
      // Note: 450 degrees wraps to 90 degrees.
      prim.CreateAttribute(kGoboRotationToken, pxr::SdfValueTypeNames->Float).Set(450.0f);
      // Note: Doubles are accepted as well as floats.
      prim.CreateAttribute(kGoboFovToken, pxr::SdfValueTypeNames->Double).Set(40.0);

      const RtLight light = createMergedLight(prim, makeSpotLight());
      require(light.getType() == RtLightType::Sphere, "The merged light must be a sphere light");
      require(light.getSphereLight().getShaping().getEnabled(), "The merged light must keep the game light's shaping");

      const RtLightGobo* pGobo = light.getGobo();
      require(pGobo != nullptr, "The gobo must survive the merge with the game light");
      require(pGobo->texture.isValid() && pGobo->texture.getUniqueKey() == kTestTextureKey, "The gobo must keep its texture");
      require(pGobo->scale.x == 2.0f && pGobo->scale.y == 0.5f, "The gobo scale must be read from the float2 attribute");
      require(pGobo->offset.x == 0.25f && pGobo->offset.y == -0.25f, "The gobo offset must be read from the float2 attribute");
      require(approxEqual(pGobo->rotationRadians, 90.0f * kDegreesToRadians), "The gobo rotation must be wrapped and converted to radians");
      require(approxEqual(pGobo->fovRadians, 40.0f * kDegreesToRadians), "The gobo field of view must be read from a double attribute");

      std::cout << "Gobo override survives the merge: PASSED" << std::endl;
    }

    // Tests the field of view default and the clamping of out of range and invalid values.
    void testDefaultsAndClamping() {
      {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
        const pxr::UsdPrim prim = makeOverridePrim(stage);

        const RtLight light = createMergedLight(prim, makeSpotLight());
        const RtLightGobo* pGobo = light.getGobo();
        require(pGobo != nullptr, "A gobo with only a texture must be attached");
        require(pGobo->scale.x == 1.0f && pGobo->scale.y == 1.0f, "The default gobo scale must be one");
        require(pGobo->offset.x == 0.0f && pGobo->offset.y == 0.0f, "The default gobo offset must be zero");
        require(pGobo->rotationRadians == 0.0f, "The default gobo rotation must be zero");
        // Note: The game light's cone angle is half of Phi, so twice the cone angle is Phi.
        require(approxEqual(pGobo->fovRadians, 1.0f), "The default gobo field of view must be twice the cone angle");
      }

      {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
        const pxr::UsdPrim prim = makeOverridePrim(stage);
        prim.CreateAttribute(kGoboScaleToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(1000.0f, -std::numeric_limits<float>::infinity()));
        prim.CreateAttribute(kGoboOffsetToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(std::numeric_limits<float>::quiet_NaN(), 0.5f));
        prim.CreateAttribute(kGoboRotationToken, pxr::SdfValueTypeNames->Float).Set(std::numeric_limits<float>::infinity());
        prim.CreateAttribute(kGoboFovToken, pxr::SdfValueTypeNames->Float).Set(500.0f);

        const RtLight light = createMergedLight(prim, makeSpotLight());
        const RtLightGobo* pGobo = light.getGobo();
        require(pGobo != nullptr, "A gobo with out of range values must still be attached");
        require(pGobo->scale.x == 100.0f && pGobo->scale.y == -100.0f, "The gobo scale must be clamped to [-100, 100]");
        require(pGobo->offset.x == 0.0f && pGobo->offset.y == 0.0f, "A gobo offset holding NaN must be ignored");
        require(pGobo->rotationRadians == 0.0f, "An infinite gobo rotation must be ignored");
        require(approxEqual(pGobo->fovRadians, 179.0f * kDegreesToRadians), "The gobo field of view must be clamped to 179 degrees");
      }

      {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
        const pxr::UsdPrim prim = makeOverridePrim(stage);
        prim.CreateAttribute(kGoboFovToken, pxr::SdfValueTypeNames->Float).Set(-5.0f);

        const RtLight light = createMergedLight(prim, makeSpotLight());
        require(light.getGobo() != nullptr && approxEqual(light.getGobo()->fovRadians, 1.0f), "A negative gobo field of view must select the default");
      }

      std::cout << "Gobo defaults and clamping: PASSED" << std::endl;
    }

    // Tests that a gobo is only attached to sphere lights with shaping and with a texture.
    void testGoboRequirements() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboScaleToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(2.0f, 2.0f));

      require(createMergedLight(prim, makePointLight()).getGobo() == nullptr, "A gobo on a sphere light without shaping must be ignored");
      require(createMergedLight(prim, makeSpotLight(), false).getGobo() == nullptr, "Gobo attributes without a texture must not attach a gobo");
      require(LightData::tryCreate(makeSpotLight())->toRtLight().getGobo() == nullptr, "A game light must not have a gobo");

      std::cout << "Gobo requirements: PASSED" << std::endl;
    }

    struct DecodedGobo {
      bool flag;
      uint32_t flags;
      uint32_t wrapMode;
      uint32_t textureIndex;
      Vector3 tangent;
      Vector2 uvScale;
      Vector2 uvOffset;
      uint32_t cols;
      uint32_t rows;
      float frameBlend;
      float strength;
      uint32_t cell;
      uint32_t nextCell;
    };

    // Decodes the gobo from a packed sphere light like sphereLightCreate and lightGoboCreate do.
    DecodedGobo decodeGobo(const RtLight& light, bool isWorldLeftHanded = false) {
      std::array<uint32_t, kLightGPUSize / sizeof(uint32_t)> data = {};
      size_t offset = 0;
      light.writeGPUData(reinterpret_cast<unsigned char*>(data.data()), offset, isWorldLeftHanded);
      require(offset == kLightGPUSize, "A packed light must be exactly kLightGPUSize bytes");

      const auto low = [](uint32_t word) { return static_cast<uint16_t>(word & 0xFFFFu); };
      const auto high = [](uint32_t word) { return static_cast<uint16_t>(word >> 16); };

      // Note: data0 is words 0 to 3, data2 is words 8 to 11 and data3 is words 12 to 15.
      DecodedGobo gobo;
      gobo.flags = data[15];
      gobo.flag = (gobo.flags & lightFlagGobo) != 0;
      gobo.wrapMode = (gobo.flags >> lightGoboWrapShift) & lightGoboWrapMask;
      gobo.textureIndex = data[3] >> 16;
      gobo.tangent = normalize(Vector3(lightGoboDecodeSnorm16(low(data[8])), lightGoboDecodeSnorm16(high(data[8])), lightGoboDecodeSnorm16(low(data[9]))));
      gobo.uvScale = Vector2(glm::unpackHalf1x16(low(data[10])), glm::unpackHalf1x16(high(data[10])));

      if (gobo.wrapMode == lightGoboWrapRepeat || gobo.wrapMode == lightGoboWrapMirror) {
        const float period = gobo.wrapMode == lightGoboWrapMirror ? 2.0f : 1.0f;
        gobo.uvOffset = Vector2(low(data[11]) * period / 65536.0f, high(data[11]) * period / 65536.0f);
      } else {
        gobo.uvOffset = Vector2(glm::unpackHalf1x16(low(data[11])), glm::unpackHalf1x16(high(data[11])));
      }

      gobo.cols = data[12] & 0xFFu;
      gobo.rows = (data[12] >> 8) & 0xFFu;
      gobo.frameBlend = ((data[12] >> 16) & 0xFFu) / 256.0f;
      gobo.strength = (data[12] >> 24) / 255.0f;
      gobo.cell = low(data[13]);
      gobo.nextCell = high(data[13]);
      return gobo;
    }

    // Tests that the packed gobo matches the layout the shaders decode, including the projection frame.
    void testGpuPacking() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboScaleToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(2.0f, 1.0f));
      prim.CreateAttribute(kGoboOffsetToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(0.25f, -0.5f));
      prim.CreateAttribute(kGoboFovToken, pxr::SdfValueTypeNames->Float).Set(90.0f);

      const RtLight light = createMergedLight(prim, makeSpotLight());

      // Note: Without a resident texture the light is packed without its gobo.
      require(!light.isGoboRendered() && !decodeGobo(light).flag, "A gobo without a texture index must not be packed");

      light.setGoboTexture(7, 256, 256, VK_FORMAT_BC7_SRGB_BLOCK);
      require(light.isGoboRendered(), "A gobo with a texture index must be rendered");

      const DecodedGobo gobo = decodeGobo(light);
      const Vector3 axis = light.getSphereLight().getShaping().getDirection();
      require(gobo.flag, "The gobo flag must be set");
      require(gobo.textureIndex == 7, "The texture index must be packed into data0.w");
      require(gobo.cols == 1 && gobo.rows == 1 && gobo.cell == 0 && gobo.nextCell == 0, "The default sprite sheet must be one cell");
      require(gobo.frameBlend == 0.0f && gobo.strength == 1.0f, "The default gobo must be at full strength");
      require((gobo.flags & 0x3F0u) == 0, "The default gobo must pack no options");
      require(approxEqual(length(gobo.tangent), 1.0f, 1e-3f), "The packed tangent must be normalized");
      require(approxEqual(dot(gobo.tangent, axis), 0.0f, 1e-3f), "The packed tangent must be orthogonal to the shaping axis");
      // Note: A 90 degree field of view gives 0.5 / tan(45 degrees) = 0.5.
      require(approxEqual(gobo.uvScale.x, 1.0f, 1e-3f) && approxEqual(gobo.uvScale.y, 0.5f, 1e-3f), "The UV scale must fold in the field of view");
      require(gobo.uvOffset.x == 0.25f && gobo.uvOffset.y == -0.5f, "The UV offset must be packed into data2.w");

      light.setGoboTexture(BINDING_INDEX_INVALID, 1, 1, VK_FORMAT_UNDEFINED);
      require(!decodeGobo(light).flag, "Clearing the texture index must remove the gobo from the packed light");

      std::cout << "Gobo GPU packing: PASSED" << std::endl;
    }

    // Tests that a shaping axis parallel to the up vector still gives a valid projection frame.
    void testParallelUpVector() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);

      for (const Vector3& direction : { Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f) }) {
        const RtLight light = createMergedLight(prim, makeSpotLight(direction));
        light.setGoboTexture(0, 1, 1, VK_FORMAT_UNDEFINED);

        const DecodedGobo gobo = decodeGobo(light);
        const Vector3 axis = light.getSphereLight().getShaping().getDirection();
        require(gobo.flag, "A light pointing along the up vector must keep its gobo");
        require(approxEqual(length(gobo.tangent), 1.0f, 1e-3f), "The fallback tangent must be normalized");
        require(approxEqual(dot(gobo.tangent, axis), 0.0f, 1e-3f), "The fallback tangent must be orthogonal to the shaping axis");
      }

      std::cout << "Gobo with a parallel up vector: PASSED" << std::endl;
    }

    // Tests that the shaders' tangent decoder reads every value that glm::packSnorm1x16 writes.
    void testTangentDecoder() {
      for (uint32_t value = 0; value <= 0xFFFFu; ++value) {
        const float decoded = lightGoboDecodeSnorm16(value);
        // Note: Both -32768 and -32767 decode to -1, which packs as -32767.
        const uint16_t expected = value == 0x8000u ? 0x8001u : static_cast<uint16_t>(value);
        require(glm::packSnorm1x16(decoded) == expected, "The shaders' tangent decoder must invert glm::packSnorm1x16");
        require(std::abs(decoded - glm::unpackSnorm1x16(static_cast<uint16_t>(value))) <= 1e-6f, "The shaders' tangent decoder must match glm::unpackSnorm1x16");
      }

      std::cout << "Gobo tangent decoder: PASSED" << std::endl;
    }

    // Returns a perspective projection like D3DXMatrixPerspectiveFovLH or RH, as columns.
    Matrix4d makeProjection(bool leftHanded) {
      const double n = 0.1;
      const double f = 1000.0;
      const double depth = leftHanded ? f / (f - n) : f / (n - f);
      return Matrix4d(
        Vector4d(1.0, 0.0, 0.0, 0.0),
        Vector4d(0.0, 1.5, 0.0, 0.0),
        Vector4d(0.0, 0.0, depth, leftHanded ? 1.0 : -1.0),
        Vector4d(0.0, 0.0, leftHanded ? -n * depth : n * depth, 0.0));
    }

    // Tests the world handedness that the light manager takes from the main camera.
    void testWorldHandedness() {
      const Matrix4d identity;
      require(LightUtils::getWorldHandedness(identity, makeProjection(true)) == 1, "A left-handed projection must show a left-handed world");
      require(LightUtils::getWorldHandedness(identity, makeProjection(false)) == -1, "A right-handed projection must show a right-handed world");

      // Note: A parameterized API camera builds its view from right, up and forward and uses a left-handed projection.
      // A host that swaps X and Y of a right-handed Z-up world sends a camera looking north as right +Y, up +Z and forward +X.
      const Matrix4d viewToWorld(
        Vector4d(0.0, 1.0, 0.0, 0.0),
        Vector4d(0.0, 0.0, 1.0, 0.0),
        Vector4d(1.0, 0.0, 0.0, 0.0),
        Vector4d(10.0, 20.0, 30.0, 1.0));
      require(LightUtils::getWorldHandedness(inverse(viewToWorld), makeProjection(true)) == 1, "A host that swaps X and Y must show a left-handed world");

      const Matrix4d zero(Vector4d(0.0), Vector4d(0.0), Vector4d(0.0), Vector4d(0.0));
      require(LightUtils::getWorldHandedness(identity, zero) == 0, "A degenerate projection must have no handedness");

      // The image's right and top must be on the screen's right and top for a camera behind the light, upright in the scene.
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      const Vector3 sceneUp(0.0f, 1.0f, 0.0f);

      for (const Vector3& direction : { Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), normalize(Vector3(-0.3f, -0.4f, 0.8f)) }) {
        const RtLight light = createMergedLight(prim, makeSpotLight(direction));
        light.setGoboTexture(1, 256, 256, VK_FORMAT_BC7_SRGB_BLOCK);
        const Vector3 axis = light.getSphereLight().getShaping().getDirection();

        for (const bool isLeftHanded : { false, true }) {
          const DecodedGobo gobo = decodeGobo(light, isLeftHanded);
          const Vector3 screenRight = isLeftHanded ? cross(sceneUp, axis) : cross(axis, sceneUp);
          const Vector3 bitangent = cross(axis, gobo.tangent);
          require(dot(gobo.tangent, screenRight) > 0.0f, "The image's right must be on the screen's right");
          require(dot(bitangent, sceneUp) * gobo.uvScale.y < 0.0f, "The image's top must be on the screen's top");
        }
      }

      std::cout << "Gobo world handedness: PASSED" << std::endl;
    }

    // Tests that a light placed relative to a spot light gets an upright image whatever way the spot light points,
    // and keeps a roll of its own.
    void testRelativeGoboUp() {
      RtLightGobo goboSettings;
      goboSettings.texture = makeTestTexture();
      goboSettings.fovRadians = 1.0f;
      const auto gobo = std::make_shared<const RtLightGobo>(goboSettings);
      const float kRollRadians = 30.0f * kDegreesToRadians;

      for (const Vector3& sceneUp : { Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) }) {
        float rollSign = 0.0f;

        for (const Vector3& direction : { Vector3(1.0f, 0.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, -1.0f, 0.0f),
                                          Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, -1.0f), normalize(Vector3(0.7f, 0.7f, -0.14f)) }) {
          if (std::abs(dot(direction, sceneUp)) > 0.99f) {
            continue;
          }

          const Matrix4 parentToWorld = LightUtils::getLightTransform(makeSpotLight(direction));
          float roll[2] = {};

          for (uint32_t i = 0; i < 2; ++i) {
            // Note: The light points along its local -Z axis, and the second light is rolled around it.
            const float localRoll = i == 0 ? 0.0f : kRollRadians;
            const RtLightShaping shaping(true, Vector3(0.0f, 0.0f, -1.0f), std::cos(0.5f), 0.0f, 0.0f);
            RtLight light(RtSphereLight(Vector3(0.0f), Vector3(1.0f), 1.0f, shaping));
            light.setGobo(gobo, Vector3(-std::sin(localRoll), std::cos(localRoll), 0.0f));
            light.applyTransform(parentToWorld);
            light.levelGoboUp(parentToWorld, sceneUp);
            light.setGoboTexture(1, 256, 256, VK_FORMAT_BC7_SRGB_BLOCK);

            const DecodedGobo decoded = decodeGobo(light);
            const Vector3 axis = light.getSphereLight().getShaping().getDirection();
            require(dot(axis, direction) > 0.999f, "The relative light must point along its parent");

            // Note: The image's top points opposite to cross(axis, tangent).
            const Vector3 imageUp = -cross(axis, decoded.tangent);
            const Vector3 upright = normalize(sceneUp - axis * dot(sceneUp, axis));
            roll[i] = std::atan2(dot(cross(upright, imageUp), axis), dot(upright, imageUp));
          }

          require(std::abs(roll[0]) < 0.5f * kDegreesToRadians, "A relative light without a roll must get an upright image");
          require(std::abs(std::abs(roll[1]) - kRollRadians) < 0.5f * kDegreesToRadians, "A relative light must keep its own roll");

          // Note: The roll turns the same way for every parent direction.
          if (rollSign == 0.0f) {
            rollSign = roll[1] > 0.0f ? 1.0f : -1.0f;
          }
          require(roll[1] * rollSign > 0.0f, "A relative light must roll the same way for every parent direction");
        }
      }

      std::cout << "Gobo up of a relative light: PASSED" << std::endl;
    }

    // Tests the parsing of every option, with the types that USD tools write.
    void testOptionParsing() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboAspectToken, pxr::SdfValueTypeNames->Double).Set(2.0);
      prim.CreateAttribute(kGoboWrapModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("mirror"));
      prim.CreateAttribute(kGoboStrengthToken, pxr::SdfValueTypeNames->Float).Set(0.5f);
      prim.CreateAttribute(kGoboInvertToken, pxr::SdfValueTypeNames->Bool).Set(true);
      prim.CreateAttribute(kGoboUseAlphaToken, pxr::SdfValueTypeNames->Int).Set(1);
      prim.CreateAttribute(kGoboColorSpaceToken, pxr::SdfValueTypeNames->String).Set(std::string("srgb"));
      prim.CreateAttribute(kGoboScrollSpeedToken, pxr::SdfValueTypeNames->Double2).Set(pxr::GfVec2d(0.5, -0.25));
      prim.CreateAttribute(kGoboRotationSpeedToken, pxr::SdfValueTypeNames->Float).Set(90.0f);
      prim.CreateAttribute(kGoboShakeAngleToken, pxr::SdfValueTypeNames->Float).Set(10.0f);
      prim.CreateAttribute(kGoboShakeFrequencyToken, pxr::SdfValueTypeNames->Float).Set(3.0f);
      prim.CreateAttribute(kGoboTimeOffsetToken, pxr::SdfValueTypeNames->Float).Set(-1.5f);
      prim.CreateAttribute(kGoboTimeOffsetRandomToken, pxr::SdfValueTypeNames->Float).Set(4.0f);
      prim.CreateAttribute(kGoboRowsToken, pxr::SdfValueTypeNames->Int).Set(4);
      prim.CreateAttribute(kGoboColsToken, pxr::SdfValueTypeNames->UInt).Set(8u);
      prim.CreateAttribute(kGoboFpsToken, pxr::SdfValueTypeNames->Int).Set(24);
      prim.CreateAttribute(kGoboFirstFrameToken, pxr::SdfValueTypeNames->Int).Set(3);
      prim.CreateAttribute(kGoboFrameCountToken, pxr::SdfValueTypeNames->Int).Set(20);
      prim.CreateAttribute(kGoboBlendToken, pxr::SdfValueTypeNames->Bool).Set(true);
      prim.CreateAttribute(kGoboLoopModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("ping_pong"));

      const RtLight light = createMergedLight(prim, makeSpotLight());
      const RtLightGobo* pGobo = light.getGobo();
      require(pGobo != nullptr, "A gobo with options must be attached");
      require(pGobo->aspect == 2.0f, "The aspect must be read");
      require(pGobo->wrapMode == RtLightGoboWrapMode::Mirror, "The wrap mode must be read from a token");
      require(pGobo->strength == 0.5f, "The strength must be read");
      require(pGobo->invert && pGobo->useAlpha, "The bools must be read from a bool or an int");
      require(pGobo->colorSpace == RtLightGoboColorSpace::Srgb, "The color space must be read from a string");
      require(pGobo->scrollSpeed.x == 0.5f && pGobo->scrollSpeed.y == -0.25f, "The scroll speed must be read from a double2");
      require(approxEqual(pGobo->rotationSpeedRadians, 90.0f * kDegreesToRadians), "The rotation speed must be converted to radians");
      require(approxEqual(pGobo->shakeAngleRadians, 10.0f * kDegreesToRadians) && pGobo->shakeFrequency == 3.0f, "The shake must be read");
      require(pGobo->timeOffset == -1.5f && pGobo->timeOffsetRandom == 4.0f, "The time offsets must be read");
      require(pGobo->spriteSheetRows == 4 && pGobo->spriteSheetCols == 8, "The sheet size must be read from integers");
      require(pGobo->spriteSheetFps == 24.0f, "The frame rate must be read from an integer");
      require(pGobo->spriteSheetFirstFrame == 3 && pGobo->spriteSheetFrameCount == 20, "The frame range must be read");
      require(pGobo->spriteSheetBlend && pGobo->spriteSheetLoopMode == RtLightGoboLoopMode::PingPong, "The playback must be read");

      std::cout << "Gobo option parsing: PASSED" << std::endl;
    }

    // Tests the defaults, clamps and rejected values of the options.
    void testOptionDefaultsAndClamping() {
      {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
        const RtLight light = createMergedLight(makeOverridePrim(stage), makeSpotLight());
        const RtLightGobo* pGobo = light.getGobo();

        require(pGobo != nullptr && pGobo->aspect == 0.0f, "The default aspect must be automatic");
        require(pGobo->wrapMode == RtLightGoboWrapMode::Clamp, "The default wrap mode must be clamp");
        require(pGobo->strength == 1.0f, "The default must be full strength");
        require(!pGobo->invert && !pGobo->useAlpha && pGobo->colorSpace == RtLightGoboColorSpace::Auto, "The default must use the color as it is");
        require(pGobo->spriteSheetRows == 1 && pGobo->spriteSheetCols == 1 && pGobo->spriteSheetFps == 0.0f, "The default must be a still image");
        require(pGobo->spriteSheetLoopMode == RtLightGoboLoopMode::Loop && !pGobo->isAnimated(0.0), "The default must loop and not animate");
      }

      {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
        const pxr::UsdPrim prim = makeOverridePrim(stage);
        const float inf = std::numeric_limits<float>::infinity();
        prim.CreateAttribute(kGoboAspectToken, pxr::SdfValueTypeNames->Float).Set(inf);
        prim.CreateAttribute(kGoboWrapModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("wrap"));
        prim.CreateAttribute(kGoboStrengthToken, pxr::SdfValueTypeNames->Float).Set(5.0f);
        prim.CreateAttribute(kGoboScrollSpeedToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(inf, -1000.0f));
        prim.CreateAttribute(kGoboRotationSpeedToken, pxr::SdfValueTypeNames->Float).Set(inf);
        prim.CreateAttribute(kGoboShakeFrequencyToken, pxr::SdfValueTypeNames->Float).Set(-2.0f);
        prim.CreateAttribute(kGoboRowsToken, pxr::SdfValueTypeNames->Int).Set(0);
        prim.CreateAttribute(kGoboColsToken, pxr::SdfValueTypeNames->Int).Set(1000);
        prim.CreateAttribute(kGoboFpsToken, pxr::SdfValueTypeNames->Float).Set(-1000.0f);
        prim.CreateAttribute(kGoboFrameCountToken, pxr::SdfValueTypeNames->Int).Set(-4);
        prim.CreateAttribute(kGoboLoopModeToken, pxr::SdfValueTypeNames->Float).Set(1.0f);

        const RtLight light = createMergedLight(prim, makeSpotLight());
        const RtLightGobo* pGobo = light.getGobo();
        require(pGobo != nullptr && pGobo->aspect == 100.0f, "The aspect must be clamped to 100");
        require(pGobo->wrapMode == RtLightGoboWrapMode::Clamp, "An unknown wrap mode must be ignored");
        require(pGobo->strength == 1.0f, "The strength must be clamped to 1");
        require(pGobo->scrollSpeed.x == 100.0f && pGobo->scrollSpeed.y == -100.0f, "The scroll speed must be clamped");
        require(approxEqual(pGobo->rotationSpeedRadians, 36000.0f * kDegreesToRadians, 1e-2f), "The rotation speed must be clamped");
        require(pGobo->shakeFrequency == 0.0f, "A negative shake frequency must be clamped to zero");
        require(pGobo->spriteSheetRows == 1 && pGobo->spriteSheetCols == 255, "The sheet size must be clamped to [1, 255]");
        require(pGobo->spriteSheetFps == -255.0f, "The frame rate must be clamped to [-255, 255]");
        require(pGobo->spriteSheetFrameCount == 0, "A negative frame count must select every frame");
        require(pGobo->spriteSheetLoopMode == RtLightGoboLoopMode::Loop, "A loop mode of the wrong type must be ignored");
      }

      std::cout << "Gobo option defaults and clamping: PASSED" << std::endl;
    }

    RtLightGobo makeFlipbook(uint32_t rows, uint32_t cols, float fps) {
      RtLightGobo gobo;
      gobo.spriteSheetRows = rows;
      gobo.spriteSheetCols = cols;
      gobo.spriteSheetFps = fps;
      return gobo;
    }

    // Tests the frame that plays over time, for full and partial sheets, every loop mode and large times.
    void testFrameSelection() {
      RtLightGobo gobo = makeFlipbook(2, 4, 10.0f);
      require(gobo.getFrames(0.0, 0).cell == 0, "Playback must start on the first cell");
      require(gobo.getFrames(0.55, 0).cell == 5, "Playback must follow the frame rate");
      require(gobo.getFrames(0.85, 0).cell == 0, "A full sheet must loop");
      require(gobo.getFrames(-0.05, 0).cell == 7, "A negative time must wrap to the last cell");
      require(gobo.getFrames(0.55, 0).blend == 0.0f, "Frames must not blend by default");

      gobo.spriteSheetFrameCount = 6;
      require(gobo.getFrames(0.55, 0).cell == 5, "A partial sheet must show its last frame");
      require(gobo.getFrames(0.65, 0).cell == 0, "A partial sheet must loop after its last frame");

      gobo.spriteSheetFrameCount = 100;
      require(gobo.getFrames(0.75, 0).cell == 7 && gobo.getFrames(0.85, 0).cell == 0, "A frame count past the sheet must stop at the last cell");

      gobo.spriteSheetFirstFrame = 5;
      gobo.spriteSheetFrameCount = 0;
      require(gobo.getFrames(0.05, 0).cell == 5 && gobo.getFrames(0.25, 0).cell == 7, "Playback must start on the first frame");
      require(gobo.getFrames(0.35, 0).cell == 5, "Playback must loop back to the first frame");

      gobo.spriteSheetFps = 0.0f;
      require(gobo.getFrames(123.0, 0).cell == 5, "A frame rate of zero must hold the first frame");
      gobo.spriteSheetFirstFrame = 50;
      require(gobo.getFrames(0.0, 0).cell == 7, "A first frame past the sheet must select the last cell");

      gobo = makeFlipbook(1, 4, -10.0f);
      require(gobo.getFrames(0.05, 0).cell == 3 && gobo.getFrames(0.15, 0).cell == 2, "A negative frame rate must play in reverse");

      gobo = makeFlipbook(1, 4, 10.0f);
      gobo.spriteSheetLoopMode = RtLightGoboLoopMode::PingPong;
      const uint32_t pingPong[] = { 0, 1, 2, 3, 2, 1, 0, 1 };
      for (uint32_t i = 0; i < std::size(pingPong); ++i) {
        require(gobo.getFrames((i + 0.5) / 10.0, 0).cell == pingPong[i], "Ping pong must not repeat the end frames");
      }

      gobo.spriteSheetLoopMode = RtLightGoboLoopMode::Once;
      gobo.spriteSheetBlend = true;
      require(gobo.getFrames(-1.0, 0).cell == 0, "Playing once must hold the first frame before it starts");
      require(gobo.getFrames(9.95, 0).cell == 3 && gobo.getFrames(9.95, 0).blend == 0.0f, "Playing once must hold the last frame");
      require(gobo.isAnimated(0.1) && !gobo.isAnimated(1.0), "Playing once must stop animating at the end");

      gobo.spriteSheetLoopMode = RtLightGoboLoopMode::Loop;
      const RtLightGoboFrames blended = gobo.getFrames(0.375, 0);
      require(blended.cell == 3 && blended.nextCell == 0 && approxEqual(blended.blend, 0.75f), "Blending must wrap from the last frame to the first");

      gobo = makeFlipbook(4, 4, 8.0f);
      gobo.spriteSheetLoopMode = RtLightGoboLoopMode::Random;
      bool differs = false;
      for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t cell = gobo.getFrames(i / 8.0, 1).cell;
        require(cell < 16, "A random frame must be on the sheet");
        require(cell == gobo.getFrames(i / 8.0 + 0.01, 1).cell, "A random frame must hold for the whole frame");
        differs |= cell != gobo.getFrames(i / 8.0, 2).cell;
      }
      require(differs, "Random playback must differ between lights");

      // Note: This is the math of calcSpritesheetValues at times up to the wrap of the animation time.
      gobo = makeFlipbook(5, 7, 255.0f);
      for (const uint32_t timeMs : { 0u, 999u, 3600000u, 14400001u, (1u << 24) - 1u }) {
        const float shaderTime = timeMs / 1000.f;
        const uint32_t shaderFrame = static_cast<uint32_t>(shaderTime * 255.0f) % 35u;
        require(gobo.getFrames(timeMs / 1000.0, 0).cell == shaderFrame, "A gobo must play in step with a material sprite sheet");
      }

      // Note: Blending must stay smooth after hours of play, where single precision would step.
      gobo = makeFlipbook(1, 8, 30.0f);
      gobo.spriteSheetBlend = true;
      const double hours = 4.0 * 3600.0;
      require(approxEqual(gobo.getFrames(hours + 0.0125, 0).blend, 0.375f), "Blending must keep its precision after hours");

      std::cout << "Gobo frame selection: PASSED" << std::endl;
    }

    // Tests scrolling, rotation, shake and time offsets, including after days of play.
    void testMotion() {
      RtLightGobo gobo;
      gobo.offset = Vector2(2.25f, -0.5f);
      gobo.scrollSpeed = Vector2(0.25f, -0.5f);
      require(gobo.getOffset(3.0).x == 2.25f && !gobo.isAnimated(0.0), "Scrolling must need a wrap mode that repeats");

      gobo.wrapMode = RtLightGoboWrapMode::Repeat;
      require(gobo.isAnimated(0.0), "A repeating image that scrolls must be animated");
      require(approxEqual(gobo.getOffset(3.0).x, 0.0f) && approxEqual(gobo.getOffset(3.0).y, 0.0f), "Repeat must wrap the offset to one image");
      require(approxEqual(gobo.getOffset(864000.5).x, 0.375f), "Scrolling must keep its precision after days");

      gobo.wrapMode = RtLightGoboWrapMode::Mirror;
      require(approxEqual(gobo.getOffset(1.0).x, 0.5f) && approxEqual(gobo.getOffset(1.0).y, 1.0f), "Mirror must wrap the offset to two images");

      gobo = RtLightGobo();
      gobo.rotationRadians = 0.5f;
      require(gobo.getRotationRadians(1000.0) == 0.5f, "A still gobo must keep its rotation");
      gobo.rotationSpeedRadians = 0.25f;
      const double expected = std::fmod(0.5 + 0.25 * 864000.5, 6.283185307179586476925);
      require(approxEqual(gobo.getRotationRadians(864000.5), static_cast<float>(expected)), "Rotation must keep its precision after days");

      gobo = RtLightGobo();
      gobo.shakeAngleRadians = 0.2f;
      gobo.shakeFrequency = 2.0f;
      require(approxEqual(gobo.getRotationRadians(0.125), 0.2f) && approxEqual(gobo.getRotationRadians(0.375), -0.2f), "Shaking must swing both ways");
      gobo.strength = 0.0f;
      require(!gobo.isAnimated(0.0), "A gobo without strength must not count as animated");

      gobo = RtLightGobo();
      gobo.timeOffset = 2.0f;
      require(gobo.getTimeOffset(1) == 2.0, "The time offset must apply as authored");
      gobo.timeOffsetRandom = 3.0f;
      require(gobo.getTimeOffset(1) >= 2.0 && gobo.getTimeOffset(1) < 5.0, "The random time offset must stay in its range");
      require(gobo.getTimeOffset(1) == gobo.getTimeOffset(1) && gobo.getTimeOffset(1) != gobo.getTimeOffset(2), "The random time offset must be fixed per light");

      std::cout << "Gobo motion: PASSED" << std::endl;
    }

    // Projects a slope like lightGoboCalcPattern does.
    Vector2 projectSlope(const DecodedGobo& gobo, const Vector3& axis, const Vector3& slope) {
      const Vector3 bitangent = cross(axis, gobo.tangent);
      return Vector2(
        0.5f + dot(slope, gobo.tangent) * gobo.uvScale.x + gobo.uvOffset.x,
        0.5f + dot(slope, bitangent) * gobo.uvScale.y + gobo.uvOffset.y);
    }

    // Tests the aspect with sheets, scale and rotation.
    void testAspect() {
      RtLightGobo image;
      require(image.getAspect(512, 512) == 1.0f && image.getAspect(1024, 256) == 4.0f, "The automatic aspect must follow the image");
      image.spriteSheetRows = 2;
      image.spriteSheetCols = 4;
      require(image.getAspect(1024, 512) == 1.0f && image.getAspect(1024, 256) == 2.0f, "The automatic aspect must follow one cell");
      image.aspect = 3.0f;
      require(image.getAspect(1024, 512) == 3.0f, "An authored aspect must replace the automatic one");

      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboFovToken, pxr::SdfValueTypeNames->Float).Set(90.0f);
      prim.CreateAttribute(kGoboRotationToken, pxr::SdfValueTypeNames->Float).Set(30.0f);
      prim.CreateAttribute(kGoboScaleToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(2.0f, 2.0f));

      const RtLight light = createMergedLight(prim, makeSpotLight());
      light.setGoboTexture(1, 512, 256, VK_FORMAT_BC7_SRGB_BLOCK);

      const DecodedGobo wide = decodeGobo(light);
      const Vector3 axis = light.getSphereLight().getShaping().getDirection();
      const Vector3 bitangent = cross(axis, wide.tangent);
      require(approxEqual(wide.uvScale.x, 1.0f, 1e-3f) && approxEqual(wide.uvScale.y, 2.0f, 1e-3f), "The field of view must span the longer side");

      // Note: The rotated corners of the rectangle must land on the image's corners, so the rotation does not shear it.
      const Vector2 topLeft = projectSlope(wide, axis, wide.tangent * -0.5f + bitangent * -0.25f);
      const Vector2 bottomRight = projectSlope(wide, axis, wide.tangent * 0.5f + bitangent * 0.25f);
      require(approxEqual(topLeft.x, 0.0f, 1e-3f) && approxEqual(topLeft.y, 0.0f, 1e-3f), "The top left corner must not shear");
      require(approxEqual(bottomRight.x, 1.0f, 1e-3f) && approxEqual(bottomRight.y, 1.0f, 1e-3f), "The bottom right corner must not shear");

      light.setGoboTexture(1, 256, 512, VK_FORMAT_BC7_SRGB_BLOCK);
      const DecodedGobo tall = decodeGobo(light);
      require(approxEqual(tall.uvScale.x, 2.0f, 1e-3f) && approxEqual(tall.uvScale.y, 1.0f, 1e-3f), "A tall image must span its height");
      require(tall.tangent.x == wide.tangent.x && tall.tangent.y == wide.tangent.y, "The aspect must not turn the image");

      std::cout << "Gobo aspect: PASSED" << std::endl;
    }

    // Tests the packing of the options and the format detection.
    void testOptionPacking() {
      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboWrapModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
      prim.CreateAttribute(kGoboOffsetToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(99.25f, -0.25f));
      prim.CreateAttribute(kGoboStrengthToken, pxr::SdfValueTypeNames->Float).Set(0.0f);
      prim.CreateAttribute(kGoboInvertToken, pxr::SdfValueTypeNames->Bool).Set(true);
      prim.CreateAttribute(kGoboUseAlphaToken, pxr::SdfValueTypeNames->Bool).Set(true);
      prim.CreateAttribute(kGoboColorSpaceToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("srgb"));
      prim.CreateAttribute(kGoboRowsToken, pxr::SdfValueTypeNames->Int).Set(255);
      prim.CreateAttribute(kGoboColsToken, pxr::SdfValueTypeNames->Int).Set(255);
      prim.CreateAttribute(kGoboFirstFrameToken, pxr::SdfValueTypeNames->Int).Set(65024);

      const RtLight light = createMergedLight(prim, makeSpotLight());
      light.setGoboTexture(9, 2048, 2048, VK_FORMAT_BC4_UNORM_BLOCK);

      const DecodedGobo gobo = decodeGobo(light);
      require(gobo.flag && gobo.wrapMode == lightGoboWrapRepeat, "The wrap mode must be packed into the flags");
      require((gobo.flags >> 29) == lightTypeSphere && (gobo.flags & 1u) != 0, "The options must not change the type and shaping bits");
      require((gobo.flags & lightFlagGoboInvert) != 0, "Invert must be packed");
      require((gobo.flags & lightFlagGoboUseAlpha) != 0, "Use alpha must be packed");
      require((gobo.flags & lightFlagGoboGrayscale) != 0, "A one channel format must be grayscale");
      require((gobo.flags & lightFlagGoboDecodeSrgb) != 0, "An sRGB image in a linear format must be decoded");
      require(gobo.uvOffset.x == 0.25f && gobo.uvOffset.y == 0.75f, "A repeating offset must be packed as a fraction of the image");
      require(gobo.strength == 0.0f, "The strength must be packed");
      require(gobo.rows == 255 && gobo.cols == 255 && gobo.cell == 65024 && gobo.nextCell == 65024, "The largest sheet must fit the packed fields");

      light.setGoboTexture(9, 2048, 2048, VK_FORMAT_BC7_SRGB_BLOCK);
      const DecodedGobo srgb = decodeGobo(light);
      require((srgb.flags & lightFlagGoboDecodeSrgb) == 0, "An sRGB format must not be decoded twice");
      require((srgb.flags & lightFlagGoboGrayscale) == 0, "An RGB format must not be grayscale");

      std::cout << "Gobo option packing: PASSED" << std::endl;
    }

    // Tests that a light that is not updated still animates, without changing its hashes.
    void testPackedAnimation() {
      GlobalTime::get().init(0.25f);
      GlobalTime::get().update();

      const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateInMemory("test_light_gobo.usda");
      const pxr::UsdPrim prim = makeOverridePrim(stage);
      prim.CreateAttribute(kGoboWrapModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("repeat"));
      prim.CreateAttribute(kGoboScrollSpeedToken, pxr::SdfValueTypeNames->Float2).Set(pxr::GfVec2f(0.5f, 0.0f));
      prim.CreateAttribute(kGoboColsToken, pxr::SdfValueTypeNames->Int).Set(64);
      prim.CreateAttribute(kGoboFpsToken, pxr::SdfValueTypeNames->Int).Set(8);
      prim.CreateAttribute(kGoboLoopModeToken, pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("once"));

      RtLight light = createMergedLight(prim, makeSpotLight());
      light.setGoboTexture(1, 4096, 64, VK_FORMAT_BC7_SRGB_BLOCK);

      const XXH64_hash_t initialHash = light.getInitialHash();
      const XXH64_hash_t transformedHash = light.getTransformedHash();
      const DecodedGobo first = decodeGobo(light);
      require(first.cell == 0 && light.isGoboAnimated(), "Playing once must start on the light's first frame");

      GlobalTime::get().update();
      const DecodedGobo second = decodeGobo(light);
      require(second.cell == 2, "The flipbook must advance without an update of the light");
      require(approxEqual(second.uvOffset.x - first.uvOffset.x, 0.125f) || approxEqual(second.uvOffset.x - first.uvOffset.x, -0.875f), "Scrolling must advance without an update of the light");
      require(light.getInitialHash() == initialHash && light.getTransformedHash() == transformedHash, "Animation must not change the light's hashes");

      // Note: Externally tracked lights are updated by assignment.
      light = createMergedLight(prim, makeSpotLight());
      light.setGoboTexture(1, 4096, 64, VK_FORMAT_BC7_SRGB_BLOCK);
      GlobalTime::get().update();
      require(decodeGobo(light).cell == 4, "Updating a light must not restart its flipbook");

      std::cout << "Gobo packed animation: PASSED" << std::endl;
    }
  }
}

int main() {
  try {
    dxvk::RtxOptionLayer::initializeSystemLayers();
    dxvk::RtxOptionImpl::setInitialized(true);
    dxvk::RtxOptionManager::markOptionsWithCallbacksDirty();
    dxvk::RtxOptionManager::applyPendingValues(nullptr, true);

    dxvk::testOverrideSurvivesMerge();
    dxvk::testDefaultsAndClamping();
    dxvk::testGoboRequirements();
    dxvk::testGpuPacking();
    dxvk::testParallelUpVector();
    dxvk::testOptionParsing();
    dxvk::testOptionDefaultsAndClamping();
    dxvk::testFrameSelection();
    dxvk::testMotion();
    dxvk::testAspect();
    dxvk::testOptionPacking();
    dxvk::testPackedAnimation();
    dxvk::testTangentDecoder();
    dxvk::testWorldHandedness();
    dxvk::testRelativeGoboUp();
  } catch (const dxvk::DxvkError& error) {
    std::cerr << "TEST FAILED: " << error.message() << std::endl;
    return -1;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILED: " << error.what() << std::endl;
    return -1;
  }
  return 0;
}
