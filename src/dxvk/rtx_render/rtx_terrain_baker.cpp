/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_terrain_baker.h"

#include "dxvk_device.h"
#include "../tracy/Tracy.hpp"
#include "dxvk_scoped_annotation.h"
#include "rtx_imgui.h"
#include "rtx_option.h"
#include "rtx_texture.h"

#include "../d3d9/d3d9_state.h"
#include "../d3d9/d3d9_spec_constants.h"
#include "../dxso/dxso_util.h"
#include "../../d3d9/d3d9_rtx.h"
#include "../../dxso/dxso_util.h"
#include "../../d3d9/d3d9_caps.h"

namespace dxvk {
  VkFormat getTextureFormat(ReplacementMaterialTextureType::Enum textureType) {
    switch (textureType) {
    case ReplacementMaterialTextureType::Normal:
    case ReplacementMaterialTextureType::Tangent:
      return VK_FORMAT_R8G8B8A8_SNORM;
      break;

    case ReplacementMaterialTextureType::AlbedoOpacity:
    case ReplacementMaterialTextureType::Emissive:
      return VK_FORMAT_R8G8B8A8_UNORM;
      break;

      // R16
    case ReplacementMaterialTextureType::Height:
    case ReplacementMaterialTextureType::Roughness:
    case ReplacementMaterialTextureType::Metallic:
      return VK_FORMAT_R8_UNORM;
      break;

    default:
      assert(0);
      return VK_FORMAT_UNDEFINED;
      break;
    }
  }

  bool TerrainBaker::isPSReplacementSupportEnabled(const DrawCallState& drawCallState) {
    if (drawCallState.usesPixelShader) {
      return Material::replacementSupportInPS() && 
             Material::replacementSupportInPS_programmableShaders() &&
             drawCallState.programmablePixelShaderInfo.majorVersion() <= 1;
    } else {
      return Material::replacementSupportInPS() && Material::replacementSupportInPS_fixedFunction();
    }
  }

  // Gathers available textures from a replacement material and 
  // runs a compute shader to convert them into a compatible format for baking
  bool TerrainBaker::gatherAndPreprocessReplacementTextures(Rc<RtxContext> ctx,
                                                            const DrawCallState& drawCallState,
                                                            OpaqueMaterialData* replacementMaterial,
                                                            std::vector<RtxGeometryUtils::TextureConversionInfo>& replacementTextures) {
    if (!replacementMaterial) {
      return false;
    }

    SceneManager& sceneManager = ctx->getSceneManager();
    Resources& resourceManager = ctx->getResourceManager();
    const bool hasTexcoords = drawCallState.hasTextureCoordinates();
    // We're going to use this to create a modified sampler for textures.
    DxvkSampler* pOriginalSampler = drawCallState.getMaterialData().getSampler().ptr();
    Rc<DxvkContext> dxvkCtx = ctx;

    // Opacity texture is currently required for blending to work. 
    // Scenarios where blending does not require a colorOpacity texture or 
    // replacement material is using a colorOpacity constant are not currently supported
    if (!replacementMaterial->getAlbedoOpacityTexture().isValid()) {
      ONCE(Logger::warn(str::format("[RTX Texture Baker] Replacement material for ", drawCallState.getMaterialData().getHash(), " does not have a color opacity texture.",
                                    " This scenario is not currently supported by the texture baker. Ignoring the replacement material.")));
      return false;
    }

    if (!drawCallState.getMaterialData().getColorTexture2().isValid()) {
      ONCE(Logger::warn(str::format("[RTX Texture Baker] Legacy material for ", drawCallState.getMaterialData().getHash(), " has a second color texture.",
                                    "Only single texture legacy materials are supported. Ignoring the second color texture.")));
    }

    // Ensures a texture stays in VidMem
    auto trackAndFinalizeTexture = [&](TextureRef& texture) {
      uint32_t unusedTextureIndex;
      sceneManager.trackTexture(ctx, texture, unusedTextureIndex, hasTexcoords, nullptr);
      // Force the full resolution promotion
      if (texture.isPromotable()) {
        texture.finalizePendingPromotion();
      }
    };

    // Track the source albedo opacity texture to keep it in VidMem as it's needed for baking
    trackAndFinalizeTexture(replacementMaterial->getAlbedoOpacityTexture());

    const DxvkImageCreateInfo& aoImageInfo = replacementMaterial->getAlbedoOpacityTexture().getImageView()->imageInfo();

    // Returns a scaled down the extent that fits within the max resolution constraint preserving the aspect ratio (barring float to integer conversion errors)
    auto calculateScaledResolution2D = [&](VkExtent3D extent, const uint32_t maxResolutionPerDimension) {
      const float scalingFactor = 
        std::min(
          1.f,     // Don't scale up the input dimensions
          1 / std::max(
            extent.width / static_cast<float>(maxResolutionPerDimension),
            extent.height / static_cast<float>(maxResolutionPerDimension)));

      extent.width = static_cast<uint32_t>(extent.width * scalingFactor);
      extent.height = static_cast<uint32_t>(extent.height * scalingFactor);

      return extent;
    };

    auto addValidTexture = [&](TextureRef& texture, ReplacementMaterialTextureType::Enum textureType) {

      if (!texture.isValid()) {
        return;
      }

      // Track the source material texture to keep it in VidMem while it's being used for baking.
      // This needs to be done prior to checking for having valid views 
      // since the views are not created until the texture is promoted
      trackAndFinalizeTexture(texture);

      if (!texture.getImageView()) {
        return;
      }

      RtxGeometryUtils::TextureConversionInfo& conversionInfo = replacementTextures.emplace_back();
      conversionInfo.type = textureType;
      conversionInfo.sourceTexture = &texture;

      if (isPSReplacementSupportEnabled(drawCallState)) {
        conversionInfo.targetTexture = TextureRef(texture.getImageView());
      } else {
        const DxvkImageCreateInfo& imageInfo = texture.getImageView()->imageInfo();
        const VkExtent3D& extent = imageInfo.extent;

        const VkExtent3D adjustedExtent = calculateScaledResolution2D(extent, Material::maxResolutionToUseForReplacementMaterials());

        TextureKey textureKey;
        textureKey.width = adjustedExtent.width;
        textureKey.height = adjustedExtent.height;
        textureKey.textureType = textureType;
        XXH64_hash_t textureKeyHash = textureKey.calculateHash();

        auto textureIter = m_stagingTextureCache.find(textureKeyHash);

        // Staging texture must be 4 channel as the 4th channel will contain opacity
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

        if (textureType == ReplacementMaterialTextureType::Normal ||
            textureType == ReplacementMaterialTextureType::Tangent) {
          format = VK_FORMAT_R8G8B8A8_SNORM;
        }

        // No matching cached texture found, create a new one
        if (textureIter == m_stagingTextureCache.end()) {
          textureIter =
            m_stagingTextureCache.emplace(
              textureKeyHash,
              Resources::createImageResource(dxvkCtx, "terrain baking: staging replacement texture", adjustedExtent,
                                             format, 1, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, 0)).first;
        }

        conversionInfo.targetTexture = TextureRef(textureIter->second.view);

        // Track lifetime of the resource now since targetTexture object is about to get destroyed
        ctx->getCommandList()->trackResource<DxvkAccess::Write>(textureIter->second.image);
      }
    };

    // Gather all replacement textures that need to be preprocessed
    replacementTextures.reserve(ReplacementMaterialTextureType::Count);

    if (Material::bakeSecondaryPBRTextures()) {
      addValidTexture(replacementMaterial->getNormalTexture(), ReplacementMaterialTextureType::Normal);
      addValidTexture(replacementMaterial->getTangentTexture(), ReplacementMaterialTextureType::Tangent);
      addValidTexture(replacementMaterial->getHeightTexture(), ReplacementMaterialTextureType::Height);
      addValidTexture(replacementMaterial->getRoughnessTexture(), ReplacementMaterialTextureType::Roughness);
      addValidTexture(replacementMaterial->getMetallicTexture(), ReplacementMaterialTextureType::Metallic);
      addValidTexture(replacementMaterial->getEmissiveColorTexture(), ReplacementMaterialTextureType::Emissive);


      if (!isPSReplacementSupportEnabled(drawCallState)) {
        // Pre-process textures to be compatible with baking
        ctx->getCommonObjects()->metaGeometryUtils().decodeAndAddOpacity(ctx, replacementMaterial->getAlbedoOpacityTexture(), replacementTextures);
      }
    }

    // Add the remaining albedo opacity which does not needed to be preprocessed to the texture list for baking.
    RtxGeometryUtils::TextureConversionInfo& conversionInfo = replacementTextures.emplace_back();
    conversionInfo.type = ReplacementMaterialTextureType::AlbedoOpacity;
    conversionInfo.sourceTexture = nullptr;
    conversionInfo.targetTexture = replacementMaterial->getAlbedoOpacityTexture();

    // Move albedo opacity to the front of the baking queue as the baking aborts if baking of albedo opacity texture fails
    if (replacementTextures.size() > 1) {
      std::swap(replacementTextures.front(), replacementTextures.back());
    }

    return true;
  }

