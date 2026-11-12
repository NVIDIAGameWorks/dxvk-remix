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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <iostream>

#include "../../test_utils.h"
#include "../../../src/dxvk/rtx_render/rtx_alpha_blend_dissolve.h"
#include "../../../src/dxvk/rtx_render/rtx_option.h"
#include "../../../src/dxvk/rtx_render/rtx_option_layer.h"

namespace dxvk {
  Logger Logger::s_instance("test_alpha_blend_dissolve.log");

  namespace {
    constexpr uint8_t kLegacyReference = 83;
    uint32_t checks = 0;

    void require(bool condition, const char* message) {
      ++checks;
      if (!condition) {
        throw DxvkError(message);
      }
    }

    DrawCallState makeDraw(VkBlendFactor src, VkBlendFactor dst, bool blended, AlphaTestType comparator = AlphaTestType::kAlways) {
      DrawCallState draw;
      auto& legacy = draw.modifyMaterialData();
      legacy.alphaTestEnabled = comparator != AlphaTestType::kAlways;
      legacy.alphaTestCompareOp = VkCompareOp(comparator);
      legacy.alphaTestReferenceValue = kLegacyReference;
      legacy.blendMode.enableBlending = blended;
      legacy.blendMode.colorSrcFactor = src;
      legacy.blendMode.colorDstFactor = dst;
      legacy.blendMode.colorBlendOp = VK_BLEND_OP_ADD;
      // The game fades an opaque texture by selecting its texture factor alpha, see TextureFactorFade::Select.
      legacy.textureAlphaArg1Source = RtTextureArgSource::TFactor;
      legacy.textureAlphaArg2Source = RtTextureArgSource::None;
      legacy.textureAlphaOperation = DxvkRtTextureOperation::SelectArg1;
      legacy.textureFactorFade = TextureFactorFade::Select;
      legacy.tFactor = 0x80ffffff;
      return draw;
    }

    DrawCallState makeAlphaBlendDraw(AlphaTestType comparator = AlphaTestType::kAlways) {
      return makeDraw(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true, comparator);
    }

    MaterialData makeMaterial(bool legacyAlphaState, bool authoredBlend = false, AlphaTestType authoredTest = AlphaTestType::kAlways) {
      OpaqueMaterialData opaque;
      opaque.setUseLegacyAlphaState(legacyAlphaState);
      opaque.setBlendEnabled(authoredBlend);
      opaque.setBlendType(BlendType::kAlpha);
      opaque.setAlphaTestType(authoredTest);
      opaque.setAlphaTestReferenceValue(149);
      return MaterialData(opaque);
    }

    void requireNotConverted(const DrawCallState& draw, const MaterialData& material, const char* message) {
      require(calculateAlphaBlendDissolve(draw, false, material) == AlphaBlendDissolve::None, message);
    }

    void testStandardAlphaBlend() {
      const auto material = makeMaterial(true);

      require(calculateAlphaBlendDissolve(makeAlphaBlendDraw(), false, material) == AlphaBlendDissolve::TextureFactor,
              "A standard alpha blend faded by a selected texture factor alpha must dissolve by that alpha.");
      require(calculateAlphaBlendDissolve(makeAlphaBlendDraw(AlphaTestType::kGreater), false, material) == AlphaBlendDissolve::TextureFactor,
              "A game alpha test does not change the dissolve mode.");

      auto textureFaded = makeAlphaBlendDraw();
      textureFaded.modifyMaterialData().textureFactorFade = TextureFactorFade::ModulateTexture;
      require(calculateAlphaBlendDissolve(textureFaded, false, material) == AlphaBlendDissolve::TextureAlphaTimesTextureFactor,
              "A texture alpha multiplied by the texture factor alpha must dissolve by the product.");

      // A standard alpha blend that the game does not fade keeps blending.
      auto notFading = makeAlphaBlendDraw();
      notFading.modifyMaterialData().tFactor = 0xffffffff;
      requireNotConverted(notFading, material, "A standard alpha blend with an opaque texture factor must keep blending.");
      notFading.modifyMaterialData().tFactor = 0x80ffffff;
      notFading.modifyMaterialData().textureFactorFade = TextureFactorFade::None;
      requireNotConverted(notFading, material, "A standard alpha blend without a recognized texture factor fade must keep blending.");
    }

