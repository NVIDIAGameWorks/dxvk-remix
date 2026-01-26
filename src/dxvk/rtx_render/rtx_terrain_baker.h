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
#include "rtx_geometry_utils.h"
#include "rtx_resources.h"
#include "rtx_mipmap.h"

namespace dxvk {

  class TerrainBaker {
  public:
    TerrainBaker() { }
    ~TerrainBaker() { }

    bool bakeDrawCall(Rc<RtxContext> rtxContext, const DxvkContextState& dxvkCtxState,
                      DxvkRaytracingInstanceState& rtState, const DrawParameters& params,
                      const DrawCallState& drawCallState, OpaqueMaterialData* replacementMaterial,
                      Matrix4& textureTransformOut);
    TerrainArgs getTerrainArgs() const;

    void onFrameEnd(Rc<DxvkContext> ctx);
    void prepareSceneData(Rc<RtxContext> ctx);

    const RtxMipmap::Resource& getTerrainTexture(ReplacementMaterialTextureType::Enum textureType) const;
    const MaterialData* getMaterialData() const;
    const Rc<DxvkSampler>& getTerrainSampler() const;

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
    RTX_OPTION("rtx.terrainBaker", bool, disableBackFaceCulling, false, "Disables back-face culling for baked terrain instances. When enabled, all terrain will render as double-sided.");

    // Returns shared enablement composed of multiple enablement inputs
    static bool needsTerrainBaking();

    struct Material {
      friend class ImGUI; // <-- we want to modify these values directly.
      friend class TerrainBaker; // <-- we want to modify these values directly.

      RTX_OPTION("rtx.terrainBaker.material", bool, replacementSupportInPS, true, 
                 "Enables reading of secondary PBR replacement textures in pixel shaders when supported.\n"
                 "Current support is limitted to fixed function pipelines and programmable shaders with Shader Model 1.0.\n"
                 "When set to false or unsupported, an extra compute shader is used to preproces the secondary textures to make them compatible at an expense of performance and quality instead.\n"
                 "Requires \"rtx.terrainBaker.material.replacementSupportInPS_fixedFunction = True\" to apply for draw calls with fixed function graphics pipeline.\n"
                 "Requires \"rtx.terrainBaker.material.replacementSupportInPS_programmableShaders = True\" to apply for draw calls with programmable graphics pipeline.");
      RTX_OPTION("rtx.terrainBaker.material", bool, replacementSupportInPS_fixedFunction, true, 
                 "Enables reading of secondary PBR replacement textures in pixel shaders for games with fixed function graphics pipelines.\n"
                 "When set to false, an extra compute shader is used to preproces the secondary textures to make them compatible at an expense of performance and quality instead.\n"
                 "This parameter must be set at launch to apply.");
      RTX_OPTION_ENV("rtx.terrainBaker.material", bool, replacementSupportInPS_programmableShaders, true, "RTX_TERRAIN_BAKER_PS_REPLACEMENT_SUPPORT_IN_PROGRAMMABLE_SHADERS",
                 "[Experimental] Enables reading of secondary PBR replacement textures in pixel shaders for games with programmable graphics pipelines.\""
                 "When set to false, an extra compute shader is used to preproces the secondary textures to make them compatible at an expense of performance and quality instead.\n"
                 "This parameter must be set at launch to apply. The current support for this is limitted to draw calls with programmable shaders with Shader Model 1.0 only.\n"
                 "Draw calls with Shader Model 2.0+ will use the preprocessing compute pass.");
      RTX_OPTION("rtx.terrainBaker.material", bool, bakeReplacementMaterials, true, "Enables baking of replacement materials when they are present.");
      // ToDo disable by default
      RTX_OPTION_ENV("rtx.terrainBaker.material", bool, bakeSecondaryPBRTextures, true, "RTX_TERRAIN_BAKER_BAKE_SECONDARY_PBR_TEXTURES", 
                     "Enables baking of secondary textures in replacement materials when they are present.\n"
                     "Secondary textures are all PBR textures except for albedoOpacity. So that includes normal, roughness, etc.");
      RTX_OPTION("rtx.terrainBaker.material", uint32_t, maxResolutionToUseForReplacementMaterials, 8192, 
                 "Max resolution to use for preprocessing and baking of input replacement material textures other than color opacity which is used as is.\n"
                 "Applies only to a case when a preprocessing compute shader is used to support baking of secondary PBR materials.\n"
                 "Replacement materials need to be preprocessed prior to baking them and limitting the max resolution allows to balance the quality vs performance cost.");

