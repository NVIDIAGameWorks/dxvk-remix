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

#ifndef REMIX_C_H_
#define REMIX_C_H_

#include <stdint.h>
#include <windows.h>


// __stdcall convention
#define REMIXAPI_CALL __stdcall
#define REMIXAPI_PTR  REMIXAPI_CALL

#ifdef REMIX_LIBRARY_EXPORTS
  #define REMIXAPI __declspec(dllexport)
#else
  #define REMIXAPI __declspec(dllimport)
#endif // REMIX_LIBRARY_EXPORTS


#define REMIXAPI_VERSION_MAKE(major, minor, patch) ( \
    (((uint64_t)(major)) << 48) | \
    (((uint64_t)(minor)) << 16) | \
    (((uint64_t)(patch))      ) )
#define REMIXAPI_VERSION_GET_MAJOR(version) (((uint64_t)(version) >> 48) & (uint64_t)0xFFFF)
#define REMIXAPI_VERSION_GET_MINOR(version) (((uint64_t)(version) >> 16) & (uint64_t)0xFFFFFFFF)
#define REMIXAPI_VERSION_GET_PATCH(version) (((uint64_t)(version)      ) & (uint64_t)0xFFFF)

#define REMIXAPI_VERSION_MAJOR 0
#define REMIXAPI_VERSION_MINOR 2
#define REMIXAPI_VERSION_PATCH 0