    void testOtherBlendModes() {
      const struct { VkBlendFactor src; VkBlendFactor dst; } modes[] = {
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA },
        { VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },
        { VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_COLOR },
        { VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE },
        { VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR },
        { VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE },
        { VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO },
        { VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR },
      };

      const auto material = makeMaterial(true);
      for (const auto& mode : modes) {
        requireNotConverted(makeDraw(mode.src, mode.dst, true), material, "Only standard alpha blending may dissolve.");
      }
      requireNotConverted(makeDraw(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, false), material,
                          "Unblended draw calls must not dissolve.");
      auto subtract = makeAlphaBlendDraw();
      subtract.modifyMaterialData().blendMode.colorBlendOp = VK_BLEND_OP_SUBTRACT;
      requireNotConverted(subtract, material, "Only additive blend operations may dissolve.");
    }

    void testExclusions() {
      const InstanceCategories categories[] = {
        InstanceCategories::Particle, InstanceCategories::Beam,
        InstanceCategories::DecalStatic, InstanceCategories::DecalDynamic,
        InstanceCategories::DecalSingleOffset, InstanceCategories::DecalNoOffset,
        InstanceCategories::AlphaBlendToCutout, InstanceCategories::HairCards,
        InstanceCategories::Terrain, InstanceCategories::AnimatedWater,
        InstanceCategories::WorldUI, InstanceCategories::WorldMatte, InstanceCategories::Sky,
      };

      for (InstanceCategories category : categories) {
        auto draw = makeAlphaBlendDraw();
        draw.modifyCategoryFlags().set(category);
        requireNotConverted(draw, makeMaterial(true), "Excluded categories must keep their alpha handling.");
      }

      auto renderTarget = makeAlphaBlendDraw();
      renderTarget.isUsingRaytracedRenderTarget = true;
      requireNotConverted(renderTarget, makeMaterial(true), "Render target surfaces must not dissolve.");

      requireNotConverted(makeAlphaBlendDraw(), MaterialData(TranslucentMaterialData {}), "Translucent materials must not dissolve.");
      requireNotConverted(makeAlphaBlendDraw(), MaterialData(RayPortalMaterialData {}), "Ray portals must not dissolve.");
      requireNotConverted(makeAlphaBlendDraw(), makeMaterial(false, true), "Authored replacement blending must be kept.");
    }

    void testEyes() {
      for (bool blended : { false, true }) {
        for (bool legacyAlphaState : { false, true }) {
          for (AlphaTestType authoredTest : { AlphaTestType::kAlways, AlphaTestType::kLessOrEqual }) {
            const auto draw = makeDraw(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, blended);
            const auto material = makeMaterial(legacyAlphaState, false, authoredTest);
            require(calculateAlphaBlendDissolve(draw, true, material) == AlphaBlendDissolve::VertexAlpha,
                    "Eyes must dissolve by noise against vertex alpha, whatever their blend, alpha state source and authored test.");
          }
        }
      }

      auto particleEye = makeDraw(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, true);
      particleEye.modifyCategoryFlags().set(InstanceCategories::Particle);
      require(calculateAlphaBlendDissolve(particleEye, true, makeMaterial(true)) == AlphaBlendDissolve::None,
              "Excluded categories also apply to eyes.");
    }

    // How the game draws a material in the replacement matrix.
    enum class GameState { Opaque, BlendedNoFade, SelectFade, ModulateTextureFade, OpaqueModulateTexture };

