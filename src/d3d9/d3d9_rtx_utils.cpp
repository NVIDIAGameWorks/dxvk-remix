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

  RtTextureArgSource convertColorSource(uint32_t source) {
    switch (source) {
    default:
    case D3DMCS_COLOR2: // TODO: support 2nd vertex color array
    case D3DMCS_MATERIAL: return RtTextureArgSource::None;
    case D3DMCS_COLOR1: return RtTextureArgSource::VertexColor0;
    }
  }

  RtTextureArgSource convertTextureArg(uint32_t arg, RtTextureArgSource color0, RtTextureArgSource color1) {
    // TODO: support more D3DTA_* macro when necessary
    switch (arg) {
    default: return RtTextureArgSource::None;
    case D3DTA_CURRENT:
    case D3DTA_DIFFUSE: return color0;
    case D3DTA_SPECULAR: return color1;
    case D3DTA_TEXTURE: return RtTextureArgSource::Texture;
    case D3DTA_TFACTOR: return RtTextureArgSource::TFactor;
    }
  }

  void setTextureStageState(const Direct3DState9& d3d9State, const uint32_t stageIdx, bool useStageTextureFactorBlending, bool useMultipleStageTextureFactorBlending, LegacyMaterialData& materialData, DrawCallTransforms& transformData) {
    materialData.textureColorOperation = convertTextureOp(d3d9State.textureStages[stageIdx][DXVK_TSS_COLOROP]);
    materialData.textureColorArg1Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_COLORARG1], materialData.diffuseColorSource, materialData.specularColorSource);
    materialData.textureColorArg2Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_COLORARG2], materialData.diffuseColorSource, materialData.specularColorSource);
    if (!useStageTextureFactorBlending) {
      if (materialData.textureColorArg1Source == RtTextureArgSource::TFactor) {
        materialData.textureColorArg1Source = RtTextureArgSource::None;
      }
      if (materialData.textureColorArg2Source == RtTextureArgSource::TFactor) {
        materialData.textureColorArg2Source = RtTextureArgSource::None;
      }
    }

    materialData.textureAlphaOperation = convertTextureOp(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAOP]);
    materialData.textureAlphaArg1Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAARG1], materialData.diffuseColorSource, materialData.specularColorSource);
    materialData.textureAlphaArg2Source = convertTextureArg(d3d9State.textureStages[stageIdx][DXVK_TSS_ALPHAARG2], materialData.diffuseColorSource, materialData.specularColorSource);
    if (!useStageTextureFactorBlending) {
      if (materialData.textureAlphaArg1Source == RtTextureArgSource::TFactor) {
        materialData.textureAlphaArg1Source = RtTextureArgSource::None;
      }
      if (materialData.textureAlphaArg2Source == RtTextureArgSource::TFactor) {
        materialData.textureAlphaArg2Source = RtTextureArgSource::None;
      }
    }

    materialData.isTextureFactorBlend = useMultipleStageTextureFactorBlending;

    const DWORD texcoordIndex = d3d9State.textureStages[stageIdx][DXVK_TSS_TEXCOORDINDEX];
    const DWORD transformFlags = d3d9State.textureStages[stageIdx][DXVK_TSS_TEXTURETRANSFORMFLAGS];

    const auto textureTransformCount = transformFlags & 0x3;

    if (textureTransformCount != D3DTTFF_DISABLE) {
      transformData.textureTransform = d3d9State.transforms[GetTransformIndex(D3DTS_TEXTURE0) + stageIdx];

      if (textureTransformCount > 2) {
        ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Use of texture transform element counts beyond 2 is not supported in Remix yet (and thus will be clamped to 2 elements).")));
      }

      // Todo: Store texture transform element count (1-4) in the future.
    } else {
      transformData.textureTransform = Matrix4();
    }

    if (transformFlags & D3DTTFF_PROJECTED) {
      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Use of projected texture transform detected, but it's not supported in Remix yet.")));

      // Todo: Store texture transform projection flag in the future.
    }

    switch (texcoordIndex) {
    default:
    case D3DTSS_TCI_PASSTHRU:
      transformData.texgenMode = TexGenMode::None;

      break;
    case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
    case D3DTSS_TCI_SPHEREMAP:
      transformData.texgenMode = TexGenMode::None;

      ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Use of special TCI flags detected (spheremap or camera space reflection vector), but they're not supported in Remix yet.")));

      break;
    case D3DTSS_TCI_CAMERASPACEPOSITION:
      transformData.texgenMode = TexGenMode::ViewPositions;

      break;
    case D3DTSS_TCI_CAMERASPACENORMAL:
      transformData.texgenMode = TexGenMode::ViewNormals;

      break;
    }
  }

  void setLegacyMaterialState(D3D9DeviceEx* pDevice, const bool alphaSwizzle, LegacyMaterialData& materialData) {
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

    materialData.alphaTestEnabled = pDevice->IsAlphaTestEnabled();
    materialData.alphaTestCompareOp = materialData.alphaTestEnabled ? DecodeCompareOp(D3DCMPFUNC(d3d9State.renderStates[D3DRS_ALPHAFUNC])) : VK_COMPARE_OP_ALWAYS;
    materialData.alphaTestReferenceValue = d3d9State.renderStates[D3DRS_ALPHAREF] & 0xFF; // Note: Only bottom 8 bits should be used as per the standard.

    materialData.diffuseColorSource = convertColorSource(diffuseSource);
    materialData.specularColorSource = convertColorSource(specularSource);

    materialData.tFactor = d3d9State.renderStates[D3DRS_TEXTUREFACTOR];

    DxvkBlendMode& m = materialData.blendMode;
    m.enableBlending = d3d9State.renderStates[D3DRS_ALPHABLENDENABLE] != FALSE;

    D3D9BlendState color;
    color.Src = D3DBLEND(d3d9State.renderStates[D3DRS_SRCBLEND]);
    color.Dst = D3DBLEND(d3d9State.renderStates[D3DRS_DESTBLEND]);
    color.Op = D3DBLENDOP(d3d9State.renderStates[D3DRS_BLENDOP]);
    FixupBlendState(color);

    D3D9BlendState alpha = color;
    if (d3d9State.renderStates[D3DRS_SEPARATEALPHABLENDENABLE]) {
      alpha.Src = D3DBLEND(d3d9State.renderStates[D3DRS_SRCBLENDALPHA]);
      alpha.Dst = D3DBLEND(d3d9State.renderStates[D3DRS_DESTBLENDALPHA]);
      alpha.Op = D3DBLENDOP(d3d9State.renderStates[D3DRS_BLENDOPALPHA]);
      FixupBlendState(alpha);
    }

    m.colorSrcFactor = DecodeBlendFactor(color.Src, false);
    m.colorDstFactor = DecodeBlendFactor(color.Dst, false);
    m.colorBlendOp = DecodeBlendOp(color.Op);

    m.alphaSrcFactor = DecodeBlendFactor(alpha.Src, true);
    m.alphaDstFactor = DecodeBlendFactor(alpha.Dst, true);
    m.alphaBlendOp = DecodeBlendOp(alpha.Op);

    m.writeMask = d3d9State.renderStates[ColorWriteIndex(0)];

    auto NormalizeFactor = [alphaSwizzle](VkBlendFactor f) {
      if (alphaSwizzle) {
        if (f == VK_BLEND_FACTOR_DST_ALPHA) {
          return VK_BLEND_FACTOR_ONE;
        }
        if (f == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA) {
          return VK_BLEND_FACTOR_ZERO;
        }
      }
      return f;
    };
    m.colorSrcFactor = NormalizeFactor(m.colorSrcFactor);
    m.colorDstFactor = NormalizeFactor(m.colorDstFactor);
    m.alphaSrcFactor = NormalizeFactor(m.alphaSrcFactor);
    m.alphaDstFactor = NormalizeFactor(m.alphaDstFactor);

    materialData.d3dMaterial = d3d9State.material;
  }

  void setFogState(D3D9DeviceEx* pDevice, FogState& fogState) {
    const Direct3DState9& d3d9State = *pDevice->GetRawState();

    if (d3d9State.renderStates[D3DRS_FOGENABLE]) {
      Vector4 color;
      DecodeD3DCOLOR(D3DCOLOR(d3d9State.renderStates[D3DRS_FOGCOLOR]), color.data);

      float end = bit::cast<float>(d3d9State.renderStates[D3DRS_FOGEND]);
      float start = bit::cast<float>(d3d9State.renderStates[D3DRS_FOGSTART]);

      fogState.mode = d3d9State.renderStates[D3DRS_FOGTABLEMODE] != D3DFOG_NONE ? d3d9State.renderStates[D3DRS_FOGTABLEMODE] : d3d9State.renderStates[D3DRS_FOGVERTEXMODE];
      fogState.color = color.xyz();
      fogState.scale = 1.0f / (end - start);
      fogState.end = end;
      fogState.density = bit::cast<float>(d3d9State.renderStates[D3DRS_FOGDENSITY]);
    } else {
      fogState.mode = D3DFOG_NONE;
    }
  }
}