// External
typedef struct IDirect3D9Ex       IDirect3D9Ex;
typedef struct IDirect3DDevice9Ex IDirect3DDevice9Ex;
typedef struct IDirect3DSurface9  IDirect3DSurface9;


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

  typedef enum remixapi_StructType {
    REMIXAPI_STRUCT_TYPE_NONE                                 = 0,
    REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO              = 1,
    REMIXAPI_STRUCT_TYPE_MATERIAL_INFO                        = 2,
    REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_PORTAL_EXT             = 3,
    REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT        = 4,
    REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT             = 5,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO                           = 6,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT               = 7,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_CYLINDER_EXT              = 8,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT                  = 9,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT                  = 10,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT                = 11,
    REMIXAPI_STRUCT_TYPE_MESH_INFO                            = 12,
    REMIXAPI_STRUCT_TYPE_INSTANCE_INFO                        = 13,
    REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT    = 14,
    REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT              = 15,
    REMIXAPI_STRUCT_TYPE_CAMERA_INFO                          = 16,
    REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT        = 17,
    REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT  = 18,
    REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT     = 19,
    REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT                  = 20,
  } remixapi_StructType;

  typedef enum remixapi_ErrorCode {
    REMIXAPI_ERROR_CODE_SUCCESS                           = 0,
    REMIXAPI_ERROR_CODE_GENERAL_FAILURE                   = 1,
    // WinAPI's LoadLibrary has failed
    REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE              = 2,
    REMIXAPI_ERROR_CODE_WRONG_ARGUMENTS                   = 3,
    // Couldn't find 'remixInitialize' function in the .dll
    REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE          = 4,
    // CreateD3D9 / RegisterD3D9Device can be called only once
    REMIXAPI_ERROR_CODE_ALREADY_EXISTS                    = 5,
    // RegisterD3D9Device requires the device that was created with IDirect3DDevice9Ex, returned by CreateD3D9
    REMIXAPI_ERROR_CODE_REGISTERING_NON_REMIX_D3D9_DEVICE = 6,
    // RegisterD3D9Device was not called
    REMIXAPI_ERROR_CODE_REMIX_DEVICE_WAS_NOT_REGISTERED   = 7,
    REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION              = 8,
  } remixapi_ErrorCode;

  typedef uint32_t remixapi_Bool;

  typedef struct remixapi_Float2D {
    float x;
    float y;
    float z;
  } remixapi_Float2D;

  typedef struct remixapi_Float3D {
    float x;
    float y;
    float z;
  } remixapi_Float3D;

  typedef struct remixapi_Float4D {
    float x;
    float y;
    float z;
    float w;
  } remixapi_Float4D;

  typedef struct remixapi_Transform {
    float matrix[3][4];
  } remixapi_Transform;

  typedef struct remixapi_MaterialHandle_T* remixapi_MaterialHandle;
  typedef struct remixapi_MeshHandle_T* remixapi_MeshHandle;
  typedef struct remixapi_LightHandle_T* remixapi_LightHandle;

  typedef const wchar_t* remixapi_Path;



  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_Shutdown)();



  typedef struct remixapi_MaterialInfoOpaqueEXT {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_Path       roughnessTexture;
    remixapi_Path       metallicTexture;
    float               anisotropy;
    remixapi_Float3D    albedoConstant;
    float               opacityConstant;
    float               roughnessConstant;
    float               metallicConstant;
    remixapi_Bool       thinFilmThickness_hasvalue;
    float               thinFilmThickness_value;
    remixapi_Bool       alphaIsThinFilmThickness;
    remixapi_Path       heightTexture;
    float               heightTextureStrength;
    // If true, InstanceInfoBlendEXT is used as a source for alpha state
    remixapi_Bool       useDrawCallAlphaState;
    remixapi_Bool       blendType_hasvalue;
    int                 blendType_value;
    remixapi_Bool       invertedBlend;
    int                 alphaTestType;
    uint8_t             alphaReferenceValue;
  } remixapi_MaterialInfoOpaqueEXT;

  // Valid only if remixapi_MaterialInfo contains remixapi_MaterialInfoOpaqueEXT in pNext chain
  typedef struct remixapi_MaterialInfoOpaqueSubsurfaceEXT {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_Path       subsurfaceTransmittanceTexture;
    remixapi_Path       subsurfaceThicknessTexture;
    remixapi_Path       subsurfaceSingleScatteringAlbedoTexture;
    remixapi_Float3D    subsurfaceTransmittanceColor;
    float               subsurfaceMeasurementDistance;
    remixapi_Float3D    subsurfaceSingleScatteringAlbedo;
    float               subsurfaceVolumetricAnisotropy;
  } remixapi_MaterialInfoOpaqueSubsurfaceEXT;

  typedef struct remixapi_MaterialInfoTranslucentEXT {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_Path       transmittanceTexture;
    float               refractiveIndex;
    remixapi_Float3D    transmittanceColor;
    float               transmittanceMeasurementDistance;
    remixapi_Bool       thinWallThickness_hasvalue;
    float               thinWallThickness_value;
    remixapi_Bool       useDiffuseLayer;
  } remixapi_MaterialInfoTranslucentEXT;

  typedef struct remixapi_MaterialInfoPortalEXT {
    remixapi_StructType sType;
    void*               pNext;
    uint8_t             rayPortalIndex;
    float               rotationSpeed;
  } remixapi_MaterialInfoPortalEXT;

  typedef struct remixapi_MaterialInfo {
    remixapi_StructType sType;
    void*               pNext;
    uint64_t            hash;
    remixapi_Path       albedoTexture;
    remixapi_Path       normalTexture;
    remixapi_Path       tangentTexture;
    remixapi_Path       emissiveTexture;
    float               emissiveIntensity;
    remixapi_Float3D    emissiveColorConstant;
    uint8_t             spriteSheetRow;
    uint8_t             spriteSheetCol;
    uint8_t             spriteSheetFps;
    uint8_t             filterMode;
    uint8_t             wrapModeU;
    uint8_t             wrapModeV;
  } remixapi_MaterialInfo;


  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_CreateMaterial)(
    const remixapi_MaterialInfo*  info,
    remixapi_MaterialHandle*      out_handle);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_DestroyMaterial)(
    remixapi_MaterialHandle       handle);

  typedef struct remixapi_HardcodedVertex {
    float    position[3];
    float    normal[3];
    float    texcoord[2];
    uint32_t color;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    uint32_t _pad3;
    uint32_t _pad4;
    uint32_t _pad5;
    uint32_t _pad6;
  } remixapi_HardcodedVertex;

  typedef struct remixapi_MeshInfoSkinning {
    uint32_t                        bonesPerVertex;
    // Each tuple of 'bonesPerVertex' float-s defines a vertex.
    // I.e. the size must be (bonesPerVertex * vertexCount).
    const float*                    blendWeights_values;
    uint32_t                        blendWeights_count;
    // Each tuple of 'bonesPerVertex' uint32_t-s defines a vertex.
    // I.e. the size must be (bonesPerVertex * vertexCount).
    const uint32_t*                 blendIndices_values;
    uint32_t                        blendIndices_count;
  } remixapi_MeshInfoSkinning;

  typedef struct remixapi_MeshInfoSurfaceTriangles {
    const remixapi_HardcodedVertex* vertices_values;
    uint64_t                        vertices_count;
    const uint32_t*                 indices_values;
    uint64_t                        indices_count;
    remixapi_Bool                   skinning_hasvalue;
    remixapi_MeshInfoSkinning       skinning_value;
    remixapi_MaterialHandle         material;
  } remixapi_MeshInfoSurfaceTriangles;

  typedef struct remixapi_MeshInfo {
    remixapi_StructType                      sType;
    void*                                    pNext;
    uint64_t                                 hash;
    const remixapi_MeshInfoSurfaceTriangles* surfaces_values;
    uint32_t                                 surfaces_count;
  } remixapi_MeshInfo;

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_CreateMesh)(
    const remixapi_MeshInfo*  info,
    remixapi_MeshHandle*      out_handle);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_DestroyMesh)(
    remixapi_MeshHandle       handle);



  typedef enum remixapi_CameraType {
    REMIXAPI_CAMERA_TYPE_WORLD,
    REMIXAPI_CAMERA_TYPE_SKY,
    REMIXAPI_CAMERA_TYPE_VIEW_MODEL,
  } remixapi_CameraType;

  typedef struct remixapi_CameraInfoParameterizedEXT {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_Float3D    position;
    remixapi_Float3D    forward;
    remixapi_Float3D    up;
    remixapi_Float3D    right;
    float               fovYInDegrees;
    float               aspect;
    float               nearPlane;
    float               farPlane;
  } remixapi_CameraInfoParameterizedEXT;

  typedef struct remixapi_CameraInfo {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_CameraType type;
    float               view[4][4];
    float               projection[4][4];
  } remixapi_CameraInfo;

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_SetupCamera)(
    const remixapi_CameraInfo* info);



