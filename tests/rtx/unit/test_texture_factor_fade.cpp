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

#include <initializer_list>
#include <iostream>

#include "../../test_utils.h"
#include "../../../src/d3d9/d3d9_rtx_texture_factor_fade.h"

namespace dxvk {
  Logger Logger::s_instance("test_texture_factor_fade.log");

  namespace {
    using StageStates = std::array<DWORD, TextureStageStateCount>;

    uint32_t checks = 0;

    void require(bool condition, const char* message) {
      ++checks;
      if (!condition) {
        throw DxvkError(message);
      }
    }

    StageStates makeStage(DWORD colorOp, DWORD colorArg1, DWORD colorArg2, DWORD alphaOp, DWORD alphaArg1, DWORD alphaArg2) {
      StageStates states {};
      states[DXVK_TSS_COLOROP] = colorOp;
      states[DXVK_TSS_COLORARG1] = colorArg1;
      states[DXVK_TSS_COLORARG2] = colorArg2;
      states[DXVK_TSS_ALPHAOP] = alphaOp;
      states[DXVK_TSS_ALPHAARG1] = alphaArg1;
      states[DXVK_TSS_ALPHAARG2] = alphaArg2;
      states[DXVK_TSS_RESULTARG] = D3DTA_CURRENT;
      return states;
    }

    // Matches the given stages, with every following stage disabled.
    TextureFactorFade match(std::initializer_list<StageStates> chain) {
      D3D9TextureStageStates stages;
      stages.fill(makeStage(D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_CURRENT));
      uint32_t stage = 0;
      for (const StageStates& states : chain) {
        stages[stage++] = states;
      }
      return getTextureFactorFade(stages);
    }

    // Stage 0 of a lit, textured draw call: its color is the texture times the vertex color.
    StageStates makeStage0(DWORD alphaOp, DWORD alphaArg1, DWORD alphaArg2 = D3DTA_CURRENT) {
      return makeStage(D3DTOP_MODULATE2X, D3DTA_TEXTURE, D3DTA_DIFFUSE, alphaOp, alphaArg1, alphaArg2);
    }

    const StageStates kSelectFactor = makeStage0(D3DTOP_SELECTARG1, D3DTA_TFACTOR);
    const StageStates kSelectTexture = makeStage0(D3DTOP_SELECTARG1, D3DTA_TEXTURE);
    // Stage 1 modulating the color by the texture factor and passing the alpha through.
    const StageStates kColorFactor = makeStage(D3DTOP_MODULATE, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_SELECTARG1, D3DTA_CURRENT, D3DTA_TFACTOR);
    // Stage 1 modulating the color and the alpha by the texture factor.
    const StageStates kAlphaFactor = makeStage(D3DTOP_MODULATE, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_MODULATE, D3DTA_CURRENT, D3DTA_TFACTOR);

    void testRecognizedFades() {
      require(match({ kSelectFactor }) == TextureFactorFade::Select, "A selected texture factor alpha must be a Select fade.");
      require(match({ kSelectFactor, kColorFactor }) == TextureFactorFade::Select,
              "A second stage that only modulates the color must keep the Select fade.");
      require(match({ kSelectTexture, kAlphaFactor }) == TextureFactorFade::ModulateTexture,
              "A texture alpha multiplied by the texture factor in stage 1 must be a ModulateTexture fade.");
      require(match({ kSelectTexture, makeStage(D3DTOP_SELECTARG1, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_MODULATE, D3DTA_TFACTOR, D3DTA_CURRENT) }) ==
              TextureFactorFade::ModulateTexture, "Either argument order of the stage 1 multiply must match.");
    }

    // Anything outside the two setups keeps blending.
    void testOtherSetups() {
      require(match({ kSelectTexture }) == TextureFactorFade::None, "A texture alpha alone is not a fade.");
      require(match({ kSelectTexture, kColorFactor }) == TextureFactorFade::None, "A texture factor that only modulates the color is not a fade.");
      require(match({ kSelectFactor, kAlphaFactor }) == TextureFactorFade::None, "A texture factor applied twice is not a recognized fade.");
      require(match({ makeStage0(D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR) }) == TextureFactorFade::None,
              "A stage 0 multiply is not a recognized fade.");
      require(match({ makeStage0(D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_TFACTOR) }) == TextureFactorFade::None,
              "Only SELECTARG1 selects the texture factor.");
      require(match({ makeStage0(D3DTOP_ADD, D3DTA_TEXTURE, D3DTA_TFACTOR) }) == TextureFactorFade::None,
              "An added texture factor alpha is not a fade.");
      require(match({ makeStage0(D3DTOP_SELECTARG1, D3DTA_TFACTOR | D3DTA_COMPLEMENT) }) == TextureFactorFade::None,
              "An argument modifier must not match.");
      require(match({ kSelectTexture, makeStage(D3DTOP_MODULATE, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_MODULATE2X, D3DTA_CURRENT, D3DTA_TFACTOR) }) ==
              TextureFactorFade::None, "A scaled multiply must not match.");
      require(match({ kSelectTexture, makeStage(D3DTOP_MODULATE, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_TFACTOR) }) ==
              TextureFactorFade::None, "A multiply that does not read the stage 0 result must not match.");
      require(match({ makeStage(D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_SELECTARG1, D3DTA_TFACTOR, D3DTA_CURRENT) }) ==
              TextureFactorFade::None, "Nothing runs when stage 0 is disabled.");
    }

    void testStageCountAndRegisters() {
      const StageStates passThrough = makeStage(D3DTOP_SELECTARG1, D3DTA_CURRENT, D3DTA_TFACTOR, D3DTOP_SELECTARG1, D3DTA_CURRENT, D3DTA_TFACTOR);
      require(match({ kSelectFactor, kColorFactor, passThrough }) == TextureFactorFade::None, "A third stage must not match.");
      require(match({ kSelectTexture, kAlphaFactor, passThrough }) == TextureFactorFade::None, "A third stage must not match.");

      StageStates temp0 = kSelectFactor;
      temp0[DXVK_TSS_RESULTARG] = D3DTA_TEMP;
      require(match({ temp0 }) == TextureFactorFade::None, "A stage 0 result written to TEMP must not match.");
      StageStates temp1 = kAlphaFactor;
      temp1[DXVK_TSS_RESULTARG] = D3DTA_TEMP;
      require(match({ kSelectTexture, temp1 }) == TextureFactorFade::None, "A stage 1 result written to TEMP must not match.");
    }
  }
}

int main() {
  const struct { const char* name; void (*run)(); } tests[] = {
    { "RecognizedFades", dxvk::testRecognizedFades },
    { "OtherSetups", dxvk::testOtherSetups },
    { "StageCountAndRegisters", dxvk::testStageCountAndRegisters },
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
  std::cout << "Texture factor fade matching passed (" << dxvk::checks << " checks)." << std::endl;
  return 0;
}