    DrawCallState makeGameDraw(GameState state) {
      auto draw = makeDraw(VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, state != GameState::Opaque);
      auto& legacy = draw.modifyMaterialData();
      switch (state) {
      case GameState::Opaque:
      case GameState::SelectFade:
        legacy.textureFactorFade = TextureFactorFade::Select;
        break;
      case GameState::BlendedNoFade:
        // Blended by texture alpha alone.
        legacy.textureAlphaArg1Source = RtTextureArgSource::Texture;
        legacy.textureFactorFade = TextureFactorFade::None;
        break;
      case GameState::ModulateTextureFade:
        legacy.textureAlphaArg1Source = RtTextureArgSource::Texture;
        legacy.textureFactorFade = TextureFactorFade::ModulateTexture;
        break;
      case GameState::OpaqueModulateTexture:
        // The second stage multiplies alpha by an opaque texture factor, so nothing fades.
        legacy.textureAlphaArg1Source = RtTextureArgSource::Texture;
        legacy.textureFactorFade = TextureFactorFade::ModulateTexture;
        legacy.tFactor = 0xffffffff;
        break;
      }
      return draw;
    }

    // Only draw calls that the game fades and blends are converted. Under the legacy alpha state a replacement dissolves by the game's
    // opacity, whatever its authored alpha test. A replacement with its own alpha state dissolves by the game's fade unless it blends on its own.
    void testReplacementMatrix() {
      const GameState states[] = { GameState::Opaque, GameState::BlendedNoFade, GameState::SelectFade,
                                   GameState::ModulateTextureFade, GameState::OpaqueModulateTexture };
      for (bool legacyAlphaState : { false, true }) {
        for (bool authoredBlend : { false, true }) {
          for (AlphaTestType authoredTest : { AlphaTestType::kAlways, AlphaTestType::kLessOrEqual, AlphaTestType::kGreater }) {
            for (GameState state : states) {
              const auto draw = makeGameDraw(state);
              const auto material = makeMaterial(legacyAlphaState, authoredBlend, authoredTest);

              const AlphaBlendDissolve mode = calculateAlphaBlendDissolve(draw, false, material);

              const bool fading = state == GameState::SelectFade || state == GameState::ModulateTextureFade;
              AlphaBlendDissolve expected = AlphaBlendDissolve::None;
              if (legacyAlphaState && fading) {
                expected = state == GameState::ModulateTextureFade ? AlphaBlendDissolve::TextureAlphaTimesTextureFactor
                                                                   : AlphaBlendDissolve::TextureFactor;
              } else if (!legacyAlphaState && fading && !authoredBlend) {
                expected = AlphaBlendDissolve::Fade;
              }
              require(mode == expected, "Wrong dissolve mode for the replacement matrix.");
            }
          }
        }
      }
    }
  }
}

int main() {
  dxvk::RtxOptionLayer::initializeSystemLayers();
  dxvk::RtxOptionImpl::setInitialized(true);

  const struct { const char* name; void (*run)(); } tests[] = {
    { "StandardAlphaBlend", dxvk::testStandardAlphaBlend },
    { "OtherBlendModes", dxvk::testOtherBlendModes },
    { "Exclusions", dxvk::testExclusions },
    { "Eyes", dxvk::testEyes },
    { "ReplacementMatrix", dxvk::testReplacementMatrix },
  };

  uint32_t failures = 0;
  for (const auto& test : tests) {
    try {
      test.run();
      std::cout << "PASSED: " << test.name << std::endl;
    } catch (const dxvk::DxvkError& error) {
      std::cerr << "TEST FAILED: " << test.name << ": " << error.message() << std::endl;
      ++failures;
    } catch (const std::exception& error) {
      std::cerr << "TEST FAILED: " << test.name << ": " << error.what() << std::endl;
      ++failures;
    }
  }

  if (failures != 0) {
    return 1;
  }
  std::cout << "Alpha blend dissolve qualification and identity hashing passed (" << dxvk::checks << " checks)." << std::endl;
  return 0;
}