  bool TerrainBaker::bakeDrawCall(Rc<RtxContext> ctx,
                                  const DxvkContextState& dxvkCtxState,
                                  DxvkRaytracingInstanceState& rtState,
                                  const DrawParameters& drawParams,
                                  const DrawCallState& drawCallState,
                                  OpaqueMaterialData* replacementMaterial,
                                  Matrix4& textureTransformOut) {

    ScopedGpuProfileZone(ctx, "Terrain Baker: Bake Draw Call");

    SceneManager& sceneManager = ctx->getSceneManager();
    Resources& resourceManager = ctx->getResourceManager();
    RtxTextureManager& textureManger = ctx->getCommonObjects()->getTextureManager();
    const RtCamera& camera = sceneManager.getCamera();

    if (drawCallState.usesVertexShader && !D3D9Rtx::useVertexCapture()) {
      ONCE(Logger::warn(str::format("[RTX Terrain Baker] Terrain texture corresponds to a draw call with programmable Vertex Shader usage. Vertex capture must be enabled to support baking of such draw calls. Ignoring the draw call.")));
      return false;
    }

    if (!Material::bakeReplacementMaterials()) {
      replacementMaterial = nullptr;
    }

    // Register mesh and preprocess state for baking for this frame
    registerTerrainMesh(ctx, dxvkCtxState, drawCallState);

    if (!debugDisableBinding()) {
      textureTransformOut = m_bakingParams.viewToCascade0TextureSpace;
    }

    if (debugDisableBaking()) {
      const bool isBaked =
        (debugDisableBinding() ? false : true) &&
        getTerrainTexture(ReplacementMaterialTextureType::AlbedoOpacity).view != nullptr;

      // Recreate material data as it will be needed and textures are available even though baking is currently disabled
      if (isBaked) {
        updateMaterialData(ctx);
      }
      
      return isBaked;
    }

    union UnifiedCB {
      D3D9RtxVertexCaptureData programmablePipeline;
      D3D9FixedFunctionVS fixedFunction;

      UnifiedCB() { }
    };

    UnifiedCB prevCB;

    if (drawCallState.usesVertexShader) {
      prevCB.programmablePipeline = *static_cast<D3D9RtxVertexCaptureData*>(rtState.vertexCaptureCB->mapPtr(0));
    } else {
      prevCB.fixedFunction = *static_cast<D3D9FixedFunctionVS*>(rtState.vsFixedFunctionCB->mapPtr(0));
    }

    const float2 float2CascadeLevelResolution = float2 {
      static_cast<float>(m_bakingParams.cascadeLevelResolution.width),
      static_cast<float>(m_bakingParams.cascadeLevelResolution.height)
    };

    // Save viewports
    const uint32_t prevViewportCount = dxvkCtxState.gp.state.rs.viewportCount();
    const DxvkViewportState prevViewportState = dxvkCtxState.vp;

    // Save previous render targets
    DxvkRenderTargets prevRenderTargets = dxvkCtxState.om.renderTargets; 
    Rc<DxvkSampler> prevSecondaryResourceSlotSampler;   // Initialized when overriden

    // Gather replacement textures, if available, to be used for baking
    std::vector<RtxGeometryUtils::TextureConversionInfo> replacementTextures;
    bool bakeReplacementTextures = gatherAndPreprocessReplacementTextures(ctx, drawCallState, replacementMaterial, replacementTextures);

    const uint32_t numTexturesToBake = bakeReplacementTextures ? replacementTextures.size() : 1;

    // Lookup texture slots to bind replacement textures at
    uint32_t colorTextureSlot = kInvalidResourceSlot;
    uint32_t secondaryTextureSlot = kInvalidResourceSlot;

    if (bakeReplacementTextures) {
      colorTextureSlot = drawCallState.getMaterialData().getColorTextureSlot(0);

      // Check that the slot for secondary textures is available
      const uint32_t textureSlot = drawCallState.getMaterialData().getColorTextureSlot(kTerrainBakerSecondaryTextureStage);

      if (textureSlot == kInvalidResourceSlot) {
        auto shaderSampler = RemapStateSamplerShader(static_cast<uint8_t>(kTerrainBakerSecondaryTextureStage));
        const uint32_t bindingIndex = shaderSampler.second;
        secondaryTextureSlot = computeResourceSlotId(DxsoProgramType::PixelShader, DxsoBindingType::Image, bindingIndex);
      }
    }

    // Update spec constants
    DxvkScInfo prevSpecConstantsInfo = ctx->getSpecConstantsInfo(VK_PIPELINE_BIND_POINT_GRAPHICS);
    {
      // Disable fog

      ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::FogEnabled, false);
      ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::VertexFogMode, D3DFOG_NONE);
      ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::PixelFogMode, D3DFOG_NONE);

      if (drawCallState.usesVertexShader) {
        ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::CustomVertexTransformEnabled, true);
      }
    }

    bool bakingResult = false;

    ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::ReplacementTextureCategory, static_cast<uint32_t>(ReplacementMaterialTextureCategory::AlbedoOpacity));

    // Bake all material textures
    for (uint32_t iTexture = 0; iTexture < numTexturesToBake; iTexture++) {
      
      ReplacementMaterialTextureType::Enum textureType = ReplacementMaterialTextureType::AlbedoOpacity;

      // Bind a source replacement texture to bake, if available.
      // Otherwise the legacy albedoOpacity texture that's already bound will be baked
      if (bakeReplacementTextures) {
        TextureRef& replacementTexture = replacementTextures[iTexture].targetTexture;
        textureType = replacementTextures[iTexture].type;

        ctx->bindResourceView(colorTextureSlot, replacementTexture.getImageView(), nullptr);

        if (isPSReplacementSupportEnabled(drawCallState)) {

          if (drawCallState.usesPixelShader) {
            if (textureType != ReplacementMaterialTextureType::Enum::AlbedoOpacity &&
                drawCallState.programmablePixelShaderInfo.majorVersion() >= 2) {
              // Unsupported right now - REMIX-2223 
              ONCE(Logger::err("[RTX Terrain Baker] Draw call associated with a terrain texture uses a shader model version 2 or higher. This is currently not supported when baking replacement PBR material textures other than albedoOpacity. Skipping baking of the replacement texture of all but albedoOpacity."));
              continue;
            }
          }

          // Set texture category in a specconst
          switch (textureType) {
          case ReplacementMaterialTextureType::Enum::AlbedoOpacity:
          default:
            ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::ReplacementTextureCategory, static_cast<uint32_t>(ReplacementMaterialTextureCategory::AlbedoOpacity));
            break;

          case ReplacementMaterialTextureType::Enum::Normal:
          case ReplacementMaterialTextureType::Enum::Tangent:
            ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::ReplacementTextureCategory, static_cast<uint32_t>(ReplacementMaterialTextureCategory::SecondaryOctahedralEncoded));
            break;

          case ReplacementMaterialTextureType::Enum::Roughness:
          case ReplacementMaterialTextureType::Enum::Metallic:
          case ReplacementMaterialTextureType::Enum::Height:
          case ReplacementMaterialTextureType::Enum::Emissive:
            ctx->setSpecConstant(VK_PIPELINE_BIND_POINT_GRAPHICS, D3D9SpecConstantId::ReplacementTextureCategory, static_cast<uint32_t>(ReplacementMaterialTextureCategory::SecondaryRaw));
            break;
          }

          // Finalize bindings when baking a secondary non-albedo opacity texture
          if (textureType != ReplacementMaterialTextureType::AlbedoOpacity) {
            if (secondaryTextureSlot == kInvalidResourceSlot) {
              ONCE(Logger::err("[RTX Terrain Baker] Failed to retrieve a valid secondary texture slot required for baking of secondary replacement textures. Possibly due to it being used by the terrain draw call itself. Skipping baking for all but the AlbedoOpacity replacement texture."));
              continue;
            }

            // Bind the albedo opacity texture as a secondary texture when baking non-albedo opacity replacement textures
            TextureRef& albedoOpacityReplacementTexture = replacementTextures[ReplacementMaterialTextureType::AlbedoOpacity].targetTexture;
            ctx->bindResourceView(secondaryTextureSlot, albedoOpacityReplacementTexture.getImageView(), nullptr);

            // Bind a sampler for the secondary texture
            prevSecondaryResourceSlotSampler = ctx->getShaderResourceSlot(secondaryTextureSlot).sampler;
            ctx->bindResourceSampler(secondaryTextureSlot, ctx->getShaderResourceSlot(colorTextureSlot).sampler);
          }
        }
      }

      // Bind terrain texture as render target 
      {
        const Rc<DxvkImageView>& terrainTextureView =
          getTerrainTexture(ctx, textureManger, textureType, m_bakingParams.cascadeMapResolution.width,
                            m_bakingParams.cascadeMapResolution.height).view;

        if (terrainTextureView == nullptr) {
          if (textureType == ReplacementMaterialTextureType::AlbedoOpacity) {
            ONCE(Logger::err(str::format("[RTX Terrain Baker] Failed to retrieve a terrain texture of type albedo opacity. This texture is required for baking of any replacement texture. Skipping baking of the material for this draw call.")));
            break;
          } else {
            ONCE(Logger::err(str::format("[RTX Terrain Baker] Failed to retrieve a terrain texture of type ", static_cast<uint32_t>(textureType), ". Skipping baking of the texture.")));
            continue;
          }
        }

        // Bind the target terrain texture as render target
        DxvkRenderTargets terrainRt;
        terrainRt.color[0].view = terrainTextureView;
        terrainRt.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;
        ctx->bindRenderTargets(terrainRt);
      
        m_materialTextures[textureType].markAsBaked();
      }

      const Matrix4& world = drawCallState.usesVertexShader ? prevCB.programmablePipeline.normalTransform : prevCB.fixedFunction.World;
      Matrix4 worldSceneView = m_bakingParams.sceneView * world;

      // Render into all cascade levels. 
      // The levels are tiled left to right top to bottom in the combined render target texture
      for (uint32_t iCascade = 0; iCascade < m_bakingParams.numCascades; iCascade++) {

        Vector2i cascade2DIndex;
        cascade2DIndex.y = iCascade / m_bakingParams.cascadeMapSize.x;
        cascade2DIndex.x = iCascade - cascade2DIndex.y * m_bakingParams.cascadeMapSize.x;

        // Set viewport which maps clip space <-1, 1> to screen space <0, resolution>.
        // Accounts for inverted y coordinate in Vulkan
        VkViewport viewport {
          cascade2DIndex.x * float2CascadeLevelResolution.x,
          (cascade2DIndex.y + 1) * float2CascadeLevelResolution.y,
          float2CascadeLevelResolution.x,
          -float2CascadeLevelResolution.y,
          0.f, 1.f
        };

        VkOffset2D cascadeOffset = VkOffset2D {
          static_cast<int>(cascade2DIndex.x * m_bakingParams.cascadeLevelResolution.width),
          static_cast<int>(cascade2DIndex.y * m_bakingParams.cascadeLevelResolution.height) };

        // Set scissor window which clips the screen space
        VkRect2D scissor = { cascadeOffset, m_bakingParams.cascadeLevelResolution };

        ctx->setViewports(1, &viewport, &scissor);

        // Update constant buffers
        // 
        // Programmable VS path
        if (drawCallState.usesVertexShader) {
          D3D9RtxVertexCaptureData& cbData = ctx->allocAndMapVertexCaptureConstantBuffer();
          cbData = prevCB.programmablePipeline;
          cbData.customWorldToProjection = m_bakingParams.bakingCameraOrthoProjection[iCascade] * worldSceneView;
        } 
        else { // Fixed function path
          D3D9FixedFunctionVS& cbData = ctx->allocAndMapFixedFunctionConstantBuffer();
          cbData = prevCB.fixedFunction;

          cbData.InverseView = m_bakingParams.inverseSceneView;
          cbData.View = m_bakingParams.sceneView;
          cbData.WorldView = worldSceneView;
          cbData.Projection = m_bakingParams.bakingCameraOrthoProjection[iCascade];

          // Disable lighting
          for (auto& light : cbData.Lights) {
            light.Diffuse = Vector4(0.f);
            light.Specular = Vector4(0.f);
            light.Ambient = Vector4(1.f);
          }
        }

        if (drawParams.indexCount == 0) {
          ctx->DxvkContext::draw(drawParams.vertexCount, drawParams.instanceCount, drawParams.vertexOffset, 0);
        } else {
          ctx->DxvkContext::drawIndexed(drawParams.indexCount, drawParams.instanceCount, drawParams.firstIndex, drawParams.vertexOffset, 0);
        }
      }

      if (textureType == ReplacementMaterialTextureType::AlbedoOpacity) {
        bakingResult = true;
      }
    }

    // Restore prev state
    {
      ctx->setViewports(prevViewportCount, prevViewportState.viewports.data(), prevViewportState.scissorRects.data());
      ctx->bindRenderTargets(prevRenderTargets);
      ctx->setSpecConstantsInfo(VK_PIPELINE_BIND_POINT_GRAPHICS, prevSpecConstantsInfo);

      if (drawCallState.usesVertexShader) {
        ctx->allocAndMapVertexCaptureConstantBuffer() = prevCB.programmablePipeline;
      } else {
        ctx->allocAndMapFixedFunctionConstantBuffer() = prevCB.fixedFunction;
      }

      if (secondaryTextureSlot != kInvalidResourceSlot) {
        // Secondary texture slot wasn't used prior to baking, so set it to a null view
        ctx->bindResourceView(secondaryTextureSlot, nullptr, nullptr);

        if (prevSecondaryResourceSlotSampler.ptr()) {
          ctx->bindResourceSampler(secondaryTextureSlot, prevSecondaryResourceSlotSampler);
        }
      }

      // Input color texture will be restored in RtxContext::bakeTerrain
    }

    updateMaterialData(ctx);

    return bakingResult;
  }

  void TerrainBaker::updateMaterialData(Rc<RtxContext> ctx) {
    if (m_hasInitializedMaterialDataThisFrame && !m_needsMaterialDataUpdate) {
      return;
    }

    // We're going to use this to create a modified sampler for terrain textures.
    // Terrain textures have only mip 0, so use nearest for mip filtering
    if (!m_terrainSampler.ptr()) {
      Resources& resourceManager = ctx->getResourceManager();
      m_terrainSampler = resourceManager.getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    }

    // ToDo use TerrainBaker's material defaults
    const LegacyMaterialDefaults& defaults = RtxOptions::Get()->legacyMaterial;
    
    auto createTextureRef = [&](ReplacementMaterialTextureType::Enum textureType) {
      return m_materialTextures[textureType].isBaked()
        ? TextureRef(m_materialTextures[textureType].texture.view)
        : TextureRef();
    };

    // Create a material with the baked material textures
    m_materialData.emplace(OpaqueMaterialData(
      createTextureRef(ReplacementMaterialTextureType::AlbedoOpacity),
      createTextureRef(ReplacementMaterialTextureType::Normal),
      createTextureRef(ReplacementMaterialTextureType::Tangent),
      createTextureRef(ReplacementMaterialTextureType::Height),
      createTextureRef(ReplacementMaterialTextureType::Roughness),
      createTextureRef(ReplacementMaterialTextureType::Metallic),
      createTextureRef(ReplacementMaterialTextureType::Emissive),
      TextureRef(), TextureRef(), TextureRef(), // SSS textures
      Material::Properties::roughnessAnisotropy(),
      Material::Properties::emissiveIntensity(),
      Vector3(1, 1, 1), // AlbedoConstant - unused since the AlbedoOpacity texture must be always present for baking
      1.f, // OpacityConstant - unused since the AlbedoOpacity texture must be always present for baking
      Material::Properties::roughnessConstant(),
      Material::Properties::metallicConstant(),
      Material::Properties::emissiveColorConstant(),
      Material::Properties::enableEmission(),
      // Setting expected constant values. Baked terrain should not need to have other values for the below material parameters set
      1, 1, 0, /* spriteSheet* */
      false, // defaults.enableThinFilm(),
      false, // defaults.alphaIsThinFilmThickness(),
      0.f,
      false, // Set to false for now, otherwise the baked terrain is not fully opaque - opaqueMaterialDefaults.UseLegacyAlphaState
      false, // opaqueMaterialDefaults.BlendEnabled,
      BlendType::kAlpha,
      false, // opaqueMaterialDefaults.InvertedBlend,
      AlphaTestType::kAlways,
      0,//opaqueMaterialDefaults.AlphaReferenceValue;
      0.0f,  // opaqueMaterialDefaults.DisplaceIn
      Vector3(),  // opaqueMaterialDefaults.subsurfaceTransmittanceColor
      0.0f,  // opaqueMaterialDefaults.subsurfaceMeasurementDistance
      Vector3(),  // opaqueMaterialDefaults.subsurfaceSingleScatteringAlbedo
      0.0f, // opaqueMaterialDefaults.subsurfaceVolumetricAnisotropy
      lss::Mdl::Filter::Nearest,
      lss::Mdl::WrapMode::Repeat, // U
      lss::Mdl::WrapMode::Repeat  // V
    ));  

    m_hasInitializedMaterialDataThisFrame = true;
    m_needsMaterialDataUpdate = false;
  }

  const Resources::Resource& TerrainBaker::getTerrainTexture(ReplacementMaterialTextureType::Enum textureType) const {
    return m_materialTextures[textureType].texture;
  }

  const Resources::Resource& TerrainBaker::getTerrainTexture(
    Rc<DxvkContext> ctx, 
    RtxTextureManager& textureManager, 
    ReplacementMaterialTextureType::Enum textureType, 
    uint32_t width, 
    uint32_t height) {
    VkExtent3D resolution = { width, height, 1 };

    Resources::Resource& texture = m_materialTextures[static_cast<uint32_t>(textureType)].texture;

    // Recreate the texture
    if (!texture.isValid() ||
        texture.image->info().extent != resolution) {

      // WAR (REMIX-1557) to force release previous terrain texture reference from texture cache since it doesn't do it automatically resulting in a leak
      if (texture.isValid()) {
        TextureRef textureRef = TextureRef(texture.view);
        textureManager.releaseTexture(textureRef);
      }

      texture = Resources::createImageResource(
        ctx, "baked terrain texture", resolution, getTextureFormat(textureType), 1, VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, 0, true);

      if (clearTerrainBeforeBaking()) {
        clearMaterialTexture(ctx, textureType);
      }

      m_needsMaterialDataUpdate = true;
    }

    return texture;
  }

  const Rc<DxvkSampler>& TerrainBaker::getTerrainSampler() const {
    return m_terrainSampler;
  }

  const MaterialData* TerrainBaker::getMaterialData() const {
    if (m_materialData.has_value()) {
      return &(*m_materialData);
    }

    return nullptr;
  }

  TerrainArgs TerrainBaker::getTerrainArgs() const {

    TerrainArgs args;

    args.cascadeMapSize = m_bakingParams.cascadeMapSize;
    args.rcpCascadeMapSize.x = 1.f / args.cascadeMapSize.x;
    args.rcpCascadeMapSize.y = 1.f / args.cascadeMapSize.y;

    args.maxCascadeLevel = m_bakingParams.numCascades - 1;
    args.lastCascadeScale = m_bakingParams.lastCascadeScale;

    return args;
  }

  void TerrainBaker::showImguiSettings() const {

    constexpr ImGuiTreeNodeFlags collapsingHeaderClosedFlags = ImGuiTreeNodeFlags_CollapsingHeader;
    constexpr ImGuiTreeNodeFlags collapsingHeaderFlags = collapsingHeaderClosedFlags | ImGuiTreeNodeFlags_DefaultOpen;
    constexpr ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;

    if (ImGui::CollapsingHeader("Terrain System [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Enable Runtime Terrain Baking", &enableBakingObject());
      ImGui::Checkbox("Use Terrain Bounding Box", &cascadeMap.useTerrainBBOXObject());
      ImGui::Checkbox("Clear Terrain Textures Before Terrain Baking", &clearTerrainBeforeBakingObject());

      if (ImGui::CollapsingHeader("Material", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        const bool isPSReplacementSupportEnabled = Material::replacementSupportInPS_fixedFunction() || Material::replacementSupportInPS_programmableShaders();
        ImGui::BeginDisabled(!isPSReplacementSupportEnabled);
        ImGui::Checkbox("Replacements Support in PS", &Material::replacementSupportInPSObject());
        ImGui::EndDisabled();

        ImGui::Checkbox("Bake Replacement Materials", &Material::bakeReplacementMaterialsObject());
        ImGui::Checkbox("Bake Secondary PBR Textures", &Material::bakeSecondaryPBRTexturesObject());
        ImGui::DragInt("Max Resolution (except for colorOpacity)", &Material::maxResolutionToUseForReplacementMaterialsObject(), 1.f, 1, 16384);

        if (ImGui::CollapsingHeader("Properties", collapsingHeaderFlags)) {
          ImGui::Indent();

          ImGui::ColorEdit3("Emissive Color", &Material::Properties::emissiveColorConstantObject());
          ImGui::Checkbox("Enable Emission", &Material::Properties::enableEmissionObject());
          ImGui::DragFloat("Emissive Intensity", &Material::Properties::emissiveIntensityObject(), 0.01f, 0.f, FLT_MAX, "%.3f", sliderFlags);
          ImGui::DragFloat("Roughness", &Material::Properties::roughnessConstantObject(), 0.01f, 0.f, 1.f, "%.3f", sliderFlags);
          ImGui::DragFloat("Metallic", &Material::Properties::metallicConstantObject(), 0.01f, 0.f, 1.f, "%.3f", sliderFlags);
          ImGui::DragFloat("Anisotropy", &Material::Properties::roughnessAnisotropyObject(), 0.01f, -1.0f, 1.f, "%.3f", sliderFlags);

          ImGui::Unindent();
        }
        ImGui::Unindent();
      }

      if (ImGui::CollapsingHeader("Cascade Map", collapsingHeaderClosedFlags)) {
        ImGui::Indent();

        ImGui::DragFloat("Cascade Map's Default Half Width [meters]", &cascadeMap.defaultHalfWidthObject(), 1.f, 0.1f, 10000.f);
        ImGui::DragFloat("Cascade Map's Default Height [meters]", &cascadeMap.defaultHeightObject(), 1.f, 0.1f, 10000.f);
        ImGui::DragFloat("First Cascade Level's Half Width [meters]", &cascadeMap.levelHalfWidthObject(), 1.f, 0.1f, 10000.f);

        ImGui::DragInt("Max Cascade Levels", &cascadeMap.maxLevelsObject(), 1.f, 1, 16);
        RTX_OPTION_CLAMP(cascadeMap.maxLevels, 1u, 16u);
        ImGui::DragInt("Texture Resolution Per Cascade Level", &cascadeMap.levelResolutionObject(), 8.f, 1, 32 * 1024);
        RTX_OPTION_CLAMP(cascadeMap.levelResolution, 1u, 32 * 1024u);
        ImGui::Checkbox("Expand Last Cascade Level", &cascadeMap.expandLastCascadeObject());

        if (ImGui::CollapsingHeader("Statistics", collapsingHeaderClosedFlags)) {
          ImGui::Indent();
        
          ImGui::Text("Cascade Levels: %u", m_bakingParams.numCascades);
          ImGui::Text("Cascade Level Resolution: %u, %u", m_bakingParams.cascadeLevelResolution.width, m_bakingParams.cascadeLevelResolution.height);
          ImGui::Text("Cascade Map Resolution: %u, %u", m_bakingParams.cascadeMapResolution.width, m_bakingParams.cascadeMapResolution.height);
        
          ImGui::Unindent();
        }

        ImGui::Unindent();
      }

      ImGui::Checkbox("Debug: Disable Baking", &debugDisableBakingObject());
      ImGui::Checkbox("Debug: Disable Binding", &debugDisableBindingObject());

      ImGui::Unindent();
    }
  }

  void TerrainBaker::calculateTerrainBBOX(const uint32_t currentFrameIndex) {
    m_bakedTerrainBBOX.invalidate();

    // Find the union of all terrain mesh BBOXes
    if (m_terrainMeshBBOXes.size() > 0) {
      for (auto& meshBBOX : m_terrainMeshBBOXes) {
        m_bakedTerrainBBOX.unionWith(meshBBOX.calculateAABBInWorldSpace());
      }
      m_terrainMeshBBOXes.clear();
      m_terrainBBOXFrameIndex = currentFrameIndex;
    }
  }

  void TerrainBaker::onFrameEnd(Rc<DxvkContext> ctx) {
    RtxTextureManager& textureManager = ctx->getCommonObjects()->getTextureManager();
    const uint32_t currentFrameIndex = ctx->getDevice()->getCurrentFrameId();

    if (TerrainBaker::needsTerrainBaking()) {
      // Expects the mesh BBOXes to be calculated by this point
      calculateTerrainBBOX(currentFrameIndex);
    }

    m_hasInitializedMaterialDataThisFrame = false;

    for (BakedTexture& texture : m_materialTextures) {
      texture.onFrameEnd(ctx);
    }

    m_stagingTextureCache.clear();

    // Destroy material data every frame so as not keep texture references around.
    // Material data gets recreated every frame on baking
    m_materialData.reset();
  }

  void TerrainBaker::BakedTexture::onFrameEnd(Rc<DxvkContext> ctx) {

    auto releaseTexture = [&](Resources::Resource& texture) {
      if (!texture.isValid()) {
        return;
      }

      RtxTextureManager& textureManager = ctx->getCommonObjects()->getTextureManager();

      // WAR (REMIX-1557) to force release terrain texture reference from texture cache since it doesn't do it automatically resulting in a leak
      TextureRef textureRef = TextureRef(texture.view);
      texture.reset();
      textureManager.releaseTexture(textureRef);
    };

    // Retain textures when baking is disabled as they are not being refreshed and can still be used
    if (!debugDisableBaking()) {
      if (numFramesToRetain > 0) {
        numFramesToRetain--;
      }
    }

    // Release the texture if it has not been baked to recently
    if (numFramesToRetain == 0) {
      releaseTexture(texture);
    }
  }

  void TerrainBaker::updateTextureFormat(const DxvkContextState& dxvkCtxState) {
    DxvkRenderTargets currentRenderTargets = dxvkCtxState.om.renderTargets;

    VkFormat terrainRtColorFormat = currentRenderTargets.color[0].view->image()->info().format;
    VkFormat terrainSrgbColorFormat = TextureUtils::toSRGB(terrainRtColorFormat);

    // RT shaders expect the textures in sRGB format but but as linear targets
    if (terrainRtColorFormat == terrainSrgbColorFormat) {
      ONCE(Logger::warn(str::format("[RTX Terrain Baker] Terrain render target is of sRGB format ", terrainRtColorFormat, ". Instead, it is expected to be of linear format.")));
    }
  }

  void TerrainBaker::clearMaterialTexture(Rc<DxvkContext> ctx, ReplacementMaterialTextureType::Enum textureType) {
    Resources::Resource& texture = m_materialTextures[textureType].texture;

    VkClearColorValue clear = { 0.f, 0.f, 0.f, 0.f };

    VkImageSubresourceRange subRange = {};
    subRange.layerCount = 1;
    subRange.levelCount = 1;
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    ctx->clearColorImage(texture.image, clear, subRange);
  }

  TerrainBaker::AxisAlignedBoundingBoxLink::AxisAlignedBoundingBoxLink(const DrawCallState& drawCallState)
    : aabbObjectSpace(drawCallState.getGeometryData().boundingBox)
    , objectToWorld(drawCallState.getTransformData().objectToWorld) {
  }

  AxisAlignedBoundingBox TerrainBaker::AxisAlignedBoundingBoxLink::calculateAABBInWorldSpace() {
    AxisAlignedBoundingBox aabb;
    aabb.minPos = (objectToWorld * Vector4(aabbObjectSpace.minPos, 1.f)).xyz();
    aabb.maxPos = (objectToWorld * Vector4(aabbObjectSpace.maxPos, 1.f)).xyz();
    return aabb;
  }

  bool TerrainBaker::needsTerrainBaking() {
    return enableBaking() && RtxOptions::Get()->terrainTextures().size() > 0;
  }

  void TerrainBaker::onFrameBegin(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState) {
    Resources& resourceManager = ctx->getResourceManager();
    RtxTextureManager& textureManger = ctx->getCommonObjects()->getTextureManager();

    // Force material data update every frame to pick up any material parameter changes
    m_needsMaterialDataUpdate = true;

    updateTextureFormat(dxvkCtxState);
    calculateBakingParameters(ctx, dxvkCtxState);

    // Clear terrain textures
    if (clearTerrainBeforeBaking() && !debugDisableBaking()) {
      for (uint32_t i = 0; i < ReplacementMaterialTextureType::Count; i++) {
        if (m_materialTextures[i].texture.isValid()) {
          clearMaterialTexture(ctx, static_cast<ReplacementMaterialTextureType::Enum>(i));
        }
      }
    }
  }

  void TerrainBaker::registerTerrainMesh(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState, const DrawCallState& drawCallState) {
    const uint32_t currentFrameIndex = ctx->getDevice()->getCurrentFrameId();

    // This is the first call in a frame, set up baking state for the new frame
    if (m_bakingParams.frameIndex != currentFrameIndex) {
      onFrameBegin(ctx, dxvkCtxState);
    }

    if (cascadeMap.useTerrainBBOX()) { 
      m_terrainMeshBBOXes.emplace_back(AxisAlignedBoundingBoxLink(drawCallState));
    }
  }

  void TerrainBaker::calculateCascadeMapResolution(const Rc<DxvkDevice>& device) {
    // ToDo: switch to using vkGetPhysicalDeviceImageFormatProperties which may allow larger dimensions for a given image config
    const VkPhysicalDeviceLimits& limits = device->adapter()->deviceProperties().limits;
    const uint32_t maxDimension = limits.maxImageDimension2D;

    m_bakingParams.cascadeLevelResolution = VkExtent2D { cascadeMap.levelResolution(), cascadeMap.levelResolution() };

    // Calculate cascade map resolution
    m_bakingParams.cascadeMapResolution.width = m_bakingParams.cascadeMapSize.x * m_bakingParams.cascadeLevelResolution.width;
    m_bakingParams.cascadeMapResolution.height = m_bakingParams.cascadeMapSize.y * m_bakingParams.cascadeLevelResolution.height;

    // Ensure the texture resolution fits within device limits
    if (m_bakingParams.cascadeMapResolution.width > maxDimension || m_bakingParams.cascadeMapResolution.height > maxDimension) {
      const float2 downscale = {
        static_cast<float>(maxDimension) / m_bakingParams.cascadeMapResolution.width,
        static_cast<float>(maxDimension) / m_bakingParams.cascadeMapResolution.height };

      VkExtent2D prevCascadeMapResolution = m_bakingParams.cascadeMapResolution;

      m_bakingParams.cascadeLevelResolution.width = static_cast<uint32_t>(floor(downscale.x * m_bakingParams.cascadeMapResolution.width) / m_bakingParams.cascadeMapSize.x);
      m_bakingParams.cascadeLevelResolution.height = static_cast<uint32_t>(floor(downscale.y * m_bakingParams.cascadeMapResolution.height) / m_bakingParams.cascadeMapSize.y);

      m_bakingParams.cascadeMapResolution.width = m_bakingParams.cascadeLevelResolution.width * m_bakingParams.cascadeMapSize.x;
      m_bakingParams.cascadeMapResolution.height = m_bakingParams.cascadeLevelResolution.height * m_bakingParams.cascadeMapSize.y;

      ONCE(Logger::warn(str::format("[RTX Terrain Baker] Requested terrain cascade map resolution {", prevCascadeMapResolution.width, ", ", prevCascadeMapResolution.height, "} is outside the device limits {", maxDimension, ", ", maxDimension, "}. Reducing the cascade map resolution to {", m_bakingParams.cascadeMapResolution.width, ", ", m_bakingParams.cascadeMapResolution.height, "}.")));
    }
  }

  void TerrainBaker::calculateBakingParameters(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState) {

    SceneManager& sceneManager = ctx->getSceneManager();
    Resources& resourceManager = ctx->getResourceManager();
    const RtCamera& camera = sceneManager.getCamera();
    const uint32_t currentFrameIndex = ctx->getDevice()->getCurrentFrameId();
    const float metersToWorldUnitScale = RtxOptions::Get()->getMeterToWorldUnitScale();

    m_bakingParams.frameIndex = currentFrameIndex;

    const bool terrainBBOXIsValid = m_terrainBBOXFrameIndex == currentFrameIndex - 1;
    const float epsilon = 0.01f;      // Epsilon to ensure distances are greater or equal

    const float terrainHeight =
      terrainBBOXIsValid
      ? SceneManager::worldToSceneOrientedVector(m_bakedTerrainBBOX.maxPos - m_bakedTerrainBBOX.minPos).z
      : metersToWorldUnitScale * cascadeMap.defaultHeight();

    const float cameraRelativeTerrainHeight =
      terrainBBOXIsValid
      ? SceneManager::worldToSceneOrientedVector(m_bakedTerrainBBOX.maxPos - camera.getPosition()).z
      : metersToWorldUnitScale * cascadeMap.defaultHeight() / 2; // Assume camera is in the middle of terrain's height span

    // Constants set to what makes generally should make sense
    const float zNear = 0.01f;
    const float zFar = terrainHeight * (1 + epsilon) + zNear; // Offset by zNear to match the baking camera position being offset by it 

    // Compute the relative half width of the cascade map around the camera
    float cascadeMapHalfWidth = metersToWorldUnitScale * cascadeMap.defaultHalfWidth();
    if (terrainBBOXIsValid) {
      // Add offset for all terrain samples to be within the baked terrain texture
      const float halfTexelOffset = 10;       // ToDo: calculate an exact value

      // Compute bbox relative to the camera
      AxisAlignedBoundingBox cameraRelativeTerrainBBOX = {
        m_bakedTerrainBBOX.minPos - camera.getPosition() - halfTexelOffset,
        m_bakedTerrainBBOX.maxPos - camera.getPosition() + halfTexelOffset 
      };

      // Convert the bbox to scene space
      cameraRelativeTerrainBBOX.minPos = SceneManager::worldToSceneOrientedVector(cameraRelativeTerrainBBOX.minPos);
      cameraRelativeTerrainBBOX.maxPos = SceneManager::worldToSceneOrientedVector(cameraRelativeTerrainBBOX.maxPos);

      // Calculate a half width of a cascade map around camera that covers the terrain's BBOX
      cascadeMapHalfWidth =
        std::max(std::max(abs(cameraRelativeTerrainBBOX.maxPos.x), abs(cameraRelativeTerrainBBOX.minPos.x)),
                 std::max(abs(cameraRelativeTerrainBBOX.maxPos.y), abs(cameraRelativeTerrainBBOX.minPos.y)));
    }

    // Construct a scene oriented view
    Matrix4 sceneView;
    {
      const Vector3 up = SceneManager::getSceneUp();
      const Vector3 forward = SceneManager::getSceneForward();
      const Vector3 right = SceneManager::calculateSceneRight();

      // Set baking camera position just above the terrain
      // Offset by zNear so that zNear doesn't clip the terrain
      // Offset by epsilon so that it doesn't clip top of the terrain
      const Vector3 bakingCameraPosition = cameraRelativeTerrainHeight >= 0.f
        ? camera.getPosition() + (cameraRelativeTerrainHeight * (1 + epsilon) + zNear) * up
        : camera.getPosition() + (cameraRelativeTerrainHeight * (1 - epsilon) - zNear) * up;

      const Vector3 translation = Vector3(
        dot(right, -bakingCameraPosition),
        dot(forward, -bakingCameraPosition),
        dot(up, -bakingCameraPosition));

      sceneView[0] = Vector4(right.x, forward.x, up.x, 0.f);
      sceneView[1] = Vector4(right.y, forward.y, up.y, 0.f);
      sceneView[2] = Vector4(right.z, forward.z, up.z, 0.f);
      sceneView[3] = Vector4(translation.x, translation.y, translation.z, 1.f);
    }

    m_bakingParams.sceneView = sceneView;
    m_bakingParams.inverseSceneView = inverse(sceneView);

    // Number of cascades required to cover the whole bbox
    const uint32_t numRequiredCascades = 
      1 + static_cast<uint32_t>(ceil(log2(std::max(1.f, cascadeMapHalfWidth / (metersToWorldUnitScale * cascadeMap.levelHalfWidth())))));

    // Number of cascades actually used
    m_bakingParams.numCascades = std::min(cascadeMap.maxLevels(), numRequiredCascades);

    // If there isn't enough cascades to cover the terrain radius, expand the last cascade to cover the cascade map's span
    const bool isLastCascadeExpanded = m_bakingParams.numCascades != numRequiredCascades;
    m_bakingParams.lastCascadeScale = 1.f;

    m_bakingParams.cascadeMapSize.x = static_cast<uint32_t>(ceilf(sqrtf(static_cast<float>(m_bakingParams.numCascades))));
    m_bakingParams.cascadeMapSize.y = static_cast<uint32_t>(ceilf(static_cast<float>(m_bakingParams.numCascades) / m_bakingParams.cascadeMapSize.x));

    m_bakingParams.bakingCameraOrthoProjection.resize(m_bakingParams.numCascades);

    // Calculate cascade map resolution
    calculateCascadeMapResolution(ctx->getDevice());

    const float2 float2CascadeLevelResolution = float2 {
      static_cast<float>(m_bakingParams.cascadeLevelResolution.width),
      static_cast<float>(m_bakingParams.cascadeLevelResolution.height)
    };

    // Calculate params for each cascade level.
    // The levels are tiled left to right top to bottom in the combined render target texture
    for (uint32_t iCascade = 0; iCascade < m_bakingParams.numCascades; iCascade++) {

      Vector2i cascade2DIndex;
      cascade2DIndex.y = iCascade / m_bakingParams.cascadeMapSize.x;
      cascade2DIndex.x = iCascade - cascade2DIndex.y * m_bakingParams.cascadeMapSize.x;

      // Set viewport which maps clip space <-1, 1> to screen space <0, resolution>.
      // Accounts for inverted y coordinate in Vulkan
      VkViewport viewport = VkViewport {
        cascade2DIndex.x * float2CascadeLevelResolution.x,
        (cascade2DIndex.y + 1) * float2CascadeLevelResolution.y,
        float2CascadeLevelResolution.x,
        -float2CascadeLevelResolution.y,
        0.f, 1.f
      };

      VkOffset2D cascadeOffset = VkOffset2D {
        static_cast<int>(cascade2DIndex.x * m_bakingParams.cascadeLevelResolution.width),
        static_cast<int>(cascade2DIndex.y * m_bakingParams.cascadeLevelResolution.height) };

      // Set scissor window which clips the screen space
      VkRect2D scissor = VkRect2D { cascadeOffset, m_bakingParams.cascadeLevelResolution };

      // Half width of the cascade level
      float halfWidth = metersToWorldUnitScale * cascadeMap.levelHalfWidth() * pow(2, iCascade);

      // Expand the last cascade level if necessary
      const bool isLastCascade = iCascade == m_bakingParams.numCascades - 1;
      if (isLastCascade && isLastCascadeExpanded && cascadeMap.expandLastCascade()) {
        // Note: 1st cascade is naturally expanded by matching the projection to the expanded range, rather than applying 
        // expansion scale if it is to be expanded. But for pedantic purposes we set the scale to 1 here anyway
        m_bakingParams.lastCascadeScale = iCascade > 0 ? cascadeMapHalfWidth / halfWidth : 1.f;
        halfWidth = cascadeMapHalfWidth;
      }

      // Setup orthographic projection top-down that maps <-halfWidth, halfWidth> around camera to <0, 1> in clip space
      float4x4& newProjection = *reinterpret_cast<float4x4*>(&m_bakingParams.bakingCameraOrthoProjection[iCascade]);
      newProjection.SetupByOrthoProjection(-halfWidth, halfWidth, -halfWidth, halfWidth, zNear, zFar);

      if (iCascade == 0) {
        // Convert from clip space <-1, 1> to <0, 1> and flip y coordinate for Vulkan
        const Matrix4 textureOffset = Matrix4(Vector4(.5f, 0, 0, 0),
                                              Vector4(0, -.5f, 0, 0),
                                              Vector4(0, 0, 1, 0),
                                              Vector4(.5f, .5f, 0, 1));

        m_bakingParams.viewToCascade0TextureSpace = textureOffset * m_bakingParams.bakingCameraOrthoProjection[iCascade] * sceneView * camera.getViewToWorld();
      }
    }
  }
}