      struct Properties {
        friend class ImGUI;
        friend class TerrainBaker; // <-- we want to modify these values directly.

        RTX_OPTION("rtx.terrainBaker.material.properties", float, roughnessAnisotropy, 0.f, "Roughness anisotropy. Valid range is <-1, 1>, where 0 is isotropic.");
        RTX_OPTION("rtx.terrainBaker.material.properties", float, emissiveIntensity, 0.f, "Emissive intensity.");
        RTX_OPTION("rtx.terrainBaker.material.properties", float, roughnessConstant, 0.7f, "Perceptual roughness constant. Valid range is <0, 1>.");
        RTX_OPTION("rtx.terrainBaker.material.properties", float, metallicConstant, 0.1f, "Metallic constant. Valid range is <0, 1>.");
        RTX_OPTION("rtx.terrainBaker.material.properties", Vector3, emissiveColorConstant, Vector3(0.0f, 0.0f, 0.0f), "Emissive color constant. Should be a color in sRGB colorspace with gamma encoding.");
        RTX_OPTION("rtx.terrainBaker.material.properties", bool, enableEmission, false, "A flag to determine if emission is enabled.");
        RTX_OPTION("rtx.terrainBaker.material.properties", float, displaceInFactor, 1.f,
                   "The max depth and height the baked terrain can support will be larger than the max \n"
                   "of any incoming draw call, which results in a loss of detail. When this is \n"
                   "too low, the displacement will lack detail. When it is too high, the lowest \n"
                   "and highest parts of the POM will flatten out.  This affects both displaceIn \n"
                   "and displaceOut, despite the name.");
      };
    };

    static struct CascadeMap {
      friend class ImGUI; // <-- we want to modify these values directly.
      friend class TerrainBaker; // <-- we want to modify these values directly.

      RTX_OPTION("rtx.terrainBaker.cascadeMap", bool, useTerrainBBOX, true, "Uses terrain's bounding box to calculate the cascade map's scene footprint.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, defaultHalfWidth, 1000.f, "Cascade map square's default half width around the camera [meters]. Used when the terrain's BBOX couldn't be estimated.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, defaultHeight, 1000.f, "Cascade map baker's camera default height above the in-game camera [meters]. Used when the terrain's BBOX couldn't be estimated.");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", float, levelHalfWidth, 10.f, "First cascade level square's half width around the camera [meters].");
      RTX_OPTION_ARGS("rtx.terrainBaker.cascadeMap", uint32_t, maxLevels, 8, "Max number of cascade levels.",
                      args.minValue = 1,
                      args.maxValue = 16,
                      args.environment = "RTX_TERRAIN_BAKER_MAX_CASCADE_LEVELS");
      RTX_OPTION_ARGS("rtx.terrainBaker.cascadeMap", uint32_t, levelResolution, 4096, "Texture resolution per cascade level.",
                      args.minValue = 1,
                      args.maxValue = 32 * 1024,
                      args.environment = "RTX_TERRAIN_BAKER_LEVEL_RESOLUTION");
      RTX_OPTION("rtx.terrainBaker.cascadeMap", bool, expandLastCascade, true, 
                 "Expands the last cascade's footprint to cover the whole cascade map.\n"
                 "This ensures whole terrain surface has valid baked texture data to sample from\n"
                 "even if there isn't enough cascades generated (due to the current settings or limitations).");
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
    bool gatherAndPreprocessReplacementTextures(Rc<RtxContext> ctx, const DrawCallState& drawCallState, OpaqueMaterialData* replacementMaterial, std::vector<RtxGeometryUtils::TextureConversionInfo>& replacementTextures);
    void updateMaterialData(Rc<RtxContext> ctx);
    void onFrameBegin(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState);
    void registerTerrainMesh(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState, const DrawCallState& drawCallState);
    void calculateTerrainBBOX(const uint32_t currentFrameIndex);
    void calculateBakingParameters(Rc<RtxContext> ctx, const DxvkContextState& dxvkCtxState);
    void updateTextureFormat(const DxvkContextState& dxvkCtxState);
    void calculateCascadeMapResolution(const Rc<DxvkDevice>& device);
    const RtxMipmap::Resource& getTerrainTexture(Rc<DxvkContext> ctx, RtxTextureManager& textureManager, ReplacementMaterialTextureType::Enum textureType, uint32_t width, uint32_t height);
    void clearMaterialTexture(Rc<DxvkContext> ctx, ReplacementMaterialTextureType::Enum textureType);
    static bool isPSReplacementSupportEnabled(const DrawCallState& drawCallState);
    VkClearColorValue getClearColor(ReplacementMaterialTextureType::Enum textureType);

