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

#include "rtx_context.h"

namespace dxvk {

  class TerrainBaker {
  public:
    TerrainBaker() { }

    bool bakeDrawCall(Rc<RtxContext> rtxContext, const DxvkContextState& dxvkCtxState,
                      DxvkRaytracingInstanceState& rtState, const DrawParameters& params,
                      const DrawCallState& drawCallState, Matrix4& textureTransformOut);
    TerrainArgs getTerrainArgs() const;

    void onFrameEnd(const uint32_t currentFrameIndex);

    void showImguiSettings() const;

    //
    // RTX OPTIONS
    
    friend class ImGUI; // <-- we want to modify these values directly.

    // Note: call needsTerrainBaking() to check if baking is enabled
    RTX_OPTION("rtx.terrainBaker", bool, enableBaking, true, "[Experimental] Enables runtime baking of blended terrains from top down (i.e. in an opposite direction of \"rtx.zUp\").\n"
                                                             "It bakes multiple blended albedo terrain textures into a single texture sampled during ray tracing. The system requires \"Terrain Textures\" to contain hashes of the terrain textures to apply.\n"
                                                              "Only use this system if the game renders terrain surfaces with multiple blended surfaces on top of each other (i.e. sand mixed with dirt, grass, snow, etc.).\n"
                                                              "Requirement: the baked terrain surfaces must not be placed vertically in the game world. Horizontal surfaces will have the best image quality. Requires \"rtx.zUp\" to be set properly.");

    RTX_OPTION("rtx.terrainBaker", bool, clearTerrainBeforeBaking, false, "Performs a clear on the terrain texture before it is baked to in a frame.");
    RTX_OPTION("rtx.terrainBaker", bool, debugDisableBaking , false, "Force disables rebaking every frame. Used for debugging only.")
    RTX_OPTION("rtx.terrainBaker", bool, debugDisableBinding, false, "Force disables binding of the baked terrain texture to the terrain meshes. Used for debugging only.");

    // Returns shared enablement composed of multiple enablement inputs
    static bool needsTerrainBaking();

    static struct CascadeMap {
      friend class ImGUI; // <-- we want to modify these values directly.
      friend class TerrainBaker; // <-- we want to modify these values directly.

      RTX_OPTION("rtx.terrainBaker.cascadeMap", bool, useTerrainBBOX, true, "Uses terrain's bounding box to calculate the cascade map's scene footprint.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, defaultHalfWidth, 1000.f, "Cascade map square's default half width around the camera [meters]. Used when the terrain's BBOX couldn't be estimated.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, defaultHeight, 1000.f, "Cascade map baker's camera default height above the in-game camera [meters]. Used when the terrain's BBOX couldn't be estimated.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, levelHalfWidth, 10.f, "First cascade level square's half width around the camera [meters].");
      RTX_OPTION_ENV("rtx.terrainBaker.cascadeMap", uint32_t, maxLevels, 8, "RTX_TERRAIN_BAKER_MAX_CASCADE_LEVELS", "Max number of cascade levels.");
      RTX_OPTION_ENV("rtx.terrainBaker.cascadeMap", uint32_t, levelResolution, 4096, "RTX_TERRAIN_BAKER_LEVEL_RESOLUTION", "Texture resolution per cascade level.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", bool, expandLastCascade, true, "Expands the last cascade's footprint to cover the whole cascade map. This ensures all terrain surface has valid baked texture data to sample from across the cascade map's range even if there isn't enough cascades generated (due to the current settings or limitations).");
    } cascadeMap;
    
    // RTX OPTIONS

  private:
    struct BakingParameters {
      uint32_t numCascades;
      uint2 cascadeMapSize;
      VkExtent2D cascadeLevelResolution;
      VkExtent2D cascadeMapResolution;

      Matrix4 sceneView;  // View matrix for a camera looking along scene's forward axis
      Matrix4 inverseSceneView;
      std::vector<Matrix4> bakingCameraOrthoProjection;  // Ortho projections to bake for all cascades
      Matrix4 viewToCascade0TextureSpace; // Matrix transforming viwe coordinates to 1st cascade texture space
      float zNear;
      float zFar;
      float lastCascadeScale; // Scale applied on last cascade's size to expand it to cover the whole cascade map span
      uint32_t frameIndex = kInvalidFrameIndex;  // Frame index for which the parameters have been calculated
    };

    void onFrameBegin(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState);
    void registerTerrainMesh(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState, const DrawCallState& drawCallState);
    void calculateTerrainBBOX(const uint32_t currentFrameIndex);
    void calculateBakingParameters(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState);
    void updateTextureFormat(const DxvkContextState& dxvkCtxState);
    void calculateCascadeMapResolution(const Rc<DxvkDevice>& device);

    BakingParameters m_bakingParams;

    VkFormat m_terrainRtColorFormat = VK_FORMAT_UNDEFINED;

    class AxisAlignedBoundingBoxLink {
    public:
      AxisAlignedBoundingBoxLink(const DrawCallState& drawCallState);
      AxisAlignedBoundingBox calculateAABBInWorldSpace();

    private:
      const AxisAlignedBoundingBox aabbObjectSpace;
      const Matrix4 objectToWorld;
    };

    std::list<AxisAlignedBoundingBoxLink> m_terrainMeshBBOXes;

    // Terrain BBOX found during previous frame
    AxisAlignedBoundingBox m_bakedTerrainBBOX = {
      -Vector3(cascadeMap.defaultHalfWidth()),
      Vector3(cascadeMap.defaultHalfWidth()) };          

    // Frame index for which the terrain BBOX has been calculated
    // // -1 to avoid aliasing on frame 0, since this value is checked for a match a frame later
    uint32_t m_terrainBBOXFrameIndex = kInvalidFrameIndex - 1;  
  };
}