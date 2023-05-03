#pragma once

#include "d3d9_include.h"
#include "d3d9_state.h"

#include "d3d9_util.h"
#include "d3d9_buffer.h"

#include "d3d9_rtx_utils.h"
#include "d3d9_device.h"

#include "../util/util_fastops.h"
#include "../util/util_math.h"

namespace dxvk {
  bool getMinMaxBoneIndices(const uint8_t* pBoneIndices, uint32_t stride, uint32_t vertexCount, uint32_t numBonesPerVertex, int& minBoneIndex, int& maxBoneIndex) {
    ScopedCpuProfileZone();
    if (vertexCount == 0)
      return false;

    minBoneIndex = 256;
    maxBoneIndex = -1;

    for (uint32_t i = 0; i < vertexCount; ++i) {
      for (uint32_t j = 0; j < numBonesPerVertex; ++j) {
        minBoneIndex = std::min(minBoneIndex, (int) pBoneIndices[j]);
        maxBoneIndex = std::max(maxBoneIndex, (int) pBoneIndices[j]);
      }
      pBoneIndices += stride;
    }

    return true;
  }

  bool isRenderTargetPrimary(const D3DPRESENT_PARAMETERS& presenterParams, const D3D9_COMMON_TEXTURE_DESC* renderTargetDesc) {
    return presenterParams.BackBufferWidth == renderTargetDesc->Width &&
           presenterParams.BackBufferHeight == renderTargetDesc->Height;
  }

  DxvkRtTextureOperation convertTextureOp(uint32_t op) {
    // TODO: support more D3DTEXTUREOP member when necessary
    switch (op) {
    default:
    case D3DTOP_MODULATE: return DxvkRtTextureOperation::Modulate;
    case D3DTOP_DISABLE: return DxvkRtTextureOperation::Disable;
    case D3DTOP_SELECTARG1: return DxvkRtTextureOperation::SelectArg1;
    case D3DTOP_SELECTARG2: return DxvkRtTextureOperation::SelectArg2;
    case D3DTOP_MODULATE2X: return DxvkRtTextureOperation::Modulate2x;
    case D3DTOP_MODULATE4X: return DxvkRtTextureOperation::Modulate4x;
    case D3DTOP_ADD: return DxvkRtTextureOperation::Add;
    }
  }

  DxvkRtTextureArgSource convertTextureArg(uint32_t arg) {
    // TODO: support more D3DTA_* macro when necessary
    switch (arg) {
    default:
    case D3DTA_CURRENT:
    case D3DTA_DIFFUSE: return DxvkRtTextureArgSource::Diffuse;
    case D3DTA_SPECULAR: return DxvkRtTextureArgSource::Specular;
    case D3DTA_TEXTURE: return DxvkRtTextureArgSource::Texture;
    case D3DTA_TFACTOR: return DxvkRtTextureArgSource::TFactor;
    }
  }

  DxvkRtColorSource convertColorSource(uint32_t source) {
    switch (source) {
    default:
    case D3DMCS_COLOR2: // TODO: support 2nd vertex color array
    case D3DMCS_MATERIAL: return DxvkRtColorSource::None;
    case D3DMCS_COLOR1: return DxvkRtColorSource::Color0;
    }
  }


  DxvkRtxTextureStageState createTextureStageState(const Direct3DState9& d3d9State, const uint32_t stageIdx) {
    DxvkRtxTextureStageState stage;
    stage.colorOperation = convertTextureOp(d3d9State.textureStages[stageIdx][DXVK_TSS_COLOROP]);
    stage.colorArg1Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_COLORARG1]);
    stage.colorArg2Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_COLORARG2]);

    stage.alphaOperation = convertTextureOp(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAOP]);
    stage.alphaArg1Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAARG1]);
    stage.alphaArg2Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAARG2]);

    stage.texcoordIndex = d3d9State.textureStages[stageIdx][DXVK_TSS_TEXCOORDINDEX];

    stage.transformFlags = d3d9State.textureStages[stageIdx][DXVK_TSS_TEXTURETRANSFORMFLAGS];
    stage.transform = d3d9State.transforms[GetTransformIndex(D3DTS_TEXTURE0) + stageIdx];
    return stage;
  }

  DxvkRtxLegacyState createLegacyState(D3D9DeviceEx* pDevice) {
    assert(pDevice != nullptr);
    const Direct3DState9& d3d9State = *pDevice->GetRawState();

    const bool hasPositionT = d3d9State.vertexDecl != nullptr && d3d9State.vertexDecl->TestFlag(D3D9VertexDeclFlag::HasPositionT);
    const bool hasColor0 = d3d9State.vertexDecl != nullptr && d3d9State.vertexDecl->TestFlag(D3D9VertexDeclFlag::HasColor0);
    const bool hasColor1 = d3d9State.vertexDecl != nullptr && d3d9State.vertexDecl->TestFlag(D3D9VertexDeclFlag::HasColor1);
    const bool lighting = d3d9State.renderStates[D3DRS_LIGHTING] != 0 && !hasPositionT;

    uint32_t diffuseSource = hasColor0 ? D3DMCS_COLOR1 : D3DMCS_MATERIAL;
    uint32_t specularSource = hasColor1 ? D3DMCS_COLOR2 : D3DMCS_MATERIAL;
    if (lighting) {
      const bool colorVertex = d3d9State.renderStates[D3DRS_COLORVERTEX] != 0;
      const uint32_t mask = (lighting && colorVertex) ? (diffuseSource | specularSource) : 0;

      diffuseSource = d3d9State.renderStates[D3DRS_DIFFUSEMATERIALSOURCE] & mask;
      specularSource = d3d9State.renderStates[D3DRS_SPECULARMATERIALSOURCE] & mask;
    }

    DxvkRtxLegacyState state;
    state.alphaTestEnabled = pDevice->IsAlphaTestEnabled();
    state.alphaTestCompareOp = state.alphaTestEnabled ? DecodeCompareOp(D3DCMPFUNC(d3d9State.renderStates[D3DRS_ALPHAFUNC])) : VK_COMPARE_OP_ALWAYS;
    state.alphaTestReferenceValue = d3d9State.renderStates[D3DRS_ALPHAREF] & 0xFF; // Note: Only bottom 8 bits should be used as per the standard.

    state.diffuseColorSource = convertColorSource(diffuseSource);
    state.specularColorSource = convertColorSource(specularSource);

    state.tFactor = d3d9State.renderStates[D3DRS_TEXTUREFACTOR];

    return state;
  }
}
