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

#include "../d3d9/d3d9_state.h"
#include "../d3d9/d3d9_spec_constants.h"
#include "../dxso/dxso_util.h"
#include "../../d3d9/d3d9_rtx.h"

namespace dxvk {
  bool TerrainBaker::bakeDrawCall(Rc<RtxContext> ctx,
                                  const DxvkContextState& dxvkCtxState,
                                  DxvkRaytracingInstanceState& rtState,
                                  const DrawParameters& drawParams,
                                  const DrawCallState& drawCallState,
                                  Matrix4& textureTransformOut) {

    ScopedGpuProfileZone(ctx, "Terrain Baker: Bake Draw Call");

    SceneManager& sceneManager = ctx->getSceneManager();
    Resources& resourceManager = ctx->getResourceManager();
    RtxTextureManager& textureManger = ctx->getCommonObjects()->getTextureManager();
    const RtCamera& camera = sceneManager.getCamera();

    if (!debugDisableBinding()) {
      textureTransformOut = m_bakingParams.viewToCascade0TextureSpace;
    }

    if (debugDisableBaking()) {
      return (debugDisableBinding() ? false : true) &&
              resourceManager.getTerrainTexture(ctx).view != nullptr;
    }

    if (drawCallState.usesVertexShader && !D3D9Rtx::useVertexCapture()) {
      ONCE(Logger::warn(str::format("[RTX Terrain Baker] Terrain texture corresponds to a draw call with programmable Vertex Shader usage. Vertex capture must be enabled to support baking of such draw calls. Ignoring the draw call.")));
      return false;
    }

    // Register mesh and preprocess state for baking
    registerTerrainMesh(ctx, dxvkCtxState, drawCallState);

    const Rc<DxvkImageView>& terrainView =
      resourceManager.getTerrainTexture(ctx, textureManger, m_bakingParams.cascadeMapResolution.width, m_bakingParams.cascadeMapResolution.height,
                                        m_terrainRtColorFormat).view;

    if (terrainView == nullptr) {
      ONCE(Logger::err(str::format("[RTX Terrain Baker] Failed to retrieve a terrain texture. Ignoring terrain baking draw call.")));
      return false;
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

    // Save viewports
    const uint32_t prevViewportCount = dxvkCtxState.gp.state.rs.viewportCount();
    const DxvkViewportState prevViewportState = dxvkCtxState.vp;

    // Save previous render targets
    DxvkRenderTargets prevRenderTargets = dxvkCtxState.om.renderTargets;

    // Bind the target terrain texture as render target
    DxvkRenderTargets terrainRt;
    terrainRt.color[0].view = resourceManager.getCompatibleViewForView(terrainView, m_terrainRtColorFormat);
    terrainRt.color[0].layout = VK_IMAGE_LAYOUT_GENERAL;
    ctx->bindRenderTargets(terrainRt);

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
    }

    return true;
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

    if (ImGui::CollapsingHeader("Terrain System [Experimental]", collapsingHeaderClosedFlags)) {
      ImGui::Indent();

      ImGui::Checkbox("Enable Runtime Terrain Baking", &enableBakingObject());
      ImGui::Checkbox("Use Terrain Bounding Box", &cascadeMap.useTerrainBBOXObject());
      ImGui::Checkbox("Clear Terrain Texture Before Terrain Baking", &clearTerrainBeforeBakingObject());
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

  void TerrainBaker::onFrameEnd(const uint32_t currentFrameIndex) {
    // Expects the mesh BBOXes to be calculated by this point
    calculateTerrainBBOX(currentFrameIndex);
  }

  void TerrainBaker::updateTextureFormat(const DxvkContextState& dxvkCtxState) {
    DxvkRenderTargets currentRenderTargets = dxvkCtxState.om.renderTargets;

    m_terrainRtColorFormat = currentRenderTargets.color[0].view->image()->info().format;
    VkFormat terrainSrgbColorFormat = TextureUtils::toSRGB(m_terrainRtColorFormat);

    // RT shaders expect the textures in sRGB format but but as linear targets
    if (m_terrainRtColorFormat == terrainSrgbColorFormat) {
      ONCE(Logger::warn(str::format("[RTX Terrain Baker] Terrain render target is of sRGB format ", m_terrainRtColorFormat, ". Instead, it is expected to be of linear format.")));
    }
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

    updateTextureFormat(dxvkCtxState);
    calculateBakingParameters(ctx, dxvkCtxState);

    if (clearTerrainBeforeBaking() && !debugDisableBaking()) {
      const Rc<DxvkImageView>& view = resourceManager.getTerrainTexture(ctx, textureManger, m_bakingParams.cascadeMapResolution.width, 
                                                                        m_bakingParams.cascadeMapResolution.height, 
                                                                        m_terrainRtColorFormat).view;

      if (view != nullptr) {
        VkClearValue clear;
        clear.color.float32[0] = 0.f;
        clear.color.float32[1] = 0.f;
        clear.color.float32[2] = 0.f;
        clear.color.float32[3] = 0.f;

        ctx->DxvkContext::clearRenderTarget(view, VK_IMAGE_ASPECT_COLOR_BIT, clear);
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

    // Check for terrain format consistency within the frame
    {
      DxvkRenderTargets currentRenderTargets = dxvkCtxState.om.renderTargets;
      VkFormat terrainRtColorFormat = currentRenderTargets.color[0].view->image()->info().format;

      if (terrainRtColorFormat != m_terrainRtColorFormat) {
        ONCE(Logger::warn(str::format("[RTX Terrain Baker] Terrain draw call's render target format is ", terrainRtColorFormat, " which is different from the first seen and accepted format ", m_terrainRtColorFormat, ". Disregarding  the format change.")));
      }
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

      // Offset by zNear so that zNear doesn't clip the terrain
      // Offset by epsilon so that it doesn't clip top of the terrain
      const Vector3 bakingCameraPosition = camera.getPosition() + (cameraRelativeTerrainHeight * (1 + epsilon) + zNear) * up;

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