#define REMIXAPI_INSTANCE_INFO_MAX_BONES_COUNT 256

  typedef struct remixapi_InstanceInfoBoneTransformsEXT {
      remixapi_StructType       sType;
      void*                     pNext;
      const remixapi_Transform* boneTransforms_values;
      uint32_t                  boneTransforms_count;
  } remixapi_InstanceInfoBoneTransformsEXT;

  typedef struct remixapi_InstanceInfoBlendEXT {
    remixapi_StructType sType;
    void*               pNext;
    remixapi_Bool       alphaTestEnabled;
    uint8_t             alphaTestReferenceValue;
    uint32_t            alphaTestCompareOp;
    remixapi_Bool       alphaBlendEnabled;
    uint32_t            srcColorBlendFactor;
    uint32_t            dstColorBlendFactor;
    uint32_t            colorBlendOp;
    uint32_t            textureColorArg1Source;
    uint32_t            textureColorArg2Source;
    uint32_t            textureColorOperation;
    uint32_t            textureAlphaArg1Source;
    uint32_t            textureAlphaArg2Source;
    uint32_t            textureAlphaOperation;
    uint32_t            tFactor;
    remixapi_Bool       isTextureFactorBlend;
  } remixapi_InstanceInfoBlendEXT;

  typedef struct remixapi_InstanceInfoObjectPickingEXT {
    remixapi_StructType            sType;
    void*                          pNext;
    // A value to write into REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING
    uint32_t                       objectPickingValue;
  } remixapi_InstanceInfoObjectPickingEXT;

  typedef enum remixapi_InstanceCategoryBit {
    REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_UI                  = 1 << 0,
    REMIXAPI_INSTANCE_CATEGORY_BIT_WORLD_MATTE               = 1 << 1,
    REMIXAPI_INSTANCE_CATEGORY_BIT_SKY                       = 1 << 2,
    REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE                    = 1 << 3,
    REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_LIGHTS             = 1 << 4,
    REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_ANTI_CULLING       = 1 << 5,
    REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_MOTION_BLUR        = 1 << 6,
    REMIXAPI_INSTANCE_CATEGORY_BIT_IGNORE_OPACITY_MICROMAP   = 1 << 7,
    REMIXAPI_INSTANCE_CATEGORY_BIT_HIDDEN                    = 1 << 8,
    REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE                  = 1 << 9,
    REMIXAPI_INSTANCE_CATEGORY_BIT_BEAM                      = 1 << 10,
    REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_STATIC              = 1 << 11,
    REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_DYNAMIC             = 1 << 12,
    REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_SINGLE_OFFSET       = 1 << 13,
    REMIXAPI_INSTANCE_CATEGORY_BIT_DECAL_NO_OFFSET           = 1 << 14,
    REMIXAPI_INSTANCE_CATEGORY_BIT_ALPHA_BLEND_TO_CUTOUT     = 1 << 15,
    REMIXAPI_INSTANCE_CATEGORY_BIT_TERRAIN                   = 1 << 16,
    REMIXAPI_INSTANCE_CATEGORY_BIT_ANIMATED_WATER            = 1 << 17,
    REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_MODEL = 1 << 18,
    REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_BODY  = 1 << 19,
  } remixapi_InstanceCategoryBit;

  typedef uint32_t remixapi_InstanceCategoryFlags;

  typedef struct remixapi_InstanceInfo {
    remixapi_StructType            sType;
    void*                          pNext;
    remixapi_InstanceCategoryFlags categoryFlags;
    remixapi_MeshHandle            mesh;
    remixapi_Transform             transform;
    remixapi_Bool                  doubleSided;
  } remixapi_InstanceInfo;

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_DrawInstance)(
    const remixapi_InstanceInfo* info);



  typedef struct remixapi_LightInfoLightShaping {
    remixapi_Float3D               primaryAxis;
    float                          coneAngleDegrees;
    float                          coneSoftness;
    float                          focusExponent;
  } remixapi_LightInfoLightShaping;

  typedef struct remixapi_LightInfoSphereEXT {
    remixapi_StructType            sType;
    void*                          pNext;
    remixapi_Float3D               position;
    float                          radius;
    remixapi_Bool                  shaping_hasvalue;
    remixapi_LightInfoLightShaping shaping_value;
  } remixapi_LightInfoSphereEXT;

  typedef struct remixapi_LightInfoRectEXT {
    remixapi_StructType            sType;
    void*                          pNext;
    remixapi_Float3D               position;
    remixapi_Float3D               xAxis;
    float                          xSize;
    remixapi_Float3D               yAxis;
    float                          ySize;
    remixapi_Bool                  shaping_hasvalue;
    remixapi_LightInfoLightShaping shaping_value;
  } remixapi_LightInfoRectEXT;

  typedef struct remixapi_LightInfoDiskEXT {
    remixapi_StructType            sType;
    void*                          pNext;
    remixapi_Float3D               position;
    remixapi_Float3D               xAxis;
    float                          xRadius;
    remixapi_Float3D               yAxis;
    float                          yRadius;
    remixapi_Bool                  shaping_hasvalue;
    remixapi_LightInfoLightShaping shaping_value;
  } remixapi_LightInfoDiskEXT;

  typedef struct remixapi_LightInfoCylinderEXT {
    remixapi_StructType            sType;
    void*                          pNext;
    remixapi_Float3D               position;
    float                          radius;
    remixapi_Float3D               axis;
    float                          axisLength;
  } remixapi_LightInfoCylinderEXT;

  typedef struct remixapi_LightInfoDistantEXT {
    remixapi_StructType             sType;
    void*                           pNext;
    remixapi_Float3D                direction;
    float                           angularDiameterDegrees;
  } remixapi_LightInfoDistantEXT;

  typedef struct remixapi_LightInfoDomeEXT {
    remixapi_StructType             sType;
    void* pNext;
    remixapi_Transform              transform;
    remixapi_Path                   colorTexture;
  } remixapi_LightInfoDomeEXT;

  typedef struct remixapi_LightInfo {
    remixapi_StructType             sType;
    void*                           pNext;
    uint64_t                        hash;
    remixapi_Float3D                radiance;
  } remixapi_LightInfo;

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_CreateLight)(
    const remixapi_LightInfo* info,
    remixapi_LightHandle*     out_handle);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_DestroyLight)(
    remixapi_LightHandle      handle);


  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_DrawLightInstance)(
    remixapi_LightHandle lightHandle);


  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_SetConfigVariable)(
    const char*         key,
    const char*         value);



  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_CreateD3D9)(
    remixapi_Bool       disableSrgbConversionForOutput,
    IDirect3D9Ex**      out_pD3D9);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_RegisterD3D9Device)(
    IDirect3DDevice9Ex* d3d9Device);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_GetExternalSwapchain)(
    uint64_t*           out_vkImage,
    uint64_t*           out_vkSemaphoreRenderingDone,
    uint64_t*           out_vkSemaphoreResumeSemaphore);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_GetVkImage)(
    IDirect3DSurface9*  source,
    uint64_t*           out_vkImage);

  typedef enum remixapi_dxvk_CopyRenderingOutputType {
    REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_FINAL_COLOR = 0,
    REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_DEPTH = 1,
    REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_NORMALS = 2,
    REMIXAPI_DXVK_COPY_RENDERING_OUTPUT_TYPE_OBJECT_PICKING = 3,
  } remixapi_dxvk_CopyRenderingOutputType;

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_CopyRenderingOutput)(
    IDirect3DSurface9*                    destination,
    remixapi_dxvk_CopyRenderingOutputType type);

  typedef remixapi_ErrorCode(REMIXAPI_PTR* PFN_remixapi_dxvk_SetDefaultOutput)(
    remixapi_dxvk_CopyRenderingOutputType type,
    const remixapi_Float4D& color);


  typedef struct remixapi_InitializeLibraryInfo {
    remixapi_StructType sType;
    void*               pNext;
    uint64_t            version;
  } remixapi_InitializeLibraryInfo;

  typedef struct remixapi_Interface {
    PFN_remixapi_Shutdown                  Shutdown;
    PFN_remixapi_CreateMaterial            CreateMaterial;
    PFN_remixapi_DestroyMaterial           DestroyMaterial;
    PFN_remixapi_CreateMesh                CreateMesh;
    PFN_remixapi_DestroyMesh               DestroyMesh;
    PFN_remixapi_SetupCamera               SetupCamera;
    PFN_remixapi_DrawInstance              DrawInstance;
    PFN_remixapi_CreateLight               CreateLight;
    PFN_remixapi_DestroyLight              DestroyLight;
    PFN_remixapi_DrawLightInstance         DrawLightInstance;
    PFN_remixapi_SetConfigVariable         SetConfigVariable;
    // DXVK interoperability
    PFN_remixapi_dxvk_CreateD3D9           dxvk_CreateD3D9;
    PFN_remixapi_dxvk_RegisterD3D9Device   dxvk_RegisterD3D9Device;
    PFN_remixapi_dxvk_GetExternalSwapchain dxvk_GetExternalSwapchain;
    PFN_remixapi_dxvk_GetVkImage           dxvk_GetVkImage;
    PFN_remixapi_dxvk_CopyRenderingOutput  dxvk_CopyRenderingOutput;
    PFN_remixapi_dxvk_SetDefaultOutput     dxvk_SetDefaultOutput;
  } remixapi_Interface;

  REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(
    const remixapi_InitializeLibraryInfo* info,
    remixapi_Interface*                   out_result);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // REMIX_C_H_
