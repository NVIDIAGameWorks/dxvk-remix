#pragma once

#include <array>
#include <cstdint>

#include "d3d9_caps.h"
#include "d3d9_state.h"
#include "d3d9_util.h"
#include "../dxvk/rtx_render/rtx_materials.h"

namespace dxvk {
  using D3D9TextureStageState = std::array<DWORD, TextureStageStateCount>;
  using D3D9TextureStageStates = std::array<D3D9TextureStageState, caps::TextureStageCount>;

  /**
    * \brief: Matches the fixed-function stage setups that fade the alpha by the texture factor alpha, see TextureFactorFade.
    *         Only these exact setups match: any other operation, argument, argument modifier, result register or additional
    *         stage returns TextureFactorFade::None, so rtx.enableAlphaBlendDissolve leaves such draw calls blending.
    *         The caller must only use the result when Remix emulates stage 0.
    *
    *         Select:          stage 0 alpha = SELECTARG1(TFACTOR); stage 1 disabled, or its alpha = SELECTARG1(CURRENT).
    *         ModulateTexture: stage 0 alpha = SELECTARG1(TEXTURE); stage 1 alpha = MODULATE(CURRENT, TFACTOR) in either order.
    *         In both, every stage writes CURRENT and stage 2 is disabled.
    *
    * \param [in] textureStages: texture stage states of all stages
    */
  inline TextureFactorFade getTextureFactorFade(const D3D9TextureStageStates& textureStages) {
    const D3D9TextureStageState& stage0 = textureStages[0];
    const D3D9TextureStageState& stage1 = textureStages[1];

    if (stage0[DXVK_TSS_COLOROP] == D3DTOP_DISABLE || stage0[DXVK_TSS_RESULTARG] != D3DTA_CURRENT ||
        stage0[DXVK_TSS_ALPHAOP] != D3DTOP_SELECTARG1) {
      return TextureFactorFade::None;
    }

    const bool isStage1Enabled = stage1[DXVK_TSS_COLOROP] != D3DTOP_DISABLE;
    if (isStage1Enabled && (stage1[DXVK_TSS_RESULTARG] != D3DTA_CURRENT || textureStages[2][DXVK_TSS_COLOROP] != D3DTOP_DISABLE)) {
      return TextureFactorFade::None;
    }

    const DWORD stage1Arg1 = stage1[DXVK_TSS_ALPHAARG1];
    const DWORD stage1Arg2 = stage1[DXVK_TSS_ALPHAARG2];

    if (stage0[DXVK_TSS_ALPHAARG1] == D3DTA_TFACTOR) {
      const bool passesAlpha = !isStage1Enabled ||
        (stage1[DXVK_TSS_ALPHAOP] == D3DTOP_SELECTARG1 && stage1Arg1 == D3DTA_CURRENT);
      return passesAlpha ? TextureFactorFade::Select : TextureFactorFade::None;
    }

    if (stage0[DXVK_TSS_ALPHAARG1] == D3DTA_TEXTURE && isStage1Enabled && stage1[DXVK_TSS_ALPHAOP] == D3DTOP_MODULATE &&
        ((stage1Arg1 == D3DTA_CURRENT && stage1Arg2 == D3DTA_TFACTOR) || (stage1Arg1 == D3DTA_TFACTOR && stage1Arg2 == D3DTA_CURRENT))) {
      return TextureFactorFade::ModulateTexture;
    }

    return TextureFactorFade::None;
  }
}