    BakingParameters m_bakingParams;

    struct TextureKey {
      uint16_t width;
      uint16_t height;
      uint16_t /*ReplacementMaterialTextureType::Enum*/ textureType;
      
      XXH64_hash_t calculateHash() const {
        return XXH3_64bits(this, sizeof(TextureKey));
      }
    };

    static_assert(sizeof(TextureKey) == 6 && "Validate the struct is fully packed and update the static assert check.");

    fast_unordered_cache<Resources::Resource> m_stagingTextureCache;

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
    // -1 to avoid aliasing on frame 0, since this value is checked for a match a frame later
    uint32_t m_terrainBBOXFrameIndex = kInvalidFrameIndex - 1;

    // Since blending of anything but the material textures is not supported 
    // baked terrain uses the first material data properties it sees in a frame
    bool m_hasInitializedMaterialDataThisFrame = false;

    // Made optional since it requires valid explicit parameters specified on construction
    std::optional<MaterialData> m_materialData;

    float m_currFrameMaxDisplaceIn = 0.f;
    float m_prevFrameMaxDisplaceIn = 0.f;

    float m_currFrameMaxDisplaceOut = 0.f;
    float m_prevFrameMaxDisplaceOut = 0.f;

    // Set to true when m_materialData needs to be updated to reflect latest changes.
    bool m_needsMaterialDataUpdate = false;
    
    // Set to true when a button is clicked in the GUI, tracks all incoming uv densities for a frame and changes displaceInFactor to the resulting value.
    bool m_calculatingDisplaceInFactor = false;
    float m_calculatedDisplaceInFactor = 1.0f;
    // UI button click may come in mid frame, so set a transient bool, then start actually calculating it next frame.
    mutable bool m_calculateDisplaceInFactorNextFrame = false;

    struct BakedTexture {
      // Baked texture needs to be retained for at least a frame after it was baked so that 
      // a bound terrain material is consistent (i.e. bounds same texture types) across all draw calls
      // even if a draw does not bake into all PBR textures that all draws combined in a frame do.
      // Note the consistency is only ensured for a frame in which no new texture types have been added.
      // 1: means current frame only
      const uint8_t kNumFramesToRetainBakedTexture = 2;
      
      RtxMipmap::Resource texture;
      uint8_t numFramesToRetain = 0;

      bool isBaked() {
        return numFramesToRetain > 0;
      }

      void markAsBaked() {
        numFramesToRetain = kNumFramesToRetainBakedTexture;
      }

      void onFrameEnd(Rc<DxvkContext> ctx);

    };

    BakedTexture m_materialTextures[ReplacementMaterialTextureType::Count];

    Rc<DxvkSampler> m_terrainSampler;
  };
}